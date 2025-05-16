#include "vyn/parser/ast.hpp"
#include "vyn/parser/parser.hpp"
#include <stdexcept>
#include <string> // Required for std::to_string
#include <algorithm> // Required for std::any_of, if used by match or other helpers

namespace vyn {

    // Constructor now takes a reference to a BaseParser instance
    ExpressionParser::ExpressionParser(BaseParser& parent_parser)
        : BaseParser(parent_parser), // Calls BaseParser's copy constructor or a new constructor if defined
          parent_parser_ref_(parent_parser) {}

    vyn::ExprPtr ExpressionParser::parse() {
        // All token operations (peek, consume, match, expect, error, etc.)
        // will now be called on parent_parser_ref_ or via the new helper methods
        // that delegate to parent_parser_ref_.
        // For example, instead of this->peek(), it would be peek() which calls parent_parser_ref_.peek().
        return this->parse_expression();
    }

    vyn::ExprPtr ExpressionParser::parse_atom() {
        skip_comments_and_newlines();
        const vyn::token::Token& token = peek();

        // --- Handle explicit pointer dereference: loc(expr) ---
        if (token.type == vyn::TokenType::IDENTIFIER && token.lexeme == "loc") {
            vyn::SourceLocation loc_loc = token.location;
            consume(); // consume 'loc'
            if (peek().type != vyn::TokenType::LPAREN) {
                throw error(peek(), "Expected '(' after 'loc' for location dereference");
            }
            consume(); // consume '('
            vyn::ExprPtr ptr_expr = parse_expression();
            if (peek().type != vyn::TokenType::RPAREN) {
                throw error(peek(), "Expected ')' after location expression in 'loc(...)'");
            }
            consume(); // consume ')'
            return std::make_unique<vyn::PointerDerefExpression>(loc_loc, std::move(ptr_expr));
        }
        // --- Handle address-of: addr(loc) ---
        if (token.type == vyn::TokenType::IDENTIFIER && token.lexeme == "addr") {
            vyn::SourceLocation addr_loc = token.location;
            consume(); // consume 'addr'
            if (peek().type != vyn::TokenType::LPAREN) {
                throw error(peek(), "Expected '(' after 'addr' for address-of");
            }
            consume(); // consume '('
            vyn::ExprPtr loc_expr = parse_expression();
            if (peek().type != vyn::TokenType::RPAREN) {
                throw error(peek(), "Expected ')' after location expression in 'addr(...)'");
            }
            consume(); // consume ')'
            return std::make_unique<vyn::AddrOfExpression>(addr_loc, std::move(loc_expr));
        }
        // --- Handle from(addr) for integer-to-loc<T> conversion ---
        if (token.type == vyn::TokenType::IDENTIFIER && token.lexeme == "from") {
            vyn::SourceLocation from_loc = token.location;
            consume(); // consume 'from'
            if (peek().type != vyn::TokenType::LPAREN) {
                throw error(peek(), "Expected '(' after 'from' for integer-to-loc conversion");
            }
            consume(); // consume '('
            vyn::ExprPtr addr_expr = parse_expression();
            if (peek().type != vyn::TokenType::RPAREN) {
                throw error(peek(), "Expected ')' after address expression in 'from(...)'");
            }
            consume(); // consume ')'
            return std::make_unique<vyn::FromIntToLocExpression>(from_loc, std::move(addr_expr));
        }

        #ifdef VERBOSE
        std::cerr << "[PARSE_ATOM] Next token: " << vyn::token_type_to_string(token.type)
                  << " (" << token.lexeme << ") at " 
                  << token.location.filePath << ":" << token.location.line << ":" << token.location.column << std::endl;
        #endif

        // Gracefully handle end-of-file and end-of-block tokens
        if (token.type == vyn::TokenType::END_OF_FILE ||
            token.type == vyn::TokenType::DEDENT ||
            token.type == vyn::TokenType::RBRACE) {
            #ifdef VERBOSE
            std::cerr << "[PARSE_ATOM] Returning nullptr for EOF/DEDENT/RBRACE token" << std::endl;
            #endif
            return nullptr;
        }

        // Handle identifiers and keywords that can be part of expressions
        if (token.type == vyn::TokenType::IDENTIFIER ||
            token.type == vyn::TokenType::KEYWORD_CLASS || 
            token.type == vyn::TokenType::KEYWORD_FN ||
            token.type == vyn::TokenType::KEYWORD_ASYNC ||
            // Note: KEYWORD_AWAIT is handled specially in parse_primary_expr
            token.type == vyn::TokenType::KEYWORD_STRUCT ||
            token.type == vyn::TokenType::KEYWORD_ENUM ||
            token.type == vyn::TokenType::KEYWORD_TRAIT ||
            token.type == vyn::TokenType::KEYWORD_IMPL ||
            token.type == vyn::TokenType::KEYWORD_OPERATOR) {
            // Save identifier token
            vyn::token::Token ident_token = token;
            consume();
            // Check for struct/constructor literal: IDENTIFIER { ... }
            if (peek().type == vyn::TokenType::LBRACE) {
                consume(); // consume '{'
                vyn::SourceLocation obj_loc = previous_token().location;
                std::vector<vyn::ObjectProperty> properties;
                if (peek().type != vyn::TokenType::RBRACE) {
                    do {
                        vyn::SourceLocation prop_loc = peek().location;
                        if (peek().type != vyn::TokenType::IDENTIFIER) {
                            throw error(peek(), "Expected property name in object initializer");
                        }
                        std::string field_name = peek().lexeme;
                        auto key_ident = std::make_unique<vyn::Identifier>(peek().location, field_name);
                        consume();
                        if (peek().type != vyn::TokenType::COLON) {
                            throw error(peek(), "Expected ':' after property name in object initializer");
                        }
                        consume();
                        vyn::ExprPtr value = parse_expression();
                        if (!value) {
                            throw error(peek(), "Expected value after ':' in object initializer");
                        }
                        properties.emplace_back(prop_loc, std::move(key_ident), std::move(value));
                    } while (match(vyn::TokenType::COMMA) && peek().type != vyn::TokenType::RBRACE);
                }
                expect(vyn::TokenType::RBRACE);
                auto object_literal = std::make_unique<vyn::ObjectLiteral>(obj_loc, std::move(properties));
                std::vector<vyn::ExprPtr> args;
                args.push_back(std::move(object_literal));
                auto callee = std::make_unique<vyn::Identifier>(ident_token.location, ident_token.lexeme);
                return std::make_unique<vyn::CallExpression>(ident_token.location, std::move(callee), std::move(args));
            }
            // Otherwise, just an identifier
            return std::make_unique<vyn::Identifier>(ident_token.location, ident_token.lexeme);
        } else if (token.type == vyn::TokenType::INT_LITERAL) {
            consume(); // Delegated
            return std::make_unique<vyn::IntegerLiteral>(token.location, std::stoll(token.lexeme));
        } else if (token.type == vyn::TokenType::FLOAT_LITERAL) {
            consume(); // Delegated
            return std::make_unique<vyn::FloatLiteral>(token.location, std::stod(token.lexeme));
        } else if (token.type == vyn::TokenType::STRING_LITERAL) {
            consume(); // Delegated
            return std::make_unique<vyn::StringLiteral>(token.location, token.lexeme);
        } else if (token.type == vyn::TokenType::KEYWORD_TRUE || token.type == vyn::TokenType::KEYWORD_FALSE) {
            consume(); // Delegated
            return std::make_unique<vyn::BooleanLiteral>(token.location, token.type == vyn::TokenType::KEYWORD_TRUE);
        } else if (token.type == vyn::TokenType::KEYWORD_NIL) { 
            consume(); // Delegated
            return std::make_unique<vyn::NilLiteral>(token.location);
        } else if (token.type == vyn::TokenType::LPAREN) {
            consume(); // Delegated
            vyn::ExprPtr expr = this->parse_expression(); // Recursive call, will use delegated methods
            expect(vyn::TokenType::RPAREN); // Delegated
            return expr;
        } else if (token.type == vyn::TokenType::LBRACE) { // Object initializer syntax
            #ifdef VERBOSE
            std::cerr << "[PARSE_ATOM] Found object initializer opening brace" << std::endl;
            #endif

            consume(); // consume '{'
            vyn::SourceLocation obj_loc = previous_token().location;

            std::vector<vyn::ObjectProperty> properties;

            if (peek().type != vyn::TokenType::RBRACE) {
                do {
                    vyn::SourceLocation prop_loc = peek().location;
                    // Parse field name
                    if (peek().type != vyn::TokenType::IDENTIFIER) {
                        #ifdef VERBOSE
                        std::cerr << "[PARSE_ATOM] ERROR: Expected identifier for property name, but got "
                                  << vyn::token_type_to_string(peek().type) << " (" << peek().lexeme << ") at "
                                  << peek().location.filePath << ":" << peek().location.line << ":" << peek().location.column
                                  << std::endl;
                        #endif
                        throw error(peek(), "Expected property name in object initializer");
                    }

                    std::string field_name = peek().lexeme;
                    #ifdef VERBOSE
                    std::cerr << "[PARSE_ATOM] Found property name: " << field_name << std::endl;
                    #endif

                    auto key_ident = std::make_unique<vyn::Identifier>(peek().location, field_name);
                    consume(); // Consume the field name

                    // Parse colon and value
                    #ifdef VERBOSE
                    std::cerr << "[PARSE_ATOM] Expecting colon after property name" << std::endl;
                    #endif

                    if (peek().type != vyn::TokenType::COLON) {
                        #ifdef VERBOSE
                        std::cerr << "[PARSE_ATOM] ERROR: Expected colon after property name, but got "
                                  << vyn::token_type_to_string(peek().type) << " (" << peek().lexeme << ") at "
                                  << peek().location.filePath << ":" << peek().location.line << ":" << peek().location.column
                                  << std::endl;
                        #endif
                        throw error(peek(), "Expected ':' after property name in object initializer");
                    }
                    consume(); // Consume the colon manually instead of using expect()

                    #ifdef VERBOSE
                    std::cerr << "[PARSE_ATOM] Parsing property value expression" << std::endl;
                    #endif
                    vyn::ExprPtr value = parse_expression();
                    if (!value) {
                        #ifdef VERBOSE
                        std::cerr << "[PARSE_ATOM] ERROR: Expected value expression after colon" << std::endl;
                        #endif
                        throw error(peek(), "Expected value after ':' in object initializer");
                    }

                    #ifdef VERBOSE
                    std::cerr << "[PARSE_ATOM] Adding property " << field_name << " to object literal" << std::endl;
                    #endif
                    properties.emplace_back(prop_loc, std::move(key_ident), std::move(value));
                } while (match(vyn::TokenType::COMMA) && peek().type != vyn::TokenType::RBRACE);
            }

            expect(vyn::TokenType::RBRACE);

            return std::make_unique<vyn::ObjectLiteral>(obj_loc, std::move(properties));
        } else if (token.type == vyn::TokenType::LBRACKET) { // Array literals and list comprehensions
            consume(); // consume '['
            vyn::SourceLocation arr_loc = previous_token().location; // Delegated
            
            // Handle empty array
            if (check(vyn::TokenType::RBRACKET)) {
                consume(); // consume ']'
                return std::make_unique<vyn::ArrayLiteralNode>(arr_loc, std::vector<vyn::ExprPtr>());
            }
            
            // Parse the first expression in the array or list comprehension
            vyn::ExprPtr expr = this->parse_expression();
            
            // Check if this is a list comprehension [expr for var in iterable]
            if (this->peek().type == vyn::TokenType::KEYWORD_FOR) {
                // This is a list comprehension
                this->consume(); // consume 'for'
                
                // Parse the variable identifier
                if (this->peek().type != vyn::TokenType::IDENTIFIER) {
                    throw std::runtime_error("Expected identifier after 'for' in list comprehension at " + 
                        this->peek().location.filePath + ":" + 
                        std::to_string(this->peek().location.line) + ":" + 
                        std::to_string(this->peek().location.column));
                }
                auto var_ident = std::make_unique<vyn::Identifier>(this->peek().location, this->peek().lexeme);
                this->consume(); // consume the identifier
                
                // Parse 'in'
                if (this->peek().type != vyn::TokenType::KEYWORD_IN) {
                    throw std::runtime_error("Expected 'in' after identifier in list comprehension at " + 
                        this->peek().location.filePath + ":" + 
                        std::to_string(this->peek().location.line) + ":" + 
                        std::to_string(this->peek().location.column));
                }
                this->consume(); // consume 'in'
                
                // Parse the iterable expression
                vyn::ExprPtr iterable = this->parse_expression();
                
                // Create a special array literal with the expression, variable, and iterable
                // For now, we'll use CallExpression with a special name to represent list comprehensions
                // The AST should ideally have a dedicated ListComprehensionNode type
                auto comp_name = std::make_unique<vyn::Identifier>(arr_loc, "_list_comprehension");
                std::vector<vyn::ExprPtr> args;
                args.push_back(std::move(expr));
                args.push_back(std::move(var_ident));
                args.push_back(std::move(iterable));
                this->expect(vyn::TokenType::RBRACKET); // consume ']'
                return std::make_unique<vyn::CallExpression>(arr_loc, std::move(comp_name), std::move(args));
            } 
            else {
                // This is a regular array literal
                std::vector<vyn::ExprPtr> elements;
                elements.push_back(std::move(expr));
                
                // Parse any additional elements
                while (match(vyn::TokenType::COMMA)) {
                    if (check(vyn::TokenType::RBRACKET)) break;
                    elements.push_back(this->parse_expression());
                }
                
                expect(vyn::TokenType::RBRACKET); // consume ']'
                return std::make_unique<vyn::ArrayLiteralNode>(arr_loc, std::move(elements));
            }
        }
        
        vyn::SourceLocation loc = current_location(); // Delegated
        #ifdef VERBOSE
        std::cerr << "[PARSE_ATOM] ERROR: Unexpected token in atom: " 
                  << vyn::token_type_to_string(token.type) << " (" << token.lexeme << ") at " 
                  << token.location.filePath << ":" << token.location.line << ":" << token.location.column
                  << std::endl;
                  
        // Print context: show a few tokens before and after
        std::cerr << "[PARSE_ATOM] Token context:" << std::endl;
        size_t start_pos = pos_ > 3 ? pos_ - 3 : 0;
        size_t end_pos = std::min(pos_ + 3, tokens_.size() - 1);
        for (size_t i = start_pos; i <= end_pos; i++) {
            std::cerr << "  " << (i == pos_ ? "→ " : "  ") 
                      << vyn::token_type_to_string(tokens_[i].type) 
                      << " (" << tokens_[i].lexeme << ") at "
                      << tokens_[i].location.line << ":" << tokens_[i].location.column
                      << std::endl;
        }
        #endif
        
        // Use the error() method which is now also delegated
        throw error(token, "Unexpected token in atom: " + token.lexeme);
    }

    vyn::ExprPtr ExpressionParser::parse_primary_expr() {
        skip_comments_and_newlines();
        vyn::SourceLocation loc = current_location();

        #ifdef VERBOSE
        std::cerr << "[EXPR_PARSER] Parsing primary expression at " 
                  << loc.filePath << ":" << loc.line << ":" << loc.column << std::endl;
        #endif

        // Check for await expression
        if (peek().type == vyn::TokenType::KEYWORD_AWAIT) {
            vyn::SourceLocation await_loc = current_location(); // Store await location
            #ifdef VERBOSE
            std::cerr << "[EXPR_PARSER] Found await keyword at " 
                     << await_loc.filePath << ":" << await_loc.line << ":" << await_loc.column << std::endl;
            #endif
            
            consume(); // Consume 'await'
            skip_comments_and_newlines(); // Skip any whitespace after 'await'
            
            // Parse the expression after await
            vyn::ExprPtr expr = parse_primary_expr(); // Changed from parse_expression to handle parenthesized expressions correctly
            
            if (!expr) {
                #ifdef VERBOSE
                std::cerr << "[EXPR_PARSER] ERROR: No expression found after 'await'" << std::endl;
                #endif
                throw error(previous_token(), "Expected expression after 'await'");
            }
            
            // Create await wrapper
            auto await_id = std::make_unique<vyn::Identifier>(await_loc, "_await");
            std::vector<vyn::ExprPtr> await_args;
            await_args.push_back(std::move(expr));
            return std::make_unique<vyn::CallExpression>(await_loc, std::move(await_id), std::move(await_args));
        }

        return parse_atom();
    }

    vyn::ExprPtr ExpressionParser::parse_postfix_expr() {
        vyn::ExprPtr expr = this->parse_primary_expr(); // Changed from parse_atom to parse_primary_expr
        if (!expr) return nullptr;
        
        std::optional<vyn::token::Token> lparen_token_opt;
        std::optional<vyn::token::Token> lbracket_token_opt;
        std::optional<vyn::token::Token> dot_token_opt;
        std::optional<vyn::token::Token> coloncolon_token_opt;
        
        while (true) {
            // All match, expect, check calls below are now delegated to parent_parser_ref_
            if ((lparen_token_opt = this->match(vyn::TokenType::LPAREN))) {
                vyn::SourceLocation call_loc = lparen_token_opt->location;
                std::vector<vyn::ExprPtr> args;
                if (!this->check(vyn::TokenType::RPAREN)) {
                    do {
                        args.push_back(this->parse_expression());
                    } while (this->match(vyn::TokenType::COMMA));
                }
                this->expect(vyn::TokenType::RPAREN);
                expr = std::make_unique<vyn::CallExpression>(call_loc, std::move(expr), std::move(args));
            } else if ((lbracket_token_opt = this->match(vyn::TokenType::LBRACKET))) {
                vyn::SourceLocation access_loc = lbracket_token_opt->location;
                vyn::ExprPtr index = this->parse_expression();
                this->expect(vyn::TokenType::RBRACKET);
                expr = std::make_unique<vyn::MemberExpression>(access_loc, std::move(expr), std::move(index), true /* computed */);
            } else if ((dot_token_opt = this->match(vyn::TokenType::DOT))) {
                vyn::SourceLocation access_loc = dot_token_opt->location;
                if (this->peek().type != vyn::TokenType::IDENTIFIER) {
                    throw std::runtime_error("Expected identifier after '.' operator at " + 
                        access_loc.filePath + ":" + 
                        std::to_string(access_loc.line) + ":" + 
                        std::to_string(access_loc.column));
                }
                vyn::token::Token member_token = this->consume(); // Get the identifier
                auto member_identifier = std::make_unique<vyn::Identifier>(member_token.location, member_token.lexeme);
                expr = std::make_unique<vyn::MemberExpression>(access_loc, std::move(expr), std::move(member_identifier), false /* computed */);
            } else if ((coloncolon_token_opt = this->match(vyn::TokenType::COLONCOLON))) {
                vyn::SourceLocation access_loc = coloncolon_token_opt->location;
                if (this->peek().type != vyn::TokenType::IDENTIFIER) {
                    throw std::runtime_error("Expected identifier after '::' operator at " + 
                        access_loc.filePath + ":" + 
                        std::to_string(access_loc.line) + ":" + 
                        std::to_string(access_loc.column));
                }
                vyn::token::Token member_token = this->consume(); // Get the identifier
                auto member_identifier = std::make_unique<vyn::Identifier>(member_token.location, member_token.lexeme);
                expr = std::make_unique<vyn::MemberExpression>(access_loc, std::move(expr), std::move(member_identifier), false /* computed */);
            } else {
                break;
            }
        }
        return expr;
    }

    vyn::ExprPtr ExpressionParser::parse_unary_expr() {
        // All peek, consume, match calls below are now delegated
        skip_comments_and_newlines();
        const vyn::token::Token& current_token = peek();

        if (current_token.type == vyn::TokenType::KEYWORD_BORROW) {
            consume(); 
            vyn::ExprPtr expr_to_borrow = this->parse_unary_expr();
            return std::make_unique<vyn::BorrowExprNode>(current_token.location, std::move(expr_to_borrow), vyn::BorrowKind::MUTABLE_BORROW);
        } else if (current_token.type == vyn::TokenType::KEYWORD_VIEW) {
            consume(); 
            vyn::ExprPtr expr_to_view = this->parse_unary_expr(); 
            return std::make_unique<vyn::BorrowExprNode>(current_token.location, std::move(expr_to_view), vyn::BorrowKind::IMMUTABLE_VIEW);
        }

        std::optional<vyn::token::Token> op_token_opt;
        if ((op_token_opt = this->match({vyn::TokenType::PLUS, vyn::TokenType::MINUS, vyn::TokenType::BANG}))) {
            vyn::token::Token op_token = *op_token_opt;
            vyn::ExprPtr right = this->parse_unary_expr();
            return std::make_unique<vyn::UnaryExpression>(op_token.location, op_token, std::move(right));
        }
        return this->parse_postfix_expr();
    }

    vyn::ExprPtr ExpressionParser::parse_multiplicative_expr() {
        vyn::ExprPtr expr = this->parse_unary_expr();
        std::optional<vyn::token::Token> op_token_opt;
        while ((op_token_opt = this->match({vyn::TokenType::MULTIPLY, vyn::TokenType::DIVIDE, vyn::TokenType::MODULO}))) {
            vyn::token::Token op_token = *op_token_opt;
            vyn::ExprPtr right = this->parse_unary_expr();
            expr = std::make_unique<vyn::BinaryExpression>(op_token.location, std::move(expr), op_token, std::move(right));
        }
        return expr;
    }

    vyn::ExprPtr ExpressionParser::parse_additive_expr() {
        vyn::ExprPtr expr = this->parse_multiplicative_expr();
        std::optional<vyn::token::Token> op_token_opt;
        while ((op_token_opt = this->match({vyn::TokenType::PLUS, vyn::TokenType::MINUS}))) {
            vyn::token::Token op_token = *op_token_opt;
            vyn::ExprPtr right = this->parse_multiplicative_expr();
            expr = std::make_unique<vyn::BinaryExpression>(op_token.location, std::move(expr), op_token, std::move(right));
        }
        return expr;
    }

    vyn::ExprPtr ExpressionParser::parse_shift_expr() {
        vyn::ExprPtr expr = this->parse_additive_expr();
        std::optional<vyn::token::Token> op_token_opt;
        // Corrected to use `this->match` which is now the delegated version.
        while ((op_token_opt = this->match({vyn::TokenType::LSHIFT, vyn::TokenType::RSHIFT}))) {
            // Ensure LSHIFT and RSHIFT are defined in vyn::TokenType
            vyn::token::Token op_token = *op_token_opt;
            vyn::ExprPtr right = this->parse_additive_expr();
            expr = std::make_unique<vyn::BinaryExpression>(op_token.location, std::move(expr), op_token, std::move(right));
        }
        return expr;
    }

    vyn::ExprPtr ExpressionParser::parse_relational_expr() {
        vyn::ExprPtr expr = this->parse_shift_expr();
        std::optional<vyn::token::Token> op_token_opt;
        while ((op_token_opt = this->match({vyn::TokenType::LT, vyn::TokenType::GT, vyn::TokenType::LTEQ, vyn::TokenType::GTEQ}))) {
            vyn::token::Token op_token = *op_token_opt;
            vyn::ExprPtr right = this->parse_shift_expr();
            expr = std::make_unique<vyn::BinaryExpression>(op_token.location, std::move(expr), op_token, std::move(right));
        }
        return expr;
    }

    vyn::ExprPtr ExpressionParser::parse_equality_expr() {
        vyn::ExprPtr expr = this->parse_relational_expr();
        std::optional<vyn::token::Token> op_token_opt;
        while ((op_token_opt = this->match({vyn::TokenType::EQEQ, vyn::TokenType::NOTEQ}))) {
            vyn::token::Token op_token = *op_token_opt;
            vyn::ExprPtr right = this->parse_relational_expr();
            expr = std::make_unique<vyn::BinaryExpression>(op_token.location, std::move(expr), op_token, std::move(right));
        }
        return expr;
    }

    vyn::ExprPtr ExpressionParser::parse_bitwise_and_expr() {
        vyn::ExprPtr expr = this->parse_equality_expr();
        std::optional<vyn::token::Token> op_token_opt;
        while ((op_token_opt = this->match({vyn::TokenType::AMPERSAND}))) {
            vyn::token::Token op_token = *op_token_opt;
            vyn::ExprPtr right = this->parse_equality_expr();
            expr = std::make_unique<vyn::BinaryExpression>(op_token.location, std::move(expr), op_token, std::move(right));
        }
        return expr;
    }

    vyn::ExprPtr ExpressionParser::parse_bitwise_xor_expr() {
        vyn::ExprPtr expr = this->parse_bitwise_and_expr();
        std::optional<vyn::token::Token> op_token_opt;
        // Corrected to use `this->match`
        while ((op_token_opt = this->match({vyn::TokenType::CARET}))) {
            // Ensure CARET is defined in vyn::TokenType
            vyn::token::Token op_token = *op_token_opt;
            vyn::ExprPtr right = this->parse_bitwise_and_expr();
            expr = std::make_unique<vyn::BinaryExpression>(op_token.location, std::move(expr), op_token, std::move(right));
        }
        return expr;
    }

    vyn::ExprPtr ExpressionParser::parse_bitwise_or_expr() {
        vyn::ExprPtr expr = this->parse_bitwise_xor_expr();
        std::optional<vyn::token::Token> op_token_opt;
        // Corrected to use `this->match`
        while ((op_token_opt = this->match({vyn::TokenType::PIPE}))) {
            // Ensure PIPE is defined in vyn::TokenType
            vyn::token::Token op_token = *op_token_opt;
            vyn::ExprPtr right = this->parse_bitwise_xor_expr();
            expr = std::make_unique<vyn::BinaryExpression>(op_token.location, std::move(expr), op_token, std::move(right));
        }
        return expr;
    }

    vyn::ExprPtr ExpressionParser::parse_logical_and_expr() {
        vyn::ExprPtr expr = this->parse_bitwise_or_expr();
        std::optional<vyn::token::Token> op_token_opt;
        while ((op_token_opt = this->match({vyn::TokenType::AND}))) { 
            vyn::token::Token op_token = *op_token_opt;
            vyn::ExprPtr right = this->parse_bitwise_or_expr();
            expr = std::make_unique<vyn::BinaryExpression>(op_token.location, std::move(expr), op_token, std::move(right));
        }
        return expr;
    }

    vyn::ExprPtr ExpressionParser::parse_logical_or_expr() {
        vyn::ExprPtr expr = this->parse_logical_and_expr();
        std::optional<vyn::token::Token> op_token_opt;
        while ((op_token_opt = this->match({vyn::TokenType::OR}))) { 
            vyn::token::Token op_token = *op_token_opt;
            vyn::ExprPtr right = this->parse_logical_and_expr();
            expr = std::make_unique<vyn::BinaryExpression>(op_token.location, std::move(expr), op_token, std::move(right));
        }
        return expr;
    }
    
    // Add support for range expressions x..y
    vyn::ExprPtr ExpressionParser::parse_range_expr() {
        vyn::ExprPtr expr = this->parse_logical_or_expr();
        std::optional<vyn::token::Token> op_token_opt;
        if ((op_token_opt = this->match({vyn::TokenType::DOTDOT}))) {
            vyn::token::Token op_token = *op_token_opt;
            vyn::ExprPtr right = this->parse_logical_or_expr();
            // Create a binary expression for range expression
            expr = std::make_unique<vyn::BinaryExpression>(op_token.location, std::move(expr), op_token, std::move(right));
        }
        return expr;
    }

    vyn::ExprPtr ExpressionParser::parse_assignment_expr() {
        vyn::ExprPtr left = this->parse_range_expr(); // Updated to call parse_range_expr

        if (auto op_token_opt = this->match({vyn::TokenType::EQ}); op_token_opt) {
            vyn::token::Token op_token = *op_token_opt;
            vyn::ExprPtr right = this->parse_assignment_expr(); // Right-associative
            return std::make_unique<vyn::AssignmentExpression>(op_token.location, std::move(left), op_token, std::move(right));
        }
        return left;
    }

    vyn::ExprPtr ExpressionParser::parse_expression() {
        return this->parse_assignment_expr();
    }

} // namespace vyn
