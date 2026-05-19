#include "backend/codegen.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <stdexcept>

CodeGen::CodeGen() {}

// ── Type helpers ──────────────────────────────────────────────────────────────

llvm::Type* CodeGen::llvm_scalar_type(FluxType::Kind k) {
    switch (k) {
        case FluxType::Kind::Int:   return llvm::Type::getInt32Ty(ctx_);
        case FluxType::Kind::Float: return llvm::Type::getDoubleTy(ctx_);
        case FluxType::Kind::Bool:  return llvm::Type::getInt1Ty(ctx_);
        case FluxType::Kind::Void:  return llvm::Type::getVoidTy(ctx_);
    }
    throw std::logic_error("llvm_scalar_type: unknown kind");
}

llvm::Type* CodeGen::llvm_storage_type(const FluxType& t) {
    auto* elem = llvm_scalar_type(t.kind);
    if (t.is_array())
        return llvm::ArrayType::get(elem, (uint64_t)t.array_size);
    return elem;
}

// In LLVM, arrays are passed and returned by pointer.
llvm::Type* CodeGen::llvm_param_type(const FluxType& t) {
    if (t.is_array()) return llvm::PointerType::getUnqual(ctx_);
    return llvm_scalar_type(t.kind);
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

        FnInfo info;
        info.return_type = FluxType::parse(fn->return_type);
        for (const auto& p : fn->params)
            info.param_types.push_back(FluxType::parse(p.type_name));

        // Build the LLVM signature.
        //   Array-returning fn:  void f(ptr %ret, ...real_params)
        //   Scalar-returning fn: T    f(...real_params)
        std::vector<llvm::Type*> param_tys;
        llvm::Type* llvm_ret_ty;
        if (info.return_type.is_array()) {
            param_tys.push_back(llvm::PointerType::getUnqual(ctx_));
            llvm_ret_ty = llvm::Type::getVoidTy(ctx_);
        } else {
            llvm_ret_ty = llvm_scalar_type(info.return_type.kind);
        }
        for (const auto& pt : info.param_types)
            param_tys.push_back(llvm_param_type(pt));

        auto* fn_ty   = llvm::FunctionType::get(llvm_ret_ty, param_tys, false);
        auto* llvm_fn = llvm::Function::Create(
            fn_ty, llvm::Function::ExternalLinkage, fn->name, module_.get());

        size_t arg_offset = info.return_type.is_array() ? 1 : 0;
        if (arg_offset) llvm_fn->getArg(0)->setName(fn->name + ".out");
        for (size_t i = 0; i < fn->params.size(); ++i)
            llvm_fn->getArg(i + arg_offset)->setName(fn->params[i].name);

        info.fn = llvm_fn;
        functions_[fn->name] = std::move(info);
    }
}

// ── Function codegen ──────────────────────────────────────────────────────────

void CodeGen::gen_fn(const FnDecl& fn) {
    const auto& info = functions_.at(fn.name);
    current_fn_          = info.fn;
    current_return_type_ = info.return_type;
    named_values_.clear();

    auto* entry = llvm::BasicBlock::Create(ctx_, "entry", current_fn_);
    builder_->SetInsertPoint(entry);

    // For array-returning functions, the LLVM signature has a hidden first
    // pointer parameter (the return slot) — skip it when binding user params.
    const size_t arg_offset = info.returns_array() ? 1 : 0;

    for (size_t i = 0; i < fn.params.size(); ++i) {
        auto& arg = *(current_fn_->arg_begin() + i + arg_offset);
        FluxType pt = info.param_types[i];
        std::string name = fn.params[i].name;

        //   Scalar: alloca + store (so subsequent loads/stores look uniform).
        //   Array : use the incoming pointer directly (no copy — v2 pass-by-reference).
        if (pt.is_array()) {
            named_values_[name] = { &arg, pt };
        } else {
            auto* alloca = create_entry_alloca(name, arg.getType());
            builder_->CreateStore(&arg, alloca);
            named_values_[name] = { alloca, pt };
        }
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
    current_return_type_ = FluxType::scalar(FluxType::Kind::Int);
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

// ── Static type re-derivation (mirrors typechecker but cheaper here) ─────────

FluxType CodeGen::expr_type(const Expr& e) const {
    return std::visit([this, &e](const auto& v) -> FluxType {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, IntLitExpr>)   return FluxType::scalar(FluxType::Kind::Int);
        if constexpr (std::is_same_v<T, FloatLitExpr>) return FluxType::scalar(FluxType::Kind::Float);
        if constexpr (std::is_same_v<T, BoolLitExpr>)  return FluxType::scalar(FluxType::Kind::Bool);
        if constexpr (std::is_same_v<T, IdentExpr>)    return named_values_.at(v.name).type;
        if constexpr (std::is_same_v<T, ArrayLitExpr>) {
            FluxType first = expr_type(*v.elements[0]);
            return FluxType::array(first.kind, (int)v.elements.size());
        }
        if constexpr (std::is_same_v<T, IndexExpr>)    return expr_type(*v.array).elem();
        if constexpr (std::is_same_v<T, UnaryExpr>)    return expr_type(*v.operand);
        if constexpr (std::is_same_v<T, BinaryExpr>) {
            FluxType lhs = expr_type(*v.left);
            const bool is_cmp = (v.op=="=="||v.op=="!="||v.op=="<"||v.op==">"||v.op=="<="||v.op==">=");
            if (is_cmp && !lhs.is_array()) return FluxType::scalar(FluxType::Kind::Bool);
            return lhs;
        }
        if constexpr (std::is_same_v<T, CallExpr>) {
            if (v.callee == "sum") return expr_type(*v.args[0]).elem();
            if (v.callee == "dot") return expr_type(*v.args[0]).elem();
            return functions_.at(v.callee).return_type;
        }
        throw std::runtime_error("expr_type: unhandled expression");
    }, e.data);
}

// ── Scalar binary ops ─────────────────────────────────────────────────────────

llvm::Value* CodeGen::gen_scalar_binop(const std::string& op,
                                       llvm::Value* lhs, llvm::Value* rhs,
                                       FluxType::Kind kind) {
    const bool fp = (kind == FluxType::Kind::Float);

    // Comparisons → i1
    if (op == "==") return fp ? builder_->CreateFCmpOEQ(lhs,rhs,"cmp") : builder_->CreateICmpEQ(lhs,rhs,"cmp");
    if (op == "!=") return fp ? builder_->CreateFCmpONE(lhs,rhs,"cmp") : builder_->CreateICmpNE(lhs,rhs,"cmp");
    if (op == "<")  return fp ? builder_->CreateFCmpOLT(lhs,rhs,"cmp") : builder_->CreateICmpSLT(lhs,rhs,"cmp");
    if (op == ">")  return fp ? builder_->CreateFCmpOGT(lhs,rhs,"cmp") : builder_->CreateICmpSGT(lhs,rhs,"cmp");
    if (op == "<=") return fp ? builder_->CreateFCmpOLE(lhs,rhs,"cmp") : builder_->CreateICmpSLE(lhs,rhs,"cmp");
    if (op == ">=") return fp ? builder_->CreateFCmpOGE(lhs,rhs,"cmp") : builder_->CreateICmpSGE(lhs,rhs,"cmp");

    if (fp) {
        if (op == "+") return builder_->CreateFAdd(lhs, rhs, "add");
        if (op == "-") return builder_->CreateFSub(lhs, rhs, "sub");
        if (op == "*") return builder_->CreateFMul(lhs, rhs, "mul");
        if (op == "/") return builder_->CreateFDiv(lhs, rhs, "div");
    } else {
        if (op == "+") return builder_->CreateAdd(lhs, rhs, "add");
        if (op == "-") return builder_->CreateSub(lhs, rhs, "sub");
        if (op == "*") return builder_->CreateMul(lhs, rhs, "mul");
        if (op == "/") return builder_->CreateSDiv(lhs, rhs, "div");
        if (op == "%") return builder_->CreateSRem(lhs, rhs, "rem");
    }
    throw std::runtime_error("gen_scalar_binop: unknown op '" + op + "'");
}

// ── Array literal: alloca + per-element store, returns ptr ────────────────────

llvm::Value* CodeGen::gen_array_literal(const ArrayLitExpr& a, const FluxType& t) {
    auto* arr_ty  = llvm::ArrayType::get(llvm_scalar_type(t.kind), (uint64_t)t.array_size);
    auto* storage = create_entry_alloca("arr.lit", arr_ty);
    auto* i32     = llvm::Type::getInt32Ty(ctx_);
    auto* zero    = llvm::ConstantInt::get(i32, 0);

    for (size_t i = 0; i < a.elements.size(); ++i) {
        auto* val = gen_expr(*a.elements[i]);
        auto* idx = llvm::ConstantInt::get(i32, (uint64_t)i);
        auto* gep = builder_->CreateInBoundsGEP(arr_ty, storage, {zero, idx}, "lit.gep");
        builder_->CreateStore(val, gep);
    }
    return storage;
}

// ── Element-wise array op (with scalar broadcasting) ─────────────────────────

llvm::Value* CodeGen::gen_array_binop(const std::string& op,
                                      llvm::Value* lhs, FluxType lhs_t,
                                      llvm::Value* rhs, FluxType rhs_t,
                                      const FluxType& arr_t) {
    auto* elem_ty = llvm_scalar_type(arr_t.kind);
    auto* arr_ty  = llvm::ArrayType::get(elem_ty, (uint64_t)arr_t.array_size);
    auto* result  = create_entry_alloca("arr.tmp", arr_ty);

    auto* i32     = llvm::Type::getInt32Ty(ctx_);
    auto* zero    = llvm::ConstantInt::get(i32, 0);
    auto* one     = llvm::ConstantInt::get(i32, 1);
    auto* size    = llvm::ConstantInt::get(i32, (uint64_t)arr_t.array_size);

    auto* idx_slot = create_entry_alloca("arr.i", i32);
    builder_->CreateStore(zero, idx_slot);

    auto* cond_bb = llvm::BasicBlock::Create(ctx_, "arr.cond", current_fn_);
    auto* body_bb = llvm::BasicBlock::Create(ctx_, "arr.body", current_fn_);
    auto* exit_bb = llvm::BasicBlock::Create(ctx_, "arr.exit", current_fn_);

    builder_->CreateBr(cond_bb);
    builder_->SetInsertPoint(cond_bb);
    auto* i_val = builder_->CreateLoad(i32, idx_slot, "i");
    auto* cmp   = builder_->CreateICmpSLT(i_val, size, "i.lt.N");
    builder_->CreateCondBr(cmp, body_bb, exit_bb);

    builder_->SetInsertPoint(body_bb);
    // Load array elements, or use the scalar SSA value directly (broadcast).
    llvm::Value* op_lhs;
    if (lhs_t.is_array()) {
        auto* lp = builder_->CreateInBoundsGEP(arr_ty, lhs, {zero, i_val}, "l.gep");
        op_lhs   = builder_->CreateLoad(elem_ty, lp, "l");
    } else {
        op_lhs = lhs;
    }
    llvm::Value* op_rhs;
    if (rhs_t.is_array()) {
        auto* rp = builder_->CreateInBoundsGEP(arr_ty, rhs, {zero, i_val}, "r.gep");
        op_rhs   = builder_->CreateLoad(elem_ty, rp, "r");
    } else {
        op_rhs = rhs;
    }
    auto* res    = gen_scalar_binop(op, op_lhs, op_rhs, arr_t.kind);
    auto* op_dst = builder_->CreateInBoundsGEP(arr_ty, result, {zero, i_val}, "d.gep");
    builder_->CreateStore(res, op_dst);

    auto* next = builder_->CreateAdd(i_val, one, "i.next");
    builder_->CreateStore(next, idx_slot);
    builder_->CreateBr(cond_bb);

    builder_->SetInsertPoint(exit_bb);
    return result;
}

// memcpy(dest, src, sizeof([N x T])) — used for array returns and let-copies.
void CodeGen::emit_array_memcpy(llvm::Value* dest, llvm::Value* src, const FluxType& arr_t) {
    auto* arr_ty = llvm_storage_type(arr_t);
    uint64_t size = module_->getDataLayout().getTypeAllocSize(arr_ty).getFixedValue();
    llvm::Align align = module_->getDataLayout().getABITypeAlign(arr_ty);
    builder_->CreateMemCpy(dest, align, src, align, size);
}

// ── Index read: GEP + load ────────────────────────────────────────────────────

llvm::Value* CodeGen::gen_index(const IndexExpr& ix) {
    FluxType arr_t = expr_type(*ix.array);
    auto* arr_ptr  = gen_expr(*ix.array);
    auto* idx_val  = gen_expr(*ix.index);

    auto* elem_ty  = llvm_scalar_type(arr_t.kind);
    auto* arr_ty   = llvm::ArrayType::get(elem_ty, (uint64_t)arr_t.array_size);
    auto* zero     = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_), 0);

    auto* gep = builder_->CreateInBoundsGEP(arr_ty, arr_ptr, {zero, idx_val}, "ix.gep");
    return builder_->CreateLoad(elem_ty, gep, "ix");
}

// ── Built-in calls: sum(a), dot(a, b) ────────────────────────────────────────

llvm::Value* CodeGen::gen_builtin_call(const CallExpr& v) {
    if (v.callee != "sum" && v.callee != "dot") return nullptr;

    FluxType arr_t  = expr_type(*v.args[0]);
    auto*    a_ptr  = gen_expr(*v.args[0]);
    auto*    b_ptr  = (v.callee == "dot") ? gen_expr(*v.args[1]) : nullptr;

    auto* elem_ty = llvm_scalar_type(arr_t.kind);
    auto* arr_ty  = llvm::ArrayType::get(elem_ty, (uint64_t)arr_t.array_size);
    auto* i32     = llvm::Type::getInt32Ty(ctx_);
    auto* zero    = llvm::ConstantInt::get(i32, 0);
    auto* one     = llvm::ConstantInt::get(i32, 1);
    auto* size    = llvm::ConstantInt::get(i32, (uint64_t)arr_t.array_size);

    const bool fp = (arr_t.kind == FluxType::Kind::Float);

    auto* acc_slot = create_entry_alloca(v.callee + ".acc", elem_ty);
    builder_->CreateStore(
        fp ? (llvm::Value*)llvm::ConstantFP::get(elem_ty, 0.0)
           : (llvm::Value*)llvm::ConstantInt::get(elem_ty, 0),
        acc_slot);

    auto* idx_slot = create_entry_alloca(v.callee + ".i", i32);
    builder_->CreateStore(zero, idx_slot);

    auto* cond_bb = llvm::BasicBlock::Create(ctx_, v.callee + ".cond", current_fn_);
    auto* body_bb = llvm::BasicBlock::Create(ctx_, v.callee + ".body", current_fn_);
    auto* exit_bb = llvm::BasicBlock::Create(ctx_, v.callee + ".exit", current_fn_);

    builder_->CreateBr(cond_bb);
    builder_->SetInsertPoint(cond_bb);
    auto* i_val = builder_->CreateLoad(i32, idx_slot, "i");
    auto* cmp   = builder_->CreateICmpSLT(i_val, size, "i.lt.N");
    builder_->CreateCondBr(cmp, body_bb, exit_bb);

    builder_->SetInsertPoint(body_bb);
    auto* ap = builder_->CreateInBoundsGEP(arr_ty, a_ptr, {zero, i_val}, "a.gep");
    auto* av = builder_->CreateLoad(elem_ty, ap, "a");
    llvm::Value* term = av;
    if (v.callee == "dot") {
        auto* bp = builder_->CreateInBoundsGEP(arr_ty, b_ptr, {zero, i_val}, "b.gep");
        auto* bv = builder_->CreateLoad(elem_ty, bp, "b");
        term = fp ? builder_->CreateFMul(av, bv, "prod")
                  : builder_->CreateMul (av, bv, "prod");
    }
    auto* prev = builder_->CreateLoad(elem_ty, acc_slot, "acc");
    auto* sum  = fp ? builder_->CreateFAdd(prev, term, "acc.next")
                    : builder_->CreateAdd (prev, term, "acc.next");
    builder_->CreateStore(sum, acc_slot);

    auto* next = builder_->CreateAdd(i_val, one, "i.next");
    builder_->CreateStore(next, idx_slot);
    builder_->CreateBr(cond_bb);

    builder_->SetInsertPoint(exit_bb);
    return builder_->CreateLoad(elem_ty, acc_slot, v.callee + ".result");
}

// ── Expression codegen ────────────────────────────────────────────────────────

llvm::Value* CodeGen::gen_expr(const Expr& e) {
    return std::visit([this, &e](const auto& v) -> llvm::Value* {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, IntLitExpr>)
            return llvm::ConstantInt::getSigned(llvm::Type::getInt32Ty(ctx_), v.value);

        if constexpr (std::is_same_v<T, FloatLitExpr>)
            return llvm::ConstantFP::get(llvm::Type::getDoubleTy(ctx_), v.value);

        if constexpr (std::is_same_v<T, BoolLitExpr>)
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(ctx_), v.value ? 1 : 0);

        if constexpr (std::is_same_v<T, IdentExpr>) {
            auto& b = named_values_.at(v.name);
            if (b.type.is_array()) return b.ptr;            // arrays: hand back the pointer
            auto* alloca = llvm::cast<llvm::AllocaInst>(b.ptr);
            return builder_->CreateLoad(alloca->getAllocatedType(), alloca, v.name);
        }

        if constexpr (std::is_same_v<T, ArrayLitExpr>) {
            FluxType t = expr_type(e);
            return gen_array_literal(v, t);
        }

        if constexpr (std::is_same_v<T, IndexExpr>) {
            return gen_index(v);
        }

        if constexpr (std::is_same_v<T, UnaryExpr>) {
            auto* val = gen_expr(*v.operand);
            if (val->getType()->isDoubleTy()) return builder_->CreateFNeg(val, "fneg");
            return builder_->CreateNeg(val, "neg");
        }

        if constexpr (std::is_same_v<T, BinaryExpr>) {
            FluxType lhs_t = expr_type(*v.left);
            FluxType rhs_t = expr_type(*v.right);
            auto* lhs = gen_expr(*v.left);
            auto* rhs = gen_expr(*v.right);
            if (lhs_t.is_array() || rhs_t.is_array()) {
                FluxType arr_t = lhs_t.is_array() ? lhs_t : rhs_t;
                return gen_array_binop(v.op, lhs, lhs_t, rhs, rhs_t, arr_t);
            }
            return gen_scalar_binop(v.op, lhs, rhs, lhs_t.kind);
        }

        if constexpr (std::is_same_v<T, CallExpr>) {
            if (auto* built = gen_builtin_call(v)) return built;
            const auto& info = functions_.at(v.callee);

            std::vector<llvm::Value*> args;
            llvm::Value* result_buf = nullptr;
            if (info.returns_array()) {
                // Allocate a buffer in the caller and pass its pointer as the
                // hidden first argument; the call itself returns void.
                result_buf = create_entry_alloca(v.callee + ".ret",
                                                 llvm_storage_type(info.return_type));
                args.push_back(result_buf);
            }
            for (const auto& a : v.args) args.push_back(gen_expr(*a));

            if (info.returns_array()) {
                builder_->CreateCall(info.fn, args);
                return result_buf;
            }
            return builder_->CreateCall(info.fn, args, v.callee + ".ret");
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
            FluxType t = FluxType::parse(v.type_name);
            if (t.is_array()) {
                // For initializers that already produce a fresh buffer (array
                // literal, element-wise op, array-returning call), we can just
                // bind the name to that pointer — nothing else aliases it.
                // For an IdentExpr referring to an existing array, we must
                // memcpy so that mutations to the new variable don't leak.
                const bool needs_copy = std::holds_alternative<IdentExpr>(v.init->data);
                if (needs_copy) {
                    auto* dest = create_entry_alloca(v.name, llvm_storage_type(t));
                    auto* src  = gen_expr(*v.init);
                    emit_array_memcpy(dest, src, t);
                    named_values_[v.name] = { dest, t };
                } else {
                    auto* ptr = gen_expr(*v.init);
                    named_values_[v.name] = { ptr, t };
                }
            } else {
                auto* alloca = create_entry_alloca(v.name, llvm_scalar_type(t.kind));
                builder_->CreateStore(gen_expr(*v.init), alloca);
                named_values_[v.name] = { alloca, t };
            }

        } else if constexpr (std::is_same_v<T, AssignStmt>) {
            // Scalar only (typechecker rejects whole-array assignment).
            auto& b = named_values_.at(v.name);
            builder_->CreateStore(gen_expr(*v.value), b.ptr);

        } else if constexpr (std::is_same_v<T, IndexAssignStmt>) {
            FluxType arr_t = expr_type(*v.array);
            auto* arr_ptr  = gen_expr(*v.array);
            auto* idx_val  = gen_expr(*v.index);
            auto* val      = gen_expr(*v.value);
            auto* elem_ty  = llvm_scalar_type(arr_t.kind);
            auto* arr_ty   = llvm::ArrayType::get(elem_ty, (uint64_t)arr_t.array_size);
            auto* zero     = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_), 0);
            auto* gep      = builder_->CreateInBoundsGEP(arr_ty, arr_ptr, {zero, idx_val}, "ix.set");
            builder_->CreateStore(val, gep);

        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
            if (current_return_type_.is_array()) {
                // Array return: memcpy into the hidden out-buffer (arg 0) and
                // emit `ret void`. The caller knows the slot it passed in.
                auto* src  = gen_expr(*v.value);
                auto* dest = current_fn_->getArg(0);
                emit_array_memcpy(dest, src, current_return_type_);
                builder_->CreateRetVoid();
            } else {
                builder_->CreateRet(gen_expr(*v.value));
            }

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
