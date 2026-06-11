#include "midend/passes.hpp"
#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

// ── Optimization passes ─────────────────────────────────────────────────────
//
// All passes operate per-Function. They follow a simple invariant: an
// instruction's `result` ValueId is always defined before any reference to
// it in the function's block-linear order (the IR has no SSA φ-nodes;
// loop-carried state lives in alloca slots and is read via LOAD/STORE).
// This lets every pass do a single forward sweep.

namespace mir {

namespace {

// ── Compile-time value tracking for scalars ─────────────────────────────────

enum class CKind { None, Int, Float, Bool };
struct CVal {
    CKind kind = CKind::None;
    int64_t i = 0;
    double  f = 0.0;
    bool    b = false;

    static CVal of_int  (int64_t x) { CVal v; v.kind = CKind::Int;   v.i = x; return v; }
    static CVal of_float(double  x) { CVal v; v.kind = CKind::Float; v.f = x; return v; }
    static CVal of_bool (bool    x) { CVal v; v.kind = CKind::Bool;  v.b = x; return v; }
};

using Consts = std::unordered_map<ValueId, CVal>;

static const CVal* find_const(const Consts& c, ValueId id) {
    auto it = c.find(id);
    return it == c.end() ? nullptr : &it->second;
}

// Mutate an Inst into a constant of the given kind.
static void make_const_int(Inst& i, int64_t v) {
    i.op       = Op::CONST_INT;
    i.operands.clear();
    i.ival     = v;
    i.sval.clear();
}
static void make_const_float(Inst& i, double v) {
    i.op       = Op::CONST_FLOAT;
    i.operands.clear();
    i.fval     = v;
    i.sval.clear();
}
static void make_const_bool(Inst& i, bool v) {
    i.op       = Op::CONST_BOOL;
    i.operands.clear();
    i.bval     = v;
    i.sval.clear();
}

// Records the just-emitted constant value of an inst in the table.
static void record_constant(const Inst& i, Consts& c) {
    switch (i.op) {
        case Op::CONST_INT:   c[i.result] = CVal::of_int  (i.ival); break;
        case Op::CONST_FLOAT: c[i.result] = CVal::of_float(i.fval); break;
        case Op::CONST_BOOL:  c[i.result] = CVal::of_bool (i.bval); break;
        default: break;
    }
}

}  // namespace

// ── Constant folding ────────────────────────────────────────────────────────
//
// Evaluates scalar arithmetic, comparison, and negation whose operands are
// known compile-time constants. Mutates the inst in place into the
// corresponding CONST_* op so downstream consumers can keep folding.
//
// Skips DIV / MOD when the divisor is zero (preserves runtime error).
int const_fold(Function& fn) {
    Consts known;
    int changes = 0;

    for (auto& b : fn.blocks) {
        for (auto& i : b.insts) {
            // Pick up pre-existing constants regardless of pass action.
            if (i.op == Op::CONST_INT || i.op == Op::CONST_FLOAT || i.op == Op::CONST_BOOL) {
                record_constant(i, known);
                continue;
            }

            switch (i.op) {
                case Op::NEG: {
                    const CVal* x = find_const(known, i.operands[0]);
                    if (!x) break;
                    if (x->kind == CKind::Int)         { make_const_int  (i, -x->i); ++changes; }
                    else if (x->kind == CKind::Float)  { make_const_float(i, -x->f); ++changes; }
                    break;
                }
                case Op::ADD: case Op::SUB: case Op::MUL: case Op::DIV: case Op::MOD: {
                    const CVal* l = find_const(known, i.operands[0]);
                    const CVal* r = find_const(known, i.operands[1]);
                    if (!l || !r || l->kind != r->kind) break;
                    if (l->kind == CKind::Int) {
                        int64_t a = l->i, c = r->i, out = 0;
                        bool ok = true;
                        switch (i.op) {
                            case Op::ADD: out = a + c; break;
                            case Op::SUB: out = a - c; break;
                            case Op::MUL: out = a * c; break;
                            case Op::DIV: if (c == 0) { ok = false; break; } out = a / c; break;
                            case Op::MOD: if (c == 0) { ok = false; break; } out = a % c; break;
                            default: ok = false; break;
                        }
                        if (ok) { make_const_int(i, out); ++changes; }
                    } else if (l->kind == CKind::Float) {
                        double a = l->f, c = r->f, out = 0.0;
                        bool ok = true;
                        switch (i.op) {
                            case Op::ADD: out = a + c; break;
                            case Op::SUB: out = a - c; break;
                            case Op::MUL: out = a * c; break;
                            case Op::DIV: if (c == 0.0) { ok = false; break; } out = a / c; break;
                            default: ok = false; break;   // MOD not supported on float
                        }
                        if (ok) { make_const_float(i, out); ++changes; }
                    }
                    break;
                }
                case Op::CMP_EQ: case Op::CMP_NE:
                case Op::CMP_LT: case Op::CMP_GT:
                case Op::CMP_LE: case Op::CMP_GE: {
                    const CVal* l = find_const(known, i.operands[0]);
                    const CVal* r = find_const(known, i.operands[1]);
                    if (!l || !r || l->kind != r->kind) break;
                    bool out = false;
                    if (l->kind == CKind::Int) {
                        switch (i.op) {
                            case Op::CMP_EQ: out = l->i == r->i; break;
                            case Op::CMP_NE: out = l->i != r->i; break;
                            case Op::CMP_LT: out = l->i <  r->i; break;
                            case Op::CMP_GT: out = l->i >  r->i; break;
                            case Op::CMP_LE: out = l->i <= r->i; break;
                            case Op::CMP_GE: out = l->i >= r->i; break;
                            default: break;
                        }
                    } else if (l->kind == CKind::Float) {
                        switch (i.op) {
                            case Op::CMP_EQ: out = l->f == r->f; break;
                            case Op::CMP_NE: out = l->f != r->f; break;
                            case Op::CMP_LT: out = l->f <  r->f; break;
                            case Op::CMP_GT: out = l->f >  r->f; break;
                            case Op::CMP_LE: out = l->f <= r->f; break;
                            case Op::CMP_GE: out = l->f >= r->f; break;
                            default: break;
                        }
                    } else if (l->kind == CKind::Bool) {
                        switch (i.op) {
                            case Op::CMP_EQ: out = l->b == r->b; break;
                            case Op::CMP_NE: out = l->b != r->b; break;
                            default: break;
                        }
                    }
                    make_const_bool(i, out);
                    ++changes;
                    break;
                }
                default: break;
            }
            record_constant(i, known);
        }
    }
    return changes;
}

// ── Algebraic simplification ────────────────────────────────────────────────
//
// Rewrites identity patterns by aliasing the result ValueId to a simpler
// existing value, then propagates the alias forward through subsequent
// operand references. The original (now-dead) instruction stays in place;
// DCE will sweep it up later.
//
// Patterns handled:
//   x + 0 = x,   0 + x = x,
//   x - 0 = x,   x - x = 0,
//   x * 1 = x,   1 * x = x,   x * 0 = 0,   0 * x = 0,
//   x / 1 = x,
//   -(-x) = x.
int algebraic_simplify(Function& fn) {
    std::unordered_map<ValueId, ValueId> alias;
    // Maps a value produced by a NEG inst to its original (pre-negation) operand.
    // Lets us collapse `-(-x)` to `x` without a separate forward sweep.
    std::unordered_map<ValueId, ValueId> neg_origins;
    Consts known;
    int changes = 0;

    auto resolve = [&](ValueId id) {
        // Follow alias chains to a fixed point.
        while (true) {
            auto it = alias.find(id);
            if (it == alias.end()) return id;
            id = it->second;
        }
    };

    auto is_int_const = [&](ValueId id, int64_t val) {
        const CVal* c = find_const(known, resolve(id));
        return c && c->kind == CKind::Int && c->i == val;
    };
    auto is_float_const = [&](ValueId id, double val) {
        const CVal* c = find_const(known, resolve(id));
        return c && c->kind == CKind::Float && c->f == val;
    };
    auto is_zero = [&](ValueId id) { return is_int_const(id, 0) || is_float_const(id, 0.0); };
    auto is_one  = [&](ValueId id) { return is_int_const(id, 1) || is_float_const(id, 1.0); };

    for (auto& b : fn.blocks) {
        for (auto& i : b.insts) {
            // Rewrite operands using the current alias map.
            for (auto& op : i.operands) op = resolve(op);

            // Constants get tracked but never rewritten themselves.
            if (i.op == Op::CONST_INT || i.op == Op::CONST_FLOAT || i.op == Op::CONST_BOOL) {
                record_constant(i, known);
                continue;
            }

            switch (i.op) {
                case Op::ADD: {
                    ValueId l = i.operands[0], r = i.operands[1];
                    if (is_zero(l))      { alias[i.result] = r; ++changes; }
                    else if (is_zero(r)) { alias[i.result] = l; ++changes; }
                    break;
                }
                case Op::SUB: {
                    ValueId l = i.operands[0], r = i.operands[1];
                    if (is_zero(r))      { alias[i.result] = l; ++changes; }
                    else if (l == r) {
                        // x - x = 0   (same type as the operand)
                        if (i.result_type.kind == FluxType::Kind::Float) make_const_float(i, 0.0);
                        else                                             make_const_int  (i, 0);
                        record_constant(i, known);
                        ++changes;
                    }
                    break;
                }
                case Op::MUL: {
                    ValueId l = i.operands[0], r = i.operands[1];
                    if (is_zero(l) || is_zero(r)) {
                        if (i.result_type.kind == FluxType::Kind::Float) make_const_float(i, 0.0);
                        else                                             make_const_int  (i, 0);
                        record_constant(i, known);
                        ++changes;
                    } else if (is_one(l))      { alias[i.result] = r; ++changes; }
                      else if (is_one(r))      { alias[i.result] = l; ++changes; }
                    break;
                }
                case Op::DIV: {
                    ValueId r = i.operands[1];
                    if (is_one(r)) { alias[i.result] = i.operands[0]; ++changes; }
                    break;
                }
                case Op::NEG: {
                    // -(-x) → x : if the operand was itself a NEG, peel both
                    // layers by aliasing this result to that NEG's operand.
                    auto inner = neg_origins.find(i.operands[0]);
                    if (inner != neg_origins.end()) {
                        alias[i.result] = inner->second;
                        ++changes;
                    } else {
                        neg_origins[i.result] = i.operands[0];
                    }
                    break;
                }
                default: break;
            }
            record_constant(i, known);
        }

        // Fix up terminator operand references too.
        if (b.term.kind == Terminator::BR_COND) b.term.cond    = resolve(b.term.cond);
        if (b.term.kind == Terminator::RET)     b.term.ret_val = resolve(b.term.ret_val);
    }
    return changes;
}

// ── Loop fusion ─────────────────────────────────────────────────────────────
//
// Fuses chains of ARRAY_OP instructions (and optional REDUCE_SUM) into a
// single FUSED_LOOP so the backend emits one LLVM loop instead of one loop
// per element-wise op plus a separate reduction loop.
//
// Example: sum(2.0 * x + y)
//   %t1 = array.op '*' %2.0, %x
//   %t2 = array.op '+' %t1, %y
//   %s  = reduce.sum %t2
// becomes:
//   %s = fused.loop [*, +, sum] %2.0, %x, %y

static void count_uses(Function& fn, std::unordered_map<ValueId, int>& uses) {
    uses.clear();
    for (auto& b : fn.blocks) {
        for (auto& i : b.insts)
            for (ValueId op : i.operands) ++uses[op];
        if (b.term.kind == Terminator::BR_COND) ++uses[b.term.cond];
        if (b.term.kind == Terminator::RET)     ++uses[b.term.ret_val];
    }
}

static Inst* find_def(Function& fn, ValueId id) {
    for (auto& b : fn.blocks)
        for (auto& i : b.insts)
            if (i.result == id) return &i;
    return nullptr;
}

static bool is_fusable_array_op(const Inst& i) {
    if (i.op != Op::ARRAY_OP) return false;
    return i.sval == "+" || i.sval == "-" || i.sval == "*" ||
           i.sval == "/" || i.sval == "%";
}

static bool collect_array_op_chain(Function& fn,
                                   const std::unordered_map<ValueId, int>& uses,
                                   ValueId start,
                                   std::vector<Inst*>& chain) {
    chain.clear();
    ValueId cur = start;
    while (true) {
        Inst* def = find_def(fn, cur);
        if (!def || !is_fusable_array_op(*def)) return false;
        // Every value in the chain — including the head — must have exactly
        // one use: fusion deletes these defs, so any other user (e.g. an
        // index.load on the intermediate array) would be left dangling.
        auto uit = uses.find(cur);
        if (uit == uses.end() || uit->second != 1) return false;
        chain.push_back(def);
        ValueId prev = NO_VALUE;
        for (ValueId op : def->operands) {
            Inst* op_def = find_def(fn, op);
            if (op_def && op_def->op == Op::ARRAY_OP) prev = op;
        }
        if (prev == NO_VALUE) return true;
        cur = prev;
    }
}

static bool build_fused_rpn(const std::vector<Inst*>& chain,
                            std::vector<ValueId>& leaves,
                            std::vector<std::string>& rpn) {
    if (chain.empty()) return false;
    leaves.clear();
    rpn.clear();

    std::vector<Inst*> ordered = chain;
    std::reverse(ordered.begin(), ordered.end());

    ValueId cur_temp = NO_VALUE;
    for (size_t idx = 0; idx < ordered.size(); ++idx) {
        Inst* op = ordered[idx];
        if (idx == 0) {
            for (ValueId v : op->operands) leaves.push_back(v);
        } else {
            bool found_prev = false;
            for (ValueId v : op->operands) {
                if (v == cur_temp) found_prev = true;
                else leaves.push_back(v);
            }
            if (!found_prev) return false;
        }
        rpn.push_back(op->sval);
        cur_temp = op->result;
    }
    return true;
}

int loop_fusion(Function& fn) {
    struct FusionSite {
        Inst*              reduce = nullptr;
        std::vector<Inst*> chain;
        std::vector<ValueId> leaves;
        std::vector<std::string> rpn;
    };
    std::vector<FusionSite> sites;

    std::unordered_map<ValueId, int> uses;
    count_uses(fn, uses);

    for (auto& b : fn.blocks) {
        for (auto& i : b.insts) {
            if (i.op != Op::REDUCE_SUM) continue;
            if (i.operands.size() != 1) continue;

            std::vector<Inst*> chain;
            if (!collect_array_op_chain(fn, uses, i.operands[0], chain))
                continue;
            if (chain.empty()) continue;

            FusionSite site;
            site.reduce = &i;
            site.chain  = chain;
            if (!build_fused_rpn(chain, site.leaves, site.rpn)) continue;
            site.rpn.push_back("sum");
            sites.push_back(std::move(site));
        }
    }

  // Map-only: fuse ARRAY_OP chains whose result is used once by a non-ARRAY_OP.
    for (auto& b : fn.blocks) {
        for (auto& i : b.insts) {
            if (!is_fusable_array_op(i)) continue;
            if (uses[i.result] != 1) continue;
            // Skip if already part of a reduce fusion site.
            bool consumed = false;
            for (const auto& s : sites) {
                for (Inst* p : s.chain)
                    if (p == &i) { consumed = true; break; }
                if (s.reduce == &i) consumed = true;
                if (consumed) break;
            }
            if (consumed) continue;

            std::vector<Inst*> chain;
            if (!collect_array_op_chain(fn, uses, i.result, chain)) continue;
            if (chain.size() < 2) continue;

            FusionSite site;
            site.reduce = &i;
            site.chain  = chain;
            if (!build_fused_rpn(chain, site.leaves, site.rpn)) continue;
            sites.push_back(std::move(site));
        }
    }

    if (sites.empty()) return 0;

    std::unordered_map<Inst*, bool> remove;
    int changes = 0;

    for (auto& site : sites) {
        const bool is_reduce = site.reduce->op == Op::REDUCE_SUM;
        Inst& target = is_reduce ? *site.reduce : *site.chain[0];

        Inst fused;
        fused.op          = Op::FUSED_LOOP;
        fused.operands    = site.leaves;
        fused.fused_ops   = site.rpn;
        fused.result      = target.result;
        fused.result_type = target.result_type;
        fused.result_role = target.result_role;

        if (is_reduce) {
            fused.result_type = target.result_type;
            fused.result_role = ValueRole::SCALAR;
        }

        target = std::move(fused);
        ++changes;

        for (Inst* p : site.chain) {
            if (p != &target) remove[p] = true;
        }
    }

    for (auto& b : fn.blocks) {
        auto& insts = b.insts;
        insts.erase(std::remove_if(insts.begin(), insts.end(), [&](const Inst& i) {
            Inst* mut = const_cast<Inst*>(&i);
            auto it = remove.find(mut);
            return it != remove.end() && it->second;
        }), insts.end());
    }

    return changes;
}

// ── Dead code elimination ───────────────────────────────────────────────────
//
// Iteratively removes value-producing instructions whose result has no uses,
// provided the op has no side effects. Side-effecting ops (STORE,
// INDEX_STORE, ARRAY_COPY, CALL, PRINT) are always preserved.
//
// CALL is preserved conservatively: even if its result is unused, the
// function may print or otherwise mutate state.
int dce(Function& fn) {
    auto has_side_effect = [](Op op) {
        switch (op) {
            case Op::STORE: case Op::INDEX_STORE: case Op::ARRAY_COPY:
            case Op::CALL:  case Op::PRINT:
                return true;
            default:
                return false;
        }
    };

    int total = 0;
    while (true) {
        std::unordered_map<ValueId, int> uses;
        for (auto& b : fn.blocks) {
            for (auto& i : b.insts)
                for (ValueId op : i.operands) ++uses[op];
            if (b.term.kind == Terminator::BR_COND) ++uses[b.term.cond];
            if (b.term.kind == Terminator::RET)     ++uses[b.term.ret_val];
        }

        int round = 0;
        for (auto& b : fn.blocks) {
            auto& insts = b.insts;
            insts.erase(std::remove_if(insts.begin(), insts.end(), [&](const Inst& i) {
                if (has_side_effect(i.op))     return false;
                if (i.result == NO_VALUE)      return false;
                if (uses[i.result] > 0)        return false;
                ++round;
                return true;
            }), insts.end());
        }
        if (round == 0) break;
        total += round;
    }
    return total;
}

// ── Pipeline ────────────────────────────────────────────────────────────────

PassReport PassPipeline::run(Module& m, int max_iterations) {
    PassReport report;
    std::unordered_map<std::string, int> agg;
    std::vector<std::string> order;          // first-seen order for stable reporting

    for (int it = 0; it < max_iterations; ++it) {
        int round_changes = 0;
        for (const auto& [name, fn] : passes) {
            int c = 0;
            for (auto& f : m.functions) c += fn(f);

            PassStep step;
            step.name      = name;
            step.iteration = it;
            step.changes   = c;
            step.mir_after = print_module(m);
            report.steps.push_back(std::move(step));

            if (!agg.count(name)) { agg[name] = 0; order.push_back(name); }
            agg[name]     += c;
            round_changes += c;
        }
        ++report.iterations;
        if (round_changes == 0) break;
    }

    for (const auto& name : order) report.passes.push_back({name, agg[name]});
    return report;
}

PassPipeline default_pipeline() {
    return {{
        {"const-fold",         const_fold},
        {"algebraic-simplify", algebraic_simplify},
        {"const-fold",         const_fold},   // catch consts exposed by algebraic-simplify
        {"loop-fusion",        loop_fusion},
        {"dce",                dce},
    }};
}

}  // namespace mir
