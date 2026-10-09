#pragma once
#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include <string>
#include <vector>

// Serializes the (type-annotated) AST as a generic tree:
//   {"cat":"decl|stmt|expr","label":..,"detail":..,"type":..,"line":..,"col":..,
//    "children":[{"edge":..,"node":{...}}]}
std::string ast_to_json(const Program& prog);
std::string tokens_to_json(const std::vector<Token>& tokens);
