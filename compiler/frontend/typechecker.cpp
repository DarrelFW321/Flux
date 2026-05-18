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

Type TypeChecker::type_from_name(const std::string& name) {
    if (name == "int")   return Type::Int;
    if (name == "float") return Type::Float;
    if (name == "bool")  return Type::Bool;
    if (name == "void")  return Type::Void;
    throw std::logic_error("unknown type: " + name);
}

std::string TypeChecker::type_name(Type t) {
    switch (t) {
        case Type::Int:   return "int";
        case Type::Float: return "float";
        case Type::Bool:  return "bool";
        case Type::Void:  return "void";
    }
    return "?";
}

// ── Entry point ───────────────────────────────────────────────────────────────

void TypeChecker::check(const Program& prog) {
    first_pass(prog);
    for (const auto& item : prog.items) {
        std::visit([this](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, FnDecl>) {
                check_fn(v);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<Stmt>>) {
                push_scope();
                check_stmt(*v);
                pop_scope();
            }
        }, item);
    }
}

// First pass: collect all function signatures so forward calls are valid.
void TypeChecker::first_pass(const Program& prog) {
    for (const auto& item : prog.items) {
        if (const auto* fn = std::get_if<FnDecl>(&item)) {
            if (functions_.count(fn->name))
                error("function '" + fn->name + "' already defined", fn->line, fn->col);
            FnSignature sig;
            sig.return_type = type_from_name(fn->return_type);
            for (const auto& p : fn->params)
                sig.param_types.push_back(type_from_name(p.type_name));
            functions_[fn->name] = std::move(sig);
        }
    }
}

// ── Functions ─────────────────────────────────────────────────────────────────

void TypeChecker::check_fn(const FnDecl& fn) {
    current_return_type_ = type_from_name(fn.return_type);
    push_scope();
    for (const auto& p : fn.params)
        declare(p.name, type_from_name(p.type_name), fn.line, fn.col);
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
            Type decl_t = type_from_name(v.type_name);
            Type init_t = check_expr(*v.init);
            if (decl_t != init_t)
                error("let '" + v.name + "': declared " + type_name(decl_t) +
                      " but initializer is " + type_name(init_t), s.line, s.col);
            declare(v.name, decl_t, s.line, s.col);

        } else if constexpr (std::is_same_v<T, AssignStmt>) {
            Type var_t = lookup(v.name, s.line, s.col);
            Type val_t = check_expr(*v.value);
            if (var_t != val_t)
                error("assignment to '" + v.name + "': expected " + type_name(var_t) +
                      ", got " + type_name(val_t), s.line, s.col);

        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
            Type ret_t = check_expr(*v.value);
            if (ret_t != current_return_type_)
                error("return type mismatch: expected " + type_name(current_return_type_) +
                      ", got " + type_name(ret_t), s.line, s.col);

        } else if constexpr (std::is_same_v<T, PrintStmt>) {
            Type t = check_expr(*v.value);
            if (t == Type::Void)
                error("cannot print a void expression", s.line, s.col);

        } else if constexpr (std::is_same_v<T, IfStmt>) {
            Type cond_t = check_expr(*v.condition);
            if (cond_t != Type::Bool)
                error("if condition must be bool, got " + type_name(cond_t), s.line, s.col);
            check_block(*v.then_block);
            if (v.else_block) check_block(**v.else_block);

        } else if constexpr (std::is_same_v<T, WhileStmt>) {
            Type cond_t = check_expr(*v.condition);
            if (cond_t != Type::Bool)
                error("while condition must be bool, got " + type_name(cond_t), s.line, s.col);
            check_block(*v.body);

        } else if constexpr (std::is_same_v<T, ExprStmt>) {
            check_expr(*v.expr);
        }
    }, s.data);
}

// ── Expression type inference ─────────────────────────────────────────────────

Type TypeChecker::check_expr(const Expr& e) const {
    return std::visit([this, &e](const auto& v) -> Type {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, IntLitExpr>)   return Type::Int;
        if constexpr (std::is_same_v<T, FloatLitExpr>) return Type::Float;
        if constexpr (std::is_same_v<T, BoolLitExpr>)  return Type::Bool;

        if constexpr (std::is_same_v<T, IdentExpr>)
            return lookup(v.name, e.line, e.col);

        if constexpr (std::is_same_v<T, UnaryExpr>) {
            Type operand_t = check_expr(*v.operand);
            if (v.op == "-") {
                if (operand_t != Type::Int && operand_t != Type::Float)
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

            if (lhs != rhs)
                error("type mismatch in '" + v.op + "': " +
                      type_name(lhs) + " vs " + type_name(rhs), e.line, e.col);

            if (is_arith) {
                if (lhs == Type::Bool)
                    error("arithmetic on bool is not allowed", e.line, e.col);
                if (v.op == "%" && lhs == Type::Float)
                    error("'%' is not supported on float", e.line, e.col);
                return lhs;
            }
            if (is_cmp) {
                if (lhs == Type::Bool && v.op != "==" && v.op != "!=")
                    error("'" + v.op + "' is not supported on bool", e.line, e.col);
                return Type::Bool;
            }
            error("unknown binary operator '" + v.op + "'", e.line, e.col);
        }

        if constexpr (std::is_same_v<T, CallExpr>) {
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
                          "': expected " + type_name(sig.param_types[i]) +
                          ", got " + type_name(arg_t), e.line, e.col);
            }
            return sig.return_type;
        }

        error("unknown expression variant", e.line, e.col);
    }, e.data);
}
