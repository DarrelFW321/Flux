#pragma once
#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"

inline Program parse_source(const std::string& src) {
    Lexer  lexer(src);
    Parser parser(lexer.tokenize());
    return parser.parse();
}
