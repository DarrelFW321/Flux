#include <emscripten/bind.h>
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/typechecker.hpp"
#include "frontend/dumper.hpp"
#include <stdexcept>
#include <string>

// Mirrors the --dump-stages JSON shape but only covers the frontend pipeline
// (tokens + AST + type checking). IR stays on the native backend.
static std::string compile_frontend(const std::string& source) {
    std::string tokens_json, ast_json, error_msg;

    try {
        Lexer lexer(source);
        auto  tokens = lexer.tokenize();
        tokens_json  = dump_tokens_json(tokens);

        Parser  parser(std::move(tokens));
        Program prog = parser.parse();
        ast_json     = dump_ast_json(prog);

        TypeChecker tc;
        tc.check(prog);
    } catch (const std::exception& e) {
        error_msg = e.what();
    }

    auto escape = [](const std::string& s) -> std::string {
        if (s.empty()) return "null";
        std::string out = "\"";
        for (char c : s) {
            if      (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else                out += c;
        }
        return out + "\"";
    };

    return "{\"tokens\":"  + (tokens_json.empty() ? "null" : tokens_json) +
           ",\"ast\":"     + (ast_json.empty()    ? "null" : ast_json)    +
           ",\"error\":"   + escape(error_msg) + "}";
}

EMSCRIPTEN_BINDINGS(flux) {
    emscripten::function("compile_frontend", &compile_frontend);
}
