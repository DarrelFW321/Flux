cmake_minimum_required(VERSION 3.16)
project(flux CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FLUX_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
include(compiler/sources.cmake)

# The compiler core has no dependencies beyond the C++ standard library.
add_library(flux_core STATIC ${FLUX_COMPILER_SOURCES})
target_include_directories(flux_core PUBLIC compiler)

if(MSVC)
    target_compile_options(flux_core PRIVATE /W4)
else()
    target_compile_options(flux_core PRIVATE -Wall -Wextra)
endif()

add_executable(fluxc compiler/main.cpp)
target_link_libraries(fluxc PRIVATE flux_core)
if(MINGW)
    target_link_options(fluxc PRIVATE -static)
endif()

# ── Unit tests ────────────────────────────────────────────────────────────────
option(FLUX_BUILD_TESTS "Build the Catch2 unit test suite" OFF)
if(FLUX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
