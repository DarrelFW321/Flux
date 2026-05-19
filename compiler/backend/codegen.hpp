#pragma once
#include "frontend/ast.hpp"
#include "frontend/types.hpp"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <memory>
#include <string>
#include <unordered_map>

class CodeGen {
public:
    CodeGen();
    // Caller must keep this CodeGen alive while using the returned module
    // (the module shares the LLVMContext owned by this object).
    std::unique_ptr<llvm::Module> generate(const Program& prog);

private:
    llvm::LLVMContext              ctx_;
    std::unique_ptr<llvm::Module>  module_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;

    // Per-variable binding.
    //   Scalar:  ptr = alloca holding the scalar value           (load to read)
    //   Array :  ptr = pointer to the first element of the array (GEP to access)
    struct Binding {
        llvm::Value* ptr;
        FluxType     type;
    };

    // Per-function metadata. For array-returning functions, the LLVM signature
    // has a hidden first pointer parameter into which the result is written;
    // the user-visible return type is recorded here for callers to look up.
    struct FnInfo {
        llvm::Function*       fn;
        FluxType              return_type;
        std::vector<FluxType> param_types;
        bool returns_array() const { return return_type.is_array(); }
    };

    std::unordered_map<std::string, Binding> named_values_;
    std::unordered_map<std::string, FnInfo>  functions_;

    llvm::Function* current_fn_         = nullptr;
    llvm::Function* printf_fn_          = nullptr;
    FluxType        current_return_type_ = FluxType::scalar(FluxType::Kind::Void);

    void declare_builtins();
    void first_pass(const Program& prog);
    void gen_fn(const FnDecl& fn);
    void gen_top_level_stmts(const Program& prog);

    void              gen_stmt(const Stmt& s);
    void              gen_block(const BlockStmt& b);
    llvm::Value*      gen_expr(const Expr& e);                    // scalar value, or ptr to array
    FluxType          expr_type(const Expr& e) const;             // static type re-derivation

    // Array-aware helpers
    llvm::Value*      gen_array_literal(const ArrayLitExpr& a, const FluxType& t);
    // Element-wise op. lhs_t / rhs_t may be scalar (broadcast) or array;
    // at least one must be array, both share the same element kind.
    llvm::Value*      gen_array_binop  (const std::string& op,
                                        llvm::Value* lhs, FluxType lhs_t,
                                        llvm::Value* rhs, FluxType rhs_t,
                                        const FluxType& result_t);
    llvm::Value*      gen_index        (const IndexExpr& ix);
    llvm::Value*      gen_builtin_call (const CallExpr& call);
    llvm::Value*      gen_scalar_binop (const std::string& op, llvm::Value* lhs, llvm::Value* rhs,
                                        FluxType::Kind kind);
    void              emit_array_memcpy(llvm::Value* dest, llvm::Value* src, const FluxType& arr_t);

    // Creates an alloca at the entry block of current_fn_ (keeps mem2reg happy).
    llvm::AllocaInst* create_entry_alloca(const std::string& name, llvm::Type* ty);

    llvm::Type*       llvm_scalar_type(FluxType::Kind k);
    llvm::Type*       llvm_storage_type(const FluxType& t);  // scalar or [N x scalar]
    llvm::Type*       llvm_param_type  (const FluxType& t);  // ptr for arrays, scalar otherwise

    void emit_print(llvm::Value* val);
};
