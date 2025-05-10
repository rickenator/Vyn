#include "vyn/parser.hpp"
#include <stdexcept>
#include <iostream>

BaseParser::BaseParser(const std::vector<Token>& tokens, size_t& pos) : tokens_(tokens), pos_(pos), indent_levels_({0}) {}

Token BaseParser::peek() const {
    return pos_ < tokens_.size() ? tokens_[pos_] : Token{TokenType::EOF_TOKEN, "", 0, 0};
}

void BaseParser::expect(TokenType type) {
    if (pos_ >= tokens_.size() || tokens_[pos_].type != type) {
        std::string found = pos_ < tokens_.size() ? token_type_to_string(tokens_[pos_].type) : "EOF";
        throw std::runtime_error("Expected " + token_type_to_string(type) + " but found " + found +
                                 " at line " + std::to_string(peek().line) + ", column " + std::to_string(peek().column));
    }
    pos_++;
}

void BaseParser::skip_comments() {
    while (pos_ < tokens_.size() && tokens_[pos_].type == TokenType::COMMENT) {
        std::cout << "DEBUG: Skipping comment at line " << tokens_[pos_].line 
                  << ", column " << tokens_[pos_].column << ", pos_ = " << pos_ << std::endl;
        pos_++;
    }
}

void BaseParser::skip_indents() {
    while (pos_ < tokens_.size() && (tokens_[pos_].type == TokenType::INDENT || tokens_[pos_].type == TokenType::DEDENT)) {
        if (tokens_[pos_].type == TokenType::INDENT) {
            indent_levels_.push_back(indent_levels_.back() + 1);
            std::cout << "DEBUG: Pushed indent level " << indent_levels_.back() << " at line " 
                      << tokens_[pos_].line << ", column " << tokens_[pos_].column << std::endl;
        } else if (tokens_[pos_].type == TokenType::DEDENT && indent_levels_.size() > 1) {
            indent_levels_.pop_back();
            std::cout << "DEBUG: Popped indent level to " << indent_levels_.back() << " at line " 
                      << tokens_[pos_].line << ", column " << tokens_[pos_].column << std::endl;
        }
        pos_++;
    }
}

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
            node->children.push_back(parse_expression()); // Parse full expression (e.g., x * x)
        }
        if (peek().type == TokenType::KEYWORD_FOR) {
            expect(TokenType::KEYWORD_FOR);
            auto var_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek());
            expect(TokenType::IDENTIFIER);
            var_node->token = tokens_[pos_ - 1];
            node->children.push_back(std::move(var_node));
            expect(TokenType::KEYWORD_IN);
            auto range_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression);
            range_node->children.push_back(parse_expression()); // Parse range start (e.g., 0)
            if (peek().type == TokenType::DOTDOT) {
                range_node->token = peek();
                expect(TokenType::DOTDOT);
                range_node->children.push_back(parse_expression()); // Parse range end (e.g., 10)
            }
            node->children.push_back(std::move(range_node));
            expect(TokenType::RBRACKET); // Ensure RBRACKET closes comprehension
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
        node->children.push_back(parse()); // Parse element type (e.g., K)
        if (peek().type == TokenType::SEMICOLON) {
            expect(TokenType::SEMICOLON);
            ExpressionParser expr_parser(tokens_, pos_);
            node->children.push_back(expr_parser.parse()); // Parse size expression (e.g., M-1)
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
                    // Handle size expressions like M-1
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
        return node; // Return empty block node
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

ModuleParser::ModuleParser(const std::vector<Token>& tokens, size_t& pos) : BaseParser(tokens, pos) {}

std::unique_ptr<ASTNode> ModuleParser::parse() {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::Module);
    while (pos_ < tokens_.size() && tokens_[pos_].type != TokenType::EOF_TOKEN) {
        skip_comments();
        skip_indents();
        if (peek().type == TokenType::EOF_TOKEN) break;
        std::cout << "DEBUG: Processing token " << token_type_to_string(peek().type) 
                  << " at line " << peek().line << ", column " << peek().column 
                  << ", pos_ = " << pos_ << ", indent_level = " << indent_levels_.back() << std::endl;
        if (peek().type == TokenType::KEYWORD_IMPORT || peek().type == TokenType::KEYWORD_SMUGGLE) {
            StatementParser stmt_parser(tokens_, pos_);
            node->children.push_back(stmt_parser.parse());
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
        } else if (peek().type == TokenType::INDENT) {
            std::cout << "DEBUG: Handling INDENT token at line " << peek().line 
                      << ", column " << peek().column << ", pos_ = " << pos_ << std::endl;
            StatementParser stmt_parser(tokens_, pos_);
            node->children.push_back(stmt_parser.parse_block());
        } else {
            std::cout << "DEBUG: Skipping unexpected token " << token_type_to_string(peek().type) 
                      << " at line " << peek().line << ", column " << peek().column 
                      << ", pos_ = " << pos_ << std::endl;
            pos_++;
        }
    }
    return node;
}