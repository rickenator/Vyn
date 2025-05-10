#include "vyn/parser.hpp"
#include <stdexcept>
#include <iostream>

ModuleParser::ModuleParser(const std::vector<Token>& tokens, size_t& pos) : BaseParser(tokens, pos) {}

std::unique_ptr<ASTNode> ModuleParser::parse() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Module);
    while (pos_ < tokens_.size() && tokens_[pos_].type != TokenType::EOF_TOKEN) {
        skip_comments();
        skip_indents();
        if (peek().type == TokenType::EOF_TOKEN) break;
        std::cout << "DEBUG: Processing token " << token_type_to_string(peek().type) 
                  << " at line " << peek().line << ", column " << peek().column 
                  << ", pos_ = " << pos_ << ", indent_level = " << indent_levels_.back() << std::endl;
        if (peek().type == TokenType::KEYWORD_IMPORT || peek().type == TokenType::KEYWORD_SMUGGLE) {
            StatementParser stmt_parser(tokens_, pos_);
            node->children.push_back(stmt_parser.parse());
        } else if (peek().type == TokenType::KEYWORD_TEMPLATE) {
            DeclarationParser decl_parser(tokens_, pos_);
            node->children.push_back(decl_parser.parse_template());
        } else if (peek().type == TokenType::KEYWORD_CLASS) {
            DeclarationParser decl_parser(tokens_, pos_);
            node->children.push_back(decl_parser.parse_class());
        } else if (peek().type == TokenType::KEYWORD_FN || peek().type == TokenType::KEYWORD_ASYNC) {
            DeclarationParser decl_parser(tokens_, pos_);
            node->children.push_back(decl_parser.parse_function());
        } else if (peek().type == TokenType::KEYWORD_VAR || peek().type == TokenType::KEYWORD_CONST) {
            StatementParser stmt_parser(tokens_, pos_);
            node->children.push_back(stmt_parser.parse());
        } else if (peek().type == TokenType::INDENT) {
            std::cout << "DEBUG: Handling INDENT token at line " << peek().line 
                      << ", column " << peek().column << ", pos_ = " << pos_ << std::endl;
            StatementParser stmt_parser(tokens_, pos_);
            node->children.push_back(stmt_parser.parse_block());
        } else {
            std::cout << "DEBUG: Skipping unexpected token " << token_type_to_string(peek().type) 
                      << " at line " << peek().line << ", column " << peek().column 
                      << ", pos_ = " << pos_ << std::endl;
            pos_++;
        }
    }
    return node;
}