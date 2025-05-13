#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include "vyn/token.hpp"
#include <vector>
#include <memory>
#include <stdexcept> // Required for std::runtime_error

// Constructor for ModuleParser
ModuleParser::ModuleParser(const std::vector<Vyn::Token>& tokens, size_t& pos, const std::string& file_path, DeclarationParser& declaration_parser)
    : BaseParser(tokens, pos, file_path), declaration_parser_(declaration_parser) {}

std::unique_ptr<Vyn::AST::ModuleNode> ModuleParser::parse() {
    Vyn::AST::SourceLocation module_loc = current_location(); // Get location at the start of the module
    std::vector<std::unique_ptr<Vyn::AST::DeclNode>> declarations;

    skip_comments_and_newlines(); // Skip any leading comments or newlines

    while (peek().type != Vyn::TokenType::EOF_TOKEN) {
        // At the module level, we expect declarations (functions, structs, impls, etc.)
        // Or potentially import/export statements if those are treated as declarations.
        // For now, let's assume DeclarationParser handles all top-level constructs.
        
        Vyn::AST::SourceLocation decl_loc = current_location();
        auto decl_node = declaration_parser_.parse(); // This will attempt to parse one declaration

        if (decl_node) {
            declarations.push_back(std::move(decl_node));
        } else {
            // DeclarationParser::parse() should throw on error or return nullptr only at EOF.
            // If it returns nullptr and it's not EOF, it's an issue or unexpected token.
            // DeclarationParser::parse() now throws on unexpected token if not EOF.
            // So, if we are here and decl is nullptr, it implies EOF was hit by DeclarationParser,
            // which contradicts the while loop condition.
            // This path should ideally not be taken if DeclarationParser works as expected.
            // For safety, if an unexpected nullptr occurs, break to avoid infinite loop.
            break; 
        }
        skip_comments_and_newlines(); // Skip stuff between declarations
    }

    return std::make_unique<Vyn::AST::ModuleNode>(module_loc, current_file_path_, std::move(declarations));
}