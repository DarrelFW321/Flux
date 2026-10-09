#pragma once
#include <stdexcept>
#include <string>
#include <vector>

struct SourceLoc {
    int line = 0;   // 1-based; 0 means "unknown"
    int col  = 0;   // 1-based
    int len  = 1;   // length of the highlighted span, in columns
};

struct Diagnostic {
    enum class Severity { Error, Warning, Note };
    Severity    severity = Severity::Error;
    SourceLoc   loc;
    std::string message;
};

class Diagnostics {
public:
    void error(SourceLoc loc, std::string msg)   { add(Diagnostic::Severity::Error, loc, std::move(msg)); }
    void warning(SourceLoc loc, std::string msg) { add(Diagnostic::Severity::Warning, loc, std::move(msg)); }
    void note(SourceLoc loc, std::string msg)    { add(Diagnostic::Severity::Note, loc, std::move(msg)); }

    bool has_errors() const { return errors_ > 0; }
    int  error_count() const { return errors_; }
    const std::vector<Diagnostic>& all() const { return items_; }

    // Human-readable "file:line:col: error: msg" rendering for the CLI.
    std::string format(const std::string& file) const {
        std::string out;
        for (const auto& d : items_) {
            const char* sev = d.severity == Diagnostic::Severity::Error   ? "error"
                            : d.severity == Diagnostic::Severity::Warning ? "warning"
                                                                          : "note";
            out += file + ":" + std::to_string(d.loc.line) + ":" + std::to_string(d.loc.col)
                 + ": " + sev + ": " + d.message + "\n";
        }
        return out;
    }

private:
    std::vector<Diagnostic> items_;
    int errors_ = 0;

    void add(Diagnostic::Severity s, SourceLoc loc, std::string msg) {
        if (s == Diagnostic::Severity::Error) ++errors_;
        items_.push_back({s, loc, std::move(msg)});
    }
};

// Thrown by the lexer and parser, which stop at the first syntax error.
// The type checker reports through Diagnostics instead so it can surface
// every semantic problem in one pass.
struct CompileError : std::runtime_error {
    SourceLoc loc;
    CompileError(SourceLoc l, const std::string& msg) : std::runtime_error(msg), loc(l) {}
};
