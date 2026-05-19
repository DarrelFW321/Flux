#include "frontend/typechecker.hpp"
#include <stdexcept>

// ── Helpers ──────────────────────────────────────────────────────────────────

void TypeChecker::push_scope() { scopes_.emplace_back(); }
void TypeChecker::pop_scope()  { scopes_.pop_back(); }

void TypeChecker::declare(const std::string& name, Type t, int line, int col) {
    auto& top = scopes_.back();
    if (top.count(name))
        error("'" + name + "' already declared in this scope", line, col);
    top[name] = t;
}

Type TypeChecker::lookup(const std::string& name, int line, int col) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return found->second;
    }
    error("undefined variable '" + name + "'", line, col);
}

[[noreturn]] void TypeChecker::error(const std::string& msg, int line, int col) {
    throw std::runtime_error(
        "[line " + std::to_string(line) + ":" + std::to_string(col) +
        "] Type error: " + msg);
}

// ── Entry point ───────────────────────────────────────────────────────────────

void TypeChecker::check(const Program& prog) {
    first_pass(prog);

    // Functions get their own scope inside check_fn.
    // Top-level statements share a single global scope (so `let x` declared at
    // file scope is visible to subsequent statements).
    bool global_scope_open = false;
    for (const auto& item : prog.items) {
        std::visit([this, &global_scope_open](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, FnDecl>) {
                check_fn(v);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<Stmt>>) {
                if (!global_scope_open) { push_scope(); global_scope_open = true; }
                check_stmt(*v);
            }
        }, item);
    }
    if (global_scope_open) pop_scope();
}

// First pass: collect all function signatures so forward calls are valid.
void TypeChecker::first_pass(const Program& prog) {
    for (const auto& item : prog.items) {
        if (const auto* fn = std::get_if<FnDecl>(&item)) {
            if (functions_.count(fn->name))
                error("function '" + fn->name + "' already defined", fn->line, fn->col);
            FnSignature sig;
            sig.return_type = Type::parse(fn->return_type);
            for (const auto& p : fn->params)
                sig.param_types.push_back(Type::parse(p.type_name));
            functions_[fn->name] = std::move(sig);
        }
    }
}

// Extract a compile-time integer value from a const-expression, if possible.
// Handles plain literals and unary-minus literals (`-3`).
static std::optional<int64_t> const_int(const Expr& e) {
    if (auto* lit = std::get_if<IntLitExpr>(&e.data))
        return lit->value;
    if (auto* u = std::get_if<UnaryExpr>(&e.data); u && u->op == "-") {
        if (auto* lit = std::get_if<IntLitExpr>(&u->operand->data))
            return -lit->value;
    }
    return std::nullopt;
}

// ── Functions ─────────────────────────────────────────────────────────────────

void TypeChecker::check_fn(const FnDecl& fn) {
    current_return_type_ = Type::parse(fn.return_type);
    push_scope();
    for (const auto& p : fn.params)
        declare(p.name, Type::parse(p.type_name), fn.line, fn.col);
    check_block(*fn.body);
    pop_scope();
}

void TypeChecker::check_block(const BlockStmt& block) {
    push_scope();
    for (const auto& s : block.stmts) check_stmt(*s);
    pop_scope();
}

// ── Statements ────────────────────────────────────────────────────────────────

void TypeChecker::check_stmt(const Stmt& s) {
    std::visit([this, &s](const auto& v) {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, LetStmt>) {
            Type decl_t = Type::parse(v.type_name);
            Type init_t = check_expr(*v.init);
            if (decl_t != init_t)
                error("let '" + v.name + "': declared " + decl_t.name() +
                      " but initializer is " + init_t.name(), s.line, s.col);
            declare(v.name, decl_t, s.line, s.col);

        } else if constexpr (std::is_same_v<T, AssignStmt>) {
            Type var_t = lookup(v.name, s.line, s.col);
            Type val_t = check_expr(*v.value);
            if (var_t.is_array())
                error("cannot assign to whole array '" + v.name +
                      "'; assign per-element with " + v.name + "[i] = ...", s.line, s.col);
            if (var_t != val_t)
                error("assignment to '" + v.name + "': expected " + var_t.name() +
                      ", got " + val_t.name(), s.line, s.col);

        } else if constexpr (std::is_same_v<T, IndexAssignStmt>) {
            Type arr_t = check_expr(*v.array);
            if (!arr_t.is_array())
                error("indexed assignment target must be an array, got " + arr_t.name(),
                      s.line, s.col);
            Type idx_t = check_expr(*v.index);
            if (idx_t != Type::scalar(Type::Kind::Int))
                error("array index must be int, got " + idx_t.name(), s.line, s.col);
            if (auto ci = const_int(*v.index)) {
                if (*ci < 0 || *ci >= arr_t.array_size)
                    error("index " + std::to_string(*ci) + " out of bounds for " +
                          arr_t.name() + " (size " + std::to_string(arr_t.array_size) + ")",
                          s.line, s.col);
            }
            Type val_t = check_expr(*v.value);
            if (val_t != arr_t.elem())
                error("element assignment: array holds " + arr_t.elem().name() +
                      " but value is " + val_t.name(), s.line, s.col);

        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
            Type ret_t = check_expr(*v.value);
            if (ret_t != current_return_type_)
                error("return type mismatch: expected " + current_return_type_.name() +
                      ", got " + ret_t.name(), s.line, s.col);

        } else if constexpr (std::is_same_v<T, PrintStmt>) {
            Type t = check_expr(*v.value);
            if (t.is_array())
                error("cannot print an array directly; print elements individually",
                      s.line, s.col);
            if (t.kind == Type::Kind::Void)
                error("cannot print a void expression", s.line, s.col);

        } else if constexpr (std::is_same_v<T, IfStmt>) {
            Type cond_t = check_expr(*v.condition);
            if (cond_t != Type::scalar(Type::Kind::Bool))
                error("if condition must be bool, got " + cond_t.name(), s.line, s.col);
            check_block(*v.then_block);
            if (v.else_block) check_block(**v.else_block);

        } else if constexpr (std::is_same_v<T, WhileStmt>) {
            Type cond_t = check_expr(*v.condition);
            if (cond_t != Type::scalar(Type::Kind::Bool))
                error("while condition must be bool, got " + cond_t.name(), s.line, s.col);
            check_block(*v.body);

        } else if constexpr (std::is_same_v<T, ExprStmt>) {
            check_expr(*v.expr);
        }
    }, s.data);
}

// ── Built-ins: sum(arr), dot(a, b) ────────────────────────────────────────────

Type TypeChecker::check_builtin_call(const CallExpr& v, int line, int col) const {
    if (v.callee == "sum") {
        if (v.args.size() != 1)
            error("sum() takes exactly 1 argument, got " +
                  std::to_string(v.args.size()), line, col);
        Type a = check_expr(*v.args[0]);
        if (!a.is_array())
            error("sum() requires an array, got " + a.name(), line, col);
        if (a.kind == Type::Kind::Bool)
            error("sum() is not defined for bool arrays", line, col);
        return a.elem();
    }
    if (v.callee == "dot") {
        if (v.args.size() != 2)
            error("dot() takes exactly 2 arguments, got " +
                  std::to_string(v.args.size()), line, col);
        Type a = check_expr(*v.args[0]);
        Type b = check_expr(*v.args[1]);
        if (!a.is_array() || !b.is_array())
            error("dot() requires two arrays, got " +
                  a.name() + " and " + b.name(), line, col);
        if (a != b)
            error("dot() argument types differ: " + a.name() +
                  " vs " + b.name(), line, col);
        if (a.kind == Type::Kind::Bool)
            error("dot() is not defined for bool arrays", line, col);
        return a.elem();
    }
    return Type::scalar(Type::Kind::Void); // sentinel: not a built-in
}

// ── Expression type inference ─────────────────────────────────────────────────

Type TypeChecker::check_expr(const Expr& e) const {
    return std::visit([this, &e](const auto& v) -> Type {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, IntLitExpr>)   return Type::scalar(Type::Kind::Int);
        if constexpr (std::is_same_v<T, FloatLitExpr>) return Type::scalar(Type::Kind::Float);
        if constexpr (std::is_same_v<T, BoolLitExpr>)  return Type::scalar(Type::Kind::Bool);

        if constexpr (std::is_same_v<T, IdentExpr>)
            return lookup(v.name, e.line, e.col);

        if constexpr (std::is_same_v<T, ArrayLitExpr>) {
            if (v.elements.empty())
                error("empty array literal '[]' is not allowed", e.line, e.col);
            Type first = check_expr(*v.elements[0]);
            if (!first.is_scalar())
                error("array elements must be scalar, got " + first.name(), e.line, e.col);
            for (size_t i = 1; i < v.elements.size(); ++i) {
                Type t = check_expr(*v.elements[i]);
                if (t != first)
                    error("mixed element types in array literal: " +
                          first.name() + " and " + t.name(), e.line, e.col);
            }
            return Type::array(first.kind, (int)v.elements.size());
        }

        if constexpr (std::is_same_v<T, IndexExpr>) {
            Type a = check_expr(*v.array);
            if (!a.is_array())
                error("cannot index non-array value of type " + a.name(), e.line, e.col);
            Type i = check_expr(*v.index);
            if (i != Type::scalar(Type::Kind::Int))
                error("array index must be int, got " + i.name(), e.line, e.col);
            if (auto ci = const_int(*v.index)) {
                if (*ci < 0 || *ci >= a.array_size)
                    error("index " + std::to_string(*ci) + " out of bounds for " +
                          a.name() + " (size " + std::to_string(a.array_size) + ")",
                          e.line, e.col);
            }
            return a.elem();
        }

        if constexpr (std::is_same_v<T, UnaryExpr>) {
            Type operand_t = check_expr(*v.operand);
            if (v.op == "-") {
                if (operand_t.is_array())
                    error("unary '-' on arrays is not supported yet", e.line, e.col);
                if (operand_t.kind != Type::Kind::Int && operand_t.kind != Type::Kind::Float)
                    error("unary '-' requires int or float", e.line, e.col);
                return operand_t;
            }
            error("unknown unary operator '" + v.op + "'", e.line, e.col);
        }

        if constexpr (std::is_same_v<T, BinaryExpr>) {
            Type lhs = check_expr(*v.left);
            Type rhs = check_expr(*v.right);

            const bool is_arith = (v.op=="+" || v.op=="-" || v.op=="*" || v.op=="/" || v.op=="%");
            const bool is_cmp   = (v.op=="=="|| v.op=="!="|| v.op=="<" || v.op==">" ||
                                   v.op=="<="|| v.op==">=");

            // Array op array: element-wise. Same shape and kind required.
            if (lhs.is_array() && rhs.is_array()) {
                if (lhs != rhs)
                    error("type mismatch in '" + v.op + "': " +
                          lhs.name() + " vs " + rhs.name(), e.line, e.col);
                if (!is_arith)
                    error("operator '" + v.op + "' is not defined on arrays", e.line, e.col);
                if (lhs.kind == Type::Kind::Bool)
                    error("arithmetic on bool arrays is not allowed", e.line, e.col);
                if (v.op == "%" && lhs.kind == Type::Kind::Float)
                    error("'%' is not supported on float arrays", e.line, e.col);
                return lhs;
            }

            // Broadcasting: one side scalar, other side array of same element kind.
            if (lhs.is_array() || rhs.is_array()) {
                Type arr_t = lhs.is_array() ? lhs : rhs;
                Type sc_t  = lhs.is_array() ? rhs : lhs;
                if (sc_t != arr_t.elem())
                    error("type mismatch in '" + v.op + "': " +
                          lhs.name() + " vs " + rhs.name(), e.line, e.col);
                if (!is_arith)
                    error("operator '" + v.op + "' is not defined on arrays", e.line, e.col);
                if (arr_t.kind == Type::Kind::Bool)
                    error("arithmetic on bool arrays is not allowed", e.line, e.col);
                if (v.op == "%" && arr_t.kind == Type::Kind::Float)
                    error("'%' is not supported on float arrays", e.line, e.col);
                return arr_t;
            }

            // Scalar op scalar.
            if (lhs != rhs)
                error("type mismatch in '" + v.op + "': " +
                      lhs.name() + " vs " + rhs.name(), e.line, e.col);
            if (is_arith) {
                if (lhs.kind == Type::Kind::Bool)
                    error("arithmetic on bool is not allowed", e.line, e.col);
                if (v.op == "%" && lhs.kind == Type::Kind::Float)
                    error("'%' is not supported on float", e.line, e.col);
                return lhs;
            }
            if (is_cmp) {
                if (lhs.kind == Type::Kind::Bool && v.op != "==" && v.op != "!=")
                    error("'" + v.op + "' is not supported on bool", e.line, e.col);
                return Type::scalar(Type::Kind::Bool);
            }
            error("unknown binary operator '" + v.op + "'", e.line, e.col);
        }

        if constexpr (std::is_same_v<T, CallExpr>) {
            // Try built-ins first
            Type builtin_t = check_builtin_call(v, e.line, e.col);
            if (builtin_t.kind != Type::Kind::Void || builtin_t.is_array())
                return builtin_t;

            auto it = functions_.find(v.callee);
            if (it == functions_.end())
                error("undefined function '" + v.callee + "'", e.line, e.col);
            const auto& sig = it->second;
            if (v.args.size() != sig.param_types.size())
                error("'" + v.callee + "' expects " +
                      std::to_string(sig.param_types.size()) +
                      " arg(s), got " + std::to_string(v.args.size()), e.line, e.col);
            for (size_t i = 0; i < v.args.size(); ++i) {
                Type arg_t = check_expr(*v.args[i]);
                if (arg_t != sig.param_types[i])
                    error("arg " + std::to_string(i + 1) + " of '" + v.callee +
                          "': expected " + sig.param_types[i].name() +
                          ", got " + arg_t.name(), e.line, e.col);
            }
            return sig.return_type;
        }

        error("unknown expression variant", e.line, e.col);
    }, e.data);
}
