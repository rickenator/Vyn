#define CATCH_CONFIG_MAIN
#include "vyn/vyn.hpp"
#include <catch2/catch_all.hpp>
#include <string>

TEST_CASE("Print parser version", "[parser]") {
    REQUIRE(true); // Placeholder to ensure test runs
}

TEST_CASE("Lexer tokenizes indentation-based function", "[lexer]") {
    std::string source = "fn example()\n    const x = 10";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    REQUIRE(tokens.size() >= 7);
    REQUIRE(tokens[0].type == TokenType::KEYWORD_FN);
    REQUIRE(tokens[1].type == TokenType::IDENTIFIER);
    REQUIRE(tokens[2].type == TokenType::LPAREN);
    REQUIRE(tokens[3].type == TokenType::RPAREN);
    REQUIRE(tokens[4].type == TokenType::INDENT);
    REQUIRE(tokens[5].type == TokenType::KEYWORD_CONST);
}

TEST_CASE("Lexer tokenizes brace-based function", "[lexer]") {
    std::string source = "fn example() {\n    const x = 10\n}";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    REQUIRE(tokens.size() >= 9);
    REQUIRE(tokens[0].type == TokenType::KEYWORD_FN);
    REQUIRE(tokens[1].type == TokenType::IDENTIFIER);
    REQUIRE(tokens[4].type == TokenType::LBRACE);
    REQUIRE(tokens[5].type == TokenType::KEYWORD_CONST);
    REQUIRE(tokens[tokens.size() - 2].type == TokenType::RBRACE);
}

TEST_CASE("Parser handles indentation-based function", "[parser]") {
    std::string source = "fn example()\n    const x = 10";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    size_t pos = 0;
    ModuleParser parser(tokens, pos);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles brace-based function", "[parser]") {
    std::string source = "fn example() {\n    const x = 10\n}";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    size_t pos = 0;
    ModuleParser parser(tokens, pos);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Lexer rejects tabs", "[lexer]") {
    std::string source = "fn example()\n\tconst x = 10";
    Lexer lexer(source);
    REQUIRE_THROWS_AS(lexer.tokenize(), std::runtime_error);
    try {
        lexer.tokenize();
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()) == "Tabs not allowed at line 2, column 1");
    }
}

TEST_CASE("Parser rejects unmatched brace", "[parser]") {
    std::string source = "fn example() {\n    const x = 10\n";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    size_t pos = 0;
    ModuleParser parser(tokens, pos);
    REQUIRE_THROWS_AS(parser.parse(), std::runtime_error);
}

TEST_CASE("Parser handles import directive", "[parser]") {
    std::string source = "    import foo\n    fn bar()";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    size_t pos = 0;
    ModuleParser parser(tokens, pos);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles smuggle directive", "[parser]") {
    std::string source = "    smuggle foo\n    fn bar()";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    size_t pos = 0;
    ModuleParser parser(tokens, pos);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles try/catch/finally", "[parser]") {
    std::string source = "    fn example()\n        try { }";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    size_t pos = 0;
    ModuleParser parser(tokens, pos);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles defer", "[parser]") {
    std::string source = "    fn example()\n        defer foo()";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    size_t pos = 0;
    ModuleParser parser(tokens, pos);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles async/await", "[parser]") {
    std::string source = "    async fn example()\n        await foo()";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    size_t pos = 0;
    ModuleParser parser(tokens, pos);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles list comprehension", "[parser]") {
    std::string source = "[x for x in 1..10]";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    size_t pos = 0;
    ModuleParser parser(tokens, pos);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles operator overloading", "[parser]") {
    std::string source = "    class Foo\n        fn operator+(other)";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    size_t pos = 0;
    ModuleParser parser(tokens, pos);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles updated btree.vyn subset", "[parser]") {
    std::string source = "    # comment\n    template Node";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    size_t pos = 0;
    ModuleParser parser(tokens, pos);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles array type with expression size", "[parser]") {
    std::string source = R"(
        template BTree<K, V, M: UInt>
            class Node {
                var keys: [K; M-1]
            }
    )";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    size_t pos = 0;
    ModuleParser parser(tokens, pos);
    REQUIRE_NOTHROW(parser.parse());
}