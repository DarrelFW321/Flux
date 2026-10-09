#include "ir/passes.hpp"
#include "common/builtins.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <unordered_set>

namespace ir {
namespace {

// ── Shared machinery ─────────────────────────────────────────────────────────

// When a pass proves `%a` equals an existing `%b`, it deletes `%a` and records
// an alias; later operands are rewritten through the alias table. Because
// regions are visited in program order and SSA values are only used after
// their definition, a single forward walk resolves every use.
struct Aliases {
    std::unordered_map<int, int> map;
    int get(int id) const {
        auto it = map.find(id);
        while (it != map.end()) {
            id = it->second;
            it = map.find(id);
        }
        return id;
    }
    void set(int from, int to) { map[from] = to; }
    void apply(Inst& i) const {
        for (int& a : i.args) a = get(a);
        if (i.for_var >= 0)   i.for_var = get(i.for_var);
        if (i.for_start >= 0) i.for_start = get(i.for_start);
        if (i.for_end >= 0)   i.for_end = get(i.for_end);
    }
};

// A compile-time constant: components in column-major order. Ints and bools
// are stored as doubles (exact for 32-bit values).
struct CVal {
    Type type;
    std::vector<double> v;
};
using ConstMap = std::unordered_map<int, CVal>;

double to_f32(double x) { return static_cast<double>(static_cast<float>(x)); }
double to_i32(long long x) { return static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(x))); }

bool finite(const CVal& c) {
    for (double x : c.v) if (!std::isfinite(x)) return false;
    return true;
}

const CVal* lookup(const ConstMap& cm, int id) {
    auto it = cm.find(id);
    return it == cm.end() ? nullptr : &it->second;
}

bool all_equal(const CVal* c, double k) {
    if (!c) return false;
    for (double x : c->v) if (x != k) return false;
    return true;
}

// Emits instructions materializing `c` into `out`. The outermost instruction
// gets id `top_id`. Returns the id.
int emit_const(std::vector<Inst>& out, Function& f, const CVal& c, int line, int top_id,
               ConstMap* cm, std::unordered_set<int>* scalar_ids, std::unordered_set<int>* canonical,
               const std::string& name = "") {
    auto scalar = [&](double v, int id) {
        Inst k;
        k.op = Op::Const;
        k.type = c.type.scalar();
        k.id = id;
        k.line = line;
        if (k.type.is_float()) k.fconst = v;
        else                   k.iconst = static_cast<long long>(v);
        if (cm) (*cm)[id] = {k.type, {v}};
        if (scalar_ids) scalar_ids->insert(id);
        out.push_back(std::move(k));
        return id;
    };
    if (c.type.is_scalar()) {
        int id = scalar(c.v[0], top_id);
        out.back().name = name;
        return id;
    }
    auto vector = [&](const double* comps, Type t, int id) {
        // Uniform vectors become a splat of one constant; others a construct.
        bool uniform = true;
        for (int k = 1; k < t.rows; ++k) uniform = uniform && comps[k] == comps[0];
        Inst v;
        v.type = t;
        v.id = id;
        v.line = line;
        if (uniform) {
            v.op = Op::Splat;
            v.args = {scalar(comps[0], f.fresh())};
        } else {
            v.op = Op::Construct;
            for (int k = 0; k < t.rows; ++k) v.args.push_back(scalar(comps[k], f.fresh()));
        }
        if (cm) (*cm)[id] = {t, std::vector<double>(comps, comps + t.rows)};
        if (canonical) canonical->insert(id);
        out.push_back(std::move(v));
        return id;
    };
    if (c.type.is_vector()) {
        int id = vector(c.v.data(), c.type, top_id);
        out.back().name = name;
        return id;
    }
    Inst m;
    m.op = Op::Construct;
    m.type = c.type;
    m.id = top_id;
    m.line = line;
    m.name = name;
    for (int col = 0; col < c.type.cols; ++col)
        m.args.push_back(vector(c.v.data() + col * c.type.rows, c.type.column(), f.fresh()));
    if (cm) (*cm)[top_id] = c;
    if (canonical) canonical->insert(top_id);
    out.push_back(std::move(m));
    return top_id;
}

// ── Constant evaluation ──────────────────────────────────────────────────────

std::optional<CVal> eval_builtin(const Inst& i, const std::vector<const CVal*>& a) {
    BuiltinFn fn = static_cast<BuiltinFn>(i.imm);
    Type t = i.type;
    bool is_int = a[0]->type.base == Type::Base::Int;
    size_t n = a[0]->v.size();
    CVal r{t, {}};

    auto unary = [&](double (*g)(double)) {
        for (double x : a[0]->v) r.v.push_back(to_f32(g(x)));
    };
    auto comp = [&](int k, size_t j) { return a[k]->v[a[k]->v.size() == 1 ? 0 : j]; };
    auto dot = [&](const CVal* x, const CVal* y) {
        double s = 0;
        for (size_t j = 0; j < x->v.size(); ++j) s += x->v[j] * y->v[j];
        return s;
    };

    switch (fn) {
        case BuiltinFn::Sin:   unary([](double x) { return std::sin(x); }); break;
        case BuiltinFn::Cos:   unary([](double x) { return std::cos(x); }); break;
        case BuiltinFn::Tan:   unary([](double x) { return std::tan(x); }); break;
        case BuiltinFn::Asin:  unary([](double x) { return std::asin(x); }); break;
        case BuiltinFn::Acos:  unary([](double x) { return std::acos(x); }); break;
        case BuiltinFn::Atan:  unary([](double x) { return std::atan(x); }); break;
        case BuiltinFn::Exp:   unary([](double x) { return std::exp(x); }); break;
        case BuiltinFn::Exp2:  unary([](double x) { return std::exp2(x); }); break;
        case BuiltinFn::Log:   unary([](double x) { return x > 0 ? std::log(x) : NAN; }); break;
        case BuiltinFn::Log2:  unary([](double x) { return x > 0 ? std::log2(x) : NAN; }); break;
        case BuiltinFn::Sqrt:  unary([](double x) { return x >= 0 ? std::sqrt(x) : NAN; }); break;
        case BuiltinFn::InverseSqrt: unary([](double x) { return x > 0 ? 1.0 / std::sqrt(x) : NAN; }); break;
        case BuiltinFn::Floor: unary([](double x) { return std::floor(x); }); break;
        case BuiltinFn::Ceil:  unary([](double x) { return std::ceil(x); }); break;
        case BuiltinFn::Fract: unary([](double x) { return x - std::floor(x); }); break;
        case BuiltinFn::Round: unary([](double x) { return std::nearbyint(x); }); break;   // half to even, like WGSL
        case BuiltinFn::Trunc: unary([](double x) { return std::trunc(x); }); break;
        case BuiltinFn::Radians: unary([](double x) { return x * 0.017453292519943295; }); break;
        case BuiltinFn::Degrees: unary([](double x) { return x * 57.29577951308232; }); break;
        case BuiltinFn::Abs:
            for (double x : a[0]->v) r.v.push_back(is_int ? to_i32(static_cast<long long>(std::fabs(x))) : std::fabs(x));
            break;
        case BuiltinFn::Sign:
            for (double x : a[0]->v) r.v.push_back(x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0);
            break;
        case BuiltinFn::Atan2:
            for (size_t j = 0; j < n; ++j) r.v.push_back(to_f32(std::atan2(comp(0, j), comp(1, j))));
            break;
        case BuiltinFn::Pow:
            for (size_t j = 0; j < n; ++j) {
                double x = comp(0, j), y = comp(1, j);
                if (x < 0 || (x == 0 && y <= 0)) return std::nullopt;   // undefined in GLSL/WGSL
                r.v.push_back(to_f32(std::pow(x, y)));
            }
            break;
        case BuiltinFn::Min:
            for (size_t j = 0; j < n; ++j) r.v.push_back(std::min(comp(0, j), comp(1, j)));
            break;
        case BuiltinFn::Max:
            for (size_t j = 0; j < n; ++j) r.v.push_back(std::max(comp(0, j), comp(1, j)));
            break;
        case BuiltinFn::Clamp:
            for (size_t j = 0; j < n; ++j) {
                if (comp(1, j) > comp(2, j)) return std::nullopt;
                r.v.push_back(std::min(std::max(comp(0, j), comp(1, j)), comp(2, j)));
            }
            break;
        case BuiltinFn::Mix:
            for (size_t j = 0; j < n; ++j) {
                double x = comp(0, j), y = comp(1, j), s = comp(2, j);
                r.v.push_back(to_f32(x * (1.0 - s) + y * s));
            }
            break;
        case BuiltinFn::Step:
            n = a[1]->v.size();
            for (size_t j = 0; j < n; ++j) r.v.push_back(comp(1, j) < comp(0, j) ? 0.0 : 1.0);
            break;
        case BuiltinFn::Smoothstep:
            n = a[2]->v.size();
            for (size_t j = 0; j < n; ++j) {
                double e0 = comp(0, j), e1 = comp(1, j), x = comp(2, j);
                if (e0 >= e1) return std::nullopt;
                double s = std::min(std::max((x - e0) / (e1 - e0), 0.0), 1.0);
                r.v.push_back(to_f32(s * s * (3.0 - 2.0 * s)));
            }
            break;
        case BuiltinFn::Length:
            r.v.push_back(to_f32(std::sqrt(dot(a[0], a[0]))));
            break;
        case BuiltinFn::Distance: {
            double s = 0;
            for (size_t j = 0; j < n; ++j) s += (a[0]->v[j] - a[1]->v[j]) * (a[0]->v[j] - a[1]->v[j]);
            r.v.push_back(to_f32(std::sqrt(s)));
            break;
        }
        case BuiltinFn::Dot:
            r.v.push_back(to_f32(dot(a[0], a[1])));
            break;
        case BuiltinFn::Cross: {
            const auto& x = a[0]->v;
            const auto& y = a[1]->v;
            r.v = {to_f32(x[1] * y[2] - x[2] * y[1]), to_f32(x[2] * y[0] - x[0] * y[2]),
                   to_f32(x[0] * y[1] - x[1] * y[0])};
            break;
        }
        case BuiltinFn::Normalize: {
            double len = std::sqrt(dot(a[0], a[0]));
            if (len == 0) return std::nullopt;
            for (double x : a[0]->v) r.v.push_back(to_f32(x / len));
            break;
        }
        case BuiltinFn::Reflect: {
            double d = dot(a[1], a[0]);
            for (size_t j = 0; j < n; ++j) r.v.push_back(to_f32(a[0]->v[j] - 2.0 * d * a[1]->v[j]));
            break;
        }
        case BuiltinFn::Refract: {
            double d = dot(a[1], a[0]), eta = a[2]->v[0];
            double k = 1.0 - eta * eta * (1.0 - d * d);
            for (size_t j = 0; j < n; ++j)
                r.v.push_back(k < 0 ? 0.0 : to_f32(eta * a[0]->v[j] - (eta * d + std::sqrt(k)) * a[1]->v[j]));
            break;
        }
        case BuiltinFn::Transpose: {
            int dim = a[0]->type.cols;
            r.v.resize(dim * dim);
            for (int c = 0; c < dim; ++c)
                for (int row = 0; row < dim; ++row) r.v[row * dim + c] = a[0]->v[c * dim + row];
            break;
        }
        case BuiltinFn::Determinant: {
            const auto& m = a[0]->v;
            int dim = a[0]->type.cols;
            if (dim == 2) r.v.push_back(to_f32(m[0] * m[3] - m[2] * m[1]));
            else if (dim == 3)
                r.v.push_back(to_f32(m[0] * (m[4] * m[8] - m[7] * m[5]) - m[3] * (m[1] * m[8] - m[7] * m[2]) +
                                     m[6] * (m[1] * m[5] - m[4] * m[2])));
            else return std::nullopt;
            break;
        }
        default:
            return std::nullopt;
    }
    if (!finite(r)) return std::nullopt;
    return r;
}

std::optional<CVal> eval(const Inst& i, const ConstMap& cm) {
    std::vector<const CVal*> a;
    for (int id : i.args) {
        const CVal* c = lookup(cm, id);
        if (!c) {
            // Select only needs its condition... but a value-producing fold needs all operands.
            return std::nullopt;
        }
        a.push_back(c);
    }
    Type t = i.type;
    bool fl = t.base == Type::Base::Float;
    CVal r{t, {}};

    auto zip = [&](auto fn) -> std::optional<CVal> {
        if (a[0]->v.size() != a[1]->v.size()) return std::nullopt;
        for (size_t j = 0; j < a[0]->v.size(); ++j) {
            auto x = fn(a[0]->v[j], a[1]->v[j]);
            if (!x) return std::nullopt;
            r.v.push_back(*x);
        }
        return r;
    };
    using D = std::optional<double>;
    auto arith = [&](double x, double y, Op op) -> D {
        if (fl) {
            switch (op) {
                case Op::Add: return to_f32(x + y);
                case Op::Sub: return to_f32(x - y);
                case Op::Mul: return to_f32(x * y);
                case Op::Div: if (y == 0) return std::nullopt; return to_f32(x / y);
                case Op::Rem: if (y == 0) return std::nullopt; return to_f32(std::fmod(x, y));
                default: return std::nullopt;
            }
        }
        long long p = static_cast<long long>(x), q = static_cast<long long>(y);
        switch (op) {
            case Op::Add: return to_i32(p + q);
            case Op::Sub: return to_i32(p - q);
            case Op::Mul: return to_i32(p * q);
            case Op::Div: if (q == 0 || (p == INT32_MIN && q == -1)) return std::nullopt; return to_i32(p / q);
            case Op::Rem: if (q == 0 || (p == INT32_MIN && q == -1)) return std::nullopt; return to_i32(p % q);
            default: return std::nullopt;
        }
    };

    switch (i.op) {
        case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Rem:
            return zip([&](double x, double y) { return arith(x, y, i.op); });
        case Op::Neg:
            for (double x : a[0]->v) r.v.push_back(fl ? -x : to_i32(-static_cast<long long>(x)));
            return r;
        case Op::Not: return CVal{t, {a[0]->v[0] ? 0.0 : 1.0}};
        case Op::And: return CVal{t, {(a[0]->v[0] && a[1]->v[0]) ? 1.0 : 0.0}};
        case Op::Or:  return CVal{t, {(a[0]->v[0] || a[1]->v[0]) ? 1.0 : 0.0}};
        case Op::Eq:  return CVal{t, {a[0]->v[0] == a[1]->v[0] ? 1.0 : 0.0}};
        case Op::Ne:  return CVal{t, {a[0]->v[0] != a[1]->v[0] ? 1.0 : 0.0}};
        case Op::Lt:  return CVal{t, {a[0]->v[0] <  a[1]->v[0] ? 1.0 : 0.0}};
        case Op::Le:  return CVal{t, {a[0]->v[0] <= a[1]->v[0] ? 1.0 : 0.0}};
        case Op::Gt:  return CVal{t, {a[0]->v[0] >  a[1]->v[0] ? 1.0 : 0.0}};
        case Op::Ge:  return CVal{t, {a[0]->v[0] >= a[1]->v[0] ? 1.0 : 0.0}};
        case Op::Select: return *(a[0]->v[0] ? a[1] : a[2]);
        case Op::Splat:
            r.v.assign(t.components(), a[0]->v[0]);
            return r;
        case Op::Construct:
            for (const CVal* c : a) r.v.insert(r.v.end(), c->v.begin(), c->v.end());
            if (static_cast<int>(r.v.size()) != t.components()) return std::nullopt;
            return r;
        case Op::Extract: {
            const CVal& c = *a[0];
            int rows = c.type.is_matrix() ? c.type.rows : 1;
            r.v.assign(c.v.begin() + i.imm * rows, c.v.begin() + (i.imm + 1) * rows);
            return r;
        }
        case Op::ExtractDyn: {
            long long k = static_cast<long long>(a[1]->v[0]);
            if (k < 0 || k >= static_cast<long long>(a[0]->v.size())) return std::nullopt;
            return CVal{t, {a[0]->v[k]}};
        }
        case Op::Insert: {
            r = *a[0];
            int rows = r.type.is_matrix() ? r.type.rows : 1;
            for (int k = 0; k < rows; ++k) r.v[i.imm * rows + k] = a[1]->v[k];
            return r;
        }
        case Op::Swizzle:
            for (int l : i.lanes) r.v.push_back(a[0]->v[l]);
            return r;
        case Op::Shuffle: {
            std::vector<double> both = a[0]->v;
            both.insert(both.end(), a[1]->v.begin(), a[1]->v.end());
            for (int l : i.lanes) r.v.push_back(both[l]);
            return r;
        }
        case Op::Convert: {
            double x = a[0]->v[0];
            if (t.is_float()) return CVal{t, {to_f32(x)}};
            if (!std::isfinite(x) || x >= 2147483648.0 || x < -2147483648.0) return std::nullopt;
            return CVal{t, {std::trunc(x)}};
        }
        case Op::MatScale:
            for (double x : a[0]->v) r.v.push_back(to_f32(x * a[1]->v[0]));
            return r;
        case Op::MatMul: {
            const CVal& A = *a[0];
            const CVal& B = *a[1];
            if (A.type.is_matrix() && B.type.is_matrix()) {
                int n = A.type.rows;
                r.v.assign(n * n, 0.0);
                for (int col = 0; col < n; ++col)
                    for (int row = 0; row < n; ++row) {
                        double s = 0;
                        for (int k = 0; k < n; ++k) s += A.v[k * n + row] * B.v[col * n + k];
                        r.v[col * n + row] = to_f32(s);
                    }
            } else if (A.type.is_matrix()) {
                int n = A.type.rows;
                for (int row = 0; row < n; ++row) {
                    double s = 0;
                    for (int k = 0; k < n; ++k) s += A.v[k * n + row] * B.v[k];
                    r.v.push_back(to_f32(s));
                }
            } else {
                int n = B.type.rows;
                for (int col = 0; col < n; ++col) {
                    double s = 0;
                    for (int k = 0; k < n; ++k) s += A.v[k] * B.v[col * n + k];
                    r.v.push_back(to_f32(s));
                }
            }
            return r;
        }
        case Op::Builtin: return eval_builtin(i, a);
        default: return std::nullopt;
    }
}

// Region-rebuilding walker: subclasses override `visit` for leaf instructions.
// Nested regions are walked recursively before their owning instruction is
// re-emitted.
template <class Self>
struct Walker {
    Function* f = nullptr;
    Aliases   al;
    int       changes = 0;

    void region(Region& r) {
        std::vector<Inst> out;
        out.reserve(r.insts.size());
        for (Inst& in : r.insts) {
            Inst i = std::move(in);
            al.apply(i);
            if (!i.regions.empty()) {
                static_cast<Self*>(this)->structured(i, out);
                continue;
            }
            static_cast<Self*>(this)->visit(i, out);
        }
        r.insts = std::move(out);
    }

    // Default: walk each sub-region, keep the instruction.
    void structured(Inst& i, std::vector<Inst>& out) {
        for (auto& sub : i.regions) region(sub);
        if (i.cond >= 0) i.cond = al.get(i.cond);
        out.push_back(std::move(i));
    }

    void visit(Inst& i, std::vector<Inst>& out) { out.push_back(std::move(i)); }

    // Value ids are per function: passes reset their tables here.
    void begin_function() {}

    int run(Module& m) {
        for (auto& fn : m.functions) {
            f = &fn;
            al = Aliases{};
            static_cast<Self*>(this)->begin_function();
            region(fn.body);
        }
        return changes;
    }
};

// ── const-fold ───────────────────────────────────────────────────────────────

struct ConstFold : Walker<ConstFold> {
    ConstMap cm;
    std::unordered_set<int> scalar_ids, canonical;

    void begin_function() { cm.clear(); scalar_ids.clear(); canonical.clear(); }

    void visit(Inst& i, std::vector<Inst>& out) {
        if (i.op == Op::Const) {
            cm[i.id] = {i.type, {i.type.is_float() ? i.fconst : static_cast<double>(i.iconst)}};
            scalar_ids.insert(i.id);
            out.push_back(std::move(i));
            return;
        }
        if (i.op == Op::Select) {
            if (const CVal* c = lookup(cm, i.args[0])) {
                al.set(i.id, c->v[0] ? i.args[1] : i.args[2]);
                ++changes;
                return;
            }
        }
        if (i.id < 0 || !is_pure(i.op) || i.op == Op::Arg || i.op == Op::Uniform || i.op == Op::Call) {
            out.push_back(std::move(i));
            return;
        }
        auto v = eval(i, cm);
        if (!v) { out.push_back(std::move(i)); return; }

        // Already in canonical constant form? Then just remember the value.
        bool is_canon = false;
        if (i.op == Op::Splat) is_canon = scalar_ids.count(i.args[0]) > 0;
        if (i.op == Op::Construct) {
            is_canon = true;
            for (int a : i.args)
                is_canon = is_canon && (i.type.is_matrix() ? canonical.count(a) > 0 : scalar_ids.count(a) > 0);
            // A uniform vector is canonically a splat.
            if (is_canon && i.type.is_vector()) {
                bool same = true;
                for (int k = 1; k < i.type.rows; ++k) same = same && v->v[k] == v->v[0];
                if (same) is_canon = false;
            }
        }
        if (is_canon) {
            cm[i.id] = *v;
            canonical.insert(i.id);
            out.push_back(std::move(i));
            return;
        }
        emit_const(out, *f, *v, i.line, i.id, &cm, &scalar_ids, &canonical, i.name);
        ++changes;
    }
};

// ── simplify ─────────────────────────────────────────────────────────────────

struct Simplify : Walker<Simplify> {
    ConstMap cm;
    std::unordered_map<int, Inst> defs;   // leaf definitions, for pattern matching

    void begin_function() { cm.clear(); defs.clear(); }

    const Inst* def(int id) const {
        auto it = defs.find(id);
        return it == defs.end() ? nullptr : &it->second;
    }

    void alias(Inst& i, int to) {
        al.set(i.id, to);
        ++changes;
    }

    void keep(Inst& i, std::vector<Inst>& out) {
        if (i.op == Op::Const) cm[i.id] = {i.type, {i.type.is_float() ? i.fconst : static_cast<double>(i.iconst)}};
        if (i.op == Op::Splat || i.op == Op::Construct) {
            if (auto v = eval(i, cm)) cm[i.id] = *v;
        }
        if (i.id >= 0) {
            Inst copy = i;
            copy.regions.clear();
            defs[i.id] = std::move(copy);
        }
        out.push_back(std::move(i));
    }

    // Scalar constant id for `v` (splatted to `t` if needed), emitted into `out`.
    int make_const(std::vector<Inst>& out, Type t, double v, int line) {
        CVal c{t, std::vector<double>(t.components(), v)};
        return emit_const(out, *f, c, line, f->fresh(), &cm, nullptr, nullptr);
    }

    void visit(Inst& i, std::vector<Inst>& out) {
        if (i.id < 0 || !is_pure(i.op)) { keep(i, out); return; }
        const CVal* a = i.args.size() > 0 ? lookup(cm, i.args[0]) : nullptr;
        const CVal* b = i.args.size() > 1 ? lookup(cm, i.args[1]) : nullptr;
        bool fl = i.type.base == Type::Base::Float;

        switch (i.op) {
            case Op::Add:
                if (all_equal(b, 0)) return alias(i, i.args[0]);
                if (all_equal(a, 0)) return alias(i, i.args[1]);
                break;
            case Op::Sub:
                if (all_equal(b, 0)) return alias(i, i.args[0]);
                if (all_equal(a, 0)) {   // 0 - x → -x
                    i.op = Op::Neg;
                    i.args = {i.args[1]};
                    ++changes;
                }
                break;
            case Op::Mul:
                if (all_equal(b, 1)) return alias(i, i.args[0]);
                if (all_equal(a, 1)) return alias(i, i.args[1]);
                if (all_equal(b, 0)) return alias(i, i.args[1]);   // fast-math: x * 0 = 0
                if (all_equal(a, 0)) return alias(i, i.args[0]);
                if (all_equal(b, -1)) { i.op = Op::Neg; i.args = {i.args[0]}; ++changes; }
                else if (all_equal(a, -1)) { i.op = Op::Neg; i.args = {i.args[1]}; ++changes; }
                break;
            case Op::Div:
                if (all_equal(b, 1)) return alias(i, i.args[0]);
                // x / 2^k → x * 2^-k: exact in binary floating point.
                if (fl && b && b->v.size() > 0 && all_equal(b, b->v[0]) && b->v[0] != 0) {
                    int e;
                    double mant = std::frexp(b->v[0], &e);
                    if (std::fabs(mant) == 0.5) {
                        int k = make_const(out, i.type, 1.0 / b->v[0], i.line);
                        i.op = Op::Mul;
                        i.args = {i.args[0], k};
                        ++changes;
                    }
                }
                break;
            case Op::Neg:
                if (const Inst* d = def(i.args[0]); d && d->op == Op::Neg) return alias(i, d->args[0]);
                break;
            case Op::Not:
                if (const Inst* d = def(i.args[0])) {
                    if (d->op == Op::Not) return alias(i, d->args[0]);
                    const Inst* lhs = def(d->args.empty() ? -1 : d->args[0]);
                    bool int_cmp = lhs && lhs->type.base != Type::Base::Float;
                    Op inv = d->op == Op::Eq ? Op::Ne : d->op == Op::Ne ? Op::Eq
                           : !int_cmp ? Op::Not   // keep float comparisons: NaN breaks !(a<b) == (a>=b)
                           : d->op == Op::Lt ? Op::Ge : d->op == Op::Ge ? Op::Lt
                           : d->op == Op::Gt ? Op::Le : d->op == Op::Le ? Op::Gt : Op::Not;
                    if (inv != Op::Not) {
                        i.op = inv;
                        i.args = d->args;
                        ++changes;
                    }
                }
                break;
            case Op::Select:
                if (i.args[1] == i.args[2]) return alias(i, i.args[1]);
                break;
            case Op::Swizzle: {
                const Inst* d = def(i.args[0]);
                if (d && d->op == Op::Swizzle) {
                    std::vector<int> lanes;
                    for (int l : i.lanes) lanes.push_back(d->lanes[l]);
                    i.args = d->args;
                    i.lanes = lanes;
                    ++changes;
                    d = def(i.args[0]);
                }
                if (d && d->op == Op::Splat) {
                    i.op = Op::Splat;
                    i.args = d->args;
                    i.lanes.clear();
                    ++changes;
                    break;
                }
                // Identity swizzle: v.xyz on a vec3.
                const Inst* src = def(i.args[0]);
                if (src && src->type == i.type) {
                    bool identity = true;
                    for (size_t k = 0; k < i.lanes.size(); ++k) identity = identity && i.lanes[k] == static_cast<int>(k);
                    if (identity) return alias(i, i.args[0]);
                }
                break;
            }
            case Op::Extract: {
                const Inst* d = def(i.args[0]);
                if (!d) break;
                if (d->op == Op::Swizzle) {
                    i.imm = d->lanes[i.imm];
                    i.args = d->args;
                    ++changes;
                    d = def(i.args[0]);
                    if (!d) break;
                }
                if (d->op == Op::Splat) return alias(i, d->args[0]);
                if (d->op == Op::Insert) {
                    if (d->imm == i.imm) return alias(i, d->args[1]);
                    i.args = {d->args[0]};
                    ++changes;
                    break;
                }
                if (d->op == Op::Construct) {
                    if (d->type.is_matrix()) return alias(i, d->args[i.imm]);
                    int at = 0;
                    for (int part : d->args) {
                        const Inst* p = def(part);
                        if (!p) break;
                        int w = p->type.rows;
                        if (i.imm < at + w) {
                            if (p->type.is_scalar()) return alias(i, part);
                            i.args = {part};
                            i.imm -= at;
                            ++changes;
                            break;
                        }
                        at += w;
                    }
                }
                break;
            }
            case Op::Construct: {
                // vec3(v.x, v.y, v.z) → v
                if (!i.type.is_vector()) break;
                int src = -1;
                bool identity = static_cast<int>(i.args.size()) == i.type.rows;
                for (size_t k = 0; identity && k < i.args.size(); ++k) {
                    const Inst* d = def(i.args[k]);
                    identity = d && d->op == Op::Extract && d->imm == static_cast<int>(k) &&
                               (src < 0 || d->args[0] == src);
                    if (identity) src = d->args[0];
                }
                const Inst* s = identity ? def(src) : nullptr;
                if (s && s->type == i.type) return alias(i, src);
                break;
            }
            case Op::Convert: {
                const Inst* d = def(i.args[0]);
                const Inst* src = d && d->op == Op::Convert ? def(d->args[0]) : nullptr;
                // int → float → int is exact; float → int → float is not.
                if (src && src->type == i.type && i.type.is_int()) return alias(i, d->args[0]);
                break;
            }
            case Op::Builtin: {
                BuiltinFn fn = static_cast<BuiltinFn>(i.imm);
                if (fn == BuiltinFn::Pow && b) {
                    if (all_equal(b, 1)) return alias(i, i.args[0]);
                    if (all_equal(b, 2)) { i.op = Op::Mul; i.args = {i.args[0], i.args[0]}; ++changes; break; }
                    if (all_equal(b, 0.5)) { i.imm = static_cast<int>(BuiltinFn::Sqrt); i.args = {i.args[0]}; ++changes; break; }
                }
                if (fn == BuiltinFn::Abs || fn == BuiltinFn::Floor || fn == BuiltinFn::Ceil ||
                    fn == BuiltinFn::Round || fn == BuiltinFn::Trunc || fn == BuiltinFn::Normalize) {
                    const Inst* d = def(i.args[0]);   // idempotent: f(f(x)) = f(x)
                    if (d && d->op == Op::Builtin && d->imm == i.imm) return alias(i, i.args[0]);
                }
                break;
            }
            default:
                break;
        }
        keep(i, out);
    }
};

// ── branch-fold ──────────────────────────────────────────────────────────────

struct BranchFold : Walker<BranchFold> {
    ConstMap cm;

    void begin_function() { cm.clear(); }

    void visit(Inst& i, std::vector<Inst>& out) {
        if (i.op == Op::Const) cm[i.id] = {i.type, {static_cast<double>(i.iconst)}};
        out.push_back(std::move(i));
    }

    void structured(Inst& i, std::vector<Inst>& out) {
        for (auto& sub : i.regions) region(sub);
        if (i.cond >= 0) i.cond = al.get(i.cond);
        if (i.op == Op::If) {
            const CVal* c = lookup(cm, i.args[0]);
            if (c || (i.regions[0].insts.empty() && i.regions[1].insts.empty())) {
                if (c) {
                    Region& taken = i.regions[c->v[0] ? 0 : 1];
                    for (auto& x : taken.insts) out.push_back(std::move(x));
                }
                ++changes;
                return;
            }
        }
        if (i.op == Op::Loop && i.cond >= 0) {
            const CVal* c = lookup(cm, i.cond);
            bool header_pure = true;
            for (const auto& x : i.regions[0].insts) header_pure = header_pure && (is_pure(x.op) || x.op == Op::Load);
            if (c && !c->v[0] && header_pure) { ++changes; return; }   // never enters the body
        }
        out.push_back(std::move(i));
    }
};

// ── forward (store→load forwarding) ──────────────────────────────────────────

void stored_vars(const Region& r, std::unordered_set<int>& out) {
    for_each_inst(r, [&](const Inst& i) { if (i.op == Op::Store) out.insert(i.args[0]); });
}

struct Forward {
    Function* f = nullptr;
    Aliases   al;
    int       changes = 0;
    using Known = std::unordered_map<int, int>;   // var → current value

    void region(Region& r, Known& k) {
        std::vector<Inst> out;
        out.reserve(r.insts.size());
        for (Inst& in : r.insts) {
            Inst i = std::move(in);
            al.apply(i);
            switch (i.op) {
                case Op::Store:
                    k[i.args[0]] = i.args[1];
                    break;
                case Op::Load: {
                    auto it = k.find(i.args[0]);
                    if (it != k.end()) {
                        al.set(i.id, it->second);
                        ++changes;
                        continue;
                    }
                    k[i.args[0]] = i.id;   // later loads reuse this one
                    break;
                }
                case Op::If: {
                    Known before = k;
                    Known a = k, b = k;
                    region(i.regions[0], a);
                    region(i.regions[1], b);
                    std::vector<const Known*> live;
                    if (!region_terminates(i.regions[0])) live.push_back(&a);
                    if (!region_terminates(i.regions[1])) live.push_back(&b);
                    k.clear();
                    for (auto& [var, val] : before) {
                        bool same = true;
                        for (const Known* arm : live) {
                            auto it = arm->find(var);
                            same = same && it != arm->end() && it->second == val;
                        }
                        if (same) k[var] = val;
                    }
                    break;
                }
                case Op::Loop: {
                    std::unordered_set<int> stored;
                    for (const auto& sub : i.regions) stored_vars(sub, stored);
                    Known base = k;
                    for (int v : stored) base.erase(v);
                    Known h = base;
                    region(i.regions[0], h);
                    if (i.cond >= 0) i.cond = al.get(i.cond);
                    Known body = h;
                    region(i.regions[1], body);
                    Known cont = base;
                    region(i.regions[2], cont);
                    k = base;
                    break;
                }
                default:
                    break;
            }
            out.push_back(std::move(i));
        }
        r.insts = std::move(out);
    }

    int run(Module& m) {
        for (auto& fn : m.functions) {
            f = &fn;
            al = Aliases{};
            Known k;
            region(fn.body, k);
        }
        return changes;
    }
};

// ── dse (dead stores and variables) ──────────────────────────────────────────

void loaded_vars(const Region& r, std::unordered_set<int>& out) {
    for_each_inst(r, [&](const Inst& i) { if (i.op == Op::Load) out.insert(i.args[0]); });
}

struct DeadStores {
    int changes = 0;
    std::unordered_set<int> dead_vars;

    // Within one region, a store that is overwritten before any possible read is dead.
    void local(Region& r) {
        std::unordered_map<int, size_t> pending;   // var → index of last unread store
        std::vector<bool> drop(r.insts.size(), false);
        for (size_t idx = 0; idx < r.insts.size(); ++idx) {
            Inst& i = r.insts[idx];
            for (auto& sub : i.regions) local(sub);
            if (i.op == Op::Store) {
                if (dead_vars.count(i.args[0])) { drop[idx] = true; continue; }
                auto it = pending.find(i.args[0]);
                if (it != pending.end()) drop[it->second] = true;
                pending[i.args[0]] = idx;
            } else if (i.op == Op::Load) {
                pending.erase(i.args[0]);
            } else if (i.op == Op::Var && dead_vars.count(i.id)) {
                drop[idx] = true;
            } else if (!i.regions.empty()) {
                std::unordered_set<int> reads;
                for (const auto& sub : i.regions) loaded_vars(sub, reads);
                for (int v : reads) pending.erase(v);
                if (i.op == Op::Loop) {
                    // A loop body may run again and read before the next store.
                    pending.clear();
                }
            } else if (is_terminator(i.op)) {
                pending.clear();   // values may be read after a break/continue
            }
        }
        std::vector<Inst> out;
        for (size_t idx = 0; idx < r.insts.size(); ++idx) {
            if (drop[idx]) { ++changes; continue; }
            out.push_back(std::move(r.insts[idx]));
        }
        r.insts = std::move(out);
    }

    int run(Module& m) {
        for (auto& fn : m.functions) {
            std::unordered_set<int> vars, loaded;
            for_each_inst(fn.body, [&](const Inst& i) {
                if (i.op == Op::Var) vars.insert(i.id);
                if (i.op == Op::Load) loaded.insert(i.args[0]);
            });
            dead_vars.clear();
            for (int v : vars) if (!loaded.count(v)) dead_vars.insert(v);
            // Stores right before a function exit are dead too (locals die there),
            // but only at top level where nothing can follow.
            local(fn.body);
        }
        return changes;
    }
};

// ── cse ──────────────────────────────────────────────────────────────────────

std::string cse_key(const Inst& i) {
    std::string k = std::to_string(static_cast<int>(i.op)) + "|" + i.type.name() + "|" + std::to_string(i.imm) + "|";
    for (int a : i.args) k += std::to_string(a) + ",";
    k += "|";
    for (int l : i.lanes) k += std::to_string(l) + ",";
    if (i.op == Op::Const) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "|%a|%lld", i.fconst, i.iconst);
        k += buf;
    }
    if (i.op == Op::Call) k += "|" + i.callee;
    return k;
}

struct Cse {
    Aliases al;
    int     changes = 0;
    using Table = std::unordered_map<std::string, int>;

    void region(Region& r, Table t) {
        std::vector<Inst> out;
        out.reserve(r.insts.size());
        for (Inst& in : r.insts) {
            Inst i = std::move(in);
            al.apply(i);
            if (i.op == Op::If) {
                region(i.regions[0], t);
                region(i.regions[1], t);
            } else if (i.op == Op::Loop) {
                // Header values dominate the body and the continuing block; body
                // values do not dominate `continuing` (a `continue` can skip them).
                Table h = t;
                region_into(i.regions[0], h);
                if (i.cond >= 0) i.cond = al.get(i.cond);
                region(i.regions[1], h);
                region(i.regions[2], h);
            } else if (i.id >= 0 && is_pure(i.op)) {
                std::string key = cse_key(i);
                auto it = t.find(key);
                if (it != t.end()) {
                    al.set(i.id, it->second);
                    ++changes;
                    continue;
                }
                t.emplace(std::move(key), i.id);
            }
            out.push_back(std::move(i));
        }
        r.insts = std::move(out);
    }

    // Like region(), but leaves the table populated for the caller.
    void region_into(Region& r, Table& t) {
        std::vector<Inst> out;
        for (Inst& in : r.insts) {
            Inst i = std::move(in);
            al.apply(i);
            if (i.id >= 0 && is_pure(i.op) && i.regions.empty()) {
                std::string key = cse_key(i);
                auto it = t.find(key);
                if (it != t.end()) {
                    al.set(i.id, it->second);
                    ++changes;
                    continue;
                }
                t.emplace(std::move(key), i.id);
            } else {
                for (auto& sub : i.regions) region(sub, t);
            }
            out.push_back(std::move(i));
        }
        r.insts = std::move(out);
    }

    int run(Module& m) {
        for (auto& fn : m.functions) {
            al = Aliases{};
            region(fn.body, {});
        }
        return changes;
    }
};

// ── unroll ───────────────────────────────────────────────────────────────────

constexpr int kMaxTrip = 16;
constexpr int kMaxUnrolledInsts = 800;

// Does `r` contain a break/continue that targets the enclosing loop?
bool has_loop_exit(const Region& r) {
    for (const auto& i : r.insts) {
        if (i.op == Op::Break || i.op == Op::Continue) return true;
        if (i.op == Op::If && (has_loop_exit(i.regions[0]) || has_loop_exit(i.regions[1]))) return true;
        // Nested loops own their own break/continue.
    }
    return false;
}

struct Unroll : Walker<Unroll> {
    ConstMap cm;

    void begin_function() { cm.clear(); }

    void visit(Inst& i, std::vector<Inst>& out) {
        if (i.op == Op::Const) cm[i.id] = {i.type, {static_cast<double>(i.iconst)}};
        out.push_back(std::move(i));
    }

    void structured(Inst& i, std::vector<Inst>& out) {
        for (auto& sub : i.regions) region(sub);
        if (i.cond >= 0) i.cond = al.get(i.cond);
        if (i.op != Op::Loop || i.for_var < 0) { out.push_back(std::move(i)); return; }

        const CVal* s = lookup(cm, i.for_start);
        const CVal* e = lookup(cm, i.for_end);
        if (!s || !e) { out.push_back(std::move(i)); return; }
        long long start = static_cast<long long>(s->v[0]), end = static_cast<long long>(e->v[0]);
        long long trip = std::max(0LL, end - start);
        int size = count_insts(i.regions[0]) + count_insts(i.regions[1]) + count_insts(i.regions[2]);
        bool header_ok = true;
        for (const auto& x : i.regions[0].insts) header_ok = header_ok && (is_pure(x.op) || x.op == Op::Load);
        bool counter_written_in_body = false;
        for_each_inst(i.regions[1], [&](const Inst& x) {
            if (x.op == Op::Store && x.args[0] == i.for_var) counter_written_in_body = true;
        });
        if (trip > kMaxTrip || trip * size > kMaxUnrolledInsts || !header_ok || counter_written_in_body ||
            has_loop_exit(i.regions[1]) || has_loop_exit(i.regions[2])) {
            out.push_back(std::move(i));
            return;
        }

        for (long long k = 0; k < trip; ++k) {
            Inst c;
            c.op = Op::Const;
            c.type = Type::int_();
            c.iconst = start + k;
            c.id = f->fresh();
            c.line = i.line;
            int cid = c.id;
            cm[cid] = {c.type, {static_cast<double>(c.iconst)}};
            out.push_back(std::move(c));
            Inst st;
            st.op = Op::Store;
            st.args = {i.for_var, cid};
            st.line = i.line;
            out.push_back(std::move(st));

            std::unordered_map<int, int> remap;
            bool stop = false;
            for (int part = 0; part < 3 && !stop; ++part) {
                Region copy = clone_region(i.regions[part], *f, remap);
                for (auto& x : copy.insts) out.push_back(std::move(x));
                // A body that returns/discards ends the unrolled sequence.
                stop = part == 1 && region_terminates(copy);
            }
            if (stop) break;
        }
        ++changes;
    }
};

// ── licm (loop-invariant code motion) ───────────────────────────────────────

// Hoists pure instructions whose operands are all defined outside a loop to
// just before it. Pure instructions never trap on the GPU, so hoisting out of
// the loop (even past the exit test) is safe.
struct Licm : Walker<Licm> {
    void structured(Inst& i, std::vector<Inst>& out) {
        for (auto& sub : i.regions) region(sub);
        if (i.cond >= 0) i.cond = al.get(i.cond);
        if (i.op == Op::Loop) {
            std::unordered_set<int> inside;
            for (const auto& sub : i.regions)
                for_each_inst(sub, [&](const Inst& x) { if (x.id >= 0) inside.insert(x.id); });
            for (auto& sub : i.regions) {
                std::vector<Inst> keep;
                for (auto& x : sub.insts) {
                    bool invariant = x.id >= 0 && x.regions.empty() && is_pure(x.op) && x.op != Op::Arg;
                    for (int a : x.args) invariant = invariant && !inside.count(a);
                    if (invariant) {
                        inside.erase(x.id);   // now defined outside: dependents may follow
                        if (x.op != Op::Const) ++changes;   // moving constants is bookkeeping
                        out.push_back(std::move(x));
                    } else {
                        keep.push_back(std::move(x));
                    }
                }
                sub.insts = std::move(keep);
            }
        }
        out.push_back(std::move(i));
    }
};

// ── inline ───────────────────────────────────────────────────────────────────

bool inlinable(const Function& g) {
    if (g.stage != Stage::None || g.body.insts.empty()) return false;
    if (g.body.insts.back().op != Op::Return) return false;
    int returns = 0;
    bool discard = false;
    for_each_inst(g.body, [&](const Inst& i) {
        if (i.op == Op::Return) ++returns;
        if (i.op == Op::Discard) discard = true;
    });
    return returns == 1 && !discard;
}

struct Inline : Walker<Inline> {
    Module* m = nullptr;

    void visit(Inst& i, std::vector<Inst>& out) {
        if (i.op != Op::Call) { out.push_back(std::move(i)); return; }
        const Function* g = m->find(i.callee);
        if (!g || !inlinable(*g) || g == f) { out.push_back(std::move(i)); return; }

        Region body;
        std::unordered_map<int, int> remap;
        for (const auto& x : g->body.insts) {
            if (x.op == Op::Arg) remap[x.id] = i.args[x.imm];
            else if (x.op != Op::Return) body.insts.push_back(x);
        }
        const Inst& ret = g->body.insts.back();
        Region copy = clone_region(body, *f, remap);
        // Inlined code keeps the callee's source lines, so hovering a helper
        // in the editor still highlights its code inside the caller.
        for (auto& x : copy.insts) out.push_back(std::move(x));
        if (!ret.args.empty()) {
            auto it = remap.find(ret.args[0]);
            al.set(i.id, it == remap.end() ? ret.args[0] : it->second);
        }
        ++changes;
    }
};

// ── dce ──────────────────────────────────────────────────────────────────────

int dce_region(Region& r, const std::unordered_map<int, int>& uses) {
    int removed = 0;
    std::vector<Inst> out;
    for (Inst& i : r.insts) {
        for (auto& sub : i.regions) removed += dce_region(sub, uses);
        bool unused = i.id >= 0 && uses.find(i.id) == uses.end();
        if (unused && (is_pure(i.op) || i.op == Op::Var || i.op == Op::Load)) { ++removed; continue; }
        if (i.op == Op::If && i.regions[0].insts.empty() && i.regions[1].insts.empty()) { ++removed; continue; }
        out.push_back(std::move(i));
    }
    r.insts = std::move(out);
    return removed;
}

int run_dce(Module& m) {
    int changes = 0;
    for (auto& fn : m.functions) {
        for (;;) {
            auto uses = use_counts(fn);
            int n = dce_region(fn.body, uses);
            if (!n) break;
            changes += n;
        }
    }
    // Drop functions unreachable from any entry point.
    std::unordered_set<std::string> live;
    std::vector<const Function*> work;
    for (const auto& fn : m.functions)
        if (fn.stage != Stage::None) { live.insert(fn.name); work.push_back(&fn); }
    while (!work.empty()) {
        const Function* fn = work.back();
        work.pop_back();
        for_each_inst(fn->body, [&](const Inst& i) {
            if (i.op == Op::Call && live.insert(i.callee).second)
                if (const Function* g = m.find(i.callee)) work.push_back(g);
        });
    }
    auto before = m.functions.size();
    m.functions.erase(std::remove_if(m.functions.begin(), m.functions.end(),
                                     [&](const Function& fn) { return !live.count(fn.name); }),
                      m.functions.end());
    changes += static_cast<int>(before - m.functions.size());
    return changes;
}

} // namespace

// ── Public entry points ──────────────────────────────────────────────────────

int pass_inline(Module& m)      { Inline p; p.m = &m; return p.run(m); }
int pass_const_fold(Module& m)  { return ConstFold{}.run(m); }
int pass_simplify(Module& m)    { return Simplify{}.run(m); }
int pass_branch_fold(Module& m) { return BranchFold{}.run(m); }
int pass_forward(Module& m)     { return Forward{}.run(m); }
int pass_dse(Module& m)         { return DeadStores{}.run(m); }
int pass_cse(Module& m)         { return Cse{}.run(m); }
int pass_unroll(Module& m)      { return Unroll{}.run(m); }
int pass_dce(Module& m)         { return run_dce(m); }
int pass_licm(Module& m)        { return Licm{}.run(m); }

const std::vector<PassInfo>& all_passes() {
    static const std::vector<PassInfo> passes = {
        {"inline",      "Inline calls to single-exit helper functions",            pass_inline},
        {"const-fold",  "Evaluate operations whose operands are all constants",    pass_const_fold},
        {"simplify",    "Algebraic identities (x*1, x+0, pow(x,2)) and swizzle/extract peepholes", pass_simplify},
        {"branch-fold", "Resolve ifs with constant conditions; drop empty ifs",    pass_branch_fold},
        {"forward",     "Forward stored values to later loads (mem2reg-lite)",     pass_forward},
        {"dse",         "Remove overwritten stores and never-read variables",      pass_dse},
        {"cse",         "Merge identical pure computations (scoped value numbering)", pass_cse},
        {"licm",        "Hoist loop-invariant computations out of loops",          pass_licm},
        {"unroll",      "Fully unroll for-loops with small constant trip counts",  pass_unroll},
        {"dce",         "Remove unused values and unreachable functions",          pass_dce},
    };
    return passes;
}

int count_module_insts(const Module& m) {
    int n = 0;
    for (const auto& f : m.functions) n += count_insts(f.body);
    return n;
}

PipelineReport run_pipeline(Module& m, const std::vector<std::string>& disabled, bool record) {
    PipelineReport rep;
    rep.insts_before = count_module_insts(m);
    for (const auto& p : all_passes()) rep.totals.push_back({p.name, 0});

    constexpr int kMaxIterations = 8;
    for (int iter = 1; iter <= kMaxIterations; ++iter) {
        rep.iterations = iter;
        int total = 0;
        for (size_t k = 0; k < all_passes().size(); ++k) {
            const PassInfo& p = all_passes()[k];
            if (std::find(disabled.begin(), disabled.end(), p.name) != disabled.end()) continue;
            int changes = p.run(m);
            for (auto& fn : m.functions) changes += truncate_unreachable(fn.body);
            total += changes;
            rep.totals[k].changes += changes;
            PassStep step;
            step.pass = p.name;
            step.iteration = iter;
            step.changes = changes;
            if (record && changes > 0) {
                Printed pr = print_module(m);
                step.ir_after = std::move(pr.text);
                step.line_map = std::move(pr.line_map);
            }
            rep.steps.push_back(std::move(step));
        }
        if (total == 0) break;
    }
    rep.insts_after = count_module_insts(m);
    return rep;
}

} // namespace ir
