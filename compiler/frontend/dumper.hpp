#pragma once
#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include <string>
#include <vector>

// Each function returns a JSON string (no trailing newline).
std::string dump_tokens_json(const std::vector<Token>& tokens);
std::string dump_ast_json(const Program& prog);
