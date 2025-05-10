#include "vyn/parser.hpp"
#include <stdexcept>
#include <iostream>

DeclarationParser::DeclarationParser(const std::vector<Token>& tokens, size_t& pos) : BaseParser(tokens, pos) {}

std::unique_ptr<ASTNode> DeclarationParser::parse_template() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Template);
    expect(TokenType::KEYWORD_TEMPLATE);
    std::cout << "DEBUG: Parsing template at line " << peek().line << ", column " << peek().column << std::endl;
    node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
    expect(TokenType::IDENTIFIER);
    if (peek().type == TokenType::LT) {
        expect(TokenType::LT);
        while (peek().type != TokenType::GT && peek().type != TokenType::EOF_TOKEN) {
            if (peek().type == TokenType::IDENTIFIER) {
                std::cout << "DEBUG: Parsing template param " << peek().value << " at line " << peek().line << std::endl;
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
    StatementParser stmt_parser(tokens_, pos_);
    std::cout << "DEBUG: Parsing template body at line " << peek().line << std::endl;
    node->children.push_back(stmt_parser.parse_block());
    return node;
}

std::unique_ptr<ASTNode> DeclarationParser::parse_class() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Class);
    expect(TokenType::KEYWORD_CLASS);
    node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
    expect(TokenType::IDENTIFIER);
    StatementParser stmt_parser(tokens_, pos_);
    node->children.push_back(stmt_parser.parse_block());
    return node;
}

std::unique_ptr<ASTNode> DeclarationParser::parse_function() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Function);
    std::cout << "DEBUG: Entering parse_function at token " << token_type_to_string(peek().type) 
              << " at line " << peek().line << ", column " << peek().column 
              << ", pos_ = " << pos_ << ", indent_level = " << indent_levels_.back() << std::endl;
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
    if (peek().type == TokenType::PLUS || peek().type == TokenType::MINUS ||
        peek().type == TokenType::DIVIDE || peek().type == TokenType::MULTIPLY) {
        node->children.back()->token = peek();
        pos_++;
    }
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
            TypeParser type_parser(tokens_, pos_);
            node->children.push_back(type_parser.parse());
        }
        if (peek().type == TokenType::COMMA) expect(TokenType::COMMA);
    }
    expect(TokenType::RPAREN);
    if (peek().type == TokenType::ARROW) {
        expect(TokenType::ARROW);
        TypeParser type_parser(tokens_, pos_);
        node->children.push_back(type_parser.parse());
    }
    if (peek().type == TokenType::LBRACE || peek().type == TokenType::INDENT) {
        std::cout << "DEBUG: Parsing function body at token " << token_type_to_string(peek().type) 
                  << " at line " << peek().line << ", column " << peek().column 
                  << ", pos_ = " << pos_ << ", indent_level = " << indent_levels_.back() << std::endl;
        StatementParser stmt_parser(tokens_, pos_);
        node->children.push_back(stmt_parser.parse_block());
    }
    std::cout << "DEBUG: Exiting parse_function, next token " << token_type_to_string(peek().type) 
              << " at line " << peek().line << ", column " << peek().column 
              << ", pos_ = " << pos_ << ", indent_level = " << indent_levels_.back() << std::endl;
    return node;
}