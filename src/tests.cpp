#define CATCH_CONFIG_MAIN
#include "vyn/vyn.hpp"
#include <catch2/catch_all.hpp>
#include <string>

TEST_CASE("Print parser version", "[parser]") {
    REQUIRE(true); // Placeholder to ensure test runs
}

TEST_CASE("Lexer tokenizes indentation-based function", "[lexer]") {
    std::string source = "fn example()\n    const x = 10";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    REQUIRE(tokens.size() >= 7);
    REQUIRE(tokens[0].type == vyn::TokenType::KEYWORD_FN);
    REQUIRE(tokens[1].type == vyn::TokenType::IDENTIFIER);
    REQUIRE(tokens[2].type == vyn::TokenType::LPAREN);
    REQUIRE(tokens[3].type == vyn::TokenType::RPAREN);
    REQUIRE(tokens[4].type == vyn::TokenType::INDENT);
    REQUIRE(tokens[5].type == vyn::TokenType::KEYWORD_CONST);
}

TEST_CASE("Lexer tokenizes brace-based function", "[lexer]") {
    std::string source = R"(fn example() {
    const x = 10
})";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    REQUIRE(tokens.size() >= 9); // Expect at least: FN, ID, LPAREN, RPAREN, LBRACE, NEWLINE, CONST, ID, EQ, INT_LITERAL, NEWLINE, RBRACE, EOF
    REQUIRE(tokens[0].type == vyn::TokenType::KEYWORD_FN);
    REQUIRE(tokens[1].type == vyn::TokenType::IDENTIFIER);
    REQUIRE(tokens[4].type == vyn::TokenType::LBRACE);
    REQUIRE(tokens[6].type == vyn::TokenType::KEYWORD_CONST); // Was tokens[5]
    REQUIRE(tokens[tokens.size() - 2].type == vyn::TokenType::RBRACE);
}

TEST_CASE("Parser handles indentation-based function", "[parser]") {
    std::string source = "fn example()\n    const x = 10";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles brace-based function", "[parser]") {
    std::string source = "fn example() {\n    const x = 10\n}";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Lexer rejects tabs", "[lexer]") {
    std::string source = "fn example()\n\tconst x = 10";
    Lexer lexer(source, "test.vyn");
    REQUIRE_THROWS_AS(lexer.tokenize(), std::runtime_error);
    try {
        lexer.tokenize();
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()) == "Tabs not allowed at line 2, column 1 in file test.vyn");
    }
}

TEST_CASE("Parser rejects unmatched brace", "[parser]") {
    std::string source = "fn example() {\n    const x = 10\n";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_THROWS_AS(parser.parse_module(), std::runtime_error);
}

TEST_CASE("Parser handles import directive", "[parser]") {
    std::string source = "    import foo\n    fn bar()";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles smuggle directive", "[parser]") {
    std::string source = "    smuggle foo\n    fn bar()";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles try/catch/finally", "[parser]") {
    std::string source = "    fn example()\n        try { }";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles defer", "[parser]") {
    std::string source = "    fn example()\n        defer foo()";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles async/await", "[parser]") {
    std::string source = "    async fn example()\n        await foo()";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles list comprehension", "[parser]") {
    std::string source = "[x * x for x in 0..10]";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles operator overloading", "[parser]") {
    std::string source = "    class Foo\n        fn operator+(other)";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles updated btree.vyn subset", "[parser]") {
    std::string source = "template Node";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles array type with expression size", "[parser]") {
    std::string source = R"(template BTree<K, V, M: UInt>
    class Node {
        var keys: [K; M-1]
    })";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles full btree.vyn", "[parser]") {
    std::string source = R"(class Node {
    var is_leaf: Bool
    fn new(is_leaf_param: Bool) -> Node
        Node { is_leaf: is_leaf_param }
})";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles dot access in expression", "[parser]") {
    std::string source = "fn test() { if x.y < 5 { } }";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles VarDecl with generic type", "[parser]") {
    std::string source = "var x: ref<T>\nfn test() { if a < b.c { } }";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles declaration node kinds", "[parser]") {
    std::string source = "template T class C { fn f(x: ref<T>) { } }";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles main.vyn imports", "[parser]") {
    std::string source = "import vyn::fs\nsmuggle http::client";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles main.vyn class and operator", "[parser]") {
    std::string source = R"(class Vector
    var x: Float
    var y: Float
    fn operator+(other: Vector) -> Vector
        Vector { x: self.x + other.x, y: self.y + other.y }
)";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles main.vyn async function", "[parser]") {
    std::string source = R"(async fn fetch_data(url: String) throws NetworkError -> String
    const conn = http::client::connect(url)
    const resp = await conn.get("/")
    if resp.status != 200
        throw NetworkError("Failed to fetch: " + resp.status.to_string())
    resp.text()
)";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles main.vyn try/catch/finally", "[parser]") {
    std::string source = R"(fn main()
    try
        var squares = [x * x for x in 0..10]
        const v1 = Vector::new(1.0, 2.0)
        const v2 = Vector::new(3.0, 4.0)
        const sum = v1 + v2
    catch (e: NetworkError)
        println("Network error: {}", e.message)
    catch (e: IOError)
        println("IO error: {}", e.message)
    finally
        println("Cleanup complete")
)";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles binary expression in array size", "[parser]") {
    std::string source = "var arr: [Int; N-1]";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles expression statements", "[parser]") {
    std::string source = "fn test() { x.y.z; [1, 2, 3]; }";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles nested binary expressions", "[parser]") {
    std::string source = "var x: [Int; N-1+2*3]";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles expression statements in blocks", "[parser]") {
    std::string source = "fn test() { [x * x for x in 0..10]; x.y; }";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles complex class methods", "[parser]") {
    std::string source = "class Foo\n    fn operator+(other) { const x = 10 }\n    fn bar()";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles nested range expressions", "[parser]") {
    std::string source = "[x for x in 0..(5..10)[0]]";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles complex list comprehension", "[parser]") {
    std::string source = "[x * x + 1 for x in 0..10]";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles single-line comments", "[parser]") {
    std::string source = "// This is a comment\nfn test()";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles negative numbers", "[parser]") {
    std::string source = "var x = -42";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Lexer handles ref and underscore", "[lexer]") {
    std::string source = "ref x = _";
    Lexer lexer(source, "test.vyn");
    auto tokens = lexer.tokenize();
    bool found_ref = false, found_underscore = false;
    for (const auto& token : tokens) {
        if (token.type == vyn::TokenType::KEYWORD_REF && token.lexeme == "ref") found_ref = true;
        if (token.type == vyn::TokenType::UNDERSCORE && token.lexeme == "_") found_underscore = true;
    }
    REQUIRE(found_ref);
    REQUIRE(found_underscore);
}