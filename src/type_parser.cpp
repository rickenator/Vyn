#include "vyn/parser.hpp"
#include <stdexcept>
#include <iostream>

TypeParser::TypeParser(const std::vector<Token>& tokens, size_t& pos) : BaseParser(tokens, pos) {}

std::unique_ptr<ASTNode> TypeParser::parse() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Type);
    std::cout << "DEBUG: TypeParser::parse at token " << token_type_to_string(peek().type) 
              << ", value = " << peek().value << ", line = " << peek().line << std::endl;
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
            ExpressionParser expr_parser(tokens_, pos_);
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
                    ExpressionParser expr_parser(tokens_, pos_);
                    node->children.push_back(expr_parser.parse());
                } else {
                    ExpressionParser expr_parser(tokens_, pos_);
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