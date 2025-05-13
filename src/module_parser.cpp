#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include "vyn/token.hpp"
#include <vector>
#include <memory>
#include <stdexcept> // Required for std::runtime_error

namespace Vyn {

// Constructor for ModuleParser
ModuleParser::ModuleParser(const std::vector<Vyn::Token>& tokens, size_t& pos, const std::string& file_path, DeclarationParser& declaration_parser)
    : BaseParser(tokens, pos, file_path), declaration_parser_(declaration_parser) {}

std::unique_ptr<Vyn::AST::ModuleNode> ModuleParser::parse() {
    Vyn::AST::SourceLocation module_loc = this->current_location(); // Get location at the start of the module
    std::vector<std::unique_ptr<Vyn::AST::DeclNode>> declarations_decl_type;

    this->skip_comments_and_newlines(); // Skip any leading comments or newlines

    while (this->peek().type != Vyn::TokenType::END_OF_FILE) { // Corrected EOF_TOKEN and peek call
        Vyn::AST::SourceLocation decl_loc = this->current_location();
        auto decl_node = this->declaration_parser_.parse(); // Corrected member access

        if (decl_node) {
            declarations_decl_type.push_back(std::move(decl_node));
        } else {
            break;
        }
        this->skip_comments_and_newlines(); // Skip stuff between declarations
    }

    // Convert vector<unique_ptr<DeclNode>> to vector<unique_ptr<Node>>
    std::vector<std::unique_ptr<Vyn::AST::Node>> declarations_node_type;
    for (auto& decl : declarations_decl_type) {
        declarations_node_type.push_back(std::move(decl));
    }

    // ModuleNode constructor in ast.hpp is: ModuleNode(SourceLocation loc, std::vector<std::unique_ptr<Node>> b, Node* p = nullptr)
    // The current_file_path_ is part of the SourceLocation or managed internally by BaseParser.
    // It is not a direct argument to ModuleNode constructor based on ast.hpp
    return std::make_unique<Vyn::AST::ModuleNode>(module_loc, std::move(declarations_node_type));
}

} // End namespace Vyn