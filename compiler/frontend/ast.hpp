#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// ── Forward declarations ────────────────────────────────────────────────────
struct Expr;
struct Stmt;

// ── Expression node types ───────────────────────────────────────────────────
struct IntLitExpr   { int64_t value; };
struct FloatLitExpr { double  value; };
struct BoolLitExpr  { bool    value; };
struct IdentExpr    { std::string name; };

struct BinaryExpr {
    std::string           op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct UnaryExpr {
    std::string           op;
    std::unique_ptr<Expr> operand;
};

struct CallExpr {
    std::string                        callee;
    std::vector<std::unique_ptr<Expr>> args;
};

struct ArrayLitExpr {
    std::vector<std::unique_ptr<Expr>> elements;
};

struct IndexExpr {
    std::unique_ptr<Expr> array;
    std::unique_ptr<Expr> index;
};

using ExprVariant = std::variant<
    IntLitExpr, FloatLitExpr, BoolLitExpr,
    IdentExpr, BinaryExpr, UnaryExpr, CallExpr,
    ArrayLitExpr, IndexExpr
>;

struct Expr {
    ExprVariant data;
    int line = 0, col = 0;
};

// ── Statement node types ────────────────────────────────────────────────────
struct BlockStmt {
    std::vector<std::unique_ptr<Stmt>> stmts;
};

struct LetStmt {
    std::string           name;
    std::string           type_name;
    std::unique_ptr<Expr> init;
};

struct AssignStmt {
    std::string           name;
    std::unique_ptr<Expr> value;
};

struct ReturnStmt {
    std::unique_ptr<Expr> value;
};

struct PrintStmt {
    std::unique_ptr<Expr> value;
};

struct IfStmt {
    std::unique_ptr<Expr>      condition;
    std::unique_ptr<BlockStmt> then_block;
    // else branch is optional
    std::optional<std::unique_ptr<BlockStmt>> else_block;
};

struct WhileStmt {
    std::unique_ptr<Expr>      condition;
    std::unique_ptr<BlockStmt> body;
};

struct ExprStmt {
    std::unique_ptr<Expr> expr;
};

// `a[i] = value;`
struct IndexAssignStmt {
    std::unique_ptr<Expr> array;
    std::unique_ptr<Expr> index;
    std::unique_ptr<Expr> value;
};

using StmtVariant = std::variant<
    LetStmt, AssignStmt, ReturnStmt, PrintStmt,
    IfStmt, WhileStmt, ExprStmt,
    IndexAssignStmt
>;

struct Stmt {
    StmtVariant data;
    int line = 0, col = 0;
};

// ── Top-level declarations ──────────────────────────────────────────────────
struct Param {
    std::string name;
    std::string type_name;
};

struct FnDecl {
    std::string              name;
    std::vector<Param>       params;
    std::string              return_type;
    std::unique_ptr<BlockStmt> body;
    int line = 0, col = 0;
};

// A program is a sequence of function declarations and/or top-level statements.
using TopLevel = std::variant<FnDecl, std::unique_ptr<Stmt>>;

struct Program {
    std::vector<TopLevel> items;
};
