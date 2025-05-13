#include "vyn/parser.hpp"
#include "vyn/token.hpp" // For Vyn::Token, Vyn::TokenType, Vyn::token_type_to_string
#include "vyn/ast.hpp"   // For Vyn::AST::SourceLocation
#include <stdexcept>
#include <iostream> // For debug output, consider removing for production

namespace Vyn {

    Vyn::AST::SourceLocation BaseParser::current_location() const {
        if (pos_ < tokens_.size()) {
            const auto& token = tokens_[pos_];
            return Vyn::AST::SourceLocation(current_file_path_, token.line, token.column);
        } else if (!tokens_.empty()) {
            const auto& token = tokens_.back(); // Use last token for EOF location
            return Vyn::AST::SourceLocation(current_file_path_, token.line, token.column);
        }
        return Vyn::AST::SourceLocation(current_file_path_, 0, 0); // Default if no tokens
    }

    void BaseParser::skip_comments_and_newlines() {
        while (pos_ < tokens_.size() &&
               (tokens_[pos_].type == Vyn::TokenType::COMMENT ||
                tokens_[pos_].type == Vyn::TokenType::NEWLINE)) {
            pos_++;
        }
    }

    const Vyn::Token& BaseParser::peek() const {
        size_t temp_pos = pos_;
        while (temp_pos < tokens_.size() &&
               (tokens_[temp_pos].type == Vyn::TokenType::COMMENT ||
                tokens_[temp_pos].type == Vyn::TokenType::NEWLINE)) {
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

    Vyn::Token BaseParser::consume() {
        skip_comments_and_newlines(); 
        if (pos_ >= tokens_.size()) {
            if (tokens_.empty()) { 
                throw std::runtime_error("Consume called on empty token stream from file: " + current_file_path_);
            }
            // Return the last token, which should be EOF or the last valid token if stream ended abruptly.
            // This behavior might need refinement based on how EOF is handled.
            return tokens_.back(); 
        }
        const Vyn::Token& current_token = tokens_[pos_]; // Use Vyn::Token
        pos_++; 
        return current_token; 
    }

    Vyn::Token BaseParser::expect(Vyn::TokenType type) {
        const Vyn::Token& next_token = peek(); 
        if (next_token.type != type) {
            throw std::runtime_error("Expected " + token_type_to_string(type) +
                                     " but found " + token_type_to_string(next_token.type) +
                                     " at file " + current_file_path_ +
                                     ", line " + std::to_string(next_token.line) +
                                     ", column " + std::to_string(next_token.column));
        }
        return consume(); 
    }

    std::optional<Token> BaseParser::match(TokenType type) {
        if (check(type)) {
            return consume();
        }
        return std::nullopt;
    }

    std::optional<Token> BaseParser::match(const std::vector<TokenType>& types) {
        for (TokenType type : types) {
            if (check(type)) {
                return consume();
            }
        }
        return std::nullopt;
    }

    void BaseParser::skip_indents_dedents() {
        while (pos_ < tokens_.size()) {
            if (tokens_[pos_].type == Vyn::TokenType::INDENT) { // Use Vyn::TokenType
                pos_++;
                skip_comments_and_newlines(); 
            } else if (tokens_[pos_].type == Vyn::TokenType::DEDENT) { // Use Vyn::TokenType
                pos_++;
                skip_comments_and_newlines(); 
            } else {
                break; 
            }
        }
    }

    size_t BaseParser::get_current_pos() const {
        return pos_;\
    }

    bool BaseParser::IsAtEnd() const {
        // Ensure pos_ is within bounds before accessing tokens_[pos_]
        if (pos_ >= tokens_.size()) {
            return true;
        }
        return tokens_[pos_].type == Vyn::TokenType::EOF_TOKEN; // Corrected to EOF_TOKEN
    }

    bool BaseParser::IsDataType(const Vyn::Token &token) const {
        // Simplified: only checks for IDENTIFIER as type names.
        // Actual type checking (int, float, custom types) is more complex
        // and depends on language specification and semantic analysis.
        return token.type == Vyn::TokenType::IDENTIFIER;
    }

    bool BaseParser::IsLiteral(const Vyn::Token &token) const {
        return token.type == Vyn::TokenType::INT_LITERAL ||
               token.type == Vyn::TokenType::FLOAT_LITERAL ||
               token.type == Vyn::TokenType::STRING_LITERAL;
               // token.type == Vyn::TokenType::TRUE_KEYWORD || // TRUE_KEYWORD not in TokenType
               // token.type == Vyn::TokenType::FALSE_KEYWORD;  // FALSE_KEYWORD not in TokenType
    }

    bool BaseParser::IsOperator(const Vyn::Token &token) const {
        return token.type == Vyn::TokenType::PLUS ||
               token.type == Vyn::TokenType::MINUS ||
               token.type == Vyn::TokenType::MULTIPLY ||     // Corrected from STAR
               token.type == Vyn::TokenType::DIVIDE ||       // Corrected from SLASH
               token.type == Vyn::TokenType::EQEQ ||         // Corrected from EQUAL_EQUAL
               token.type == Vyn::TokenType::NOTEQ ||        // Corrected from BANG_EQUAL
               token.type == Vyn::TokenType::LT ||           // Corrected from LESS
               token.type == Vyn::TokenType::LTEQ ||         // Corrected from LESS_EQUAL
               token.type == Vyn::TokenType::GT ||           // Corrected from GREATER
               token.type == Vyn::TokenType::GTEQ;          // Corrected from GREATER_EQUAL
    }

    bool BaseParser::IsUnaryOperator(const Vyn::Token &token) const {
        return token.type == Vyn::TokenType::MINUS ||
               token.type == Vyn::TokenType::BANG;
    }
}