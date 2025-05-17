// Stub file for parser other implementations
#include "vyn/parser/parser.hpp"
#include "vyn/parser/ast.hpp"
#include "vyn/parser/token.hpp"

#include <stdexcept> // For std::runtime_error
#include <string> // Required for std::to_string

namespace vyn {

// Main Parser class implementation
Parser::Parser(const std::vector<vyn::token::Token>& tokens, std::string file_path)
    : tokens_(tokens),
      current_pos_(0),
      file_path_(file_path),
      base_parser_(tokens_, current_pos_, file_path_),
      expression_parser_(tokens_, current_pos_, file_path_),
      type_parser_(tokens_, current_pos_, file_path_, expression_parser_),
      statement_parser_(tokens_, current_pos_, 0, file_path_, type_parser_, expression_parser_),
      declaration_parser_(tokens_, current_pos_, file_path_, type_parser_, expression_parser_, statement_parser_),
      module_parser_(tokens_, current_pos_, file_path_, declaration_parser_) {}

std::unique_ptr<vyn::ast::Module> Parser::parse_module() { // Corrected return type
    auto module_node = this->module_parser_.parse(); 
    
    if (!module_node) {
        size_t temp_pos = this->current_pos_;
        while (temp_pos < this->tokens_.size() &&
               (this->tokens_[temp_pos].type == vyn::TokenType::COMMENT || 
                this->tokens_[temp_pos].type == vyn::TokenType::NEWLINE)) { 
            temp_pos++;
        }
        if (temp_pos < this->tokens_.size() && this->tokens_[temp_pos].type != vyn::TokenType::END_OF_FILE) { 
             throw std::runtime_error("Parser::parse_module: Failed to parse the entire module. Unexpected token at end: " + vyn::token_type_to_string(this->tokens_[temp_pos].type) + " in file " + this->file_path_); 
        } else if (temp_pos >= this->tokens_.size() && !this->tokens_.empty() && this->tokens_.back().type != vyn::TokenType::END_OF_FILE) { 
             throw std::runtime_error("Parser::parse_module: Token stream did not end with END_OF_FILE in file " + this->file_path_); 
        }
        if (!module_node && temp_pos < this->tokens_.size() && this->tokens_[temp_pos].type == vyn::TokenType::END_OF_FILE) { 
             throw std::runtime_error("Parser::parse_module: ModuleParser returned null for a seemingly valid EOF. file: " + this->file_path_); 
        }
    }
    // After module_parser_.parse() returns, current_pos_ should be updated by ModuleParser itself (or BaseParser methods it uses).
    // We need to ensure that current_pos_ reflects the state after ModuleParser is done.
    // If ModuleParser uses its own copy of current_pos_ or doesn\'t update the one in Parser, this check might be on an old value.
    // Assuming ModuleParser correctly updates the shared current_pos_ via BaseParser references or by returning it.
    // For now, let\'s use the Parser\'s current_pos_ which should have been updated.

    size_t final_check_pos = this->current_pos_; // Use the potentially updated current_pos_

    while (final_check_pos < this->tokens_.size() &&
           (this->tokens_[final_check_pos].type == vyn::TokenType::COMMENT ||
            this->tokens_[final_check_pos].type == vyn::TokenType::NEWLINE)) {
        final_check_pos++;
    }

    if (final_check_pos < this->tokens_.size() && this->tokens_[final_check_pos].type != vyn::TokenType::END_OF_FILE) {
        throw std::runtime_error("Parser::parse_module: Did not consume all tokens. Remaining token: " + vyn::token_type_to_string(this->tokens_[final_check_pos].type) + " (\"" + this->tokens_[final_check_pos].lexeme + "\") at " + this->tokens_[final_check_pos].location.toString() + " in file " + this->file_path_);
    }

    return module_node;
}

} // namespace vyn
