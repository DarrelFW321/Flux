#include "midend/ir.hpp"
#include <sstream>

namespace mir {

const char* op_mnemonic(Op op) {
    switch (op) {
        case Op::CONST_INT:   return "const.int";
        case Op::CONST_FLOAT: return "const.float";
        case Op::CONST_BOOL:  return "const.bool";
        case Op::ALLOCA:      return "alloca";
        case Op::LOAD:        return "load";
        case Op::STORE:       return "store";
        case Op::NEG:         return "neg";
        case Op::ADD:         return "add";
        case Op::SUB:         return "sub";
        case Op::MUL:         return "mul";
        case Op::DIV:         return "div";
        case Op::MOD:         return "mod";
        case Op::CMP_EQ:      return "cmp.eq";
        case Op::CMP_NE:      return "cmp.ne";
        case Op::CMP_LT:      return "cmp.lt";
        case Op::CMP_GT:      return "cmp.gt";
        case Op::CMP_LE:      return "cmp.le";
        case Op::CMP_GE:      return "cmp.ge";
        case Op::ARRAY_LIT:   return "array.lit";
        case Op::ARRAY_OP:    return "array.op";
        case Op::INDEX_LOAD:  return "index.load";
        case Op::INDEX_STORE: return "index.store";
        case Op::ARRAY_COPY:  return "array.copy";
        case Op::REDUCE_SUM:  return "reduce.sum";
        case Op::REDUCE_DOT:  return "reduce.dot";
        case Op::FUSED_LOOP:  return "fused.loop";
        case Op::CALL:        return "call";
        case Op::PRINT:       return "print";
    }
    return "?";
}

static std::string type_str(const FluxType& t, ValueRole role) {
    std::string s = t.name();
    if (role == ValueRole::SLOT) s += "*";
    return s;
}

static void print_inst(std::ostream& os, const Inst& i) {
    os << "    ";
    if (i.result != NO_VALUE) {
        os << "%" << i.result << " : " << type_str(i.result_type, i.result_role) << " = ";
    }
    os << op_mnemonic(i.op);

    // Op-specific immediates
    switch (i.op) {
        case Op::CONST_INT:   os << " " << i.ival; break;
        case Op::CONST_FLOAT: os << " " << i.fval; break;
        case Op::CONST_BOOL:  os << " " << (i.bval ? "true" : "false"); break;
        case Op::ALLOCA:      os << " " << i.result_type.name(); break;
        case Op::ARRAY_OP:    os << " '" << i.sval << "'"; break;
        case Op::FUSED_LOOP: {
            os << " [";
            for (size_t k = 0; k < i.fused_ops.size(); ++k) {
                if (k) os << " ";
                os << i.fused_ops[k];
            }
            os << "]";
            break;
        }
        case Op::CALL:        os << " @" << i.sval; break;
        default: break;
    }

    // Operands
    for (size_t k = 0; k < i.operands.size(); ++k) {
        os << (k == 0 ? " " : ", ") << "%" << i.operands[k];
    }
    os << "\n";
}

static void print_term(std::ostream& os, const Terminator& t) {
    os << "    ";
    switch (t.kind) {
        case Terminator::BR:
            os << "br block_" << t.target_t;
            break;
        case Terminator::BR_COND:
            os << "br.cond %" << t.cond
               << ", block_" << t.target_t
               << ", block_" << t.target_f;
            break;
        case Terminator::RET:
            os << "ret %" << t.ret_val;
            break;
        case Terminator::RET_VOID:
            os << "ret";
            break;
        case Terminator::NONE:
            os << "<no terminator>";
            break;
    }
    os << "\n";
}

static void print_function(std::ostream& os, const Function& f) {
    os << "fn @" << f.name << "(";
    for (size_t i = 0; i < f.params.size(); ++i) {
        if (i) os << ", ";
        os << "%" << f.params[i].id << " : " << f.params[i].type.name();
    }
    os << ") -> " << f.return_type.name() << " {\n";
    for (const auto& b : f.blocks) {
        os << "  block_" << b.id;
        if (!b.label.empty()) os << "  [" << b.label << "]";
        os << ":\n";
        for (const auto& inst : b.insts) print_inst(os, inst);
        print_term(os, b.term);
    }
    os << "}\n";
}

std::string print_module(const Module& m) {
    std::ostringstream os;
    for (size_t i = 0; i < m.functions.size(); ++i) {
        if (i) os << "\n";
        print_function(os, m.functions[i]);
    }
    return os.str();
}

}  // namespace mir
