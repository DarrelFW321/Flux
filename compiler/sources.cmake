# Compiler sources shared by the CLI, the unit tests and the WebAssembly build.
# FLUX_ROOT must point at the repository root.
set(FLUX_COMPILER_SOURCES
    ${FLUX_ROOT}/compiler/frontend/lexer.cpp
    ${FLUX_ROOT}/compiler/frontend/parser.cpp
    ${FLUX_ROOT}/compiler/frontend/typechecker.cpp
    ${FLUX_ROOT}/compiler/frontend/ast_json.cpp
    ${FLUX_ROOT}/compiler/ir/ir.cpp
    ${FLUX_ROOT}/compiler/ir/lower.cpp
    ${FLUX_ROOT}/compiler/ir/passes.cpp
    ${FLUX_ROOT}/compiler/backend/wgsl.cpp
    ${FLUX_ROOT}/compiler/backend/spirv.cpp
    ${FLUX_ROOT}/compiler/driver.cpp
)
