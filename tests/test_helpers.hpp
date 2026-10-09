#pragma once
#include "driver.hpp"
#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/typechecker.hpp"
#include <string>

inline Program parse_source(const std::string& src) {
    return Parser(Lexer(src).tokenize()).parse();
}

// Type-checks `src` and returns the diagnostics.
inline Diagnostics check_source(const std::string& src) {
    Diagnostics d;
    Program p = parse_source(src);
    TypeChecker tc(d);
    tc.check(p);
    return d;
}

inline bool has_message(const Diagnostics& d, const std::string& needle,
                        Diagnostic::Severity sev = Diagnostic::Severity::Error) {
    for (const auto& x : d.all())
        if (x.severity == sev && x.message.find(needle) != std::string::npos) return true;
    return false;
}

// Wraps a fragment-shader body so tests can focus on statements.
inline std::string frag(const std::string& body, const std::string& globals = "") {
    return globals + "\n@fragment fn main(@builtin(position) p: vec4) -> vec4 {\n" + body + "\n}\n";
}

inline int count(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (size_t pos = hay.find(needle); pos != std::string::npos; pos = hay.find(needle, pos + 1)) ++n;
    return n;
}
