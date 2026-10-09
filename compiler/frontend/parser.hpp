#pragma once
#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include <vector>

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    // Throws CompileError on the first syntax error.
    Program parse();

private:
    std::vector<Token> tokens_;
    size_t pos_ = 0;

    const Token& peek(int offset = 0) const;
    const Token& advance();
    bool check(TokenType t) const;
    bool match(TokenType t);
    const Token& expect(TokenType t, const std::string& what);
    [[noreturn]] void error_at(const Token& tok, const std::string& msg) const;

    std::vector<Attribute> parse_attributes();
    Type parse_type();

    std::unique_ptr<UniformDecl> parse_uniform(std::vector<Attribute> attrs);
    std::unique_ptr<ConstDecl>   parse_const();
    std::unique_ptr<FnDecl>      parse_fn(std::vector<Attribute> attrs);

    std::unique_ptr<BlockStmt> parse_block();
    std::unique_ptr<Stmt> parse_stmt();
    std::unique_ptr<Stmt> parse_let();
    std::unique_ptr<Stmt> parse_if();
    std::unique_ptr<Stmt> parse_for();
    std::unique_ptr<Stmt> parse_while();
    std::unique_ptr<Stmt> parse_return();
    std::unique_ptr<Stmt> parse_simple(Stmt::Kind kind);
    std::unique_ptr<Stmt> parse_expr_or_assign();

    std::unique_ptr<Expr> parse_expr();
    std::unique_ptr<Expr> parse_ternary();
    std::unique_ptr<Expr> parse_binary(int min_prec);
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_postfix();
    std::unique_ptr<Expr> parse_primary();
    std::vector<std::unique_ptr<Expr>> parse_args();
};
