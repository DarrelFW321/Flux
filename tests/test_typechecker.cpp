#include <catch2/catch_test_macros.hpp>
#include "test_helpers.hpp"

TEST_CASE("Well-typed shaders produce no errors", "[sema]") {
    auto d = check_source(frag(
        "let uv = p.xy / 2.0; let c = vec3(uv, 1); let m = mat2(1.0); let q = m * uv;"
        "return vec4(c * q.x, 1);"));
    CHECK_FALSE(d.has_errors());
}

TEST_CASE("Int literals coerce to float but int variables do not", "[sema]") {
    CHECK_FALSE(check_source(frag("let x = 2.0 * 3; return vec4(x);")).has_errors());
    auto d = check_source(frag("let i = 3; let x = 2.0 * i; return vec4(x);"));
    CHECK(has_message(d, "mixes 'int' and 'float'"));
}

TEST_CASE("Assignments require mutable variables", "[sema]") {
    auto d = check_source(frag("let x = 1.0; x = 2.0; return vec4(x);"));
    CHECK(has_message(d, "it is a 'let' binding"));
    auto u = check_source(frag("time = 1.0; return vec4(time);", "uniform time: float;"));
    CHECK(has_message(u, "it is a uniform"));
}

TEST_CASE("Swizzles are validated", "[sema]") {
    CHECK(has_message(check_source(frag("let v = vec2(1.0); return vec4(v.xyz, 1.0);")), "past the end"));
    CHECK(has_message(check_source(frag("let v = vec4(1.0); return vec4(v.xg, 1.0, 1.0);")), "invalid swizzle"));
    CHECK(has_message(check_source(frag("var v = vec4(1.0); v.xx = vec2(0.0); return v;")), "repeated"));
}

TEST_CASE("Every path of a non-void function must return", "[sema]") {
    auto d = check_source("fn f(x: float) -> float { if x > 0.0 { return 1.0; } }\n" +
                          frag("return vec4(f(1.0));"));
    CHECK(has_message(d, "must return"));
    auto ok = check_source("fn f(x: float) -> float { if x > 0.0 { return 1.0; } else { return 2.0; } }\n" +
                           frag("return vec4(f(1.0));"));
    CHECK_FALSE(ok.has_errors());
}

TEST_CASE("Recursion is rejected", "[sema]") {
    auto d = check_source("fn f(x: float) -> float { return g(x); }\nfn g(x: float) -> float { return f(x); }\n" +
                          frag("return vec4(f(1.0));"));
    CHECK(has_message(d, "recursive"));
}

TEST_CASE("Entry-point interfaces are checked", "[sema]") {
    CHECK(has_message(check_source("@fragment fn main(p: vec4) -> vec4 { return p; }"), "@builtin"));
    CHECK(has_message(check_source("@fragment fn main(@builtin(position) p: vec4) -> vec3 { return p.xyz; }"),
                      "must return vec4"));
    CHECK(has_message(check_source("@vertex fn vs(@builtin(position) p: vec4) -> vec4 { return p; }"),
                      "not a vertex input builtin"));
    CHECK(has_message(check_source("fn helper() -> float { return 1.0; }"), "no entry point"));
}

TEST_CASE("discard is fragment-only and loops own break", "[sema]") {
    CHECK(has_message(check_source("@vertex fn vs(@builtin(vertex_index) i: int) -> vec4 { discard; }"),
                      "only allowed in a @fragment"));
    CHECK(has_message(check_source(frag("break; return vec4(1.0);")), "outside of a loop"));
}

TEST_CASE("The checker reports several errors at once", "[sema]") {
    auto d = check_source(frag("let a = nope; let b = vec3(1.0) == vec3(2.0); return vec4(oops);"));
    CHECK(d.error_count() >= 3);
}

TEST_CASE("Warnings flag unused and never-mutated variables", "[sema]") {
    auto d = check_source(frag("let unused = 1.0; var k = 2.0; return vec4(k);"));
    CHECK_FALSE(d.has_errors());
    CHECK(has_message(d, "unused variable 'unused'", Diagnostic::Severity::Warning));
    CHECK(has_message(d, "never reassigned", Diagnostic::Severity::Warning));
}

TEST_CASE("Uniform reflection follows WGSL/std140 layout", "[sema]") {
    Diagnostics d;
    Program p = parse_source(
        "uniform a: float;\nuniform b: vec3;\nuniform c: float;\nuniform d: vec2;\nuniform m: mat4;\n"
        "@range(0, 10) uniform s: float = 2.5;\n@color uniform col: vec3 = vec3(1, 0.5, 0);\n" +
        frag("return vec4(a + b.x + c + d.x + m[0].x + s + col.x);"));
    TypeChecker tc(d);
    tc.check(p);
    REQUIRE_FALSE(d.has_errors());
    const auto& u = tc.reflection().uniforms;
    CHECK(u[0].offset == 0);    // float
    CHECK(u[1].offset == 16);   // vec3 aligns to 16
    CHECK(u[2].offset == 28);   // float packs after vec3
    CHECK(u[3].offset == 32);   // vec2 aligns to 8
    CHECK(u[4].offset == 48);   // mat4 aligns to 16
    CHECK(u[5].offset == 112);
    CHECK(u[5].has_range);
    CHECK(u[5].default_value == std::vector<double>{2.5});
    CHECK(u[6].color);
    CHECK(u[6].default_value == std::vector<double>{1.0, 0.5, 0.0});
    CHECK(tc.reflection().uniform_block_size == 144);
}
