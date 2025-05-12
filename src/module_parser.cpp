#include "vyn/parser.hpp"
#include <iostream>

ModuleParser::ModuleParser(const std::vector<Token>& tokens, size_t& pos)
    : BaseParser(tokens, pos) {}

std::unique_ptr<ASTNode> ModuleParser::parse() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Module);
    std::cout << "DEBUG: ModuleParser::parse at token " << token_type_to_string(peek().type)
              << ", value = " << peek().value << ", line = " << peek().line
              << ", pos_ = " << pos_ << ", indent_level = " << indent_levels_.back() << std::endl;

    while (pos_ < tokens_.size() && tokens_[pos_].type != TokenType::EOF_TOKEN) {
        if (pos_ >= tokens_.size() || tokens_[pos_].type == TokenType::EOF_TOKEN) {
            break;
        }

        std::cout << "DEBUG: Processing token " << token_type_to_string(peek().type)
                  << ", value = " << peek().value << ", line = " << peek().line
                  << ", column = " << peek().column << ", pos_ = " << pos_
                  << ", indent_level = " << indent_levels_.back() << std::endl;

        DeclarationParser decl_parser(tokens_, pos_);
        auto decl = decl_parser.parse(); // Call public parse() instead of private functions
        if (decl) {
            node->children.push_back(std::move(decl));
        } else {
            throw std::runtime_error("Failed to parse declaration at line " +
                                    std::to_string(peek().line) + ", column " +
                                    std::to_string(peek().column));
        }
    }

    std::cout << "DEBUG: Completed ModuleParser::parse, next token " << token_type_to_string(peek().type)
              << ", value = " << peek().value << ", line = " << peek().line
              << ", pos_ = " << pos_ << std::endl;
    return node;
}