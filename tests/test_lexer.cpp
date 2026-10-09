#include <catch2/catch_test_macros.hpp>
#include "test_helpers.hpp"

static std::vector<TokenType> types(const std::string& src) {
    std::vector<TokenType> out;
    for (const auto& t : Lexer(src).tokenize()) out.push_back(t.type);
    return out;
}

TEST_CASE("Lexer distinguishes keywords, types and identifiers", "[lexer]") {
    auto t = types("fn main uniform vec3 mat4 let var x");
    REQUIRE(t == std::vector<TokenType>{TokenType::KW_FN, TokenType::IDENTIFIER, TokenType::KW_UNIFORM,
                                        TokenType::TYPE, TokenType::TYPE, TokenType::KW_LET,
                                        TokenType::KW_VAR, TokenType::IDENTIFIER, TokenType::EOF_TOK});
}

TEST_CASE("Lexer scans float literal forms", "[lexer]") {
    auto toks = Lexer("1.5 .5 2. 1e3 4e-2 7").tokenize();
    CHECK(toks[0].type == TokenType::FLOAT_LIT);
    CHECK(toks[1].type == TokenType::FLOAT_LIT);
    CHECK(toks[2].type == TokenType::FLOAT_LIT);
    CHECK(toks[3].type == TokenType::FLOAT_LIT);
    CHECK(toks[4].type == TokenType::FLOAT_LIT);
    CHECK(toks[5].type == TokenType::INT_LIT);
}

TEST_CASE("Lexer keeps ranges apart from float literals", "[lexer]") {
    auto t = types("0..10");
    REQUIRE(t == std::vector<TokenType>{TokenType::INT_LIT, TokenType::DOT_DOT, TokenType::INT_LIT,
                                        TokenType::EOF_TOK});
}

TEST_CASE("Lexer scans compound operators and attributes", "[lexer]") {
    auto t = types("a += b -> @x && !c || d != e");
    REQUIRE(t == std::vector<TokenType>{TokenType::IDENTIFIER, TokenType::PLUS_EQ, TokenType::IDENTIFIER,
                                        TokenType::ARROW, TokenType::AT, TokenType::IDENTIFIER,
                                        TokenType::AMP_AMP, TokenType::BANG, TokenType::IDENTIFIER,
                                        TokenType::PIPE_PIPE, TokenType::IDENTIFIER, TokenType::BANG_EQ,
                                        TokenType::IDENTIFIER, TokenType::EOF_TOK});
}

TEST_CASE("Lexer tracks lines and skips comments", "[lexer]") {
    auto toks = Lexer("// comment\n/* block\n comment */ x").tokenize();
    REQUIRE(toks[0].type == TokenType::IDENTIFIER);
    CHECK(toks[0].line == 3);
    CHECK(toks[0].col == 13);
}

TEST_CASE("Lexer reports bad characters with a location", "[lexer]") {
    try {
        Lexer("let x = 1 $ 2;").tokenize();
        FAIL("expected an error");
    } catch (const CompileError& e) {
        CHECK(e.loc.line == 1);
        CHECK(e.loc.col == 11);
    }
}
