#pragma once
#include "ir/ir.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace backend {

struct SpirvOutput {
    std::vector<uint32_t> words;        // the binary module
    std::string           disassembly;  // spirv-dis style text
    std::vector<int>      line_map;     // disassembly line (0-based) → Flux source line
};

// Emits a SPIR-V 1.0 module for Vulkan: Shader capability, GLSL.std.450 math,
// Logical/GLSL450 memory model. Uniforms live in a std140 `Block` at
// set 0, binding 0. Includes OpString/OpLine debug info for `source_name`.
SpirvOutput emit_spirv(const ir::Module& m, const std::string& source_name = "shader.flux");

} // namespace backend
