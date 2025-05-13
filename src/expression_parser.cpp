#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include <stdexcept>
#include <string> // Required for std::to_string
#include <algorithm> // Required for std::any_of, if used by match or other helpers

namespace Vyn {

    ExpressionParser::ExpressionParser(const std::vector<Token>& tokens, size_t& pos, const std::string& file_path)
        : BaseParser(tokens, pos, file_path) {}

    AST::ExprPtr ExpressionParser::parse() {
        return this->parse_expression();
    }

    AST::ExprPtr ExpressionParser::parse_atom() {
        const Token& token = this->peek(); // Use peek()

        if (token.type == Vyn::TokenType::IDENTIFIER) {
            this->consume(); // Consume the token
            return std::make_unique<Vyn::AST::IdentifierNode>(token.location, token.lexeme);
        } else if (token.type == Vyn::TokenType::INT_LITERAL) {
            this->consume(); // Consume the token
            return std::make_unique<Vyn::AST::IntLiteralNode>(token.location, std::stoll(token.lexeme));
        } else if (token.type == Vyn::TokenType::FLOAT_LITERAL) {
            this->consume(); // Consume the token
            return std::make_unique<Vyn::AST::FloatLiteralNode>(token.location, std::stod(token.lexeme));
        } else if (token.type == Vyn::TokenType::STRING_LITERAL) {
            this->consume(); // Consume the token
            return std::make_unique<Vyn::AST::StringLiteralNode>(token.location, token.lexeme);
        // } else if (token.type == Vyn::TokenType::CHAR_LITERAL) { // CHAR_LITERAL not in token.hpp
        //     this->consume(); // Consume the token
        //     return std::make_unique<Vyn::AST::CharLiteralNode>(token.location, token.lexeme[0]);
        // } else if (token.type == Vyn::TokenType::KEYWORD_TRUE || token.type == Vyn::TokenType::KEYWORD_FALSE) { // KEYWORD_TRUE/FALSE not in token.hpp
        //     this->consume(); // Consume the token
        //     return std::make_unique<Vyn::AST::BoolLiteralNode>(token.location, token.type == Vyn::TokenType::KEYWORD_TRUE);
        // } else if (token.type == Vyn::TokenType::KEYWORD_NULL) { // KEYWORD_NULL not in token.hpp
        //     this->consume(); // Consume the token
        //     return std::make_unique<Vyn::AST::NullLiteralNode>(token.location);
        } else if (token.type == Vyn::TokenType::LPAREN) {
            this->consume(); // Consume LPAREN
            Vyn::AST::ExprPtr expr = this->parse_expression();
            this->expect(Vyn::TokenType::RPAREN);
            return expr;
        }
        
        Vyn::AST::SourceLocation loc = this->current_location();
        throw std::runtime_error("Unexpected token in atom: " + token.lexeme + " at " +
                                 loc.filePath + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column));
    }

    AST::ExprPtr ExpressionParser::parse_postfix_expr() {
        Vyn::AST::ExprPtr expr = this->parse_atom();
        std::optional<Token> lparen_token_opt;
        std::optional<Token> lbracket_token_opt;
        std::optional<Token> dot_token_opt;
        while (true) {
            if ((lparen_token_opt = this->match({Vyn::TokenType::LPAREN}))) {
                Vyn::AST::SourceLocation call_loc = lparen_token_opt->location;
                std::vector<Vyn::AST::ExprPtr> args;
                if (!this->check(Vyn::TokenType::RPAREN)) {
                    do {
                        args.push_back(this->parse_expression());
                    } while (this->match({Vyn::TokenType::COMMA}));
                }
                this->expect(Vyn::TokenType::RPAREN);
                expr = std::make_unique<Vyn::AST::CallExprNode>(call_loc, std::move(expr), std::move(args));
            } else if ((lbracket_token_opt = this->match({Vyn::TokenType::LBRACKET}))) {
                Vyn::AST::SourceLocation access_loc = lbracket_token_opt->location;
                Vyn::AST::ExprPtr index = this->parse_expression();
                this->expect(Vyn::TokenType::RBRACKET);
                expr = std::make_unique<Vyn::AST::ArrayAccessNode>(access_loc, std::move(expr), std::move(index));
            } else if ((dot_token_opt = this->match({Vyn::TokenType::DOT}))) {
                Vyn::AST::SourceLocation access_loc = dot_token_opt->location;
                Token member_token = this->expect(Vyn::TokenType::IDENTIFIER);
                auto member_identifier = std::make_unique<Vyn::AST::IdentifierNode>(member_token.location, member_token.lexeme);
                expr = std::make_unique<Vyn::AST::MemberAccessNode>(access_loc, std::move(expr), std::move(member_identifier));
            } else {
                break;
            }
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_unary_expr() {
        std::optional<Token> op_token_opt;
        if ((op_token_opt = this->match({Vyn::TokenType::PLUS, Vyn::TokenType::MINUS, Vyn::TokenType::BANG /*, Vyn::TokenType::TILDE // TILDE not in token.hpp */}))) {
            Token op_token = *op_token_opt;
            Vyn::AST::ExprPtr right = this->parse_unary_expr();
            return std::make_unique<Vyn::AST::UnaryOpNode>(op_token.location, op_token.type, std::move(right));
        }
        return this->parse_postfix_expr();
    }

    AST::ExprPtr ExpressionParser::parse_multiplicative_expr() {
        Vyn::AST::ExprPtr expr = this->parse_unary_expr();
        std::optional<Token> op_token_opt;
        while ((op_token_opt = this->match({Vyn::TokenType::MULTIPLY, Vyn::TokenType::DIVIDE, Vyn::TokenType::MODULO}))) {
            Token op_token = *op_token_opt;
            Vyn::AST::ExprPtr right = this->parse_unary_expr();
            expr = std::make_unique<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_additive_expr() {
        Vyn::AST::ExprPtr expr = this->parse_multiplicative_expr();
        std::optional<Token> op_token_opt;
        while ((op_token_opt = this->match({Vyn::TokenType::PLUS, Vyn::TokenType::MINUS}))) {
            Token op_token = *op_token_opt;
            Vyn::AST::ExprPtr right = this->parse_multiplicative_expr();
            expr = std::make_unique<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_shift_expr() {
        Vyn::AST::ExprPtr expr = this->parse_additive_expr();
        // LSHIFT, RSHIFT not in token.hpp
        // std::optional<Token> op_token_opt;
        // while ((op_token_opt = this->match({Vyn::TokenType::LSHIFT, Vyn::TokenType::RSHIFT}))) {
        //     Token op_token = *op_token_opt;
        //     Vyn::AST::ExprPtr right = this->parse_additive_expr();
        //     expr = std::make_unique<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        // }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_relational_expr() {
        Vyn::AST::ExprPtr expr = this->parse_shift_expr();
        std::optional<Token> op_token_opt;
        while ((op_token_opt = this->match({Vyn::TokenType::LT, Vyn::TokenType::GT, Vyn::TokenType::LTEQ, Vyn::TokenType::GTEQ}))) {
            Token op_token = *op_token_opt;
            Vyn::AST::ExprPtr right = this->parse_shift_expr();
            expr = std::make_unique<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_equality_expr() {
        Vyn::AST::ExprPtr expr = this->parse_relational_expr();
        std::optional<Token> op_token_opt;
        while ((op_token_opt = this->match({Vyn::TokenType::EQEQ, Vyn::TokenType::NOTEQ}))) {
            Token op_token = *op_token_opt;
            Vyn::AST::ExprPtr right = this->parse_relational_expr();
            expr = std::make_unique<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_bitwise_and_expr() {
        Vyn::AST::ExprPtr expr = this->parse_equality_expr();
        std::optional<Token> op_token_opt;
        while ((op_token_opt = this->match({Vyn::TokenType::AMPERSAND}))) {
            Token op_token = *op_token_opt;
            Vyn::AST::ExprPtr right = this->parse_equality_expr();
            expr = std::make_unique<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_bitwise_xor_expr() {
        Vyn::AST::ExprPtr expr = this->parse_bitwise_and_expr();
        // CARET not in token.hpp
        // std::optional<Token> op_token_opt;
        // while ((op_token_opt = this->match({Vyn::TokenType::CARET}))) {
        //     Token op_token = *op_token_opt;
        //     Vyn::AST::ExprPtr right = this->parse_bitwise_and_expr();
        //     expr = std::make_unique<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        // }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_bitwise_or_expr() {
        Vyn::AST::ExprPtr expr = this->parse_bitwise_xor_expr();
        // PIPE not in token.hpp
        // std::optional<Token> op_token_opt;
        // while ((op_token_opt = this->match({Vyn::TokenType::PIPE}))) {
        //     Token op_token = *op_token_opt;
        //     Vyn::AST::ExprPtr right = this->parse_bitwise_xor_expr();
        //     expr = std::make_unique<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        // }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_logical_and_expr() {
        Vyn::AST::ExprPtr expr = this->parse_bitwise_or_expr();
        std::optional<Token> op_token_opt;
        while ((op_token_opt = this->match({Vyn::TokenType::AND}))) { // Changed AND_LOGICAL to AND
            Token op_token = *op_token_opt;
            Vyn::AST::ExprPtr right = this->parse_bitwise_or_expr();
            expr = std::make_unique<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_logical_or_expr() {
        Vyn::AST::ExprPtr expr = this->parse_logical_and_expr();
        std::optional<Token> op_token_opt;
        while ((op_token_opt = this->match({Vyn::TokenType::OR}))) { // Changed OR_LOGICAL to OR
            Token op_token = *op_token_opt;
            Vyn::AST::ExprPtr right = this->parse_logical_and_expr();
            expr = std::make_unique<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }
    
    // Conditional expression parsing (parse_conditional_expr) is often part of assignment or a separate precedence level.
    // The provided parser.hpp doesn't list it. If it's needed, it should be:
    // 1. Declared in ExpressionParser private section in parser.hpp
    // 2. Implemented here.
    // 3. parse_assignment_expr should call parse_conditional_expr instead of parse_logical_or_expr.
    // For now, assuming it's not a separate explicit step based on parser.hpp.

    AST::ExprPtr ExpressionParser::parse_assignment_expr() {
        Vyn::AST::ExprPtr left = this->parse_logical_or_expr(); 

        // Only EQ is kept. Compound assignments are not in token.hpp and AssignmentNode doesn't store operator type.
        if (auto op_token_opt = this->match({Vyn::TokenType::EQ}); op_token_opt) {
            Token op_token = *op_token_opt;
            Vyn::AST::ExprPtr right = this->parse_assignment_expr(); // Right-associative
            return std::make_unique<Vyn::AST::AssignmentNode>(op_token.location, std::move(left), std::move(right));
        }
        return left;
    }

    AST::ExprPtr ExpressionParser::parse_expression() {
        return this->parse_assignment_expr();
    }

} // namespace Vyn
