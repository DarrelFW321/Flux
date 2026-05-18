#pragma once
#include "frontend/ast.hpp"
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

    // Variable name → alloca for the current function
    std::unordered_map<std::string, llvm::AllocaInst*> named_values_;
    // User-defined function name → LLVM Function*
    std::unordered_map<std::string, llvm::Function*>   functions_;

    llvm::Function* current_fn_  = nullptr;
    llvm::Function* printf_fn_   = nullptr;

    void declare_builtins();
    void first_pass(const Program& prog);
    void gen_fn(const FnDecl& fn);
    void gen_top_level_stmts(const Program& prog);

    llvm::Value*      gen_expr(const Expr& e);
    void              gen_stmt(const Stmt& s);
    void              gen_block(const BlockStmt& b);

    // Creates an alloca at the entry block of current_fn_ (keeps mem2reg happy).
    llvm::AllocaInst* create_entry_alloca(const std::string& name, llvm::Type* ty);
    llvm::Type*       llvm_type(const std::string& type_name);

    // Emit a printf call appropriate for the runtime type of val.
    void emit_print(llvm::Value* val);
};
