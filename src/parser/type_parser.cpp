#include "vyn/parser/ast.hpp"

namespace vyn {

TypeParser::TypeParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, const std::string& file_path, ExpressionParser& expr_parser)
    : BaseParser(tokens, pos, file_path), expr_parser_(expr_parser) {}

// Forward declaration for a helper that might be needed if we parse complex generic arguments
// static vyn::TypeNodePtr parse_type_argument(TypeParser* parser); // Example

// Main entry point for parsing a type
vyn::TypeNodePtr TypeParser::parse() {
    this->skip_comments_and_newlines();
    vyn::SourceLocation start_loc = this->current_location();
    vyn::TypeNodePtr type;

    // Try parsing an ownership-wrapped type first, or a base type.
    type = this->parse_base_or_ownership_wrapped_type();

    if (!type) {
         // Use this->error for consistency, assuming it throws or can be made to throw
         throw this->error(this->peek(), "Failed to parse type at " + start_loc.toString());
    }

    // Then, parse any postfix operators (generics, array, optional, const)
    return this->parse_postfix_type(std::move(type));
}

vyn::TypeNodePtr TypeParser::parse_base_or_ownership_wrapped_type() {
    this->skip_comments_and_newlines();
    vyn::SourceLocation loc = this->current_location();

    OwnershipKind ownership_kind;
    bool is_ownership_keyword = false;

    if (this->check(vyn::TokenType::KEYWORD_MY)) {
        ownership_kind = OwnershipKind::MY;
        is_ownership_keyword = true;
    } else if (this->check(vyn::TokenType::KEYWORD_OUR)) {
        ownership_kind = OwnershipKind::OUR;
        is_ownership_keyword = true;
    } else if (this->check(vyn::TokenType::KEYWORD_THEIR)) {
        ownership_kind = OwnershipKind::THEIR;
        is_ownership_keyword = true;
    } else if (this->check(vyn::TokenType::KEYWORD_PTR)) {
        ownership_kind = OwnershipKind::PTR;
        is_ownership_keyword = true;
    }

    if (is_ownership_keyword) {
        this->consume(); // Consume the ownership keyword
        this->expect(vyn::TokenType::LT);
        // The inner type of an ownership wrapper cannot itself be another ownership-wrapped type directly.
        // It must be a base type which can then have postfix operators.
        // So, we call parse_atomic_or_group_type, then parse_postfix_type for its content.
        vyn::TypeNodePtr wrapped_base_type = this->parse_atomic_or_group_type();
        if (!wrapped_base_type) {
            throw this->error(this->peek(), "Expected a type inside ownership wrapper < > at " + this->current_location().toString());
        }
        vyn::TypeNodePtr full_wrapped_type = this->parse_postfix_type(std::move(wrapped_base_type));
        this->expect(vyn::TokenType::GT);
        // Ownership wrappers are neither const nor optional themselves; those apply to the wrapped type.
        return TypeNode::newOwnershipWrapped(loc, ownership_kind, std::move(full_wrapped_type), false, false);
    }

    // If not an ownership keyword, parse other base types
    return this->parse_atomic_or_group_type();
}

vyn::TypeNodePtr TypeParser::parse_atomic_or_group_type() {
    this->skip_comments_and_newlines();
    vyn::SourceLocation loc = this->current_location();
    vyn::ExprPtr size_expression = nullptr; 

    if (this->match(vyn::TokenType::IDENTIFIER)) {
        // Parse qualified name: IDENTIFIER (COLONCOLON IDENTIFIER)*
        vyn::SourceLocation path_loc = this->previous_token().location;
        std::string qualified_name = this->previous_token().lexeme;
        while (this->match(vyn::TokenType::COLONCOLON)) {
            if (!this->match(vyn::TokenType::IDENTIFIER)) {
                throw this->error(this->peek(), "Expected identifier after '::' in qualified type name");
            }
            qualified_name += "::" + this->previous_token().lexeme;
        }
        auto type_name_identifier = std::make_unique<vyn::Identifier>(path_loc, qualified_name);
        return TypeNode::newIdentifier(loc, std::move(type_name_identifier), {}, false, false);

    } else if (this->match(vyn::TokenType::LPAREN)) { 
        std::vector<vyn::TypeNodePtr> member_types_parsed;
        if (this->peek().type != vyn::TokenType::RPAREN) {
            do {
                member_types_parsed.push_back(this->parse());
            } while (this->match(vyn::TokenType::COMMA));
        }
        this->expect(vyn::TokenType::RPAREN);

        return TypeNode::newTuple(loc, std::move(member_types_parsed), false, false);

    } else if (this->match(vyn::TokenType::LBRACKET)) { 
        vyn::SourceLocation array_loc = this->previous_token().location; 
        TypeNodePtr element_type = this->parse();
        if (!element_type) {
            throw this->error(this->peek(), "Expected element type for array.");
        }

        if (this->match(vyn::TokenType::SEMICOLON)) {
            if (this->IsAtEnd() || this->peek().type == vyn::TokenType::RBRACKET) { // Changed is_at_end to IsAtEnd, current_token to peek
                throw this->error(this->peek(), "Expected size expression after ';' in array type.");
            }
            size_expression = expr_parser_.parse(); 
            if (!size_expression) {
                 throw this->error(this->peek(), "Failed to parse array size expression.");
            }
        }

        this->expect(vyn::TokenType::RBRACKET); 
        // expect() already throws an error if the token type doesn't match.
        return TypeNode::newArray(array_loc, std::move(element_type), std::move(size_expression), false, false);

    } else if (this->match(vyn::TokenType::KEYWORD_FN)) { 
        vyn::SourceLocation fn_loc = loc; 
        this->expect(vyn::TokenType::LPAREN);
        std::vector<vyn::TypeNodePtr> param_types_parsed;
        if (this->peek().type != vyn::TokenType::RPAREN) {
            do {
                param_types_parsed.push_back(this->parse());
            } while (this->match(vyn::TokenType::COMMA));
        }
        this->expect(vyn::TokenType::RPAREN);

        vyn::TypeNodePtr return_type_parsed = nullptr;
        if (this->match(vyn::TokenType::ARROW)) {
            return_type_parsed = this->parse();
            if (!return_type_parsed) {
                 throw this->error(this->peek(), "Expected return type after '->' in function type at " + this->current_location().toString());
            }
        }
        return TypeNode::newFunctionSignature(fn_loc, std::move(param_types_parsed), std::move(return_type_parsed), false, false);
    }

    throw this->error(this->peek(), "Expected a type identifier, '(', '[', or 'fn' to start a base type, found " + this->peek().lexeme + " (" + vyn::token_type_to_string(this->peek().type) + ") at " + loc.toString()); // corrected token_type_to_string scope
}

vyn::TypeNodePtr TypeParser::parse_postfix_type(vyn::TypeNodePtr current_type) {
    while (true) {
        this->skip_comments_and_newlines();
        if (this->match(vyn::TokenType::LT)) {
            // Generic type parameters
            std::vector<vyn::TypeNodePtr> generic_args;
            // Parse comma-separated list of type arguments
            if (this->peek().type != vyn::TokenType::GT) {
                do {
                    auto type_arg = this->parse();
                    if (!type_arg) {
                        throw this->error(this->peek(), "Expected type argument in generic type at " + 
                                        this->current_location().toString());
                    }
                    generic_args.push_back(std::move(type_arg));
                } while (this->match(vyn::TokenType::COMMA));
            }
            this->expect(vyn::TokenType::GT);
            // Update the current_type with generic arguments
            if (current_type->category == vyn::TypeNode::TypeCategory::IDENTIFIER) {
                current_type->genericArguments = std::move(generic_args);
            } else {
                throw this->error(this->previous_token(), "Generic parameters can only be applied to identifier types");
            }
        } else if (this->match(vyn::TokenType::LBRACKET)) {
            SourceLocation lbracket_loc = this->previous_token().location; 
            if (this->match(vyn::TokenType::RBRACKET)) { 
                current_type = TypeNode::newArray(lbracket_loc, std::move(current_type), nullptr, false, false); 
            } else {
                this->put_back_token(); 
                break; 
            }
        } else if (this->match(vyn::TokenType::MULTIPLY)) { // Explicitly scope TokenType, changed STAR to MULTIPLY
            if (current_type->isPointer) { 
                throw this->error(this->previous_token(), "Type is already a pointer.");
            }
            current_type->isPointer = true;
        } else if (this->match(vyn::TokenType::QUESTION_MARK)) { 
            if (current_type->isOptional) {
                throw this->error(this->previous_token(), "Type is already optional: " + current_type->loc.toString());
            }
            current_type->isOptional = true;
        } else if (this->match(vyn::TokenType::KEYWORD_CONST)) { 
            if (current_type->dataIsConst) {
                 throw this->error(this->previous_token(), "Type is already const: " + current_type->loc.toString());
            }
            current_type->dataIsConst = true;
        }
        else {
            break; 
        }
    }
    return current_type;
}

} // namespace vyn
