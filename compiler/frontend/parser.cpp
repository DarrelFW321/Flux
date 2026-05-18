#include "frontend/parser.hpp"
#include <sstream>

// GCC 13 fires a spurious -Wmaybe-uninitialized through std::variant's move
// machinery when push_back reallocates. The warning is a false positive.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

// ── Helpers ─────────────────────────────────────────────────────────────────

const Token& Parser::peek(int offset) const {
    size_t idx = pos_ + static_cast<size_t>(offset);
    // Always return EOF if out of range
    return idx < tokens_.size() ? tokens_[idx] : tokens_.back();
}

const Token& Parser::advance() {
    if (pos_ < tokens_.size()) ++pos_;
    return tokens_[pos_ - 1];
}

bool Parser::check(TokenType type) const { return peek().type == type; }

bool Parser::match(TokenType type) {
    if (check(type)) { advance(); return true; }
    return false;
}

const Token& Parser::expect(TokenType type, const std::string& msg) {
    if (!check(type)) error(msg, peek().line, peek().col);
    return advance();
}

[[noreturn]] void Parser::error(const std::string& msg, int line, int col) const {
    throw std::runtime_error("[line " + std::to_string(line) + ":" + std::to_string(col) + "] " + msg);
}

[[noreturn]] void Parser::error(const std::string& msg) const {
    error(msg, peek().line, peek().col);
}

// ── Entry point ──────────────────────────────────────────────────────────────

Program Parser::parse() { return parse_program(); }

Program Parser::parse_program() {
    Program prog;
    while (!check(TokenType::EOF_TOK)) {
        if (check(TokenType::KW_FN))
            prog.items.push_back(parse_fn_decl());
        else
            prog.items.push_back(parse_stmt());
    }
    return prog;
}

// ── Declarations ─────────────────────────────────────────────────────────────

FnDecl Parser::parse_fn_decl() {
    FnDecl fn;
    const Token& kw = expect(TokenType::KW_FN, "expected 'fn'");
    fn.line = kw.line; fn.col = kw.col;
    fn.name = expect(TokenType::IDENTIFIER, "expected function name after 'fn'").lexeme;
    expect(TokenType::LPAREN, "expected '(' after function name");

    if (!check(TokenType::RPAREN)) {
        do {
            Param p;
            p.name      = expect(TokenType::IDENTIFIER, "expected parameter name").lexeme;
            expect(TokenType::COLON, "expected ':' after parameter name");
            p.type_name = parse_type();
            fn.params.push_back(std::move(p));
        } while (match(TokenType::COMMA));
    }

    expect(TokenType::RPAREN,  "expected ')' after parameters");
    expect(TokenType::ARROW,   "expected '->' before return type");
    fn.return_type = parse_type();
    fn.body        = parse_block();
    return fn;
}

std::string Parser::parse_type() {
    const Token& t = peek();
    if (t.type == TokenType::KW_INT)   { advance(); return "int";   }
    if (t.type == TokenType::KW_FLOAT) { advance(); return "float"; }
    if (t.type == TokenType::KW_BOOL)  { advance(); return "bool";  }
    error("expected type ('int', 'float', 'bool')", t.line, t.col);
}

// ── Block & statements ────────────────────────────────────────────────────────

std::unique_ptr<BlockStmt> Parser::parse_block() {
    expect(TokenType::LBRACE, "expected '{'");
    auto block = std::make_unique<BlockStmt>();
    while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOK))
        block->stmts.push_back(parse_stmt());
    expect(TokenType::RBRACE, "expected '}'");
    return block;
}

std::unique_ptr<Stmt> Parser::parse_stmt() {
    switch (peek().type) {
        case TokenType::KW_LET:    return parse_let_stmt();
        case TokenType::KW_RETURN: return parse_return_stmt();
        case TokenType::KW_IF:     return parse_if_stmt();
        case TokenType::KW_WHILE:  return parse_while_stmt();
        case TokenType::KW_PRINT:  return parse_print_stmt();
        default:                   return parse_assign_or_expr_stmt();
    }
}

std::unique_ptr<Stmt> Parser::parse_let_stmt() {
    int line = peek().line, col = peek().col;
    expect(TokenType::KW_LET, "expected 'let'");
    LetStmt s;
    s.name      = expect(TokenType::IDENTIFIER, "expected variable name after 'let'").lexeme;
    expect(TokenType::COLON, "expected ':' after variable name");
    s.type_name = parse_type();
    expect(TokenType::EQ,        "expected '=' in let statement");
    s.init      = parse_expr();
    expect(TokenType::SEMICOLON, "expected ';' after let statement");
    auto stmt = std::make_unique<Stmt>();
    stmt->data = std::move(s);
    stmt->line = line; stmt->col = col;
    return stmt;
}

std::unique_ptr<Stmt> Parser::parse_return_stmt() {
    int line = peek().line, col = peek().col;
    expect(TokenType::KW_RETURN, "expected 'return'");
    ReturnStmt s;
    s.value = parse_expr();
    expect(TokenType::SEMICOLON, "expected ';' after return value");
    auto stmt = std::make_unique<Stmt>();
    stmt->data = std::move(s);
    stmt->line = line; stmt->col = col;
    return stmt;
}

std::unique_ptr<Stmt> Parser::parse_if_stmt() {
    int line = peek().line, col = peek().col;
    expect(TokenType::KW_IF, "expected 'if'");
    IfStmt s;
    s.condition  = parse_expr();
    s.then_block = parse_block();
    if (match(TokenType::KW_ELSE))
        s.else_block = parse_block();
    auto stmt = std::make_unique<Stmt>();
    stmt->data = std::move(s);
    stmt->line = line; stmt->col = col;
    return stmt;
}

std::unique_ptr<Stmt> Parser::parse_while_stmt() {
    int line = peek().line, col = peek().col;
    expect(TokenType::KW_WHILE, "expected 'while'");
    WhileStmt s;
    s.condition = parse_expr();
    s.body      = parse_block();
    auto stmt = std::make_unique<Stmt>();
    stmt->data = std::move(s);
    stmt->line = line; stmt->col = col;
    return stmt;
}

std::unique_ptr<Stmt> Parser::parse_print_stmt() {
    int line = peek().line, col = peek().col;
    expect(TokenType::KW_PRINT,  "expected 'print'");
    expect(TokenType::LPAREN,    "expected '(' after 'print'");
    PrintStmt s;
    s.value = parse_expr();
    expect(TokenType::RPAREN,    "expected ')' after print argument");
    expect(TokenType::SEMICOLON, "expected ';' after print statement");
    auto stmt = std::make_unique<Stmt>();
    stmt->data = std::move(s);
    stmt->line = line; stmt->col = col;
    return stmt;
}

std::unique_ptr<Stmt> Parser::parse_assign_or_expr_stmt() {
    int line = peek().line, col = peek().col;

    // Lookahead: IDENTIFIER followed by '=' (but not '==') is an assignment.
    if (check(TokenType::IDENTIFIER) &&
        pos_ + 1 < tokens_.size() &&
        tokens_[pos_ + 1].type == TokenType::EQ)
    {
        AssignStmt s;
        s.name  = advance().lexeme; // consume identifier
        advance();                  // consume '='
        s.value = parse_expr();
        expect(TokenType::SEMICOLON, "expected ';' after assignment");
        auto stmt = std::make_unique<Stmt>();
        stmt->data = std::move(s);
        stmt->line = line; stmt->col = col;
        return stmt;
    }

    ExprStmt s;
    s.expr = parse_expr();
    expect(TokenType::SEMICOLON, "expected ';' after expression");
    auto stmt = std::make_unique<Stmt>();
    stmt->data = std::move(s);
    stmt->line = line; stmt->col = col;
    return stmt;
}

// ── Pratt expression parser ───────────────────────────────────────────────────

// Returns the left binding power for infix operators, -1 if not an infix op.
static int infix_bp(TokenType t) {
    switch (t) {
        case TokenType::EQ_EQ:
        case TokenType::BANG_EQ:  return 10;
        case TokenType::LT:
        case TokenType::GT:
        case TokenType::LT_EQ:
        case TokenType::GT_EQ:    return 20;
        case TokenType::PLUS:
        case TokenType::MINUS:    return 30;
        case TokenType::STAR:
        case TokenType::SLASH:
        case TokenType::PERCENT:  return 40;
        default:                  return -1;
    }
}

std::unique_ptr<Expr> Parser::parse_expr(int min_bp) {
    auto left = parse_prefix();

    while (true) {
        int lbp = infix_bp(peek().type);
        if (lbp < 0 || lbp <= min_bp) break;

        int         line   = peek().line;
        int         col    = peek().col;
        std::string op_str = advance().lexeme;

        // Left-associative: right side parses at the same level (lbp),
        // so a subsequent operator at the same precedence will not right-bind.
        auto right = parse_expr(lbp);

        auto e = std::make_unique<Expr>();
        e->data = BinaryExpr{op_str, std::move(left), std::move(right)};
        e->line = line; e->col = col;
        left = std::move(e);
    }

    return left;
}

std::unique_ptr<Expr> Parser::parse_prefix() {
    const Token& tok = peek();
    int line = tok.line, col = tok.col;

    // Integer literal
    if (tok.type == TokenType::INT_LIT) {
        advance();
        auto e = std::make_unique<Expr>();
        e->data = IntLitExpr{std::stoll(tok.lexeme)};
        e->line = line; e->col = col;
        return e;
    }

    // Float literal
    if (tok.type == TokenType::FLOAT_LIT) {
        advance();
        auto e = std::make_unique<Expr>();
        e->data = FloatLitExpr{std::stod(tok.lexeme)};
        e->line = line; e->col = col;
        return e;
    }

    // Boolean literals
    if (tok.type == TokenType::KW_TRUE) {
        advance();
        auto e = std::make_unique<Expr>();
        e->data = BoolLitExpr{true};
        e->line = line; e->col = col;
        return e;
    }
    if (tok.type == TokenType::KW_FALSE) {
        advance();
        auto e = std::make_unique<Expr>();
        e->data = BoolLitExpr{false};
        e->line = line; e->col = col;
        return e;
    }

    // Identifier or function call
    if (tok.type == TokenType::IDENTIFIER) {
        advance();
        if (check(TokenType::LPAREN))
            return parse_call(tok.lexeme, line, col);
        auto e = std::make_unique<Expr>();
        e->data = IdentExpr{tok.lexeme};
        e->line = line; e->col = col;
        return e;
    }

    // Unary negation — right binding power 50 (higher than any infix)
    if (tok.type == TokenType::MINUS) {
        advance();
        auto e = std::make_unique<Expr>();
        e->data = UnaryExpr{"-", parse_expr(50)};
        e->line = line; e->col = col;
        return e;
    }

    // Grouped expression
    if (tok.type == TokenType::LPAREN) {
        advance();
        auto inner = parse_expr(0);
        expect(TokenType::RPAREN, "expected ')' after expression");
        return inner;
    }

    error("expected an expression", tok.line, tok.col);
}

std::unique_ptr<Expr> Parser::parse_call(std::string callee, int line, int col) {
    expect(TokenType::LPAREN, "expected '('");
    CallExpr call;
    call.callee = std::move(callee);
    if (!check(TokenType::RPAREN)) {
        do {
            call.args.push_back(parse_expr(0));
        } while (match(TokenType::COMMA));
    }
    expect(TokenType::RPAREN, "expected ')' after arguments");
    auto e = std::make_unique<Expr>();
    e->data = std::move(call);
    e->line = line; e->col = col;
    return e;
}
