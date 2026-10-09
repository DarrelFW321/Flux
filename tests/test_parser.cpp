#include <catch2/catch_test_macros.hpp>
#include "test_helpers.hpp"

TEST_CASE("Parser reads uniforms, consts and entry points", "[parser]") {
    auto p = parse_source(
        "@range(0, 2) uniform speed: float = 1.0;\n"
        "const K = 3;\n"
        "@fragment fn main(@builtin(position) frag: vec4) -> vec4 { return frag; }\n");
    REQUIRE(p.uniforms.size() == 1);
    CHECK(p.uniforms[0]->name == "speed");
    CHECK(p.uniforms[0]->attrs[0].name == "range");
    CHECK(p.uniforms[0]->attrs[0].args == std::vector<std::string>{"0", "2"});
    REQUIRE(p.consts.size() == 1);
    REQUIRE(p.functions.size() == 1);
    const auto& fn = *p.functions[0];
    CHECK(fn.attrs[0].name == "fragment");
    CHECK(fn.params[0].attrs[0].name == "builtin");
    CHECK(fn.return_type == Type::vec(4));
}

TEST_CASE("Parser respects operator precedence", "[parser]") {
    auto p = parse_source("const X = 1 + 2 * 3 < 4 && true;");
    const Expr& e = *p.consts[0]->value;
    REQUIRE(e.kind == Expr::Kind::Binary);
    CHECK(e.op == "&&");
    const Expr& cmp = *e.children[0];
    CHECK(cmp.op == "<");
    const Expr& add = *cmp.children[0];
    CHECK(add.op == "+");
    CHECK(add.children[1]->op == "*");
}

TEST_CASE("Parser handles swizzles, constructors and ternaries", "[parser]") {
    auto p = parse_source("const X = vec3(1.0).xy.x > 0.0 ? 1.0 : 2.0;");
    const Expr& t = *p.consts[0]->value;
    REQUIRE(t.kind == Expr::Kind::Ternary);
    const Expr& sw = *t.children[0]->children[0];
    CHECK(sw.kind == Expr::Kind::Swizzle);
    CHECK(sw.name == "x");
    CHECK(sw.children[0]->kind == Expr::Kind::Swizzle);
    CHECK(sw.children[0]->children[0]->kind == Expr::Kind::Construct);
}

TEST_CASE("Parser reads for loops and compound assignment", "[parser]") {
    auto p = parse_source(frag("var s = 0.0; for i in 0..4 { s += 1.0; } return vec4(s);"));
    const auto& stmts = p.functions[0]->body->stmts;
    REQUIRE(stmts.size() == 3);
    CHECK(stmts[1]->kind == Stmt::Kind::For);
    CHECK(stmts[1]->name == "i");
    CHECK(stmts[1]->body->stmts[0]->kind == Stmt::Kind::Assign);
    CHECK(stmts[1]->body->stmts[0]->op == "+=");
}

TEST_CASE("Parser reports the location of syntax errors", "[parser]") {
    try {
        parse_source("fn f() -> float {\n  return 1.0\n}");
        FAIL("expected an error");
    } catch (const CompileError& e) {
        CHECK(e.loc.line == 3);
        CHECK(std::string(e.what()).find("';'") != std::string::npos);
    }
}
