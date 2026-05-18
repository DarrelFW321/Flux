#pragma once
#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include <stdexcept>
#include <vector>

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    Program parse();

private:
    std::vector<Token> tokens_;
    size_t             pos_ = 0;

    const Token& peek(int offset = 0) const;
    const Token& advance();
    const Token& expect(TokenType type, const std::string& msg);
    bool check(TokenType type) const;
    bool match(TokenType type);

    Program                    parse_program();
    FnDecl                     parse_fn_decl();
    std::unique_ptr<BlockStmt> parse_block();
    std::unique_ptr<Stmt>      parse_stmt();
    std::unique_ptr<Stmt>      parse_let_stmt();
    std::unique_ptr<Stmt>      parse_return_stmt();
    std::unique_ptr<Stmt>      parse_if_stmt();
    std::unique_ptr<Stmt>      parse_while_stmt();
    std::unique_ptr<Stmt>      parse_print_stmt();
    std::unique_ptr<Stmt>      parse_assign_or_expr_stmt();

    // Pratt expression parser
    std::unique_ptr<Expr> parse_expr(int min_bp = 0);
    std::unique_ptr<Expr> parse_prefix();
    std::unique_ptr<Expr> parse_call(std::string callee, int line, int col);

    std::string parse_type();

    [[noreturn]] void error(const std::string& msg, int line, int col) const;
    [[noreturn]] void error(const std::string& msg) const;
};
