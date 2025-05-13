#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include <stdexcept>
#include <vector> // For std::vector
#include <memory> // For std::unique_ptr

TypeParser::TypeParser(const std::vector<Vyn::Token>& tokens, size_t& pos, const std::string& file_path, ExpressionParser& expr_parser)
    : BaseParser(tokens, pos, file_path), expr_parser_(expr_parser) {}

// Main entry point for parsing a type
std::unique_ptr<Vyn::AST::TypeNode> TypeParser::parse() {
    skip_comments_and_newlines();
    Vyn::AST::SourceLocation start_loc = current_location();
    std::unique_ptr<Vyn::AST::TypeNode> type;

    if (peek().type == Vyn::TokenType::AMPERSAND) {
        consume(); // Consume '&'
        bool is_mutable = match(Vyn::TokenType::KEYWORD_MUT); // Assuming KEYWORD_MUT for "mut"
        auto referenced_type = parse(); // Recursively parse the type being referenced
        if (!referenced_type) {
            throw std::runtime_error("Expected type after '&' or '&mut' at " + start_loc.toString());
        }
        type = std::make_unique<Vyn::AST::ReferenceTypeNode>(start_loc, std::move(referenced_type), is_mutable);
    } else if (peek().type == Vyn::TokenType::KEYWORD_FN) { // Assuming KEYWORD_FN for "fn"
        consume(); // Consume 'fn'
        expect(Vyn::TokenType::LPAREN);
        std::vector<std::unique_ptr<Vyn::AST::TypeNode>> param_types;
        if (peek().type != Vyn::TokenType::RPAREN) {
            do {
                param_types.push_back(parse());
            } while (match(Vyn::TokenType::COMMA));
        }
        expect(Vyn::TokenType::RPAREN);
        
        std::unique_ptr<Vyn::AST::TypeNode> return_type;
        if (match(Vyn::TokenType::ARROW)) {
            return_type = parse();
            if (!return_type) {
                 throw std::runtime_error("Expected return type after '->' in function type at " + current_location().toString());
            }
        } else {
            // Default return type: unit type ()
            Vyn::AST::SourceLocation unit_loc = current_location(); 
            return_type = std::make_unique<Vyn::AST::TupleTypeNode>(unit_loc, std::vector<std::unique_ptr<Vyn::AST::TypeNode>>{});
        }
        type = std::make_unique<Vyn::AST::FunctionTypeNode>(start_loc, std::move(param_types), std::move(return_type));
    } else {
        // Not a prefix type, so parse base type (identifier, tuple, array [T;N])
        type = parse_base_type();
    }

    if (!type) {
         // parse_base_type might throw, or return nullptr if we change it.
         // For now, assume it throws or returns a valid type. If it could return nullptr:
         throw std::runtime_error("Failed to parse type at " + start_loc.toString());
    }

    // After parsing a base or prefixed type, check for postfix operators
    return parse_postfix_type(std::move(type));
}

// Parses IDENTIFIER, (TUPLE), [ARRAY; N]
std::unique_ptr<Vyn::AST::TypeNode> TypeParser::parse_base_type() {
    skip_comments_and_newlines();
    Vyn::AST::SourceLocation loc = current_location();

    if (peek().type == Vyn::TokenType::IDENTIFIER) {
        // Parse a potentially qualified identifier path for the type name
        std::vector<std::unique_ptr<Vyn::AST::IdentifierNode>> segments;
        Vyn::AST::SourceLocation path_loc = current_location();
        segments.push_back(std::make_unique<Vyn::AST::IdentifierNode>(current_location(), consume().value));

        while (peek().type == Vyn::TokenType::DOUBLE_COLON) {
            consume(); // ::
            if (peek().type != Vyn::TokenType::IDENTIFIER) {
                throw std::runtime_error("Expected identifier after '::' in type path at " + current_location().toString());
            }
            segments.push_back(std::make_unique<Vyn::AST::IdentifierNode>(current_location(), consume().value));
        }
        auto path_node = std::make_unique<Vyn::AST::PathNode>(path_loc, std::move(segments));
        return std::make_unique<Vyn::AST::IdentifierTypeNode>(loc, std::move(path_node));
    } else if (match(Vyn::TokenType::LPAREN)) { // Tuple type or Unit type
        std::vector<std::unique_ptr<Vyn::AST::TypeNode>> member_types;
        if (peek().type != Vyn::TokenType::RPAREN) {
            do {
                member_types.push_back(parse()); // Recursive call to main parse for each member type
            } while (match(Vyn::TokenType::COMMA));
        }
        expect(Vyn::TokenType::RPAREN);
        return std::make_unique<Vyn::AST::TupleTypeNode>(loc, std::move(member_types));
    } else if (match(Vyn::TokenType::LBRACKET)) { // Array type like [T; size] or slice type [T]
        Vyn::AST::SourceLocation array_loc = loc;
        auto element_type = parse(); 
        if (!element_type) {
            throw std::runtime_error("Expected element type in array type definition at " + current_location().toString());
        }
        std::unique_ptr<Vyn::AST::ExprNode> size_expr = nullptr;
        if (match(Vyn::TokenType::SEMICOLON)) { // [T; size]
            size_expr = expr_parser_.parse(); 
            if (!size_expr) {
                throw std::runtime_error("Expected size expression after ';' in array type at " + current_location().toString());
            }
        }
        expect(Vyn::TokenType::RBRACKET);
        return std::make_unique<Vyn::AST::ArrayTypeNode>(array_loc, std::move(element_type), std::move(size_expr));
    }
    
    throw std::runtime_error("Expected a type identifier, '(', or '[' to start a type, found " + peek().value + " (" + Vyn::token_type_to_string(peek().type) + ") at " + loc.toString());
}

// Parses postfix operators like <GENERIC_ARGS> or [] (for T[])
std::unique_ptr<Vyn::AST::TypeNode> TypeParser::parse_postfix_type(std::unique_ptr<Vyn::AST::TypeNode> base_type) {
    // This function takes ownership of base_type
    std::unique_ptr<Vyn::AST::TypeNode> current_type = std::move(base_type);

    while (true) {
        skip_comments_and_newlines();
        // Vyn::AST::SourceLocation postfix_loc = current_location(); // Location of the postfix operator itself.
                                                                // Not strictly needed if node loc is start loc.

        if (peek().type == Vyn::TokenType::LT) { 
            consume(); // Consume '<'
            std::vector<std::unique_ptr<Vyn::AST::TypeNode>> type_args;
            if (peek().type != Vyn::TokenType::GT) {
                do {
                    type_args.push_back(parse()); // Recursive call to main parse for type arguments
                } while (match(Vyn::TokenType::COMMA));
            }
            expect(Vyn::TokenType::GT);
            // The location for GenericInstanceTypeNode should ideally span the whole T<Args>.
            // Using current_type->location (start of T) is a common simplification.
            current_type = std::make_unique<Vyn::AST::GenericInstanceTypeNode>(current_type->location, std::move(current_type), std::move(type_args));
        } else if (peek().type == Vyn::TokenType::LBRACKET) { 
            // Check if this LBRACKET is part of an expression or truly a type postfix `[]`
            // This can be ambiguous if expressions like `my_array[index]` are allowed where `my_array` is a type name (not common).
            // Assuming here that if a type is parsed and `[` follows, it's `T[]`.
            // This means `[T;N]` must be parsed via `parse_base_type` when `[` is the *first* token of the type.
            consume(); // LBRACKET
            expect(Vyn::TokenType::RBRACKET);
            current_type = std::make_unique<Vyn::AST::ArrayTypeNode>(current_type->location, std::move(current_type), nullptr); // nullptr size_expr for T[]
        } else {
            break; // No more postfix operators
        }
    }
    return current_type;
}
