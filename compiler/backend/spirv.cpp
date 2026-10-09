#include "backend/spirv.hpp"
#include "common/builtins.hpp"
#include "common/format.hpp"
#include <cstring>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace backend {
namespace {

using namespace ir;

// ── SPIR-V enums (from the Khronos unified headers) ──────────────────────────
namespace spv {
enum Op : uint16_t {
    OpSource = 3, OpName = 5, OpMemberName = 6, OpString = 7, OpLine = 8,
    OpExtInstImport = 11, OpExtInst = 12, OpMemoryModel = 14, OpEntryPoint = 15,
    OpExecutionMode = 16, OpCapability = 17,
    OpTypeVoid = 19, OpTypeBool = 20, OpTypeInt = 21, OpTypeFloat = 22, OpTypeVector = 23,
    OpTypeMatrix = 24, OpTypeStruct = 30, OpTypePointer = 32, OpTypeFunction = 33,
    OpConstantTrue = 41, OpConstantFalse = 42, OpConstant = 43, OpConstantComposite = 44,
    OpFunction = 54, OpFunctionParameter = 55, OpFunctionEnd = 56, OpFunctionCall = 57,
    OpVariable = 59, OpLoad = 61, OpStore = 62, OpAccessChain = 65,
    OpDecorate = 71, OpMemberDecorate = 72,
    OpVectorExtractDynamic = 77, OpVectorShuffle = 79, OpCompositeConstruct = 80,
    OpCompositeExtract = 81, OpCompositeInsert = 82, OpTranspose = 84,
    OpConvertFToS = 110, OpConvertSToF = 111,
    OpSNegate = 126, OpFNegate = 127, OpIAdd = 128, OpFAdd = 129, OpISub = 130, OpFSub = 131,
    OpIMul = 132, OpFMul = 133, OpSDiv = 135, OpFDiv = 136, OpSRem = 138, OpFRem = 140,
    OpVectorTimesScalar = 142, OpMatrixTimesScalar = 143, OpVectorTimesMatrix = 144,
    OpMatrixTimesVector = 145, OpMatrixTimesMatrix = 146, OpDot = 148,
    OpLogicalEqual = 164, OpLogicalNotEqual = 165, OpLogicalOr = 166, OpLogicalAnd = 167,
    OpLogicalNot = 168, OpSelect = 169, OpIEqual = 170, OpINotEqual = 171,
    OpSGreaterThan = 173, OpSGreaterThanEqual = 175, OpSLessThan = 177, OpSLessThanEqual = 179,
    OpFOrdEqual = 180, OpFUnordNotEqual = 183, OpFOrdLessThan = 184, OpFOrdGreaterThan = 186,
    OpFOrdLessThanEqual = 188, OpFOrdGreaterThanEqual = 190,
    OpLoopMerge = 246, OpSelectionMerge = 247, OpLabel = 248, OpBranch = 249,
    OpBranchConditional = 250, OpKill = 252, OpReturn = 253, OpReturnValue = 254,
    OpUnreachable = 255,
};

const char* op_name(uint16_t op) {
    switch (op) {
        case OpSource: return "OpSource"; case OpName: return "OpName"; case OpMemberName: return "OpMemberName";
        case OpString: return "OpString"; case OpLine: return "OpLine"; case OpExtInstImport: return "OpExtInstImport";
        case OpExtInst: return "OpExtInst"; case OpMemoryModel: return "OpMemoryModel"; case OpEntryPoint: return "OpEntryPoint";
        case OpExecutionMode: return "OpExecutionMode"; case OpCapability: return "OpCapability";
        case OpTypeVoid: return "OpTypeVoid"; case OpTypeBool: return "OpTypeBool"; case OpTypeInt: return "OpTypeInt";
        case OpTypeFloat: return "OpTypeFloat"; case OpTypeVector: return "OpTypeVector"; case OpTypeMatrix: return "OpTypeMatrix";
        case OpTypeStruct: return "OpTypeStruct"; case OpTypePointer: return "OpTypePointer"; case OpTypeFunction: return "OpTypeFunction";
        case OpConstantTrue: return "OpConstantTrue"; case OpConstantFalse: return "OpConstantFalse";
        case OpConstant: return "OpConstant"; case OpConstantComposite: return "OpConstantComposite";
        case OpFunction: return "OpFunction"; case OpFunctionParameter: return "OpFunctionParameter";
        case OpFunctionEnd: return "OpFunctionEnd"; case OpFunctionCall: return "OpFunctionCall";
        case OpVariable: return "OpVariable"; case OpLoad: return "OpLoad"; case OpStore: return "OpStore";
        case OpAccessChain: return "OpAccessChain"; case OpDecorate: return "OpDecorate"; case OpMemberDecorate: return "OpMemberDecorate";
        case OpVectorExtractDynamic: return "OpVectorExtractDynamic"; case OpVectorShuffle: return "OpVectorShuffle";
        case OpCompositeConstruct: return "OpCompositeConstruct"; case OpCompositeExtract: return "OpCompositeExtract";
        case OpCompositeInsert: return "OpCompositeInsert"; case OpTranspose: return "OpTranspose";
        case OpConvertFToS: return "OpConvertFToS"; case OpConvertSToF: return "OpConvertSToF";
        case OpSNegate: return "OpSNegate"; case OpFNegate: return "OpFNegate"; case OpIAdd: return "OpIAdd";
        case OpFAdd: return "OpFAdd"; case OpISub: return "OpISub"; case OpFSub: return "OpFSub"; case OpIMul: return "OpIMul";
        case OpFMul: return "OpFMul"; case OpSDiv: return "OpSDiv"; case OpFDiv: return "OpFDiv"; case OpSRem: return "OpSRem";
        case OpFRem: return "OpFRem"; case OpVectorTimesScalar: return "OpVectorTimesScalar";
        case OpMatrixTimesScalar: return "OpMatrixTimesScalar"; case OpVectorTimesMatrix: return "OpVectorTimesMatrix";
        case OpMatrixTimesVector: return "OpMatrixTimesVector"; case OpMatrixTimesMatrix: return "OpMatrixTimesMatrix";
        case OpDot: return "OpDot"; case OpLogicalEqual: return "OpLogicalEqual"; case OpLogicalNotEqual: return "OpLogicalNotEqual";
        case OpLogicalOr: return "OpLogicalOr"; case OpLogicalAnd: return "OpLogicalAnd"; case OpLogicalNot: return "OpLogicalNot";
        case OpSelect: return "OpSelect"; case OpIEqual: return "OpIEqual"; case OpINotEqual: return "OpINotEqual";
        case OpSGreaterThan: return "OpSGreaterThan"; case OpSGreaterThanEqual: return "OpSGreaterThanEqual";
        case OpSLessThan: return "OpSLessThan"; case OpSLessThanEqual: return "OpSLessThanEqual";
        case OpFOrdEqual: return "OpFOrdEqual"; case OpFUnordNotEqual: return "OpFUnordNotEqual";
        case OpFOrdLessThan: return "OpFOrdLessThan"; case OpFOrdGreaterThan: return "OpFOrdGreaterThan";
        case OpFOrdLessThanEqual: return "OpFOrdLessThanEqual"; case OpFOrdGreaterThanEqual: return "OpFOrdGreaterThanEqual";
        case OpLoopMerge: return "OpLoopMerge"; case OpSelectionMerge: return "OpSelectionMerge"; case OpLabel: return "OpLabel";
        case OpBranch: return "OpBranch"; case OpBranchConditional: return "OpBranchConditional"; case OpKill: return "OpKill";
        case OpReturn: return "OpReturn"; case OpReturnValue: return "OpReturnValue"; case OpUnreachable: return "OpUnreachable";
    }
    return "Op?";
}

enum : uint32_t {
    CapabilityShader = 1, AddressingLogical = 0, MemoryGLSL450 = 1,
    ExecVertex = 0, ExecFragment = 4, ModeOriginUpperLeft = 7,
    StorageInput = 1, StorageUniform = 2, StorageOutput = 3, StorageFunction = 7,
    DecoBlock = 2, DecoColMajor = 5, DecoMatrixStride = 7, DecoBuiltIn = 11, DecoLocation = 30,
    DecoBinding = 33, DecoDescriptorSet = 34, DecoOffset = 35,
    BuiltInPosition = 0, BuiltInFragCoord = 15, BuiltInFrontFacing = 17,
    BuiltInVertexIndex = 42, BuiltInInstanceIndex = 43,
};
} // namespace spv

// ── Instruction model ────────────────────────────────────────────────────────
// Instructions are kept symbolically so the same data produces both the
// binary words and the human-readable disassembly.
struct Operand {
    enum Kind { Id, Lit, Str, Enum } kind;
    uint32_t    value = 0;
    std::string text;   // Str: the string; Enum: the spelled name
};

struct SInst {
    uint16_t opcode;
    uint32_t type = 0;     // result type id (0 = none)
    uint32_t result = 0;   // result id (0 = none)
    std::vector<Operand> ops = {};
    int      src_line = 0;
};

Operand id(uint32_t v) { return {Operand::Id, v, ""}; }
Operand lit(uint32_t v) { return {Operand::Lit, v, ""}; }
Operand str(const std::string& s) { return {Operand::Str, 0, s}; }
Operand en(uint32_t v, const char* name) { return {Operand::Enum, v, name}; }

class Builder {
public:
    explicit Builder(const Module& m, std::string source_name) : m_(m), source_name_(std::move(source_name)) {}

    SpirvOutput run();

private:
    const Module& m_;
    std::string source_name_;
    uint32_t next_ = 1;

    // Module sections, in the order SPIR-V requires.
    std::vector<SInst> capabilities_, ext_imports_, memory_model_, entry_points_, exec_modes_,
                       debug_strings_, debug_names_, annotations_, globals_, functions_;

    std::unordered_map<uint32_t, std::string> names_;   // friendly names for disassembly
    std::unordered_set<std::string> used_names_;
    std::map<std::string, uint32_t> type_cache_, const_cache_;
    uint32_t glsl_ = 0, file_ = 0;
    std::unordered_map<std::string, uint32_t> fn_ids_;

    // Uniform block
    uint32_t uniform_var_ = 0;
    std::vector<uint32_t> uniform_member_types_;

    // Per-function state
    const Function* fn_ = nullptr;
    std::unordered_map<int, uint32_t> val_;          // IR value → SPIR-V id
    std::unordered_map<int, Type> types_;            // IR value → type
    std::unordered_map<int, const Inst*> defs_;
    std::vector<uint32_t> entry_inputs_;             // per param: Input variable
    uint32_t entry_output_ = 0;
    std::vector<uint32_t> param_ids_;
    bool block_open_ = false;
    int  last_line_ = -1;
    struct LoopTargets { uint32_t merge, cont; };
    std::vector<LoopTargets> loops_;

    uint32_t fresh() { return next_++; }

    void name(uint32_t target, const std::string& n, bool debug = true) {
        std::string base = n;
        std::string u = base;
        for (int k = 1; used_names_.count(u); ++k) u = base + "_" + std::to_string(k);
        used_names_.insert(u);
        names_[target] = u;
        if (debug) debug_names_.push_back({spv::OpName, 0, 0, {id(target), str(n)}});
    }

    // ── Types ───────────────────────────────────────────────────────────

    uint32_t cached_type(const std::string& key, const std::string& friendly, SInst inst) {
        auto it = type_cache_.find(key);
        if (it != type_cache_.end()) return it->second;
        uint32_t t = fresh();
        inst.result = t;
        globals_.push_back(std::move(inst));
        type_cache_[key] = t;
        name(t, friendly, false);
        return t;
    }

    uint32_t t_void()  { return cached_type("void", "void", {spv::OpTypeVoid}); }
    uint32_t t_bool()  { return cached_type("bool", "bool", {spv::OpTypeBool}); }
    uint32_t t_int()   { return cached_type("int", "int", {spv::OpTypeInt, 0, 0, {lit(32), lit(1)}}); }
    uint32_t t_float() { return cached_type("float", "float", {spv::OpTypeFloat, 0, 0, {lit(32)}}); }

    uint32_t type(Type t) {
        switch (t.base) {
            case Type::Base::Void: return t_void();
            case Type::Base::Bool: return t.rows > 1 ? bvec(t.rows) : t_bool();
            case Type::Base::Int:  return t_int();
            default: break;
        }
        if (t.is_matrix()) {
            uint32_t col = type(t.column());
            std::string n = "mat" + std::to_string(t.cols) + "v" + std::to_string(t.rows) + "float";
            return cached_type(n, n, {spv::OpTypeMatrix, 0, 0, {id(col), lit(static_cast<uint32_t>(t.cols))}});
        }
        if (t.is_vector()) {
            uint32_t f = t_float();
            std::string n = "v" + std::to_string(t.rows) + "float";
            return cached_type(n, n, {spv::OpTypeVector, 0, 0, {id(f), lit(static_cast<uint32_t>(t.rows))}});
        }
        return t_float();
    }

    uint32_t bvec(int n) {
        uint32_t b = t_bool();
        std::string nm = "v" + std::to_string(n) + "bool";
        return cached_type(nm, nm, {spv::OpTypeVector, 0, 0, {id(b), lit(static_cast<uint32_t>(n))}});
    }

    static const char* storage_name(uint32_t sc) {
        switch (sc) {
            case spv::StorageInput: return "Input";
            case spv::StorageUniform: return "Uniform";
            case spv::StorageOutput: return "Output";
            case spv::StorageFunction: return "Function";
        }
        return "?";
    }

    uint32_t ptr(uint32_t sc, uint32_t pointee) {
        std::string n = std::string("_ptr_") + storage_name(sc) + "_" + names_[pointee];
        return cached_type("ptr" + std::to_string(sc) + "_" + std::to_string(pointee), n,
                           {spv::OpTypePointer, 0, 0, {en(sc, storage_name(sc)), id(pointee)}});
    }

    uint32_t fn_type(uint32_t ret, const std::vector<uint32_t>& params) {
        std::string key = "fn" + std::to_string(ret), n = "fn_" + names_[ret];
        SInst inst{spv::OpTypeFunction, 0, 0, {id(ret)}};
        for (uint32_t p : params) {
            key += "_" + std::to_string(p);
            n += "_" + names_[p];
            inst.ops.push_back(id(p));
        }
        return cached_type(key, n, inst);
    }

    // ── Constants ───────────────────────────────────────────────────────

    uint32_t cached_const(const std::string& key, const std::string& friendly, SInst inst) {
        auto it = const_cache_.find(key);
        if (it != const_cache_.end()) return it->second;
        uint32_t c = fresh();
        inst.result = c;
        globals_.push_back(std::move(inst));
        const_cache_[key] = c;
        name(c, friendly, false);
        return c;
    }

    uint32_t const_int(long long v) {
        uint32_t t = t_int();
        std::string n = "int_" + std::string(v < 0 ? "n" : "") + std::to_string(v < 0 ? -v : v);
        return cached_const("i" + std::to_string(v), n,
                            {spv::OpConstant, t, 0, {lit(static_cast<uint32_t>(static_cast<int32_t>(v)))}});
    }

    uint32_t const_float(double v) {
        uint32_t t = t_float();
        float f = static_cast<float>(v);
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        std::string n = "float_" + format_float(v);
        for (char& c : n) if (c == '.' || c == '-' || c == '+') c = c == '-' ? 'n' : '_';
        return cached_const("f" + std::to_string(bits), n, {spv::OpConstant, t, 0, {lit(bits)}});
    }

    uint32_t const_bool(bool v) {
        uint32_t t = t_bool();
        return cached_const(v ? "true" : "false", v ? "true" : "false",
                            {v ? spv::OpConstantTrue : spv::OpConstantFalse, t, 0, {}});
    }

    uint32_t const_composite(Type t, const std::vector<uint32_t>& parts) {
        uint32_t ty = type(t);
        std::string key = "c" + std::to_string(ty);
        SInst inst{spv::OpConstantComposite, ty, 0, {}};
        for (uint32_t p : parts) {
            key += "_" + std::to_string(p);
            inst.ops.push_back(id(p));
        }
        return cached_const(key, "c" + names_[ty], inst);
    }

    // ── Emission into the current function ─────────────────────────────

    bool is_global_const(uint32_t v) const {
        for (const auto& [k, c] : const_cache_) if (c == v) return true;
        return false;
    }

    void emit(SInst inst, int src_line) {
        // Debug line info precedes ordinary instructions (never labels,
        // variables, or the merge+branch pairs that must stay adjacent).
        bool annotate = inst.opcode != spv::OpLabel && inst.opcode != spv::OpVariable &&
                        inst.opcode != spv::OpSelectionMerge && inst.opcode != spv::OpLoopMerge &&
                        inst.opcode != spv::OpBranch && inst.opcode != spv::OpBranchConditional &&
                        inst.opcode != spv::OpFunction && inst.opcode != spv::OpFunctionParameter &&
                        inst.opcode != spv::OpFunctionEnd;
        if (annotate && src_line > 0 && src_line != last_line_) {
            functions_.push_back({spv::OpLine, 0, 0, {id(file_), lit(static_cast<uint32_t>(src_line)), lit(1)}, src_line});
            last_line_ = src_line;
        }
        if (inst.opcode == spv::OpLabel) last_line_ = -1;   // OpLine scope ends at a block boundary
        inst.src_line = src_line;
        functions_.push_back(std::move(inst));
    }

    uint32_t value_op(uint16_t op, Type t, std::vector<Operand> ops, int line) {
        uint32_t r = fresh();
        emit({op, type(t), r, std::move(ops)}, line);
        return r;
    }

    void label(uint32_t l) {
        emit({spv::OpLabel, 0, l, {}}, 0);
        block_open_ = true;
    }

    void terminate(SInst inst, int line) {
        if (!block_open_) return;
        emit(std::move(inst), line);
        block_open_ = false;
    }

    uint32_t v(int ir_id) {
        auto it = val_.find(ir_id);
        return it == val_.end() ? 0 : it->second;
    }

    Type ty(int ir_id) {
        auto it = types_.find(ir_id);
        return it == types_.end() ? Type::float_() : it->second;
    }

    uint32_t ext(int glsl_op, Type t, const std::vector<int>& args, int line, const char* nm) {
        std::vector<Operand> ops = {id(glsl_), en(static_cast<uint32_t>(glsl_op), nm)};
        for (int a : args) ops.push_back(id(v(a)));
        return value_op(spv::OpExtInst, t, std::move(ops), line);
    }

    void region(const Region& r);
    void inst(const Inst& i);
    uint32_t value(const Inst& i);
    void function(const Function& f);
    void entry_interface(const Function& f, uint32_t fn_id);
    void uniforms();
    std::string disassemble(const std::vector<SInst>& all, std::vector<int>& line_map) const;
};

// ── Module structure ─────────────────────────────────────────────────────────

void Builder::uniforms() {
    if (m_.uniforms.empty()) return;
    SInst st{spv::OpTypeStruct, 0, 0, {}};
    for (const auto& u : m_.uniforms) {
        uint32_t t = type(u.type);
        uniform_member_types_.push_back(t);
        st.ops.push_back(id(t));
    }
    uint32_t s = fresh();
    st.result = s;
    globals_.push_back(st);
    name(s, "Uniforms");
    annotations_.push_back({spv::OpDecorate, 0, 0, {id(s), en(spv::DecoBlock, "Block")}});
    for (size_t k = 0; k < m_.uniforms.size(); ++k) {
        const auto& u = m_.uniforms[k];
        debug_names_.push_back({spv::OpMemberName, 0, 0, {id(s), lit(static_cast<uint32_t>(k)), str(u.name)}});
        annotations_.push_back({spv::OpMemberDecorate, 0, 0,
                                {id(s), lit(static_cast<uint32_t>(k)), en(spv::DecoOffset, "Offset"),
                                 lit(static_cast<uint32_t>(u.offset))}});
        if (u.type.is_matrix()) {
            annotations_.push_back({spv::OpMemberDecorate, 0, 0,
                                    {id(s), lit(static_cast<uint32_t>(k)), en(spv::DecoColMajor, "ColMajor")}});
            annotations_.push_back({spv::OpMemberDecorate, 0, 0,
                                    {id(s), lit(static_cast<uint32_t>(k)), en(spv::DecoMatrixStride, "MatrixStride"), lit(16)}});
        }
    }
    uint32_t p = ptr(spv::StorageUniform, s);
    uniform_var_ = fresh();
    globals_.push_back({spv::OpVariable, p, uniform_var_, {en(spv::StorageUniform, "Uniform")}});
    name(uniform_var_, "u");
    annotations_.push_back({spv::OpDecorate, 0, 0, {id(uniform_var_), en(spv::DecoDescriptorSet, "DescriptorSet"), lit(0)}});
    annotations_.push_back({spv::OpDecorate, 0, 0, {id(uniform_var_), en(spv::DecoBinding, "Binding"), lit(0)}});
}

static uint32_t builtin_enum(const std::string& b, const char** nm) {
    if (b == "position")       { *nm = "FragCoord";     return spv::BuiltInFragCoord; }
    if (b == "front_facing")   { *nm = "FrontFacing";   return spv::BuiltInFrontFacing; }
    if (b == "vertex_index")   { *nm = "VertexIndex";   return spv::BuiltInVertexIndex; }
    *nm = "InstanceIndex";
    return spv::BuiltInInstanceIndex;
}

void Builder::entry_interface(const Function& f, uint32_t fn_id) {
    std::vector<Operand> ep = {
        f.stage == Stage::Fragment ? en(spv::ExecFragment, "Fragment") : en(spv::ExecVertex, "Vertex"),
        id(fn_id), str(f.name)};
    entry_inputs_.clear();
    for (size_t k = 0; k < f.param_types.size(); ++k) {
        uint32_t t = type(f.param_types[k]);
        uint32_t var = fresh();
        globals_.push_back({spv::OpVariable, ptr(spv::StorageInput, t), var, {en(spv::StorageInput, "Input")}});
        name(var, f.param_names[k]);
        const EntryParam& p = f.entry_params[k];
        if (!p.builtin.empty()) {
            const char* nm;
            uint32_t b = builtin_enum(p.builtin, &nm);
            annotations_.push_back({spv::OpDecorate, 0, 0, {id(var), en(spv::DecoBuiltIn, "BuiltIn"), en(b, nm)}});
        } else {
            annotations_.push_back({spv::OpDecorate, 0, 0, {id(var), en(spv::DecoLocation, "Location"),
                                                             lit(static_cast<uint32_t>(p.location))}});
        }
        entry_inputs_.push_back(var);
        ep.push_back(id(var));
    }
    uint32_t vec4 = type(Type::vec(4));
    entry_output_ = fresh();
    globals_.push_back({spv::OpVariable, ptr(spv::StorageOutput, vec4), entry_output_, {en(spv::StorageOutput, "Output")}});
    if (f.stage == Stage::Fragment) {
        name(entry_output_, "out_color");
        annotations_.push_back({spv::OpDecorate, 0, 0, {id(entry_output_), en(spv::DecoLocation, "Location"), lit(0)}});
    } else {
        name(entry_output_, "out_position");
        annotations_.push_back({spv::OpDecorate, 0, 0, {id(entry_output_), en(spv::DecoBuiltIn, "BuiltIn"),
                                                         en(spv::BuiltInPosition, "Position")}});
    }
    ep.push_back(id(entry_output_));
    entry_points_.push_back({spv::OpEntryPoint, 0, 0, ep});
    if (f.stage == Stage::Fragment)
        exec_modes_.push_back({spv::OpExecutionMode, 0, 0, {id(fn_id), en(spv::ModeOriginUpperLeft, "OriginUpperLeft")}});
}

void Builder::function(const Function& f) {
    fn_ = &f;
    val_.clear();
    types_.clear();
    defs_.clear();
    loops_.clear();
    param_ids_.clear();
    last_line_ = -1;
    for_each_inst(f.body, [&](const Inst& i) {
        if (i.id >= 0) { types_[i.id] = i.type; defs_[i.id] = &i; }
    });

    uint32_t fn_id = fresh();
    name(fn_id, f.name);
    fn_ids_[f.name] = fn_id;
    bool entry = f.stage != Stage::None;
    uint32_t ret = entry ? t_void() : type(f.return_type);
    std::vector<uint32_t> ptypes;
    if (!entry) for (Type t : f.param_types) ptypes.push_back(type(t));
    uint32_t ft = fn_type(ret, ptypes);
    if (entry) entry_interface(f, fn_id);

    emit({spv::OpFunction, ret, fn_id, {en(0, "None"), id(ft)}}, f.line);
    for (size_t k = 0; k < ptypes.size(); ++k) {
        uint32_t p = fresh();
        emit({spv::OpFunctionParameter, ptypes[k], p, {}}, f.line);
        name(p, f.param_names[k]);
        param_ids_.push_back(p);
    }
    label(fresh());

    // OpVariable must come first in the entry block: hoist every `var`.
    for_each_inst(f.body, [&](const Inst& i) {
        if (i.op != Op::Var) return;
        uint32_t var = fresh();
        emit({spv::OpVariable, ptr(spv::StorageFunction, type(i.type)), var, {en(spv::StorageFunction, "Function")}}, i.line);
        name(var, i.name.empty() ? "v" + std::to_string(i.id) : i.name);
        val_[i.id] = var;
    });

    region(f.body);

    // Falling off the end: void functions return; anything else is unreachable
    // (the type checker guarantees every path of a non-void function returns).
    if (block_open_) {
        if (f.return_type.is_void() || entry) terminate({spv::OpReturn}, f.line);
        else terminate({spv::OpUnreachable}, f.line);
    }
    emit({spv::OpFunctionEnd}, 0);
    fn_ = nullptr;
}

// ── Instructions ─────────────────────────────────────────────────────────────

void Builder::region(const Region& r) {
    for (const auto& i : r.insts) {
        if (!block_open_) break;   // code after a terminator is unreachable
        inst(i);
    }
}

void Builder::inst(const Inst& i) {
    int line = i.line;
    switch (i.op) {
        case Op::Var: return;   // hoisted
        case Op::Store:
            emit({spv::OpStore, 0, 0, {id(v(i.args[0])), id(v(i.args[1]))}}, line);
            return;
        case Op::If: {
            uint32_t then_l = fresh(), merge_l = fresh();
            bool has_else = !i.regions[1].insts.empty();
            uint32_t else_l = has_else ? fresh() : merge_l;
            emit({spv::OpSelectionMerge, 0, 0, {id(merge_l), en(0, "None")}}, line);
            terminate({spv::OpBranchConditional, 0, 0, {id(v(i.args[0])), id(then_l), id(else_l)}}, line);
            label(then_l);
            region(i.regions[0]);
            bool then_open = block_open_;
            terminate({spv::OpBranch, 0, 0, {id(merge_l)}}, line);
            bool else_open = true;
            if (has_else) {
                label(else_l);
                region(i.regions[1]);
                else_open = block_open_;
                terminate({spv::OpBranch, 0, 0, {id(merge_l)}}, line);
            }
            label(merge_l);
            if (!then_open && !else_open) terminate({spv::OpUnreachable}, line);
            return;
        }
        case Op::Loop: {
            uint32_t header_l = fresh(), body_l = fresh(), cont_l = fresh(), merge_l = fresh();
            terminate({spv::OpBranch, 0, 0, {id(header_l)}}, line);
            label(header_l);
            region(i.regions[0]);
            emit({spv::OpLoopMerge, 0, 0, {id(merge_l), id(cont_l), en(0, "None")}}, line);
            if (i.cond >= 0)
                terminate({spv::OpBranchConditional, 0, 0, {id(v(i.cond)), id(body_l), id(merge_l)}}, line);
            else
                terminate({spv::OpBranch, 0, 0, {id(body_l)}}, line);
            loops_.push_back({merge_l, cont_l});
            label(body_l);
            region(i.regions[1]);
            terminate({spv::OpBranch, 0, 0, {id(cont_l)}}, line);
            label(cont_l);
            region(i.regions[2]);
            terminate({spv::OpBranch, 0, 0, {id(header_l)}}, line);
            loops_.pop_back();
            label(merge_l);
            return;
        }
        case Op::Break:
            terminate({spv::OpBranch, 0, 0, {id(loops_.back().merge)}}, line);
            return;
        case Op::Continue:
            terminate({spv::OpBranch, 0, 0, {id(loops_.back().cont)}}, line);
            return;
        case Op::Discard:
            terminate({spv::OpKill}, line);
            return;
        case Op::Return:
            if (fn_->stage != Stage::None) {
                if (!i.args.empty()) emit({spv::OpStore, 0, 0, {id(entry_output_), id(v(i.args[0]))}}, line);
                terminate({spv::OpReturn}, line);
            } else if (i.args.empty()) {
                terminate({spv::OpReturn}, line);
            } else {
                terminate({spv::OpReturnValue, 0, 0, {id(v(i.args[0]))}}, line);
            }
            return;
        default:
            if (i.id >= 0) val_[i.id] = value(i);
            return;
    }
}

uint32_t Builder::value(const Inst& i) {
    int line = i.line;
    Type t = i.type;
    auto args = [&]() {
        std::vector<Operand> ops;
        for (int a : i.args) ops.push_back(id(v(a)));
        return ops;
    };
    bool fl = t.base == Type::Base::Float;
    auto arith = [&](uint16_t iop, uint16_t fop) { return value_op(fl ? fop : iop, t, args(), line); };
    auto const_arg = [&](int a) { return is_global_const(v(a)); };

    switch (i.op) {
        case Op::Const:
            if (t.is_float()) return const_float(i.fconst);
            if (t.is_bool())  return const_bool(i.iconst != 0);
            return const_int(i.iconst);
        case Op::Arg:
            if (fn_->stage != Stage::None) {
                uint32_t var = entry_inputs_[i.imm];
                uint32_t r = value_op(spv::OpLoad, t, {id(var)}, line);
                name(r, i.name.empty() ? fn_->param_names[i.imm] : i.name, false);
                return r;
            }
            return param_ids_[i.imm];
        case Op::Uniform: {
            uint32_t p = ptr(spv::StorageUniform, type(t));
            uint32_t chain = fresh();
            emit({spv::OpAccessChain, p, chain, {id(uniform_var_), id(const_int(i.imm))}}, line);
            return value_op(spv::OpLoad, t, {id(chain)}, line);
        }
        case Op::Add: return arith(spv::OpIAdd, spv::OpFAdd);
        case Op::Sub: return arith(spv::OpISub, spv::OpFSub);
        case Op::Mul: {
            // vec * splat(s) → OpVectorTimesScalar
            if (fl && t.is_vector()) {
                for (int side = 0; side < 2; ++side) {
                    auto d = defs_.find(i.args[side]);
                    if (d != defs_.end() && d->second->op == Op::Splat)
                        return value_op(spv::OpVectorTimesScalar, t,
                                        {id(v(i.args[1 - side])), id(v(d->second->args[0]))}, line);
                }
            }
            return arith(spv::OpIMul, spv::OpFMul);
        }
        case Op::Div: return arith(spv::OpSDiv, spv::OpFDiv);
        case Op::Rem: return arith(spv::OpSRem, spv::OpFRem);
        case Op::Neg: return arith(spv::OpSNegate, spv::OpFNegate);
        case Op::MatScale: return value_op(spv::OpMatrixTimesScalar, t, args(), line);
        case Op::MatMul: {
            Type a = ty(i.args[0]), b = ty(i.args[1]);
            uint16_t op = a.is_matrix() && b.is_matrix() ? spv::OpMatrixTimesMatrix
                        : a.is_matrix()                  ? spv::OpMatrixTimesVector
                                                         : spv::OpVectorTimesMatrix;
            return value_op(op, t, args(), line);
        }
        case Op::Eq: case Op::Ne: case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: {
            Type a = ty(i.args[0]);
            uint16_t op = 0;
            if (a.is_bool()) op = i.op == Op::Eq ? spv::OpLogicalEqual : spv::OpLogicalNotEqual;
            else if (a.is_int()) {
                switch (i.op) {
                    case Op::Eq: op = spv::OpIEqual; break;
                    case Op::Ne: op = spv::OpINotEqual; break;
                    case Op::Lt: op = spv::OpSLessThan; break;
                    case Op::Le: op = spv::OpSLessThanEqual; break;
                    case Op::Gt: op = spv::OpSGreaterThan; break;
                    default:     op = spv::OpSGreaterThanEqual; break;
                }
            } else {
                switch (i.op) {
                    case Op::Eq: op = spv::OpFOrdEqual; break;
                    case Op::Ne: op = spv::OpFUnordNotEqual; break;   // NaN != x is true
                    case Op::Lt: op = spv::OpFOrdLessThan; break;
                    case Op::Le: op = spv::OpFOrdLessThanEqual; break;
                    case Op::Gt: op = spv::OpFOrdGreaterThan; break;
                    default:     op = spv::OpFOrdGreaterThanEqual; break;
                }
            }
            return value_op(op, t, args(), line);
        }
        case Op::And: return value_op(spv::OpLogicalAnd, t, args(), line);
        case Op::Or:  return value_op(spv::OpLogicalOr, t, args(), line);
        case Op::Not: return value_op(spv::OpLogicalNot, t, args(), line);
        case Op::Select: {
            uint32_t c = v(i.args[0]), a = v(i.args[1]), b = v(i.args[2]);
            if (t.is_scalar()) return value_op(spv::OpSelect, t, {id(c), id(a), id(b)}, line);
            if (t.is_vector()) {
                // SPIR-V 1.0 needs a condition with one bool per component.
                std::vector<Operand> parts(t.rows, id(c));
                uint32_t bc = fresh();
                emit({spv::OpCompositeConstruct, bvec(t.rows), bc, parts}, line);
                return value_op(spv::OpSelect, t, {id(bc), id(a), id(b)}, line);
            }
            // Matrices: select column by column.
            Type col = t.column();
            std::vector<Operand> parts(t.rows, id(c));
            uint32_t bc = fresh();
            emit({spv::OpCompositeConstruct, bvec(t.rows), bc, parts}, line);
            std::vector<Operand> cols;
            for (int k = 0; k < t.cols; ++k) {
                uint32_t ca = value_op(spv::OpCompositeExtract, col, {id(a), lit(static_cast<uint32_t>(k))}, line);
                uint32_t cb = value_op(spv::OpCompositeExtract, col, {id(b), lit(static_cast<uint32_t>(k))}, line);
                cols.push_back(id(value_op(spv::OpSelect, col, {id(bc), id(ca), id(cb)}, line)));
            }
            return value_op(spv::OpCompositeConstruct, t, cols, line);
        }
        case Op::Splat: {
            if (const_arg(i.args[0])) return const_composite(t, std::vector<uint32_t>(t.rows, v(i.args[0])));
            std::vector<Operand> parts(t.rows, id(v(i.args[0])));
            return value_op(spv::OpCompositeConstruct, t, parts, line);
        }
        case Op::Construct: {
            bool all_const = true;
            for (int a : i.args) all_const = all_const && const_arg(a);
            // OpConstantComposite needs exactly one constituent per component.
            bool flat = static_cast<int>(i.args.size()) == (t.is_matrix() ? t.cols : t.rows);
            if (all_const && flat) {
                std::vector<uint32_t> parts;
                for (int a : i.args) parts.push_back(v(a));
                return const_composite(t, parts);
            }
            return value_op(spv::OpCompositeConstruct, t, args(), line);
        }
        case Op::Extract:
            return value_op(spv::OpCompositeExtract, t, {id(v(i.args[0])), lit(static_cast<uint32_t>(i.imm))}, line);
        case Op::ExtractDyn:
            return value_op(spv::OpVectorExtractDynamic, t, args(), line);
        case Op::Insert:
            return value_op(spv::OpCompositeInsert, t,
                            {id(v(i.args[1])), id(v(i.args[0])), lit(static_cast<uint32_t>(i.imm))}, line);
        case Op::Swizzle: {
            std::vector<Operand> ops = {id(v(i.args[0])), id(v(i.args[0]))};
            for (int l : i.lanes) ops.push_back(lit(static_cast<uint32_t>(l)));
            return value_op(spv::OpVectorShuffle, t, ops, line);
        }
        case Op::Shuffle: {
            std::vector<Operand> ops = {id(v(i.args[0])), id(v(i.args[1]))};
            for (int l : i.lanes) ops.push_back(lit(static_cast<uint32_t>(l)));
            return value_op(spv::OpVectorShuffle, t, ops, line);
        }
        case Op::Convert: {
            Type from = ty(i.args[0]);
            if (from == t) return v(i.args[0]);
            return value_op(t.is_float() ? spv::OpConvertSToF : spv::OpConvertFToS, t, args(), line);
        }
        case Op::Builtin: {
            BuiltinFn fn = static_cast<BuiltinFn>(i.imm);
            const BuiltinInfo& b = builtin_info(fn);
            if (fn == BuiltinFn::Dot) return value_op(spv::OpDot, t, args(), line);
            if (fn == BuiltinFn::Transpose) return value_op(spv::OpTranspose, t, args(), line);
            int op = b.glsl450;
            const char* nm = b.name;
            bool is_int = ty(i.args[0]).base == Type::Base::Int;
            static const char* glsl_names[] = {
                "", "Round", "RoundEven", "Trunc", "FAbs", "SAbs", "FSign", "SSign", "Floor", "Ceil", "Fract",
                "Radians", "Degrees", "Sin", "Cos", "Tan", "Asin", "Acos", "Atan", "Sinh", "Cosh", "Tanh",
                "Asinh", "Acosh", "Atanh", "Atan2", "Pow", "Exp", "Log", "Exp2", "Log2", "Sqrt",
                "InverseSqrt", "Determinant", "MatrixInverse", "Modf", "ModfStruct", "FMin", "UMin",
                "SMin", "FMax", "UMax", "SMax", "FClamp", "UClamp", "SClamp", "FMix", "IMix", "Step",
                "SmoothStep", "Fma", "Frexp", "FrexpStruct", "Ldexp", "PackSnorm4x8", "PackUnorm4x8",
                "PackSnorm2x16", "PackUnorm2x16", "PackHalf2x16", "PackDouble2x32", "UnpackSnorm2x16",
                "UnpackUnorm2x16", "UnpackHalf2x16", "UnpackSnorm4x8", "UnpackUnorm4x8",
                "UnpackDouble2x32", "Length", "Distance", "Cross", "Normalize", "FaceForward",
                "Reflect", "Refract",
            };
            if (fn == BuiltinFn::Round) op = 2;   // RoundEven: matches WGSL's round()
            if (is_int) {
                if (fn == BuiltinFn::Abs) op = glsl::SAbs;
                if (fn == BuiltinFn::Sign) op = glsl::SSign;
                if (fn == BuiltinFn::Min) op = glsl::SMin;
                if (fn == BuiltinFn::Max) op = glsl::SMax;
                if (fn == BuiltinFn::Clamp) op = glsl::SClamp;
            }
            if (op >= 0 && op < static_cast<int>(sizeof glsl_names / sizeof *glsl_names)) nm = glsl_names[op];
            return ext(op, t, i.args, line, nm);
        }
        case Op::Call: {
            std::vector<Operand> ops = {id(fn_ids_.at(i.callee))};
            for (int a : i.args) ops.push_back(id(v(a)));
            return value_op(spv::OpFunctionCall, t, ops, line);
        }
        case Op::Load:
            return value_op(spv::OpLoad, t, {id(v(i.args[0]))}, line);
        default:
            return 0;
    }
}

// ── Serialization ────────────────────────────────────────────────────────────

void encode(const SInst& in, std::vector<uint32_t>& out) {
    size_t start = out.size();
    out.push_back(0);
    if (in.type)   out.push_back(in.type);
    if (in.result) out.push_back(in.result);
    for (const auto& o : in.ops) {
        if (o.kind == Operand::Str) {
            std::vector<uint32_t> w((o.text.size() + 4) / 4, 0);
            std::memcpy(w.data(), o.text.data(), o.text.size());
            out.insert(out.end(), w.begin(), w.end());
        } else {
            out.push_back(o.value);
        }
    }
    uint32_t count = static_cast<uint32_t>(out.size() - start);
    out[start] = (count << 16) | in.opcode;
}

std::string Builder::disassemble(const std::vector<SInst>& all, std::vector<int>& line_map) const {
    std::string out;
    auto ref = [&](uint32_t id) {
        auto it = names_.find(id);
        return "%" + (it == names_.end() ? std::to_string(id) : it->second);
    };
    auto emit_line = [&](const std::string& s, int src) {
        out += s + "\n";
        line_map.push_back(src);
    };
    emit_line("; SPIR-V", 0);
    emit_line("; Version: 1.0", 0);
    emit_line("; Generator: fluxc (Flux shader compiler)", 0);
    emit_line("; Bound: " + std::to_string(next_), 0);
    emit_line("; Schema: 0", 0);
    int indent_col = 0;
    for (const auto& in : all) {
        if (in.result) indent_col = std::max<int>(indent_col, static_cast<int>(ref(in.result).size()));
    }
    indent_col = std::min(indent_col, 22);
    for (const auto& in : all) {
        std::string lhs;
        if (in.result) lhs = ref(in.result) + " = ";
        std::string pad(std::max(0, indent_col + 3 - static_cast<int>(lhs.size())), ' ');
        std::string s = pad + lhs + spv::op_name(in.opcode);
        if (in.type) s += " " + ref(in.type);
        for (const auto& o : in.ops) {
            switch (o.kind) {
                case Operand::Id:   s += " " + ref(o.value); break;
                case Operand::Lit:
                    if (in.opcode == spv::OpConstant && in.type == type_cache_.at("float")) {
                        float f;
                        std::memcpy(&f, &o.value, 4);
                        s += " " + format_float(f);
                    } else if (in.opcode == spv::OpConstant) {
                        s += " " + std::to_string(static_cast<int32_t>(o.value));
                    } else {
                        s += " " + std::to_string(o.value);
                    }
                    break;
                case Operand::Str:  s += " \"" + o.text + "\""; break;
                case Operand::Enum: s += " " + o.text; break;
            }
        }
        if (in.opcode == spv::OpFunction) emit_line("", in.src_line);
        emit_line(s, in.src_line);
    }
    return out;
}

SpirvOutput Builder::run() {
    capabilities_.push_back({spv::OpCapability, 0, 0, {en(spv::CapabilityShader, "Shader")}});
    glsl_ = fresh();
    ext_imports_.push_back({spv::OpExtInstImport, 0, glsl_, {str("GLSL.std.450")}});
    names_[glsl_] = "glsl";
    used_names_.insert("glsl");
    memory_model_.push_back({spv::OpMemoryModel, 0, 0, {en(spv::AddressingLogical, "Logical"),
                                                         en(spv::MemoryGLSL450, "GLSL450")}});
    file_ = fresh();
    debug_strings_.push_back({spv::OpString, 0, file_, {str(source_name_)}});
    names_[file_] = "file";
    used_names_.insert("file");
    debug_strings_.push_back({spv::OpSource, 0, 0, {en(0, "Unknown"), lit(100), id(file_)}});

    // Function ids are needed before bodies reference each other: name them first.
    t_float();
    uniforms();
    // Emit callees before callers is not required by SPIR-V, but function ids
    // must exist when a call is emitted. Pre-assign by emitting in reverse
    // dependency order: helpers first (lowering keeps declaration order, and
    // Flux has no recursion, so a simple topological order always exists).
    std::vector<const Function*> order;
    std::unordered_set<std::string> done;
    std::function<void(const Function&)> visit = [&](const Function& f) {
        if (!done.insert(f.name).second) return;
        for_each_inst(f.body, [&](const Inst& i) {
            if (i.op == Op::Call)
                if (const Function* g = m_.find(i.callee)) visit(*g);
        });
        order.push_back(&f);
    };
    for (const auto& f : m_.functions) visit(f);
    for (const Function* f : order) function(*f);

    std::vector<SInst> all;
    for (auto* sec : {&capabilities_, &ext_imports_, &memory_model_, &entry_points_, &exec_modes_,
                      &debug_strings_, &debug_names_, &annotations_, &globals_, &functions_})
        all.insert(all.end(), sec->begin(), sec->end());

    SpirvOutput out;
    out.words = {0x07230203u, 0x00010000u, 0u, next_, 0u};
    for (const auto& in : all) encode(in, out.words);
    out.disassembly = disassemble(all, out.line_map);
    return out;
}

} // namespace

SpirvOutput emit_spirv(const ir::Module& m, const std::string& source_name) {
    return Builder(m, source_name).run();
}

} // namespace backend
