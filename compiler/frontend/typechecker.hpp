#pragma once
#include "frontend/ast.hpp"
#include "frontend/types.hpp"
#include <string>
#include <unordered_map>
#include <vector>

// Re-export FluxType under the historical name used by the rest of the file.
using Type = FluxType;

struct FnSignature {
    std::vector<Type> param_types;
    Type              return_type;
};

class TypeChecker {
public:
    void check(const Program& prog);

private:
    std::vector<std::unordered_map<std::string, Type>> scopes_;
    std::unordered_map<std::string, FnSignature>        functions_;
    Type current_return_type_ = Type::scalar(Type::Kind::Void);

    void push_scope();
    void pop_scope();
    void declare(const std::string& name, Type t, int line, int col);
    Type lookup(const std::string& name, int line, int col) const;

    void first_pass(const Program& prog);
    void check_fn(const FnDecl& fn);
    void check_block(const BlockStmt& block);
    void check_stmt(const Stmt& stmt);
    Type check_expr(const Expr& expr) const;

    // Built-in reductions/array ops. Returns Void to mean "not a built-in".
    Type check_builtin_call(const CallExpr& call, int line, int col) const;

    [[noreturn]] static void error(const std::string& msg, int line, int col);
};
