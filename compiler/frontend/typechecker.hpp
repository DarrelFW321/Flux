#pragma once
#include "common/diagnostics.hpp"
#include "frontend/ast.hpp"
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ── Reflection ───────────────────────────────────────────────────────────────
// What a host (the WebGPU playground) needs to drive a compiled shader: the
// uniform block layout and the entry points.
struct UniformInfo {
    std::string         name;
    Type                type;
    int                 offset = 0;     // byte offset in the uniform block
    int                 size   = 0;     // byte size
    std::vector<double> default_value;  // components, column-major for matrices
    bool                has_range = false;
    double              range_min = 0.0, range_max = 1.0;
    bool                color = false;  // @color: show a colour picker
    int                 line  = 0;
};

struct EntryInfo {
    std::string name;
    Stage       stage = Stage::None;
};

struct Reflection {
    std::vector<UniformInfo> uniforms;
    int                      uniform_block_size = 0;   // rounded up to 16 bytes
    std::vector<EntryInfo>   entries;
};

// Size and alignment of a type in the uniform address space. These rules are
// identical in WGSL's uniform layout and SPIR-V std140 for every type Flux
// allows in a uniform (mat2 and bool are rejected).
int uniform_align(Type t);
int uniform_size(Type t);

// ── Type checker ─────────────────────────────────────────────────────────────
// Resolves names, infers and checks types, inserts implicit int-literal →
// float coercions, validates entry-point interfaces, and computes uniform
// reflection. Reports every problem it can find through Diagnostics rather
// than stopping at the first one.
class TypeChecker {
public:
    explicit TypeChecker(Diagnostics& diags) : diags_(diags) {}

    void check(Program& prog);
    const Reflection& reflection() const { return reflection_; }

private:
    struct Symbol {
        SymbolKind  kind = SymbolKind::Unresolved;
        Type        type;
        const void* decl = nullptr;
        Stmt*       stmt  = nullptr;   // Let/Var: usage flags live here
        Param*      param = nullptr;   // Param: usage flag
        SourceLoc   loc;
    };
    using Scope = std::unordered_map<std::string, Symbol>;

    Diagnostics&       diags_;
    Reflection         reflection_;
    std::vector<Scope> scopes_;
    std::unordered_map<std::string, FnDecl*> functions_;
    std::unordered_map<const ConstDecl*, std::vector<double>> const_values_;

    // Per-function state
    FnDecl* current_fn_ = nullptr;
    int     loop_depth_ = 0;
    bool    in_const_init_ = false;
    std::unordered_map<const FnDecl*, std::unordered_set<const FnDecl*>> calls_;

    enum class Flow { Normal, Jumps, Returns };

    void push_scope();
    void pop_scope();
    void declare(const std::string& name, Symbol sym);
    Symbol* lookup(const std::string& name);

    void check_uniform(UniformDecl& u, int& offset);
    void check_const(ConstDecl& c);
    void check_signature(FnDecl& fn);
    void check_entry_interface(FnDecl& fn);
    void check_body(FnDecl& fn);
    void check_recursion();

    Flow check_block(BlockStmt& block, bool new_scope = true);
    Flow check_stmt(Stmt& s);
    void check_assign(Stmt& s);

    Type check_expr(Expr& e);
    Type check_unary(Expr& e);
    Type check_binary(Expr& e);
    Type binary_type(const std::string& op, Expr& l, Expr& r, SourceLoc loc);
    Type check_ternary(Expr& e);
    Type check_call(Expr& e);
    Type check_builtin(Expr& e, int builtin);
    Type check_construct(Expr& e);
    Type check_swizzle(Expr& e);
    Type check_index(Expr& e);

    // Coerces `e` to `target` if it is an int literal and `target` is float.
    bool coerce(Expr& e, Type target);
    void expect_type(Expr& e, Type target, const std::string& what);

    std::optional<std::vector<double>> eval_const(const Expr& e);
};

// Parses a swizzle like "xyz" / "rgba" into component indices. Returns false
// on mixed sets or invalid letters.
bool parse_swizzle(const std::string& s, std::vector<int>& out);
