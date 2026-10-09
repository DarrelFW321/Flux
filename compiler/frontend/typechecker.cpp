#include "frontend/typechecker.hpp"
#include "common/builtins.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <functional>

// ── Layout ───────────────────────────────────────────────────────────────────

int uniform_align(Type t) {
    if (t.is_matrix()) return 16;
    if (t.rows == 2) return 8;
    if (t.rows >= 3) return 16;
    return 4;
}

int uniform_size(Type t) {
    if (t.is_matrix()) return 16 * t.cols;
    return 4 * t.rows;
}

bool parse_swizzle(const std::string& s, std::vector<int>& out) {
    static const char* sets[] = {"xyzw", "rgba"};
    out.clear();
    if (s.empty() || s.size() > 4) return false;
    for (const char* set : sets) {
        out.clear();
        bool ok = true;
        for (char c : s) {
            const char* p = std::strchr(set, c);
            if (!p || c == '\0') { ok = false; break; }
            out.push_back(static_cast<int>(p - set));
        }
        if (ok) return true;
    }
    out.clear();
    return false;
}

// ── Scopes ───────────────────────────────────────────────────────────────────

void TypeChecker::push_scope() { scopes_.emplace_back(); }

void TypeChecker::pop_scope() {
    // Usage warnings for locals, in declaration order.
    std::vector<const Symbol*> syms;
    for (auto& [name, sym] : scopes_.back()) syms.push_back(&sym);
    std::sort(syms.begin(), syms.end(), [](const Symbol* a, const Symbol* b) {
        return a->loc.line != b->loc.line ? a->loc.line < b->loc.line : a->loc.col < b->loc.col;
    });
    for (const Symbol* sym : syms) {
        std::string name;
        for (auto& [n, s] : scopes_.back()) if (&s == sym) name = n;
        if (name.empty() || name[0] == '_') continue;
        if ((sym->kind == SymbolKind::Let || sym->kind == SymbolKind::Var) && sym->stmt) {
            if (!sym->stmt->used)
                diags_.warning(sym->loc, "unused variable '" + name + "'");
            else if (sym->kind == SymbolKind::Var && !sym->stmt->mutated)
                diags_.warning(sym->loc, "variable '" + name + "' is never reassigned; declare it with 'let'");
        } else if (sym->kind == SymbolKind::Param && sym->param && !sym->param->used) {
            diags_.warning(sym->loc, "unused parameter '" + name + "'");
        }
    }
    scopes_.pop_back();
}

void TypeChecker::declare(const std::string& name, Symbol sym) {
    auto& scope = scopes_.back();
    auto it = scope.find(name);
    if (it != scope.end()) {
        diags_.error(sym.loc, "'" + name + "' is already declared in this scope");
        return;
    }
    scope.emplace(name, sym);
}

TypeChecker::Symbol* TypeChecker::lookup(const std::string& name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) return &f->second;
    }
    return nullptr;
}

// ── Coercion helpers ─────────────────────────────────────────────────────────

static bool is_int_literal(const Expr& e) {
    if (e.kind == Expr::Kind::IntLit) return true;
    return e.kind == Expr::Kind::Unary && e.op == "-" && is_int_literal(*e.children[0]);
}

static void int_literal_to_float(Expr& e) {
    if (e.kind == Expr::Kind::IntLit) {
        e.kind = Expr::Kind::FloatLit;
        e.float_value = static_cast<double>(e.int_value);
    } else {
        int_literal_to_float(*e.children[0]);
    }
    e.type = Type::float_();
}

bool TypeChecker::coerce(Expr& e, Type target) {
    if (e.type.is_error() || target.is_error()) return true;
    if (e.type == target) return true;
    if (target == Type::float_() && e.type.is_int() && is_int_literal(e)) {
        int_literal_to_float(e);
        return true;
    }
    return false;
}

void TypeChecker::expect_type(Expr& e, Type target, const std::string& what) {
    if (!coerce(e, target))
        diags_.error(e.loc, what + ": expected '" + target.name() + "', found '" + e.type.name() + "'");
}

// ── Program ──────────────────────────────────────────────────────────────────

void TypeChecker::check(Program& prog) {
    push_scope();   // global scope

    // Functions are visible everywhere, so register them first.
    for (auto& fn : prog.functions) {
        if (find_builtin(fn->name.c_str())) {
            diags_.error(fn->name_loc, "'" + fn->name + "' is a built-in function and cannot be redefined");
            continue;
        }
        if (functions_.count(fn->name)) {
            diags_.error(fn->name_loc, "function '" + fn->name + "' is already defined");
            continue;
        }
        functions_[fn->name] = fn.get();
    }

    // Uniforms and consts in declaration order (consts may reference earlier consts).
    int offset = 0;
    for (const auto& item : prog.order) {
        if (item.kind == Program::Item::Uniform) check_uniform(*prog.uniforms[item.index], offset);
        else if (item.kind == Program::Item::Const) check_const(*prog.consts[item.index]);
    }
    reflection_.uniform_block_size = (offset + 15) / 16 * 16;

    for (auto& fn : prog.functions) check_signature(*fn);
    for (auto& fn : prog.functions) check_body(*fn);
    check_recursion();

    if (reflection_.entries.empty())
        diags_.error({1, 1, 1}, "no entry point: mark a function with @fragment or @vertex");

    scopes_.pop_back();
}

void TypeChecker::check_uniform(UniformDecl& u, int& offset) {
    if (u.type.is_void() || u.type.is_bool() ) {
        diags_.error(u.loc, "uniform '" + u.name + "' cannot have type '" + u.type.name() + "' (not host-shareable)");
        u.type = Type::error();
    } else if (u.type == Type::mat(2)) {
        diags_.error(u.loc, "mat2 uniforms are not supported (WGSL and std140 disagree on their layout); use vec4");
        u.type = Type::error();
    }

    Symbol sym;
    sym.kind = SymbolKind::Uniform;
    sym.type = u.type;
    sym.decl = &u;
    sym.loc  = u.loc;
    declare(u.name, sym);
    if (functions_.count(u.name)) diags_.error(u.loc, "'" + u.name + "' is already a function name");
    if (u.type.is_error()) return;

    UniformInfo info;
    info.name = u.name;
    info.type = u.type;
    info.line = u.loc.line;
    int align = uniform_align(u.type);
    offset = (offset + align - 1) / align * align;
    info.offset = offset;
    info.size   = uniform_size(u.type);
    offset += info.size;

    if (u.default_value) {
        in_const_init_ = true;
        check_expr(*u.default_value);
        in_const_init_ = false;
        expect_type(*u.default_value, u.type, "uniform default");
        if (auto v = eval_const(*u.default_value)) info.default_value = *v;
        else diags_.error(u.default_value->loc, "uniform default must be a constant expression");
    }
    if (info.default_value.empty()) info.default_value.assign(u.type.components(), 0.0);

    for (const auto& a : u.attrs) {
        if (a.name == "range") {
            if (a.args.size() != 2 || !u.type.is_float())
                diags_.error(a.loc, "@range(min, max) takes two numbers and applies to float uniforms");
            else {
                info.has_range = true;
                info.range_min = std::strtod(a.args[0].c_str(), nullptr);
                info.range_max = std::strtod(a.args[1].c_str(), nullptr);
            }
        } else if (a.name == "color") {
            if (u.type != Type::vec(3) && u.type != Type::vec(4))
                diags_.error(a.loc, "@color applies to vec3 or vec4 uniforms");
            else info.color = true;
        } else {
            diags_.warning(a.loc, "unknown uniform attribute '@" + a.name + "'");
        }
    }
    reflection_.uniforms.push_back(std::move(info));
}

void TypeChecker::check_const(ConstDecl& c) {
    in_const_init_ = true;
    Type t = check_expr(*c.value);
    in_const_init_ = false;
    if (c.has_type) {
        expect_type(*c.value, c.type, "constant '" + c.name + "'");
    } else {
        c.type = t;
    }
    if (c.type.is_void()) {
        diags_.error(c.loc, "constant '" + c.name + "' has no value");
        c.type = Type::error();
    }
    if (!c.type.is_error()) {
        if (auto v = eval_const(*c.value)) const_values_[&c] = *v;
    }
    Symbol sym;
    sym.kind = SymbolKind::Const;
    sym.type = c.type;
    sym.decl = &c;
    sym.loc  = c.loc;
    declare(c.name, sym);
    if (functions_.count(c.name)) diags_.error(c.loc, "'" + c.name + "' is already a function name");
}

// ── Functions ────────────────────────────────────────────────────────────────

void TypeChecker::check_signature(FnDecl& fn) {
    for (const auto& a : fn.attrs) {
        Stage s = a.name == "fragment" ? Stage::Fragment
                : a.name == "vertex"   ? Stage::Vertex
                                       : Stage::None;
        if (s == Stage::None) {
            diags_.error(a.loc, "unknown function attribute '@" + a.name + "' (expected @fragment or @vertex)");
        } else if (fn.stage != Stage::None) {
            diags_.error(a.loc, "a function can only have one stage attribute");
        } else {
            fn.stage = s;
        }
    }
    if (fn.stage != Stage::None) {
        check_entry_interface(fn);
        reflection_.entries.push_back({fn.name, fn.stage});
        return;
    }
    for (const auto& p : fn.params)
        if (!p.attrs.empty())
            diags_.error(p.attrs[0].loc, "parameter attributes are only allowed on @fragment/@vertex entry points");
    if (!fn.return_attrs.empty())
        diags_.error(fn.return_attrs[0].loc, "return attributes are only allowed on entry points");
    for (const auto& p : fn.params)
        if (p.type.is_void()) diags_.error(p.loc, "parameters cannot be 'void'");
}

void TypeChecker::check_entry_interface(FnDecl& fn) {
    bool frag = fn.stage == Stage::Fragment;
    std::unordered_set<int> locations;
    std::unordered_set<std::string> builtins;

    for (auto& p : fn.params) {
        const Attribute* b = find_attr(p.attrs, "builtin");
        const Attribute* l = find_attr(p.attrs, "location");
        if ((b != nullptr) == (l != nullptr)) {
            diags_.error(p.loc, "entry-point parameter '" + p.name +
                         "' needs exactly one of @builtin(...) or @location(n)");
            continue;
        }
        if (b) {
            if (b->args.size() != 1) { diags_.error(b->loc, "@builtin takes one name"); continue; }
            const std::string& name = b->args[0];
            Type want = Type::error();
            if (frag && name == "position")        want = Type::vec(4);
            else if (frag && name == "front_facing") want = Type::bool_();
            else if (!frag && (name == "vertex_index" || name == "instance_index")) want = Type::int_();
            if (want.is_error()) {
                diags_.error(b->loc, std::string("'") + name + "' is not a " + stage_name(fn.stage) +
                             " input builtin (" + (frag ? "position, front_facing" : "vertex_index, instance_index") + ")");
                continue;
            }
            if (p.type != want)
                diags_.error(p.loc, "@builtin(" + name + ") must have type '" + want.name() + "'");
            if (!builtins.insert(name).second)
                diags_.error(b->loc, "@builtin(" + name + ") is bound twice");
        } else {
            int loc = l->args.size() == 1 ? std::atoi(l->args[0].c_str()) : -1;
            if (loc < 0 || loc > 15) { diags_.error(l->loc, "@location expects an index in 0..15"); continue; }
            if (!p.type.is_float_scalar_or_vector())
                diags_.error(p.loc, "@location inputs must be float or vecN");
            if (!locations.insert(loc).second)
                diags_.error(l->loc, "@location(" + std::to_string(loc) + ") is bound twice");
        }
    }

    if (fn.return_type != Type::vec(4))
        diags_.error(fn.name_loc, std::string("@") + stage_name(fn.stage) + " entry point must return vec4 (" +
                     (frag ? "the colour written to @location(0)" : "the clip-space @builtin(position)") + ")");
    for (const auto& a : fn.return_attrs) {
        bool ok = frag ? (a.name == "location" && a.args.size() == 1 && a.args[0] == "0")
                       : (a.name == "builtin" && a.args.size() == 1 && a.args[0] == "position");
        if (!ok) diags_.error(a.loc, frag ? "fragment outputs are always @location(0)"
                                          : "vertex outputs are always @builtin(position)");
    }
}

void TypeChecker::check_body(FnDecl& fn) {
    current_fn_ = &fn;
    loop_depth_ = 0;
    push_scope();
    for (auto& p : fn.params) {
        Symbol sym;
        sym.kind  = SymbolKind::Param;
        sym.type  = p.type;
        sym.decl  = &p;
        sym.param = &p;
        sym.loc   = p.loc;
        declare(p.name, sym);
    }
    Flow flow = check_block(*fn.body, false);
    if (!fn.return_type.is_void() && flow != Flow::Returns)
        diags_.error(fn.name_loc, "function '" + fn.name + "' must return a '" +
                     fn.return_type.name() + "' on every path");
    pop_scope();
    current_fn_ = nullptr;
}

void TypeChecker::check_recursion() {
    // DFS over the call graph; any back edge is recursion, which GPUs can't run.
    std::unordered_map<const FnDecl*, int> state;   // 0 new, 1 on stack, 2 done
    std::function<bool(const FnDecl*)> visit = [&](const FnDecl* f) -> bool {
        state[f] = 1;
        for (const FnDecl* g : calls_[f]) {
            if (state[g] == 1) {
                diags_.error(g->name_loc, "function '" + g->name + "' is recursive; shaders cannot recurse");
                return true;
            }
            if (state[g] == 0 && visit(g)) return true;
        }
        state[f] = 2;
        return false;
    };
    for (auto& [name, fn] : functions_)
        if (state[fn] == 0 && visit(fn)) return;
}

// ── Statements ───────────────────────────────────────────────────────────────

TypeChecker::Flow TypeChecker::check_block(BlockStmt& block, bool new_scope) {
    if (new_scope) push_scope();
    Flow flow = Flow::Normal;
    bool warned = false;
    for (auto& s : block.stmts) {
        if (flow != Flow::Normal && !warned) {
            diags_.warning(s->loc, "unreachable code");
            warned = true;
        }
        Flow f = check_stmt(*s);
        if (flow == Flow::Normal) flow = f;
    }
    if (new_scope) pop_scope();
    return flow;
}

TypeChecker::Flow TypeChecker::check_stmt(Stmt& s) {
    switch (s.kind) {
        case Stmt::Kind::Let: {
            Type t = s.value ? check_expr(*s.value) : s.decl_type;
            if (s.value) {
                if (s.has_type) expect_type(*s.value, s.decl_type, "initializer of '" + s.name + "'");
                else            s.decl_type = t;
            }
            if (s.decl_type.is_void()) {
                diags_.error(s.name_loc, "cannot bind '" + s.name + "' to a value of type 'void'");
                s.decl_type = Type::error();
            }
            Symbol sym;
            sym.kind = s.is_var ? SymbolKind::Var : SymbolKind::Let;
            sym.type = s.decl_type;
            sym.decl = &s;
            sym.stmt = &s;
            sym.loc  = s.name_loc;
            declare(s.name, sym);
            return Flow::Normal;
        }
        case Stmt::Kind::Assign:
            check_assign(s);
            return Flow::Normal;
        case Stmt::Kind::If: {
            check_expr(*s.value);
            expect_type(*s.value, Type::bool_(), "if condition");
            Flow a = check_block(*s.body);
            Flow b = Flow::Normal;
            if (s.else_stmt) b = check_stmt(*s.else_stmt);
            if (a == Flow::Returns && b == Flow::Returns) return Flow::Returns;
            if (a != Flow::Normal && b != Flow::Normal) return Flow::Jumps;
            return Flow::Normal;
        }
        case Stmt::Kind::For: {
            check_expr(*s.value);
            check_expr(*s.end);
            expect_type(*s.value, Type::int_(), "range start");
            expect_type(*s.end, Type::int_(), "range end");
            push_scope();
            Symbol sym;
            sym.kind = SymbolKind::LoopIndex;
            sym.type = Type::int_();
            sym.decl = &s;
            sym.loc  = s.name_loc;
            declare(s.name, sym);
            ++loop_depth_;
            check_block(*s.body);
            --loop_depth_;
            pop_scope();
            return Flow::Normal;
        }
        case Stmt::Kind::While:
            check_expr(*s.value);
            expect_type(*s.value, Type::bool_(), "while condition");
            ++loop_depth_;
            check_block(*s.body);
            --loop_depth_;
            return Flow::Normal;
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            if (loop_depth_ == 0)
                diags_.error(s.loc, std::string("'") + (s.kind == Stmt::Kind::Break ? "break" : "continue") +
                             "' outside of a loop");
            return Flow::Jumps;
        case Stmt::Kind::Discard:
            if (current_fn_->stage != Stage::Fragment)
                diags_.error(s.loc, "'discard' is only allowed in a @fragment entry point");
            return Flow::Returns;
        case Stmt::Kind::Return: {
            Type want = current_fn_->return_type;
            if (s.value) {
                check_expr(*s.value);
                if (want.is_void()) diags_.error(s.value->loc, "function '" + current_fn_->name + "' returns no value");
                else expect_type(*s.value, want, "return value");
            } else if (!want.is_void()) {
                diags_.error(s.loc, "missing return value of type '" + want.name() + "'");
            }
            return Flow::Returns;
        }
        case Stmt::Kind::ExprStmt: {
            check_expr(*s.value);
            if (s.value->kind != Expr::Kind::Call)
                diags_.warning(s.loc, "expression result is unused");
            else if (!s.value->type.is_void() && !s.value->type.is_error())
                diags_.warning(s.loc, "result of '" + s.value->name + "' is unused (functions have no side effects)");
            return Flow::Normal;
        }
        case Stmt::Kind::Block:
            return check_block(*s.body);
    }
    return Flow::Normal;
}

void TypeChecker::check_assign(Stmt& s) {
    Type rhs = check_expr(*s.value);
    (void)rhs;
    Expr& target = *s.target;

    // Find the root variable: x, x.yz, x[i], x[i].y ...
    Expr* root = &target;
    std::vector<Expr*> path;
    while (root->kind == Expr::Kind::Swizzle || root->kind == Expr::Kind::Index) {
        path.push_back(root);
        root = root->children[0].get();
    }
    if (root->kind != Expr::Kind::Ident) {
        diags_.error(target.loc, "left side of assignment must be a variable, swizzle or index");
        return;
    }
    Type t = check_expr(target);
    if (t.is_error()) return;

    if (root->sym != SymbolKind::Var) {
        const char* what = root->sym == SymbolKind::Let       ? "a 'let' binding"
                         : root->sym == SymbolKind::Param     ? "a parameter"
                         : root->sym == SymbolKind::Uniform   ? "a uniform"
                         : root->sym == SymbolKind::Const     ? "a constant"
                         : root->sym == SymbolKind::LoopIndex ? "a loop index"
                                                              : "not a variable";
        diags_.error(root->loc, "cannot assign to '" + root->name + "': it is " + what +
                     (root->sym == SymbolKind::Let ? " (use 'var')" : ""));
        return;
    }
    static_cast<const Stmt*>(root->decl)->mutated = true;

    for (Expr* p : path) {
        if (p->kind == Expr::Kind::Swizzle) {
            std::vector<int> idx;
            parse_swizzle(p->name, idx);
            for (size_t i = 0; i < idx.size(); ++i)
                for (size_t j = i + 1; j < idx.size(); ++j)
                    if (idx[i] == idx[j])
                        diags_.error(p->loc, "cannot assign to swizzle '" + p->name + "' with repeated components");
        }
    }
    if (target.kind == Expr::Kind::Index && target.children[1]->kind != Expr::Kind::IntLit)
        diags_.error(target.children[1]->loc, "assignment through an index needs a constant integer index");
    if (path.size() > 1)
        diags_.error(target.loc, "only one level of swizzle or index is supported on the left of '='");

    if (s.op == "=") {
        expect_type(*s.value, t, "assignment");
        return;
    }
    // Compound assignment: `x op= v` must type-check as `x = x op v`.
    Type result = binary_type(s.op.substr(0, 1), target, *s.value, s.loc);
    if (!result.is_error() && result != t)
        diags_.error(s.loc, "'" + s.op + "' would change the type of '" + root->name + "' from '" +
                     t.name() + "' to '" + result.name() + "'");
}

// ── Expressions ──────────────────────────────────────────────────────────────

Type TypeChecker::check_expr(Expr& e) {
    Type t = Type::error();
    switch (e.kind) {
        case Expr::Kind::IntLit:   t = Type::int_(); break;
        case Expr::Kind::FloatLit: t = Type::float_(); break;
        case Expr::Kind::BoolLit:  t = Type::bool_(); break;
        case Expr::Kind::Ident: {
            Symbol* sym = lookup(e.name);
            if (!sym) {
                if (functions_.count(e.name) || find_builtin(e.name.c_str()))
                    diags_.error(e.loc, "'" + e.name + "' is a function; call it with '(...)'");
                else
                    diags_.error(e.loc, "unknown name '" + e.name + "'");
                break;
            }
            if (in_const_init_ && sym->kind != SymbolKind::Const) {
                diags_.error(e.loc, "constant initializers may only reference other constants");
                break;
            }
            e.sym  = sym->kind;
            e.decl = sym->decl;
            if (sym->stmt)  sym->stmt->used = true;
            if (sym->param) sym->param->used = true;
            t = sym->type;
            break;
        }
        case Expr::Kind::Unary:     t = check_unary(e); break;
        case Expr::Kind::Binary:    t = check_binary(e); break;
        case Expr::Kind::Ternary:   t = check_ternary(e); break;
        case Expr::Kind::Call:      t = check_call(e); break;
        case Expr::Kind::Construct: t = check_construct(e); break;
        case Expr::Kind::Swizzle:   t = check_swizzle(e); break;
        case Expr::Kind::Index:     t = check_index(e); break;
    }
    e.type = t;
    return t;
}

Type TypeChecker::check_unary(Expr& e) {
    Type t = check_expr(*e.children[0]);
    if (t.is_error()) return t;
    if (e.op == "!") {
        if (!t.is_bool()) { diags_.error(e.loc, "'!' expects 'bool', found '" + t.name() + "'"); return Type::error(); }
        return t;
    }
    if (t.is_bool() || t.is_matrix() || t.is_void()) {
        diags_.error(e.loc, "cannot negate a value of type '" + t.name() + "'");
        return Type::error();
    }
    return t;
}

Type TypeChecker::check_binary(Expr& e) {
    check_expr(*e.children[0]);
    check_expr(*e.children[1]);
    return binary_type(e.op, *e.children[0], *e.children[1], e.loc);
}

Type TypeChecker::binary_type(const std::string& op, Expr& l, Expr& r, SourceLoc loc) {
    Type lt = l.type;
    Type rt = r.type;
    if (lt.is_error() || rt.is_error()) return Type::error();

    auto fail = [&]() {
        diags_.error(loc, "operator '" + op + "' cannot be applied to '" + lt.name() + "' and '" + rt.name() + "'");
        return Type::error();
    };

    // Let int literals adopt the float-ness of the other side: `x * 2`.
    if (lt.is_float() && rt.is_int() && is_int_literal(r)) { coerce(r, Type::float_()); rt = r.type; }
    if (rt.is_float() && lt.is_int() && is_int_literal(l)) { coerce(l, Type::float_()); lt = l.type; }

    if (op == "&&" || op == "||") {
        if (!lt.is_bool() || !rt.is_bool()) return fail();
        return Type::bool_();
    }
    if (op == "==" || op == "!=") {
        if (!lt.is_scalar() || lt != rt) {
            if (lt.is_vector() && lt == rt)
                diags_.error(loc, "vectors can't be compared with '" + op + "'; compare components or use distance()");
            else fail();
            return Type::error();
        }
        return Type::bool_();
    }
    if (op == "<" || op == ">" || op == "<=" || op == ">=") {
        if (!lt.is_numeric_scalar() || lt != rt) return fail();
        return Type::bool_();
    }

    // Arithmetic: + - * / %
    if (lt.is_bool() || rt.is_bool()) return fail();
    if (lt.is_matrix() || rt.is_matrix()) {
        if (op != "*") {
            diags_.error(loc, "only '*' is defined for matrices");
            return Type::error();
        }
        if (lt.is_matrix() && rt == lt) return lt;                              // mat * mat
        if (lt.is_matrix() && rt == Type::vec(lt.cols)) return rt;              // mat * vec
        if (rt.is_matrix() && lt == Type::vec(rt.rows)) return lt;              // vec * mat
        if (lt.is_matrix() && rt == Type::float_()) return lt;                  // mat * s
        if (rt.is_matrix() && lt == Type::float_()) return rt;                  // s * mat
        return fail();
    }
    if (lt == rt) return lt;
    // Scalar ⇄ vector broadcast.
    if (lt.is_vector() && rt == Type::float_()) return lt;
    if (rt.is_vector() && lt == Type::float_()) return rt;
    if ((lt.is_int() && rt.is_float()) || (lt.is_float() && rt.is_int()))
        diags_.error(loc, "operator '" + op + "' mixes 'int' and 'float'; convert explicitly with float(...) or int(...)");
    else
        fail();
    return Type::error();
}

Type TypeChecker::check_ternary(Expr& e) {
    check_expr(*e.children[0]);
    Type a = check_expr(*e.children[1]);
    Type b = check_expr(*e.children[2]);
    expect_type(*e.children[0], Type::bool_(), "condition of '?:'");
    if (a.is_error() || b.is_error()) return Type::error();
    if (a != b) {
        if (coerce(*e.children[2], a)) return a;
        if (coerce(*e.children[1], b)) return b;
        diags_.error(e.loc, "branches of '?:' have different types '" + a.name() + "' and '" + b.name() + "'");
        return Type::error();
    }
    return a;
}

Type TypeChecker::check_call(Expr& e) {
    if (const BuiltinInfo* b = find_builtin(e.name.c_str()))
        return check_builtin(e, static_cast<int>(b->fn));

    for (auto& a : e.children) check_expr(*a);
    if (in_const_init_) {
        diags_.error(e.loc, "constant initializers cannot call user functions");
        return Type::error();
    }
    auto it = functions_.find(e.name);
    if (it == functions_.end()) {
        diags_.error(e.loc, "unknown function '" + e.name + "'");
        return Type::error();
    }
    FnDecl* fn = it->second;
    e.callee = fn;
    if (fn->stage != Stage::None) {
        diags_.error(e.loc, "entry point '" + fn->name + "' cannot be called");
        return Type::error();
    }
    if (current_fn_) calls_[current_fn_].insert(fn);
    if (e.children.size() != fn->params.size()) {
        diags_.error(e.loc, "'" + fn->name + "' expects " + std::to_string(fn->params.size()) +
                     " argument(s), got " + std::to_string(e.children.size()));
        return fn->return_type;
    }
    for (size_t i = 0; i < e.children.size(); ++i)
        expect_type(*e.children[i], fn->params[i].type,
                    "argument " + std::to_string(i + 1) + " of '" + fn->name + "'");
    return fn->return_type;
}

Type TypeChecker::check_builtin(Expr& e, int id) {
    const BuiltinInfo& b = BUILTINS[id];
    e.builtin = id;
    for (auto& a : e.children) check_expr(*a);
    for (auto& a : e.children) if (a->type.is_error()) return Type::error();

    if (static_cast<int>(e.children.size()) != b.arity) {
        diags_.error(e.loc, std::string("'") + b.name + "' expects " + std::to_string(b.arity) +
                     " argument(s), got " + std::to_string(e.children.size()));
        return Type::error();
    }

    // Matrix-only builtins.
    if (b.args[0] == ArgKind::Mat) {
        Type m = e.children[0]->type;
        if (!m.is_matrix()) {
            diags_.error(e.children[0]->loc, std::string("'") + b.name + "' expects a matrix");
            return Type::error();
        }
        return b.ret == RetKind::Mat ? m : Type::float_();
    }
    if (b.args[0] == ArgKind::Vec3) {
        for (auto& a : e.children) expect_type(*a, Type::vec(3), std::string("argument of '") + b.name + "'");
        return Type::vec(3);
    }

    // Determine the generic type T from the exact-T arguments, falling back to
    // the broadcastable ones (all-scalar calls like `clamp(x, 0.0, 1.0)`).
    Type T = Type::error();
    for (int pass = 0; pass < 2 && T.is_error(); ++pass) {
        for (int i = 0; i < b.arity; ++i) {
            ArgKind k = b.args[i];
            if ((pass == 0 && k == ArgKind::Gen) || (pass == 1 && k == ArgKind::GenOrScalar)) {
                Type at = e.children[i]->type;
                if (is_int_literal(*e.children[i])) continue;   // literals adapt to T
                if (T.is_error() || (T.is_scalar() && at.is_vector())) T = at;
            }
        }
    }
    if (T.is_error()) T = Type::float_();   // e.g. abs(-3) with only literals → float

    bool int_ok = b.allows_int && T == Type::int_();
    if (!T.is_float_scalar_or_vector() && !int_ok) {
        diags_.error(e.loc, std::string("'") + b.name + "' expects float or vecN arguments" +
                     (b.allows_int ? " (or int)" : "") + ", found '" + T.name() + "'");
        return Type::error();
    }

    for (int i = 0; i < b.arity; ++i) {
        Expr& a = *e.children[i];
        std::string what = "argument " + std::to_string(i + 1) + " of '" + b.name + "'";
        switch (b.args[i]) {
            case ArgKind::Gen:
                expect_type(a, T, what);
                break;
            case ArgKind::GenOrScalar:
                if (!coerce(a, T) && !coerce(a, T.scalar()))
                    diags_.error(a.loc, what + ": expected '" + T.name() + "' or '" + T.scalar().name() +
                                 "', found '" + a.type.name() + "'");
                break;
            case ArgKind::Float:
                expect_type(a, Type::float_(), what);
                break;
            default: break;
        }
    }
    if (b.fn == BuiltinFn::Dot && !T.is_vector()) {
        diags_.error(e.loc, "'dot' expects vector arguments");
        return Type::error();
    }
    return b.ret == RetKind::Float ? Type::float_() : T;
}

Type TypeChecker::check_construct(Expr& e) {
    Type t = e.construct_type;
    for (auto& a : e.children) check_expr(*a);
    for (auto& a : e.children) if (a->type.is_error()) return Type::error();
    auto& args = e.children;

    if (t.is_void()) { diags_.error(e.loc, "cannot construct 'void'"); return Type::error(); }

    if (t.is_scalar()) {
        if (args.size() != 1) {
            diags_.error(e.loc, "'" + t.name() + "(...)' takes exactly one argument");
            return Type::error();
        }
        Type a = args[0]->type;
        bool ok = t.is_bool() ? a.is_bool() : a.is_numeric_scalar();
        if (!ok) {
            diags_.error(e.loc, "cannot convert '" + a.name() + "' to '" + t.name() + "'");
            return Type::error();
        }
        return t;
    }

    // Vectors and matrices take floats; int literals coerce.
    for (auto& a : args) coerce(*a, Type::float_());

    if (t.is_vector()) {
        if (args.size() == 1 && args[0]->type == Type::float_()) return t;   // splat
        int total = 0;
        for (auto& a : args) {
            Type at = a->type;
            if (!at.is_float_scalar_or_vector()) {
                diags_.error(a->loc, "'" + t.name() + "' components must be float or vecN, found '" + at.name() + "'" +
                             (at.is_int() ? " (use float(...))" : ""));
                return Type::error();
            }
            total += at.rows;
        }
        if (total != t.rows) {
            diags_.error(e.loc, "'" + t.name() + "' needs " + std::to_string(t.rows) + " components, got " +
                         std::to_string(total));
            return Type::error();
        }
        return t;
    }

    // Matrices: matN(diagonal), matN(N column vectors) or matN(N*N floats).
    int n = t.cols;
    if (args.size() == 1 && args[0]->type == Type::float_()) return t;
    bool all_cols = static_cast<int>(args.size()) == n;
    for (auto& a : args) all_cols = all_cols && a->type == Type::vec(n);
    bool all_scalars = static_cast<int>(args.size()) == n * n;
    for (auto& a : args) all_scalars = all_scalars && a->type == Type::float_();
    if (!all_cols && !all_scalars) {
        diags_.error(e.loc, "'" + t.name() + "' takes one float (diagonal), " + std::to_string(n) + " vec" +
                     std::to_string(n) + " columns, or " + std::to_string(n * n) + " floats");
        return Type::error();
    }
    return t;
}

Type TypeChecker::check_swizzle(Expr& e) {
    Type base = check_expr(*e.children[0]);
    if (base.is_error()) return base;
    if (!base.is_vector()) {
        diags_.error(e.loc, "cannot swizzle '." + e.name + "' on '" + base.name() + "' (only vectors have components)");
        return Type::error();
    }
    std::vector<int> idx;
    if (!parse_swizzle(e.name, idx)) {
        diags_.error(e.loc, "invalid swizzle '." + e.name + "' (use up to 4 of xyzw or rgba, not mixed)");
        return Type::error();
    }
    for (int i : idx)
        if (i >= base.rows) {
            diags_.error(e.loc, "swizzle '." + e.name + "' reads past the end of a '" + base.name() + "'");
            return Type::error();
        }
    return idx.size() == 1 ? Type::float_() : Type::vec(static_cast<int>(idx.size()));
}

Type TypeChecker::check_index(Expr& e) {
    Type base = check_expr(*e.children[0]);
    Type it   = check_expr(*e.children[1]);
    if (base.is_error() || it.is_error()) return Type::error();
    if (!it.is_int()) {
        diags_.error(e.children[1]->loc, "index must be 'int', found '" + it.name() + "'");
        return Type::error();
    }
    if (!base.is_vector() && !base.is_matrix()) {
        diags_.error(e.loc, "cannot index a value of type '" + base.name() + "'");
        return Type::error();
    }
    int n = base.is_matrix() ? base.cols : base.rows;
    if (base.is_matrix() && e.children[1]->kind != Expr::Kind::IntLit) {
        diags_.error(e.children[1]->loc, "matrix columns must be indexed with a constant integer");
        return Type::error();
    }
    if (e.children[1]->kind == Expr::Kind::IntLit) {
        long long i = e.children[1]->int_value;
        if (i < 0 || i >= n) {
            diags_.error(e.children[1]->loc, "index " + std::to_string(i) + " is out of bounds for '" + base.name() + "'");
            return Type::error();
        }
    }
    return base.is_matrix() ? base.column() : Type::float_();
}

// ── Constant evaluation (uniform defaults, reflection) ───────────────────────

std::optional<std::vector<double>> TypeChecker::eval_const(const Expr& e) {
    using V = std::vector<double>;
    switch (e.kind) {
        case Expr::Kind::IntLit:   return V{static_cast<double>(e.int_value)};
        case Expr::Kind::FloatLit: return V{e.float_value};
        case Expr::Kind::BoolLit:  return V{e.bool_value ? 1.0 : 0.0};
        case Expr::Kind::Ident: {
            if (e.sym != SymbolKind::Const) return std::nullopt;
            auto it = const_values_.find(static_cast<const ConstDecl*>(e.decl));
            if (it == const_values_.end()) return std::nullopt;
            return it->second;
        }
        case Expr::Kind::Unary: {
            auto v = eval_const(*e.children[0]);
            if (!v || e.op != "-") return std::nullopt;
            for (double& x : *v) x = -x;
            return v;
        }
        case Expr::Kind::Binary: {
            auto a = eval_const(*e.children[0]);
            auto b = eval_const(*e.children[1]);
            if (!a || !b || e.type.is_matrix() || e.type.is_bool()) return std::nullopt;
            if (e.children[0]->type.is_matrix() || e.children[1]->type.is_matrix()) return std::nullopt;
            size_t n = std::max(a->size(), b->size());
            V out(n);
            for (size_t i = 0; i < n; ++i) {
                double x = (*a)[a->size() == 1 ? 0 : i], y = (*b)[b->size() == 1 ? 0 : i];
                char op = e.op[0];
                if (op == '+') out[i] = x + y;
                else if (op == '-') out[i] = x - y;
                else if (op == '*') out[i] = x * y;
                else if (op == '/') {
                    if (y == 0.0) return std::nullopt;
                    out[i] = e.type.base == Type::Base::Int ? std::trunc(x / y) : x / y;
                } else return std::nullopt;
            }
            return out;
        }
        case Expr::Kind::Construct: {
            Type t = e.construct_type;
            V out;
            for (auto& a : e.children) {
                auto v = eval_const(*a);
                if (!v) return std::nullopt;
                out.insert(out.end(), v->begin(), v->end());
            }
            if (t.is_scalar()) {
                if (out.size() != 1) return std::nullopt;
                if (t.is_int()) out[0] = std::trunc(out[0]);
                return out;
            }
            if (out.size() == 1) {
                if (t.is_vector()) return V(t.rows, out[0]);
                V m(t.components(), 0.0);
                for (int i = 0; i < t.cols; ++i) m[i * t.rows + i] = out[0];
                return m;
            }
            return out;
        }
        case Expr::Kind::Swizzle: {
            auto v = eval_const(*e.children[0]);
            std::vector<int> idx;
            if (!v || !parse_swizzle(e.name, idx)) return std::nullopt;
            V out;
            for (int i : idx) out.push_back((*v)[i]);
            return out;
        }
        default:
            return std::nullopt;
    }
}
