#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include "frontend/lexer.hpp"

TEST_CASE("Lexer tokenizes integer and float literals", "[lexer]") {
    auto tokens = Lexer("42 3.14").tokenize();
    REQUIRE(tokens.size() == 3);
    CHECK(tokens[0].type == TokenType::INT_LIT);
    CHECK(tokens[0].lexeme == "42");
    CHECK(tokens[1].type == TokenType::FLOAT_LIT);
    CHECK(tokens[1].lexeme == "3.14");
    CHECK(tokens[2].type == TokenType::EOF_TOK);
}

TEST_CASE("Lexer recognizes keywords vs identifiers", "[lexer]") {
    auto tokens = Lexer("fn let return if else while print true false foo").tokenize();
    CHECK(tokens[0].type == TokenType::KW_FN);
    CHECK(tokens[1].type == TokenType::KW_LET);
    CHECK(tokens[2].type == TokenType::KW_RETURN);
    CHECK(tokens[3].type == TokenType::KW_IF);
    CHECK(tokens[4].type == TokenType::KW_ELSE);
    CHECK(tokens[5].type == TokenType::KW_WHILE);
    CHECK(tokens[6].type == TokenType::KW_PRINT);
    CHECK(tokens[7].type == TokenType::KW_TRUE);
    CHECK(tokens[8].type == TokenType::KW_FALSE);
    CHECK(tokens[9].type == TokenType::IDENTIFIER);
    CHECK(tokens[9].lexeme == "foo");
}

TEST_CASE("Lexer recognizes multi-character operators", "[lexer]") {
    auto tokens = Lexer("== != <= >= -> = < >").tokenize();
    CHECK(tokens[0].type == TokenType::EQ_EQ);
    CHECK(tokens[1].type == TokenType::BANG_EQ);
    CHECK(tokens[2].type == TokenType::LT_EQ);
    CHECK(tokens[3].type == TokenType::GT_EQ);
    CHECK(tokens[4].type == TokenType::ARROW);
    CHECK(tokens[5].type == TokenType::EQ);
    CHECK(tokens[6].type == TokenType::LT);
    CHECK(tokens[7].type == TokenType::GT);
}

TEST_CASE("Lexer skips line comments and tracks line numbers", "[lexer]") {
    auto tokens = Lexer("let x = 1; // comment\nlet y = 2;").tokenize();
    CHECK(tokens[0].type == TokenType::KW_LET);
    CHECK(tokens[0].line == 1);

    auto it = std::find_if(tokens.begin(), tokens.end(), [](const Token& t) {
        return t.type == TokenType::IDENTIFIER && t.lexeme == "y";
    });
    REQUIRE(it != tokens.end());
    CHECK(it->line == 2);
}

TEST_CASE("Lexer throws on an unrecognized character", "[lexer]") {
    REQUIRE_THROWS_AS(Lexer("let x = 1 & 2;").tokenize(), std::runtime_error);
}
