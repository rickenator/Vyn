#include "vyn/parser.hpp"
#include <stdexcept>
#include <iostream>

ExpressionParser::ExpressionParser(const std::vector<Token>& tokens, size_t& pos) : BaseParser(tokens, pos) {}

std::unique_ptr<ASTNode> ExpressionParser::parse() {
    std::cout << "DEBUG: ExpressionParser::parse at token " << token_type_to_string(peek().type) 
              << " at line " << peek().line << ", column " << peek().column 
              << ", pos_ = " << pos_ << std::endl;
    return parse_expression();
}

std::unique_ptr<ASTNode> ExpressionParser::parse_expression() {
    std::unique_ptr<ASTNode> node;
    std::cout << "DEBUG: ExpressionParser::parse_expression at token " << token_type_to_string(peek().type) 
              << ", value = " << peek().value << ", line = " << peek().line << std::endl;
    if (peek().type == TokenType::KEYWORD_IF) {
        node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek());
        expect(TokenType::KEYWORD_IF);
        node->children.push_back(parse_expression());
        StatementParser stmt_parser(tokens_, pos_);
        node->children.push_back(stmt_parser.parse_block());
        if (peek().type == TokenType::KEYWORD_ELSE) {
            expect(TokenType::KEYWORD_ELSE);
            std::cout << "DEBUG: Parsing ELSE clause at line " << peek().line << ", column " << peek().column << std::endl;
            if (peek().type == TokenType::LBRACE || peek().type == TokenType::INDENT) {
                node->children.push_back(stmt_parser.parse_block());
            } else {
                node->children.push_back(parse_expression());
            }
        }
    } else if (peek().type == TokenType::LBRACKET) {
        node = std::make_unique<ASTNode>(ASTNode::Kind::Expression);
        expect(TokenType::LBRACKET);
        if (peek().type != TokenType::RBRACKET) {
            node->children.push_back(parse_expression());
        }
        if (peek().type == TokenType::KEYWORD_FOR) {
            expect(TokenType::KEYWORD_FOR);
            auto var_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek());
            expect(TokenType::IDENTIFIER);
            var_node->token = tokens_[pos_ - 1];
            node->children.push_back(std::move(var_node));
            expect(TokenType::KEYWORD_IN);
            auto range_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression);
            range_node->children.push_back(parse_expression());
            if (peek().type == TokenType::DOTDOT) {
                range_node->token = peek();
                expect(TokenType::DOTDOT);
                range_node->children.push_back(parse_expression());
            }
            node->children.push_back(std::move(range_node));
            expect(TokenType::RBRACKET);
        } else {
            while (peek().type != TokenType::RBRACKET && peek().type != TokenType::EOF_TOKEN) {
                if (peek().type == TokenType::COMMA) {
                    expect(TokenType::COMMA);
                    if (peek().type != TokenType::RBRACKET) node->children.push_back(parse_expression());
                } else if (!node->children.empty()) {
                    node->children.push_back(parse_expression());
                }
            }
            expect(TokenType::RBRACKET);
        }
    } else {
        node = parse_primary();
        while (peek().type == TokenType::LT || peek().type == TokenType::GT ||
               peek().type == TokenType::EQEQ || peek().type == TokenType::PLUS ||
               peek().type == TokenType::MINUS || peek().type == TokenType::DIVIDE ||
               peek().type == TokenType::MULTIPLY || peek().type == TokenType::AND) {
            std::cout << "DEBUG: Parsing binary operator " << token_type_to_string(peek().type) 
                      << " at line " << peek().line << std::endl;
            auto op_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek());
            expect(peek().type);
            op_node->children.push_back(std::move(node));
            op_node->children.push_back(parse_primary());
            node = std::move(op_node);
        }
    }
    return node;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_primary() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Expression);
    std::cout << "DEBUG: parse_primary at token " << token_type_to_string(peek().type) 
              << " at line " << peek().line << ", column " << peek().column 
              << ", pos_ = " << pos_ << std::endl;
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
        while (peek().type == TokenType::DOT || peek().type == TokenType::LPAREN || 
               peek().type == TokenType::LBRACKET || peek().type == TokenType::COLONCOLON) {
            if (peek().type == TokenType::DOT) {
                std::cout << "DEBUG: Parsing DOT access at line " << peek().line << ", column " << peek().column << std::endl;
                expect(TokenType::DOT);
                auto field_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek());
                expect(TokenType::IDENTIFIER);
                node->children.push_back(std::move(field_node));
            } else if (peek().type == TokenType::COLONCOLON) {
                expect(TokenType::COLONCOLON);
                auto module_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek());
                expect(TokenType::IDENTIFIER);
                node->children.push_back(std::move(module_node));
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
            TypeParser type_parser(tokens_, pos_);
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
    } else if (peek().type == TokenType::FLOAT_LITERAL) {
        node->token = peek();
        expect(TokenType::FLOAT_LITERAL);
    } else if (peek().type == TokenType::STRING_LITERAL) {
        node->token = peek();
        expect(TokenType::STRING_LITERAL);
    } else if (peek().type == TokenType::LBRACKET) {
        expect(TokenType::LBRACKET);
        TypeParser type_parser(tokens_, pos_);
        node->children.push_back(type_parser.parse());
        expect(TokenType::SEMICOLON);
        node->children.push_back(parse());
        expect(TokenType::RBRACKET);
        if (peek().type == TokenType::LPAREN) {
            expect(TokenType::LPAREN);
            expect(TokenType::RPAREN);
        }
    } else {
        std::cout << "DEBUG: Unexpected token in parse_primary: " << token_type_to_string(peek().type) 
                  << " at line " << peek().line << ", column " << peek().column 
                  << ", pos_ = " << pos_ << std::endl;
        throw std::runtime_error("Expected primary expression at line " + std::to_string(peek().line) + ", column " + std::to_string(peek().column));
    }
    return node;
}