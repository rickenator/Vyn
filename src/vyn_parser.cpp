// vyn_parser.cpp
#include <string>
#include <vector>
#include <stack>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>

// Catch2 test runner and macros
#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>

// Enable verbose debugging
#define VERBOSE_DEBUG

// Token types for Vyn lexer
enum class TokenType {
    KEYWORD_FN, KEYWORD_IF, KEYWORD_ELSE, KEYWORD_LET, KEYWORD_TEMPLATE, 
    KEYWORD_CLASS, KEYWORD_VAR, KEYWORD_RETURN, KEYWORD_FOR, KEYWORD_MUT,
    KEYWORD_MATCH, KEYWORD_IN, KEYWORD_SCOPED, IDENTIFIER, INT_LITERAL, 
    STRING_LITERAL, LBRACE, RBRACE, LPAREN, RPAREN, LBRACKET, RBRACKET, 
    SEMICOLON, COLON, COLONCOLON, COMMA, EQ, EQEQ, LT, GT, PLUS, MINUS, 
    DIVIDE, DOT, ARROW, AMPERSAND, BANG, AND, FAT_ARROW, DOTDOT, COMMENT, 
    INDENT, DEDENT, EOF_TOKEN
};

// Token structure
struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
};

// Utility function for converting TokenType to string
std::string token_type_to_string(TokenType type) {
    switch (type) {
        case TokenType::KEYWORD_FN: return "KEYWORD_FN";
        case TokenType::KEYWORD_IF: return "KEYWORD_IF";
        case TokenType::KEYWORD_ELSE: return "KEYWORD_ELSE";
        case TokenType::KEYWORD_LET: return "KEYWORD_LET";
        case TokenType::KEYWORD_TEMPLATE: return "KEYWORD_TEMPLATE";
        case TokenType::KEYWORD_CLASS: return "KEYWORD_CLASS";
        case TokenType::KEYWORD_VAR: return "KEYWORD_VAR";
        case TokenType::KEYWORD_RETURN: return "KEYWORD_RETURN";
        case TokenType::KEYWORD_FOR: return "KEYWORD_FOR";
        case TokenType::KEYWORD_MUT: return "KEYWORD_MUT";
        case TokenType::KEYWORD_MATCH: return "KEYWORD_MATCH";
        case TokenType::KEYWORD_IN: return "KEYWORD_IN";
        case TokenType::KEYWORD_SCOPED: return "KEYWORD_SCOPED";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::INT_LITERAL: return "INT_LITERAL";
        case TokenType::STRING_LITERAL: return "STRING_LITERAL";
        case TokenType::LBRACE: return "LBRACE";
        case TokenType::RBRACE: return "RBRACE";
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::LBRACKET: return "LBRACKET";
        case TokenType::RBRACKET: return "RBRACKET";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::COLON: return "COLON";
        case TokenType::COLONCOLON: return "COLONCOLON";
        case TokenType::COMMA: return "COMMA";
        case TokenType::EQ: return "EQ";
        case TokenType::EQEQ: return "EQEQ";
        case TokenType::LT: return "LT";
        case TokenType::GT: return "GT";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::DIVIDE: return "DIVIDE";
        case TokenType::DOT: return "DOT";
        case TokenType::ARROW: return "ARROW";
        case TokenType::AMPERSAND: return "AMPERSAND";
        case TokenType::BANG: return "BANG";
        case TokenType::AND: return "AND";
        case TokenType::FAT_ARROW: return "FAT_ARROW";
        case TokenType::DOTDOT: return "DOTDOT";
        case TokenType::COMMENT: return "COMMENT";
        case TokenType::INDENT: return "INDENT";
        case TokenType::DEDENT: return "DEDENT";
        case TokenType::EOF_TOKEN: return "EOF_TOKEN";
        default: return "UNKNOWN";
    }
}

// Lexer class
class Lexer {
public:
    Lexer(const std::string& source) : source_(source), pos_(0), line_(1), column_(1), brace_depth_(0) {
        indent_stack_.push(0); // Root level has 0 spaces
    }

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        bool was_newline = false;
        while (pos_ < source_.size()) {
            char c = source_[pos_];
            if (std::isspace(c)) {
                handle_whitespace(tokens);
                if (c == '\n') {
                    was_newline = true;
                } else {
                    was_newline = false;
                }
            } else {
                was_newline = false;
                if (std::isalpha(c) || c == '_') {
                    Token token = read_identifier_or_keyword();
                    tokens.push_back(token);
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
                    tokens.push_back({TokenType::PLUS, "+", line_, column_++});
                    pos_++;
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
                } else {
                    throw std::runtime_error("Unexpected character at line " + std::to_string(line_) + ", column " + std::to_string(column_));
                }
            }
        }
        // Emit DEDENT tokens for any remaining indentation levels at EOF
        while (indent_stack_.size() > 1) {
            tokens.push_back({TokenType::DEDENT, "", line_, column_});
            indent_stack_.pop();
            std::cout << "DEBUG: Emitted DEDENT (EOF), line=" << line_ << ", column=" << column_ << "\n";
        }
        tokens.push_back({TokenType::EOF_TOKEN, "", line_, column_});
        return tokens;
    }

private:
    void handle_whitespace(std::vector<Token>& tokens) {
        if (brace_depth_ > 0) {
            if (source_[pos_] == '\n') {
                line_++;
                column_ = 1;
                std::cout << "DEBUG: Newline in brace block, line=" << line_ << ", column=" << column_ << "\n";
            } else {
                column_++;
                std::cout << "DEBUG: Non-newline in brace block, line=" << line_ << ", column=" << column_ << ", char='" << source_[pos_] << "'\n";
            }
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
            std::cout << "DEBUG: Newline, line=" << line_ << ", column=" << column_ << "\n";
            // Check indentation of the next line
            int spaces = 0;
            size_t temp_pos = pos_;
            bool is_blank = false;
            while (temp_pos < source_.size() && source_[temp_pos] == '\n') {
                temp_pos++;
                line_++;
                column_ = 1;
                std::cout << "DEBUG: Skipped blank line, line=" << line_ << ", column=" << column_ << "\n";
            }
            while (temp_pos < source_.size() && source_[temp_pos] == ' ') {
                spaces++;
                temp_pos++;
            }
            if (temp_pos >= source_.size() || source_[temp_pos] == '\n') {
                is_blank = true;
            }
            int current_level = indent_stack_.top();
            if (!is_blank && spaces > current_level) {
                indent_stack_.push(spaces);
                tokens.push_back({TokenType::INDENT, "", line_, column_});
                std::cout << "DEBUG: Emitted INDENT, line=" << line_ << ", column=" << column_ << "\n";
            } else if (!is_blank && spaces < current_level) {
                while (indent_stack_.size() > 1 && indent_stack_.top() > spaces) {
                    tokens.push_back({TokenType::DEDENT, "", line_, column_});
                    indent_stack_.pop();
                    std::cout << "DEBUG: Emitted DEDENT, line=" << line_ << ", column=" << column_ << "\n";
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
            std::cout << "DEBUG: Processed leading spaces, line=" << line_ << ", column=" << column_ << "\n";
            return;
        }
        if (source_[pos_] == ' ') {
            column_++;
            pos_++;
            std::cout << "DEBUG: Non-leading space, line=" << line_ << ", column=" << column_ << "\n";
            return;
        }
        std::cout << "DEBUG: Non-whitespace, line=" << line_ << ", column=" << column_ << ", char='" << source_[pos_] << "'\n";
    }

    Token read_identifier_or_keyword() {
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
        if (value == "let") return {TokenType::KEYWORD_LET, value, line_, start_column};
        if (value == "template") return {TokenType::KEYWORD_TEMPLATE, value, line_, start_column};
        if (value == "class") return {TokenType::KEYWORD_CLASS, value, line_, start_column};
        if (value == "var") return {TokenType::KEYWORD_VAR, value, line_, start_column};
        if (value == "return") return {TokenType::KEYWORD_RETURN, value, line_, start_column};
        if (value == "for") return {TokenType::KEYWORD_FOR, value, line_, start_column};
        if (value == "mut") return {TokenType::KEYWORD_MUT, value, line_, start_column};
        if (value == "match") return {TokenType::KEYWORD_MATCH, value, line_, start_column};
        if (value == "in") return {TokenType::KEYWORD_IN, value, line_, start_column};
        if (value == "scoped") return {TokenType::KEYWORD_SCOPED, value, line_, start_column};
        return {TokenType::IDENTIFIER, value, line_, start_column};
    }

    Token read_number() {
        std::string value;
        int start_column = column_;
        while (pos_ < source_.size() && std::isdigit(source_[pos_])) {
            value += source_[pos_];
            pos_++;
            column_++;
        }
        return {TokenType::INT_LITERAL, value, line_, start_column};
    }

    Token read_string() {
        std::string value;
        int start_column = column_;
        pos_++; // Skip opening quote
        column_++;
        while (pos_ < source_.size() && source_[pos_] != '"') {
            value += source_[pos_];
            pos_++;
            column_++;
        }
        if (pos_ >= source_.size()) {
            throw std::runtime_error("Unterminated string at line " + std::to_string(line_) + ", column " + std::to_string(start_column));
        }
        pos_++; // Skip closing quote
        column_++;
        return {TokenType::STRING_LITERAL, value, line_, start_column};
    }

    Token read_comment() {
        std::string value;
        int start_column = column_;
        pos_ += 2; // Skip //
        column_ += 2;
        while (pos_ < source_.size() && source_[pos_] != '\n') {
            value += source_[pos_];
            pos_++;
            column_++;
        }
        return {TokenType::COMMENT, value, line_, start_column};
    }

    std::string source_;
    size_t pos_;
    int line_;
    int column_;
    int brace_depth_;
    std::stack<int> indent_stack_;
};

// Parser class
class Parser {
public:
    Parser(const std::vector<Token>& tokens) : tokens_(tokens), pos_(0) {}

    void parse_module() {
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Entering parse_module\n";
#endif
        while (pos_ < tokens_.size() && tokens_[pos_].type != TokenType::EOF_TOKEN) {
#ifdef VERBOSE_DEBUG
            std::cout << "DEBUG: parse_module: token=" << token_type_to_string(peek().type) << ", value=" << peek().value << ", line=" << peek().line << ", column=" << peek().column << "\n";
#endif
            if (tokens_[pos_].type == TokenType::COMMENT) {
                pos_++;
                continue;
            }
            if (peek().type == TokenType::KEYWORD_TEMPLATE) {
                parse_template();
            } else if (peek().type == TokenType::KEYWORD_CLASS) {
                parse_class();
            } else if (peek().type == TokenType::KEYWORD_FN) {
                parse_function();
            } else {
                throw std::runtime_error("Unexpected token at line " + std::to_string(peek().line) + ", column " + std::to_string(peek().column));
            }
        }
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Exiting parse_module\n";
#endif
    }

private:
    void parse_template() {
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Entering parse_template\n";
#endif
        expect(TokenType::KEYWORD_TEMPLATE);
        expect(TokenType::IDENTIFIER);
        if (peek().type == TokenType::LT) {
            expect(TokenType::LT);
            while (peek().type != TokenType::GT && peek().type != TokenType::EOF_TOKEN) {
#ifdef VERBOSE_DEBUG
                std::cout << "DEBUG: parse_template: generic param, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << "\n";
#endif
                if (peek().type == TokenType::IDENTIFIER) {
                    expect(TokenType::IDENTIFIER);
                    if (peek().type == TokenType::COLON) {
                        expect(TokenType::COLON);
                        expect(TokenType::IDENTIFIER);
                    }
                } else if (peek().type == TokenType::COMMA) {
                    expect(TokenType::COMMA);
                } else {
                    pos_++;
                }
            }
            expect(TokenType::GT);
        }
        parse_block();
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Exiting parse_template\n";
#endif
    }

    void parse_class() {
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Entering parse_class\n";
#endif
        expect(TokenType::KEYWORD_CLASS);
        expect(TokenType::IDENTIFIER);
        parse_block();
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Exiting parse_class\n";
#endif
    }

    void parse_function() {
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Entering parse_function\n";
#endif
        expect(TokenType::KEYWORD_FN);
        expect(TokenType::IDENTIFIER);
        expect(TokenType::LPAREN);
        while (peek().type != TokenType::RPAREN && peek().type != TokenType::EOF_TOKEN) {
#ifdef VERBOSE_DEBUG
            std::cout << "DEBUG: parse_function: param, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << "\n";
#endif
            if (peek().type == TokenType::AMPERSAND) {
                expect(TokenType::AMPERSAND);
                if (peek().type == TokenType::KEYWORD_MUT) {
                    expect(TokenType::KEYWORD_MUT);
                }
            }
            expect(TokenType::IDENTIFIER);
            if (peek().type == TokenType::COLON) {
                expect(TokenType::COLON);
                parse_type();
            }
            if (peek().type == TokenType::COMMA) {
                expect(TokenType::COMMA);
            }
        }
        expect(TokenType::RPAREN);
        if (peek().type == TokenType::ARROW) {
            expect(TokenType::ARROW);
            parse_type();
        }
        if (peek().type == TokenType::LBRACE || peek().type == TokenType::INDENT) {
            parse_block();
        }
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Exiting parse_function\n";
#endif
    }

    void parse_block() {
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Entering parse_block\n";
#endif
        if (peek().type == TokenType::LBRACE) {
            expect(TokenType::LBRACE);
            while (peek().type != TokenType::RBRACE && peek().type != TokenType::EOF_TOKEN) {
#ifdef VERBOSE_DEBUG
                std::cout << "DEBUG: parse_block: statement, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << "\n";
#endif
                if (peek().type == TokenType::COMMENT) {
                    pos_++;
                    continue;
                }
                parse_statement();
            }
            expect(TokenType::RBRACE);
        } else {
            expect(TokenType::INDENT);
            while (peek().type != TokenType::DEDENT && peek().type != TokenType::EOF_TOKEN) {
#ifdef VERBOSE_DEBUG
                std::cout << "DEBUG: parse_block: statement, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << "\n";
#endif
                if (peek().type == TokenType::COMMENT) {
                    pos_++;
                    continue;
                }
                parse_statement();
            }
            if (peek().type == TokenType::DEDENT) {
                expect(TokenType::DEDENT);
            }
        }
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Exiting parse_block\n";
#endif
    }

    void parse_statement() {
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Entering parse_statement, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << ", line=" << peek().line << ", column=" << peek().column << "\n";
#endif
        if (peek().type == TokenType::KEYWORD_LET) {
            expect(TokenType::KEYWORD_LET);
            expect(TokenType::IDENTIFIER);
            if (peek().type == TokenType::COLON) {
                expect(TokenType::COLON);
                parse_type();
            }
            expect(TokenType::EQ);
            parse_expression();
            if (peek().type == TokenType::SEMICOLON) {
                expect(TokenType::SEMICOLON);
            }
        } else if (peek().type == TokenType::KEYWORD_VAR) {
            expect(TokenType::KEYWORD_VAR);
            expect(TokenType::IDENTIFIER);
            expect(TokenType::COLON);
            parse_type();
            if (peek().type == TokenType::EQ) {
                expect(TokenType::EQ);
                parse_expression();
            }
            if (peek().type == TokenType::SEMICOLON) {
                expect(TokenType::SEMICOLON);
            }
        } else if (peek().type == TokenType::KEYWORD_IF) {
            expect(TokenType::KEYWORD_IF);
            parse_expression();
            parse_block();
            if (peek().type == TokenType::KEYWORD_ELSE) {
                expect(TokenType::KEYWORD_ELSE);
                parse_block();
            }
        } else if (peek().type == TokenType::KEYWORD_RETURN) {
            expect(TokenType::KEYWORD_RETURN);
            if (peek().type != TokenType::SEMICOLON && peek().type != TokenType::DEDENT && peek().type != TokenType::RBRACE) {
                parse_expression();
            }
            if (peek().type == TokenType::SEMICOLON) {
                expect(TokenType::SEMICOLON);
            }
        } else if (peek().type == TokenType::KEYWORD_FN) {
            parse_function();
        } else if (peek().type == TokenType::KEYWORD_CLASS) {
            parse_class();
        } else if (peek().type == TokenType::KEYWORD_MATCH) {
            parse_match();
        } else {
            parse_expression();
            if (peek().type == TokenType::SEMICOLON) {
                expect(TokenType::SEMICOLON);
            }
        }
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Exiting parse_statement\n";
#endif
    }

    void parse_pattern() {
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Entering parse_pattern, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << ", line=" << peek().line << ", column=" << peek().column << "\n";
#endif
        if (peek().type == TokenType::IDENTIFIER) {
            expect(TokenType::IDENTIFIER);  // e.g., Some, None
            if (peek().type == TokenType::LPAREN) {
                expect(TokenType::LPAREN);
                expect(TokenType::IDENTIFIER);  // e.g., v in Some(v)
                expect(TokenType::RPAREN);
            }
        } else {
            throw std::runtime_error("Expected pattern at line " + std::to_string(peek().line) + ", column " + std::to_string(peek().column));
        }
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Exiting parse_pattern\n";
#endif
    }

    void parse_match() {
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Entering parse_match, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << ", line=" << peek().line << ", column=" << peek().column << "\n";
#endif
        expect(TokenType::KEYWORD_MATCH);
        parse_expression();  // Parse the matched expression (e.g., x)
        if (peek().type == TokenType::LBRACE) {
            expect(TokenType::LBRACE);
            while (peek().type != TokenType::RBRACE && peek().type != TokenType::EOF_TOKEN) {
#ifdef VERBOSE_DEBUG
                std::cout << "DEBUG: parse_match: arm, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << ", line=" << peek().line << ", column=" << peek().column << "\n";
#endif
                parse_pattern();  // Parse pattern (e.g., Some(v), None)
                expect(TokenType::FAT_ARROW);  // =>
                parse_expression();  // Parse arm expression
                if (peek().type == TokenType::COMMA || peek().type == TokenType::SEMICOLON) {
                    expect(peek().type);  // Optional comma or semicolon
                }
            }
            expect(TokenType::RBRACE);
        } else {
            expect(TokenType::INDENT);
            while (peek().type != TokenType::DEDENT && peek().type != TokenType::EOF_TOKEN) {
#ifdef VERBOSE_DEBUG
                std::cout << "DEBUG: parse_match: arm, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << ", line=" << peek().line << ", column=" << peek().column << "\n";
#endif
                parse_pattern();  // Parse pattern (e.g., Some(v), None)
                expect(TokenType::FAT_ARROW);  // =>
                parse_expression();  // Parse arm expression
                if (peek().type == TokenType::COMMA || peek().type == TokenType::SEMICOLON) {
                    expect(peek().type);  // Optional comma or semicolon
                }
            }
            expect(TokenType::DEDENT);
        }
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Exiting parse_match\n";
#endif
    }

    void parse_expression() {
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Entering parse_expression, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << ", line=" << peek().line << ", column=" << peek().column << "\n";
#endif
        parse_primary_expression();
        while (peek().type == TokenType::LT || peek().type == TokenType::GT || 
               peek().type == TokenType::EQEQ || peek().type == TokenType::PLUS || 
               peek().type == TokenType::MINUS || peek().type == TokenType::DIVIDE || 
               peek().type == TokenType::AND) {
#ifdef VERBOSE_DEBUG
            std::cout << "DEBUG: parse_expression: operator, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << "\n";
#endif
            TokenType op = peek().type;
            expect(op);
            parse_primary_expression(); // Allow full primary expression, including field access
        }
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Exiting parse_expression\n";
#endif
    }

    void parse_primary_expression() {
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Entering parse_primary_expression, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << ", line=" << peek().line << ", column=" << peek().column << "\n";
#endif
        if (peek().type == TokenType::BANG) {
            expect(TokenType::BANG);
            parse_primary_expression();
        } else if (peek().type == TokenType::IDENTIFIER) {
            expect(TokenType::IDENTIFIER);
            // Handle field access, method calls, or array indexing
            while (peek().type == TokenType::DOT || peek().type == TokenType::LPAREN || peek().type == TokenType::LBRACKET) {
#ifdef VERBOSE_DEBUG
                std::cout << "DEBUG: parse_primary_expression: access, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << "\n";
#endif
                if (peek().type == TokenType::DOT) {
                    expect(TokenType::DOT);
                    expect(TokenType::IDENTIFIER);
                } else if (peek().type == TokenType::LPAREN) {
                    expect(TokenType::LPAREN);
                    while (peek().type != TokenType::RPAREN && peek().type != TokenType::EOF_TOKEN) {
                        parse_expression();
                        if (peek().type == TokenType::COMMA) {
                            expect(TokenType::COMMA);
                        }
                    }
                    expect(TokenType::RPAREN);
                } else if (peek().type == TokenType::LBRACKET) {
                    expect(TokenType::LBRACKET);
                    parse_expression();
                    expect(TokenType::RBRACKET);
                }
            }
            // Handle struct initialization or generic method calls
            if (pos_ < tokens_.size() && tokens_[pos_].type == TokenType::LBRACE) {
                // Struct initialization: IDENTIFIER LBRACE (IDENTIFIER (EQ | COLON expr)? (COMMA | SEMICOLON)?)* RBRACE
                expect(TokenType::LBRACE);
                while (peek().type != TokenType::RBRACE && peek().type != TokenType::EOF_TOKEN) {
#ifdef VERBOSE_DEBUG
                    std::cout << "DEBUG: parse_primary_expression: struct field, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << "\n";
#endif
                    expect(TokenType::IDENTIFIER); // Field name, e.g., "root"
                    if (peek().type == TokenType::EQ || peek().type == TokenType::COLON) {
                        expect(peek().type); // EQ or COLON
                        parse_expression(); // Field value, e.g., "0"
                    } // Else, shorthand: field name implies same-named variable
                    if (peek().type == TokenType::COMMA || peek().type == TokenType::SEMICOLON) {
                        expect(peek().type); // Optional comma or semicolon separator
                    }
                }
                expect(TokenType::RBRACE);
            } else if (pos_ < tokens_.size() && tokens_[pos_].type == TokenType::LT) {
                // Generic type method call: IDENTIFIER LT IDENTIFIER GT COLONCOLON IDENTIFIER
                expect(TokenType::LT);
                expect(TokenType::IDENTIFIER);
                expect(TokenType::GT);
                if (peek().type == TokenType::COLONCOLON) {
                    expect(TokenType::COLONCOLON);
                    expect(TokenType::IDENTIFIER); // Method name, e.g., "new"
                    if (peek().type == TokenType::LPAREN) {
                        expect(TokenType::LPAREN);
                        while (peek().type != TokenType::RPAREN && peek().type != TokenType::EOF_TOKEN) {
                            parse_expression();
                            if (peek().type == TokenType::COMMA) {
                                expect(TokenType::COMMA);
                            }
                        }
                        expect(TokenType::RPAREN);
                    }
                }
            }
        } else if (peek().type == TokenType::INT_LITERAL) {
            expect(TokenType::INT_LITERAL);
        } else if (peek().type == TokenType::STRING_LITERAL) {
            expect(TokenType::STRING_LITERAL);
        } else if (peek().type == TokenType::LBRACKET) {
            expect(TokenType::LBRACKET);
            parse_type(); // Type, e.g., "K"
            expect(TokenType::SEMICOLON);
            parse_expression(); // Size expression, e.g., "M-1"
            expect(TokenType::RBRACKET);
            if (peek().type == TokenType::LPAREN) {
                expect(TokenType::LPAREN);
                expect(TokenType::RPAREN);
            }
        } else {
            throw std::runtime_error("Expected primary expression at line " + std::to_string(peek().line) + ", column " + std::to_string(peek().column));
        }
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Exiting parse_primary_expression\n";
#endif
    }

    void parse_type() {
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Entering parse_type, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << ", line=" << peek().line << ", column=" << peek().column << "\n";
#endif
        if (peek().type == TokenType::LBRACKET) {
            expect(TokenType::LBRACKET);
            parse_type();  // Parse the inner type (e.g., K in [K; M-1])
            if (peek().type == TokenType::SEMICOLON) {
                expect(TokenType::SEMICOLON);
#ifdef VERBOSE_DEBUG
                std::cout << "DEBUG: parse_type: parsing array size expression\n";
#endif
                parse_expression();  // Parse the array size as an expression (e.g., M-1)
            }
            expect(TokenType::RBRACKET);
        } else if (peek().type == TokenType::AMPERSAND) {
            expect(TokenType::AMPERSAND);
            if (peek().type == TokenType::KEYWORD_MUT) {
                expect(TokenType::KEYWORD_MUT);
            }
            parse_type();  // Parse the type after & or &mut
        } else {
            expect(TokenType::IDENTIFIER);
            while (peek().type == TokenType::LBRACKET || peek().type == TokenType::LT) {
#ifdef VERBOSE_DEBUG
                std::cout << "DEBUG: parse_type: array/generic, token=" << token_type_to_string(peek().type) << ", value=" << peek().value << "\n";
#endif
                if (peek().type == TokenType::LBRACKET) {
                    expect(TokenType::LBRACKET);
                    if (peek().type == TokenType::SEMICOLON) {
                        expect(TokenType::SEMICOLON);
                        parse_expression();  // Parse the array size as an expression
                    } else if (peek().type == TokenType::INT_LITERAL || peek().type == TokenType::IDENTIFIER) {
                        parse_expression();  // Allow expression for size
                    }
                    expect(TokenType::RBRACKET);
                } else if (peek().type == TokenType::LT) {
                    expect(TokenType::LT);
                    while (peek().type != TokenType::GT && peek().type != TokenType::EOF_TOKEN) {
                        parse_type();
                        if (peek().type == TokenType::COMMA) {
                            expect(TokenType::COMMA);
                        }
                    }
                    expect(TokenType::GT);
                }
            }
        }
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: Exiting parse_type\n";
#endif
    }

    void expect(TokenType type) {
#ifdef VERBOSE_DEBUG
        std::cout << "DEBUG: expect: expecting=" << token_type_to_string(type) << ", found=" << token_type_to_string(peek().type) << ", value=" << peek().value << ", line=" << peek().line << ", column=" << peek().column << "\n";
#endif
        if (pos_ >= tokens_.size() || tokens_[pos_].type != type) {
            std::string found = pos_ < tokens_.size() ? token_type_to_string(tokens_[pos_].type) : "EOF";
            throw std::runtime_error("Expected " + token_type_to_string(type) + " but found " + found + 
                                     " at line " + std::to_string(peek().line) + ", column " + std::to_string(peek().column));
        }
        pos_++;
    }

    Token peek() const {
        return pos_ < tokens_.size() ? tokens_[pos_] : Token{TokenType::EOF_TOKEN, "", 0, 0};
    }

    std::vector<Token> tokens_;
    size_t pos_;
};

// Print tokens for debugging
void print_tokens(const std::vector<Token>& tokens) {
    for (const auto& token : tokens) {
        std::cout << "Token(type: " << token_type_to_string(token.type) << ", value: \"" << token.value
                  << "\", line: " << token.line << ", column " << token.column << ")\n";
    }
}

// Main function
int main(int argc, char* argv[]) {
    if (argc >= 2 && std::string(argv[1]) == "--test") {
        Catch::Session session;
        int test_argc = 1;
        char* test_argv[] = { argv[0], nullptr };
        if (argc > 2) {
            test_argc = argc - 1;
            test_argv[0] = argv[0];
            for (int i = 2; i < argc; ++i) {
                test_argv[i - 1] = argv[i];
            }
            test_argv[test_argc] = nullptr;
        }
        return session.run(test_argc, test_argv);
    }

    std::string source;
    if (argc == 2) {
        std::ifstream file(argv[1]);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open file " << argv[1] << std::endl;
            return 1;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        source = buffer.str();
        file.close();
    } else {
        std::stringstream buffer;
        buffer << std::cin.rdbuf();
        source = buffer.str();
    }

    try {
        Lexer lexer(source);
        auto tokens = lexer.tokenize();
        std::cout << "Tokens:\n";
        print_tokens(tokens);
        Parser parser(tokens);
        parser.parse_module();
        std::cout << "Parsing successful. AST generated (stubbed).\n";
    } catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

// Test cases
TEST_CASE("Print parser version", "[parser]") {
    std::cout << "Version: 0.1.7\n";
}

TEST_CASE("Lexer tokenizes indentation-based function", "[lexer]") {
    std::string code = "\nfn main()\n  let x = 1\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    REQUIRE(tokens.size() >= 7);
    REQUIRE(tokens[0].type == TokenType::KEYWORD_FN);
    REQUIRE(tokens[1].type == TokenType::IDENTIFIER);
    REQUIRE(tokens[2].type == TokenType::LPAREN);
    REQUIRE(tokens[3].type == TokenType::RPAREN);
    REQUIRE(tokens[4].type == TokenType::INDENT);
    REQUIRE(tokens[5].type == TokenType::KEYWORD_LET);
}

TEST_CASE("Lexer tokenizes brace-based function", "[lexer]") {
    std::string code = "\nfn main() {\n  let x = 1;\n}\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    REQUIRE(tokens.size() >= 9);
    REQUIRE(tokens[0].type == TokenType::KEYWORD_FN);
    REQUIRE(tokens[1].type == TokenType::IDENTIFIER);
    REQUIRE(tokens[4].type == TokenType::LBRACE);
    REQUIRE(tokens[5].type == TokenType::KEYWORD_LET);
    REQUIRE(tokens[tokens.size() - 2].type == TokenType::RBRACE);
}

TEST_CASE("Parser handles indentation-based function", "[parser]") {
    std::string code = "\nfn main()\n  let x = 1\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles brace-based function", "[parser]") {
    std::string code = "\nfn main() {\n  let x = 1;\n}\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Lexer rejects tabs", "[lexer]") {
    std::string code = "\nfn main()\n\tlet x = 1\n";
    Lexer lexer(code);
    REQUIRE_THROWS_AS(lexer.tokenize(), std::runtime_error);
    try {
        lexer.tokenize();
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()) == "Tabs not allowed at line 3, column 1");
    }
}

TEST_CASE("Parser rejects unmatched brace", "[parser]") {
    std::string code = "\nfn main() {\n  let x = 1;\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    REQUIRE_THROWS_AS(parser.parse_module(), std::runtime_error);
}

TEST_CASE("Parser handles btree.vyn subset with comments and division", "[parser]") {
    std::string code = R"(
template Comparable
  fn lt(&self, other: &Self) -> Bool
  fn eq(&self, other: &Self) -> Bool

class Node
  var keys: [Int; 2]
  var num_keys: Int
  fn new() -> Node
    let x = 5 / 2
)";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles template with function", "[parser]") {
    std::string code = R"(
template Comparable
  fn lt(&self, other: &Self) -> Bool
  fn eq(&self, other: &Self) -> Bool
)";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles array type with expression", "[parser]") {
    std::string code = "class Test { var keys: [K; M-1]; }";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles struct initialization with array initializer", "[parser]") {
    std::string code = R"(
template BTree<K, V, M>
  class Node {
    var keys: [K; M-1];
    fn new() -> Node {
      Node { keys = [K; M-1]() }
    }
  }
)";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    REQUIRE_NOTHROW(parser.parse_module());
}

TEST_CASE("Parser handles match expression", "[parser]") {
    std::string code = R"(
fn main() {
  match x {
    Some(v) => v;
    None => 0;
  }
}
)";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    REQUIRE_NOTHROW(parser.parse_module());
}