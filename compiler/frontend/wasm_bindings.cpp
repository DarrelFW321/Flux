#include <emscripten/bind.h>
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/typechecker.hpp"
#include "frontend/dumper.hpp"
#include "midend/ir.hpp"
#include "midend/lower_from_ast.hpp"
#include "midend/passes.hpp"
#include <stdexcept>
#include <string>

// Mirrors the --dump-stages JSON shape but only covers the LLVM-free pipeline
// (tokens + AST + type checking + MIR + optimization passes). LLVM IR
// generation requires the native backend.
static std::string compile_frontend(const std::string& source) {
    std::string tokens_json, ast_json, mir_raw, mir_opt, error_msg;
    mir::PassReport report;

    try {
        Lexer lexer(source);
        auto  tokens = lexer.tokenize();
        tokens_json  = dump_tokens_json(tokens);

        Parser  parser(std::move(tokens));
        Program prog = parser.parse();
        ast_json     = dump_ast_json(prog);

        TypeChecker tc;
        tc.check(prog);

        mir::Module mir_mod = mir::lower_program(prog);
        mir_raw = mir::print_module(mir_mod);

        report  = mir::default_pipeline().run(mir_mod);
        mir_opt = mir::print_module(mir_mod);
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

    std::string passes_json = "[";
    for (size_t i = 0; i < report.passes.size(); ++i) {
        if (i) passes_json += ",";
        passes_json += "{\"name\":" + escape(report.passes[i].name)
                     + ",\"changes\":" + std::to_string(report.passes[i].changes) + "}";
    }
    passes_json += "]";

    std::string steps_json = "[";
    for (size_t i = 0; i < report.steps.size(); ++i) {
        if (i) steps_json += ",";
        steps_json += "{\"name\":"      + escape(report.steps[i].name)
                   +  ",\"iteration\":" + std::to_string(report.steps[i].iteration)
                   +  ",\"changes\":"   + std::to_string(report.steps[i].changes)
                   +  ",\"mir_after\":" + escape(report.steps[i].mir_after) + "}";
    }
    steps_json += "]";

    return "{\"tokens\":"        + (tokens_json.empty() ? "null" : tokens_json) +
           ",\"ast\":"           + (ast_json.empty()    ? "null" : ast_json)    +
           ",\"mir_raw\":"       + escape(mir_raw) +
           ",\"mir_optimized\":" + escape(mir_opt) +
           ",\"passes\":"        + passes_json +
           ",\"pass_steps\":"    + steps_json +
           ",\"error\":"         + escape(error_msg) + "}";
}

EMSCRIPTEN_BINDINGS(flux) {
    emscripten::function("compile_frontend", &compile_frontend);
}
