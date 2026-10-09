#include "ir/ir.hpp"
#include "common/builtins.hpp"
#include "common/format.hpp"

namespace ir {

const char* op_name(Op op) {
    switch (op) {
        case Op::Const:      return "const";
        case Op::Arg:        return "arg";
        case Op::Uniform:    return "uniform";
        case Op::Add:        return "add";
        case Op::Sub:        return "sub";
        case Op::Mul:        return "mul";
        case Op::Div:        return "div";
        case Op::Rem:        return "rem";
        case Op::Neg:        return "neg";
        case Op::MatMul:     return "matmul";
        case Op::MatScale:   return "matscale";
        case Op::Eq:         return "eq";
        case Op::Ne:         return "ne";
        case Op::Lt:         return "lt";
        case Op::Le:         return "le";
        case Op::Gt:         return "gt";
        case Op::Ge:         return "ge";
        case Op::And:        return "and";
        case Op::Or:         return "or";
        case Op::Not:        return "not";
        case Op::Select:     return "select";
        case Op::Construct:  return "construct";
        case Op::Splat:      return "splat";
        case Op::Extract:    return "extract";
        case Op::ExtractDyn: return "extract.dyn";
        case Op::Insert:     return "insert";
        case Op::Swizzle:    return "swizzle";
        case Op::Shuffle:    return "shuffle";
        case Op::Convert:    return "convert";
        case Op::Builtin:    return "builtin";
        case Op::Call:       return "call";
        case Op::Var:        return "var";
        case Op::Load:       return "load";
        case Op::Store:      return "store";
        case Op::If:         return "if";
        case Op::Loop:       return "loop";
        case Op::Break:      return "break";
        case Op::Continue:   return "continue";
        case Op::Return:     return "return";
        case Op::Discard:    return "discard";
    }
    return "?";
}

Function* Module::find(const std::string& name) {
    for (auto& f : functions) if (f.name == name) return &f;
    return nullptr;
}

const Function* Module::find(const std::string& name) const {
    for (const auto& f : functions) if (f.name == name) return &f;
    return nullptr;
}

bool is_terminator(Op op) {
    return op == Op::Break || op == Op::Continue || op == Op::Return || op == Op::Discard;
}

static bool inst_terminates(const Inst& i) {
    if (is_terminator(i.op)) return true;
    if (i.op == Op::If) return region_terminates(i.regions[0]) && region_terminates(i.regions[1]);
    return false;
}

bool region_terminates(const Region& r) {
    return !r.insts.empty() && inst_terminates(r.insts.back());
}

bool is_pure(Op op) {
    switch (op) {
        case Op::Var: case Op::Load: case Op::Store:
        case Op::If: case Op::Loop:
        case Op::Break: case Op::Continue: case Op::Return: case Op::Discard:
            return false;
        default:
            return true;   // user functions can't have side effects in Flux
    }
}

int truncate_unreachable(Region& r) {
    int removed = 0;
    for (size_t i = 0; i < r.insts.size(); ++i) {
        for (auto& sub : r.insts[i].regions) removed += truncate_unreachable(sub);
        if (inst_terminates(r.insts[i]) && i + 1 < r.insts.size()) {
            for (size_t j = i + 1; j < r.insts.size(); ++j) removed += count_insts_one(r.insts[j]);
            r.insts.resize(i + 1);
            break;
        }
    }
    return removed;
}

int count_insts_one(const Inst& i) {
    int n = 1;
    for (const auto& r : i.regions) n += count_insts(r);
    return n;
}

void for_each_inst(Region& r, const std::function<void(Inst&)>& fn) {
    for (auto& i : r.insts) {
        fn(i);
        for (auto& sub : i.regions) for_each_inst(sub, fn);
    }
}

void for_each_inst(const Region& r, const std::function<void(const Inst&)>& fn) {
    for (const auto& i : r.insts) {
        fn(i);
        for (const auto& sub : i.regions) for_each_inst(sub, fn);
    }
}

int count_insts(const Region& r) {
    int n = 0;
    for (const auto& i : r.insts) n += count_insts_one(i);
    return n;
}

std::unordered_map<int, int> use_counts(const Function& f) {
    std::unordered_map<int, int> uses;
    for_each_inst(f.body, [&](const Inst& i) {
        for (int a : i.args) ++uses[a];
        if (i.op == Op::Loop && i.cond >= 0) ++uses[i.cond];
    });
    return uses;
}

static void clone_into(const Region& src, Region& dst, Function& f, std::unordered_map<int, int>& remap) {
    auto map = [&](int id) {
        auto it = remap.find(id);
        return it == remap.end() ? id : it->second;
    };
    for (const auto& i : src.insts) {
        Inst c = i;
        c.regions.clear();
        for (int& a : c.args) a = map(a);
        if (i.id >= 0) { c.id = f.fresh(); remap[i.id] = c.id; }
        for (const auto& sub : i.regions) {
            c.regions.emplace_back();
            clone_into(sub, c.regions.back(), f, remap);
        }
        c.cond      = i.cond >= 0 ? map(i.cond) : -1;
        c.for_var   = i.for_var >= 0 ? map(i.for_var) : -1;
        c.for_start = i.for_start >= 0 ? map(i.for_start) : -1;
        c.for_end   = i.for_end >= 0 ? map(i.for_end) : -1;
        dst.insts.push_back(std::move(c));
    }
}

Region clone_region(const Region& src, Function& f, std::unordered_map<int, int>& remap) {
    Region out;
    clone_into(src, out, f, remap);
    return out;
}

// ── Printer ──────────────────────────────────────────────────────────────────

namespace {

struct Printer {
    const Module& m;
    Printed out;
    int indent = 0;
    // Values are renumbered in print order, so an edit that doesn't change
    // the program (e.g. dead code) prints identically — clean diffs.
    mutable std::unordered_map<int, int> numbering;

    void line(const std::string& s, int src_line) {
        out.text += std::string(indent * 2, ' ') + s + "\n";
        out.line_map.push_back(src_line);
    }

    std::string v(int id) const {
        auto it = numbering.find(id);
        if (it == numbering.end()) it = numbering.emplace(id, static_cast<int>(numbering.size())).first;
        return "%" + std::to_string(it->second);
    }

    std::string args(const Inst& i) const {
        // (operands are always defined before use, so they're already numbered)
        std::string s;
        for (size_t k = 0; k < i.args.size(); ++k) s += (k ? ", " : "") + v(i.args[k]);
        return s;
    }

    static std::string lanes_of(const std::vector<int>& l, bool shuffle) {
        static const char* xyzw = "xyzw";
        std::string s;
        for (int x : l) {
            if (shuffle) s += (s.empty() ? "" : " ") + std::to_string(x);
            else s += xyzw[x];
        }
        return s;
    }

    std::string body(const Inst& i) const {
        switch (i.op) {
            case Op::Const:
                if (i.type.is_float()) return "const " + format_float(i.fconst);
                if (i.type.is_bool())  return std::string("const ") + (i.iconst ? "true" : "false");
                return "const " + std::to_string(i.iconst);
            case Op::Arg:
                return "arg " + std::to_string(i.imm);
            case Op::Uniform:
                return "uniform @" + (i.imm < static_cast<int>(m.uniforms.size()) ? m.uniforms[i.imm].name : "?");
            case Op::Extract:
                return "extract " + args(i) + ", " + std::to_string(i.imm);
            case Op::Insert:
                return "insert " + args(i) + ", " + std::to_string(i.imm);
            case Op::Swizzle:
                return "swizzle " + args(i) + "." + lanes_of(i.lanes, false);
            case Op::Shuffle:
                return "shuffle " + args(i) + " [" + lanes_of(i.lanes, true) + "]";
            case Op::Builtin:
                return std::string(builtin_info(static_cast<BuiltinFn>(i.imm)).name) + " " + args(i);
            case Op::Call:
                return "call @" + i.callee + "(" + args(i) + ")";
            default:
                return std::string(op_name(i.op)) + (i.args.empty() ? "" : " " + args(i));
        }
    }

    void region(const Region& r) {
        ++indent;
        for (const auto& i : r.insts) inst(i);
        --indent;
    }

    void inst(const Inst& i) {
        std::string hint = i.name.empty() ? "" : "  ; " + i.name;
        switch (i.op) {
            case Op::If:
                line("if " + v(i.args[0]) + " {", i.line);
                region(i.regions[0]);
                if (!i.regions[1].insts.empty()) {
                    line("} else {", i.line);
                    region(i.regions[1]);
                }
                line("}", i.line);
                return;
            case Op::Loop: {
                bool is_for = i.for_var >= 0;
                line(std::string("loop {") + (is_for ? "  ; for " + v(i.for_var) + " in " +
                     v(i.for_start) + ".." + v(i.for_end) : ""), i.line);
                region(i.regions[0]);
                line(i.cond >= 0 ? "} while " + v(i.cond) + " {" : "} {", i.line);
                region(i.regions[1]);
                if (!i.regions[2].insts.empty()) {
                    line("} continuing {", i.line);
                    region(i.regions[2]);
                }
                line("}", i.line);
                return;
            }
            case Op::Var:
                line(v(i.id) + " = var " + i.type.name() + hint, i.line);
                return;
            default:
                break;
        }
        if (i.id >= 0) line(v(i.id) + " = " + body(i) + " : " + i.type.name() + hint, i.line);
        else           line(body(i) + hint, i.line);
    }

    void function(const Function& f) {
        numbering.clear();
        std::string sig = std::string(f.stage == Stage::None ? "" : std::string("@") + stage_name(f.stage) + " ") +
                          "fn @" + f.name + "(";
        for (size_t k = 0; k < f.param_types.size(); ++k)
            sig += (k ? ", " : "") + f.param_names[k] + ": " + f.param_types[k].name();
        sig += ") -> " + f.return_type.name() + " {";
        line(sig, f.line);
        region(f.body);
        line("}", f.line);
    }
};

} // namespace

Printed print_module(const Module& m) {
    Printer p{m, {}, 0};
    if (!m.uniforms.empty()) {
        p.line("uniforms {", 0);
        for (const auto& u : m.uniforms)
            p.line("  @" + u.name + ": " + u.type.name() + "  ; offset " + std::to_string(u.offset), 0);
        p.line("}", 0);
        p.line("", 0);
    }
    for (size_t k = 0; k < m.functions.size(); ++k) {
        if (k) p.line("", 0);
        p.function(m.functions[k]);
    }
    return p.out;
}

} // namespace ir
