#pragma once
#include "ir/ir.hpp"
#include <string>
#include <vector>

namespace backend {

struct WgslOutput {
    std::string      code;
    std::vector<int> line_map;   // WGSL line (0-based) → Flux source line (0 = none)
};

// Emits a WGSL module. The uniform block becomes `struct Uniforms` bound at
// @group(0) @binding(0) as `u`; field offsets match the reflection layout.
WgslOutput emit_wgsl(const ir::Module& m);

} // namespace backend
