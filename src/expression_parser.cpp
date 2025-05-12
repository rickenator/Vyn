#include "vyn/parser.hpp"
#include <stdexcept>
#include <iostream>

ExpressionParser::ExpressionParser(const std::vector<Token>& tokens, size_t& pos)
    : BaseParser(tokens, pos) {}

std::unique_ptr<ASTNode> ExpressionParser::parse() {
  return parse_logical_expr();
}

std::unique_ptr<ASTNode> ExpressionParser::parse_logical_expr() {
    auto node = parse_equality_expr();
    while (peek().type == TokenType::AND || peek().type == TokenType::OR) { // Added OR
        Token op = consume(); // Consume 'AND' or 'OR'
        auto right = parse_equality_expr();
        auto new_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, op);
        new_node->children.push_back(std::move(node));
        new_node->children.push_back(std::move(right));
        node = std::move(new_node);
    }
    return node;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_equality_expr() {
    auto node = parse_relational_expr();
    while (peek().type == TokenType::EQEQ || peek().type == TokenType::NOTEQ) { // Changed BANGEQ to NOTEQ
        Token op = consume(); // Consume 'EQEQ' or 'NOTEQ'
        auto right = parse_relational_expr();
        auto new_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, op);
        new_node->children.push_back(std::move(node));
        new_node->children.push_back(std::move(right));
        node = std::move(new_node);
    }
    return node;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_relational_expr() {
    auto node = parse_additive_expr();
    while (peek().type == TokenType::LT || peek().type == TokenType::GT ||
           peek().type == TokenType::LTEQ || peek().type == TokenType::GTEQ) { // Added LTEQ, GTEQ
        Token op = consume(); // Consume '<', '>', '<=', '>='
        auto right = parse_additive_expr();
        auto new_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, op);
        new_node->children.push_back(std::move(node));
        new_node->children.push_back(std::move(right));
        node = std::move(new_node);
    }
    return node;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_additive_expr() {
    auto node = parse_multiplicative_expr();
    while (peek().type == TokenType::PLUS || peek().type == TokenType::MINUS) {
        Token op = consume(); // Consume '+' or '-'
        auto right = parse_multiplicative_expr();
        auto new_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, op);
        new_node->children.push_back(std::move(node));
        new_node->children.push_back(std::move(right));
        node = std::move(new_node);
    }
    return node;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_multiplicative_expr() {
    auto node = parse_unary_expr();
    while (peek().type == TokenType::MULTIPLY || peek().type == TokenType::DIVIDE || peek().type == TokenType::MODULO) { // Added MODULO
        Token op = consume(); // Consume '*', '/', '%'
        auto right = parse_unary_expr();
        auto new_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, op);
        new_node->children.push_back(std::move(node));
        new_node->children.push_back(std::move(right));
        node = std::move(new_node);
    }
    return node;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_unary_expr() {
    if (peek().type == TokenType::BANG || peek().type == TokenType::MINUS) { // Added MINUS for negation
        Token op = consume(); // Consume '!' or '-'
        auto operand = parse_unary_expr(); // Unary ops can chain, e.g., !-x
        auto node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, op);
        node->children.push_back(std::move(operand));
        return node;
    } else if (peek().type == TokenType::KEYWORD_AWAIT) {
        Token await_tok = consume(); // Consume 'await'
        auto operand = parse_primary(); 
        auto node = std::make_unique<ASTNode>(ASTNode::Kind::AwaitExpr, await_tok); // Use AwaitExpr
        node->children.push_back(std::move(operand));
        return node;
    }
    return parse_primary();
}

std::unique_ptr<ASTNode> ExpressionParser::parse_primary() {
    std::unique_ptr<ASTNode> node;
    Token current_token = peek();

    if (current_token.type == TokenType::IDENTIFIER) {
        node = std::make_unique<ASTNode>(ASTNode::Kind::Identifier, consume());
    } else if (current_token.type == TokenType::INT_LITERAL) {
        node = std::make_unique<ASTNode>(ASTNode::Kind::IntLiteral, consume());
    } else if (current_token.type == TokenType::STRING_LITERAL) {
        node = std::make_unique<ASTNode>(ASTNode::Kind::StringLiteral, consume());
    } else if (current_token.type == TokenType::LPAREN) {
        consume(); // Consume '('
        node = parse(); // Parse expression inside parentheses
        expect(TokenType::RPAREN); // Consume ')'
        // The 'node' is the expression itself, no separate grouping node needed unless specified
    } else if (current_token.type == TokenType::LBRACKET) {
        // Could be ArrayExpr [Type; Size](), simple array literal [a,b,c], or start of list comprehension
        // For now, let's assume parse_array_expr handles the [Type;Size]() and simple literals
        // List comprehensions might need a different starting point or more lookahead.
        // The EBNF for ArrayExpr is specific. If it's a list literal, parse_array_expr should handle it.
        node = parse_array_expr();
    } else if (current_token.type == TokenType::DOTDOT) { // Standalone range like ..10 or ..
         // This case needs to be handled carefully. A .. might be preceded by an expression.
         // The current parse_range expects to parse the start expression itself.
         // This indicates a leading .. which is not typical for `expr .. expr`
         // For now, let's assume parse_range is called when `expr DOTDOT` is seen.
         // This path might be an error or a specific feature (e.g. open-ended range start).
         // For now, let's throw, as parse_range should be called after a primary usually.
        throw std::runtime_error("Unexpected token DOTDOT at start of primary expression at line " + std::to_string(current_token.line));
    }
    // ... other primary expressions like 'true', 'false', 'null', 'this'
    else {
        throw std::runtime_error("Unexpected token in primary expression at line " + std::to_string(current_token.line) + ": '" + current_token.value + "'");
    }

    // Postfix operators / Access expressions
    while (true) {
        if (peek().type == TokenType::LPAREN) { // Call expression
            node = parse_call_expr(std::move(node));
        } else if (peek().type == TokenType::DOT) { // Member access
            node = parse_member_expr(std::move(node));
        } else if (peek().type == TokenType::LBRACKET) { // Subscript access
            Token lbracket_tok = consume(); // LBRACKET
            auto index_expr = parse();
            expect(TokenType::RBRACKET);
            // Use lbracket_tok for the SubscriptExpr node's token for location info
            auto access_node = std::make_unique<ASTNode>(ASTNode::Kind::SubscriptExpr, lbracket_tok);
            access_node->children.push_back(std::move(node));
            access_node->children.push_back(std::move(index_expr));
            node = std::move(access_node);
        } else if (peek().type == TokenType::LBRACE && node->kind == ASTNode::Kind::Identifier) { // Struct literal initialization
            // This syntax `Identifier { ... }` is for struct initialization.
            // The 'Identifier' node is the type being initialized.
            Token lbrace_tok = consume(); // LBRACE
            auto struct_init_node = std::make_unique<ASTNode>(ASTNode::Kind::StructInitExpr, lbrace_tok);
            struct_init_node->children.push_back(std::move(node)); // The type identifier

            while (peek().type != TokenType::RBRACE && peek().type != TokenType::EOF_TOKEN) {
                if (peek().type != TokenType::IDENTIFIER) {
                    throw std::runtime_error("Expected field identifier in struct literal at line " + std::to_string(peek().line));
                }
                auto field_name_node = std::make_unique<ASTNode>(ASTNode::Kind::Identifier, consume());

                if (peek().type != TokenType::EQ && peek().type != TokenType::COLON) {
                    throw std::runtime_error("Expected EQ or COLON after field name in struct literal at line " + std::to_string(peek().line));
                }
                Token assign_op_token = consume(); // EQ or COLON

                auto field_value_expr = parse();
                
                // Create a node for the field assignment, e.g., using the assign_op_token
                auto field_assign_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, assign_op_token);
                field_assign_node->children.push_back(std::move(field_name_node));
                field_assign_node->children.push_back(std::move(field_value_expr));
                struct_init_node->children.push_back(std::move(field_assign_node));

                if (peek().type == TokenType::COMMA || peek().type == TokenType::SEMICOLON) {
                    consume();
                } else if (peek().type == TokenType::RBRACE) {
                    break; 
                } else {
                    throw std::runtime_error("Expected COMMA, SEMICOLON, or RBRACE in struct literal at line " + std::to_string(peek().line));
                }
            }
            expect(TokenType::RBRACE);
            node = std::move(struct_init_node);
        } else if (peek().type == TokenType::DOTDOT) { // Range expression like `primary_expr .. expr`
             Token op_token = consume(); // Consume '..'
             auto end_expr = parse_additive_expr(); // Parse the end of the range (or higher precedence?)
                                                 // EBNF for list comprehension suggests `expression .. expression`
                                                 // Let's use parse() for flexibility, which starts at logical_expr
             auto range_node = std::make_unique<ASTNode>(ASTNode::Kind::RangeExpr, op_token);
             range_node->children.push_back(std::move(node)); // The start_expr is the node we've built so far
             range_node->children.push_back(std::move(end_expr));
             node = std::move(range_node);
        }
        else {
            break; // No more postfix/access expressions
        }
    }
    return node;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_call_expr(std::unique_ptr<ASTNode> callee) {
    Token lparen_tok = consume(); // Consume '('
    // Use the callee's token for the CallExpr node, or lparen_tok if callee's token is not suitable
    auto call_node = std::make_unique<ASTNode>(ASTNode::Kind::CallExpr, callee->token.type != TokenType::INVALID ? callee->token : lparen_tok);
    call_node->children.push_back(std::move(callee));
    if (peek().type != TokenType::RPAREN) {
        while (true) {
            call_node->children.push_back(parse());
            if (peek().type == TokenType::COMMA) {
                consume(); // Consume ','
            } else if (peek().type == TokenType::RPAREN) {
                break;
            } else {
                throw std::runtime_error("Expected ',' or ')' in argument list at line " + std::to_string(peek().line));
            }
        }
    }
    expect(TokenType::RPAREN); // Consume ')'
    return call_node;
}

std::unique_ptr<ASTNode> ExpressionParser::parse_member_expr(std::unique_ptr<ASTNode> object) {
    Token dot_tok = consume(); // Consume '.'
    if (peek().type != TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected identifier after '.' at line " + std::to_string(peek().line));
    }
    auto member_name_node = std::make_unique<ASTNode>(ASTNode::Kind::Identifier, consume());

    auto member_expr_node = std::make_unique<ASTNode>(ASTNode::Kind::MemberExpr, dot_tok);
    member_expr_node->children.push_back(std::move(object));
    member_expr_node->children.push_back(std::move(member_name_node));
    return member_expr_node;
}


std::unique_ptr<ASTNode> ExpressionParser::parse_array_expr() {
    // Handles:
    // 1. [Type; Size]()  (from vyn.hpp EBNF)
    // 2. [elem1, elem2, ...] (common array literal)

    Token lbracket_tok = expect(TokenType::LBRACKET);

    // Check for empty array literal: []
    if (peek().type == TokenType::RBRACKET) {
        consume(); // RBRACKET
        auto array_node = std::make_unique<ASTNode>(ASTNode::Kind::ArrayExpr, lbracket_tok);
        // No children for empty array, or could add a special marker if needed
        return array_node;
    }

    // Try to parse as [Type; Size]()
    // This requires lookahead for SEMICOLON after a Type.
    // A simple heuristic: if next is IDENTIFIER and next+1 is SEMICOLON
    // This is tricky without full type parsing.
    // For now, let's assume if it's not a list of expressions, it might be [Type;Size]
    // A more robust way is to try parsing a Type, then check for SEMICOLON.

    // Attempt to distinguish [Type; Size] from [expr, expr]
    // A simple check: if the token after LBRACKET is an IDENTIFIER,
    // and the token after that is a SEMICOLON, assume [Type; Size] form.
    // This is a weak check. A proper TypeParser is needed.
    bool is_type_size_form = false;
    if (tokens_[pos_].type == TokenType::IDENTIFIER && (pos_ + 1 < tokens_.size()) && tokens_[pos_ + 1].type == TokenType::SEMICOLON) {
       is_type_size_form = true;
    }


    if (is_type_size_form) {
        // Parse as [Type; Size]()
        TypeParser type_parser(tokens_, pos_); // Create TypeParser
        auto type_node = type_parser.parse(); // This should parse a Type

        expect(TokenType::SEMICOLON);
        auto size_expr_node = parse(); 
        expect(TokenType::RBRACKET);

        auto array_expr_node = std::make_unique<ASTNode>(ASTNode::Kind::ArrayExpr, lbracket_tok);
        array_expr_node->children.push_back(std::move(type_node));
        array_expr_node->children.push_back(std::move(size_expr_node));

        if (match(TokenType::LPAREN)) { // Optional ()
            expect(TokenType::RPAREN);
            // Optionally mark the node if () was present, e.g. for "default initialization"
            // array_expr_node->token.value += "()"; // Or a boolean flag
        }
        return array_expr_node;
    } else {
        // Parse as simple array literal: [elem1, elem2, ...]
        auto array_node = std::make_unique<ASTNode>(ASTNode::Kind::ArrayExpr, lbracket_tok);
        if (peek().type != TokenType::RBRACKET) { // Check if not empty, already handled if it was just []
            while (true) {
                array_node->children.push_back(parse());
                if (match(TokenType::COMMA)) {
                    if (peek().type == TokenType::RBRACKET) { // Trailing comma case: [a, b,]
                        break;
                    }
                    // continue to next element
                } else if (peek().type == TokenType::RBRACKET) {
                    break;
                } else {
                    throw std::runtime_error("Expected ',' or ']' in array literal at line " + std::to_string(peek().line));
                }
            }
        }
        expect(TokenType::RBRACKET);
        return array_node;
    }
}

// parse_range is now handled as a postfix operator in parse_primary if `expr .. expr` is encountered.
// This function might be redundant or could be repurposed for list comprehension specific ranges.
// For now, removing its separate definition as its logic is integrated into parse_primary.
/*
std::unique_ptr<ASTNode> ExpressionParser::parse_range() {
    // This was intended for `expr .. expr`
    // This logic is now in parse_primary when `DOTDOT` is encountered after a primary expression.
    // If a standalone range like `..10` or `10..` or `..` needs to be parsed as a primary,
    // then parse_primary needs to handle DOTDOT differently at its start.
    // For now, this function is not directly called by the revised parse_primary.
    throw std::logic_error("parse_range should not be called directly anymore. Range parsing is in parse_primary.");
}
*/
// Ensure parse_range declaration is removed from parser.hpp if this function is removed.
// For now, keeping it commented out. If it's needed for list comprehensions specifically, it can be revived.
