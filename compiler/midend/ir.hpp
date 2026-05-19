#pragma once
#include "frontend/types.hpp"
#include <cstdint>
#include <string>
#include <vector>

// ── FluxIR — a typed, block-structured mid-level IR ─────────────────────────
//
// Sits between the AST and LLVM IR. Designed so that whole-array operations
// (`array.op`, `reduce.sum`, `reduce.dot`) appear as single instructions,
// which gives optimization passes a high-level place to work before any
// scalar loops are emitted by the LLVM backend.
//
// Layout (closely mirrors LLVM IR but at a higher level):
//   Module { Function* }
//   Function { name, signature, params, Block* }
//   Block    { id, label, Inst*, Terminator }
//   Inst     { op, result?, operands..., immediates... }
//
// Every value has a (FluxType, ValueRole) pair so lowerings can tell the
// difference between an SSA scalar, an array pointer, and an alloca slot.
namespace mir {

using ValueId = uint32_t;
using BlockId = uint32_t;
constexpr ValueId NO_VALUE = 0;

enum class Op {
    // Constants
    CONST_INT, CONST_FLOAT, CONST_BOOL,
    // Scalar storage slots (think LLVM alloca/load/store)
    ALLOCA, LOAD, STORE,
    // Scalar arithmetic
    NEG, ADD, SUB, MUL, DIV, MOD,
    // Comparison (always returns bool)
    CMP_EQ, CMP_NE, CMP_LT, CMP_GT, CMP_LE, CMP_GE,
    // Array operations — operate on whole arrays as single ops
    ARRAY_LIT,     // operands: element values; result: array
    ARRAY_OP,      // sval: arith op ("+","-","*","/","%"); operands: lhs, rhs (either may be scalar for broadcast)
    INDEX_LOAD,    // operands: array, index; result: element
    INDEX_STORE,   // operands: array, index, value
    ARRAY_COPY,    // operand: src array; result: fresh copy
    REDUCE_SUM,    // operand: array; result: scalar
    REDUCE_DOT,    // operands: a, b; result: scalar
    // Calls and I/O
    CALL,          // sval: callee; operands: args; result: scalar or array
    PRINT,         // operand: value
};

const char* op_mnemonic(Op op);

// How a ValueId should be interpreted.
enum class ValueRole {
    SCALAR,   // SSA scalar value
    ARRAY,    // SSA value is a pointer to array storage
    SLOT,     // SSA value is a pointer to scalar storage (from ALLOCA)
};

struct Inst {
    Op        op;
    ValueId   result      = NO_VALUE;
    FluxType  result_type {};
    ValueRole result_role = ValueRole::SCALAR;
    std::vector<ValueId> operands;
    int64_t      ival = 0;
    double       fval = 0.0;
    bool         bval = false;
    std::string  sval;
};

struct Terminator {
    enum Kind { NONE, BR, BR_COND, RET, RET_VOID } kind = NONE;
    BlockId target_t = 0;        // BR or BR_COND-then
    BlockId target_f = 0;        // BR_COND-else
    ValueId cond     = NO_VALUE;
    ValueId ret_val  = NO_VALUE;
};

struct Block {
    BlockId      id    = 0;
    std::string  label;
    std::vector<Inst> insts;
    Terminator   term;
};

struct Param {
    ValueId      id;
    std::string  name;
    FluxType     type;
    ValueRole    role;
};

struct Function {
    std::string         name;
    FluxType            return_type {};
    std::vector<Param>  params;
    std::vector<Block>  blocks;
    bool                is_synthetic_main = false;

    // Allocator state
    ValueId next_value = 1;   // 0 reserved as "no value"
    BlockId next_block = 0;

    BlockId new_block(const std::string& label) {
        Block b;
        b.id    = next_block++;
        b.label = label;
        blocks.push_back(std::move(b));
        return blocks.back().id;
    }
    ValueId new_value_id() { return next_value++; }
};

struct Module {
    std::vector<Function> functions;
};

// LLVM-IR-like pretty-printer for an entire module.
std::string print_module(const Module& m);

}  // namespace mir
