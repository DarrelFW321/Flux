#pragma once
#include <string>
#include <vector>
#include <stdexcept>

enum class TokenType {
    // Literals
    INT_LIT, FLOAT_LIT,
    // Identifiers
    IDENTIFIER,
    // Keywords
    KW_INT, KW_FLOAT, KW_BOOL,
    KW_LET, KW_FN, KW_RETURN,
    KW_IF, KW_ELSE, KW_WHILE,
    KW_PRINT, KW_TRUE, KW_FALSE,
    // Binary operators
    PLUS, MINUS, STAR, SLASH, PERCENT,
    EQ_EQ, BANG_EQ, LT, GT, LT_EQ, GT_EQ,
    EQ,
    // Punctuation
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    COMMA, COLON, SEMICOLON, ARROW,
    // End of input
    EOF_TOK,
};

struct Token {
    TokenType   type;
    std::string lexeme;
    int         line;
    int         col;
};

const char* token_type_name(TokenType t);

class Lexer {
public:
    explicit Lexer(std::string src);
    std::vector<Token> tokenize();

private:
    std::string src_;
    size_t      pos_  = 0;
    int         line_ = 1;
    int         col_  = 1;

    char peek(int offset = 0) const;
    char advance();
    void skip_whitespace_and_comments();
    Token scan_number();
    Token scan_ident_or_keyword();
};
