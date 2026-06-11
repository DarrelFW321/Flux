#include <catch2/catch_test_macros.hpp>
#include "test_helpers.hpp"

TEST_CASE("Parser parses a simple function declaration", "[parser]") {
    auto prog = parse_source("fn add(a: int, b: int) -> int { return a + b; }");
    REQUIRE(prog.items.size() == 1);

    auto* fn = std::get_if<FnDecl>(&prog.items[0]);
    REQUIRE(fn != nullptr);
    CHECK(fn->name == "add");
    REQUIRE(fn->params.size() == 2);
    CHECK(fn->params[0].name == "a");
    CHECK(fn->params[0].type_name == "int");
    CHECK(fn->params[1].name == "b");
    CHECK(fn->return_type == "int");

    REQUIRE(fn->body->stmts.size() == 1);
    auto* ret = std::get_if<ReturnStmt>(&fn->body->stmts[0]->data);
    REQUIRE(ret != nullptr);
    auto* bin = std::get_if<BinaryExpr>(&ret->value->data);
    REQUIRE(bin != nullptr);
    CHECK(bin->op == "+");
}

TEST_CASE("Parser respects arithmetic operator precedence", "[parser]") {
    auto prog = parse_source("let x: int = 1 + 2 * 3;");
    auto* let = std::get_if<std::unique_ptr<Stmt>>(&prog.items[0]);
    REQUIRE(let != nullptr);
    auto* ls = std::get_if<LetStmt>(&(*let)->data);
    REQUIRE(ls != nullptr);

    // '+' binds last (lowest precedence), so it sits at the root.
    auto* add = std::get_if<BinaryExpr>(&ls->init->data);
    REQUIRE(add != nullptr);
    CHECK(add->op == "+");

    auto* lhs = std::get_if<IntLitExpr>(&add->left->data);
    REQUIRE(lhs != nullptr);
    CHECK(lhs->value == 1);

    // '2 * 3' should be grouped on the right.
    auto* mul = std::get_if<BinaryExpr>(&add->right->data);
    REQUIRE(mul != nullptr);
    CHECK(mul->op == "*");
}

TEST_CASE("Parser binds unary minus tighter than binary operators", "[parser]") {
    auto prog = parse_source("let x: int = -1 + 2;");
    auto* let = std::get_if<std::unique_ptr<Stmt>>(&prog.items[0]);
    auto* ls  = std::get_if<LetStmt>(&(*let)->data);

    auto* add = std::get_if<BinaryExpr>(&ls->init->data);
    REQUIRE(add != nullptr);
    CHECK(add->op == "+");

    auto* neg = std::get_if<UnaryExpr>(&add->left->data);
    REQUIRE(neg != nullptr);
    CHECK(neg->op == "-");
}

TEST_CASE("Parser parses let, while, and if/else statements", "[parser]") {
    auto prog = parse_source(
        "let x: int = 0;"
        "while x < 10 {"
        "  if x == 5 {"
        "    x = x + 1;"
        "  } else {"
        "    x = x + 2;"
        "  }"
        "}"
    );
    REQUIRE(prog.items.size() == 2);

    auto* let = std::get_if<std::unique_ptr<Stmt>>(&prog.items[0]);
    REQUIRE(let != nullptr);
    CHECK(std::holds_alternative<LetStmt>((*let)->data));

    auto* whileItem = std::get_if<std::unique_ptr<Stmt>>(&prog.items[1]);
    REQUIRE(whileItem != nullptr);
    auto* ws = std::get_if<WhileStmt>(&(*whileItem)->data);
    REQUIRE(ws != nullptr);

    REQUIRE(ws->body->stmts.size() == 1);
    auto* ifs = std::get_if<IfStmt>(&ws->body->stmts[0]->data);
    REQUIRE(ifs != nullptr);
    CHECK(ifs->else_block.has_value());
}

TEST_CASE("Parser parses array literals and indexing", "[parser]") {
    auto prog = parse_source(
        "let a: float[3] = [1.0, 2.0, 3.0];"
        "let b: float = a[1];"
    );
    REQUIRE(prog.items.size() == 2);

    auto* letA = std::get_if<std::unique_ptr<Stmt>>(&prog.items[0]);
    auto* lsA  = std::get_if<LetStmt>(&(*letA)->data);
    auto* arr  = std::get_if<ArrayLitExpr>(&lsA->init->data);
    REQUIRE(arr != nullptr);
    CHECK(arr->elements.size() == 3);

    auto* letB = std::get_if<std::unique_ptr<Stmt>>(&prog.items[1]);
    auto* lsB  = std::get_if<LetStmt>(&(*letB)->data);
    auto* idx  = std::get_if<IndexExpr>(&lsB->init->data);
    REQUIRE(idx != nullptr);
}

TEST_CASE("Parser throws on malformed input", "[parser]") {
    CHECK_THROWS_AS(parse_source("let x: int = 1"), std::runtime_error);                   // missing ';'
    CHECK_THROWS_AS(parse_source("fn f(a: int -> int { return a; }"), std::runtime_error); // missing ')'
    CHECK_THROWS_AS(parse_source("let x: int = ;"), std::runtime_error);                   // missing expression
    CHECK_THROWS_AS(parse_source("let x: int[4] = [];"), std::runtime_error);              // empty array literal
}
