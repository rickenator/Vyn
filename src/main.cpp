#include "vyn/vyn.hpp"
#include <catch2/catch_session.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>

int main(int argc, char** argv) {
    std::cout << "./vyn_parser: Version: 0.3.0\n" << std::endl;

    bool run_tests = false;
    bool show_success = false;
    std::string filename;

    // Parse command-line arguments
    std::vector<std::string> catch_args = {"vyn_parser"}; // Program name as argv[0]
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--test") {
            run_tests = true;
            // Skip adding --test to catch_args
        } else if (arg == "--success") {
            show_success = true;
            catch_args.push_back("-s"); // Map --success to Catch2's -s (show successes)
        } else if (arg[0] != '-') {
            filename = arg;
        } else {
            catch_args.push_back(arg); // Pass other args to Catch2 (e.g., test filters)
        }
    }

    if (run_tests) {
        // Convert string vector to char* array for Catch2
        std::vector<char*> catch_argv;
        for (auto& arg : catch_args) {
            catch_argv.push_back(const_cast<char*>(arg.c_str()));
        }

        // Run Catch2 tests
        std::cout << "Running tests...\n";
        Catch::Session session;
        int result = session.run(catch_argv.size(), catch_argv.data());
        return result;
    }

    if (filename.empty()) {
        std::cerr << "Error: No input file specified.\n";
        return 1;
    }

    // Read input file
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << ".\n";
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    // Tokenize
    Lexer lexer(source, filename); // Pass filename to Lexer constructor
    std::vector<vyn::token::Token> tokens; // Changed Vyn::Token to vyn::token::Token
    try {
        tokens = lexer.tokenize();
    } catch (const std::runtime_error& e) {
        std::cerr << "Lexing error: " << e.what() << "\n";
        return 1;
    }

    // Parse
    vyn::Parser parser(tokens, filename); // Changed Vyn::Parser to vyn::Parser and pass filename
    std::unique_ptr<vyn::Module> ast; // Changed Vyn::AST::Node to vyn::Module
    try {
        ast = parser.parse_module(); // Changed to parse_module()
    } catch (const std::runtime_error& e) {
        std::cerr << "Parsing error: " << e.what() << "\n";
        return 1;
    }

    // Print success if requested
    if (show_success) {
        std::cout << "Parsing successful.\n";
    }

    return 0;
}