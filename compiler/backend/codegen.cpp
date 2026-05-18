#include "backend/codegen.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <stdexcept>

CodeGen::CodeGen() {}

// ── Type helpers ──────────────────────────────────────────────────────────────

llvm::Type* CodeGen::llvm_type(const std::string& name) {
    if (name == "int")   return llvm::Type::getInt32Ty(ctx_);
    if (name == "float") return llvm::Type::getDoubleTy(ctx_);
    if (name == "bool")  return llvm::Type::getInt1Ty(ctx_);
    throw std::logic_error("unknown type: " + name);
}

llvm::AllocaInst* CodeGen::create_entry_alloca(const std::string& name, llvm::Type* ty) {
    llvm::IRBuilder<> tmp(&current_fn_->getEntryBlock(),
                          current_fn_->getEntryBlock().begin());
    return tmp.CreateAlloca(ty, nullptr, name);
}

// ── Builtins ──────────────────────────────────────────────────────────────────

void CodeGen::declare_builtins() {
    // int printf(char*, ...)
    auto* printf_ty = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(ctx_),
        {llvm::PointerType::getUnqual(ctx_)},
        /* isVarArg */ true);
    printf_fn_ = llvm::Function::Create(
        printf_ty, llvm::Function::ExternalLinkage, "printf", module_.get());
}

void CodeGen::emit_print(llvm::Value* val) {
    auto* ty = val->getType();
    llvm::Value* fmt_str;

    if (ty->isIntegerTy(32)) {
        fmt_str = builder_->CreateGlobalString("%d\n", ".fmt.int");
    } else if (ty->isDoubleTy()) {
        fmt_str = builder_->CreateGlobalString("%g\n", ".fmt.float");
    } else if (ty->isIntegerTy(1)) {
        // Zero-extend bool to i32 so printf sees an int argument.
        val     = builder_->CreateZExt(val, llvm::Type::getInt32Ty(ctx_), "b2i");
        fmt_str = builder_->CreateGlobalString("%d\n", ".fmt.bool");
    } else {
        throw std::runtime_error("emit_print: unsupported value type");
    }

    builder_->CreateCall(printf_fn_->getFunctionType(), printf_fn_, {fmt_str, val});
}

// ── First pass: register function stubs so calls work in any order ────────────

void CodeGen::first_pass(const Program& prog) {
    for (const auto& item : prog.items) {
        const auto* fn = std::get_if<FnDecl>(&item);
        if (!fn) continue;

        std::vector<llvm::Type*> param_tys;
        for (const auto& p : fn->params) param_tys.push_back(llvm_type(p.type_name));

        auto* fn_ty  = llvm::FunctionType::get(llvm_type(fn->return_type), param_tys, false);
        auto* llvm_fn = llvm::Function::Create(
            fn_ty, llvm::Function::ExternalLinkage, fn->name, module_.get());

        size_t i = 0;
        for (auto& arg : llvm_fn->args()) arg.setName(fn->params[i++].name);

        functions_[fn->name] = llvm_fn;
    }
}

// ── Function codegen ──────────────────────────────────────────────────────────

void CodeGen::gen_fn(const FnDecl& fn) {
    current_fn_ = functions_.at(fn.name);
    named_values_.clear();

    auto* entry = llvm::BasicBlock::Create(ctx_, "entry", current_fn_);
    builder_->SetInsertPoint(entry);

    // Spill each parameter into an alloca so it can be stored/loaded uniformly.
    for (auto& arg : current_fn_->args()) {
        auto* alloca = create_entry_alloca(std::string(arg.getName()), arg.getType());
        builder_->CreateStore(&arg, alloca);
        named_values_[std::string(arg.getName())] = alloca;
    }

    gen_block(*fn.body);

    // Safety net: add a default terminator if the last block has none.
    if (!builder_->GetInsertBlock()->getTerminator()) {
        auto* ret_ty = current_fn_->getReturnType();
        if (ret_ty->isVoidTy())
            builder_->CreateRetVoid();
        else
            builder_->CreateRet(llvm::Constant::getNullValue(ret_ty));
    }
}

// ── Top-level statements → synthetic main() ──────────────────────────────────

void CodeGen::gen_top_level_stmts(const Program& prog) {
    bool has_stmts = false;
    for (const auto& item : prog.items)
        if (std::holds_alternative<std::unique_ptr<Stmt>>(item)) { has_stmts = true; break; }
    if (!has_stmts) return;

    auto* main_ty = llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx_), false);
    current_fn_   = llvm::Function::Create(
        main_ty, llvm::Function::ExternalLinkage, "main", module_.get());
    named_values_.clear();

    auto* entry = llvm::BasicBlock::Create(ctx_, "entry", current_fn_);
    builder_->SetInsertPoint(entry);

    for (const auto& item : prog.items)
        if (const auto* sp = std::get_if<std::unique_ptr<Stmt>>(&item))
            gen_stmt(**sp);

    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_), 0));
}

// ── Entry point ───────────────────────────────────────────────────────────────

std::unique_ptr<llvm::Module> CodeGen::generate(const Program& prog) {
    module_  = std::make_unique<llvm::Module>("flux_module", ctx_);
    builder_ = std::make_unique<llvm::IRBuilder<>>(ctx_);

    declare_builtins();
    first_pass(prog);

    for (const auto& item : prog.items)
        if (const auto* fn = std::get_if<FnDecl>(&item)) gen_fn(*fn);

    gen_top_level_stmts(prog);

    return std::move(module_);
}

// ── Expression codegen ────────────────────────────────────────────────────────

llvm::Value* CodeGen::gen_expr(const Expr& e) {
    return std::visit([this](const auto& v) -> llvm::Value* {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, IntLitExpr>)
            return llvm::ConstantInt::getSigned(llvm::Type::getInt32Ty(ctx_), v.value);

        if constexpr (std::is_same_v<T, FloatLitExpr>)
            return llvm::ConstantFP::get(llvm::Type::getDoubleTy(ctx_), v.value);

        if constexpr (std::is_same_v<T, BoolLitExpr>)
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(ctx_), v.value ? 1 : 0);

        if constexpr (std::is_same_v<T, IdentExpr>) {
            auto* alloca = named_values_.at(v.name);
            return builder_->CreateLoad(alloca->getAllocatedType(), alloca, v.name);
        }

        if constexpr (std::is_same_v<T, UnaryExpr>) {
            auto* val = gen_expr(*v.operand);
            if (val->getType()->isDoubleTy()) return builder_->CreateFNeg(val, "fneg");
            return builder_->CreateNeg(val, "neg");
        }

        if constexpr (std::is_same_v<T, BinaryExpr>) {
            auto* lhs = gen_expr(*v.left);
            auto* rhs = gen_expr(*v.right);
            const bool fp = lhs->getType()->isDoubleTy();

            // Comparisons → i1
            if (v.op == "==") return fp ? builder_->CreateFCmpOEQ(lhs,rhs,"cmp") : builder_->CreateICmpEQ(lhs,rhs,"cmp");
            if (v.op == "!=") return fp ? builder_->CreateFCmpONE(lhs,rhs,"cmp") : builder_->CreateICmpNE(lhs,rhs,"cmp");
            if (v.op == "<")  return fp ? builder_->CreateFCmpOLT(lhs,rhs,"cmp") : builder_->CreateICmpSLT(lhs,rhs,"cmp");
            if (v.op == ">")  return fp ? builder_->CreateFCmpOGT(lhs,rhs,"cmp") : builder_->CreateICmpSGT(lhs,rhs,"cmp");
            if (v.op == "<=") return fp ? builder_->CreateFCmpOLE(lhs,rhs,"cmp") : builder_->CreateICmpSLE(lhs,rhs,"cmp");
            if (v.op == ">=") return fp ? builder_->CreateFCmpOGE(lhs,rhs,"cmp") : builder_->CreateICmpSGE(lhs,rhs,"cmp");

            // Arithmetic
            if (fp) {
                if (v.op == "+") return builder_->CreateFAdd(lhs, rhs, "add");
                if (v.op == "-") return builder_->CreateFSub(lhs, rhs, "sub");
                if (v.op == "*") return builder_->CreateFMul(lhs, rhs, "mul");
                if (v.op == "/") return builder_->CreateFDiv(lhs, rhs, "div");
            } else {
                if (v.op == "+") return builder_->CreateAdd(lhs, rhs, "add");
                if (v.op == "-") return builder_->CreateSub(lhs, rhs, "sub");
                if (v.op == "*") return builder_->CreateMul(lhs, rhs, "mul");
                if (v.op == "/") return builder_->CreateSDiv(lhs, rhs, "div");
                if (v.op == "%") return builder_->CreateSRem(lhs, rhs, "rem");
            }
            throw std::runtime_error("codegen: unknown binary op '" + v.op + "'");
        }

        if constexpr (std::is_same_v<T, CallExpr>) {
            auto* fn = functions_.at(v.callee);
            std::vector<llvm::Value*> args;
            for (const auto& a : v.args) args.push_back(gen_expr(*a));
            return builder_->CreateCall(fn, args, v.callee + ".ret");
        }

        throw std::runtime_error("codegen: unhandled expression variant");
    }, e.data);
}

// ── Statement codegen ─────────────────────────────────────────────────────────

void CodeGen::gen_block(const BlockStmt& b) {
    // Save outer-scope bindings; any new names declared here are removed on exit.
    auto saved = named_values_;
    for (const auto& s : b.stmts) {
        if (builder_->GetInsertBlock()->getTerminator()) break; // dead code after return
        gen_stmt(*s);
    }
    named_values_ = saved;
}

void CodeGen::gen_stmt(const Stmt& s) {
    std::visit([this](const auto& v) {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, LetStmt>) {
            auto* ty     = llvm_type(v.type_name);
            auto* alloca = create_entry_alloca(v.name, ty);
            builder_->CreateStore(gen_expr(*v.init), alloca);
            named_values_[v.name] = alloca;

        } else if constexpr (std::is_same_v<T, AssignStmt>) {
            builder_->CreateStore(gen_expr(*v.value), named_values_.at(v.name));

        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
            builder_->CreateRet(gen_expr(*v.value));

        } else if constexpr (std::is_same_v<T, PrintStmt>) {
            emit_print(gen_expr(*v.value));

        } else if constexpr (std::is_same_v<T, IfStmt>) {
            auto* cond = gen_expr(*v.condition);

            if (v.else_block) {
                auto* then_bb  = llvm::BasicBlock::Create(ctx_, "if.then",  current_fn_);
                auto* else_bb  = llvm::BasicBlock::Create(ctx_, "if.else",  current_fn_);
                auto* merge_bb = llvm::BasicBlock::Create(ctx_, "if.merge", current_fn_);
                builder_->CreateCondBr(cond, then_bb, else_bb);

                builder_->SetInsertPoint(then_bb);
                gen_block(*v.then_block);
                if (!builder_->GetInsertBlock()->getTerminator()) builder_->CreateBr(merge_bb);

                builder_->SetInsertPoint(else_bb);
                gen_block(**v.else_block);
                if (!builder_->GetInsertBlock()->getTerminator()) builder_->CreateBr(merge_bb);

                builder_->SetInsertPoint(merge_bb);
            } else {
                auto* then_bb  = llvm::BasicBlock::Create(ctx_, "if.then",  current_fn_);
                auto* merge_bb = llvm::BasicBlock::Create(ctx_, "if.merge", current_fn_);
                builder_->CreateCondBr(cond, then_bb, merge_bb);

                builder_->SetInsertPoint(then_bb);
                gen_block(*v.then_block);
                if (!builder_->GetInsertBlock()->getTerminator()) builder_->CreateBr(merge_bb);

                builder_->SetInsertPoint(merge_bb);
            }

        } else if constexpr (std::is_same_v<T, WhileStmt>) {
            auto* cond_bb = llvm::BasicBlock::Create(ctx_, "while.cond", current_fn_);
            auto* body_bb = llvm::BasicBlock::Create(ctx_, "while.body", current_fn_);
            auto* exit_bb = llvm::BasicBlock::Create(ctx_, "while.exit", current_fn_);

            builder_->CreateBr(cond_bb);

            builder_->SetInsertPoint(cond_bb);
            builder_->CreateCondBr(gen_expr(*v.condition), body_bb, exit_bb);

            builder_->SetInsertPoint(body_bb);
            gen_block(*v.body);
            if (!builder_->GetInsertBlock()->getTerminator()) builder_->CreateBr(cond_bb);

            builder_->SetInsertPoint(exit_bb);

        } else if constexpr (std::is_same_v<T, ExprStmt>) {
            gen_expr(*v.expr);
        }
    }, s.data);
}
