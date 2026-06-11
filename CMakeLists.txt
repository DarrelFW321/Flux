cmake_minimum_required(VERSION 3.16)
project(flux CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── LLVM dependencies (must be found before LLVM so imported targets exist) ───
find_package(ZLIB)
find_package(zstd QUIET)

# ── LLVM ──────────────────────────────────────────────────────────────────────
set(LLVM_DIR "C:/msys2/mingw64/lib/cmake/llvm" CACHE PATH "LLVM CMake config dir")
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

# ── Sources ───────────────────────────────────────────────────────────────────
add_executable(flux
    compiler/main.cpp
    compiler/frontend/lexer.cpp
    compiler/frontend/parser.cpp
    compiler/frontend/typechecker.cpp
    compiler/frontend/dumper.cpp
    compiler/midend/ir.cpp
    compiler/midend/lower_from_ast.cpp
    compiler/midend/passes.cpp
    compiler/backend/codegen.cpp
    compiler/backend/native.cpp
)

target_include_directories(flux PRIVATE compiler)
# SYSTEM suppresses warnings from LLVM headers (they have many -Wunused-parameter hits).
target_include_directories(flux SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})

target_compile_definitions(flux PRIVATE ${LLVM_DEFINITIONS})

# ── Compiler flags ────────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(flux PRIVATE /W4)
else()
    target_compile_options(flux PRIVATE -Wall -Wextra)
    target_link_options(flux PRIVATE -static-libgcc)
endif()

# ── LLVM linking — core IR + native code generation ──────────────────────────
llvm_map_components_to_libnames(llvm_libs
    core support irreader
    codegen nativecodegen native
    passes
)
target_link_libraries(flux PRIVATE ${llvm_libs})

# ── Copy runtime DLLs next to the binary (Windows) ───────────────────────────
if(WIN32)
    set(MSYS2_BIN "C:/msys2/mingw64/bin")
    foreach(dll libstdc++-6.dll libwinpthread-1.dll libgcc_s_seh-1.dll)
        add_custom_command(TARGET flux POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MSYS2_BIN}/${dll}" $<TARGET_FILE_DIR:flux>
            COMMENT "Copying ${dll}"
        )
    endforeach()
endif()

# ── Unit tests ────────────────────────────────────────────────────────────────
option(FLUX_BUILD_TESTS "Build the Catch2 unit test suite" OFF)
if(FLUX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
