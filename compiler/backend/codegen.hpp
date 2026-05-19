#pragma once
#include "frontend/types.hpp"
#include "midend/ir.hpp"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Lowers a FluxIR `mir::Module` to an LLVM module.
//
// All optimization happens at the FluxIR level; this pass is intentionally
// "dumb": one MIR instruction → one (or a small fixed pattern of) LLVM
// instruction(s). Whole-array ops (array.op, reduce.sum, reduce.dot) are the
// only places where a single MIR inst expands into a loop.
//
// ABI choices that survive from the AST-driven codegen:
//   • Scalar params spill into an entry-block alloca (LLVM pre-mem2reg style).
//   • Arrays pass by pointer.
//   • Array returns use a hidden out-pointer: `void f(ptr %ret, ...args)`.
class CodeGen {
public:
    CodeGen();
    std::unique_ptr<llvm::Module> generate(const mir::Module& m);

private:
    llvm::LLVMContext                  ctx_;
    std::unique_ptr<llvm::Module>      module_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;
    llvm::Function*                    printf_fn_ = nullptr;

    // Function-level info. For array-returning functions, the LLVM signature
    // has a hidden first pointer parameter holding the return slot.
    struct FnInfo {
        llvm::Function*       fn;
        FluxType              return_type;
        std::vector<FluxType> param_types;
        bool returns_array() const { return return_type.is_array(); }
    };
    std::unordered_map<std::string, FnInfo> functions_;

    // Per-function state (cleared between functions).
    llvm::Function*  current_fn_           = nullptr;
    FluxType         current_return_type_  = FluxType::scalar(FluxType::Kind::Void);
    llvm::Value*     current_ret_buf_      = nullptr;  // hidden out-buffer for array-returning fns

    std::unordered_map<mir::ValueId, llvm::Value*> values_;
    std::unordered_map<mir::ValueId, FluxType>     value_types_;
    std::unordered_map<mir::BlockId, llvm::BasicBlock*> blocks_;

    // ── Setup ──
    void declare_builtins();
    void declare_function(const mir::Function& fn);

    // ── Function / block / inst lowering ──
    void gen_function   (const mir::Function& fn);
    void gen_inst       (const mir::Inst& i);
    void gen_terminator (const mir::Terminator& t);

    // ── Specialized emitters for MIR ops that expand to LLVM loops ──
    llvm::Value* emit_array_literal(const mir::Inst& i);
    llvm::Value* emit_array_op     (const mir::Inst& i);
    llvm::Value* emit_array_copy   (const mir::Inst& i);
    llvm::Value* emit_reduce_sum   (const mir::Inst& i);
    llvm::Value* emit_reduce_dot   (const mir::Inst& i);
    llvm::Value* emit_index_load   (const mir::Inst& i);
    void         emit_index_store  (const mir::Inst& i);
    llvm::Value* emit_call         (const mir::Inst& i);
    void         emit_print        (const mir::Inst& i);

    // ── Helpers ──
    llvm::Value*      emit_scalar_binop(mir::Op op, llvm::Value* lhs, llvm::Value* rhs, FluxType::Kind k);
    llvm::AllocaInst* create_entry_alloca(const std::string& name, llvm::Type* ty);
    void              emit_array_memcpy(llvm::Value* dest, llvm::Value* src, const FluxType& arr_t);

    // ── Type helpers ──
    llvm::Type* llvm_scalar_type(FluxType::Kind k);
    llvm::Type* llvm_storage_type(const FluxType& t);   // scalar or [N x scalar]
    llvm::Type* llvm_param_type  (const FluxType& t);   // ptr for arrays, scalar otherwise

    // ── Value bookkeeping ──
    llvm::Value*    get_value (mir::ValueId id) const;
    const FluxType& value_type(mir::ValueId id) const;
    void            bind_value(mir::ValueId id, llvm::Value* v, FluxType t);
};
