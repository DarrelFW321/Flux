#include <catch2/catch_test_macros.hpp>
#include "test_helpers.hpp"
#include "frontend/typechecker.hpp"

static void typecheck(const std::string& src) {
    auto prog = parse_source(src);
    TypeChecker tc;
    tc.check(prog);
}

TEST_CASE("Typechecker accepts a well-typed program with functions and arrays", "[typechecker]") {
    CHECK_NOTHROW(typecheck(
        "fn scale(a: float[4], k: float) -> float[4] {"
        "  return a * k;"
        "}"
        "let x: float[4] = [1.0, 2.0, 3.0, 4.0];"
        "let y: float[4] = scale(x, 2.0);"
        "print(sum(y));"
        "print(dot(x, y));"
    ));
}

TEST_CASE("Typechecker rejects a let binding with mismatched types", "[typechecker]") {
    CHECK_THROWS_AS(typecheck("let x: int = 1.0;"), std::runtime_error);
}

TEST_CASE("Typechecker rejects use of an undefined variable", "[typechecker]") {
    CHECK_THROWS_AS(typecheck("let x: int = y + 1;"), std::runtime_error);
}

TEST_CASE("Typechecker validates sum() and dot() builtin argument types", "[typechecker]") {
    CHECK_NOTHROW(typecheck(
        "let a: float[4] = [1.0, 2.0, 3.0, 4.0];"
        "print(sum(a));"
        "print(dot(a, a));"
    ));

    // sum() requires an array, not a scalar.
    CHECK_THROWS_AS(typecheck("let a: int = 1; print(sum(a));"), std::runtime_error);

    // dot() requires both arguments to share the same type.
    CHECK_THROWS_AS(typecheck(
        "let a: float[4] = [1.0, 2.0, 3.0, 4.0];"
        "let b: int[4] = [1, 2, 3, 4];"
        "print(dot(a, b));"
    ), std::runtime_error);
}

TEST_CASE("Typechecker rejects calls with the wrong argument count", "[typechecker]") {
    CHECK_THROWS_AS(typecheck(
        "fn f(a: int) -> int { return a; }"
        "let x: int = f(1, 2);"
    ), std::runtime_error);
}

TEST_CASE("Typechecker requires bool conditions for if/while", "[typechecker]") {
    CHECK_THROWS_AS(typecheck(
        "fn main() -> int {"
        "  if 1 { return 1; }"
        "  return 0;"
        "}"
    ), std::runtime_error);

    CHECK_THROWS_AS(typecheck(
        "fn main() -> int {"
        "  while 1 { return 1; }"
        "  return 0;"
        "}"
    ), std::runtime_error);
}
