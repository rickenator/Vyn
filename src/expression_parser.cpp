#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include <stdexcept>
#include <string> // Required for std::to_string
#include <algorithm> // Required for std::any_of, if used by match or other helpers

namespace vyn {

    ExpressionParser::ExpressionParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, const std::string& file_path)
        : BaseParser(tokens, pos, file_path) {}

    vyn::ExprPtr ExpressionParser::parse() {
        return this->parse_expression();
    }

    vyn::ExprPtr ExpressionParser::parse_atom() {
        const vyn::token::Token& token = this->peek();

        if (token.type == vyn::TokenType::IDENTIFIER) {
            this->consume();
            return std::make_unique<vyn::Identifier>(token.location, token.lexeme);
        } else if (token.type == vyn::TokenType::INT_LITERAL) {
            this->consume();
            return std::make_unique<vyn::IntegerLiteral>(token.location, std::stoll(token.lexeme));
        } else if (token.type == vyn::TokenType::FLOAT_LITERAL) {
            this->consume();
            return std::make_unique<vyn::FloatLiteral>(token.location, std::stod(token.lexeme));
        } else if (token.type == vyn::TokenType::STRING_LITERAL) {
            this->consume();
            return std::make_unique<vyn::StringLiteral>(token.location, token.lexeme);
        } else if (token.type == vyn::TokenType::KEYWORD_TRUE || token.type == vyn::TokenType::KEYWORD_FALSE) {
            this->consume();
            return std::make_unique<vyn::BooleanLiteral>(token.location, token.type == vyn::TokenType::KEYWORD_TRUE);
        } else if (token.type == vyn::TokenType::KEYWORD_NIL) { 
            this->consume();
            return std::make_unique<vyn::NilLiteral>(token.location);
        } else if (token.type == vyn::TokenType::LPAREN) {
            this->consume(); 
            vyn::ExprPtr expr = this->parse_expression();
            this->expect(vyn::TokenType::RPAREN);
            return expr;
        // Removed KEYWORD_BORROW and KEYWORD_VIEW from here, they are handled in parse_unary_expr
        }
        
        vyn::SourceLocation loc = this->current_location();
        throw std::runtime_error("Unexpected token in atom: " + token.lexeme + " at " +
                                 loc.filePath + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column));
    }

    vyn::ExprPtr ExpressionParser::parse_postfix_expr() {
        vyn::ExprPtr expr = this->parse_atom();
        std::optional<vyn::token::Token> lparen_token_opt;
        std::optional<vyn::token::Token> lbracket_token_opt;
        std::optional<vyn::token::Token> dot_token_opt;
        while (true) {
            if ((lparen_token_opt = this->match({vyn::TokenType::LPAREN}))) {
                vyn::SourceLocation call_loc = lparen_token_opt->location;
                std::vector<vyn::ExprPtr> args;
                if (!this->check(vyn::TokenType::RPAREN)) {
                    do {
                        args.push_back(this->parse_expression());
                    } while (this->match({vyn::TokenType::COMMA}));
                }
                this->expect(vyn::TokenType::RPAREN);
                expr = std::make_unique<vyn::CallExpression>(call_loc, std::move(expr), std::move(args));
            } else if ((lbracket_token_opt = this->match({vyn::TokenType::LBRACKET}))) {
                vyn::SourceLocation access_loc = lbracket_token_opt->location;
                vyn::ExprPtr index = this->parse_expression();
                this->expect(vyn::TokenType::RBRACKET);
                expr = std::make_unique<vyn::MemberExpression>(access_loc, std::move(expr), std::move(index), true /* computed */);
            } else if ((dot_token_opt = this->match({vyn::TokenType::DOT}))) {
                vyn::SourceLocation access_loc = dot_token_opt->location;
                vyn::token::Token member_token = this->expect(vyn::TokenType::IDENTIFIER);
                auto member_identifier = std::make_unique<vyn::Identifier>(member_token.location, member_token.lexeme);
                expr = std::make_unique<vyn::MemberExpression>(access_loc, std::move(expr), std::move(member_identifier), false /* computed */);
            } else {
                break;
            }
        }
        return expr;
    }

    vyn::ExprPtr ExpressionParser::parse_unary_expr() {
        std::optional<vyn::token::Token> op_token_opt;
        const vyn::token::Token& current_token = this->peek();

        if (current_token.type == vyn::TokenType::KEYWORD_BORROW) {
            this->consume(); // consume 'borrow'
            // According to RFC: borrow <expr>. No parentheses required by default.
            // If parentheses are desired for grouping, they'd be part of <expr>.
            vyn::ExprPtr expr_to_borrow = this->parse_unary_expr(); // Borrow has high precedence, applies to the following unary expression
            return std::make_unique<vyn::BorrowExprNode>(current_token.location, std::move(expr_to_borrow), vyn::BorrowKind::MUTABLE_BORROW);
        } else if (current_token.type == vyn::TokenType::KEYWORD_VIEW) {
            this->consume(); // consume 'view'
            vyn::ExprPtr expr_to_view = this->parse_unary_expr(); // View has high precedence
            return std::make_unique<vyn::BorrowExprNode>(current_token.location, std::move(expr_to_view), vyn::BorrowKind::IMMUTABLE_VIEW);
        }

        if ((op_token_opt = this->match({vyn::TokenType::PLUS, vyn::TokenType::MINUS, vyn::TokenType::BANG /*, vyn::TokenType::TILDE */}))) {
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
        // LSHIFT, RSHIFT not in token.hpp
        // std::optional<vyn::token::Token> op_token_opt;
        // while ((op_token_opt = this->match({vyn::TokenType::LSHIFT, vyn::TokenType::RSHIFT}))) {
        //     vyn::token::Token op_token = *op_token_opt;
        //     vyn::ExprPtr right = this->parse_additive_expr();
        //     expr = std::make_unique<vyn::BinaryExpression>(op_token.location, std::move(expr), op_token, std::move(right));
        // }
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
        // CARET not in token.hpp
        // std::optional<vyn::token::Token> op_token_opt;
        // while ((op_token_opt = this->match({vyn::TokenType::CARET}))) {
        //     vyn::token::Token op_token = *op_token_opt;
        //     vyn::ExprPtr right = this->parse_bitwise_and_expr();
        //     expr = std::make_unique<vyn::BinaryExpression>(op_token.location, std::move(expr), op_token, std::move(right));
        // }
        return expr;
    }

    vyn::ExprPtr ExpressionParser::parse_bitwise_or_expr() {
        vyn::ExprPtr expr = this->parse_bitwise_xor_expr();
        // PIPE not in token.hpp
        // std::optional<vyn::token::Token> op_token_opt;
        // while ((op_token_opt = this->match({vyn::TokenType::PIPE}))) {
        //     vyn::token::Token op_token = *op_token_opt;
        //     vyn::ExprPtr right = this->parse_bitwise_xor_expr();
        //     expr = std::make_unique<vyn::BinaryExpression>(op_token.location, std::move(expr), op_token, std::move(right));
        // }
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
    
    // Conditional expression parsing (parse_conditional_expr) is often part of assignment or a separate precedence level.
    // The provided parser.hpp doesn't list it. If it's needed, it should be:
    // 1. Declared in ExpressionParser private section in parser.hpp
    // 2. Implemented here.
    // 3. parse_assignment_expr should call parse_conditional_expr instead of parse_logical_or_expr.
    // For now, assuming it's not a separate explicit step based on parser.hpp.

    vyn::ExprPtr ExpressionParser::parse_assignment_expr() {
        vyn::ExprPtr left = this->parse_logical_or_expr(); 

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
