#include "vyn/parser.hpp"
#include <stdexcept>

TypeParser::TypeParser(const std::vector<Token>& tokens, size_t& pos)
    : BaseParser(tokens, pos) {}

std::unique_ptr<ASTNode> TypeParser::parse() {
    Token type_token = peek(); // Initial token for the type node, might be updated
    std::unique_ptr<ASTNode> node;

    if (match(TokenType::KEYWORD_REF)) {
        node = std::make_unique<ASTNode>(ASTNode::Kind::Type, type_token); // type_token is REF
        expect(TokenType::LT);
        node->children.push_back(parse()); // Parse inner type
        expect(TokenType::GT);
    } else if (match(TokenType::LBRACKET)) {
        node = std::make_unique<ASTNode>(ASTNode::Kind::Type, type_token); // type_token is LBRACKET
        node->children.push_back(parse()); // Parse element type
        expect(TokenType::SEMICOLON);
        ExpressionParser expr_parser(tokens_, pos_);
        node->children.push_back(expr_parser.parse()); // Parse size expression
        expect(TokenType::RBRACKET);
    } else if (match(TokenType::AMPERSAND)) {
        node = std::make_unique<ASTNode>(ASTNode::Kind::Type, type_token); // type_token is AMPERSAND
        Token ampersand_token = type_token; // Save the ampersand

        // Check for 'mut' after '&'
        if (peek().type == TokenType::IDENTIFIER && peek().value == "mut") {
            Token mut_token = consume(); // Consume 'mut'
            // Create a child node for 'mut' or incorporate it into the Type node's properties if needed
            // For now, let's assume 'mut' modifies the referenced type, parsed next.
            // The AST structure for &mut T might be a Type node (AMPERSAND) with a child Type node (T)
            // and some flag or child for mutability.
            // Or, it could be a specific ASTNode::Kind for mutable references.
            // Let's make 'mut' an identifier child for now.
            node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, mut_token));
        }
        node->children.push_back(parse()); // Parse the referenced type
    } else if (peek().type == TokenType::IDENTIFIER) {
        Token id_token = consume(); // Consume the identifier
        node = std::make_unique<ASTNode>(ASTNode::Kind::Type, id_token);

        // Handle type parameters (generics) or array type shorthand
        // Loop for potential postfix type constructs like <T> or []
        while (true) {
            Token current_peek = peek();
            if (current_peek.type == TokenType::LBRACKET) { // Array type: T[]
                Token lbracket_token = consume(); // Consume LBRACKET
                auto array_type_node = std::make_unique<ASTNode>(ASTNode::Kind::Type, lbracket_token);
                array_type_node->children.push_back(std::move(node)); // The base type T

                // Check for explicit size in type definition, e.g., [T; size_expr]
                // This is different from array literal. Here it's part of type syntax.
                // For a simple T[], there might be no expression inside.
                if (peek().type != TokenType::RBRACKET) {
                    ExpressionParser expr_parser(tokens_, pos_);
                    array_type_node->children.push_back(expr_parser.parse());
                }
                expect(TokenType::RBRACKET);
                node = std::move(array_type_node);
            } else if (current_peek.type == TokenType::LT) { // Generic parameters: T<U, V>
                Token lt_token = consume(); // Consume LT
                auto generic_type_node = std::make_unique<ASTNode>(ASTNode::Kind::Type, lt_token); // Use < token
                generic_type_node->children.push_back(std::move(node)); // The base generic type T

                while (peek().type != TokenType::GT && peek().type != TokenType::EOF_TOKEN) {
                    generic_type_node->children.push_back(parse()); // Parse type arguments
                    if (!match(TokenType::COMMA)) {
                        break; // No comma, so expect GT or error
                    }
                }
                expect(TokenType::GT);
                node = std::move(generic_type_node);
            } else {
                break; // No more postfix type constructs
            }
        }
    } else {
        throw std::runtime_error("Unexpected token in TypeParser: " + token_type_to_string(peek().type) +
                                 ", value '" + peek().value + "' at line = " + std::to_string(peek().line));
    }
    return node;
}
