#include "vyn/parser/parser.hpp"
#include "vyn/parser/token.hpp"
#include "vyn/parser/ast.hpp"   
#include <stdexcept>
#include <iostream> 
#include <sstream>

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
        // Only skip COMMENT and NEWLINE, but do NOT skip INDENT or DEDENT
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
        #ifdef VERBOSE
        std::cerr << "[PEEK] " << vyn::token_type_to_string(tokens_[temp_pos].type)
                  << " (" << tokens_[temp_pos].lexeme << ") at " 
                  << tokens_[temp_pos].location.filePath << ":" 
                  << tokens_[temp_pos].location.line << ":" 
                  << tokens_[temp_pos].location.column << std::endl;
        #endif
        return tokens_[temp_pos];
    }

    const vyn::token::Token& BaseParser::peekNext() const {
        size_t temp_pos = pos_;
        // Skip first non-comment, non-newline token
        while (temp_pos < tokens_.size() &&
               (tokens_[temp_pos].type == vyn::TokenType::COMMENT ||
                tokens_[temp_pos].type == vyn::TokenType::NEWLINE)) {
            temp_pos++;
        }
        temp_pos++; // Skip the first token
        
        // Skip any comments or newlines after the first token
        while (temp_pos < tokens_.size() &&
               (tokens_[temp_pos].type == vyn::TokenType::COMMENT ||
                tokens_[temp_pos].type == vyn::TokenType::NEWLINE)) {
            temp_pos++;
        }
        
        if (temp_pos >= tokens_.size()) {
            if (tokens_.empty()) { 
                throw std::runtime_error("PeekNext called on empty token stream from file: " + current_file_path_);
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
        #ifdef VERBOSE
        std::cerr << "[CONSUME] " << vyn::token_type_to_string(current_token.type)
                  << " (" << current_token.lexeme << ") at " 
                  << current_token.location.filePath << ":" 
                  << current_token.location.line << ":" 
                  << current_token.location.column << std::endl;
        #endif
        pos_++; 
        return current_token; 
    }

    vyn::token::Token BaseParser::expect(vyn::TokenType type) {
        const vyn::token::Token& next_token = peek(); 
        #ifdef VERBOSE
        std::cerr << "[EXPECT] " << vyn::token_type_to_string(type)
                  << " checking against " << vyn::token_type_to_string(next_token.type)
                  << " (" << next_token.lexeme << ")" << std::endl;
        #endif
        if (next_token.type != type) {
            std::string error_msg = "Expected " + vyn::token_type_to_string(type) +
                                   " but found " + vyn::token_type_to_string(next_token.type) +
                                   " at file " + current_file_path_ +
                                   ", line " + std::to_string(next_token.location.line) +
                                   ", column " + std::to_string(next_token.location.column);
            #ifdef VERBOSE
            std::cerr << "[ERROR] " << error_msg << std::endl;
            #endif
            throw std::runtime_error(error_msg);
        }
        return consume(); 
    }

    vyn::token::Token BaseParser::expect(vyn::TokenType type, const std::string& lexeme) {
        const vyn::token::Token& next_token = peek();
        #ifdef VERBOSE
        std::cerr << "[EXPECT] " << vyn::token_type_to_string(type)
                  << " (\"" << lexeme << "\") checking against " << vyn::token_type_to_string(next_token.type)
                  << " (\"" << next_token.lexeme << "\")" << std::endl;
        #endif
        if (next_token.type != type || next_token.lexeme != lexeme) {
            std::string error_msg = "Expected " + vyn::token_type_to_string(type) +
                                   " (\"" + lexeme + "\") but found " + vyn::token_type_to_string(next_token.type) +
                                   " (\"" + next_token.lexeme + "\") at file " + current_file_path_ +
                                   ", line " + std::to_string(next_token.location.line) +
                                   ", column " + std::to_string(next_token.location.column);
            #ifdef VERBOSE
            std::cerr << "[ERROR] " << error_msg << std::endl;
            #endif
            throw std::runtime_error(error_msg);
        }
        return consume();
    }

    std::optional<vyn::token::Token> BaseParser::match(vyn::TokenType type) {
        if (check(type)) {
            return consume();
        }
        return std::nullopt;
    }

    std::optional<vyn::token::Token> BaseParser::match(vyn::TokenType type, const std::string& lexeme) {
        if (check(type) && peek().lexeme == lexeme) {
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

    [[noreturn]] std::runtime_error BaseParser::error(const token::Token& token, const std::string& message) const {
        // Ensure that the location is correctly converted to a string.
        // If SourceLocation has a method like toString() or similar, use that.
        // For example, if it has a toString() method:
        std::string location_str;
        // Assuming SourceLocation has line and column members.
        // Adjust if your SourceLocation struct has different members or a different string conversion method.
        // location_str = "line " + std::to_string(token.location.line) + ", column " + std::to_string(token.location.column);
        
        std::string error_message = "Error at " + token.location.toString() + ": " + message + " (found '" + token.lexeme + "')";
        throw std::runtime_error(error_message);
    }

    const vyn::token::Token& BaseParser::previous_token() const {
        if (pos_ == 0) {
            throw std::runtime_error("previous_token() called at the beginning of the token stream.");
        }
        // This needs to be smarter if skip_comments_and_newlines can be called between consume() and previous_token()
        // For now, assume it gives the token right before the current pos_ (which might be a comment/newline if not careful)
        // A better approach might be to store the actual last consumed non-comment/newline token.
        // However, current usage in TypeParser implies previous_token() is called right after a match() or consume() that returned a significant token.
        size_t prev_pos = pos_ -1;
        while(prev_pos > 0 && (tokens_[prev_pos].type == vyn::TokenType::COMMENT || tokens_[prev_pos].type == vyn::TokenType::NEWLINE)) {
            prev_pos--;
        }
        return tokens_[prev_pos];
    }

    void BaseParser::put_back_token() {
        if (pos_ == 0) {
            throw std::runtime_error("put_back_token() called at the beginning of the token stream.");
        }
        // This needs to be smarter to skip over comments/newlines if the token being put back was preceded by them.
        // For now, simple decrement. This could misalign if not used carefully.
        // A robust solution would involve storing the *actual* last consumed token's original position.
        // Current usage in TypeParser seems to be after a match() on LBRACKET, then another match() on RBRACKET fails.
        // In that scenario, pos_ would have advanced past LBRACKET. We want to go back to before LBRACKET.
        // This means we need to find the position of the token that was consumed by the first match().
        // This simple decrement is likely incorrect for the intended use.
        // Let's assume for now that `pos_` points to the token *after* the one we want to put back.
        // So, decrementing `pos_` makes `peek()` look at the token we effectively "put back".
        // However, if there were skipped comments/newlines, this is not robust.
        // A better `put_back_token` would require knowing how many tokens were consumed by the operation we are undoing.
        // For now, let's try a simple decrement, but this is a known weak point.
        
        // Find the last non-comment/newline token before current pos_
        size_t target_pos = pos_ -1;
        while(target_pos > 0 && (tokens_[target_pos].type == vyn::TokenType::COMMENT || tokens_[target_pos].type == vyn::TokenType::NEWLINE)) {
            target_pos--;
        }
        // Now, we need to ensure that pos_ is set such that the *next* call to peek() or consume() will see tokens_[target_pos].
        // This means pos_ should be target_pos.
        pos_ = target_pos;
    }

} // namespace vyn