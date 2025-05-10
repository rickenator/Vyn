#include "vyn/lexer.hpp"
#include <stdexcept>
#include <cctype>

Lexer::Lexer(const std::string& source) : source_(source), pos_(0), line_(1), column_(1), brace_depth_(0) {
    indent_stack_.push(0);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    bool was_newline = false;
    while (pos_ < source_.size()) {
        char c = source_[pos_];
        if (std::isspace(c)) {
            handle_whitespace(tokens);
            if (c == '\n') was_newline = true;
            else was_newline = false;
        } else {
            was_newline = false;
            if (std::isalpha(c) || c == '_') {
                tokens.push_back(read_identifier_or_keyword());
            } else if (std::isdigit(c)) {
                tokens.push_back(read_number());
            } else if (c == '"') {
                tokens.push_back(read_string());
            } else if (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '/') {
                tokens.push_back(read_comment());
            } else if (c == '/') {
                tokens.push_back({TokenType::DIVIDE, "/", line_, column_++});
                pos_++;
            } else if (c == '{') {
                tokens.push_back({TokenType::LBRACE, "{", line_, column_++});
                brace_depth_++;
                pos_++;
            } else if (c == '}') {
                tokens.push_back({TokenType::RBRACE, "}", line_, column_++});
                brace_depth_--;
                if (brace_depth_ < 0) {
                    throw std::runtime_error("Unmatched closing brace at line " + std::to_string(line_) + ", column " + std::to_string(column_));
                }
                pos_++;
            } else if (c == '(') {
                tokens.push_back({TokenType::LPAREN, "(", line_, column_++});
                pos_++;
            } else if (c == ')') {
                tokens.push_back({TokenType::RPAREN, ")", line_, column_++});
                pos_++;
            } else if (c == '[') {
                tokens.push_back({TokenType::LBRACKET, "[", line_, column_++});
                pos_++;
            } else if (c == ']') {
                tokens.push_back({TokenType::RBRACKET, "]", line_, column_++});
                pos_++;
            } else if (c == ';') {
                tokens.push_back({TokenType::SEMICOLON, ";", line_, column_++});
                pos_++;
            } else if (c == ':') {
                if (pos_ + 1 < source_.size() && source_[pos_ + 1] == ':') {
                    tokens.push_back({TokenType::COLONCOLON, "::", line_, column_});
                    column_ += 2;
                    pos_ += 2;
                } else {
                    tokens.push_back({TokenType::COLON, ":", line_, column_++});
                    pos_++;
                }
            } else if (c == ',') {
                tokens.push_back({TokenType::COMMA, ",", line_, column_++});
                pos_++;
            } else if (c == '=') {
                if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
                    tokens.push_back({TokenType::EQEQ, "==", line_, column_});
                    column_ += 2;
                    pos_ += 2;
                } else if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '>') {
                    tokens.push_back({TokenType::FAT_ARROW, "=>", line_, column_});
                    column_ += 2;
                    pos_ += 2;
                } else {
                    tokens.push_back({TokenType::EQ, "=", line_, column_++});
                    pos_++;
                }
            } else if (c == '<') {
                tokens.push_back({TokenType::LT, "<", line_, column_++});
                pos_++;
            } else if (c == '>') {
                tokens.push_back({TokenType::GT, ">", line_, column_++});
                pos_++;
            } else if (c == '+') {
                if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
                    tokens.push_back({TokenType::PLUS, "+=", line_, column_});
                    column_ += 2;
                    pos_ += 2;
                } else {
                    tokens.push_back({TokenType::PLUS, "+", line_, column_++});
                    pos_++;
                }
            } else if (c == '-') {
                if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '>') {
                    tokens.push_back({TokenType::ARROW, "->", line_, column_});
                    column_ += 2;
                    pos_ += 2;
                } else {
                    tokens.push_back({TokenType::MINUS, "-", line_, column_++});
                    pos_++;
                }
            } else if (c == '.') {
                if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '.') {
                    tokens.push_back({TokenType::DOTDOT, "..", line_, column_});
                    column_ += 2;
                    pos_ += 2;
                } else {
                    tokens.push_back({TokenType::DOT, ".", line_, column_++});
                    pos_++;
                }
            } else if (c == '&') {
                if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '&') {
                    tokens.push_back({TokenType::AND, "&&", line_, column_});
                    column_ += 2;
                    pos_ += 2;
                } else {
                    tokens.push_back({TokenType::AMPERSAND, "&", line_, column_++});
                    pos_++;
                }
            } else if (c == '!') {
                tokens.push_back({TokenType::BANG, "!", line_, column_++});
                pos_++;
            } else if (c == '@') {
                tokens.push_back({TokenType::AT, "@", line_, column_++});
                pos_++;
            } else {
                throw std::runtime_error("Unexpected character at line " + std::to_string(line_) + ", column " + std::to_string(column_));
            }
        }
    }
    while (indent_stack_.size() > 1) {
        tokens.push_back({TokenType::DEDENT, "", line_, column_});
        indent_stack_.pop();
    }
    tokens.push_back({TokenType::EOF_TOKEN, "", line_, column_});
    return tokens;
}

void Lexer::handle_whitespace(std::vector<Token>& tokens) {
    if (brace_depth_ > 0) {
        if (source_[pos_] == '\n') { line_++; column_ = 1; }
        else column_++;
        pos_++;
        return;
    }
    if (source_[pos_] == '\t') {
        throw std::runtime_error("Tabs not allowed at line " + std::to_string(line_) + ", column " + std::to_string(column_));
    }
    if (source_[pos_] == '\n') {
        line_++;
        column_ = 1;
        pos_++;
        int spaces = 0;
        size_t temp_pos = pos_;
        bool is_blank = false;
        while (temp_pos < source_.size() && source_[temp_pos] == '\n') {
            temp_pos++;
            line_++;
            column_ = 1;
        }
        while (temp_pos < source_.size() && source_[temp_pos] == ' ') {
            spaces++;
            temp_pos++;
        }
        if (temp_pos >= source_.size() || source_[temp_pos] == '\n') is_blank = true;
        int current_level = indent_stack_.top();
        if (!is_blank && spaces > current_level) {
            indent_stack_.push(spaces);
            tokens.push_back({TokenType::INDENT, "", line_, column_});
        } else if (!is_blank && spaces < current_level) {
            while (indent_stack_.size() > 1 && indent_stack_.top() > spaces) {
                tokens.push_back({TokenType::DEDENT, "", line_, column_});
                indent_stack_.pop();
            }
            if (indent_stack_.top() != spaces) {
                throw std::runtime_error("Inconsistent indentation at line " + std::to_string(line_) + ", column " + std::to_string(column_));
            }
        }
        return;
    }
    if (source_[pos_] == ' ' && column_ == 1) {
        while (pos_ < source_.size() && source_[pos_] == ' ') {
            pos_++;
            column_++;
        }
        return;
    }
    if (source_[pos_] == ' ') {
        column_++;
        pos_++;
        return;
    }
}

Token Lexer::read_identifier_or_keyword() {
    std::string value;
    int start_column = column_;
    while (pos_ < source_.size() && (std::isalnum(source_[pos_]) || source_[pos_] == '_')) {
        value += source_[pos_];
        pos_++;
        column_++;
    }
    if (value == "fn") return {TokenType::KEYWORD_FN, value, line_, start_column};
    if (value == "if") return {TokenType::KEYWORD_IF, value, line_, start_column};
    if (value == "else") return {TokenType::KEYWORD_ELSE, value, line_, start_column};
    if (value == "var") return {TokenType::KEYWORD_VAR, value, line_, start_column};
    if (value == "const") return {TokenType::KEYWORD_CONST, value, line_, start_column};
    if (value == "template") return {TokenType::KEYWORD_TEMPLATE, value, line_, start_column};
    if (value == "class") return {TokenType::KEYWORD_CLASS, value, line_, start_column};
    if (value == "return") return {TokenType::KEYWORD_RETURN, value, line_, start_column};
    if (value == "for") return {TokenType::KEYWORD_FOR, value, line_, start_column};
    if (value == "in") return {TokenType::KEYWORD_IN, value, line_, start_column};
    if (value == "scoped") return {TokenType::KEYWORD_SCOPED, value, line_, start_column};
    if (value == "import") return {TokenType::KEYWORD_IMPORT, value, line_, start_column};
    if (value == "smuggle") return {TokenType::KEYWORD_SMUGGLE, value, line_, start_column};
    if (value == "try") return {TokenType::KEYWORD_TRY, value, line_, start_column};
    if (value == "catch") return {TokenType::KEYWORD_CATCH, value, line_, start_column};
    if (value == "finally") return {TokenType::KEYWORD_FINALLY, value, line_, start_column};
    if (value == "defer") return {TokenType::KEYWORD_DEFER, value, line_, start_column};
    if (value == "async") return {TokenType::KEYWORD_ASYNC, value, line_, start_column};
    if (value == "await") return {TokenType::KEYWORD_AWAIT, value, line_, start_column};
    if (value == "match") return {TokenType::KEYWORD_MATCH, value, line_, start_column};
    if (value == "operator") return {TokenType::IDENTIFIER, value, line_, start_column};
    if (value == "while") return {TokenType::IDENTIFIER, value, line_, start_column};
    return {TokenType::IDENTIFIER, value, line_, start_column};
}

Token Lexer::read_number() {
    std::string value;
    int start_column = column_;
    while (pos_ < source_.size() && std::isdigit(source_[pos_])) {
        value += source_[pos_];
        pos_++;
        column_++;
    }
    return {TokenType::INT_LITERAL, value, line_, start_column};
}

Token Lexer::read_string() {
    std::string value;
    int start_column = column_;
    pos_++; column_++; // Skip opening quote
    while (pos_ < source_.size() && source_[pos_] != '"') {
        value += source_[pos_];
        pos_++;
        column_++;
    }
    if (pos_ >= source_.size()) {
        throw std::runtime_error("Unterminated string at line " + std::to_string(line_) + ", column " + std::to_string(start_column));
    }
    pos_++; column_++; // Skip closing quote
    return {TokenType::STRING_LITERAL, value, line_, start_column};
}

Token Lexer::read_comment() {
    std::string value;
    int start_column = column_;
    pos_ += 2; column_ += 2; // Skip //
    while (pos_ < source_.size() && source_[pos_] != '\n') {
        value += source_[pos_];
        pos_++;
        column_++;
    }
    return {TokenType::COMMENT, value, line_, start_column};
}