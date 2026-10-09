#pragma once
#include "common/types.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// FluxIR — a structured SSA IR.
//
// Every instruction that produces a value defines a fresh SSA id (%N). Control
// flow is *structured*: `if` and `loop` own nested Regions instead of jumping
// between basic blocks. That is exactly the shape both targets want — WGSL
// has only structured statements, and SPIR-V requires structured control flow
// (OpSelectionMerge / OpLoopMerge) — so neither backend has to reconstruct
// structure from a CFG.
//
// Mutable `var`s are memory slots (Var / Load / Store). `let` bindings,
// parameters and temporaries are plain SSA values. A value defined in a
// region is visible to later instructions in that region and in its nested
// regions, never outside it — cross-region dataflow goes through a Var.
namespace ir {

enum class Op {
    // Leaves
    Const,       // scalar constant (fconst / iconst, by type)
    Arg,         // function parameter #imm
    Uniform,     // uniform-block member #imm
    // Arithmetic — operands share the result type (vectors are componentwise)
    Add, Sub, Mul, Div, Rem, Neg,
    MatMul,      // mat*mat, mat*vec, vec*mat
    MatScale,    // mat * float
    // Comparison & logic — scalar operands, bool result
    Eq, Ne, Lt, Le, Gt, Ge, And, Or, Not,
    Select,      // args: cond, if_true, if_false
    // Composites
    Construct,   // vector from scalars/vectors, or matrix from columns
    Splat,       // scalar → vector
    Extract,     // component/column #imm
    ExtractDyn,  // args: vector, int index
    Insert,      // args: composite, part; replaces component/column #imm
    Swizzle,     // lanes
    Shuffle,     // args: a, b; lanes index into concat(a, b)
    Convert,     // int ⇄ float
    Builtin,     // math builtin #imm (BuiltinFn)
    Call,        // user function `callee`
    // Memory
    Var,         // function-local mutable slot holding a `type`
    Load,        // args: var
    Store,       // args: var, value
    // Structured control flow
    If,          // args: cond; regions: then, else
    Loop,        // regions: header, body, continuing. Runs header, exits unless `cond`, then body, continuing.
    Break, Continue,
    Return,      // args: value (optional)
    Discard,
};

const char* op_name(Op op);

struct Inst;

struct Region {
    std::vector<Inst> insts;
};

struct Inst {
    int         id = -1;          // SSA id of the result (-1: no result)
    Op          op = Op::Const;
    Type        type = Type::void_();
    std::vector<int> args;
    std::vector<int> lanes;       // Swizzle / Shuffle
    int         imm = 0;          // Arg/Uniform index, Extract/Insert index, BuiltinFn
    double      fconst = 0.0;
    long long   iconst = 0;       // ints and bools
    std::string callee;           // Call
    std::string name;             // name hint from source (`let uv = ...`)
    int         line = 0;         // source line, for source maps
    std::vector<Region> regions;  // If: {then, else}; Loop: {header, body, continuing}
    int         cond = -1;        // Loop: id of the header's continue condition (-1: infinite)

    // `for i in a..b` loops remember their shape so the unroller can find them.
    int         for_var = -1, for_start = -1, for_end = -1;
};

struct EntryParam {
    std::string builtin;          // "position", "vertex_index", ... or empty
    int         location = -1;    // @location(n), or -1
};

struct Function {
    std::string                name;
    std::vector<std::string>   param_names;
    std::vector<Type>          param_types;
    std::vector<EntryParam>    entry_params;   // entry points only
    Type                       return_type = Type::void_();
    Stage                      stage = Stage::None;
    Region                     body;
    int                        next_id = 0;
    int                        line = 0;

    int fresh() { return next_id++; }
};

struct UniformMember {
    std::string name;
    Type        type;
    int         offset = 0;
};

struct Module {
    std::vector<UniformMember> uniforms;
    std::vector<Function>      functions;

    Function*       find(const std::string& name);
    const Function* find(const std::string& name) const;
};

// ── Utilities ────────────────────────────────────────────────────────────────

bool is_terminator(Op op);
// True if control never falls out of the end of this region.
bool region_terminates(const Region& r);
// True if the instruction has no side effects and may be deleted when unused
// or merged with an identical instruction (CSE). Loads read memory and so are
// not pure in that sense.
bool is_pure(Op op);
// Deletes instructions that follow a terminator (or an `if` whose arms both
// terminate). Returns the number of instructions removed.
int  truncate_unreachable(Region& r);

void for_each_inst(Region& r, const std::function<void(Inst&)>& fn);
void for_each_inst(const Region& r, const std::function<void(const Inst&)>& fn);

// Number of instructions, recursively.
int  count_insts(const Region& r);
int  count_insts_one(const Inst& i);
// Counts how many times each value id is used as an operand.
std::unordered_map<int, int> use_counts(const Function& f);

// Deep-copies `src`, giving every defined value a fresh id in `f`. `remap`
// maps old ids to new ids (pre-seeded entries rewrite operands, e.g. Arg →
// actual argument when inlining).
Region clone_region(const Region& src, Function& f, std::unordered_map<int, int>& remap);

// ── Printing ─────────────────────────────────────────────────────────────────
struct Printed {
    std::string      text;
    std::vector<int> line_map;   // output line (0-based) → source line (0 = none)
};
Printed print_module(const Module& m);

} // namespace ir
