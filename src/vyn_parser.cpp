#include <string>
#include <vector>
#include <stack>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>

// Catch2 test runner and macros
#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>

// Verbose debugging disabled
// #define VERBOSE_DEBUG

// Token types
enum class TokenType {
    KEYWORD_FN, KEYWORD_IF, KEYWORD_ELSE, KEYWORD_VAR, KEYWORD_CONST,
    KEYWORD_TEMPLATE, KEYWORD_CLASS, KEYWORD_RETURN, KEYWORD_FOR, KEYWORD_IN,
    KEYWORD_SCOPED, KEYWORD_IMPORT, KEYWORD_SMUGGLE, KEYWORD_TRY, KEYWORD_CATCH,
    KEYWORD_FINALLY, KEYWORD_DEFER, KEYWORD_ASYNC, KEYWORD_AWAIT, KEYWORD_MATCH,
    IDENTIFIER, INT_LITERAL, STRING_LITERAL, LBRACE, RBRACE, LPAREN, RPAREN,
    LBRACKET, RBRACKET, SEMICOLON, COLON, COLONCOLON, COMMA, EQ, EQEQ, LT, GT,
    PLUS, MINUS, DIVIDE, DOT, ARROW, AMPERSAND, BANG, AND, FAT_ARROW, DOTDOT,
    AT, COMMENT, INDENT, DEDENT, EOF_TOKEN
};

// Token structure
struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
};

// Utility function for token type to string
std::string token_type_to_string(TokenType type) {
    switch (type) {
        case TokenType::KEYWORD_FN: return "KEYWORD_FN";
        case TokenType::KEYWORD_IF: return "KEYWORD_IF";
        case TokenType::KEYWORD_ELSE: return "KEYWORD_ELSE";
        case TokenType::KEYWORD_VAR: return "KEYWORD_VAR";
        case TokenType::KEYWORD_CONST: return "KEYWORD_CONST";
        case TokenType::KEYWORD_TEMPLATE: return "KEYWORD_TEMPLATE";
        case TokenType::KEYWORD_CLASS: return "KEYWORD_CLASS";
        case TokenType::KEYWORD_RETURN: return "KEYWORD_RETURN";
        case TokenType::KEYWORD_FOR: return "KEYWORD_FOR";
        case TokenType::KEYWORD_IN: return "KEYWORD_IN";
        case TokenType::KEYWORD_SCOPED: return "KEYWORD_SCOPED";
        case TokenType::KEYWORD_IMPORT: return "KEYWORD_IMPORT";
        case TokenType::KEYWORD_SMUGGLE: return "KEYWORD_SMUGGLE";
        case TokenType::KEYWORD_TRY: return "KEYWORD_TRY";
        case TokenType::KEYWORD_CATCH: return "KEYWORD_CATCH";
        case TokenType::KEYWORD_FINALLY: return "KEYWORD_FINALLY";
        case TokenType::KEYWORD_DEFER: return "KEYWORD_DEFER";
        case TokenType::KEYWORD_ASYNC: return "KEYWORD_ASYNC";
        case TokenType::KEYWORD_AWAIT: return "KEYWORD_AWAIT";
        case TokenType::KEYWORD_MATCH: return "KEYWORD_MATCH";
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
        case TokenType::AT: return "AT";
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
        indent_stack_.push(0);
    }

    std::vector<Token> tokenize();

private:
    void handle_whitespace(std::vector<Token>& tokens);
    Token read_identifier_or_keyword();
    Token read_number();
    Token read_string();
    Token read_comment();

    std::string source_;
    size_t pos_;
    int line_;
    int column_;
    int brace_depth_;
    std::stack<int> indent_stack_;
};

// AST Node structure
struct ASTNode {
    enum class Kind { Module, Template, Class, Function, Statement, Expression, Type };
    Kind kind;
    std::vector<std::unique_ptr<ASTNode>> children;
    Token token;
    ASTNode(Kind k, Token t = {}) : kind(k), token(t) {}
};

// Base Parser class
class BaseParser {
protected:
    BaseParser(const std::vector<Token>& tokens) : tokens_(tokens), pos_(0) {}

    Token peek() const {
        return pos_ < tokens_.size() ? tokens_[pos_] : Token{TokenType::EOF_TOKEN, "", 0, 0};
    }

    void expect(TokenType type) {
        if (pos_ >= tokens_.size() || tokens_[pos_].type != type) {
            std::string found = pos_ < tokens_.size() ? token_type_to_string(tokens_[pos_].type) : "EOF";
            throw std::runtime_error("Expected " + token_type_to_string(type) + " but found " + found +
                                     " at line " + std::to_string(peek().line) + ", column " + std::to_string(peek().column));
        }
        pos_++;
    }

    void skip_comments() {
        while (pos_ < tokens_.size() && tokens_[pos_].type == TokenType::COMMENT) pos_++;
    }

    void skip_indents() {
        while (pos_ < tokens_.size() && (tokens_[pos_].type == TokenType::INDENT || tokens_[pos_].type == TokenType::DEDENT)) pos_++;
    }

    std::vector<Token> tokens_;
    size_t pos_;
};

// Parser class declarations
class ExpressionParser : public BaseParser {
public:
    ExpressionParser(const std::vector<Token>& tokens, size_t& pos) : BaseParser(tokens), shared_pos_(pos) {}
    std::unique_ptr<ASTNode> parse();

private:
    std::unique_ptr<ASTNode> parse_primary();
    size_t& shared_pos_;
};

class TypeParser : public BaseParser {
public:
    TypeParser(const std::vector<Token>& tokens, size_t& pos) : BaseParser(tokens), shared_pos_(pos) {}
    std::unique_ptr<ASTNode> parse();

private:
    size_t& shared_pos_;
};

class StatementParser : public BaseParser {
public:
    StatementParser(const std::vector<Token>& tokens, size_t& pos) : BaseParser(tokens), shared_pos_(pos) {}
    std::unique_ptr<ASTNode> parse();
    std::unique_ptr<ASTNode> parse_block();

private:
    void parse_match(std::unique_ptr<ASTNode>& node);
    void parse_pattern(std::unique_ptr<ASTNode>& node);
    size_t& shared_pos_;
};

class DeclarationParser : public BaseParser {
public:
    DeclarationParser(const std::vector<Token>& tokens, size_t& pos) : BaseParser(tokens), shared_pos_(pos) {}
    std::unique_ptr<ASTNode> parse_template();
    std::unique_ptr<ASTNode> parse_class();
    std::unique_ptr<ASTNode> parse_function();

private:
    size_t& shared_pos_;
};

class ModuleParser : public BaseParser {
public:
    ModuleParser(const std::vector<Token>& tokens) : BaseParser(tokens) {}
    std::unique_ptr<ASTNode> parse();
};

// Lexer implementations
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

// Expression Parser implementations
std::unique_ptr<ASTNode> ExpressionParser::parse() {
    if (peek().type == TokenType::KEYWORD_IF) {
        auto node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek());
        expect(TokenType::KEYWORD_IF);
        node->children.push_back(parse());
        StatementParser stmt_parser(tokens_, shared_pos_);
        node->children.push_back(stmt_parser.parse_block());
        if (peek().type == TokenType::KEYWORD_ELSE) {
            expect(TokenType::KEYWORD_ELSE);
            if (peek().type == TokenType::LBRACE || peek().type == TokenType::INDENT) {
                node->children.push_back(stmt_parser.parse_block());
            } else {
                node->children.push_back(parse());
            }
        }
        return node;
    } else if (peek().type == TokenType::LBRACKET) {
        auto node = std::make_unique<ASTNode>(ASTNode::Kind::Expression);
        expect(TokenType::LBRACKET);
        if (peek().type != TokenType::RBRACKET) {
            node->children.push_back(parse());
        }
        if (peek().type == TokenType::KEYWORD_FOR) {
            expect(TokenType::KEYWORD_FOR);
            auto var_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek());
            expect(TokenType::IDENTIFIER);
            var_node->token = tokens_[pos_ - 1];
            node->children.push_back(std::move(var_node));
            expect(TokenType::KEYWORD_IN);
            auto range_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression);
            range_node->children.push_back(parse());
            if (peek().type == TokenType::DOTDOT) {
                range_node->token = peek();
                expect(TokenType::DOTDOT);
                range_node->children.push_back(parse());
            }
            node->children.push_back(std::move(range_node));
        } else {
            while (peek().type != TokenType::RBRACKET && peek().type != TokenType::EOF_TOKEN) {
                if (peek().type == TokenType::COMMA) {
                    expect(TokenType::COMMA);
                    if (peek().type != TokenType::RBRACKET) node->children.push_back(parse());
                } else if (!node->children.empty()) {
                    node->children.push_back(parse());
                }
            }
        }
        expect(TokenType::RBRACKET);
        return node;
    }
    auto node = parse_primary();
    while (peek().type == TokenType::LT || peek().type == TokenType::GT ||
           peek().type == TokenType::EQEQ || peek().type == TokenType::PLUS ||
           peek().type == TokenType::MINUS || peek().type == TokenType::DIVIDE ||
           peek().type == TokenType::AND) {
        auto op_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek());
        expect(peek().type);
        op_node->children.push_back(std::move(node));
        op_node->children.push_back(parse_primary());
        node = std::move(op_node);
    }
    return node;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_primary() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Expression);
    if (peek().type == TokenType::BANG) {
        node->token = peek();
        expect(TokenType::BANG);
        node->children.push_back(parse_primary());
    } else if (peek().type == TokenType::KEYWORD_AWAIT) {
        node->token = peek();
        expect(TokenType::KEYWORD_AWAIT);
        node->children.push_back(parse_primary());
    } else if (peek().type == TokenType::IDENTIFIER) {
        node->token = peek();
        expect(TokenType::IDENTIFIER);
        while (peek().type == TokenType::DOT || peek().type == TokenType::LPAREN || peek().type == TokenType::LBRACKET) {
            if (peek().type == TokenType::DOT) {
                expect(TokenType::DOT);
                auto field_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek());
                expect(TokenType::IDENTIFIER);
                node->children.push_back(std::move(field_node));
            } else if (peek().type == TokenType::LPAREN) {
                expect(TokenType::LPAREN);
                while (peek().type != TokenType::RPAREN && peek().type != TokenType::EOF_TOKEN) {
                    node->children.push_back(parse());
                    if (peek().type == TokenType::COMMA) expect(TokenType::COMMA);
                }
                expect(TokenType::RPAREN);
            } else if (peek().type == TokenType::LBRACKET) {
                expect(TokenType::LBRACKET);
                node->children.push_back(parse());
                expect(TokenType::RBRACKET);
            }
        }
        if (peek().type == TokenType::LBRACE) {
            expect(TokenType::LBRACE);
            while (peek().type != TokenType::RBRACE && peek().type != TokenType::EOF_TOKEN) {
                auto field_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek());
                expect(TokenType::IDENTIFIER);
                if (peek().type == TokenType::EQ || peek().type == TokenType::COLON) {
                    expect(peek().type);
                    field_node->children.push_back(parse());
                }
                node->children.push_back(std::move(field_node));
                if (peek().type == TokenType::COMMA || peek().type == TokenType::SEMICOLON) expect(peek().type);
            }
            expect(TokenType::RBRACE);
        } else if (peek().type == TokenType::LT) {
            expect(TokenType::LT);
            TypeParser type_parser(tokens_, shared_pos_);
            node->children.push_back(type_parser.parse());
            expect(TokenType::GT);
            if (peek().type == TokenType::COLONCOLON) {
                expect(TokenType::COLONCOLON);
                node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
                expect(TokenType::IDENTIFIER);
                if (peek().type == TokenType::LPAREN) {
                    expect(TokenType::LPAREN);
                    while (peek().type != TokenType::RPAREN && peek().type != TokenType::EOF_TOKEN) {
                        node->children.push_back(parse());
                        if (peek().type == TokenType::COMMA) expect(TokenType::COMMA);
                    }
                    expect(TokenType::RPAREN);
                }
            }
        }
    } else if (peek().type == TokenType::INT_LITERAL) {
        node->token = peek();
        expect(TokenType::INT_LITERAL);
    } else if (peek().type == TokenType::STRING_LITERAL) {
        node->token = peek();
        expect(TokenType::STRING_LITERAL);
    } else if (peek().type == TokenType::LBRACKET) {
        expect(TokenType::LBRACKET);
        TypeParser type_parser(tokens_, shared_pos_);
        node->children.push_back(type_parser.parse());
        expect(TokenType::SEMICOLON);
        node->children.push_back(parse());
        expect(TokenType::RBRACKET);
        if (peek().type == TokenType::LPAREN) {
            expect(TokenType::LPAREN);
            expect(TokenType::RPAREN);
        }
    } else {
        throw std::runtime_error("Expected primary expression at line " + std::to_string(peek().line) + ", column " + std::to_string(peek().column));
    }
    return node;
}

// Type Parser implementations
std::unique_ptr<ASTNode> TypeParser::parse() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Type);
    if (peek().type == TokenType::IDENTIFIER && peek().value == "ref") {
        node->token = peek();
        expect(TokenType::IDENTIFIER);
        expect(TokenType::LT);
        node->children.push_back(parse());
        expect(TokenType::GT);
    } else if (peek().type == TokenType::LBRACKET) {
        expect(TokenType::LBRACKET);
        node->children.push_back(parse());
        if (peek().type == TokenType::SEMICOLON) {
            expect(TokenType::SEMICOLON);
            ExpressionParser expr_parser(tokens_, shared_pos_);
            node->children.push_back(expr_parser.parse());
        }
        expect(TokenType::RBRACKET);
    } else if (peek().type == TokenType::AMPERSAND) {
        node->token = peek();
        expect(TokenType::AMPERSAND);
        if (peek().type == TokenType::IDENTIFIER && peek().value == "mut") {
            node->token = peek();
            expect(TokenType::IDENTIFIER);
        }
        node->children.push_back(parse());
    } else {
        node->token = peek();
        expect(TokenType::IDENTIFIER);
        while (peek().type == TokenType::LBRACKET || peek().type == TokenType::LT) {
            if (peek().type == TokenType::LBRACKET) {
                expect(TokenType::LBRACKET);
                if (peek().type == TokenType::SEMICOLON) {
                    expect(TokenType::SEMICOLON);
                    ExpressionParser expr_parser(tokens_, shared_pos_);
                    node->children.push_back(expr_parser.parse());
                } else if (peek().type == TokenType::INT_LITERAL || peek().type == TokenType::IDENTIFIER) {
                    ExpressionParser expr_parser(tokens_, shared_pos_);
                    node->children.push_back(expr_parser.parse());
                }
                expect(TokenType::RBRACKET);
            } else if (peek().type == TokenType::LT) {
                expect(TokenType::LT);
                while (peek().type != TokenType::GT && peek().type != TokenType::EOF_TOKEN) {
                    node->children.push_back(parse());
                    if (peek().type == TokenType::COMMA) expect(TokenType::COMMA);
                }
                expect(TokenType::GT);
            }
        }
    }
    return node;
}

// Statement Parser implementations
std::unique_ptr<ASTNode> StatementParser::parse() {
    skip_comments();
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Statement);
    if (peek().type == TokenType::KEYWORD_CONST) {
        node->token = peek();
        expect(TokenType::KEYWORD_CONST);
        node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
        expect(TokenType::IDENTIFIER);
        if (peek().type == TokenType::COLON) {
            expect(TokenType::COLON);
            TypeParser type_parser(tokens_, shared_pos_);
            node->children.push_back(type_parser.parse());
        }
        expect(TokenType::EQ);
        ExpressionParser expr_parser(tokens_, shared_pos_);
        node->children.push_back(expr_parser.parse());
        if (peek().type == TokenType::SEMICOLON) expect(TokenType::SEMICOLON);
    } else if (peek().type == TokenType::KEYWORD_VAR) {
        node->token = peek();
        expect(TokenType::KEYWORD_VAR);
        node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
        expect(TokenType::IDENTIFIER);
        if (peek().type == TokenType::COLON) {
            expect(TokenType::COLON);
            TypeParser type_parser(tokens_, shared_pos_);
            node->children.push_back(type_parser.parse());
        }
        if (peek().type == TokenType::EQ) {
            expect(TokenType::EQ);
            ExpressionParser expr_parser(tokens_, shared_pos_);
            node->children.push_back(expr_parser.parse());
        }
        if (peek().type == TokenType::SEMICOLON) expect(TokenType::SEMICOLON);
    } else if (peek().type == TokenType::KEYWORD_IF) {
        node->token = peek();
        expect(TokenType::KEYWORD_IF);
        ExpressionParser expr_parser(tokens_, shared_pos_);
        node->children.push_back(expr_parser.parse());
        node->children.push_back(parse_block());
        if (peek().type == TokenType::KEYWORD_ELSE) {
            expect(TokenType::KEYWORD_ELSE);
            node->children.push_back(parse_block());
        }
    } else if (peek().type == TokenType::KEYWORD_FOR) {
        node->token = peek();
        expect(TokenType::KEYWORD_FOR);
        node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
        expect(TokenType::IDENTIFIER);
        expect(TokenType::KEYWORD_IN);
        ExpressionParser expr_parser(tokens_, shared_pos_);
        node->children.push_back(expr_parser.parse());
        node->children.push_back(parse_block());
    } else if (peek().type == TokenType::KEYWORD_RETURN) {
        node->token = peek();
        expect(TokenType::KEYWORD_RETURN);
        if (peek().type != TokenType::SEMICOLON && peek().type != TokenType::DEDENT && peek().type != TokenType::RBRACE) {
            ExpressionParser expr_parser(tokens_, shared_pos_);
            node->children.push_back(expr_parser.parse());
        }
        if (peek().type == TokenType::SEMICOLON) expect(TokenType::SEMICOLON);
    } else if (peek().type == TokenType::KEYWORD_DEFER) {
        node->token = peek();
        expect(TokenType::KEYWORD_DEFER);
        ExpressionParser expr_parser(tokens_, shared_pos_);
        node->children.push_back(expr_parser.parse());
        if (peek().type == TokenType::SEMICOLON) expect(TokenType::SEMICOLON);
    } else if (peek().type == TokenType::KEYWORD_TRY) {
        node->token = peek();
        expect(TokenType::KEYWORD_TRY);
        node->children.push_back(parse_block());
        while (peek().type == TokenType::KEYWORD_CATCH) {
            auto catch_node = std::make_unique<ASTNode>(ASTNode::Kind::Statement, peek());
            expect(TokenType::KEYWORD_CATCH);
            expect(TokenType::LPAREN);
            catch_node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
            expect(TokenType::IDENTIFIER);
            expect(TokenType::COLON);
            TypeParser type_parser(tokens_, shared_pos_);
            catch_node->children.push_back(type_parser.parse());
            expect(TokenType::RPAREN);
            catch_node->children.push_back(parse_block());
            node->children.push_back(std::move(catch_node));
        }
        if (peek().type == TokenType::KEYWORD_FINALLY) {
            expect(TokenType::KEYWORD_FINALLY);
            node->children.push_back(parse_block());
        }
    } else if (peek().type == TokenType::KEYWORD_MATCH) {
        parse_match(node);
    } else {
        ExpressionParser expr_parser(tokens_, shared_pos_);
        node->children.push_back(expr_parser.parse());
        if (peek().type == TokenType::SEMICOLON) expect(TokenType::SEMICOLON);
    }
    return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_block() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Statement);
    if (peek().type == TokenType::LBRACE) {
        expect(TokenType::LBRACE);
        while (peek().type != TokenType::RBRACE && peek().type != TokenType::EOF_TOKEN) {
            skip_comments();
            node->children.push_back(parse());
        }
        expect(TokenType::RBRACE);
    } else {
        if (peek().type == TokenType::INDENT) expect(TokenType::INDENT);
        while (peek().type != TokenType::DEDENT && peek().type != TokenType::EOF_TOKEN &&
               peek().type != TokenType::KEYWORD_ELSE && peek().type != TokenType::KEYWORD_CATCH &&
               peek().type != TokenType::KEYWORD_FINALLY) {
            skip_comments();
            node->children.push_back(parse());
        }
        if (peek().type == TokenType::DEDENT) expect(TokenType::DEDENT);
    }
    return node;
}

void StatementParser::parse_match(std::unique_ptr<ASTNode>& node) {
    node->token = peek();
    expect(TokenType::KEYWORD_MATCH);
    ExpressionParser expr_parser(tokens_, shared_pos_);
    node->children.push_back(expr_parser.parse());
    if (peek().type == TokenType::LBRACE) {
        expect(TokenType::LBRACE);
        while (peek().type != TokenType::RBRACE && peek().type != TokenType::EOF_TOKEN) {
            auto arm_node = std::make_unique<ASTNode>(ASTNode::Kind::Statement);
            parse_pattern(arm_node);
            expect(TokenType::FAT_ARROW);
            arm_node->children.push_back(expr_parser.parse());
            node->children.push_back(std::move(arm_node));
            if (peek().type == TokenType::COMMA || peek().type == TokenType::SEMICOLON) expect(peek().type);
        }
        expect(TokenType::RBRACE);
    } else {
        if (peek().type == TokenType::INDENT) expect(TokenType::INDENT);
        while (peek().type != TokenType::DEDENT && peek().type != TokenType::EOF_TOKEN) {
            auto arm_node = std::make_unique<ASTNode>(ASTNode::Kind::Statement);
            parse_pattern(arm_node);
            expect(TokenType::FAT_ARROW);
            arm_node->children.push_back(expr_parser.parse());
            node->children.push_back(std::move(arm_node));
            if (peek().type == TokenType::COMMA || peek().type == TokenType::SEMICOLON) expect(peek().type);
        }
        if (peek().type == TokenType::DEDENT) expect(TokenType::DEDENT);
    }
}

void StatementParser::parse_pattern(std::unique_ptr<ASTNode>& node) {
    node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
    expect(TokenType::IDENTIFIER);
    if (peek().type == TokenType::LPAREN) {
        expect(TokenType::LPAREN);
        node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
        expect(TokenType::IDENTIFIER);
        expect(TokenType::RPAREN);
    }
}

// Declaration Parser implementations
std::unique_ptr<ASTNode> DeclarationParser::parse_template() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Template);
    expect(TokenType::KEYWORD_TEMPLATE);
    node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
    expect(TokenType::IDENTIFIER);
    if (peek().type == TokenType::LT) {
        expect(TokenType::LT);
        while (peek().type != TokenType::GT && peek().type != TokenType::EOF_TOKEN) {
            if (peek().type == TokenType::IDENTIFIER) {
                node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
                expect(TokenType::IDENTIFIER);
                if (peek().type == TokenType::COLON) {
                    expect(TokenType::COLON);
                    node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
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
    StatementParser stmt_parser(tokens_, shared_pos_);
    node->children.push_back(stmt_parser.parse_block());
    return node;
}

std::unique_ptr<ASTNode> DeclarationParser::parse_class() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Class);
    expect(TokenType::KEYWORD_CLASS);
    node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
    expect(TokenType::IDENTIFIER);
    StatementParser stmt_parser(tokens_, shared_pos_);
    node->children.push_back(stmt_parser.parse_block());
    return node;
}

std::unique_ptr<ASTNode> DeclarationParser::parse_function() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Function);
    while (peek().type == TokenType::AT) {
        expect(TokenType::AT);
        node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
        expect(TokenType::IDENTIFIER);
    }
    if (peek().type == TokenType::KEYWORD_ASYNC) {
        node->token = peek();
        expect(TokenType::KEYWORD_ASYNC);
    }
    expect(TokenType::KEYWORD_FN);
    node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
    expect(TokenType::IDENTIFIER);
    expect(TokenType::LPAREN);
    while (peek().type != TokenType::RPAREN && peek().type != TokenType::EOF_TOKEN) {
        if (peek().type == TokenType::AMPERSAND) {
            expect(TokenType::AMPERSAND);
            if (peek().type == TokenType::IDENTIFIER && peek().value == "mut") expect(TokenType::IDENTIFIER);
        }
        node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
        expect(TokenType::IDENTIFIER);
        if (peek().type == TokenType::COLON) {
            expect(TokenType::COLON);
            TypeParser type_parser(tokens_, shared_pos_);
            node->children.push_back(type_parser.parse());
        }
        if (peek().type == TokenType::COMMA) expect(TokenType::COMMA);
    }
    expect(TokenType::RPAREN);
    if (peek().type == TokenType::ARROW) {
        expect(TokenType::ARROW);
        TypeParser type_parser(tokens_, shared_pos_);
        node->children.push_back(type_parser.parse());
    }
    if (peek().type == TokenType::LBRACE || peek().type == TokenType::INDENT) {
        StatementParser stmt_parser(tokens_, shared_pos_);
        node->children.push_back(stmt_parser.parse_block());
    }
    return node;
}

// Module Parser implementations
std::unique_ptr<ASTNode> ModuleParser::parse() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Module);
    while (pos_ < tokens_.size() && tokens_[pos_].type != TokenType::EOF_TOKEN) {
        skip_comments();
        skip_indents();
        if (peek().type == TokenType::EOF_TOKEN) break;
        std::cout << "DEBUG: Processing token " << token_type_to_string(peek().type) 
                  << " at line " << peek().line << ", column " << peek().column << std::endl;
        if (peek().type == TokenType::KEYWORD_IMPORT || peek().type == TokenType::KEYWORD_SMUGGLE) {
            auto import_node = std::make_unique<ASTNode>(ASTNode::Kind::Statement, peek());
            expect(peek().type);
            import_node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
            expect(TokenType::IDENTIFIER);
            while (peek().type == TokenType::COLONCOLON) {
                expect(TokenType::COLONCOLON);
                import_node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
                expect(TokenType::IDENTIFIER);
            }
            if (peek().type == TokenType::SEMICOLON) expect(TokenType::SEMICOLON);
            node->children.push_back(std::move(import_node));
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
        } else {
            std::cout << "DEBUG: Skipping unexpected token " << token_type_to_string(peek().type) 
                      << " at line " << peek().line << ", column " << peek().column << std::endl;
            pos_++;
            continue;
        }
    }
    return node;
}

// Print tokens for debugging
void print_tokens(const std::vector<Token>& tokens) {
    for (const auto& token : tokens) {
        std::cout << "Token(type: " << token_type_to_string(token.type) << ", value: \"" << token.value
                  << "\", line: " << token.line << ", column " << token.column << ")\n";
    }
}

// Main function
int main(int argc, char* argv[]) {
    std::cout << argv[0] << ": Version: 0.2.7\n" << std::endl;

    if (argc >= 2 && std::string(argv[1]) == "--test") {
        Catch::Session session;
        int test_argc = 1;
        char* test_argv[] = { argv[0], nullptr };
        if (argc > 2) {
            test_argc = argc - 1;
            test_argv[0] = argv[0];
            for (int i = 2; i < argc; ++i) test_argv[i - 1] = argv[i];
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
        ModuleParser parser(tokens);
        auto ast = parser.parse();
        std::cout << "Parsing successful. AST generated.\n";
    } catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

// Test cases
TEST_CASE("Print parser version", "[parser]") {
    std::cout << "Version: 0.2.7\n";
}

TEST_CASE("Lexer tokenizes indentation-based function", "[lexer]") {
    std::string code = "\nfn main()\n  const x = 1\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    REQUIRE(tokens.size() >= 7);
    REQUIRE(tokens[0].type == TokenType::KEYWORD_FN);
    REQUIRE(tokens[1].type == TokenType::IDENTIFIER);
    REQUIRE(tokens[2].type == TokenType::LPAREN);
    REQUIRE(tokens[3].type == TokenType::RPAREN);
    REQUIRE(tokens[4].type == TokenType::INDENT);
    REQUIRE(tokens[5].type == TokenType::KEYWORD_CONST);
}

TEST_CASE("Lexer tokenizes brace-based function", "[lexer]") {
    std::string code = "\nfn main() {\n  const x = 1\n}\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    REQUIRE(tokens.size() >= 9);
    REQUIRE(tokens[0].type == TokenType::KEYWORD_FN);
    REQUIRE(tokens[1].type == TokenType::IDENTIFIER);
    REQUIRE(tokens[4].type == TokenType::LBRACE);
    REQUIRE(tokens[5].type == TokenType::KEYWORD_CONST);
    REQUIRE(tokens[tokens.size() - 2].type == TokenType::RBRACE);
}

TEST_CASE("Parser handles indentation-based function", "[parser]") {
    std::string code = "\nfn main()\n  const x = 1\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles brace-based function", "[parser]") {
    std::string code = "\nfn main() {\n  const x = 1\n}\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Lexer rejects tabs", "[lexer]") {
    std::string code = "\nfn main()\n\tconst x = 1\n";
    Lexer lexer(code);
    REQUIRE_THROWS_AS(lexer.tokenize(), std::runtime_error);
    try {
        lexer.tokenize();
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()) == "Tabs not allowed at line 3, column 1");
    }
}

TEST_CASE("Parser rejects unmatched brace", "[parser]") {
    std::string code = "\nfn main() {\n  const x = 1\n";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_THROWS_AS(parser.parse(), std::runtime_error);
}

TEST_CASE("Parser handles import directive", "[parser]") {
    std::string code = R"(
    import vyn::fs
    fn main() {
      const x = 1
    }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles smuggle directive", "[parser]") {
    std::string code = R"(
    smuggle http::client
    fn main() {
      const x = 1
    }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles try/catch/finally", "[parser]") {
    std::string code = R"(
    fn main() {
      try {
        risky()
      } catch (e: Error) {
        println("Caught")
      } finally {
        cleanup()
      }
    }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles defer", "[parser]") {
    std::string code = R"(
    fn main() {
      var f = open_file("data.txt")
      defer f.close()
      f.write("data")
    }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles async/await", "[parser]") {
    std::string code = R"(
    async fn fetch() -> String {
      const data = await http_get("url")
      data
    }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles list comprehension", "[parser]") {
    std::string code = R"(
    fn main() {
      const squares = [x * x for x in 0..10]
    }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles operator overloading", "[parser]") {
    std::string code = R"(
    class Vector {
      var x: Float
      fn operator+(other: Vector) -> Vector {
        Vector { x: self.x + other.x }
      }
    }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}

TEST_CASE("Parser handles updated btree.vyn subset", "[parser]") {
    std::string code = R"(
    // B-tree implementation in Vyn
    template Comparable
      fn lt(&self, other: &Self) -> Bool
      fn eq(&self, other: &Self) -> Bool

    template BTree<K: Comparable, V, M: UInt>
      class Node {
        var keys: [K; M-1]
        var values: [V; M-1]
        var num_keys: UInt
        fn new(is_leaf: Bool) -> Node {
          Node { keys = [K; M-1](), values = [V; M-1](), num_keys = 0 }
        }
      }
    )";
    Lexer lexer(code);
    auto tokens = lexer.tokenize();
    ModuleParser parser(tokens);
    REQUIRE_NOTHROW(parser.parse());
}