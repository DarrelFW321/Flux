#include "backend/codegen.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <stdexcept>

// ── FluxIR → LLVM IR lowering ────────────────────────────────────────────────
//
// One MIR instruction lowers to one LLVM operation, with three exceptions
// that expand to a small loop:
//   • ARRAY_OP   — element-wise (and broadcast) arithmetic
//   • REDUCE_SUM — left fold over an array
//   • REDUCE_DOT — pairwise multiply + sum of two arrays
//
// Those emitters create their own cond/body/exit basic blocks and leave the
// builder positioned at the exit, so subsequent MIR insts continue from
// there.

CodeGen::CodeGen() {}

// ── Type helpers ─────────────────────────────────────────────────────────────

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

llvm::Type* CodeGen::llvm_param_type(const FluxType& t) {
    if (t.is_array()) return llvm::PointerType::getUnqual(ctx_);
    return llvm_scalar_type(t.kind);
}

llvm::AllocaInst* CodeGen::create_entry_alloca(const std::string& name, llvm::Type* ty) {
    llvm::IRBuilder<> tmp(&current_fn_->getEntryBlock(),
                          current_fn_->getEntryBlock().begin());
    return tmp.CreateAlloca(ty, nullptr, name);
}

void CodeGen::emit_array_memcpy(llvm::Value* dest, llvm::Value* src, const FluxType& arr_t) {
    auto* arr_ty = llvm_storage_type(arr_t);
    uint64_t   size  = module_->getDataLayout().getTypeAllocSize(arr_ty).getFixedValue();
    llvm::Align align = module_->getDataLayout().getABITypeAlign(arr_ty);
    builder_->CreateMemCpy(dest, align, src, align, size);
}

// ── Value bookkeeping ───────────────────────────────────────────────────────

llvm::Value* CodeGen::get_value(mir::ValueId id) const {
    auto it = values_.find(id);
    if (it == values_.end())
        throw std::runtime_error("codegen: undefined ValueId %" + std::to_string(id));
    return it->second;
}

const FluxType& CodeGen::value_type(mir::ValueId id) const {
    auto it = value_types_.find(id);
    if (it == value_types_.end())
        throw std::runtime_error("codegen: untyped ValueId %" + std::to_string(id));
    return it->second;
}

void CodeGen::bind_value(mir::ValueId id, llvm::Value* v, FluxType t) {
    values_[id]      = v;
    value_types_[id] = t;
}

// ── Built-ins ───────────────────────────────────────────────────────────────

void CodeGen::declare_builtins() {
    auto* printf_ty = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(ctx_),
        {llvm::PointerType::getUnqual(ctx_)},
        /* isVarArg */ true);
    printf_fn_ = llvm::Function::Create(
        printf_ty, llvm::Function::ExternalLinkage, "printf", module_.get());
}

// ── Function declarations ───────────────────────────────────────────────────
//
// Walks the module up-front and registers every function's LLVM signature so
// calls can resolve regardless of definition order.
void CodeGen::declare_function(const mir::Function& fn) {
    FnInfo info;
    info.return_type = fn.return_type;
    for (const auto& p : fn.params) info.param_types.push_back(p.type);

    std::vector<llvm::Type*> param_tys;
    llvm::Type* llvm_ret_ty;

    if (info.returns_array()) {
        param_tys.push_back(llvm::PointerType::getUnqual(ctx_));
        llvm_ret_ty = llvm::Type::getVoidTy(ctx_);
    } else {
        llvm_ret_ty = llvm_scalar_type(info.return_type.kind);
    }
    for (const auto& pt : info.param_types) param_tys.push_back(llvm_param_type(pt));

    auto* fn_ty   = llvm::FunctionType::get(llvm_ret_ty, param_tys, /*isVarArg*/ false);
    auto* llvm_fn = llvm::Function::Create(
        fn_ty, llvm::Function::ExternalLinkage, fn.name, module_.get());

    size_t arg_offset = info.returns_array() ? 1 : 0;
    if (arg_offset) llvm_fn->getArg(0)->setName(fn.name + ".out");
    for (size_t i = 0; i < fn.params.size(); ++i)
        llvm_fn->getArg(i + arg_offset)->setName(fn.params[i].name);

    info.fn = llvm_fn;
    functions_[fn.name] = std::move(info);
}

// ── Function body lowering ──────────────────────────────────────────────────

void CodeGen::gen_function(const mir::Function& fn) {
    const auto& info     = functions_.at(fn.name);
    current_fn_          = info.fn;
    current_return_type_ = info.return_type;
    current_ret_buf_     = info.returns_array() ? current_fn_->getArg(0) : nullptr;
    values_.clear();
    value_types_.clear();
    blocks_.clear();

    // Pre-create one LLVM BasicBlock per MIR block. Branch terminators target
    // these directly; specialized emitters may interpose their own blocks but
    // always leave the builder at a block that flows into the next instruction.
    for (const auto& b : fn.blocks) {
        std::string name = b.label.empty() ? ("bb" + std::to_string(b.id)) : b.label;
        blocks_[b.id] = llvm::BasicBlock::Create(ctx_, name, current_fn_);
    }

    // Bind parameter ValueIds to LLVM arg values.
    size_t arg_offset = info.returns_array() ? 1 : 0;
    for (size_t i = 0; i < fn.params.size(); ++i) {
        auto& arg = *(current_fn_->arg_begin() + i + arg_offset);
        bind_value(fn.params[i].id, &arg, fn.params[i].type);
    }

    // Emit each block.
    for (const auto& b : fn.blocks) {
        builder_->SetInsertPoint(blocks_[b.id]);
        for (const auto& inst : b.insts) gen_inst(inst);
        gen_terminator(b.term);
    }

    // Safety net: any block left without a terminator gets a default.
    for (auto& bb : *current_fn_) {
        if (bb.getTerminator()) continue;
        builder_->SetInsertPoint(&bb);
        auto* ret_ty = current_fn_->getReturnType();
        if (ret_ty->isVoidTy()) builder_->CreateRetVoid();
        else                    builder_->CreateRet(llvm::Constant::getNullValue(ret_ty));
    }
}

// ── Terminator lowering ─────────────────────────────────────────────────────

void CodeGen::gen_terminator(const mir::Terminator& t) {
    switch (t.kind) {
        case mir::Terminator::BR:
            builder_->CreateBr(blocks_.at(t.target_t));
            return;
        case mir::Terminator::BR_COND:
            builder_->CreateCondBr(get_value(t.cond),
                                   blocks_.at(t.target_t),
                                   blocks_.at(t.target_f));
            return;
        case mir::Terminator::RET: {
            llvm::Value* rv = get_value(t.ret_val);
            if (current_return_type_.is_array()) {
                emit_array_memcpy(current_ret_buf_, rv, current_return_type_);
                builder_->CreateRetVoid();
            } else {
                builder_->CreateRet(rv);
            }
            return;
        }
        case mir::Terminator::RET_VOID:
            builder_->CreateRetVoid();
            return;
        case mir::Terminator::NONE:
            // Block has no MIR terminator (e.g. unreachable merge); the
            // post-pass in gen_function will supply a safety-net terminator.
            return;
    }
}

// ── Instruction lowering ────────────────────────────────────────────────────

void CodeGen::gen_inst(const mir::Inst& i) {
    using mir::Op;

    switch (i.op) {
        case Op::CONST_INT: {
            auto* c = llvm::ConstantInt::getSigned(llvm::Type::getInt32Ty(ctx_), i.ival);
            bind_value(i.result, c, i.result_type);
            return;
        }
        case Op::CONST_FLOAT: {
            auto* c = llvm::ConstantFP::get(llvm::Type::getDoubleTy(ctx_), i.fval);
            bind_value(i.result, c, i.result_type);
            return;
        }
        case Op::CONST_BOOL: {
            auto* c = llvm::ConstantInt::get(llvm::Type::getInt1Ty(ctx_), i.bval ? 1 : 0);
            bind_value(i.result, c, i.result_type);
            return;
        }

        case Op::ALLOCA: {
            // Always at the entry block so mem2reg can promote it later.
            auto* slot = create_entry_alloca("slot", llvm_scalar_type(i.result_type.kind));
            bind_value(i.result, slot, i.result_type);
            return;
        }
        case Op::LOAD: {
            auto* slot  = llvm::cast<llvm::AllocaInst>(get_value(i.operands[0]));
            auto* val   = builder_->CreateLoad(slot->getAllocatedType(), slot, "ld");
            bind_value(i.result, val, i.result_type);
            return;
        }
        case Op::STORE:
            builder_->CreateStore(get_value(i.operands[1]), get_value(i.operands[0]));
            return;

        case Op::NEG: {
            auto* v = get_value(i.operands[0]);
            auto* r = v->getType()->isDoubleTy() ? builder_->CreateFNeg(v, "fneg")
                                                 : builder_->CreateNeg (v, "neg");
            bind_value(i.result, r, i.result_type);
            return;
        }

        case Op::ADD: case Op::SUB: case Op::MUL: case Op::DIV: case Op::MOD:
        case Op::CMP_EQ: case Op::CMP_NE:
        case Op::CMP_LT: case Op::CMP_GT: case Op::CMP_LE: case Op::CMP_GE: {
            auto* lhs = get_value(i.operands[0]);
            auto* rhs = get_value(i.operands[1]);
            FluxType::Kind k = value_type(i.operands[0]).kind;
            auto* r = emit_scalar_binop(i.op, lhs, rhs, k);
            bind_value(i.result, r, i.result_type);
            return;
        }

        case Op::ARRAY_LIT:   bind_value(i.result, emit_array_literal(i), i.result_type); return;
        case Op::ARRAY_OP:    bind_value(i.result, emit_array_op     (i), i.result_type); return;
        case Op::FUSED_LOOP:  bind_value(i.result, emit_fused_loop   (i), i.result_type); return;
        case Op::ARRAY_COPY:  bind_value(i.result, emit_array_copy   (i), i.result_type); return;
        case Op::INDEX_LOAD:  bind_value(i.result, emit_index_load   (i), i.result_type); return;
        case Op::INDEX_STORE: emit_index_store(i); return;
        case Op::REDUCE_SUM:  bind_value(i.result, emit_reduce_sum(i), i.result_type); return;
        case Op::REDUCE_DOT:  bind_value(i.result, emit_reduce_dot(i), i.result_type); return;
        case Op::CALL:        bind_value(i.result, emit_call(i), i.result_type); return;
        case Op::PRINT:       emit_print(i); return;
    }
    throw std::runtime_error("codegen: unhandled MIR op");
}

// ── Scalar binary ops ───────────────────────────────────────────────────────

llvm::Value* CodeGen::emit_scalar_binop(mir::Op op, llvm::Value* lhs, llvm::Value* rhs,
                                        FluxType::Kind k) {
    const bool fp = (k == FluxType::Kind::Float);
    switch (op) {
        case mir::Op::ADD: return fp ? builder_->CreateFAdd(lhs, rhs, "add") : builder_->CreateAdd(lhs, rhs, "add");
        case mir::Op::SUB: return fp ? builder_->CreateFSub(lhs, rhs, "sub") : builder_->CreateSub(lhs, rhs, "sub");
        case mir::Op::MUL: return fp ? builder_->CreateFMul(lhs, rhs, "mul") : builder_->CreateMul(lhs, rhs, "mul");
        case mir::Op::DIV: return fp ? builder_->CreateFDiv(lhs, rhs, "div") : builder_->CreateSDiv(lhs, rhs, "div");
        case mir::Op::MOD: return builder_->CreateSRem(lhs, rhs, "rem");
        case mir::Op::CMP_EQ: return fp ? builder_->CreateFCmpOEQ(lhs, rhs, "cmp") : builder_->CreateICmpEQ (lhs, rhs, "cmp");
        case mir::Op::CMP_NE: return fp ? builder_->CreateFCmpONE(lhs, rhs, "cmp") : builder_->CreateICmpNE (lhs, rhs, "cmp");
        case mir::Op::CMP_LT: return fp ? builder_->CreateFCmpOLT(lhs, rhs, "cmp") : builder_->CreateICmpSLT(lhs, rhs, "cmp");
        case mir::Op::CMP_GT: return fp ? builder_->CreateFCmpOGT(lhs, rhs, "cmp") : builder_->CreateICmpSGT(lhs, rhs, "cmp");
        case mir::Op::CMP_LE: return fp ? builder_->CreateFCmpOLE(lhs, rhs, "cmp") : builder_->CreateICmpSLE(lhs, rhs, "cmp");
        case mir::Op::CMP_GE: return fp ? builder_->CreateFCmpOGE(lhs, rhs, "cmp") : builder_->CreateICmpSGE(lhs, rhs, "cmp");
        default: break;
    }
    throw std::logic_error("emit_scalar_binop: not a binop");
}

// ── Array literal: alloca + element stores ──────────────────────────────────

llvm::Value* CodeGen::emit_array_literal(const mir::Inst& i) {
    const FluxType& t = i.result_type;
    auto* arr_ty   = llvm::ArrayType::get(llvm_scalar_type(t.kind), (uint64_t)t.array_size);
    auto* storage  = create_entry_alloca("arr.lit", arr_ty);
    auto* i32      = llvm::Type::getInt32Ty(ctx_);
    auto* zero     = llvm::ConstantInt::get(i32, 0);
    for (size_t k = 0; k < i.operands.size(); ++k) {
        auto* val = get_value(i.operands[k]);
        auto* idx = llvm::ConstantInt::get(i32, (uint64_t)k);
        auto* gep = builder_->CreateInBoundsGEP(arr_ty, storage, {zero, idx}, "lit.gep");
        builder_->CreateStore(val, gep);
    }
    return storage;
}

// ── Element-wise array op (with scalar broadcast) ───────────────────────────

llvm::Value* CodeGen::emit_array_op(const mir::Inst& i) {
    const FluxType& arr_t = i.result_type;
    FluxType lhs_t = value_type(i.operands[0]);
    FluxType rhs_t = value_type(i.operands[1]);
    llvm::Value* lhs = get_value(i.operands[0]);
    llvm::Value* rhs = get_value(i.operands[1]);

    auto* elem_ty = llvm_scalar_type(arr_t.kind);
    auto* arr_ty  = llvm::ArrayType::get(elem_ty, (uint64_t)arr_t.array_size);
    auto* result  = create_entry_alloca("arr.tmp", arr_ty);

    auto* i32  = llvm::Type::getInt32Ty(ctx_);
    auto* zero = llvm::ConstantInt::get(i32, 0);
    auto* one  = llvm::ConstantInt::get(i32, 1);
    auto* size = llvm::ConstantInt::get(i32, (uint64_t)arr_t.array_size);

    auto* idx_slot = create_entry_alloca("arr.i", i32);
    builder_->CreateStore(zero, idx_slot);

    auto* cond_bb = llvm::BasicBlock::Create(ctx_, "arr.cond", current_fn_);
    auto* body_bb = llvm::BasicBlock::Create(ctx_, "arr.body", current_fn_);
    auto* exit_bb = llvm::BasicBlock::Create(ctx_, "arr.exit", current_fn_);

    builder_->CreateBr(cond_bb);
    builder_->SetInsertPoint(cond_bb);
    auto* i_val = builder_->CreateLoad(i32, idx_slot, "i");
    builder_->CreateCondBr(builder_->CreateICmpSLT(i_val, size, "i.lt.N"), body_bb, exit_bb);

    builder_->SetInsertPoint(body_bb);
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

    // sval is the original arith operator ("+", "-", "*", "/", "%").
    mir::Op scalar_op;
    if      (i.sval == "+") scalar_op = mir::Op::ADD;
    else if (i.sval == "-") scalar_op = mir::Op::SUB;
    else if (i.sval == "*") scalar_op = mir::Op::MUL;
    else if (i.sval == "/") scalar_op = mir::Op::DIV;
    else if (i.sval == "%") scalar_op = mir::Op::MOD;
    else throw std::runtime_error("emit_array_op: unknown op '" + i.sval + "'");

    auto* res    = emit_scalar_binop(scalar_op, op_lhs, op_rhs, arr_t.kind);
    auto* op_dst = builder_->CreateInBoundsGEP(arr_ty, result, {zero, i_val}, "d.gep");
    builder_->CreateStore(res, op_dst);

    auto* next = builder_->CreateAdd(i_val, one, "i.next");
    builder_->CreateStore(next, idx_slot);
    builder_->CreateBr(cond_bb);

    builder_->SetInsertPoint(exit_bb);
    return result;
}

// ── Fused map / map+reduce (one LLVM loop) ───────────────────────────────────

static mir::Op fused_op_from_string(const std::string& s) {
    if (s == "+") return mir::Op::ADD;
    if (s == "-") return mir::Op::SUB;
    if (s == "*") return mir::Op::MUL;
    if (s == "/") return mir::Op::DIV;
    if (s == "%") return mir::Op::MOD;
    throw std::runtime_error("fused_loop: unknown op '" + s + "'");
}

llvm::Value* CodeGen::emit_fused_loop(const mir::Inst& i) {
    const bool has_sum = !i.fused_ops.empty() && i.fused_ops.back() == "sum";

    FluxType arr_t = i.result_type;
    if (!has_sum) {
        arr_t = i.result_type;
    } else {
        arr_t = FluxType::array(i.result_type.kind, 0);
        for (mir::ValueId v : i.operands) {
            FluxType t = value_type(v);
            if (t.is_array()) { arr_t = t; break; }
        }
    }

    auto* elem_ty = llvm_scalar_type(arr_t.kind);
    auto* arr_ty  = llvm::ArrayType::get(elem_ty, (uint64_t)arr_t.array_size);
    auto* i32     = llvm::Type::getInt32Ty(ctx_);
    auto* zero    = llvm::ConstantInt::get(i32, 0);
    auto* one     = llvm::ConstantInt::get(i32, 1);
    auto* size    = llvm::ConstantInt::get(i32, (uint64_t)arr_t.array_size);
    const bool fp = (arr_t.kind == FluxType::Kind::Float);

    llvm::Value* result_arr = nullptr;
    if (!has_sum)
        result_arr = create_entry_alloca("fused.arr", arr_ty);

    llvm::Value* acc_slot = nullptr;
    if (has_sum) {
        acc_slot = create_entry_alloca("fused.acc", elem_ty);
        builder_->CreateStore(
            fp ? (llvm::Value*)llvm::ConstantFP::get(elem_ty, 0.0)
               : (llvm::Value*)llvm::ConstantInt::get(elem_ty, 0),
            acc_slot);
    }

    auto* idx_slot = create_entry_alloca("fused.i", i32);
    builder_->CreateStore(zero, idx_slot);

    auto* cond_bb = llvm::BasicBlock::Create(ctx_, "fused.cond", current_fn_);
    auto* body_bb = llvm::BasicBlock::Create(ctx_, "fused.body", current_fn_);
    auto* exit_bb = llvm::BasicBlock::Create(ctx_, "fused.exit", current_fn_);

    builder_->CreateBr(cond_bb);
    builder_->SetInsertPoint(cond_bb);
    auto* i_val = builder_->CreateLoad(i32, idx_slot, "i");
    builder_->CreateCondBr(builder_->CreateICmpSLT(i_val, size, "i.lt.N"), body_bb, exit_bb);

    builder_->SetInsertPoint(body_bb);

    std::vector<llvm::Value*> leaf_vals;
    leaf_vals.reserve(i.operands.size());
    for (mir::ValueId vid : i.operands) {
        FluxType t = value_type(vid);
        if (t.is_array()) {
            auto* ptr = get_value(vid);
            auto* gep = builder_->CreateInBoundsGEP(arr_ty, ptr, {zero, i_val}, "f.gep");
            leaf_vals.push_back(builder_->CreateLoad(elem_ty, gep, "f.ld"));
        } else {
            leaf_vals.push_back(get_value(vid));
        }
    }

    std::vector<llvm::Value*> stack;
    stack.reserve(leaf_vals.size() + i.fused_ops.size());
    for (llvm::Value* v : leaf_vals) stack.push_back(v);

    for (const std::string& op : i.fused_ops) {
        if (op == "sum") {
            if (stack.size() != 1)
                throw std::runtime_error("fused_loop: bad RPN stack at sum");
            auto* val  = stack.back();
            auto* prev = builder_->CreateLoad(elem_ty, acc_slot, "acc");
            auto* next = fp ? builder_->CreateFAdd(prev, val, "acc.next")
                            : builder_->CreateAdd (prev, val, "acc.next");
            builder_->CreateStore(next, acc_slot);
            stack.clear();
            break;
        }
        if (stack.size() < 2)
            throw std::runtime_error("fused_loop: bad RPN stack at '" + op + "'");
        auto* rhs = stack.back(); stack.pop_back();
        auto* lhs = stack.back(); stack.pop_back();
        stack.push_back(emit_scalar_binop(fused_op_from_string(op), lhs, rhs, arr_t.kind));
    }

    if (!has_sum) {
        if (stack.size() != 1)
            throw std::runtime_error("fused_loop: bad RPN stack at store");
        auto* dst = builder_->CreateInBoundsGEP(arr_ty, result_arr, {zero, i_val}, "f.dst");
        builder_->CreateStore(stack.back(), dst);
    }

    auto* next = builder_->CreateAdd(i_val, one, "i.next");
    builder_->CreateStore(next, idx_slot);
    builder_->CreateBr(cond_bb);

    builder_->SetInsertPoint(exit_bb);
    if (has_sum)
        return builder_->CreateLoad(elem_ty, acc_slot, "fused.sum");
    return result_arr;
}

// ── Array copy: alloca + memcpy ─────────────────────────────────────────────

llvm::Value* CodeGen::emit_array_copy(const mir::Inst& i) {
    const FluxType& t = i.result_type;
    auto* dest = create_entry_alloca("arr.copy", llvm_storage_type(t));
    emit_array_memcpy(dest, get_value(i.operands[0]), t);
    return dest;
}

// ── Reductions: sum and dot ─────────────────────────────────────────────────

llvm::Value* CodeGen::emit_reduce_sum(const mir::Inst& i) {
    FluxType arr_t = value_type(i.operands[0]);
    auto* a_ptr = get_value(i.operands[0]);

    auto* elem_ty = llvm_scalar_type(arr_t.kind);
    auto* arr_ty  = llvm::ArrayType::get(elem_ty, (uint64_t)arr_t.array_size);
    auto* i32     = llvm::Type::getInt32Ty(ctx_);
    auto* zero    = llvm::ConstantInt::get(i32, 0);
    auto* one     = llvm::ConstantInt::get(i32, 1);
    auto* size    = llvm::ConstantInt::get(i32, (uint64_t)arr_t.array_size);
    const bool fp = (arr_t.kind == FluxType::Kind::Float);

    auto* acc_slot = create_entry_alloca("sum.acc", elem_ty);
    builder_->CreateStore(
        fp ? (llvm::Value*)llvm::ConstantFP::get(elem_ty, 0.0)
           : (llvm::Value*)llvm::ConstantInt::get(elem_ty, 0),
        acc_slot);

    auto* idx_slot = create_entry_alloca("sum.i", i32);
    builder_->CreateStore(zero, idx_slot);

    auto* cond_bb = llvm::BasicBlock::Create(ctx_, "sum.cond", current_fn_);
    auto* body_bb = llvm::BasicBlock::Create(ctx_, "sum.body", current_fn_);
    auto* exit_bb = llvm::BasicBlock::Create(ctx_, "sum.exit", current_fn_);

    builder_->CreateBr(cond_bb);
    builder_->SetInsertPoint(cond_bb);
    auto* i_val = builder_->CreateLoad(i32, idx_slot, "i");
    builder_->CreateCondBr(builder_->CreateICmpSLT(i_val, size, "i.lt.N"), body_bb, exit_bb);

    builder_->SetInsertPoint(body_bb);
    auto* ap   = builder_->CreateInBoundsGEP(arr_ty, a_ptr, {zero, i_val}, "a.gep");
    auto* av   = builder_->CreateLoad(elem_ty, ap, "a");
    auto* prev = builder_->CreateLoad(elem_ty, acc_slot, "acc");
    auto* sum  = fp ? builder_->CreateFAdd(prev, av, "acc.next")
                    : builder_->CreateAdd (prev, av, "acc.next");
    builder_->CreateStore(sum, acc_slot);

    auto* next = builder_->CreateAdd(i_val, one, "i.next");
    builder_->CreateStore(next, idx_slot);
    builder_->CreateBr(cond_bb);

    builder_->SetInsertPoint(exit_bb);
    return builder_->CreateLoad(elem_ty, acc_slot, "sum.result");
}

llvm::Value* CodeGen::emit_reduce_dot(const mir::Inst& i) {
    FluxType arr_t = value_type(i.operands[0]);
    auto* a_ptr = get_value(i.operands[0]);
    auto* b_ptr = get_value(i.operands[1]);

    auto* elem_ty = llvm_scalar_type(arr_t.kind);
    auto* arr_ty  = llvm::ArrayType::get(elem_ty, (uint64_t)arr_t.array_size);
    auto* i32     = llvm::Type::getInt32Ty(ctx_);
    auto* zero    = llvm::ConstantInt::get(i32, 0);
    auto* one     = llvm::ConstantInt::get(i32, 1);
    auto* size    = llvm::ConstantInt::get(i32, (uint64_t)arr_t.array_size);
    const bool fp = (arr_t.kind == FluxType::Kind::Float);

    auto* acc_slot = create_entry_alloca("dot.acc", elem_ty);
    builder_->CreateStore(
        fp ? (llvm::Value*)llvm::ConstantFP::get(elem_ty, 0.0)
           : (llvm::Value*)llvm::ConstantInt::get(elem_ty, 0),
        acc_slot);

    auto* idx_slot = create_entry_alloca("dot.i", i32);
    builder_->CreateStore(zero, idx_slot);

    auto* cond_bb = llvm::BasicBlock::Create(ctx_, "dot.cond", current_fn_);
    auto* body_bb = llvm::BasicBlock::Create(ctx_, "dot.body", current_fn_);
    auto* exit_bb = llvm::BasicBlock::Create(ctx_, "dot.exit", current_fn_);

    builder_->CreateBr(cond_bb);
    builder_->SetInsertPoint(cond_bb);
    auto* i_val = builder_->CreateLoad(i32, idx_slot, "i");
    builder_->CreateCondBr(builder_->CreateICmpSLT(i_val, size, "i.lt.N"), body_bb, exit_bb);

    builder_->SetInsertPoint(body_bb);
    auto* ap = builder_->CreateInBoundsGEP(arr_ty, a_ptr, {zero, i_val}, "a.gep");
    auto* av = builder_->CreateLoad(elem_ty, ap, "a");
    auto* bp = builder_->CreateInBoundsGEP(arr_ty, b_ptr, {zero, i_val}, "b.gep");
    auto* bv = builder_->CreateLoad(elem_ty, bp, "b");
    auto* prod = fp ? builder_->CreateFMul(av, bv, "prod") : builder_->CreateMul(av, bv, "prod");
    auto* prev = builder_->CreateLoad(elem_ty, acc_slot, "acc");
    auto* sum  = fp ? builder_->CreateFAdd(prev, prod, "acc.next")
                    : builder_->CreateAdd (prev, prod, "acc.next");
    builder_->CreateStore(sum, acc_slot);

    auto* next = builder_->CreateAdd(i_val, one, "i.next");
    builder_->CreateStore(next, idx_slot);
    builder_->CreateBr(cond_bb);

    builder_->SetInsertPoint(exit_bb);
    return builder_->CreateLoad(elem_ty, acc_slot, "dot.result");
}

// ── Indexed access ──────────────────────────────────────────────────────────

llvm::Value* CodeGen::emit_index_load(const mir::Inst& i) {
    FluxType arr_t = value_type(i.operands[0]);
    auto* arr_ptr  = get_value(i.operands[0]);
    auto* idx_val  = get_value(i.operands[1]);
    auto* elem_ty  = llvm_scalar_type(arr_t.kind);
    auto* arr_ty   = llvm::ArrayType::get(elem_ty, (uint64_t)arr_t.array_size);
    auto* zero     = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_), 0);
    auto* gep = builder_->CreateInBoundsGEP(arr_ty, arr_ptr, {zero, idx_val}, "ix.gep");
    return builder_->CreateLoad(elem_ty, gep, "ix");
}

void CodeGen::emit_index_store(const mir::Inst& i) {
    FluxType arr_t = value_type(i.operands[0]);
    auto* arr_ptr  = get_value(i.operands[0]);
    auto* idx_val  = get_value(i.operands[1]);
    auto* val      = get_value(i.operands[2]);
    auto* elem_ty  = llvm_scalar_type(arr_t.kind);
    auto* arr_ty   = llvm::ArrayType::get(elem_ty, (uint64_t)arr_t.array_size);
    auto* zero     = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_), 0);
    auto* gep      = builder_->CreateInBoundsGEP(arr_ty, arr_ptr, {zero, idx_val}, "ix.set");
    builder_->CreateStore(val, gep);
}

// ── Call ────────────────────────────────────────────────────────────────────

llvm::Value* CodeGen::emit_call(const mir::Inst& i) {
    const auto& info = functions_.at(i.sval);

    std::vector<llvm::Value*> args;
    llvm::Value* result_buf = nullptr;
    if (info.returns_array()) {
        result_buf = create_entry_alloca(i.sval + ".ret", llvm_storage_type(info.return_type));
        args.push_back(result_buf);
    }
    for (mir::ValueId arg_id : i.operands) args.push_back(get_value(arg_id));

    if (info.returns_array()) {
        builder_->CreateCall(info.fn, args);
        return result_buf;
    }
    return builder_->CreateCall(info.fn, args, i.sval + ".ret");
}

// ── Print ───────────────────────────────────────────────────────────────────

void CodeGen::emit_print(const mir::Inst& i) {
    llvm::Value* val = get_value(i.operands[0]);
    auto* ty = val->getType();
    llvm::Value* fmt_str;
    if (ty->isIntegerTy(32)) {
        fmt_str = builder_->CreateGlobalString("%d\n", ".fmt.int");
    } else if (ty->isDoubleTy()) {
        fmt_str = builder_->CreateGlobalString("%g\n", ".fmt.float");
    } else if (ty->isIntegerTy(1)) {
        val     = builder_->CreateZExt(val, llvm::Type::getInt32Ty(ctx_), "b2i");
        fmt_str = builder_->CreateGlobalString("%d\n", ".fmt.bool");
    } else {
        throw std::runtime_error("emit_print: unsupported value type");
    }
    builder_->CreateCall(printf_fn_->getFunctionType(), printf_fn_, {fmt_str, val});
}

// ── Entry point ─────────────────────────────────────────────────────────────

std::unique_ptr<llvm::Module> CodeGen::generate(const mir::Module& m) {
    module_  = std::make_unique<llvm::Module>("flux_module", ctx_);
    builder_ = std::make_unique<llvm::IRBuilder<>>(ctx_);

    declare_builtins();
    for (const auto& fn : m.functions) declare_function(fn);
    for (const auto& fn : m.functions) gen_function(fn);

    return std::move(module_);
}
