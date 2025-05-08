// vyn_parser.cpp
#include <string>
#include <vector>
#include <stack>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>

// Catch2 test runner and macros
#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>

// Token types for Vyn lexer
enum class TokenType {
    KEYWORD_FN, KEYWORD_IF, KEYWORD_LET, IDENTIFIER, INT_LITERAL,
    LBRACE, RBRACE, LPAREN, RPAREN, SEMICOLON, COLON, EQ,
    INDENT, DEDENT, EOF_TOKEN
};

// Token structure
struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
};

// Lexer class
class Lexer {
public:
    Lexer(const std::string& source) : source_(source), pos_(0), line_(1), column_(1), in_braces_(false) {
        indent_stack_.push(0); // Root level has 0 spaces
    }

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (pos_ < source_.size()) {
            char c = source_[pos_];
            if (std::isspace(c)) {
                handle_whitespace(tokens);
            } else if (std::isalpha(c) || c == '_') {
                tokens.push_back(read_identifier_or_keyword());
            } else if (std::isdigit(c)) {
                tokens.push_back(read_number());
            } else if (c == '{') {
                tokens.push_back({TokenType::LBRACE, "{", line_, column_++});
                in_braces_ = true;
                pos_++;
            } else if (c == '}') {
                tokens.push_back({TokenType::RBRACE, "}", line_, column_++});
                in_braces_ = false;
                pos_++;
            } else if (c == '(') {
                tokens.push_back({TokenType::LPAREN, "(", line_, column_++});
                pos_++;
            } else if (c == ')') {
                tokens.push_back({TokenType::RPAREN, ")", line_, column_++});
                pos_++;
            } else if (c == ';') {
                tokens.push_back({TokenType::SEMICOLON, ";", line_, column_++});
                pos_++;
            } else if (c == ':') {
                tokens.push_back({TokenType::COLON, ":", line_, column_++});
                pos_++;
            } else if (c == '=') {
                tokens.push_back({TokenType::EQ, "=", line_, column_++});
                pos_++;
            } else {
                throw std::runtime_error("Unexpected character at line " + std::to_string(line_) + ", column " + std::to_string(column_));
            }
        }
        // Emit DEDENTs to close indentation levels
        while (indent_stack_.size() > 1) {
            tokens.push_back({TokenType::DEDENT, "", line_, column_});
            indent_stack_.pop();
        }
        tokens.push_back({TokenType::EOF_TOKEN, "", line_, column_});
        return tokens;
    }

private:
    void handle_whitespace(std::vector<Token>& tokens) {
        if (column_ != 1 || in_braces_) {
            // Skip non-leading whitespace or whitespace in braces
            if (source_[pos_] == '\n') {
                line_++;
                column_ = 1;
            } else {
                column_++;
            }
            pos_++;
            return;
        }

        // Count leading spaces
        int spaces = 0;
        while (pos_ < source_.size() && (source_[pos_] == ' ' || source_[pos_] == '\t')) {
            if (source_[pos_] == '\t') {
                throw std::runtime_error("Tabs not allowed at line " + std::to_string(line_) + ", column " + std::to_string(column_));
            }
            spaces++;
            pos_++;
            column_++;
        }

        // Handle newline or end of indentation
        if (pos_ < source_.size() && source_[pos_] == '\n') {
            line_++;
            column_ = 1;
            pos_++;
            return;
        }

        // Must be multiple of 2 spaces
        if (spaces % 2 != 0) {
            throw std::runtime_error("Indentation must be multiple of 2 spaces at line " + std::to_string(line_) + ", column " + std::to_string(column_));
        }

        int current_level = indent_stack_.top();
        if (spaces > current_level) {
            indent_stack_.push(spaces);
            tokens.push_back({TokenType::INDENT, "", line_, column_});
        } else if (spaces < current_level) {
            while (indent_stack_.size() > 1 && indent_stack_.top() > spaces) {
                tokens.push_back({TokenType::DEDENT, "", line_, column_});
                indent_stack_.pop();
            }
            if (indent_stack_.top() != spaces) {
                throw std::runtime_error("Inconsistent indentation at line " + std::to_string(line_) + ", column " + std::to_string(column_));
            }
        }
    }

    Token read_identifier_or_keyword() {
        std::string value;
        int start_column = column_;
        while (pos_ < source_.size() && (std::isalnum(source_[pos_]) || source_[pos_] == '_')) {
            value += source_[pos_];
            pos_++;
            column_++;
        }
        if (value == "fn") {
            return {TokenType::KEYWORD_FN, value, line_, start_column};
        } else if (value == "if") {
            return {TokenType::KEYWORD_IF, value, line_, start_column};
        } else if (value == "let") {
            return {TokenType::KEYWORD_LET, value, line_, start_column};
        }
        return {TokenType::IDENTIFIER, value, line_, start_column};
    }

    Token read_number() {
        std::string value;
        int start_column = column_;
        while (pos_ < source_.size() && std::isdigit(source_[pos_])) {
            value += source_[pos_];
            pos_++;
            column_++;
        }
        return {TokenType::INT_LITERAL, value, line_, start_column};
    }

    std::string source_;
    size_t pos_;
    int line_;
    int column_;
    bool in_braces_;
    std::stack<int> indent_stack_;
};

// Parser class (skeleton)
class Parser {
public:
    Parser(const std::vector<Token>& tokens) : tokens_(tokens), pos_(0) {}

    // Parse top-level module
    void parse_module() {
        while (pos_ < tokens_.size() && tokens_[pos_].type != TokenType::EOF_TOKEN) {
            parse_function();
        }
    }

private:
    void parse_function() {
        expect(TokenType::KEYWORD_FN);
        expect(TokenType::IDENTIFIER);
        expect(TokenType::LPAREN);
        expect(TokenType::RPAREN);
        parse_block();
    }

    void parse_block() {
        if (peek().type == TokenType::LBRACE) {
            expect(TokenType::LBRACE);
            while (peek().type != TokenType::RBRACE && peek().type != TokenType::EOF_TOKEN) {
                parse_statement();
            }
            expect(TokenType::RBRACE);
        } else {
            expect(TokenType::INDENT);
            while (peek().type != TokenType::DEDENT && peek().type != TokenType::EOF_TOKEN) {
                parse_statement();
            }
            expect(TokenType::DEDENT);
        }
    }

    void parse_statement() {
        if (peek().type == TokenType::KEYWORD_LET) {
            expect(TokenType::KEYWORD_LET);
            expect(TokenType::IDENTIFIER);
            expect(TokenType::EQ);
            expect(TokenType::INT_LITERAL);
            if (peek().type == TokenType::SEMICOLON) {
                expect(TokenType::SEMICOLON);
            }
        } else if (peek().type == TokenType::KEYWORD_IF) {
            expect(TokenType::KEYWORD_IF);
            expect(TokenType::IDENTIFIER); // Simplified condition
            parse_block();
        } else {
            throw std::runtime_error("Unexpected token at line " + std::to_string(peek().line) + ", column " + std::to_string(peek().column));
        }
    }

    void expect(TokenType type) {
        if (pos_ >= tokens_.size() || tokens_[pos_].type != type) {
            throw std::runtime_error("Expected token type at line " + std::to_string(peek().line) + ", column " + std::to_string(peek().column));
        }
        pos_++;
    }

    Token peek() const {
        return pos_ < tokens_.size() ? tokens_[pos_] : Token{TokenType::EOF_TOKEN, "", 0, 0};
    }

    std::vector<Token> tokens_;
    size_t pos_;
};

// Print tokens for debugging
void print_tokens(const std::vector<Token>& tokens) {
    for (const auto& token : tokens) {
        std::string type_str;
        switch (token.type) {
            case TokenType::KEYWORD_FN: type_str = "KEYWORD_FN"; break;
            case TokenType::KEYWORD_IF: type_str = "KEYWORD_IF"; break;
            case TokenType::KEYWORD_LET: type_str = "KEYWORD_LET"; break;
            case TokenType::IDENTIFIER: type_str = "IDENTIFIER"; break;
            case TokenType::INT_LITERAL: type_str = "INT_LITERAL"; break;
            case TokenType::LBRACE: type_str = "LBRACE"; break;
            case TokenType::RBRACE: type_str = "RBRACE"; break;
            case TokenType::LPAREN: type_str = "LPAREN"; break;
            case TokenType::RPAREN: type_str = "RPAREN"; break;
            case TokenType::SEMICOLON: type_str = "SEMICOLON"; break;
            case TokenType::COLON: type_str = "COLON"; break;
            case TokenType::EQ: type_str = "EQ"; break;
            case TokenType::INDENT: type_str = "INDENT"; break;
            case TokenType::DEDENT: type_str = "DEDENT"; break;
            case TokenType::EOF_TOKEN: type_str = "EOF"; break;
            default: type_str = "UNKNOWN"; break;
        }
        std::cout << "Token(type: " << type_str << ", value: \"" << token.value
                  << "\", line: " << token.line << ", column: " << token.column << ")\n";
    }
}

// Main function
int main(int argc, char* argv[]) {
    // Check for test flag
    if (argc >= 2 && std::string(argv[1]) == "--test") {
        // Run Catch2 tests, excluding --test from argv
        Catch::Session session;
        // Adjust argc and argv to skip --test
        int test_argc = 1;
        char* test_argv[] = { argv[0], nullptr };
        if (argc > 2) {
            // Pass additional arguments (e.g., --success, test names)
            test_argc = argc - 1;
            test_argv[0] = argv[0];
            for (int i = 2; i < argc; ++i) {
                test_argv[i - 1] = argv[i];
            }
            test_argv[test_argc] = nullptr;
        }
        return session.run(test_argc, test_argv);
    }

    // Read input from file or stdin
    std::string source;
    if (argc == 2) {
        // Read from file
        std::ifstream file(argv[1]);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open file " << argv[1] << std::endl;
            return 1;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        source = buffer.str();
        file.close();
    } else {
        // Read from stdin
        std::stringstream buffer;
        buffer << std::cin.rdbuf();
        source = buffer.str();
    }

    try {
        // Tokenize
        Lexer lexer(source);
        auto tokens = lexer.tokenize();
        std::cout << "Tokens:\n";
        print_tokens(tokens);

        // Parse
        Parser parser(tokens);
        parser.parse_module();
        std::cout << "Parsing successful. AST generated (stubbed).\n";
        // TODO: Print AST when implemented
    } catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

// Catch2 test cases
TEST_CASE("Lexer tokenizes indentation-based function", "[lexer]") {
    std::string code = "\nfn main()\n  let x = 1\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();

    REQUIRE(tokens.size() >= 7);
    REQUIRE(tokens[0].type == TokenType::KEYWORD_FN);
    REQUIRE(tokens[1].type == TokenType::IDENTIFIER);
    REQUIRE(tokens[2].type == TokenType::LPAREN);
    REQUIRE(tokens[3].type == TokenType::RPAREN);
    REQUIRE(tokens[4].type == TokenType::INDENT);
    REQUIRE(tokens[5].type == TokenType::KEYWORD_LET);
    REQUIRE(tokens[tokens.size() - 2].type == TokenType::DEDENT);
}

TEST_CASE("Lexer tokenizes brace-based function", "[lexer]") {
    std::string code = "\nfn main() {\n  let x = 1;\n}\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();

    REQUIRE(tokens.size() >= 9);
    REQUIRE(tokens[0].type == TokenType::KEYWORD_FN);
    REQUIRE(tokens[1].type == TokenType::IDENTIFIER);
    REQUIRE(tokens[4].type == TokenType::LBRACE);
    REQUIRE(tokens[5].type == TokenType::KEYWORD_LET);
    REQUIRE(tokens[tokens.size() - 2].type == TokenType::RBRACE);
}

TEST_CASE("Parser handles indentation-based function", "[parser]") {
    std::string code = "\nfn main()\n  let x = 1\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles brace-based function", "[parser]") {
    std::string code = "\nfn main() {\n  let x = 1;\n}\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Lexer rejects tabs", "[lexer]") {
    std::string code = "\nfn main()\n\tlet x = 1\n";
    Lexer lexer(code);
    REQUIRE_THROWS_AS(lexer.tokenize(), std::runtime_error);
    try {
        lexer.tokenize();
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()) == "Tabs not allowed at line 3, column 1");
    }
}

TEST_CASE("Parser rejects unmatched brace", "[parser]") {
    std::string code = "\nfn main() {\n  let x = 1;\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    REQUIRE_THROWS_AS(parser.parse_module(), std::runtime_error);
    try {
        parser.parse_module();
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()) == "Expected token type at line 4, column 1");
    }
}