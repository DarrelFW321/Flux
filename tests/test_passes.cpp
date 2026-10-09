#include <catch2/catch_test_macros.hpp>
#include "test_helpers.hpp"

static CompileResult build(const std::string& src, std::vector<std::string> disabled = {}) {
    CompileOptions o;
    o.disabled_passes = std::move(disabled);
    CompileResult r = compile(src, o);
    REQUIRE(r.ok);
    return r;
}

static int total(const CompileResult& r, const std::string& pass) {
    for (const auto& t : r.passes.totals) if (t.name == pass) return t.changes;
    return -1;
}

TEST_CASE("const-fold evaluates arithmetic and builtins", "[passes]") {
    auto r = build(frag("let x = sqrt(16.0) * 2.0 + 1.0; return vec4(x);"));
    CHECK(r.ir_opt.text.find("const 9.0") != std::string::npos);
    CHECK(r.ir_opt.text.find("sqrt") == std::string::npos);
}

TEST_CASE("simplify removes algebraic identities", "[passes]") {
    auto r = build(frag("let x = p.x * 1.0 + 0.0; let y = pow(p.y, 2.0); return vec4(x, y, 0, 1);"));
    CHECK(r.ir_opt.text.find("pow") == std::string::npos);
    CHECK(count(r.ir_opt.text, "= add") == 0);
}

TEST_CASE("inline removes calls to single-exit helpers", "[passes]") {
    auto r = build("fn sq(x: float) -> float { return x * x; }\n" + frag("return vec4(sq(p.x));"));
    CHECK(r.ir_opt.text.find("call") == std::string::npos);
    CHECK(r.ir_opt.text.find("fn @sq") == std::string::npos);   // dead function removed
    CHECK(total(r, "inline") == 1);
}

TEST_CASE("functions with early returns are kept as calls", "[passes]") {
    auto r = build("fn f(x: float) -> float { if x > 0.0 { return 1.0; } return 2.0; }\n" +
                   frag("return vec4(f(p.x));"));
    CHECK(r.ir_opt.text.find("call @f") != std::string::npos);
    CHECK(r.wgsl.code.find("fn f(") != std::string::npos);
}

TEST_CASE("unroll flattens constant-trip loops", "[passes]") {
    auto r = build(frag("var s = 0.0; for i in 0..4 { s += float(i); } return vec4(s);"));
    CHECK(r.ir_opt.text.find("loop") == std::string::npos);
    CHECK(r.ir_opt.text.find("const 6.0") != std::string::npos);   // 0+1+2+3 folded
}

TEST_CASE("loops with break are not unrolled", "[passes]") {
    auto r = build(frag("var s = 0.0; for i in 0..4 { if p.x > 1.0 { break; } s += 1.0; } return vec4(s);"));
    CHECK(r.ir_opt.text.find("loop") != std::string::npos);
}

TEST_CASE("cse merges repeated pure computations", "[passes]") {
    auto r = build(frag("return vec4(sin(p.x) + sin(p.x));"));
    CHECK(count(r.ir_opt.text, "= sin ") == 1);
}

TEST_CASE("forward + branch-fold remove constant branches through variables", "[passes]") {
    auto r = build(frag("var debug = false; if debug { return vec4(1.0); } return vec4(p.x);"));
    CHECK(r.ir_opt.text.find("if ") == std::string::npos);
    CHECK(r.ir_opt.text.find("var") == std::string::npos);
}

TEST_CASE("licm hoists invariant work out of loops", "[passes]") {
    auto r = build(frag("var s = 0.0; var i = 0; while i < 100 { s += sin(p.x); i += 1; } return vec4(s);"));
    auto loop = r.ir_opt.text.find("loop");
    auto sin_pos = r.ir_opt.text.find("= sin ");
    REQUIRE(loop != std::string::npos);
    CHECK(sin_pos < loop);
}

TEST_CASE("disabled passes do not run", "[passes]") {
    auto r = build(frag("var s = 0.0; for i in 0..4 { s += 1.0; } return vec4(s);"), {"unroll"});
    CHECK(r.ir_opt.text.find("loop") != std::string::npos);
    CHECK(total(r, "unroll") == 0);
}

TEST_CASE("the pipeline reaches a fixed point", "[passes]") {
    auto r = build(frag("let a = 1.0; return vec4(a);"));
    int last = r.passes.iterations;
    int changes = 0;
    for (const auto& s : r.passes.steps) if (s.iteration == last) changes += s.changes;
    CHECK(changes == 0);
}
