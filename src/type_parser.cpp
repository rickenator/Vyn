#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include <stdexcept>
#include <vector> // For std::vector
#include <memory> // For std::unique_ptr

namespace Vyn {

TypeParser::TypeParser(const std::vector<Vyn::Token>& tokens, size_t& pos, const std::string& file_path, ExpressionParser& expr_parser)
    : BaseParser(tokens, pos, file_path), expr_parser_(expr_parser) {}

// Main entry point for parsing a type
AST::TypePtr TypeParser::parse() {
    this->skip_comments_and_newlines();
    AST::SourceLocation start_loc = this->current_location();
    AST::TypePtr type;

    if (this->peek().type == Vyn::TokenType::AMPERSAND) {
        this->consume(); // Consume '&'
        bool is_mutable = false;
        if (this->match(Vyn::TokenType::KEYWORD_MUT)) { // Check if KEYWORD_MUT exists
            is_mutable = true;
        }
        auto referenced_type = this->parse(); // Recursively parse the type being referenced
        if (!referenced_type) {
            throw std::runtime_error("Expected type after '&' or '&mut' at " + start_loc.filePath + ":" + std::to_string(start_loc.line) + ":" + std::to_string(start_loc.column));
        }
        type = std::make_unique<AST::ReferenceTypeNode>(start_loc, std::move(referenced_type), is_mutable);
    } else if (this->peek().type == Vyn::TokenType::KEYWORD_FN) { // Assuming KEYWORD_FN for "fn"
        this->consume(); // Consume 'fn'
        this->expect(Vyn::TokenType::LPAREN);
        std::vector<AST::TypePtr> param_types;
        if (this->peek().type != Vyn::TokenType::RPAREN) {
            do {
                param_types.push_back(this->parse());
            } while (this->match(Vyn::TokenType::COMMA));
        }
        this->expect(Vyn::TokenType::RPAREN);
        
        AST::TypePtr return_type;
        if (this->match(Vyn::TokenType::ARROW)) {
            return_type = this->parse();
            if (!return_type) {
                 throw std::runtime_error("Expected return type after '->' in function type at " + this->current_location().filePath + ":" + std::to_string(this->current_location().line) + ":" + std::to_string(this->current_location().column));
            }
        } else {
            // Default return type: unit type ()
            AST::SourceLocation unit_loc = this->current_location(); 
            return_type = std::make_unique<AST::TupleTypeNode>(unit_loc, std::vector<AST::TypePtr>{});
        }
        type = std::make_unique<AST::FunctionTypeNode>(start_loc, std::move(param_types), std::move(return_type));
    } else {
        // Not a prefix type, so parse base type (identifier, tuple, array [T;N])
        type = this->parse_base_type();
    }

    if (!type) {
         throw std::runtime_error("Failed to parse type at " + start_loc.filePath + ":" + std::to_string(start_loc.line) + ":" + std::to_string(start_loc.column));
    }

    // After parsing a base or prefixed type, check for postfix operators
    return this->parse_postfix_type(std::move(type));
}

// Parses IDENTIFIER, (TUPLE), [ARRAY; N]
AST::TypePtr TypeParser::parse_base_type() {
    this->skip_comments_and_newlines();
    AST::SourceLocation loc = this->current_location();

    if (this->peek().type == Vyn::TokenType::IDENTIFIER) {
        // Parse a potentially qualified identifier path for the type name
        std::vector<std::unique_ptr<AST::IdentifierNode>> segments;
        AST::SourceLocation path_loc = this->current_location();
        segments.push_back(std::make_unique<AST::IdentifierNode>(this->current_location(), this->consume().lexeme));

        while (this->peek().type == Vyn::TokenType::COLONCOLON) { // Check for COLONCOLON
            this->consume(); // ::
            if (this->peek().type != Vyn::TokenType::IDENTIFIER) {
                throw std::runtime_error("Expected identifier after '::' in type path at " + this->current_location().filePath + ":" + std::to_string(this->current_location().line) + ":" + std::to_string(this->current_location().column));
            }
            segments.push_back(std::make_unique<AST::IdentifierNode>(this->current_location(), this->consume().lexeme));
        }
        auto path_node = std::make_unique<AST::PathNode>(path_loc, std::move(segments));
        return std::make_unique<AST::IdentifierTypeNode>(loc, std::move(path_node));
    } else if (this->match(Vyn::TokenType::LPAREN)) { // Tuple type or Unit type
        std::vector<AST::TypePtr> member_types;
        if (this->peek().type != Vyn::TokenType::RPAREN) {
            do {
                member_types.push_back(this->parse()); // Recursive call to main parse for each member type
            } while (this->match(Vyn::TokenType::COMMA));
        }
        this->expect(Vyn::TokenType::RPAREN);
        return std::make_unique<AST::TupleTypeNode>(loc, std::move(member_types));
    } else if (this->match(Vyn::TokenType::LBRACKET)) { // Array type like [T; size] or slice type [T]
        AST::SourceLocation array_loc = loc;
        auto element_type = this->parse(); 
        if (!element_type) {
            throw std::runtime_error("Expected element type in array type definition at " + this->current_location().filePath + ":" + std::to_string(this->current_location().line) + ":" + std::to_string(this->current_location().column));
        }
        std::unique_ptr<AST::ExprNode> size_expr = nullptr;
        if (this->match(Vyn::TokenType::SEMICOLON)) { // [T; size]
            size_expr = expr_parser_.parse(); 
            if (!size_expr) {
                throw std::runtime_error("Expected size expression after ';' in array type at " + this->current_location().filePath + ":" + std::to_string(this->current_location().line) + ":" + std::to_string(this->current_location().column));
            }
        }
        this->expect(Vyn::TokenType::RBRACKET);
        return std::make_unique<AST::ArrayTypeNode>(array_loc, std::move(element_type), std::move(size_expr));
    }
    
    throw std::runtime_error("Expected a type identifier, '(', or '[' to start a type, found " + this->peek().lexeme + " (" + Vyn::token_type_to_string(this->peek().type) + ") at " + loc.filePath + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column));
}

// Parses postfix operators like <GENERIC_ARGS> or [] (for T[])
AST::TypePtr TypeParser::parse_postfix_type(AST::TypePtr base_type) {
    AST::TypePtr current_type = std::move(base_type);

    while (true) {
        this->skip_comments_and_newlines();

        if (this->peek().type == Vyn::TokenType::LT) { 
            this->consume(); // Consume '<'
            std::vector<AST::TypePtr> type_args;
            if (this->peek().type != Vyn::TokenType::GT) {
                do {
                    type_args.push_back(this->parse()); // Recursive call to main parse for type arguments
                } while (this->match(Vyn::TokenType::COMMA));
            }
            this->expect(Vyn::TokenType::GT);
            current_type = std::make_unique<AST::GenericInstanceTypeNode>(current_type->location, std::move(current_type), std::move(type_args));
        } else if (this->peek().type == Vyn::TokenType::LBRACKET) { 
            this->consume(); // LBRACKET
            this->expect(Vyn::TokenType::RBRACKET);
            current_type = std::make_unique<AST::ArrayTypeNode>(current_type->location, std::move(current_type), nullptr); // nullptr size_expr for T[]
        } else {
            break; // No more postfix operators
        }
    }
    return current_type;
}

} // namespace Vyn
