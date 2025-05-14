#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include <stdexcept>
#include <vector> // For std::vector
#include <memory> // For std::unique_ptr
#include <string> // For std::to_string

namespace vyn {

TypeParser::TypeParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, const std::string& file_path, ExpressionParser& expr_parser)
    : BaseParser(tokens, pos, file_path), expr_parser_(expr_parser) {}

// Main entry point for parsing a type
vyn::TypeAnnotationPtr TypeParser::parse() {
    this->skip_comments_and_newlines();
    vyn::SourceLocation start_loc = this->current_location();
    vyn::TypeAnnotationPtr type;

    if (this->peek().type == vyn::TokenType::AMPERSAND) {
        this->consume(); // Consume '&'
        // bool is_mutable = false; // Mutable info lost for now
        if (this->match(vyn::TokenType::KEYWORD_MUT)) {
            // is_mutable = true;
        }
        auto referenced_type = this->parse(); // Recursively parse the type being referenced
        if (!referenced_type) {
            throw std::runtime_error("Expected type after '&' or '&mut' at " + start_loc.filePath + ":" + std::to_string(start_loc.line) + ":" + std::to_string(start_loc.column));
        }
        // Simplification: return the referenced type, losing reference information
        type = std::move(referenced_type);
    } else if (this->peek().type == vyn::TokenType::KEYWORD_FN) {
        this->consume(); // Consume 'fn'
        this->expect(vyn::TokenType::LPAREN);
        // std::vector<vyn::TypeAnnotationPtr> param_types; // Info lost
        if (this->peek().type != vyn::TokenType::RPAREN) {
            do {
                this->parse(); // Parse to consume tokens, but result not stored in TypeAnnotation
            } while (this->match(vyn::TokenType::COMMA));
        }
        this->expect(vyn::TokenType::RPAREN);
        
        if (this->match(vyn::TokenType::ARROW)) {
            auto return_type_parsed = this->parse(); // Parse to consume, info lost
            if (!return_type_parsed) {
                 throw std::runtime_error("Expected return type after '->' in function type at " + this->current_location().filePath + ":" + std::to_string(this->current_location().line) + ":" + std::to_string(this->current_location().column));
            }
        }
        // Simplification: represent as TypeAnnotation named "Function"
        auto fn_identifier = std::make_unique<vyn::Identifier>(start_loc, "Function");
        type = std::make_unique<vyn::TypeAnnotation>(start_loc, std::move(fn_identifier));
    } else {
        type = this->parse_base_type();
    }

    if (!type) {
         throw std::runtime_error("Failed to parse type at " + start_loc.filePath + ":" + std::to_string(start_loc.line) + ":" + std::to_string(start_loc.column));
    }

    return this->parse_postfix_type(std::move(type));
}

vyn::TypeAnnotationPtr TypeParser::parse_base_type() {
    this->skip_comments_and_newlines();
    vyn::SourceLocation loc = this->current_location();

    if (this->peek().type == vyn::TokenType::IDENTIFIER) {
        vyn::SourceLocation path_loc = this->current_location();
        std::string full_path_name = this->consume().lexeme;

        while (this->peek().type == vyn::TokenType::COLONCOLON) {
            this->consume(); // ::
            if (this->peek().type != vyn::TokenType::IDENTIFIER) {
                throw std::runtime_error("Expected identifier after '::' in type path at " + this->current_location().filePath + ":" + std::to_string(this->current_location().line) + ":" + std::to_string(this->current_location().column));
            }
            full_path_name += "::";
            full_path_name += this->consume().lexeme;
        }
        auto type_name_identifier = std::make_unique<vyn::Identifier>(path_loc, full_path_name);
        return std::make_unique<vyn::TypeAnnotation>(loc, std::move(type_name_identifier));
    } else if (this->match(vyn::TokenType::LPAREN)) {
        std::vector<vyn::TypeAnnotationPtr> member_types_parsed; 
        bool is_empty_tuple = true;
        if (this->peek().type != vyn::TokenType::RPAREN) {
            is_empty_tuple = false;
            do {
                member_types_parsed.push_back(this->parse()); 
            } while (this->match(vyn::TokenType::COMMA));
        }
        this->expect(vyn::TokenType::RPAREN);
        
        std::string tuple_name = is_empty_tuple ? "Unit" : "Tuple";
        
        if (!is_empty_tuple && member_types_parsed.size() == 1 && !this->check(vyn::TokenType::COMMA) && this->tokens_[this->pos_-1].type == vyn::TokenType::RPAREN) {
             // Check if the previous token before RPAREN was not a COMMA for single element tuples like (T) vs (T,)
             // This logic is tricky; for now, we simplify. If it was (T), it should be T.
             // The current TypeAnnotation model doesn't directly support tuple types with multiple elements distinctly from generic types or array types.
             // We are returning a TypeAnnotation with a simple name "Tuple" or "Unit".
             // If member_types_parsed.size() == 1, and it was meant to be just the inner type (e.g. (MyType) -> MyType),
             // we should return that inner type.
             // This requires more robust parsing of parenthesized expressions/types.
             // For now, let's assume (T) is parsed as a TypeAnnotation named "T" if it's a simple identifier.
             // And (T, U) or (T,) becomes "Tuple". () becomes "Unit".
             // The current code makes all non-empty tuples "Tuple".
        }

        auto tuple_identifier = std::make_unique<vyn::Identifier>(loc, tuple_name);
        return std::make_unique<vyn::TypeAnnotation>(loc, std::move(tuple_identifier));
    } else if (this->match(vyn::TokenType::LBRACKET)) {
        vyn::SourceLocation array_loc = loc;
        auto element_type = this->parse(); 
        if (!element_type) {
            // This path should ideally throw if parse() can return nullptr on failure and that's an error.
            // If parse() throws on failure, this check might be redundant or for a different case.
            throw std::runtime_error("Expected element type in array type definition at " + this->current_location().filePath + ":" + std::to_string(this->current_location().line) + ":" + std::to_string(this->current_location().column));
        }
        
        if (this->match(vyn::TokenType::SEMICOLON)) {
            auto size_expr_val = expr_parser_.parse(); 
            if (!size_expr_val) { // Ensure parse() failure is handled if it returns nullptr
                 throw std::runtime_error("Expected size expression after ';' in array type at " + this->current_location().filePath + ":" + std::to_string(this->current_location().line) + ":" + std::to_string(this->current_location().column));
            }
        }
        this->expect(vyn::TokenType::RBRACKET);
        return std::make_unique<vyn::TypeAnnotation>(array_loc, std::move(element_type), true /*isArrayMarker*/);
    }
    
    // Corrected call to token_type_to_string
    throw std::runtime_error("Expected a type identifier, '(', or '[' to start a type, found " + this->peek().lexeme + " (" + vyn::token_type_to_string(this->peek().type) + ") at " + loc.filePath + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column));
    // The throw above ensures this function doesn't end without returning/throwing.
    // The warning might be a false positive or related to the token_type_to_string error.
}

vyn::TypeAnnotationPtr TypeParser::parse_postfix_type(vyn::TypeAnnotationPtr base_type) {
    vyn::TypeAnnotationPtr current_type = std::move(base_type);

    while (true) {
        this->skip_comments_and_newlines();
        vyn::SourceLocation postfix_loc = current_type ? current_type->loc : this->current_location();


        if (this->peek().type == vyn::TokenType::LT) { 
            if (!current_type) { // Should not happen if base_type was valid
                 throw std::runtime_error("Cannot apply generic arguments to a null base type at " + postfix_loc.filePath + ":" + std::to_string(postfix_loc.line) + ":" + std::to_string(postfix_loc.column));
            }
            this->consume(); // Consume '<'
            std::vector<vyn::TypeAnnotationPtr> type_args;
            if (this->peek().type != vyn::TokenType::GT) {
                do {
                    type_args.push_back(this->parse()); 
                } while (this->match(vyn::TokenType::COMMA));
            }
            this->expect(vyn::TokenType::GT);

            if (current_type->isSimpleType() && current_type->simpleTypeName) {
                current_type = std::make_unique<vyn::TypeAnnotation>(postfix_loc, std::move(current_type->simpleTypeName), std::move(type_args));
            } else {
                 throw std::runtime_error("Base of generic type is not a simple identifier at " + postfix_loc.filePath + ":" + std::to_string(postfix_loc.line) + ":" + std::to_string(postfix_loc.column));
            }
        } else if (this->peek().type == vyn::TokenType::LBRACKET) { 
             if (!current_type) { // Should not happen
                 throw std::runtime_error("Cannot apply array operator to a null base type at " + postfix_loc.filePath + ":" + std::to_string(postfix_loc.line) + ":" + std::to_string(postfix_loc.column));
            }
            this->consume(); // LBRACKET
            this->expect(vyn::TokenType::RBRACKET);
            current_type = std::make_unique<vyn::TypeAnnotation>(postfix_loc, std::move(current_type), true /*isArrayMarker*/);
        } else {
            break; 
        }
    }
    return current_type;
}

} // namespace vyn
