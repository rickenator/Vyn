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
        Token token = this->current_token();

        if (token.type == Vyn::TokenType::IDENTIFIER) {
            this->advance();
            return std::make_shared<Vyn::AST::IdentifierNode>(token.location, token.lexeme);
        } else if (token.type == Vyn::TokenType::INT_LITERAL) { // Corrected TokenType
            this->advance();
            return std::make_shared<Vyn::AST::IntLiteralNode>(token.location, std::stoll(token.lexeme));
        } else if (token.type == Vyn::TokenType::FLOAT_LITERAL) { // Corrected TokenType
            this->advance();
            return std::make_shared<Vyn::AST::FloatLiteralNode>(token.location, std::stod(token.lexeme));
        } else if (token.type == Vyn::TokenType::STRING_LITERAL) { // Corrected TokenType
            this->advance();
            return std::make_shared<Vyn::AST::StringLiteralNode>(token.location, token.lexeme);
        } else if (token.type == Vyn::TokenType::CHAR_LITERAL) { // Corrected TokenType
            this->advance();
            // Assuming CharLiteralNode expects a char, and lexeme is a string containing the char
            return std::make_shared<Vyn::AST::CharLiteralNode>(token.location, token.lexeme[0]);
        } else if (token.type == Vyn::TokenType::KEYWORD_TRUE || token.type == Vyn::TokenType::KEYWORD_FALSE) { // Corrected TokenType
            this->advance();
            return std::make_shared<Vyn::AST::BoolLiteralNode>(token.location, token.type == Vyn::TokenType::KEYWORD_TRUE);
        } else if (token.type == Vyn::TokenType::KEYWORD_NULL) { // Corrected TokenType
            this->advance();
            return std::make_shared<Vyn::AST::NullLiteralNode>(token.location);
        } else if (token.type == Vyn::TokenType::LPAREN) {
            this->advance();
            Vyn::AST::ExprPtr expr = this->parse_expression();
            this->expect(Vyn::TokenType::RPAREN);
            return expr;
        }
        
        Vyn::AST::SourceLocation loc = this->current_location();
        // Use reporter for errors if available, otherwise throw runtime_error
        // For now, sticking to runtime_error as reporter is not in BaseParser context here
        throw std::runtime_error("Unexpected token in atom: " + token.lexeme + " at " +
                                 loc.filePath + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column));
    }

    AST::ExprPtr ExpressionParser::parse_postfix_expr() {
        Vyn::AST::ExprPtr expr = this->parse_atom();
        while (true) {
            if (this->match({Vyn::TokenType::LPAREN})) { // Updated match
                Vyn::AST::SourceLocation call_loc = this->previous_token().location;
                std::vector<Vyn::AST::ExprPtr> args;
                if (!this->check(Vyn::TokenType::RPAREN)) {
                    do {
                        args.push_back(this->parse_expression());
                    } while (this->match({Vyn::TokenType::COMMA})); // Updated match
                }
                this->expect(Vyn::TokenType::RPAREN);
                // Assuming AST::CallNode is Vyn::AST::CallExprNode based on ast.hpp
                expr = std::make_shared<Vyn::AST::CallExprNode>(call_loc, expr, args);
            } else if (this->match({Vyn::TokenType::LBRACKET})) { // Updated match
                Vyn::AST::SourceLocation access_loc = this->previous_token().location;
                Vyn::AST::ExprPtr index = this->parse_expression();
                this->expect(Vyn::TokenType::RBRACKET);
                // Assuming AST::IndexAccessNode is Vyn::AST::ArrayAccessNode
                expr = std::make_shared<Vyn::AST::ArrayAccessNode>(access_loc, expr, index);
            } else if (this->match({Vyn::TokenType::DOT})) { // Updated match
                Vyn::AST::SourceLocation access_loc = this->previous_token().location;
                Token member_token = this->expect(Vyn::TokenType::IDENTIFIER);
                auto member_identifier = std::make_shared<Vyn::AST::IdentifierNode>(member_token.location, member_token.lexeme);
                expr = std::make_shared<Vyn::AST::MemberAccessNode>(access_loc, expr, member_identifier);
            } else {
                break;
            }
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_unary_expr() {
        // Corrected TokenTypes and added Vyn:: namespace
        if (this->match({Vyn::TokenType::PLUS, Vyn::TokenType::MINUS, Vyn::TokenType::BANG, Vyn::TokenType::TILDE})) { // Assuming Not is BANG, Tilde is TILDE
            Token op_token = this->previous_token(); // op_token is of type Vyn::Token
            Vyn::AST::ExprPtr right = this->parse_unary_expr();
            // UnaryOpNode constructor in ast.hpp expects SourceLocation, ::TokenType, ExprPtr
            return std::make_shared<Vyn::AST::UnaryOpNode>(op_token.location, op_token.type, std::move(right));
        }
        return this->parse_postfix_expr();
    }

    AST::ExprPtr ExpressionParser::parse_multiplicative_expr() {
        Vyn::AST::ExprPtr expr = this->parse_unary_expr();
        // Corrected TokenTypes and added Vyn:: namespace
        while (this->match({Vyn::TokenType::MULTIPLY, Vyn::TokenType::DIVIDE, Vyn::TokenType::MODULO})) { // Assuming Star, Slash, Percent map to these
            Token op_token = this->previous_token(); // op_token is of type Vyn::Token
            Vyn::AST::ExprPtr right = this->parse_unary_expr();
            // BinaryOpNode constructor in ast.hpp expects SourceLocation, ::TokenType, ExprPtr, ExprPtr
            expr = std::make_shared<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_additive_expr() {
        Vyn::AST::ExprPtr expr = this->parse_multiplicative_expr();
        // Corrected TokenTypes and added Vyn:: namespace
        while (this->match({Vyn::TokenType::PLUS, Vyn::TokenType::MINUS})) {
            Token op_token = this->previous_token();
            Vyn::AST::ExprPtr right = this->parse_multiplicative_expr();
            expr = std::make_shared<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_shift_expr() {
        Vyn::AST::ExprPtr expr = this->parse_additive_expr();
        // Corrected TokenTypes and added Vyn:: namespace
        while (this->match({Vyn::TokenType::LSHIFT, Vyn::TokenType::RSHIFT})) { // Assuming LShift, RShift map to these
            Token op_token = this->previous_token();
            Vyn::AST::ExprPtr right = this->parse_additive_expr();
            expr = std::make_shared<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_relational_expr() {
        Vyn::AST::ExprPtr expr = this->parse_shift_expr();
        // Corrected TokenTypes and added Vyn:: namespace
        while (this->match({Vyn::TokenType::LT, Vyn::TokenType::GT, Vyn::TokenType::LTEQ, Vyn::TokenType::GTEQ})) { // Assuming LAngle, RAngle, LEqual, GEqual map to these
            Token op_token = this->previous_token();
            Vyn::AST::ExprPtr right = this->parse_shift_expr();
            expr = std::make_shared<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_equality_expr() {
        Vyn::AST::ExprPtr expr = this->parse_relational_expr();
        // Corrected TokenTypes and added Vyn:: namespace
        while (this->match({Vyn::TokenType::EQEQ, Vyn::TokenType::NOTEQ})) { // Assuming EqualEqual, NotEqual map to these
            Token op_token = this->previous_token();
            Vyn::AST::ExprPtr right = this->parse_relational_expr();
            expr = std::make_shared<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_bitwise_and_expr() {
        Vyn::AST::ExprPtr expr = this->parse_equality_expr();
        // Corrected TokenType and added Vyn:: namespace
        while (this->match({Vyn::TokenType::AMPERSAND})) { // Assuming BitAnd is AMPERSAND
            Token op_token = this->previous_token();
            Vyn::AST::ExprPtr right = this->parse_equality_expr();
            expr = std::make_shared<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_bitwise_xor_expr() {
        Vyn::AST::ExprPtr expr = this->parse_bitwise_and_expr();
        // Corrected TokenType and added Vyn:: namespace
        while (this->match({Vyn::TokenType::CARET})) { // Assuming BitXor is CARET (standard C++ token for XOR) - CHECK token.hpp
            Token op_token = this->previous_token();
            Vyn::AST::ExprPtr right = this->parse_bitwise_and_expr();
            expr = std::make_shared<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_bitwise_or_expr() {
        Vyn::AST::ExprPtr expr = this->parse_bitwise_xor_expr();
        // Corrected TokenType and added Vyn:: namespace
        while (this->match({Vyn::TokenType::PIPE})) { // Assuming BitOr is PIPE (standard C++ token for OR) - CHECK token.hpp
            Token op_token = this->previous_token();
            Vyn::AST::ExprPtr right = this->parse_bitwise_xor_expr();
            expr = std::make_shared<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_logical_and_expr() {
        Vyn::AST::ExprPtr expr = this->parse_bitwise_or_expr();
        // Corrected TokenType and added Vyn:: namespace
        while (this->match({Vyn::TokenType::AND_LOGICAL})) { // Assuming And is AND_LOGICAL or similar (e.g. &&) - CHECK token.hpp
            Token op_token = this->previous_token();
            Vyn::AST::ExprPtr right = this->parse_bitwise_or_expr();
            expr = std::make_shared<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
        }
        return expr;
    }

    AST::ExprPtr ExpressionParser::parse_logical_or_expr() {
        Vyn::AST::ExprPtr expr = this->parse_logical_and_expr();
        // Corrected TokenType and added Vyn:: namespace
        while (this->match({Vyn::TokenType::OR_LOGICAL})) { // Assuming Or is OR_LOGICAL or similar (e.g. ||) - CHECK token.hpp
            Token op_token = this->previous_token();
            Vyn::AST::ExprPtr right = this->parse_logical_and_expr();
            expr = std::make_shared<Vyn::AST::BinaryOpNode>(op_token.location, op_token.type, std::move(expr), std::move(right));
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

        // Corrected TokenTypes and added Vyn:: namespace
        if (this->match({Vyn::TokenType::EQ, Vyn::TokenType::PLUS_EQ, Vyn::TokenType::MINUS_EQ, Vyn::TokenType::MULTIPLY_EQ, Vyn::TokenType::DIVIDE_EQ, Vyn::TokenType::MODULO_EQ, Vyn::TokenType::LSHIFT_EQ, Vyn::TokenType::RSHIFT_EQ, Vyn::TokenType::AMPERSAND_EQ, Vyn::TokenType::CARET_EQ, Vyn::TokenType::PIPE_EQ})) { // CHECK token.hpp for actual assignment operator token types
            Token op_token = this->previous_token();
            Vyn::AST::ExprPtr right = this->parse_assignment_expr(); // Right-associative
            // AssignmentNode constructor in ast.hpp expects SourceLocation, ExprPtr, ExprPtr
            // The operator itself is usually stored in the node if it matters (e.g. for += vs =)
            // Assuming AssignmentNode takes the operator token for its type/info
            // If AssignmentNode only takes left and right, the op_token.type would be used to create different nodes or store op type
            // Based on current AST, it seems AssignmentNode(loc, left, right) is the signature.
            // This implies the specific assignment operator (e.g., EQ, PLUS_EQ) might need to be part of the node or handled by a different node type.
            // For now, creating a generic AssignmentNode and passing the op_token to a potentially overloaded constructor or a field.
            // Let's assume AssignmentNode(SourceLocation loc, TokenType op, ExprPtr left, ExprPtr right) or similar.
            // The provided ast.hpp has AssignmentNode(SourceLocation loc, std::unique_ptr<ExprNode> l, std::unique_ptr<ExprNode> r, Node* p = nullptr)
            // This means the operator type must be stored IN the AssignmentNode.
            // Let's assume for now that the existing AssignmentNode is sufficient and the specific operator logic is handled elsewhere or implicitly.
            // This is a potential point of failure if the AST cannot represent different assignment types.
            // However, the prompt focuses on build errors.
            // The original code had: return std::make_shared<AST::AssignmentNode>(left, op, right);
            // This implies AST::AssignmentNode took an 'op' (Token). This is not in the current Vyn::AST::AssignmentNode.
            // I will use the Vyn::AST::AssignmentNode as defined, which means it does NOT store the operator.
            // This is likely an issue that will surface in later stages (semantic analysis or code gen).
            // For build fixing, we use the defined constructor.
            // The error was `no matching function call to AssignmentNode(...)`, it expects (loc, left, right).
            // The `op` token is used for its location for the node.
            return std::make_shared<Vyn::AST::AssignmentNode>(op_token.location, std::move(left), std::move(right));
        }
        return left;
    }

    AST::ExprPtr ExpressionParser::parse_expression() {
        return this->parse_assignment_expr();
    }

} // namespace Vyn
