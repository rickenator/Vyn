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
      expression_parser_(base_parser_),
      type_parser_(tokens_, current_pos_, file_path_, expression_parser_),
      statement_parser_(tokens_, current_pos_, 0, file_path_, type_parser_, expression_parser_),
      declaration_parser_(tokens_, current_pos_, file_path_, type_parser_, expression_parser_, statement_parser_),
      module_parser_(tokens_, current_pos_, file_path_, declaration_parser_) {}

std::unique_ptr<vyn::Module> Parser::parse_module() { 
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
    if (this->current_pos_ < this->tokens_.size() && this->tokens_[this->current_pos_].type != vyn::TokenType::END_OF_FILE) { 
        size_t final_check_pos = this->current_pos_;
        while (final_check_pos < this->tokens_.size() &&
               (this->tokens_[final_check_pos].type == vyn::TokenType::COMMENT || 
                this->tokens_[final_check_pos].type == vyn::TokenType::NEWLINE)) { 
            final_check_pos++;
        }
        if (final_check_pos < this->tokens_.size() && this->tokens_[final_check_pos].type != vyn::TokenType::END_OF_FILE) { 
            throw std::runtime_error("Parser::parse_module: Trailing tokens found after module parsing. Next token: " + vyn::token_type_to_string(this->tokens_[final_check_pos].type) + " at " + (this->tokens_[final_check_pos].location.filePath + ":" + std::to_string(this->tokens_[final_check_pos].location.line) + ":" + std::to_string(this->tokens_[final_check_pos].location.column)) + " in file " + this->file_path_);
        }
    } else if (this->current_pos_ >= this->tokens_.size() && !this->tokens_.empty() && this->tokens_.back().type != vyn::TokenType::END_OF_FILE) { 
        throw std::runtime_error("Parser::parse_module: Token stream did not end with END_OF_FILE (checked after parsing). File: " + this->file_path_); 
    }

    return module_node;
}

} // namespace vyn
