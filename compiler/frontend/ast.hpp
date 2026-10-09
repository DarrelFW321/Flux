#pragma once
#include "common/diagnostics.hpp"
#include "common/types.hpp"
#include <memory>
#include <string>
#include <vector>

// ── Attributes ───────────────────────────────────────────────────────────────
// `@name` or `@name(arg, ...)`. Arguments are kept as raw lexemes; the type
// checker interprets them per attribute (`@location(0)`, `@range(0.0, 2.0)`).
struct Attribute {
    std::string              name;
    std::vector<std::string> args;
    SourceLoc                loc;
};

const Attribute* find_attr(const std::vector<Attribute>& attrs, const std::string& name);

// ── Expressions ──────────────────────────────────────────────────────────────
struct Stmt;
struct FnDecl;
struct BlockStmt;

// What an identifier resolved to. Filled in by the type checker.
enum class SymbolKind { Unresolved, Let, Var, Param, Uniform, Const, LoopIndex };

struct Expr {
    enum class Kind {
        IntLit, FloatLit, BoolLit, Ident,
        Unary,      // op, children[0]
        Binary,     // op, children[0..1]
        Ternary,    // children: cond, then, else
        Call,       // name(children...)
        Construct,  // construct_type(children...)
        Swizzle,    // children[0].name
        Index,      // children[0][children[1]]
    };

    Kind        kind;
    SourceLoc   loc;
    std::string op;      // Unary/Binary operator
    std::string name;    // Ident / Call callee / Swizzle components
    long long   int_value   = 0;
    double      float_value = 0.0;
    bool        bool_value  = false;
    Type        construct_type;
    std::vector<std::unique_ptr<Expr>> children;

    // ── Semantic annotations ─────────────────────────────────────────────
    Type        type = Type::error();
    SymbolKind  sym  = SymbolKind::Unresolved;
    const void* decl = nullptr;     // Stmt*, Param*, UniformDecl*, ConstDecl*
    int         builtin = -1;       // BuiltinFn id for builtin calls
    const FnDecl* callee = nullptr; // user function for non-builtin calls
};

// ── Statements ───────────────────────────────────────────────────────────────
struct BlockStmt {
    std::vector<std::unique_ptr<Stmt>> stmts;
    SourceLoc loc;
};

struct Stmt {
    enum class Kind {
        Let,        // let/var name[: type] [= init]
        Assign,     // target op value          (op is "=", "+=", ...)
        If,         // cond, then_block, else_stmt (a Block or another If)
        For,        // for name in start..end body
        While,      // while cond body
        Break, Continue, Discard,
        Return,     // value (optional)
        ExprStmt,   // value
        Block,      // block
    };

    Kind      kind;
    SourceLoc loc;

    // Let / For
    std::string name;
    SourceLoc   name_loc;
    bool        is_var = false;          // `var` (mutable) vs `let`
    bool        has_type = false;
    Type        decl_type;               // declared or inferred type

    std::string op;                      // Assign
    std::unique_ptr<Expr> target;        // Assign
    std::unique_ptr<Expr> value;         // Let init, Assign rhs, Return, ExprStmt, If/While cond, For start
    std::unique_ptr<Expr> end;           // For end (exclusive)
    std::unique_ptr<BlockStmt> body;     // If then, For/While body, Block
    std::unique_ptr<Stmt> else_stmt;     // If

    // Semantic annotations (Let/var): written by the checker for warnings.
    mutable bool used    = false;
    mutable bool mutated = false;
};

// ── Declarations ─────────────────────────────────────────────────────────────
struct Param {
    std::string            name;
    Type                   type;
    std::vector<Attribute> attrs;
    SourceLoc              loc;
    mutable bool           used = false;
};

struct FnDecl {
    std::string            name;
    std::vector<Attribute> attrs;
    std::vector<Param>     params;
    Type                   return_type = Type::void_();
    std::vector<Attribute> return_attrs;
    std::unique_ptr<BlockStmt> body;
    SourceLoc              loc;
    SourceLoc              name_loc;
    Stage                  stage = Stage::None;   // filled by the checker
};

struct UniformDecl {
    std::string            name;
    Type                   type;
    std::vector<Attribute> attrs;
    std::unique_ptr<Expr>  default_value;   // optional, must be constant
    SourceLoc              loc;
};

struct ConstDecl {
    std::string           name;
    bool                  has_type = false;
    Type                  type;
    std::unique_ptr<Expr> value;
    SourceLoc             loc;
};

struct Program {
    std::vector<std::unique_ptr<UniformDecl>> uniforms;
    std::vector<std::unique_ptr<ConstDecl>>   consts;
    std::vector<std::unique_ptr<FnDecl>>      functions;
    // Declaration order across all three lists, for the AST view.
    struct Item { enum Kind { Uniform, Const, Fn } kind; size_t index; };
    std::vector<Item> order;
};
