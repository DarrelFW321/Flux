#pragma once
#include "backend/spirv.hpp"
#include "backend/wgsl.hpp"
#include "common/diagnostics.hpp"
#include "frontend/lexer.hpp"
#include "frontend/typechecker.hpp"
#include "ir/ir.hpp"
#include "ir/passes.hpp"
#include <string>
#include <vector>

struct CompileOptions {
    bool optimize = true;
    std::vector<std::string> disabled_passes;
    bool record_pass_steps = true;   // print IR after each pass (playground)
    std::string source_name = "shader.flux";
};

struct StageTiming {
    std::string stage;
    double      ms = 0;
};

// Everything the pipeline produced, stage by stage. Later stages are empty if
// an earlier one failed.
struct CompileResult {
    bool                      ok = false;
    std::vector<Token>        tokens;
    std::string               ast_json;     // empty if parsing failed
    Diagnostics               diags;
    Reflection                reflection;
    ir::Printed               ir_raw, ir_opt;
    ir::PipelineReport        passes;
    backend::WgslOutput       wgsl;
    backend::SpirvOutput      spirv;
    std::vector<StageTiming>  timings;
};

CompileResult compile(const std::string& source, const CompileOptions& opts = {});

// The playground's view of a CompileResult.
std::string result_to_json(const CompileResult& r);
