#include "vyn/vyn.hpp"
#include <catch2/catch_session.hpp> // Include Catch2 for test runner
#include <iostream>
#include <fstream>
#include <sstream>

void print_tokens(const std::vector<Token>& tokens) {
    for (const auto& token : tokens) {
        std::cout << "Token(type: " << token_type_to_string(token.type) << ", value: \"" << token.value
                  << "\", line: " << token.line << ", column " << token.column << ")\n";
    }
}

int main(int argc, char* argv[]) {
    std::cout << argv[0] << ": Version: 0.2.7\n" << std::endl;

    if (argc >= 2 && std::string(argv[1]) == "--test") {
        Catch::Session session;
        int test_argc = 1;
        char* test_argv[] = { argv[0], nullptr };
        if (argc > 2) {
            test_argc = argc - 1;
            test_argv[0] = argv[0];
            for (int i = 2; i < argc; ++i) test_argv[i - 1] = argv[i];
            test_argv[test_argc] = nullptr;
        }
        return session.run(test_argc, test_argv);
    }

    std::string source;
    if (argc == 2) {
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
        std::stringstream buffer;
        buffer << std::cin.rdbuf();
        source = buffer.str();
    }

    try {
        Lexer lexer(source);
        auto tokens = lexer.tokenize();
        std::cout << "Tokens:\n";
        print_tokens(tokens);
        ModuleParser parser(tokens);
        auto ast = parser.parse();
        std::cout << "Parsing successful. AST generated.\n";
    } catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}