#include "frontend/lexer.hpp"
#include <cctype>
#include <stdexcept>
#include <unordered_map>

static const std::unordered_map<std::string, TokenType> KEYWORDS = {
    {"int",    TokenType::KW_INT},
    {"float",  TokenType::KW_FLOAT},
    {"bool",   TokenType::KW_BOOL},
    {"let",    TokenType::KW_LET},
    {"fn",     TokenType::KW_FN},
    {"return", TokenType::KW_RETURN},
    {"if",     TokenType::KW_IF},
    {"else",   TokenType::KW_ELSE},
    {"while",  TokenType::KW_WHILE},
    {"print",  TokenType::KW_PRINT},
    {"true",   TokenType::KW_TRUE},
    {"false",  TokenType::KW_FALSE},
};

Lexer::Lexer(std::string src) : src_(std::move(src)) {}

char Lexer::peek(int offset) const {
    size_t idx = pos_ + static_cast<size_t>(offset);
    return idx < src_.size() ? src_[idx] : '\0';
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') { ++line_; col_ = 1; }
    else           { ++col_; }
    return c;
}

void Lexer::skip_whitespace_and_comments() {
    while (pos_ < src_.size()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            while (pos_ < src_.size() && peek() != '\n') advance();
        } else {
            break;
        }
    }
}

Token Lexer::scan_number() {
    int start_line = line_, start_col = col_;
    std::string num;
    bool is_float = false;
    while (pos_ < src_.size() && (std::isdigit(peek()) || (peek() == '.' && !is_float))) {
        if (peek() == '.') is_float = true;
        num += advance();
    }
    return {is_float ? TokenType::FLOAT_LIT : TokenType::INT_LIT, num, start_line, start_col};
}

Token Lexer::scan_ident_or_keyword() {
    int start_line = line_, start_col = col_;
    std::string ident;
    while (pos_ < src_.size() && (std::isalnum(peek()) || peek() == '_')) {
        ident += advance();
    }
    auto it = KEYWORDS.find(ident);
    TokenType type = (it != KEYWORDS.end()) ? it->second : TokenType::IDENTIFIER;
    return {type, ident, start_line, start_col};
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        skip_whitespace_and_comments();
        if (pos_ >= src_.size()) {
            tokens.push_back({TokenType::EOF_TOK, "", line_, col_});
            break;
        }

        int tok_line = line_, tok_col = col_;
        char c = peek();

        if (std::isdigit(c)) { tokens.push_back(scan_number());           continue; }
        if (std::isalpha(c) || c == '_') { tokens.push_back(scan_ident_or_keyword()); continue; }

        advance(); // consume single-char token

        switch (c) {
            case '+': tokens.push_back({TokenType::PLUS,      "+",  tok_line, tok_col}); break;
            case '*': tokens.push_back({TokenType::STAR,      "*",  tok_line, tok_col}); break;
            case '/': tokens.push_back({TokenType::SLASH,     "/",  tok_line, tok_col}); break;
            case '%': tokens.push_back({TokenType::PERCENT,   "%",  tok_line, tok_col}); break;
            case '(': tokens.push_back({TokenType::LPAREN,    "(",  tok_line, tok_col}); break;
            case ')': tokens.push_back({TokenType::RPAREN,    ")",  tok_line, tok_col}); break;
            case '{': tokens.push_back({TokenType::LBRACE,    "{",  tok_line, tok_col}); break;
            case '}': tokens.push_back({TokenType::RBRACE,    "}",  tok_line, tok_col}); break;
            case '[': tokens.push_back({TokenType::LBRACKET,  "[",  tok_line, tok_col}); break;
            case ']': tokens.push_back({TokenType::RBRACKET,  "]",  tok_line, tok_col}); break;
            case ',': tokens.push_back({TokenType::COMMA,     ",",  tok_line, tok_col}); break;
            case ':': tokens.push_back({TokenType::COLON,     ":",  tok_line, tok_col}); break;
            case ';': tokens.push_back({TokenType::SEMICOLON, ";",  tok_line, tok_col}); break;
            case '-':
                if (peek() == '>') { advance(); tokens.push_back({TokenType::ARROW,  "->", tok_line, tok_col}); }
                else               {            tokens.push_back({TokenType::MINUS,  "-",  tok_line, tok_col}); }
                break;
            case '=':
                if (peek() == '=') { advance(); tokens.push_back({TokenType::EQ_EQ,  "==", tok_line, tok_col}); }
                else               {            tokens.push_back({TokenType::EQ,      "=",  tok_line, tok_col}); }
                break;
            case '!':
                if (peek() == '=') { advance(); tokens.push_back({TokenType::BANG_EQ, "!=", tok_line, tok_col}); }
                else throw std::runtime_error(
                    "[line " + std::to_string(tok_line) + ":" + std::to_string(tok_col) +
                    "] Unexpected character '!'");
                break;
            case '<':
                if (peek() == '=') { advance(); tokens.push_back({TokenType::LT_EQ, "<=", tok_line, tok_col}); }
                else               {            tokens.push_back({TokenType::LT,    "<",  tok_line, tok_col}); }
                break;
            case '>':
                if (peek() == '=') { advance(); tokens.push_back({TokenType::GT_EQ, ">=", tok_line, tok_col}); }
                else               {            tokens.push_back({TokenType::GT,    ">",  tok_line, tok_col}); }
                break;
            default:
                throw std::runtime_error(
                    "[line " + std::to_string(tok_line) + ":" + std::to_string(tok_col) +
                    "] Unexpected character '" + c + "'");
        }
    }

    return tokens;
}

const char* token_type_name(TokenType t) {
    switch (t) {
        case TokenType::INT_LIT:    return "INT_LIT";
        case TokenType::FLOAT_LIT:  return "FLOAT_LIT";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::KW_INT:     return "KW_INT";
        case TokenType::KW_FLOAT:   return "KW_FLOAT";
        case TokenType::KW_BOOL:    return "KW_BOOL";
        case TokenType::KW_LET:     return "KW_LET";
        case TokenType::KW_FN:      return "KW_FN";
        case TokenType::KW_RETURN:  return "KW_RETURN";
        case TokenType::KW_IF:      return "KW_IF";
        case TokenType::KW_ELSE:    return "KW_ELSE";
        case TokenType::KW_WHILE:   return "KW_WHILE";
        case TokenType::KW_PRINT:   return "KW_PRINT";
        case TokenType::KW_TRUE:    return "KW_TRUE";
        case TokenType::KW_FALSE:   return "KW_FALSE";
        case TokenType::PLUS:       return "PLUS";
        case TokenType::MINUS:      return "MINUS";
        case TokenType::STAR:       return "STAR";
        case TokenType::SLASH:      return "SLASH";
        case TokenType::PERCENT:    return "PERCENT";
        case TokenType::EQ_EQ:      return "EQ_EQ";
        case TokenType::BANG_EQ:    return "BANG_EQ";
        case TokenType::LT:         return "LT";
        case TokenType::GT:         return "GT";
        case TokenType::LT_EQ:      return "LT_EQ";
        case TokenType::GT_EQ:      return "GT_EQ";
        case TokenType::EQ:         return "EQ";
        case TokenType::LPAREN:     return "LPAREN";
        case TokenType::RPAREN:     return "RPAREN";
        case TokenType::LBRACE:     return "LBRACE";
        case TokenType::RBRACE:     return "RBRACE";
        case TokenType::LBRACKET:   return "LBRACKET";
        case TokenType::RBRACKET:   return "RBRACKET";
        case TokenType::COMMA:      return "COMMA";
        case TokenType::COLON:      return "COLON";
        case TokenType::SEMICOLON:  return "SEMICOLON";
        case TokenType::ARROW:      return "ARROW";
        case TokenType::EOF_TOK:    return "EOF";
        default:                    return "?";
    }
}
