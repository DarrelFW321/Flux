#pragma once
#include "midend/ir.hpp"
#include "frontend/ast.hpp"

namespace mir {

// Lower a type-checked AST `Program` into a FluxIR `Module`.
//
// Assumes the type checker has already validated `prog`: array bounds,
// scalar/array compatibility, return types, etc. The lowerer does no
// diagnosis of its own.
Module lower_program(const Program& prog);

}  // namespace mir
