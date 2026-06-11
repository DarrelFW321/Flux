# ── Catch2 (fetched at configure time) ───────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# ── Unit tests ────────────────────────────────────────────────────────────────
# Only the lexer/parser/typechecker are exercised here — they have no LLVM
# dependency, so the test binary builds quickly and stays portable.
add_executable(flux_tests
    test_lexer.cpp
    test_parser.cpp
    test_typechecker.cpp
    ${CMAKE_SOURCE_DIR}/compiler/frontend/lexer.cpp
    ${CMAKE_SOURCE_DIR}/compiler/frontend/parser.cpp
    ${CMAKE_SOURCE_DIR}/compiler/frontend/typechecker.cpp
)

target_include_directories(flux_tests PRIVATE ${CMAKE_SOURCE_DIR}/compiler)
target_link_libraries(flux_tests PRIVATE Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(flux_tests)
