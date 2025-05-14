#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include "vyn/token.hpp"
#include <vector>
#include <memory>
#include <stdexcept> // Required for std::runtime_error

namespace vyn { // Changed Vyn to vyn

// Constructor for ModuleParser
ModuleParser::ModuleParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, const std::string& file_path, DeclarationParser& declaration_parser) // Changed Vyn::Token to vyn::token::Token
    : BaseParser(tokens, pos, file_path), declaration_parser_(declaration_parser) {}

std::unique_ptr<vyn::Module> ModuleParser::parse() { // Changed Vyn::AST::ModuleNode to vyn::Module
    vyn::SourceLocation module_loc = this->current_location(); // Get location at the start of the module // Changed Vyn::AST::SourceLocation to vyn::SourceLocation
    std::vector<vyn::DeclPtr> declarations_decl_type; // Changed Vyn::AST::DeclNode to vyn::DeclPtr

    this->skip_comments_and_newlines(); // Skip any leading comments or newlines

    while (this->peek().type != vyn::TokenType::END_OF_FILE) { // Changed Vyn::TokenType
        // Vyn::AST::SourceLocation decl_loc = this->current_location(); // Not strictly needed if decl_node carries its own location
        auto decl_node = this->declaration_parser_.parse(); 

        if (decl_node) {
            declarations_decl_type.push_back(std::move(decl_node));
        } else {
            // If parse returns nullptr, it might mean end of parsable content or an unrecoverable error.
            // For now, assume it means no more declarations.
            break; 
        }
        this->skip_comments_and_newlines(); // Skip stuff between declarations
    }

    // Convert vector<unique_ptr<Declaration>> to vector<unique_ptr<Statement>>
    // Module in ast.hpp expects std::vector<StmtPtr> body. Declaration derives from Statement.
    std::vector<vyn::StmtPtr> statements_for_module;
    for (auto& decl : declarations_decl_type) {
        statements_for_module.push_back(std::move(decl));
    }

    // vyn::Module constructor in ast.hpp is: Module(SourceLocation loc, std::vector<StmtPtr> body);
    return std::make_unique<vyn::Module>(module_loc, std::move(statements_for_module));
}

} // End namespace vyn