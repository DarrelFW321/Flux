#pragma once
#include "common/diagnostics.hpp"
#include <string>
#include <vector>

enum class TokenType {
    // Literals & names
    INT_LIT, FLOAT_LIT, IDENTIFIER, TYPE,
    // Keywords
    KW_FN, KW_LET, KW_VAR, KW_CONST, KW_UNIFORM, KW_RETURN,
    KW_IF, KW_ELSE, KW_FOR, KW_IN, KW_WHILE, KW_BREAK, KW_CONTINUE,
    KW_DISCARD, KW_TRUE, KW_FALSE,
    // Operators
    PLUS, MINUS, STAR, SLASH, PERCENT,
    PLUS_EQ, MINUS_EQ, STAR_EQ, SLASH_EQ,
    EQ, EQ_EQ, BANG_EQ, LT, GT, LT_EQ, GT_EQ,
    AMP_AMP, PIPE_PIPE, BANG, QUESTION,
    // Punctuation
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    COMMA, COLON, SEMICOLON, DOT, DOT_DOT, ARROW, AT,
    EOF_TOK,
};

struct Token {
    TokenType   type;
    std::string lexeme;
    int         line;
    int         col;

    SourceLoc loc() const {
        return {line, col, lexeme.empty() ? 1 : static_cast<int>(lexeme.size())};
    }
};

const char* token_type_name(TokenType t);

class Lexer {
public:
    explicit Lexer(std::string src);
    // Throws CompileError on the first malformed token.
    std::vector<Token> tokenize();

private:
    std::string src_;
    size_t      pos_  = 0;
    int         line_ = 1;
    int         col_  = 1;

    char  peek(int offset = 0) const;
    char  advance();
    void  skip_whitespace_and_comments();
    Token scan_number();
    Token scan_word();
};
