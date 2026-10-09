#include "ir/lower.hpp"
#include "common/builtins.hpp"
#include <unordered_map>

namespace ir {
namespace {

class Lowerer {
public:
    Lowerer(const Program& prog, const Reflection& refl) : prog_(prog) {
        for (size_t i = 0; i < prog.uniforms.size(); ++i) {
            uniform_index_[prog.uniforms[i].get()] = static_cast<int>(i);
            const UniformInfo* info = nullptr;
            for (const auto& u : refl.uniforms) if (u.name == prog.uniforms[i]->name) info = &u;
            mod_.uniforms.push_back({prog.uniforms[i]->name, prog.uniforms[i]->type, info ? info->offset : 0});
        }
    }

    Module run() {
        for (const auto& fn : prog_.functions) lower_function(*fn);
        return std::move(mod_);
    }

private:
    const Program& prog_;
    Module         mod_;
    Function*      fn_ = nullptr;
    std::vector<Region*> regions_;
    std::unordered_map<const void*, int> env_;      // decl → SSA value or Var id
    std::unordered_map<const void*, int> uniform_index_;
    std::unordered_map<int, std::string> names_;    // value id → source name
    int line_ = 0;

    // ── Emission helpers ────────────────────────────────────────────────

    int emit(Inst i) {
        i.line = line_;
        if (i.id < 0 && i.op != Op::Store && i.op != Op::If && i.op != Op::Loop && !is_terminator(i.op))
            i.id = fn_->fresh();
        int id = i.id;
        regions_.back()->insts.push_back(std::move(i));
        return id;
    }

    int op(Op o, Type t, std::vector<int> args) {
        Inst i;
        i.op = o;
        i.type = t;
        i.args = std::move(args);
        return emit(std::move(i));
    }

    int const_f(double v) { Inst i; i.op = Op::Const; i.type = Type::float_(); i.fconst = v; return emit(std::move(i)); }
    int const_i(long long v) { Inst i; i.op = Op::Const; i.type = Type::int_(); i.iconst = v; return emit(std::move(i)); }
    int const_b(bool v) { Inst i; i.op = Op::Const; i.type = Type::bool_(); i.iconst = v; return emit(std::move(i)); }

    int splat(int scalar, Type vec) { return op(Op::Splat, vec, {scalar}); }

    int extract(int v, int index, Type t) {
        Inst i;
        i.op = Op::Extract;
        i.type = t;
        i.args = {v};
        i.imm = index;
        return emit(std::move(i));
    }

    int builtin(BuiltinFn b, Type t, std::vector<int> args) {
        Inst i;
        i.op = Op::Builtin;
        i.type = t;
        i.imm = static_cast<int>(b);
        i.args = std::move(args);
        return emit(std::move(i));
    }

    int zero(Type t) {
        if (t.is_float() && t.is_scalar()) return const_f(0.0);
        if (t.is_int())  return const_i(0);
        if (t.is_bool()) return const_b(false);
        if (t.is_vector()) return splat(const_f(0.0), t);
        int col = splat(const_f(0.0), t.column());
        return op(Op::Construct, t, std::vector<int>(t.cols, col));
    }

    // Broadcast `v` (of type `from`) to `to` if it is a scalar and `to` is a vector.
    int broadcast(int v, Type from, Type to) {
        if (from.is_scalar() && to.is_vector()) return splat(v, to);
        return v;
    }

    // ── Functions ───────────────────────────────────────────────────────

    void lower_function(const FnDecl& decl) {
        mod_.functions.emplace_back();
        fn_ = &mod_.functions.back();
        fn_->name        = decl.name;
        fn_->return_type = decl.return_type;
        fn_->stage       = decl.stage;
        fn_->line        = decl.loc.line;
        names_.clear();
        regions_ = {&fn_->body};
        line_ = decl.loc.line;

        for (size_t k = 0; k < decl.params.size(); ++k) {
            const Param& p = decl.params[k];
            fn_->param_names.push_back(p.name);
            fn_->param_types.push_back(p.type);
            if (decl.stage != Stage::None) {
                EntryParam ep;
                if (const Attribute* b = find_attr(p.attrs, "builtin")) ep.builtin = b->args[0];
                if (const Attribute* l = find_attr(p.attrs, "location")) ep.location = std::atoi(l->args[0].c_str());
                fn_->entry_params.push_back(ep);
            }
            Inst a;
            a.op   = Op::Arg;
            a.type = p.type;
            a.imm  = static_cast<int>(k);
            a.name = p.name;
            env_[&p] = emit(std::move(a));
        }

        lower_block(*decl.body);

        // Attach source names to the instructions that define `let` values.
        for_each_inst(fn_->body, [&](Inst& i) {
            auto it = names_.find(i.id);
            if (it != names_.end() && i.name.empty() && i.op != Op::Const) i.name = it->second;
        });
        fn_ = nullptr;
    }

    // Returns true if the block always terminates (later code is unreachable).
    bool lower_block(const BlockStmt& b) {
        for (const auto& s : b.stmts)
            if (lower_stmt(*s)) return true;
        return false;
    }

    bool lower_region(Region& r, const BlockStmt& b) {
        regions_.push_back(&r);
        bool t = lower_block(b);
        regions_.pop_back();
        return t;
    }

    // ── Statements ──────────────────────────────────────────────────────

    bool lower_stmt(const Stmt& s) {
        line_ = s.loc.line;
        switch (s.kind) {
            case Stmt::Kind::Let: {
                if (!s.is_var) {
                    int v = lower_expr(*s.value);
                    env_[&s] = v;
                    names_.emplace(v, s.name);
                    return false;
                }
                Inst var;
                var.op   = Op::Var;
                var.type = s.decl_type;
                var.name = s.name;
                int slot = emit(std::move(var));
                int init = s.value ? lower_expr(*s.value) : zero(s.decl_type);
                op(Op::Store, Type::void_(), {slot, init});
                env_[&s] = slot;
                return false;
            }
            case Stmt::Kind::Assign:
                lower_assign(s);
                return false;
            case Stmt::Kind::If: {
                int cond = lower_expr(*s.value);
                line_ = s.loc.line;
                Inst i;
                i.op = Op::If;
                i.args = {cond};
                i.regions.resize(2);
                bool a = lower_region(i.regions[0], *s.body);
                bool b = false;
                if (s.else_stmt) {
                    regions_.push_back(&i.regions[1]);
                    b = s.else_stmt->kind == Stmt::Kind::Block ? lower_block(*s.else_stmt->body)
                                                               : lower_stmt(*s.else_stmt);
                    regions_.pop_back();
                }
                line_ = s.loc.line;
                emit(std::move(i));
                return a && b;
            }
            case Stmt::Kind::For: {
                int start = lower_expr(*s.value);
                int end   = lower_expr(*s.end);
                line_ = s.loc.line;
                Inst var;
                var.op   = Op::Var;
                var.type = Type::int_();
                var.name = s.name;
                int slot = emit(std::move(var));
                op(Op::Store, Type::void_(), {slot, start});
                env_[&s] = slot;

                Inst loop;
                loop.op = Op::Loop;
                loop.regions.resize(3);
                loop.for_var = slot;
                loop.for_start = start;
                loop.for_end = end;
                regions_.push_back(&loop.regions[0]);
                int cur = op(Op::Load, Type::int_(), {slot});
                loop.cond = op(Op::Lt, Type::bool_(), {cur, end});
                regions_.pop_back();

                lower_region(loop.regions[1], *s.body);

                line_ = s.loc.line;
                regions_.push_back(&loop.regions[2]);
                int now  = op(Op::Load, Type::int_(), {slot});
                int next = op(Op::Add, Type::int_(), {now, const_i(1)});
                op(Op::Store, Type::void_(), {slot, next});
                regions_.pop_back();
                emit(std::move(loop));
                return false;
            }
            case Stmt::Kind::While: {
                Inst loop;
                loop.op = Op::Loop;
                loop.regions.resize(3);
                regions_.push_back(&loop.regions[0]);
                loop.cond = lower_expr(*s.value);
                regions_.pop_back();
                lower_region(loop.regions[1], *s.body);
                line_ = s.loc.line;
                emit(std::move(loop));
                return false;
            }
            case Stmt::Kind::Break:    op(Op::Break, Type::void_(), {}); return true;
            case Stmt::Kind::Continue: op(Op::Continue, Type::void_(), {}); return true;
            case Stmt::Kind::Discard:  op(Op::Discard, Type::void_(), {}); return true;
            case Stmt::Kind::Return: {
                std::vector<int> args;
                if (s.value) args.push_back(lower_expr(*s.value));
                line_ = s.loc.line;
                op(Op::Return, Type::void_(), std::move(args));
                return true;
            }
            case Stmt::Kind::ExprStmt:
                lower_expr(*s.value);
                return false;
            case Stmt::Kind::Block: {
                // A nested block is just scoping; its code joins the current region.
                return lower_block(*s.body);
            }
        }
        return false;
    }

    // `a op b` for arithmetic operators, applying broadcast / matrix rules.
    int arith(char opc, int a, Type at, int b, Type bt, Type result) {
        if (opc == '*' && (at.is_matrix() || bt.is_matrix())) {
            if (at.is_matrix() && bt.is_scalar()) return op(Op::MatScale, result, {a, b});
            if (bt.is_matrix() && at.is_scalar()) return op(Op::MatScale, result, {b, a});
            return op(Op::MatMul, result, {a, b});
        }
        a = broadcast(a, at, result);
        b = broadcast(b, bt, result);
        Op o = opc == '+' ? Op::Add : opc == '-' ? Op::Sub : opc == '*' ? Op::Mul : opc == '/' ? Op::Div : Op::Rem;
        return op(o, result, {a, b});
    }

    void lower_assign(const Stmt& s) {
        const Expr& target = *s.target;
        const Expr* root = &target;
        while (root->kind != Expr::Kind::Ident) root = root->children[0].get();
        int slot = env_.at(root->decl);
        Type vt  = root->type;
        int value = lower_expr(*s.value);
        line_ = s.loc.line;
        bool compound = s.op != "=";

        if (target.kind == Expr::Kind::Ident) {
            if (compound) {
                int old = op(Op::Load, vt, {slot});
                value = arith(s.op[0], old, vt, value, s.value->type, vt);
            }
            op(Op::Store, Type::void_(), {slot, value});
            return;
        }

        int old = op(Op::Load, vt, {slot});
        std::vector<int> lanes;
        if (target.kind == Expr::Kind::Swizzle) parse_swizzle(target.name, lanes);
        else lanes = {static_cast<int>(target.children[1]->int_value)};

        Type part_t = target.type;
        if (compound) {
            int cur;
            if (lanes.size() == 1) cur = extract(old, lanes[0], part_t);
            else {
                Inst sw;
                sw.op = Op::Swizzle; sw.type = part_t; sw.args = {old}; sw.lanes = lanes;
                cur = emit(std::move(sw));
            }
            value = arith(s.op[0], cur, part_t, value, s.value->type, part_t);
        }

        int updated;
        if (lanes.size() == 1) {
            Inst ins;
            ins.op = Op::Insert; ins.type = vt; ins.args = {old, value}; ins.imm = lanes[0];
            updated = emit(std::move(ins));
        } else {
            Inst sh;
            sh.op = Op::Shuffle;
            sh.type = vt;
            sh.args = {old, value};
            for (int c = 0; c < vt.rows; ++c) {
                int pick = c;
                for (size_t k = 0; k < lanes.size(); ++k)
                    if (lanes[k] == c) pick = vt.rows + static_cast<int>(k);
                sh.lanes.push_back(pick);
            }
            updated = emit(std::move(sh));
        }
        op(Op::Store, Type::void_(), {slot, updated});
    }

    // ── Expressions ─────────────────────────────────────────────────────

    int lower_expr(const Expr& e) {
        switch (e.kind) {
            case Expr::Kind::IntLit:   return const_i(e.int_value);
            case Expr::Kind::FloatLit: return const_f(e.float_value);
            case Expr::Kind::BoolLit:  return const_b(e.bool_value);
            case Expr::Kind::Ident:
                switch (e.sym) {
                    case SymbolKind::Let:
                    case SymbolKind::Param:
                        return env_.at(e.decl);
                    case SymbolKind::Var:
                    case SymbolKind::LoopIndex:
                        return op(Op::Load, e.type, {env_.at(e.decl)});
                    case SymbolKind::Uniform: {
                        Inst u;
                        u.op = Op::Uniform;
                        u.type = e.type;
                        u.imm = uniform_index_.at(e.decl);
                        return emit(std::move(u));
                    }
                    case SymbolKind::Const:
                        return lower_expr(*static_cast<const ConstDecl*>(e.decl)->value);
                    default:
                        break;
                }
                break;
            case Expr::Kind::Unary: {
                int v = lower_expr(*e.children[0]);
                return op(e.op == "!" ? Op::Not : Op::Neg, e.type, {v});
            }
            case Expr::Kind::Binary: return lower_binary(e);
            case Expr::Kind::Ternary: {
                int c = lower_expr(*e.children[0]);
                int a = lower_expr(*e.children[1]);
                int b = lower_expr(*e.children[2]);
                return op(Op::Select, e.type, {c, a, b});
            }
            case Expr::Kind::Call:
                if (e.builtin >= 0) return lower_builtin(e);
                {
                    std::vector<int> args;
                    for (const auto& a : e.children) args.push_back(lower_expr(*a));
                    Inst c;
                    c.op = Op::Call;
                    c.type = e.type;
                    c.callee = e.name;
                    c.args = std::move(args);
                    return emit(std::move(c));
                }
            case Expr::Kind::Construct: return lower_construct(e);
            case Expr::Kind::Swizzle: {
                int base = lower_expr(*e.children[0]);
                std::vector<int> lanes;
                parse_swizzle(e.name, lanes);
                if (lanes.size() == 1) return extract(base, lanes[0], e.type);
                Inst sw;
                sw.op = Op::Swizzle;
                sw.type = e.type;
                sw.args = {base};
                sw.lanes = lanes;
                return emit(std::move(sw));
            }
            case Expr::Kind::Index: {
                int base = lower_expr(*e.children[0]);
                const Expr& idx = *e.children[1];
                if (idx.kind == Expr::Kind::IntLit) return extract(base, static_cast<int>(idx.int_value), e.type);
                int i = lower_expr(idx);
                return op(Op::ExtractDyn, e.type, {base, i});
            }
        }
        return const_f(0.0);
    }

    int lower_binary(const Expr& e) {
        const Expr& l = *e.children[0];
        const Expr& r = *e.children[1];
        int a = lower_expr(l);
        int b = lower_expr(r);
        const std::string& o = e.op;
        if (o == "&&") return op(Op::And, e.type, {a, b});
        if (o == "||") return op(Op::Or, e.type, {a, b});
        if (o == "==") return op(Op::Eq, e.type, {a, b});
        if (o == "!=") return op(Op::Ne, e.type, {a, b});
        if (o == "<")  return op(Op::Lt, e.type, {a, b});
        if (o == "<=") return op(Op::Le, e.type, {a, b});
        if (o == ">")  return op(Op::Gt, e.type, {a, b});
        if (o == ">=") return op(Op::Ge, e.type, {a, b});
        return arith(o[0], a, l.type, b, r.type, e.type);
    }

    int lower_builtin(const Expr& e) {
        const BuiltinInfo& b = BUILTINS[e.builtin];
        std::vector<int> args;
        for (const auto& a : e.children) args.push_back(lower_expr(*a));
        Type T = b.ret == RetKind::Gen ? e.type : e.children[0]->type;
        for (int k = 0; k < b.arity; ++k)
            if (b.args[k] == ArgKind::GenOrScalar) args[k] = broadcast(args[k], e.children[k]->type, T);

        if (b.fn == BuiltinFn::Mod) {
            // mod(x, y) = x - y * floor(x / y)   (floored, like GLSL; WGSL's % truncates)
            int q  = op(Op::Div, T, {args[0], args[1]});
            int fl = builtin(BuiltinFn::Floor, T, {q});
            int m  = op(Op::Mul, T, {args[1], fl});
            return op(Op::Sub, T, {args[0], m});
        }
        return builtin(b.fn, e.type, std::move(args));
    }

    int lower_construct(const Expr& e) {
        Type t = e.construct_type;
        std::vector<int> args;
        for (const auto& a : e.children) args.push_back(lower_expr(*a));

        if (t.is_scalar()) {
            if (e.children[0]->type == t) return args[0];
            return op(Op::Convert, t, {args[0]});
        }
        if (t.is_vector()) {
            if (args.size() == 1 && e.children[0]->type.is_scalar()) return splat(args[0], t);
            if (args.size() == 1) return args[0];   // vec3(v3)
            return op(Op::Construct, t, std::move(args));
        }
        // Matrices
        int n = t.cols;
        Type col_t = t.column();
        std::vector<int> cols;
        if (args.size() == 1 && e.children[0]->type.is_scalar()) {
            int z = const_f(0.0);
            for (int c = 0; c < n; ++c) {
                std::vector<int> comps(n, z);
                comps[c] = args[0];
                cols.push_back(op(Op::Construct, col_t, comps));
            }
        } else if (static_cast<int>(args.size()) == n * n) {
            for (int c = 0; c < n; ++c)
                cols.push_back(op(Op::Construct, col_t,
                                  std::vector<int>(args.begin() + c * n, args.begin() + (c + 1) * n)));
        } else {
            cols = args;
        }
        return op(Op::Construct, t, cols);
    }
};

} // namespace

Module lower_program(const Program& prog, const Reflection& refl) {
    return Lowerer(prog, refl).run();
}

} // namespace ir
