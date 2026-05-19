cmake_minimum_required(VERSION 3.16)
project(flux_wasm CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source root is one level up from this CMakeLists.
set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_executable(flux_wasm
    ${SRC}/compiler/frontend/lexer.cpp
    ${SRC}/compiler/frontend/parser.cpp
    ${SRC}/compiler/frontend/typechecker.cpp
    ${SRC}/compiler/frontend/dumper.cpp
    ${SRC}/compiler/frontend/wasm_bindings.cpp
    ${SRC}/compiler/midend/ir.cpp
    ${SRC}/compiler/midend/lower_from_ast.cpp
    ${SRC}/compiler/midend/passes.cpp
)

target_include_directories(flux_wasm PRIVATE ${SRC}/compiler)

# Emscripten output: flux_wasm.js + flux_wasm.wasm
set_target_properties(flux_wasm PROPERTIES
    OUTPUT_NAME "flux_wasm"
    SUFFIX ".js"
)

target_compile_options(flux_wasm PRIVATE -O2)

target_link_options(flux_wasm PRIVATE
    -lembind
    -sMODULARIZE=1
    -sEXPORT_NAME=FluxModule
    -sALLOW_MEMORY_GROWTH=1
    -sNO_EXIT_RUNTIME=1
    -sENVIRONMENT=web
    -O2
)
