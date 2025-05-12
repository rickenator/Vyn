#include "vyn/parser.hpp"
#include <stdexcept>
#include <iostream> // For debug output, consider removing for production

// Advances pos_ past comments and newlines
void BaseParser::skip_comments_and_newlines() {
    while (pos_ < tokens_.size() &&
           (tokens_[pos_].type == TokenType::COMMENT ||
            tokens_[pos_].type == TokenType::NEWLINE)) {
        pos_++;
    }
}

// Skips comments/newlines with a temp var, then returns tokens_[temp_pos]
const Token& BaseParser::peek() const {
    size_t temp_pos = pos_;
    // Skip comments and newlines for peeking
    while (temp_pos < tokens_.size() &&
           (tokens_[temp_pos].type == TokenType::COMMENT ||
            tokens_[temp_pos].type == TokenType::NEWLINE)) {
        temp_pos++;
    }
    if (temp_pos >= tokens_.size()) {
        // Assuming the last token in tokens_ is always EOF_TOKEN
        // or handle error if no EOF token is guaranteed.
        if (tokens_.empty()) { // Should not happen with a lexer that adds EOF
            throw std::runtime_error("Peek called on empty token stream");
        }
        return tokens_.back(); 
    }
    return tokens_[temp_pos];
}

// Calls skip_comments_and_newlines, then returns tokens_[pos_] and advances pos_
Token BaseParser::consume() {
    skip_comments_and_newlines(); // Advance pos_ past any leading comments/newlines
    if (pos_ >= tokens_.size()) {
        if (tokens_.empty()) { 
            throw std::runtime_error("Consume called on empty token stream");
        }
        // Attempting to consume beyond EOF. Return EOF token.
        return tokens_.back(); 
    }
    const Token& current_token = tokens_[pos_];
    pos_++; 
    return current_token; 
}

// Uses peek, then consumes if type matches, returns consumed token
Token BaseParser::expect(TokenType type) {
    const Token& next_token = peek(); 
    if (next_token.type != type) {
        throw std::runtime_error("Expected " + token_type_to_string(type) +
                                 " but found " + token_type_to_string(next_token.type) +
                                 " at line " + std::to_string(next_token.line) +
                                 ", column " + std::to_string(next_token.column));
    }
    return consume(); // consume() will advance past the validated token and any subsequent newlines/comments
}

// Uses peek, then consumes if type matches, returns bool
bool BaseParser::match(TokenType type) {
    if (peek().type == type) {
        consume(); 
        return true;
    }
    return false;
}

// Handles INDENT/DEDENT, may call skip_comments_and_newlines
void BaseParser::skip_indents_dedents() {
    // This function needs to be called explicitly where block structure is expected.
    // It consumes INDENT/DEDENT tokens and skips any comments/newlines around them.
    while (pos_ < tokens_.size()) {
        if (tokens_[pos_].type == TokenType::INDENT) {
            // indent_levels_.push_back(indent_levels_.back() + 1); // Indent level logic needs careful overall design
            pos_++;
            skip_comments_and_newlines(); // Skip any newlines/comments after INDENT
        } else if (tokens_[pos_].type == TokenType::DEDENT) {
            // if (!indent_levels_.empty()) { indent_levels_.pop_back(); } // Indent level logic
            pos_++;
            skip_comments_and_newlines(); // Skip any newlines/comments after DEDENT
        } else {
            break; // Not an INDENT or DEDENT token
        }
    }
}

// Removed old skip_raw_comments_and_newlines, skip_comments, skip_indents definitions.
// Their logic is now integrated into the above methods or handled by skip_comments_and_newlines.