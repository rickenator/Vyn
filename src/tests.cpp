#include "vyn/vyn.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <iostream>

TEST_CASE("Print parser version", "[parser]") {
    std::cout << "Version: 0.2.7\n";
}

TEST_CASE("Lexer tokenizes indentation-based function", "[lexer]") {
    std::string code = "\nfn main()\n  const x = 1\n";
    Lexer lexer(code);
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
    std::string code = "\nfn main() {\n  const x = 1\n}\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    REQUIRE(tokens.size() >= 9);
    REQUIRE(tokens[0].type == TokenType::KEYWORD_FN);
    REQUIRE(tokens[1].type == TokenType::IDENTIFIER);
    REQUIRE(tokens[4].type == TokenType::LBRACE);
    REQUIRE(tokens[5].type == TokenType::KEYWORD_CONST);
    REQUIRE(tokens[tokens.size() - 2].type == TokenType::RBRACE);
}

TEST_CASE("Parser handles indentation-based function", "[parser]") {
    std::string code = "\nfn main()\n  const x = 1\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles brace-based function", "[parser]") {
    std::string code = "\nfn main() {\n  const x = 1\n}\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Lexer rejects tabs", "[lexer]") {
    std::string code = "\nfn main()\n\tconst x = 1\n";
    Lexer lexer(code);
    REQUIRE_THROWS_AS(lexer.tokenize(), std::runtime_error);
    try {
        lexer.tokenize();
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()) == "Tabs not allowed at line 3, column 1");
    }
}

TEST_CASE("Parser rejects unmatched brace", "[parser]") {
    std::string code = "\nfn main() {\n  const x = 1\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_THROWS_AS(parser.parse(), std::runtime_error);
}

TEST_CASE("Parser handles import directive", "[parser]") {
    std::string code = R"(
    import vyn::fs
    fn main() {
      const x = 1
    }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles smuggle directive", "[parser]") {
    std::string code = R"(
    smuggle http::client
    fn main() {
      const x = 1
    }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles try/catch/finally", "[parser]") {
    std::string code = R"(
    fn main() {
      try {
        risky()
      } catch (e: Error) {
        println("Caught")
      } finally {
        cleanup()
      }
    }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles defer", "[parser]") {
    std::string code = R"(
    fn main() {
      var f = open_file("data.txt")
      defer f.close()
      f.write("data")
    }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles async/await", "[parser]") {
    std::string code = R"(
    async fn fetch() -> String {
      const data = await http_get("url")
      data
    }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles list comprehension", "[parser]") {
    std::string code = R"(
    fn main() {
      const squares = [x * x for x in 0..10]
    }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles operator overloading", "[parser]") {
    std::string code = R"(
    class Vector {
      var x: Float
      fn operator+(other: Vector) -> Vector {
        Vector { x: self.x + other.x }
      }
    }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles updated btree.vyn subset", "[parser]") {
    std::string code = R"(
    // B-tree implementation in Vyn
    template Comparable
      fn lt(&self, other: &Self) -> Bool
      fn eq(&self, other: &Self) -> Bool

    template BTree<K: Comparable, V, M: UInt>
      class Node {
        var keys: [K; M-1]
        var values: [V; M-1]
        var num_keys: UInt
        fn new(is_leaf: Bool) -> Node {
          Node { keys = [K; M-1](), values = [V; M-1](), num_keys = 0 }
        }
      }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}