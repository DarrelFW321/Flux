#pragma once
#include "frontend/ast.hpp"
#include "frontend/typechecker.hpp"
#include "ir/ir.hpp"

namespace ir {

// Lowers a type-checked program to FluxIR. Requires a program with no
// type errors. Implicit operations become explicit here: scalar → vector
// broadcasts turn into `splat`, `mod` expands to x - y*floor(x/y), swizzle
// assignment becomes load/shuffle/store, and `for` loops become `loop`s
// over a counter variable.
Module lower_program(const Program& prog, const Reflection& refl);

} // namespace ir
