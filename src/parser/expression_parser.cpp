#include "vyn/parser/parser.hpp" // For BaseParser and other parser components
#include "vyn/parser/ast.hpp"      // For AST node types like IntegerLiteral, etc.
#include "vyn/parser/token.hpp"    // For TokenType and Token
#include <stdexcept>               // For std::runtime_error
#include <algorithm> // Required for std::any_of, if used by match or other helpers
#include <functional> // Required for std::function
#include <vector> // Required for std::vector

namespace vyn {

    // Global helper function for converting SourceLocation to string
    inline std::string location_to_string(const vyn::SourceLocation& loc) {
        return loc.filePath + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column);
    }


    // Constructor
    ExpressionParser::ExpressionParser(const std::vector<token::Token>& tokens, size_t& pos, const std::string& file_path)
        : BaseParser(tokens, pos, file_path) {}

    // Public method to start parsing an expression
    vyn::ast::ExprPtr ExpressionParser::parse_expression() {
        return parse_assignment_expr(); // Start with the lowest precedence
    }

    // Parses assignment expressions (e.g., x = 10, y += 5)
    vyn::ast::ExprPtr ExpressionParser::parse_assignment_expr() {
        vyn::ast::ExprPtr left = parse_logical_or_expr(); 
        SourceLocation op_loc = peek().location; 

        if (match(TokenType::EQ)) {
            token::Token op_token = previous_token(); 
            vyn::ast::ExprPtr right = parse_assignment_expr(); 
            if (dynamic_cast<ast::Identifier*>(left.get()) || dynamic_cast<ast::MemberExpression*>(left.get())) {
                return std::make_unique<ast::AssignmentExpression>(op_loc, std::move(left), op_token, std::move(right));
            } else {
                // Use the error reporting mechanism from BaseParser
                // this->errors.push_back("Invalid left-hand side in assignment expression at line " + std::to_string(op_loc.line) + ", column " + std::to_string(op_loc.column));
                // For now, let's throw, assuming error() is available and throws.
                // If BaseParser has an error vector, it should be used instead.
                // Assuming BaseParser::error() is available and suitable.
                // If not, this needs adjustment based on how errors are collected.
                // For now, throwing a runtime_error as a placeholder for proper error handling.
                throw std::runtime_error("Invalid left-hand side in assignment expression at " + location_to_string(op_loc));
                return nullptr; 
            }
        }
        return left; 
    }

    vyn::ast::ExprPtr ExpressionParser::parse_call_expression(vyn::ast::ExprPtr callee_expr) {
        std::vector<vyn::ast::ExprPtr> arguments;
        SourceLocation call_loc = previous_token().location; 

        if (!match(TokenType::RPAREN)) { 
            do {
                arguments.push_back(parse_expression());
            } while (match(TokenType::COMMA));
            expect(TokenType::RPAREN);
        }
        return std::make_unique<ast::CallExpression>(call_loc, std::move(callee_expr), std::move(arguments));
    }

    vyn::ast::ExprPtr ExpressionParser::parse_member_access(vyn::ast::ExprPtr object) {
        SourceLocation member_loc = peek().location;
        if (peek().type == TokenType::IDENTIFIER) {
            token::Token property_token = consume(); 
            auto property_identifier = std::make_unique<ast::Identifier>(property_token.location, property_token.lexeme);
            return std::make_unique<ast::MemberExpression>(member_loc, std::move(object), std::move(property_identifier), false /* not computed */);
        }
        // this->errors.push_back("Expected identifier for member access at " + location_to_string(member_loc));
        throw std::runtime_error("Expected identifier for member access at " + location_to_string(member_loc));
        return nullptr; 
    }

    vyn::ast::ExprPtr ExpressionParser::parse_primary() {
        SourceLocation loc = peek().location;
        if (is_literal(peek().type)) { // is_literal needs to be a member or passed BaseParser
            return parse_literal();
        }
        if (match(TokenType::IDENTIFIER)) {
            return std::make_unique<ast::Identifier>(previous_token().location, previous_token().lexeme);
        }
        if (match(TokenType::LPAREN)) {
            vyn::ast::ExprPtr expr = parse_expression();
            expect(TokenType::RPAREN);
            return expr;
        }
        // this->errors.push_back("Expected primary expression at " + location_to_string(loc));
        throw std::runtime_error("Expected primary expression at " + location_to_string(loc));
        return nullptr;
    }

    // Parses literal expressions (integers, floats, strings, booleans, null)
    vyn::ast::ExprPtr ExpressionParser::parse_literal() {
        token::Token current_token = peek();
        switch (current_token.type) {
            case TokenType::INT_LITERAL: {
                consume(); 
                return std::make_unique<ast::IntegerLiteral>(current_token.location, std::stoll(current_token.lexeme));
            }
            case TokenType::FLOAT_LITERAL: {
                consume();
                return std::make_unique<ast::FloatLiteral>(current_token.location, std::stod(current_token.lexeme));
            }
            case TokenType::STRING_LITERAL: {
                consume();
                return std::make_unique<ast::StringLiteral>(current_token.location, current_token.lexeme);
            }
            case TokenType::KEYWORD_TRUE: {
                consume();
                return std::make_unique<ast::BooleanLiteral>(current_token.location, true);
            }
            case TokenType::KEYWORD_FALSE: {
                consume();
                return std::make_unique<ast::BooleanLiteral>(current_token.location, false);
            }
            case TokenType::KEYWORD_NULL:
            case TokenType::KEYWORD_NIL: {
                consume();
                return std::make_unique<ast::NilLiteral>(current_token.location);
            }
            default:
                // this->errors.push_back("Unexpected token in parse_literal: " + token_type_to_string(current_token.type) + " at " + location_to_string(current_token.location));
                throw std::runtime_error("Unexpected token in parse_literal: " + token_type_to_string(current_token.type) + " at " + location_to_string(current_token.location));
                return nullptr;
        }
    }

    vyn::ast::ExprPtr ExpressionParser::parse_atom() {
        return parse_primary(); 
    }

    vyn::ast::ExprPtr ExpressionParser::parse_binary_expression(std::function<vyn::ast::ExprPtr()> parse_higher_precedence, const std::vector<TokenType>& operators) {
        vyn::ast::ExprPtr left = parse_higher_precedence();

        while (std::any_of(operators.begin(), operators.end(), [&](TokenType op_type){ return match(op_type); })) {
            token::Token op_token = previous_token();
            vyn::ast::ExprPtr right = parse_higher_precedence(); // Should be parse_higher_precedence for right-associativity of some ops, or current level for left-associativity
            left = std::make_unique<ast::BinaryExpression>(op_token.location, std::move(left), op_token, std::move(right));
        }
        return left;
    }

    vyn::ast::ExprPtr ExpressionParser::parse_logical_or_expr() {
        return parse_binary_expression([this]() { return parse_logical_and_expr(); }, {TokenType::OR});
    }

    vyn::ast::ExprPtr ExpressionParser::parse_logical_and_expr() {
        return parse_binary_expression([this]() { return parse_bitwise_or_expr(); }, {TokenType::AND});
    }

    vyn::ast::ExprPtr ExpressionParser::parse_bitwise_or_expr() {
        return parse_binary_expression([this]() { return parse_bitwise_xor_expr(); }, {TokenType::PIPE});
    }

    vyn::ast::ExprPtr ExpressionParser::parse_bitwise_xor_expr() {
        return parse_binary_expression([this]() { return parse_bitwise_and_expr(); }, {TokenType::CARET});
    }

    vyn::ast::ExprPtr ExpressionParser::parse_bitwise_and_expr() {
        return parse_binary_expression([this]() { return parse_equality_expr(); }, {TokenType::AMPERSAND});
    }

    vyn::ast::ExprPtr ExpressionParser::parse_equality_expr() {
        return parse_binary_expression([this]() { return parse_relational_expr(); }, {TokenType::EQEQ, TokenType::NOTEQ});
    }

    vyn::ast::ExprPtr ExpressionParser::parse_relational_expr() {
        return parse_binary_expression([this]() { return parse_shift_expr(); }, {TokenType::LT, TokenType::LTEQ, TokenType::GT, TokenType::GTEQ});
    }

    vyn::ast::ExprPtr ExpressionParser::parse_shift_expr() {
        return parse_binary_expression([this]() { return parse_additive_expr(); }, {TokenType::LSHIFT, TokenType::RSHIFT});
    }

    vyn::ast::ExprPtr ExpressionParser::parse_additive_expr() {
        return parse_binary_expression([this]() { return parse_multiplicative_expr(); }, {TokenType::PLUS, TokenType::MINUS});
    }

    vyn::ast::ExprPtr ExpressionParser::parse_multiplicative_expr() {
        return parse_binary_expression([this]() { return parse_unary_expr(); }, {TokenType::MULTIPLY, TokenType::DIVIDE, TokenType::MODULO});
    }

    vyn::ast::ExprPtr ExpressionParser::parse_unary_expr() {
        if (match(TokenType::BANG) || match(TokenType::MINUS) || match(TokenType::TILDE)) {
            token::Token op_token = previous_token();
            vyn::ast::ExprPtr operand = parse_unary_expr(); 
            return std::make_unique<ast::UnaryExpression>(op_token.location, op_token, std::move(operand));
        }
        return parse_postfix_expr();
    }

    vyn::ast::ExprPtr ExpressionParser::parse_postfix_expr() {
        vyn::ast::ExprPtr expr = parse_primary(); 

        while (true) {
            if (match(TokenType::LPAREN)) {
                expr = parse_call_expression(std::move(expr));
            } else if (match(TokenType::DOT)) {
                expr = parse_member_access(std::move(expr));
            } else if (match(TokenType::LBRACKET)) {
                // this->errors.push_back("Array subscript operator not yet implemented at " + location_to_string(previous_token().location));
                throw std::runtime_error("Array subscript operator not yet implemented at " + location_to_string(previous_token().location));
                expect(TokenType::RBRACKET); 
            } else {
                break;
            }
        }
        return expr;
    }

    vyn::ast::ExprPtr ExpressionParser::parse_primary_expr() {
        return parse_atom(); 
    }

    // Implementation for is_literal (can be moved to BaseParser if it's general)
    bool ExpressionParser::is_literal(TokenType type) const { // Added const
        switch (type) {
            case TokenType::INT_LITERAL:
            case TokenType::FLOAT_LITERAL:
            case TokenType::STRING_LITERAL:
            // case TokenType::CHAR_LITERAL: // If you add char literals back
            case TokenType::KEYWORD_TRUE:
            case TokenType::KEYWORD_FALSE:
            case TokenType::KEYWORD_NULL:
            case TokenType::KEYWORD_NIL:
                return true;
            default:
                return false;
        }
    }

    bool ExpressionParser::is_expression_start(vyn::TokenType type) const {
        // This is a basic check. You might need to expand this based on your grammar.
        // For example, if expressions can start with keywords like 'new', 'await', etc.
        // or specific operators for unary expressions.
        return is_literal(type) ||
               type == TokenType::IDENTIFIER ||
               type == TokenType::LPAREN || // Grouped expression
               type == TokenType::MINUS ||  // Unary minus
               type == TokenType::BANG ||   // Unary not
               type == TokenType::TILDE ||  // Unary bitwise not
               type == TokenType::LBRACKET || // Array literal (if supported)
               type == TokenType::LBRACE;   // Object literal or block (requires context)
    }

} // namespace vyn
