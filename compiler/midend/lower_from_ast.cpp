#include "midend/lower_from_ast.hpp"
#include "frontend/ast.hpp"
#include "frontend/types.hpp"
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// ── AST → FluxIR lowering ────────────────────────────────────────────────────
//
// Walks a type-checked AST and emits FluxIR. Conventions:
//
//   • Scalar locals (and scalar params) live in alloca slots so they can be
//     mutated by simple LOAD/STORE. This mirrors what LLVM produces before
//     mem2reg — a later pass can promote them to SSA.
//
//   • Arrays are passed and bound by pointer (the ValueId itself "is" the
//     array). `let b = a;` for arrays emits ARRAY_COPY to keep value
//     semantics; element writes through one binding never alias another.
//
//   • Top-level statements are folded into a synthetic `main` function
//     returning `int` (defaults to `return 0;`).

namespace mir {

namespace {

class Lowerer {
public:
    Module lower_module(const Program& prog);

private:
    // Binding for a variable name in scope.
    //   For scalars, `value` is a SLOT (alloca pointer) — emit LOAD/STORE.
    //   For arrays,  `value` is the ARRAY pointer itself.
    struct Binding {
        ValueId  value;
        FluxType type;
    };

    Function*                                                fn_            = nullptr;
    BlockId                                                  current_block_ = 0;
    std::vector<std::unordered_map<std::string, Binding>>    scopes_;
    std::unordered_map<std::string, FluxType>                fn_return_types_;

    // Scopes
    void                push_scope() { scopes_.emplace_back(); }
    void                pop_scope()  { scopes_.pop_back();    }
    void                bind(const std::string& name, ValueId v, FluxType t);
    const Binding&      look(const std::string& name) const;

    // Builder helpers
    BlockId             new_block(const std::string& label) { return fn_->new_block(label); }
    void                switch_to(BlockId b) { current_block_ = b; }
    Block&              cur() { return fn_->blocks[current_block_]; }
    bool                terminated() const  { return fn_->blocks[current_block_].term.kind != Terminator::NONE; }

    ValueId             emit_value(Inst inst);
    void                emit_void (Inst inst);
    void                br        (BlockId target);
    void                br_cond   (ValueId cond, BlockId t, BlockId f);
    void                ret       (ValueId v);
    void                ret_void  ();

    // Lowering
    void                lower_fn   (const FnDecl& fn);
    void                lower_block(const BlockStmt& b);
    void                lower_stmt (const Stmt& s);
    ValueId             lower_expr (const Expr& e);

    // Re-derives the FluxType of an expression. The type checker has already
    // verified everything, so this just mirrors that logic.
    FluxType            expr_type  (const Expr& e) const;

    static Op           binop_to_op    (const std::string& op);
    static bool         is_compare_op  (const std::string& op);
};

// ── Scope helpers ───────────────────────────────────────────────────────────

void Lowerer::bind(const std::string& name, ValueId v, FluxType t) {
    scopes_.back()[name] = {v, t};
}

const Lowerer::Binding& Lowerer::look(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return found->second;
    }
    throw std::logic_error("mir lowering: undefined variable '" + name + "'");
}

// ── Builder primitives ──────────────────────────────────────────────────────

ValueId Lowerer::emit_value(Inst inst) {
    if (inst.result == NO_VALUE) inst.result = fn_->new_value_id();
    ValueId r = inst.result;
    cur().insts.push_back(std::move(inst));
    return r;
}

void Lowerer::emit_void(Inst inst) {
    inst.result = NO_VALUE;
    cur().insts.push_back(std::move(inst));
}

void Lowerer::br      (BlockId t)                       { cur().term = {Terminator::BR,       t, 0, 0, 0}; }
void Lowerer::br_cond (ValueId c, BlockId t, BlockId f) { cur().term = {Terminator::BR_COND,  t, f, c, 0}; }
void Lowerer::ret     (ValueId v)                       { cur().term = {Terminator::RET,      0, 0, 0, v}; }
void Lowerer::ret_void()                                { cur().term = {Terminator::RET_VOID, 0, 0, 0, 0}; }

// ── Op classification ──────────────────────────────────────────────────────

Op Lowerer::binop_to_op(const std::string& op) {
    if (op == "+")  return Op::ADD;
    if (op == "-")  return Op::SUB;
    if (op == "*")  return Op::MUL;
    if (op == "/")  return Op::DIV;
    if (op == "%")  return Op::MOD;
    if (op == "==") return Op::CMP_EQ;
    if (op == "!=") return Op::CMP_NE;
    if (op == "<")  return Op::CMP_LT;
    if (op == ">")  return Op::CMP_GT;
    if (op == "<=") return Op::CMP_LE;
    if (op == ">=") return Op::CMP_GE;
    throw std::logic_error("mir lowering: unknown binary op '" + op + "'");
}

bool Lowerer::is_compare_op(const std::string& op) {
    return op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=";
}

// ── Module / function lowering ──────────────────────────────────────────────

Module Lowerer::lower_module(const Program& prog) {
    Module m;

    // First sweep: record function signatures so calls resolve regardless of order.
    for (const auto& item : prog.items) {
        if (const auto* decl = std::get_if<FnDecl>(&item)) {
            fn_return_types_[decl->name] = FluxType::parse(decl->return_type);
        }
    }

    // Lower each user-defined function.
    for (const auto& item : prog.items) {
        if (const auto* decl = std::get_if<FnDecl>(&item)) {
            m.functions.emplace_back();
            fn_ = &m.functions.back();
            fn_->name        = decl->name;
            fn_->return_type = FluxType::parse(decl->return_type);
            lower_fn(*decl);
        }
    }

    // Collect any top-level statements into a synthetic `main`.
    bool has_top_stmts = false;
    for (const auto& item : prog.items) {
        if (std::holds_alternative<std::unique_ptr<Stmt>>(item)) {
            has_top_stmts = true;
            break;
        }
    }
    if (!has_top_stmts) return m;

    m.functions.emplace_back();
    fn_                       = &m.functions.back();
    fn_->name                 = "main";
    fn_->return_type          = FluxType::scalar(FluxType::Kind::Int);
    fn_->is_synthetic_main    = true;
    fn_return_types_["main"]  = fn_->return_type;

    scopes_.clear();
    push_scope();

    BlockId entry = fn_->new_block("entry");
    switch_to(entry);

    for (const auto& item : prog.items) {
        if (terminated()) break;
        if (const auto* sp = std::get_if<std::unique_ptr<Stmt>>(&item)) {
            lower_stmt(**sp);
        }
    }

    if (!terminated()) {
        Inst zero;
        zero.op          = Op::CONST_INT;
        zero.ival        = 0;
        zero.result_type = FluxType::scalar(FluxType::Kind::Int);
        zero.result_role = ValueRole::SCALAR;
        ValueId z = emit_value(std::move(zero));
        ret(z);
    }
    pop_scope();

    return m;
}

void Lowerer::lower_fn(const FnDecl& decl) {
    scopes_.clear();
    push_scope();

    // Allocate value IDs for parameters up-front so the entry block can
    // refer to them when spilling.
    for (const auto& p : decl.params) {
        FluxType pt = FluxType::parse(p.type_name);
        Param mp;
        mp.id   = fn_->new_value_id();
        mp.name = p.name;
        mp.type = pt;
        mp.role = pt.is_array() ? ValueRole::ARRAY : ValueRole::SCALAR;
        fn_->params.push_back(mp);
    }

    BlockId entry = fn_->new_block("entry");
    switch_to(entry);

    // Spill scalar params into slots; bind arrays by value.
    for (const auto& mp : fn_->params) {
        if (mp.role == ValueRole::ARRAY) {
            bind(mp.name, mp.id, mp.type);
        } else {
            Inst a;
            a.op          = Op::ALLOCA;
            a.result_type = mp.type;
            a.result_role = ValueRole::SLOT;
            ValueId slot = emit_value(std::move(a));

            Inst s;
            s.op       = Op::STORE;
            s.operands = {slot, mp.id};
            emit_void(std::move(s));

            bind(mp.name, slot, mp.type);
        }
    }

    lower_block(*decl.body);

    // Implicit return for void functions only — non-void without an
    // explicit return is a type-check error.
    if (!terminated() && fn_->return_type.kind == FluxType::Kind::Void) {
        ret_void();
    }

    pop_scope();
}

void Lowerer::lower_block(const BlockStmt& b) {
    push_scope();
    for (const auto& s : b.stmts) {
        if (terminated()) break;
        lower_stmt(*s);
    }
    pop_scope();
}

// ── Statement lowering ──────────────────────────────────────────────────────

void Lowerer::lower_stmt(const Stmt& stmt) {
    std::visit([this](const auto& v) {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, LetStmt>) {
            FluxType t    = FluxType::parse(v.type_name);
            ValueId  init = lower_expr(*v.init);

            if (t.is_array()) {
                // `let b = a;` where `a` is another array must produce a
                // distinct copy so writes through `b` don't alias `a`.
                // Other RHS shapes (literal, op result, call) already
                // produce fresh storage.
                if (std::holds_alternative<IdentExpr>(v.init->data)) {
                    Inst c;
                    c.op          = Op::ARRAY_COPY;
                    c.operands    = {init};
                    c.result_type = t;
                    c.result_role = ValueRole::ARRAY;
                    ValueId fresh = emit_value(std::move(c));
                    bind(v.name, fresh, t);
                } else {
                    bind(v.name, init, t);
                }
            } else {
                Inst a;
                a.op          = Op::ALLOCA;
                a.result_type = t;
                a.result_role = ValueRole::SLOT;
                ValueId slot = emit_value(std::move(a));

                Inst s;
                s.op       = Op::STORE;
                s.operands = {slot, init};
                emit_void(std::move(s));

                bind(v.name, slot, t);
            }
            return;
        }

        if constexpr (std::is_same_v<T, AssignStmt>) {
            const auto& b   = look(v.name);
            ValueId      val = lower_expr(*v.value);
            // Type checker only permits assigning to scalar slots; arrays
            // would have to use indexed assignment.
            Inst s;
            s.op       = Op::STORE;
            s.operands = {b.value, val};
            emit_void(std::move(s));
            return;
        }

        if constexpr (std::is_same_v<T, IndexAssignStmt>) {
            ValueId arr = lower_expr(*v.array);
            ValueId idx = lower_expr(*v.index);
            ValueId val = lower_expr(*v.value);
            Inst i;
            i.op       = Op::INDEX_STORE;
            i.operands = {arr, idx, val};
            emit_void(std::move(i));
            return;
        }

        if constexpr (std::is_same_v<T, ReturnStmt>) {
            ValueId rv = lower_expr(*v.value);
            ret(rv);
            return;
        }

        if constexpr (std::is_same_v<T, PrintStmt>) {
            ValueId vid = lower_expr(*v.value);
            Inst i;
            i.op       = Op::PRINT;
            i.operands = {vid};
            emit_void(std::move(i));
            return;
        }

        if constexpr (std::is_same_v<T, IfStmt>) {
            ValueId cond     = lower_expr(*v.condition);
            BlockId then_bb  = new_block("if.then");
            BlockId else_bb  = v.else_block ? new_block("if.else") : 0;
            BlockId merge_bb = new_block("if.merge");
            if (!v.else_block) else_bb = merge_bb;
            br_cond(cond, then_bb, else_bb);

            switch_to(then_bb);
            lower_block(*v.then_block);
            if (!terminated()) br(merge_bb);

            if (v.else_block) {
                switch_to(else_bb);
                lower_block(**v.else_block);
                if (!terminated()) br(merge_bb);
            }
            switch_to(merge_bb);
            return;
        }

        if constexpr (std::is_same_v<T, WhileStmt>) {
            BlockId cond_bb = new_block("while.cond");
            BlockId body_bb = new_block("while.body");
            BlockId exit_bb = new_block("while.exit");
            br(cond_bb);

            switch_to(cond_bb);
            ValueId c = lower_expr(*v.condition);
            br_cond(c, body_bb, exit_bb);

            switch_to(body_bb);
            lower_block(*v.body);
            if (!terminated()) br(cond_bb);

            switch_to(exit_bb);
            return;
        }

        if constexpr (std::is_same_v<T, ExprStmt>) {
            lower_expr(*v.expr);
            return;
        }
    }, stmt.data);
}

// ── Expression lowering ─────────────────────────────────────────────────────

ValueId Lowerer::lower_expr(const Expr& e) {
    return std::visit([this](const auto& v) -> ValueId {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, IntLitExpr>) {
            Inst i;
            i.op          = Op::CONST_INT;
            i.ival        = v.value;
            i.result_type = FluxType::scalar(FluxType::Kind::Int);
            i.result_role = ValueRole::SCALAR;
            return emit_value(std::move(i));
        }

        if constexpr (std::is_same_v<T, FloatLitExpr>) {
            Inst i;
            i.op          = Op::CONST_FLOAT;
            i.fval        = v.value;
            i.result_type = FluxType::scalar(FluxType::Kind::Float);
            i.result_role = ValueRole::SCALAR;
            return emit_value(std::move(i));
        }

        if constexpr (std::is_same_v<T, BoolLitExpr>) {
            Inst i;
            i.op          = Op::CONST_BOOL;
            i.bval        = v.value;
            i.result_type = FluxType::scalar(FluxType::Kind::Bool);
            i.result_role = ValueRole::SCALAR;
            return emit_value(std::move(i));
        }

        if constexpr (std::is_same_v<T, IdentExpr>) {
            const auto& b = look(v.name);
            if (b.type.is_array()) return b.value;
            Inst i;
            i.op          = Op::LOAD;
            i.operands    = {b.value};
            i.result_type = b.type;
            i.result_role = ValueRole::SCALAR;
            return emit_value(std::move(i));
        }

        if constexpr (std::is_same_v<T, ArrayLitExpr>) {
            std::vector<ValueId> elems;
            elems.reserve(v.elements.size());
            for (const auto& el : v.elements) elems.push_back(lower_expr(*el));
            FluxType first = expr_type(*v.elements[0]);
            FluxType arr_t = FluxType::array(first.kind, (int)v.elements.size());
            Inst i;
            i.op          = Op::ARRAY_LIT;
            i.operands    = std::move(elems);
            i.result_type = arr_t;
            i.result_role = ValueRole::ARRAY;
            return emit_value(std::move(i));
        }

        if constexpr (std::is_same_v<T, IndexExpr>) {
            FluxType arr_t = expr_type(*v.array);
            ValueId  arr   = lower_expr(*v.array);
            ValueId  idx   = lower_expr(*v.index);
            Inst i;
            i.op          = Op::INDEX_LOAD;
            i.operands    = {arr, idx};
            i.result_type = arr_t.elem();
            i.result_role = ValueRole::SCALAR;
            return emit_value(std::move(i));
        }

        if constexpr (std::is_same_v<T, UnaryExpr>) {
            ValueId  operand = lower_expr(*v.operand);
            FluxType t       = expr_type(*v.operand);
            Inst i;
            i.op          = Op::NEG;
            i.operands    = {operand};
            i.result_type = t;
            i.result_role = ValueRole::SCALAR;
            return emit_value(std::move(i));
        }

        if constexpr (std::is_same_v<T, BinaryExpr>) {
            FluxType lhs_t = expr_type(*v.left);
            FluxType rhs_t = expr_type(*v.right);
            bool     any_array = lhs_t.is_array() || rhs_t.is_array();

            if (any_array) {
                ValueId lhs = lower_expr(*v.left);
                ValueId rhs = lower_expr(*v.right);
                FluxType result_t = lhs_t.is_array() ? lhs_t : rhs_t;
                Inst i;
                i.op          = Op::ARRAY_OP;
                i.sval        = v.op;
                i.operands    = {lhs, rhs};
                i.result_type = result_t;
                i.result_role = ValueRole::ARRAY;
                return emit_value(std::move(i));
            }

            ValueId  lhs       = lower_expr(*v.left);
            ValueId  rhs       = lower_expr(*v.right);
            FluxType result_t  = is_compare_op(v.op) ? FluxType::scalar(FluxType::Kind::Bool) : lhs_t;
            Inst i;
            i.op          = binop_to_op(v.op);
            i.operands    = {lhs, rhs};
            i.result_type = result_t;
            i.result_role = ValueRole::SCALAR;
            return emit_value(std::move(i));
        }

        if constexpr (std::is_same_v<T, CallExpr>) {
            // Built-in reductions.
            if (v.callee == "sum") {
                ValueId  a     = lower_expr(*v.args[0]);
                FluxType arr_t = expr_type(*v.args[0]);
                Inst i;
                i.op          = Op::REDUCE_SUM;
                i.operands    = {a};
                i.result_type = arr_t.elem();
                i.result_role = ValueRole::SCALAR;
                return emit_value(std::move(i));
            }
            if (v.callee == "dot") {
                ValueId  a     = lower_expr(*v.args[0]);
                ValueId  b     = lower_expr(*v.args[1]);
                FluxType arr_t = expr_type(*v.args[0]);
                Inst i;
                i.op          = Op::REDUCE_DOT;
                i.operands    = {a, b};
                i.result_type = arr_t.elem();
                i.result_role = ValueRole::SCALAR;
                return emit_value(std::move(i));
            }

            std::vector<ValueId> args;
            args.reserve(v.args.size());
            for (const auto& a : v.args) args.push_back(lower_expr(*a));
            auto it = fn_return_types_.find(v.callee);
            if (it == fn_return_types_.end())
                throw std::logic_error("mir lowering: unknown function '" + v.callee + "'");
            FluxType ret_t = it->second;
            Inst i;
            i.op          = Op::CALL;
            i.sval        = v.callee;
            i.operands    = std::move(args);
            i.result_type = ret_t;
            i.result_role = ret_t.is_array() ? ValueRole::ARRAY : ValueRole::SCALAR;
            return emit_value(std::move(i));
        }

        throw std::logic_error("mir lowering: unhandled expression kind");
    }, e.data);
}

// ── Type re-derivation (mirrors the type checker) ───────────────────────────

FluxType Lowerer::expr_type(const Expr& e) const {
    return std::visit([this](const auto& v) -> FluxType {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, IntLitExpr>)   return FluxType::scalar(FluxType::Kind::Int);
        if constexpr (std::is_same_v<T, FloatLitExpr>) return FluxType::scalar(FluxType::Kind::Float);
        if constexpr (std::is_same_v<T, BoolLitExpr>)  return FluxType::scalar(FluxType::Kind::Bool);
        if constexpr (std::is_same_v<T, IdentExpr>)    return look(v.name).type;
        if constexpr (std::is_same_v<T, ArrayLitExpr>) {
            FluxType first = expr_type(*v.elements[0]);
            return FluxType::array(first.kind, (int)v.elements.size());
        }
        if constexpr (std::is_same_v<T, IndexExpr>) return expr_type(*v.array).elem();
        if constexpr (std::is_same_v<T, UnaryExpr>) return expr_type(*v.operand);
        if constexpr (std::is_same_v<T, BinaryExpr>) {
            FluxType lhs = expr_type(*v.left);
            if (is_compare_op(v.op) && !lhs.is_array()) return FluxType::scalar(FluxType::Kind::Bool);
            if (lhs.is_array()) return lhs;
            return expr_type(*v.right);
        }
        if constexpr (std::is_same_v<T, CallExpr>) {
            if (v.callee == "sum" || v.callee == "dot") return expr_type(*v.args[0]).elem();
            auto it = fn_return_types_.find(v.callee);
            if (it == fn_return_types_.end())
                throw std::logic_error("mir lowering: unknown function '" + v.callee + "'");
            return it->second;
        }
        throw std::logic_error("mir lowering: unhandled expression kind in expr_type");
    }, e.data);
}

}  // namespace

Module lower_program(const Program& prog) {
    Lowerer l;
    return l.lower_module(prog);
}

}  // namespace mir
