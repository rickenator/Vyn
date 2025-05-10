#include "vyn/parser.hpp"
#include <stdexcept>
#include <iostream>

StatementParser::StatementParser(const std::vector<Token>& tokens, size_t& pos) : BaseParser(tokens, pos) {}

std::unique_ptr<ASTNode> StatementParser::parse() {
    skip_comments();
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Statement);
    std::cout << "DEBUG: StatementParser::parse at token " << token_type_to_string(peek().type) 
              << " at line " << peek().line << ", column " << peek().column 
              << ", pos_ = " << pos_ << ", indent_level = " << indent_levels_.back() << std::endl;
    if (peek().type == TokenType::INDENT) {
        std::cout << "DEBUG: Handling INDENT in StatementParser::parse, delegating to parse_block" << std::endl;
        return parse_block();
    } else if (peek().type == TokenType::KEYWORD_CONST) {
        node->token = peek();
        expect(TokenType::KEYWORD_CONST);
        node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
        expect(TokenType::IDENTIFIER);
        if (peek().type == TokenType::COLON) {
            expect(TokenType::COLON);
            TypeParser type_parser(tokens_, pos_);
            node->children.push_back(type_parser.parse());
        }
        expect(TokenType::EQ);
        ExpressionParser expr_parser(tokens_, pos_);
        node->children.push_back(expr_parser.parse());
        if (peek().type == TokenType::SEMICOLON) expect(TokenType::SEMICOLON);
    } else if (peek().type == TokenType::KEYWORD_VAR) {
        node->token = peek();
        expect(TokenType::KEYWORD_VAR);
        node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
        expect(TokenType::IDENTIFIER);
        if (peek().type == TokenType::COLON) {
            expect(TokenType::COLON);
            TypeParser type_parser(tokens_, pos_);
            node->children.push_back(type_parser.parse());
        }
        if (peek().type == TokenType::EQ) {
            expect(TokenType::EQ);
            ExpressionParser expr_parser(tokens_, pos_);
            node->children.push_back(expr_parser.parse());
        }
        if (peek().type == TokenType::SEMICOLON) expect(TokenType::SEMICOLON);
    } else if (peek().type == TokenType::KEYWORD_IF) {
        node->token = peek();
        expect(TokenType::KEYWORD_IF);
        ExpressionParser expr_parser(tokens_, pos_);
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
        ExpressionParser expr_parser(tokens_, pos_);
        node->children.push_back(expr_parser.parse());
        node->children.push_back(parse_block());
    } else if (peek().type == TokenType::KEYWORD_RETURN) {
        node->token = peek();
        expect(TokenType::KEYWORD_RETURN);
        if (peek().type != TokenType::SEMICOLON && peek().type != TokenType::DEDENT && peek().type != TokenType::RBRACE) {
            ExpressionParser expr_parser(tokens_, pos_);
            node->children.push_back(expr_parser.parse());
        }
        if (peek().type == TokenType::SEMICOLON) expect(TokenType::SEMICOLON);
    } else if (peek().type == TokenType::KEYWORD_DEFER) {
        node->token = peek();
        expect(TokenType::KEYWORD_DEFER);
        ExpressionParser expr_parser(tokens_, pos_);
        node->children.push_back(expr_parser.parse());
        if (peek().type == TokenType::SEMICOLON) expect(TokenType::SEMICOLON);
    } else if (peek().type == TokenType::KEYWORD_AWAIT) {
        node->token = peek();
        expect(TokenType::KEYWORD_AWAIT);
        ExpressionParser expr_parser(tokens_, pos_);
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
            TypeParser type_parser(tokens_, pos_);
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
    } else if (peek().type == TokenType::KEYWORD_IMPORT || peek().type == TokenType::KEYWORD_SMUGGLE) {
        node->token = peek();
        expect(peek().type);
        node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
        expect(TokenType::IDENTIFIER);
        while (peek().type == TokenType::COLONCOLON) {
            expect(TokenType::COLONCOLON);
            node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
            expect(TokenType::IDENTIFIER);
        }
        if (peek().type == TokenType::SEMICOLON) expect(TokenType::SEMICOLON);
    } else if (peek().type == TokenType::IDENTIFIER || peek().type == TokenType::INT_LITERAL ||
               peek().type == TokenType::FLOAT_LITERAL || peek().type == TokenType::STRING_LITERAL) {
        ExpressionParser expr_parser(tokens_, pos_);
        node->children.push_back(expr_parser.parse());
        if (peek().type == TokenType::SEMICOLON) expect(TokenType::SEMICOLON);
    } else {
        std::cout << "DEBUG: Unexpected token in StatementParser::parse: " 
                  << token_type_to_string(peek().type) << " at line " << peek().line 
                  << ", column " << peek().column << ", pos_ = " << pos_ << std::endl;
        throw std::runtime_error("Unexpected statement token at line " + 
                                std::to_string(peek().line) + ", column " + 
                                std::to_string(peek().column));
    }
    return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_block() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Statement);
    std::cout << "DEBUG: parse_block at token " << token_type_to_string(peek().type) 
              << " at line " << peek().line << ", column " << peek().column 
              << ", pos_ = " << pos_ << ", indent_level = " << indent_levels_.back() << std::endl;
    if (peek().type == TokenType::LBRACE) {
        indent_levels_.push_back(indent_levels_.back() + 1);
        std::cout << "DEBUG: Pushed brace level " << indent_levels_.back() << " at line " 
                  << peek().line << ", column " << peek().column << std::endl;
        expect(TokenType::LBRACE);
        while (peek().type != TokenType::RBRACE && peek().type != TokenType::EOF_TOKEN) {
            skip_comments();
            if (peek().type == TokenType::KEYWORD_FN) {
                DeclarationParser decl_parser(tokens_, pos_);
                node->children.push_back(decl_parser.parse_function());
            } else if (peek().type == TokenType::KEYWORD_CLASS) {
                DeclarationParser decl_parser(tokens_, pos_);
                node->children.push_back(decl_parser.parse_class());
            } else {
                node->children.push_back(parse());
            }
        }
        expect(TokenType::RBRACE);
        indent_levels_.pop_back();
        std::cout << "DEBUG: Popped brace level to " << indent_levels_.back() << " at line " 
                  << peek().line << ", column " << peek().column << std::endl;
    } else if (peek().type == TokenType::INDENT) {
        expect(TokenType::INDENT);
        indent_levels_.push_back(indent_levels_.back() + 1);
        std::cout << "DEBUG: Consumed INDENT at line " << tokens_[pos_ - 1].line 
                  << ", column " << tokens_[pos_ - 1].column << ", pushed indent level " 
                  << indent_levels_.back() << ", pos_ = " << pos_ << std::endl;
        while (peek().type != TokenType::DEDENT && peek().type != TokenType::EOF_TOKEN &&
               peek().type != TokenType::KEYWORD_ELSE && peek().type != TokenType::KEYWORD_CATCH &&
               peek().type != TokenType::KEYWORD_FINALLY) {
            skip_comments();
            if (peek().type == TokenType::KEYWORD_FN) {
                DeclarationParser decl_parser(tokens_, pos_);
                node->children.push_back(decl_parser.parse_function());
            } else if (peek().type == TokenType::KEYWORD_CLASS) {
                DeclarationParser decl_parser(tokens_, pos_);
                node->children.push_back(decl_parser.parse_class());
            } else {
                node->children.push_back(parse());
            }
        }
        if (peek().type == TokenType::DEDENT) {
            expect(TokenType::DEDENT);
            indent_levels_.pop_back();
            std::cout << "DEBUG: Consumed DEDENT at line " << tokens_[pos_ - 1].line 
                      << ", column " << tokens_[pos_ - 1].column << ", popped indent level to " 
                      << indent_levels_.back() << ", pos_ = " << pos_ << std::endl;
        }
    } else if (peek().type == TokenType::DEDENT || peek().type == TokenType::EOF_TOKEN) {
        std::cout << "DEBUG: Empty block detected at token " << token_type_to_string(peek().type) 
                  << " at line " << peek().line << ", column " << peek().column 
                  << ", pos_ = " << pos_ << ", indent_level = " << indent_levels_.back() << std::endl;
        return node;
    } else {
        node->children.push_back(parse());
    }
    return node;
}

void StatementParser::parse_match(std::unique_ptr<ASTNode>& node) {
    node->token = peek();
    expect(TokenType::KEYWORD_MATCH);
    ExpressionParser expr_parser(tokens_, pos_);
    node->children.push_back(expr_parser.parse());
    if (peek().type == TokenType::LBRACE) {
        indent_levels_.push_back(indent_levels_.back() + 1);
        std::cout << "DEBUG: Pushed brace level " << indent_levels_.back() << " at line " 
                  << peek().line << ", column " << peek().column << std::endl;
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
        indent_levels_.pop_back();
        std::cout << "DEBUG: Popped brace level to " << indent_levels_.back() << " at line " 
                  << peek().line << ", column " << peek().column << std::endl;
    } else if (peek().type == TokenType::INDENT) {
        expect(TokenType::INDENT);
        indent_levels_.push_back(indent_levels_.back() + 1);
        std::cout << "DEBUG: Pushed indent level " << indent_levels_.back() << " at line " 
                  << peek().line << ", column " << peek().column << std::endl;
        while (peek().type != TokenType::DEDENT && peek().type != TokenType::EOF_TOKEN) {
            auto arm_node = std::make_unique<ASTNode>(ASTNode::Kind::Statement);
            parse_pattern(arm_node);
            expect(TokenType::FAT_ARROW);
            arm_node->children.push_back(expr_parser.parse());
            node->children.push_back(std::move(arm_node));
            if (peek().type == TokenType::COMMA || peek().type == TokenType::SEMICOLON) expect(peek().type);
        }
        if (peek().type == TokenType::DEDENT) {
            expect(TokenType::DEDENT);
            indent_levels_.pop_back();
            std::cout << "DEBUG: Popped indent level to " << indent_levels_.back() << " at line " 
                      << peek().line << ", column " << peek().column << std::endl;
        }
    }
}

void StatementParser::parse_pattern(std::unique_ptr<ASTNode>& node) {
    std::cout << "DEBUG: Parsing pattern at token " << token_type_to_string(peek().type) 
              << ", value = " << peek().value << ", line = " << peek().line << std::endl;
    node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
    expect(TokenType::IDENTIFIER);
    if (peek().type == TokenType::LPAREN) {
        expect(TokenType::LPAREN);
        node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
        expect(TokenType::IDENTIFIER);
        expect(TokenType::RPAREN);
    }
}