cmake_minimum_required(VERSION 3.16)
project(flux_wasm CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source root is one level up from this CMakeLists.
set(FLUX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
include(${FLUX_ROOT}/compiler/sources.cmake)

add_executable(flux_wasm ${FLUX_COMPILER_SOURCES} ${FLUX_ROOT}/compiler/wasm_bindings.cpp)
target_include_directories(flux_wasm PRIVATE ${FLUX_ROOT}/compiler)

# Emscripten output: flux_wasm.js + flux_wasm.wasm
set_target_properties(flux_wasm PROPERTIES OUTPUT_NAME "flux_wasm" SUFFIX ".js")
# The lexer and parser report syntax errors by throwing, so exceptions must work.
target_compile_options(flux_wasm PRIVATE -O2 -fexceptions)
target_link_options(flux_wasm PRIVATE
    -lembind
    -fexceptions
    -sMODULARIZE=1
    -sEXPORT_NAME=FluxModule
    -sALLOW_MEMORY_GROWTH=1
    -sNO_EXIT_RUNTIME=1
    -sENVIRONMENT=web,node
    -O2
)
