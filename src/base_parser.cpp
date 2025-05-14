#include "vyn/parser.hpp"
#include "vyn/token.hpp" 
#include "vyn/ast.hpp"   
#include <stdexcept>
#include <iostream> 

namespace vyn {

    vyn::SourceLocation BaseParser::current_location() const {
        if (pos_ < tokens_.size()) {
            const auto& token = tokens_[pos_];
            return vyn::SourceLocation(current_file_path_, token.location.line, token.location.column);
        } else if (!tokens_.empty()) {
            const auto& token = tokens_.back(); 
            return vyn::SourceLocation(current_file_path_, token.location.line, token.location.column);
        }
        return vyn::SourceLocation(current_file_path_, 0, 0); 
    }

    void BaseParser::skip_comments_and_newlines() {
        while (pos_ < tokens_.size() &&
               (tokens_[pos_].type == vyn::TokenType::COMMENT ||
                tokens_[pos_].type == vyn::TokenType::NEWLINE)) {
            pos_++;
        }
    }

    const vyn::token::Token& BaseParser::peek() const {
        size_t temp_pos = pos_;
        while (temp_pos < tokens_.size() &&
               (tokens_[temp_pos].type == vyn::TokenType::COMMENT ||
                tokens_[temp_pos].type == vyn::TokenType::NEWLINE)) {
            temp_pos++;
        }
        if (temp_pos >= tokens_.size()) {
            if (tokens_.empty()) { 
                throw std::runtime_error("Peek called on empty token stream from file: " + current_file_path_);
            }
            return tokens_.back(); 
        }
        return tokens_[temp_pos];
    }

    vyn::token::Token BaseParser::consume() {
        skip_comments_and_newlines(); 
        if (pos_ >= tokens_.size()) {
            if (tokens_.empty()) { 
                throw std::runtime_error("Consume called on empty token stream from file: " + current_file_path_);
            }
            return tokens_.back(); 
        }
        const vyn::token::Token& current_token = tokens_[pos_]; 
        pos_++; 
        return current_token; 
    }

    vyn::token::Token BaseParser::expect(vyn::TokenType type) {
        const vyn::token::Token& next_token = peek(); 
        if (next_token.type != type) {
            throw std::runtime_error("Expected " + vyn::token_type_to_string(type) +
                                     " but found " + vyn::token_type_to_string(next_token.type) +
                                     " at file " + current_file_path_ +
                                     ", line " + std::to_string(next_token.location.line) +
                                     ", column " + std::to_string(next_token.location.column));
        }
        return consume(); 
    }

    std::optional<vyn::token::Token> BaseParser::match(vyn::TokenType type) {
        if (check(type)) {
            return consume();
        }
        return std::nullopt;
    }

    std::optional<vyn::token::Token> BaseParser::match(const std::vector<vyn::TokenType>& types) {
        for (vyn::TokenType type : types) {
            if (check(type)) {
                return consume();
            }
        }
        return std::nullopt;
    }

    bool BaseParser::check(vyn::TokenType type) const {
        size_t temp_pos = pos_;
        while (temp_pos < tokens_.size() &&
               (tokens_[temp_pos].type == vyn::TokenType::COMMENT ||
                tokens_[temp_pos].type == vyn::TokenType::NEWLINE)) {
            temp_pos++;
        }
        if (temp_pos >= tokens_.size()) {
            return false; 
        }
        return tokens_[temp_pos].type == type;
    }

    bool BaseParser::check(const std::vector<vyn::TokenType>& types) const {
        for (vyn::TokenType type : types) {
            if (check(type)) {
                return true;
            }
        }
        return false;
    }

    void BaseParser::skip_indents_dedents() {
        while (pos_ < tokens_.size()) {
            if (tokens_[pos_].type == vyn::TokenType::INDENT) {
                pos_++;
                skip_comments_and_newlines(); 
            } else if (tokens_[pos_].type == vyn::TokenType::DEDENT) {
                pos_++;
                skip_comments_and_newlines(); 
            } else {
                break; 
            }
        }
    }

    size_t BaseParser::get_current_pos() const {
        return pos_;
    }

    bool BaseParser::IsAtEnd() const {
        if (pos_ >= tokens_.size()) {
            return true;
        }
        return tokens_[pos_].type == vyn::TokenType::END_OF_FILE;
    }

    bool BaseParser::IsDataType(const vyn::token::Token &token) const {
        return token.type == vyn::TokenType::IDENTIFIER;
    }

    bool BaseParser::IsLiteral(const vyn::token::Token &token) const {
        return token.type == vyn::TokenType::INT_LITERAL ||
               token.type == vyn::TokenType::FLOAT_LITERAL ||
               token.type == vyn::TokenType::STRING_LITERAL;
    }

    bool BaseParser::IsOperator(const vyn::token::Token &token) const {
        return token.type == vyn::TokenType::PLUS ||
               token.type == vyn::TokenType::MINUS ||
               token.type == vyn::TokenType::MULTIPLY ||     
               token.type == vyn::TokenType::DIVIDE ||       
               token.type == vyn::TokenType::EQEQ ||         
               token.type == vyn::TokenType::NOTEQ ||        
               token.type == vyn::TokenType::LT ||           
               token.type == vyn::TokenType::LTEQ ||         
               token.type == vyn::TokenType::GT ||           
               token.type == vyn::TokenType::GTEQ;          
    }

    bool BaseParser::IsUnaryOperator(const vyn::token::Token &token) const {
        return token.type == vyn::TokenType::MINUS ||
               token.type == vyn::TokenType::BANG;
    }
}