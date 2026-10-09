#include "frontend/lexer.hpp"
#include <cctype>
#include <unordered_map>

const char* token_type_name(TokenType t) {
    switch (t) {
        case TokenType::INT_LIT:     return "INT_LIT";
        case TokenType::FLOAT_LIT:   return "FLOAT_LIT";
        case TokenType::IDENTIFIER:  return "IDENTIFIER";
        case TokenType::TYPE:        return "TYPE";
        case TokenType::KW_FN:       return "KW_FN";
        case TokenType::KW_LET:      return "KW_LET";
        case TokenType::KW_VAR:      return "KW_VAR";
        case TokenType::KW_CONST:    return "KW_CONST";
        case TokenType::KW_UNIFORM:  return "KW_UNIFORM";
        case TokenType::KW_RETURN:   return "KW_RETURN";
        case TokenType::KW_IF:       return "KW_IF";
        case TokenType::KW_ELSE:     return "KW_ELSE";
        case TokenType::KW_FOR:      return "KW_FOR";
        case TokenType::KW_IN:       return "KW_IN";
        case TokenType::KW_WHILE:    return "KW_WHILE";
        case TokenType::KW_BREAK:    return "KW_BREAK";
        case TokenType::KW_CONTINUE: return "KW_CONTINUE";
        case TokenType::KW_DISCARD:  return "KW_DISCARD";
        case TokenType::KW_TRUE:     return "KW_TRUE";
        case TokenType::KW_FALSE:    return "KW_FALSE";
        case TokenType::PLUS:        return "PLUS";
        case TokenType::MINUS:       return "MINUS";
        case TokenType::STAR:        return "STAR";
        case TokenType::SLASH:       return "SLASH";
        case TokenType::PERCENT:     return "PERCENT";
        case TokenType::PLUS_EQ:     return "PLUS_EQ";
        case TokenType::MINUS_EQ:    return "MINUS_EQ";
        case TokenType::STAR_EQ:     return "STAR_EQ";
        case TokenType::SLASH_EQ:    return "SLASH_EQ";
        case TokenType::EQ:          return "EQ";
        case TokenType::EQ_EQ:       return "EQ_EQ";
        case TokenType::BANG_EQ:     return "BANG_EQ";
        case TokenType::LT:          return "LT";
        case TokenType::GT:          return "GT";
        case TokenType::LT_EQ:       return "LT_EQ";
        case TokenType::GT_EQ:       return "GT_EQ";
        case TokenType::AMP_AMP:     return "AMP_AMP";
        case TokenType::PIPE_PIPE:   return "PIPE_PIPE";
        case TokenType::BANG:        return "BANG";
        case TokenType::QUESTION:    return "QUESTION";
        case TokenType::LPAREN:      return "LPAREN";
        case TokenType::RPAREN:      return "RPAREN";
        case TokenType::LBRACE:      return "LBRACE";
        case TokenType::RBRACE:      return "RBRACE";
        case TokenType::LBRACKET:    return "LBRACKET";
        case TokenType::RBRACKET:    return "RBRACKET";
        case TokenType::COMMA:       return "COMMA";
        case TokenType::COLON:       return "COLON";
        case TokenType::SEMICOLON:   return "SEMICOLON";
        case TokenType::DOT:         return "DOT";
        case TokenType::DOT_DOT:     return "DOT_DOT";
        case TokenType::ARROW:       return "ARROW";
        case TokenType::AT:          return "AT";
        case TokenType::EOF_TOK:     return "EOF";
    }
    return "UNKNOWN";
}

static const std::unordered_map<std::string, TokenType> KEYWORDS = {
    {"fn", TokenType::KW_FN},           {"let", TokenType::KW_LET},
    {"var", TokenType::KW_VAR},         {"const", TokenType::KW_CONST},
    {"uniform", TokenType::KW_UNIFORM}, {"return", TokenType::KW_RETURN},
    {"if", TokenType::KW_IF},           {"else", TokenType::KW_ELSE},
    {"for", TokenType::KW_FOR},         {"in", TokenType::KW_IN},
    {"while", TokenType::KW_WHILE},     {"break", TokenType::KW_BREAK},
    {"continue", TokenType::KW_CONTINUE}, {"discard", TokenType::KW_DISCARD},
    {"true", TokenType::KW_TRUE},       {"false", TokenType::KW_FALSE},
};

static const char* TYPE_NAMES[] = {
    "void", "bool", "int", "float", "vec2", "vec3", "vec4", "mat2", "mat3", "mat4",
};

Lexer::Lexer(std::string src) : src_(std::move(src)) {}

char Lexer::peek(int offset) const {
    size_t i = pos_ + static_cast<size_t>(offset);
    return i < src_.size() ? src_[i] : '\0';
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') { ++line_; col_ = 1; }
    else           { ++col_; }
    return c;
}

void Lexer::skip_whitespace_and_comments() {
    for (;;) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            while (peek() != '\n' && peek() != '\0') advance();
        } else if (c == '/' && peek(1) == '*') {
            int line = line_, col = col_;
            advance(); advance();
            while (!(peek() == '*' && peek(1) == '/')) {
                if (peek() == '\0') throw CompileError({line, col, 2}, "unterminated block comment");
                advance();
            }
            advance(); advance();
        } else {
            return;
        }
    }
}

Token Lexer::scan_number() {
    int line = line_, col = col_;
    size_t start = pos_;
    bool is_float = false;

    while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    // A '.' starts a fraction unless it begins a range operator (`0..10`).
    if (peek() == '.' && peek(1) != '.') {
        is_float = true;
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }
    if (peek() == 'e' || peek() == 'E') {
        char n1 = peek(1), n2 = peek(2);
        bool exp = std::isdigit(static_cast<unsigned char>(n1)) ||
                   ((n1 == '+' || n1 == '-') && std::isdigit(static_cast<unsigned char>(n2)));
        if (exp) {
            is_float = true;
            advance();
            if (peek() == '+' || peek() == '-') advance();
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
        }
    }
    std::string text = src_.substr(start, pos_ - start);
    if (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_')
        throw CompileError({line, col, static_cast<int>(text.size()) + 1},
                           "invalid numeric literal '" + text + peek() + "'");
    return {is_float ? TokenType::FLOAT_LIT : TokenType::INT_LIT, text, line, col};
}

Token Lexer::scan_word() {
    int line = line_, col = col_;
    size_t start = pos_;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
    std::string word = src_.substr(start, pos_ - start);

    auto kw = KEYWORDS.find(word);
    if (kw != KEYWORDS.end()) return {kw->second, word, line, col};
    for (const char* t : TYPE_NAMES)
        if (word == t) return {TokenType::TYPE, word, line, col};
    return {TokenType::IDENTIFIER, word, line, col};
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    for (;;) {
        skip_whitespace_and_comments();
        int line = line_, col = col_;
        char c = peek();
        if (c == '\0') { out.push_back({TokenType::EOF_TOK, "", line, col}); break; }

        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
            out.push_back(scan_number());
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            out.push_back(scan_word());
            continue;
        }

        auto two = [&](char next, TokenType yes, TokenType no) {
            advance();
            if (peek() == next) {
                advance();
                return Token{yes, std::string{c, next}, line, col};
            }
            return Token{no, std::string(1, c), line, col};
        };
        auto one = [&](TokenType t) {
            advance();
            return Token{t, std::string(1, c), line, col};
        };

        switch (c) {
            case '+': out.push_back(two('=', TokenType::PLUS_EQ, TokenType::PLUS)); break;
            case '*': out.push_back(two('=', TokenType::STAR_EQ, TokenType::STAR)); break;
            case '/': out.push_back(two('=', TokenType::SLASH_EQ, TokenType::SLASH)); break;
            case '%': out.push_back(one(TokenType::PERCENT)); break;
            case '=': out.push_back(two('=', TokenType::EQ_EQ, TokenType::EQ)); break;
            case '!': out.push_back(two('=', TokenType::BANG_EQ, TokenType::BANG)); break;
            case '<': out.push_back(two('=', TokenType::LT_EQ, TokenType::LT)); break;
            case '>': out.push_back(two('=', TokenType::GT_EQ, TokenType::GT)); break;
            case '.': out.push_back(two('.', TokenType::DOT_DOT, TokenType::DOT)); break;
            case '-':
                advance();
                if (peek() == '>')      { advance(); out.push_back({TokenType::ARROW, "->", line, col}); }
                else if (peek() == '=') { advance(); out.push_back({TokenType::MINUS_EQ, "-=", line, col}); }
                else                    out.push_back({TokenType::MINUS, "-", line, col});
                break;
            case '&':
                advance();
                if (peek() != '&') throw CompileError({line, col, 1}, "expected '&&' (Flux has no bitwise '&')");
                advance();
                out.push_back({TokenType::AMP_AMP, "&&", line, col});
                break;
            case '|':
                advance();
                if (peek() != '|') throw CompileError({line, col, 1}, "expected '||' (Flux has no bitwise '|')");
                advance();
                out.push_back({TokenType::PIPE_PIPE, "||", line, col});
                break;
            case '?': out.push_back(one(TokenType::QUESTION)); break;
            case '(': out.push_back(one(TokenType::LPAREN)); break;
            case ')': out.push_back(one(TokenType::RPAREN)); break;
            case '{': out.push_back(one(TokenType::LBRACE)); break;
            case '}': out.push_back(one(TokenType::RBRACE)); break;
            case '[': out.push_back(one(TokenType::LBRACKET)); break;
            case ']': out.push_back(one(TokenType::RBRACKET)); break;
            case ',': out.push_back(one(TokenType::COMMA)); break;
            case ':': out.push_back(one(TokenType::COLON)); break;
            case ';': out.push_back(one(TokenType::SEMICOLON)); break;
            case '@': out.push_back(one(TokenType::AT)); break;
            default:
                throw CompileError({line, col, 1}, std::string("unexpected character '") + c + "'");
        }
    }
    return out;
}
