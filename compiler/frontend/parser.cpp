#include "frontend/parser.hpp"
#include <cstdlib>

const Attribute* find_attr(const std::vector<Attribute>& attrs, const std::string& name) {
    for (const auto& a : attrs)
        if (a.name == name) return &a;
    return nullptr;
}

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

// ── Token helpers ────────────────────────────────────────────────────────────

const Token& Parser::peek(int offset) const {
    size_t i = pos_ + static_cast<size_t>(offset);
    return i < tokens_.size() ? tokens_[i] : tokens_.back();
}

const Token& Parser::advance() {
    const Token& t = tokens_[pos_];
    if (pos_ + 1 < tokens_.size()) ++pos_;   // never step past EOF
    return t;
}

bool Parser::check(TokenType t) const { return peek().type == t; }

bool Parser::match(TokenType t) {
    if (!check(t)) return false;
    advance();
    return true;
}

const Token& Parser::expect(TokenType t, const std::string& what) {
    if (!check(t)) {
        const Token& tok = peek();
        std::string got = tok.type == TokenType::EOF_TOK ? "end of file" : "'" + tok.lexeme + "'";
        error_at(tok, "expected " + what + ", found " + got);
    }
    return advance();
}

void Parser::error_at(const Token& tok, const std::string& msg) const {
    throw CompileError(tok.loc(), msg);
}

static SourceLoc span(SourceLoc a, SourceLoc b) {
    // Extend `a` to cover `b` when both sit on the same line.
    if (a.line == b.line && b.col + b.len > a.col) a.len = b.col + b.len - a.col;
    return a;
}

// ── Program ──────────────────────────────────────────────────────────────────

Program Parser::parse() {
    Program prog;
    while (!check(TokenType::EOF_TOK)) {
        auto attrs = parse_attributes();
        if (check(TokenType::KW_UNIFORM)) {
            prog.order.push_back({Program::Item::Uniform, prog.uniforms.size()});
            prog.uniforms.push_back(parse_uniform(std::move(attrs)));
        } else if (check(TokenType::KW_CONST)) {
            if (!attrs.empty()) throw CompileError(attrs[0].loc, "attributes are not allowed on 'const'");
            prog.order.push_back({Program::Item::Const, prog.consts.size()});
            prog.consts.push_back(parse_const());
        } else if (check(TokenType::KW_FN)) {
            prog.order.push_back({Program::Item::Fn, prog.functions.size()});
            prog.functions.push_back(parse_fn(std::move(attrs)));
        } else {
            error_at(peek(), "expected 'fn', 'uniform' or 'const' at top level, found '" + peek().lexeme + "'");
        }
    }
    return prog;
}

std::vector<Attribute> Parser::parse_attributes() {
    std::vector<Attribute> attrs;
    while (check(TokenType::AT)) {
        const Token& at = advance();
        const Token& name = expect(TokenType::IDENTIFIER, "attribute name after '@'");
        Attribute a{name.lexeme, {}, span(at.loc(), name.loc())};
        if (match(TokenType::LPAREN)) {
            if (!check(TokenType::RPAREN)) {
                do {
                    std::string arg;
                    if (match(TokenType::MINUS)) arg = "-";
                    const Token& t = peek();
                    if (t.type != TokenType::INT_LIT && t.type != TokenType::FLOAT_LIT &&
                        t.type != TokenType::IDENTIFIER)
                        error_at(t, "expected a number or name as attribute argument");
                    advance();
                    a.args.push_back(arg + t.lexeme);
                } while (match(TokenType::COMMA));
            }
            const Token& close = expect(TokenType::RPAREN, "')' to close attribute");
            a.loc = span(a.loc, close.loc());
        }
        attrs.push_back(std::move(a));
    }
    return attrs;
}

Type Parser::parse_type() {
    const Token& t = peek();
    if (t.type != TokenType::TYPE) {
        if (t.type == TokenType::IDENTIFIER)
            error_at(t, "unknown type '" + t.lexeme + "' (expected bool, int, float, vec2-4 or mat2-4)");
        error_at(t, "expected a type");
    }
    advance();
    return Type::from_name(t.lexeme);
}

std::unique_ptr<UniformDecl> Parser::parse_uniform(std::vector<Attribute> attrs) {
    auto u = std::make_unique<UniformDecl>();
    u->loc = expect(TokenType::KW_UNIFORM, "'uniform'").loc();
    const Token& name = expect(TokenType::IDENTIFIER, "uniform name");
    u->name = name.lexeme;
    u->loc  = name.loc();
    u->attrs = std::move(attrs);
    expect(TokenType::COLON, "':' and a type after uniform name");
    u->type = parse_type();
    if (match(TokenType::EQ)) u->default_value = parse_expr();
    expect(TokenType::SEMICOLON, "';' after uniform declaration");
    return u;
}

std::unique_ptr<ConstDecl> Parser::parse_const() {
    auto c = std::make_unique<ConstDecl>();
    expect(TokenType::KW_CONST, "'const'");
    const Token& name = expect(TokenType::IDENTIFIER, "constant name");
    c->name = name.lexeme;
    c->loc  = name.loc();
    if (match(TokenType::COLON)) { c->has_type = true; c->type = parse_type(); }
    expect(TokenType::EQ, "'=' and an initializer (constants must be initialized)");
    c->value = parse_expr();
    expect(TokenType::SEMICOLON, "';' after constant");
    return c;
}

std::unique_ptr<FnDecl> Parser::parse_fn(std::vector<Attribute> attrs) {
    auto fn = std::make_unique<FnDecl>();
    fn->loc   = expect(TokenType::KW_FN, "'fn'").loc();
    fn->attrs = std::move(attrs);
    const Token& name = expect(TokenType::IDENTIFIER, "function name");
    fn->name = name.lexeme;
    fn->name_loc = name.loc();
    expect(TokenType::LPAREN, "'(' after function name");
    if (!check(TokenType::RPAREN)) {
        do {
            Param p;
            p.attrs = parse_attributes();
            const Token& pn = expect(TokenType::IDENTIFIER, "parameter name");
            p.name = pn.lexeme;
            p.loc  = pn.loc();
            expect(TokenType::COLON, "':' after parameter name");
            p.type = parse_type();
            fn->params.push_back(std::move(p));
        } while (match(TokenType::COMMA));
    }
    expect(TokenType::RPAREN, "')' after parameters");
    if (match(TokenType::ARROW)) {
        fn->return_attrs = parse_attributes();
        fn->return_type  = parse_type();
    }
    fn->body = parse_block();
    return fn;
}

// ── Statements ───────────────────────────────────────────────────────────────

std::unique_ptr<BlockStmt> Parser::parse_block() {
    auto block = std::make_unique<BlockStmt>();
    block->loc = expect(TokenType::LBRACE, "'{'").loc();
    while (!check(TokenType::RBRACE)) {
        if (check(TokenType::EOF_TOK)) error_at(peek(), "expected '}' before end of file");
        block->stmts.push_back(parse_stmt());
    }
    advance();
    return block;
}

std::unique_ptr<Stmt> Parser::parse_stmt() {
    switch (peek().type) {
        case TokenType::KW_LET:
        case TokenType::KW_VAR:      return parse_let();
        case TokenType::KW_IF:       return parse_if();
        case TokenType::KW_FOR:      return parse_for();
        case TokenType::KW_WHILE:    return parse_while();
        case TokenType::KW_RETURN:   return parse_return();
        case TokenType::KW_BREAK:    return parse_simple(Stmt::Kind::Break);
        case TokenType::KW_CONTINUE: return parse_simple(Stmt::Kind::Continue);
        case TokenType::KW_DISCARD:  return parse_simple(Stmt::Kind::Discard);
        case TokenType::LBRACE: {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Kind::Block;
            s->loc  = peek().loc();
            s->body = parse_block();
            return s;
        }
        default: return parse_expr_or_assign();
    }
}

std::unique_ptr<Stmt> Parser::parse_let() {
    auto s = std::make_unique<Stmt>();
    s->kind   = Stmt::Kind::Let;
    const Token& kw = advance();
    s->is_var = kw.type == TokenType::KW_VAR;
    s->loc    = kw.loc();
    const Token& name = expect(TokenType::IDENTIFIER, "variable name");
    s->name     = name.lexeme;
    s->name_loc = name.loc();
    if (match(TokenType::COLON)) { s->has_type = true; s->decl_type = parse_type(); }
    if (match(TokenType::EQ)) {
        s->value = parse_expr();
    } else if (!s->is_var) {
        error_at(peek(), "'let' bindings need an initializer (use 'var' for a zero-initialized variable)");
    } else if (!s->has_type) {
        error_at(peek(), "'var' without an initializer needs a type annotation");
    }
    expect(TokenType::SEMICOLON, "';' after declaration");
    return s;
}

std::unique_ptr<Stmt> Parser::parse_if() {
    auto s = std::make_unique<Stmt>();
    s->kind  = Stmt::Kind::If;
    s->loc   = advance().loc();
    s->value = parse_expr();
    s->body  = parse_block();
    if (match(TokenType::KW_ELSE)) {
        if (check(TokenType::KW_IF)) {
            s->else_stmt = parse_if();
        } else {
            auto e = std::make_unique<Stmt>();
            e->kind = Stmt::Kind::Block;
            e->loc  = peek().loc();
            e->body = parse_block();
            s->else_stmt = std::move(e);
        }
    }
    return s;
}

std::unique_ptr<Stmt> Parser::parse_for() {
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::For;
    s->loc  = advance().loc();
    const Token& name = expect(TokenType::IDENTIFIER, "loop variable name");
    s->name     = name.lexeme;
    s->name_loc = name.loc();
    expect(TokenType::KW_IN, "'in' after loop variable");
    s->value = parse_expr();
    expect(TokenType::DOT_DOT, "'..' in range (for i in start..end)");
    s->end  = parse_expr();
    s->body = parse_block();
    return s;
}

std::unique_ptr<Stmt> Parser::parse_while() {
    auto s = std::make_unique<Stmt>();
    s->kind  = Stmt::Kind::While;
    s->loc   = advance().loc();
    s->value = parse_expr();
    s->body  = parse_block();
    return s;
}

std::unique_ptr<Stmt> Parser::parse_return() {
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::Return;
    s->loc  = advance().loc();
    if (!check(TokenType::SEMICOLON)) s->value = parse_expr();
    expect(TokenType::SEMICOLON, "';' after return");
    return s;
}

std::unique_ptr<Stmt> Parser::parse_simple(Stmt::Kind kind) {
    auto s = std::make_unique<Stmt>();
    s->kind = kind;
    s->loc  = advance().loc();
    expect(TokenType::SEMICOLON, "';'");
    return s;
}

std::unique_ptr<Stmt> Parser::parse_expr_or_assign() {
    auto s = std::make_unique<Stmt>();
    s->loc = peek().loc();
    auto e = parse_expr();
    switch (peek().type) {
        case TokenType::EQ: case TokenType::PLUS_EQ: case TokenType::MINUS_EQ:
        case TokenType::STAR_EQ: case TokenType::SLASH_EQ:
            s->kind   = Stmt::Kind::Assign;
            s->op     = advance().lexeme;
            s->loc    = e->loc;
            s->target = std::move(e);
            s->value  = parse_expr();
            break;
        default:
            s->kind  = Stmt::Kind::ExprStmt;
            s->loc   = e->loc;
            s->value = std::move(e);
    }
    expect(TokenType::SEMICOLON, "';' after statement");
    return s;
}

// ── Expressions ──────────────────────────────────────────────────────────────

static std::unique_ptr<Expr> make_expr(Expr::Kind k, SourceLoc loc) {
    auto e = std::make_unique<Expr>();
    e->kind = k;
    e->loc  = loc;
    return e;
}

std::unique_ptr<Expr> Parser::parse_expr() { return parse_ternary(); }

std::unique_ptr<Expr> Parser::parse_ternary() {
    auto cond = parse_binary(0);
    if (!check(TokenType::QUESTION)) return cond;
    advance();
    auto e = make_expr(Expr::Kind::Ternary, cond->loc);
    auto a = parse_expr();
    expect(TokenType::COLON, "':' in conditional expression");
    auto b = parse_ternary();
    e->children.push_back(std::move(cond));
    e->children.push_back(std::move(a));
    e->children.push_back(std::move(b));
    return e;
}

static int binary_prec(TokenType t) {
    switch (t) {
        case TokenType::PIPE_PIPE: return 1;
        case TokenType::AMP_AMP:   return 2;
        case TokenType::EQ_EQ: case TokenType::BANG_EQ: return 3;
        case TokenType::LT: case TokenType::GT:
        case TokenType::LT_EQ: case TokenType::GT_EQ:   return 4;
        case TokenType::PLUS: case TokenType::MINUS:    return 5;
        case TokenType::STAR: case TokenType::SLASH: case TokenType::PERCENT: return 6;
        default: return -1;
    }
}

std::unique_ptr<Expr> Parser::parse_binary(int min_prec) {
    auto lhs = parse_unary();
    for (;;) {
        int prec = binary_prec(peek().type);
        if (prec < 0 || prec < min_prec) return lhs;
        const Token& op = advance();
        auto rhs = parse_binary(prec + 1);   // all binary operators are left-associative
        auto e = make_expr(Expr::Kind::Binary, op.loc());
        e->op = op.lexeme;
        e->children.push_back(std::move(lhs));
        e->children.push_back(std::move(rhs));
        lhs = std::move(e);
    }
}

std::unique_ptr<Expr> Parser::parse_unary() {
    if (check(TokenType::MINUS) || check(TokenType::BANG)) {
        const Token& op = advance();
        auto e = make_expr(Expr::Kind::Unary, op.loc());
        e->op = op.lexeme;
        e->children.push_back(parse_unary());
        return e;
    }
    return parse_postfix();
}

std::unique_ptr<Expr> Parser::parse_postfix() {
    auto e = parse_primary();
    for (;;) {
        if (check(TokenType::DOT)) {
            advance();
            const Token& field = expect(TokenType::IDENTIFIER, "swizzle after '.' (e.g. .xy, .rgb)");
            auto s = make_expr(Expr::Kind::Swizzle, field.loc());
            s->name = field.lexeme;
            s->children.push_back(std::move(e));
            e = std::move(s);
        } else if (check(TokenType::LBRACKET)) {
            const Token& lb = advance();
            auto idx = make_expr(Expr::Kind::Index, lb.loc());
            idx->children.push_back(std::move(e));
            idx->children.push_back(parse_expr());
            expect(TokenType::RBRACKET, "']'");
            e = std::move(idx);
        } else {
            return e;
        }
    }
}

std::vector<std::unique_ptr<Expr>> Parser::parse_args() {
    std::vector<std::unique_ptr<Expr>> args;
    expect(TokenType::LPAREN, "'('");
    if (!check(TokenType::RPAREN)) {
        do args.push_back(parse_expr());
        while (match(TokenType::COMMA));
    }
    expect(TokenType::RPAREN, "')' after arguments");
    return args;
}

std::unique_ptr<Expr> Parser::parse_primary() {
    const Token& t = peek();
    switch (t.type) {
        case TokenType::INT_LIT: {
            advance();
            auto e = make_expr(Expr::Kind::IntLit, t.loc());
            e->int_value = std::strtoll(t.lexeme.c_str(), nullptr, 10);
            if (e->int_value > 2147483647LL) error_at(t, "integer literal does not fit in 32 bits");
            return e;
        }
        case TokenType::FLOAT_LIT: {
            advance();
            auto e = make_expr(Expr::Kind::FloatLit, t.loc());
            e->float_value = std::strtod(t.lexeme.c_str(), nullptr);
            return e;
        }
        case TokenType::KW_TRUE:
        case TokenType::KW_FALSE: {
            advance();
            auto e = make_expr(Expr::Kind::BoolLit, t.loc());
            e->bool_value = t.type == TokenType::KW_TRUE;
            return e;
        }
        case TokenType::IDENTIFIER: {
            advance();
            if (check(TokenType::LPAREN)) {
                auto e = make_expr(Expr::Kind::Call, t.loc());
                e->name = t.lexeme;
                e->children = parse_args();
                return e;
            }
            auto e = make_expr(Expr::Kind::Ident, t.loc());
            e->name = t.lexeme;
            return e;
        }
        case TokenType::TYPE: {
            advance();
            if (!check(TokenType::LPAREN))
                error_at(peek(), "expected '(' after type name '" + t.lexeme + "' (constructor call)");
            auto e = make_expr(Expr::Kind::Construct, t.loc());
            e->construct_type = Type::from_name(t.lexeme);
            e->name = t.lexeme;
            e->children = parse_args();
            return e;
        }
        case TokenType::LPAREN: {
            advance();
            auto e = parse_expr();
            expect(TokenType::RPAREN, "')'");
            return e;
        }
        case TokenType::EOF_TOK:
            error_at(t, "expected an expression, found end of file");
        default:
            error_at(t, "expected an expression, found '" + t.lexeme + "'");
    }
}
