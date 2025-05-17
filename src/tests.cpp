#define CATCH_CONFIG_MAIN
#include "vyn/vyn.hpp"
#include <catch2/catch_all.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <memory>

// llvm includes for JIT
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

// Forward declare run_vyn_code
int run_vyn_code(const std::string& source);

TEST_CASE("Print parser version", "[parser]") {
    REQUIRE(true); // Placeholder to ensure test runs
}

TEST_CASE("Lexer tokenizes indentation-based function", "[lexer]") {
    std::string source = R"(fn example()
    const x = 10)"; // test1.vyn
    Lexer lexer(source, "test1.vyn");
    auto tokens = lexer.tokenize();
    // Expected: FN, ID, LPAREN, RPAREN, NEWLINE, INDENT, CONST, ID, EQ, INT_LITERAL, DEDENT, EOF
    REQUIRE(tokens.size() >= 10); 
    REQUIRE(tokens[0].type == vyn::TokenType::KEYWORD_FN);
    REQUIRE(tokens[1].type == vyn::TokenType::IDENTIFIER);
    REQUIRE(tokens[2].type == vyn::TokenType::LPAREN);
    REQUIRE(tokens[3].type == vyn::TokenType::RPAREN);
    REQUIRE(tokens[4].type == vyn::TokenType::NEWLINE); 
    REQUIRE(tokens[5].type == vyn::TokenType::INDENT);   
    REQUIRE(tokens[6].type == vyn::TokenType::KEYWORD_CONST); 
}

TEST_CASE("Lexer tokenizes brace-based function", "[lexer]") {
    std::string source = R"(fn example() {
    const x = 10
})";
    Lexer lexer(source, "test2.vyn");
    auto tokens = lexer.tokenize();
    REQUIRE(tokens.size() >= 9); // Expect at least: FN, ID, LPAREN, RPAREN, LBRACE, NEWLINE, CONST, ID, EQ, INT_LITERAL, NEWLINE, RBRACE, EOF
    REQUIRE(tokens[0].type == vyn::TokenType::KEYWORD_FN);
    REQUIRE(tokens[1].type == vyn::TokenType::IDENTIFIER);
    REQUIRE(tokens[4].type == vyn::TokenType::LBRACE);
    REQUIRE(tokens[6].type == vyn::TokenType::KEYWORD_CONST); // Was tokens[5]
    REQUIRE(tokens[tokens.size() - 2].type == vyn::TokenType::RBRACE);
}

TEST_CASE("Parser handles indentation-based function", "[parser]") {
    std::string source = R"(fn example()
    const x = 10)"; // test3.vyn
    Lexer lexer(source, "test3.vyn");
    auto tokens = lexer.tokenize();
    std::cerr << "Tokens for test3.vyn:" << std::endl;
    for (const auto& t : tokens) {
        std::cerr << vyn::token_type_to_string(t.type) << " (" << t.lexeme << ")\n";
    }
    vyn::Parser parser(tokens, "test3.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles brace-based function", "[parser]") {
    std::string source = R"(fn example() {
    10;
})"; // test4.vyn
    Lexer lexer(source, "test4.vyn");
    vyn::Parser parser(lexer.tokenize(), "test4.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Lexer rejects tabs", "[lexer]") {
    // This test must include a TAB character at the start of the indented line.
    std::string source = "fn example()\n\tconst x = 10";
    Lexer lexer(source, "test5.vyn"); // Ensure filename is passed
    REQUIRE_THROWS_AS(lexer.tokenize(), std::runtime_error);
    try {
        lexer.tokenize();
        FAIL("Lexer should have thrown an exception for tabs.");
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()) == "Tabs not allowed at line 2, column 1");
    }
}

TEST_CASE("Parser rejects unmatched brace", "[parser]") {
    std::string source = "fn example() {\n    const x = 10\n";
    Lexer lexer(source, "test6.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test6.vyn");
    REQUIRE_THROWS_AS(parser.parse_module(), std::runtime_error);
}

TEST_CASE("Parser handles import directive", "[parser]") {
    std::string source = R"(import foo
fn bar())"; // test7.vyn
    Lexer lexer(source, "test7.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test7.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles smuggle directive", "[parser]") {
    std::string source = R"(smuggle foo
fn bar())"; // test8.vyn
    Lexer lexer(source, "test8.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test8.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles try/catch/finally", "[parser]") {
    std::string source = R"(fn example()
    try
        const x = 1
    catch e
        const y = 2
    finally
        const z = 3)"; // test9.vyn
    Lexer lexer(source, "test9.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test9.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles defer", "[parser]") {
    std::string source = R"(fn example()
    defer foo())"; // test10.vyn
    Lexer lexer(source, "test10.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test10.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles async/await", "[parser]") {
    // Modern Vyn: async fn with indented block and await statement
    std::string source = "async fn example()\n    await foo()";
    Lexer lexer(source, "test11.vyn");
    auto tokens = lexer.tokenize();
    
    // Debug: print all tokens for inspection
    std::cerr << "\nTokens for test11.vyn:" << std::endl;
    for (size_t i = 0; i < tokens.size(); i++) {
        const auto& t = tokens[i];
        std::cerr << i << ": " << vyn::token_type_to_string(t.type) 
                  << " (" << t.lexeme << ")" << std::endl;
    }
    
    vyn::Parser parser(tokens, "test11.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles list comprehension", "[parser]") {
    std::string source = "const l = [x * x for x in 0..10];"; // Assign to var and add semicolon
    Lexer lexer(source, "test12.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test12.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles operator overloading", "[parser]") {
    std::string source = R"(class Foo {
    fn op_add(other: Foo) { // Changed operator+ to op_add for diagnosis
        const x = 1 // Placeholder body
    }
})"; // test13.vyn
    Lexer lexer(source, "test13.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test13.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles updated btree.vyn subset", "[parser]") {
    std::string source = R"(template Node<K> {
    class Foo {
        var children: [my<Node>; K]
    }
})"; // test14.vyn
    Lexer lexer(source, "test14.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test14.vyn");
    try {
        parser.parse_module();
    } catch (const std::runtime_error& e) {
        std::string error_message = e.what();
        // It's good practice to also print to cerr for immediate visibility during local runs,
        // though FAIL should capture it for the test report.
        std::cerr << "CAUGHT EXCEPTION IN TEST 'Parser handles updated btree.vyn subset': " << error_message << std::endl;
        FAIL("Parser threw an exception: " + error_message);
    }
}

TEST_CASE("Parser handles array type with expression size", "[parser]") {
    std::string source = R"(template BTree<K, V, M: UInt> {
    class Node {
        var keys: [K; M-1]
    }
})";
    Lexer lexer(source, "test15.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test15.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles full btree.vyn", "[parser]") {
    std::string source = R"(class Node {
    var is_leaf: Bool
    fn new(is_leaf_param: Bool) -> Node {
        return Node { is_leaf: is_leaf_param }
    }
})";
    Lexer lexer(source, "test16.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test16.vyn");
    try {
        parser.parse_module();
    } catch (const std::runtime_error& e) {
        std::cerr << "\nERROR in test16.vyn: " << e.what() << std::endl;
        throw; // Rethrow to fail the test
    }
}

TEST_CASE("Parser handles dot access in expression", "[parser]") {
    std::string source = "fn test() { if x.y < 5 { } }";
    Lexer lexer(source, "test17.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test17.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles VarDecl with generic type", "[parser]") {
    std::string source = R"(var x: my<T>;
fn test() { if a < b.c { } })"; // test18.vyn
    Lexer lexer(source, "test18.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test18.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles declaration node kinds", "[parser]") {
    std::string source = R"(template T { class C { fn f(x: their<T>) { } } })"; // test19.vyn
    Lexer lexer(source, "test19.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test19.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles main.vyn imports", "[parser]") {
    std::string source = R"(import vyn::fs
smuggle http::client)";
    Lexer lexer(source, "test20.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test20.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles main.vyn class and operator", "[parser]") {
    std::string source = R"(class Vector {
    var x: Float
    var y: Float
    fn operator+(other: Vector) -> Vector {
        return Vector { x: self.x + other.x, y: self.y + other.y }
    }
})";
    Lexer lexer(source, "test21.vyn");
    auto tokens = lexer.tokenize();
    
    // Debug: print all tokens for inspection
    std::cerr << "\nTokens for test21.vyn:" << std::endl;
    for (size_t i = 0; i < tokens.size(); i++) {
        const auto& t = tokens[i];
        std::cerr << i << ": " << vyn::token_type_to_string(t.type) 
                  << " (" << t.lexeme << ") at line " << t.location.line 
                  << ", col " << t.location.column << std::endl;
    }
    
    vyn::Parser parser(tokens, "test21.vyn");
    try {
        parser.parse_module();
    } catch (const std::runtime_error& e) {
        std::cerr << "\nERROR in test21.vyn: " << e.what() << std::endl;
        throw; // Rethrow to fail the test
    }
}

TEST_CASE("Parser handles main.vyn async function", "[parser]") {
    std::string source = R"(async fn fetch_data(url: String) -> String throws NetworkError {
    const conn: my<http::client::Connection> = http::client::connect(url)
    const resp: my<http::client::Response> = await (view conn).get("/")
    if (resp.status != 200) {
        throw NetworkError("Failed to fetch: " + resp.status.to_string())
    }
    return (view resp).text()
})";
    Lexer lexer(source, "test22.vyn");
    auto tokens = lexer.tokenize();
    
    // Debug: print all tokens for inspection
    std::cerr << "\nTokens for test22.vyn:" << std::endl;
    for (size_t i = 0; i < tokens.size(); i++) {
        const auto& t = tokens[i];
        std::cerr << i << ": " << vyn::token_type_to_string(t.type) 
                  << " (" << t.lexeme << ") at line " << t.location.line 
                  << ", col " << t.location.column << std::endl;
    }
    
    vyn::Parser parser(tokens, "test22.vyn");
    try {
        parser.parse_module();
    } catch (const std::runtime_error& e) {
        std::cerr << "\nERROR in test22.vyn: " << e.what() << std::endl;
        throw; // Rethrow to fail the test
    }
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
    Lexer lexer(source, "test23.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test23.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles binary expression in array size", "[parser]") {
    std::string source = "var arr: [Int; N-1];"; // Added semicolon
    Lexer lexer(source, "test24.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test24.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles expression statements", "[parser]") {
    std::string source = "fn test() { x.y.z; [1, 2, 3]; }";
    Lexer lexer(source, "test25.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test25.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles nested binary expressions", "[parser]") {
    std::string source = "var x: [Int; N-1+2*3];"; // Added semicolon
    Lexer lexer(source, "test26.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test26.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles expression statements in blocks", "[parser]") {
    std::string source = "fn test() { [x * x for x in 0..10]; x.y; }";
    Lexer lexer(source, "test27.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test27.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles complex class methods", "[parser]") {
    std::string source = R"(class Foo {
    fn operator+(other: Foo) {
        const x = 10
    }
    fn bar() {
        const y = 2 // Placeholder body
    }
})"; // test28.vyn
    Lexer lexer(source, "test28.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test28.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles nested range expressions", "[parser]") {
    std::string source = "const l = [x for x in 0..(5..10)[0]];"; // Assign to var and add semicolon
    Lexer lexer(source, "test29.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test29.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles complex list comprehension", "[parser]") {
    std::string source = "const l = [x * x + 1 for x in 0..10];"; // Assign to var and add semicolon
    Lexer lexer(source, "test30.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test30.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles single-line comments", "[parser]") {
    std::string source = "// This is a comment\nfn test() {}";
    Lexer lexer(source, "test31.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test31.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles negative numbers", "[parser]") {
    std::string source = "var x = -42;"; // Added semicolon
    Lexer lexer(source, "test32.vyn");
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test32.vyn");
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Lexer handles ref and underscore", "[lexer]") {
    std::string source = "var _x: my<Type> = _";
    Lexer lexer(source, "test33.vyn"); // Assuming Lexer is global
    auto tokens = lexer.tokenize();
    bool found_ref = false, found_underscore = false;
    for (const auto& token : tokens) {
        if (token.type == vyn::TokenType::KEYWORD_MY && token.lexeme == "my") found_ref = true;
        if (token.type == vyn::TokenType::UNDERSCORE && token.lexeme == "_") found_underscore = true;
    }
    REQUIRE(found_ref);
    REQUIRE(found_underscore);
}

// --- Codegen/Runtime Tests ---
// These tests require a working parser, semantic analyzer, and LLVM codegen backend.
// If a function like run_vyn_code(source, expected_result) exists, use it. Otherwise, add TODOs for integration.

TEST_CASE("Codegen: Pointer dereference assignment", "[codegen][pointer]") {
    std::string source = R"(
fn main() {
    var x: Int = 42
    var p: loc<Int> = loc(x)
    var q: Int
    at(p) = 99
    q = at(p)

    return q
}
)";
    int result = run_vyn_code(source);
    REQUIRE(result == 99);
}

TEST_CASE("Codegen: Member access assignment", "[codegen][member]") {
    std::string source = R"(
class Point {
    var x: Int
    var y: Int
}
fn main() {
    var p = Point { x: 1, y: 2 }
    p.x = 10
    p.y = 20
    return p.x + p.y
}
)";
    int result = run_vyn_code(source);
    REQUIRE(result == 30);
}

TEST_CASE("Codegen: Multidimensional array assignment/access", "[codegen][array]") {
    std::string source = R"(
fn main() {
    var arr: [[Int; 2]; 2] = [[1, 2], [3, 4]]
    arr[0][1] = 42
    arr[1][0] = 99
    return arr[0][1] + arr[1][0]
}
)";
    int result = run_vyn_code(source);
    REQUIRE(result == 141);
}

TEST_CASE("Semantic: loc<T> cannot be assigned from integer literal", "[semantic][pointer][error]") {
    std::string source = R"(
fn main() {
    var p: loc<Int> = 0x1234
    return 0
}
)";
    REQUIRE_THROWS(run_vyn_code(source));
}

TEST_CASE("Semantic: from(addr) only allowed in unsafe", "[semantic][pointer][unsafe]") {
    std::string source_ok = R"(
fn main() {
    var addr: Int = 0x1234
    var p: loc<Int>
    unsafe {
        p = from(addr)
    }
    return 0
}
)";
    REQUIRE_NOTHROW(run_vyn_code(source_ok));

    std::string source_err = R"(
fn main() {
    var addr: Int = 0x1234
    var p: loc<Int> = from(addr)
    return 0
}
)";
    REQUIRE_THROWS(run_vyn_code(source_err));
}

TEST_CASE("Codegen: addr(loc) and from(addr) round-trip in unsafe", "[codegen][pointer][unsafe]") {
    std::string source = R"(
fn main() {
    var x: Int = 55
    var p: loc<Int> = loc(x)
    var addr: Int
    var q: loc<Int>
    unsafe {
        addr = addr(p)
        q = from(addr)
    }
    at(q) = 99
    return at(p)
}
)";
    int result = run_vyn_code(source);
    REQUIRE(result == 99);
}

// Run Vyn code end-to-end: parse, analyze, codegen, JIT, run main(). Throws on error.
int run_vyn_code(const std::string& source) {
    // 1. Lex and parse
    Lexer lexer(source, "test_runtime.vyn"); // Assuming Lexer is global
    auto tokens = lexer.tokenize();
    vyn::Parser parser(tokens, "test_runtime.vyn"); // Assuming Parser is global
    std::unique_ptr<vyn::ast::Module> ast = parser.parse_module(); // ast::Module is in vyn::ast

    // 2. Semantic analysis
    vyn::SemanticAnalyzer sema;
    sema.analyze(ast.get());
    if (!sema.getErrors().empty()) {
        std::string all_errors;
        for(const auto& err : sema.getErrors()) {
            all_errors += err + "\n";
        }
        throw std::runtime_error("Semantic error(s): \n" + all_errors);
    }

    // 3. Codegen
    vyn::LLVMCodegen codegen; // Corrected: No arguments for constructor
    codegen.generate(ast.get(), "test_module.ll"); // Corrected: Added output filename
    std::unique_ptr<llvm::Module> llvmMod = codegen.releaseModule(); // Corrected: Use releaseModule()

    // 4. JIT setup
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    // llvm::InitializeNativeTargetAsmParser(); // Optional

    std::string errStr;
    llvm::EngineBuilder builder(std::move(llvmMod)); // llvmMod is already a unique_ptr
    builder.setErrorStr(&errStr);
    builder.setEngineKind(llvm::EngineKind::JIT);
    std::unique_ptr<llvm::ExecutionEngine> engine(builder.create());
    if (!engine) {
        std::string module_dump_str;
        llvm::raw_string_ostream rso(module_dump_str);
        // Use the llvmMod unique_ptr for dumping, as codegen no longer owns the module
        if (llvmMod) { 
             llvmMod->print(rso, nullptr);
        } else {
             rso << "Could not retrieve LLVM Module for dumping (it was null).";
        }
        rso.flush();
        throw std::runtime_error("LLVM JIT error: " + errStr);
    }

    // 5. Find and run main()
    llvm::Function* mainFn = engine->FindFunctionNamed("main"); 
    if (!mainFn) {
        std::string module_dump_str;
        llvm::raw_string_ostream rso(module_dump_str);
        if (llvmMod) { // Use llvmMod for dumping
            llvmMod->print(rso, nullptr);
        } else {
            rso << "Could not retrieve LLVM Module for dumping (main not found, module was null).";
        }
        rso.flush();
        throw std::runtime_error("No main() function found in LLVM module");
    }
    std::vector<llvm::GenericValue> noargs;
    llvm::GenericValue result = engine->runFunction(mainFn, noargs);
    return result.IntVal.getSExtValue();
}