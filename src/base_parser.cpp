#include "vyn/parser.hpp"
#include <stdexcept>
#include <iostream>

BaseParser::BaseParser(const std::vector<Token>& tokens, size_t& pos) : tokens_(tokens), pos_(pos), indent_levels_({0}) {}

Token BaseParser::peek() const {
    return pos_ < tokens_.size() ? tokens_[pos_] : Token{TokenType::EOF_TOKEN, "", 0, 0};
}

void BaseParser::expect(TokenType type) {
    if (pos_ >= tokens_.size() || tokens_[pos_].type != type) {
        std::string found = pos_ < tokens_.size() ? token_type_to_string(tokens_[pos_].type) : "EOF";
        throw std::runtime_error("Expected " + token_type_to_string(type) + " but found " + found +
                                 " at line " + std::to_string(peek().line) + ", column " + std::to_string(peek().column));
    }
    pos_++;
}

void BaseParser::skip_comments() {
    while (pos_ < tokens_.size() && tokens_[pos_].type == TokenType::COMMENT) {
        std::cout << "DEBUG: Skipping comment at line " << tokens_[pos_].line 
                  << ", column " << tokens_[pos_].column << ", pos_ = " << pos_ << std::endl;
        pos_++;
    }
}

void BaseParser::skip_indents() {
    while (pos_ < tokens_.size() && (tokens_[pos_].type == TokenType::INDENT || tokens_[pos_].type == TokenType::DEDENT)) {
        if (tokens_[pos_].type == TokenType::INDENT) {
            indent_levels_.push_back(indent_levels_.back() + 1);
            std::cout << "DEBUG: Pushed indent level " << indent_levels_.back() << " at line " 
                      << tokens_[pos_].line << ", column " << tokens_[pos_].column << std::endl;
        } else if (tokens_[pos_].type == TokenType::DEDENT && indent_levels_.size() > 1) {
            indent_levels_.pop_back();
            std::cout << "DEBUG: Popped indent level to " << indent_levels_.back() << " at line " 
                      << tokens_[pos_].line << ", column " << tokens_[pos_].column << std::endl;
        }
        pos_++;
    }
}