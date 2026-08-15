// SPDX-License-Identifier: Apache-2.0

#include "vyb/parser/ast.hpp"
#include "vyb/parser/token.hpp"
#include "vyb/parser/parser.hpp" // Added missing include for parser definitions

namespace vyb {

// Helper function to convert SourceLocation to string for error messages
std::string locationToString(const SourceLocation& loc) {
    return loc.filePath + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column);
}

// Constructor
TypeParser::TypeParser(const std::vector<token::Token>& tokens, size_t& pos, const std::string& file_path, ExpressionParser& expr_parser)
    : BaseParser(tokens, pos, file_path), expr_parser_(expr_parser) {}


vyb::ast::TypeNodePtr TypeParser::parse() { // Corrected namespace
    // This is the main entry point for parsing a type.
    // It should handle basic types, pointer types, array types, function types, etc.
    this->skip_comments_and_newlines();
    vyb::SourceLocation start_loc = this->current_location();
    vyb::ast::TypeNodePtr type;

    // Try parsing an ownership-wrapped_type first, or a base type.
    type = this->parse_base_or_ownership_wrapped_type();

    if (!type) {
         // Use this->error for consistency, assuming it throws or can be made to throw
         throw this->error(this->peek(), "Failed to parse type at " + start_loc.toString());
    }

    // Then, parse any postfix operators (generics, array, optional, const)
    return this->parse_postfix_type(std::move(type));
}


// This function parses the core type, which might be wrapped by ownership modifiers.
vyb::ast::TypeNodePtr TypeParser::parse_base_or_ownership_wrapped_type() { // Corrected namespace
    // SourceLocation start_loc = peek().location; // Potentially unused now
    // bool is_mutable = false; // This logic is removed from here.
                              // Semantic analysis should handle properties of types like "my", "our", "their".

    // Ownership keywords (my, our, their) are now expected to be parsed as
    // part of the identifier in parse_atomic_or_group_type (if they are keywords like KEYWORD_MY)
    // or as regular identifiers if they are not keywords. The generic <...> part will be handled by parse_postfix_type.
    // Thus, we don't consume them here.
    //
    //     consume();
    // } else if (check(vyb::TokenType::KEYWORD_THEIR)) {
    //     consume();
    // }

    // Parse the base type directly
    vyb::ast::TypeNodePtr type_node = parse_atomic_or_group_type();

    // The 'is_mutable' logic (and any other special handling for my/our/their)
    // previously here would now be handled by semantic analysis
    // based on the resulting type_node's name (e.g., if it's "my").
    //

    return type_node; // Return the potentially wrapped type
}

// Parses atomic types (like identifiers, primitive types) or grouped types (like (i32, String))
vyb::ast::TypeNodePtr TypeParser::parse_atomic_or_group_type() { // Corrected namespace
    SourceLocation start_loc = peek().location;
    vyb::ast::TypeNodePtr type_node = nullptr;

    // Allow special keywords as type identifiers, e.g., for my<T>, their<T>, const<T>
    if (match(vyb::TokenType::IDENTIFIER) ||
        match(vyb::TokenType::KEYWORD_MY) ||
        match(vyb::TokenType::KEYWORD_OUR) ||
        match(vyb::TokenType::KEYWORD_THEIR) ||
        match(vyb::TokenType::KEYWORD_MILD) ||
        match(vyb::TokenType::KEYWORD_CONST)) { // Added KEYWORD_CONST for const<T>

        vyb::SourceLocation path_loc = this->previous_token().location;
        std::string qualified_name = this->previous_token().lexeme;

        // Special handling for "my."
        if (qualified_name == "my" && match(vyb::TokenType::DOT)) {
            if (!match(vyb::TokenType::IDENTIFIER)) {
                throw this->error(this->peek(), "Expected identifier after \\'my.\\' in type name");
            }
            qualified_name += "." + this->previous_token().lexeme;
        }

        // Loop for '::' and '.' qualifiers
        while (true) {
            if (this->match(vyb::TokenType::COLONCOLON)) {
                if (!this->match(vyb::TokenType::IDENTIFIER)) {
                    throw this->error(this->peek(), "Expected identifier after \\'::\\' in qualified type name");
                }
                qualified_name += "::" + this->previous_token().lexeme;
            } else if (this->match(vyb::TokenType::DOT)) {
                if (!this->match(vyb::TokenType::IDENTIFIER)) {
                    throw this->error(this->peek(), "Expected identifier after \\'.\\' in qualified type name");
                }
                qualified_name += "." + this->previous_token().lexeme;
            } else {
                break; // No more qualifiers
            }
        }
        auto type_name_identifier = std::make_unique<vyb::ast::Identifier>(path_loc, qualified_name);
        // return vyb::ast::TypeNode::newIdentifier(start_loc, std::move(type_name_identifier), {}, false, false);
        return std::make_unique<vyb::ast::TypeName>(start_loc, std::move(type_name_identifier));

    } else if (this->match(vyb::TokenType::LPAREN)) {
        std::vector<vyb::ast::TypeNodePtr> member_types_parsed;
        if (this->peek().type != vyb::TokenType::RPAREN) {
            do {
                member_types_parsed.push_back(this->parse());
            } while (this->match(vyb::TokenType::COMMA));
        }
        this->expect(vyb::TokenType::RPAREN);

        // return vyb::ast::TypeNode::newTuple(start_loc, std::move(member_types_parsed), false, false);
        return std::make_unique<vyb::ast::TupleTypeNode>(start_loc, std::move(member_types_parsed));

    } else if (this->match(vyb::TokenType::LBRACKET)) {
        vyb::SourceLocation array_loc = this->previous_token().location;

        // Store current position before parsing element type
        size_t before_element_type_pos = pos_;

        // Try to parse element type
        vyb::ast::TypeNodePtr element_type = nullptr;
        try {
            element_type = this->parse();
        } catch (const std::runtime_error& e) {
            // If we fail to parse a type, backtrack and let the expression parser handle it
            pos_ = before_element_type_pos;
            throw this->error(this->peek(), "Expected element type for array.");
        }

        if (!element_type) {
            // If parse() returned nullptr without throwing, backtrack
            pos_ = before_element_type_pos;
            throw this->error(this->peek(), "Expected element type for array.");
        }

        vyb::ast::ExprPtr size_expression = nullptr; // Declared size_expression

        if (this->match(vyb::TokenType::SEMICOLON)) {
            if (this->IsAtEnd() || this->peek().type == vyb::TokenType::RBRACKET) {
                throw this->error(this->peek(), "Expected size expression after \';\' in array type.");
            }

            // Store position before parsing size expression
            size_t before_size_expr_pos = pos_;

            try {
                size_expression = expr_parser_.parse_expression();
            } catch (const std::runtime_error& e) {
                // If parsing the size expression fails, backtrack
                pos_ = before_size_expr_pos;
                throw;
            }

            if (!size_expression) {
                pos_ = before_size_expr_pos;
                throw this->error(this->peek(), "Failed to parse array size expression.");
            }
        }

        // Save position before expecting RBRACKET
        size_t before_rbracket_pos = pos_;

        try {
            this->expect(vyb::TokenType::RBRACKET);
        } catch (const std::runtime_error& e) {
            // If RBRACKET isn't found where expected, backtrack
            pos_ = before_element_type_pos;
            throw;
        }

        // expect() already throws an error if the token type doesn\\'t match.
        // return vyb::ast::TypeNode::newArray(array_loc, std::move(element_type), std::move(size_expression), false, false);
        return std::make_unique<vyb::ast::ArrayType>(array_loc, std::move(element_type), std::move(size_expression));

    } else if (this->match(vyb::TokenType::KEYWORD_FN)) {
        vyb::SourceLocation fn_loc = start_loc;
        this->expect(vyb::TokenType::LPAREN);
        std::vector<vyb::ast::TypeNodePtr> param_types_parsed;
        if (this->peek().type != vyb::TokenType::RPAREN) {
            do {
                param_types_parsed.push_back(this->parse());
            } while (this->match(vyb::TokenType::COMMA));
        }
        this->expect(vyb::TokenType::RPAREN);

        vyb::ast::TypeNodePtr return_type_parsed = nullptr;
        if (this->match(vyb::TokenType::ARROW)) {
            return_type_parsed = this->parse();
            if (!return_type_parsed) {
                 throw this->error(this->peek(), "Expected return type after \\'->\\' in function type at " + this->current_location().toString());
            }
        }
        // return vyb::ast::TypeNode::newFunctionSignature(fn_loc, std::move(param_types_parsed), std::move(return_type_parsed), false, false);
        return std::make_unique<vyb::ast::FunctionType>(fn_loc, std::move(param_types_parsed), std::move(return_type_parsed));
    }

    throw this->error(this->peek(), "Expected a type identifier, \\\\\\'(\\\\\\\\' or \\\\\\'fn\\\\\\\\' to start a base type, found " + this->peek().lexeme + " (" + vyb::token_type_to_string(this->peek().type) + ") at " + start_loc.toString());
}


// Parses postfix type constructs like array types (`[]`), pointer types (`*`), optional types (`?`)
vyb::ast::TypeNodePtr TypeParser::parse_postfix_type(vyb::ast::TypeNodePtr current_type) { // Corrected namespace for return type and parameter
    while (true) {
        SourceLocation op_loc = peek().location;
        if (this->match(vyb::TokenType::LT)) {
            // Generic type parameters
            std::vector<vyb::ast::TypeNodePtr> generic_args;
            // Parse comma-separated list of type arguments
            if (this->peek().type != vyb::TokenType::GT) { // Removed extra parenthesis
                do {
                    auto type_arg = this->parse();
                    if (!type_arg) {
                        throw this->error(this->peek(), "Expected type argument in generic type at " +
                                        this->current_location().toString());
                    }
                    generic_args.push_back(std::move(type_arg));
                } while (this->match(vyb::TokenType::COMMA));
            }
            this->expect(vyb::TokenType::GT);
            // Update the current_type with generic arguments
            // if (current_type->category == vyb::ast::TypeNode::Category::IDENTIFIER) {
            //     current_type->genericArguments = std::move(generic_args);
            if (auto* type_name_node = dynamic_cast<vyb::ast::TypeName*>(current_type.get())) {
                type_name_node->genericArgs = std::move(generic_args);
            } else {
                throw this->error(this->previous_token(), "Generic parameters can only be applied to identifier types");
            }
        } else if (this->match(vyb::TokenType::LBRACKET)) {
            SourceLocation lbracket_loc = this->previous_token().location;
            if (this->match(vyb::TokenType::RBRACKET)) {
                // current_type = vyb::ast::TypeNode::newArray(lbracket_loc, std::move(current_type), nullptr, false, false);
                current_type = std::make_unique<vyb::ast::ArrayType>(lbracket_loc, std::move(current_type), nullptr);
            } else {
                this->put_back_token();
                break;
            }
        } else if (this->match(vyb::TokenType::MULTIPLY)) {
            // if (current_type->isPointer) {
            if (dynamic_cast<vyb::ast::PointerType*>(current_type.get())) {
                throw this->error(this->previous_token(), "Type is already a pointer.");
            }
            // current_type->isPointer = true;
            current_type = std::make_unique<vyb::ast::PointerType>(op_loc, std::move(current_type));
        } else if (this->match(vyb::TokenType::QUESTION_MARK)) {
            // if (current_type->isOptional) {
            if (dynamic_cast<vyb::ast::OptionalType*>(current_type.get())) {
                throw this->error(this->previous_token(), "Type is already optional: " + current_type->loc.toString());
            }
            // current_type->isOptional = true;
            current_type = std::make_unique<vyb::ast::OptionalType>(op_loc, std::move(current_type));
        } else if (this->match(vyb::TokenType::KEYWORD_CONST)) {
            // TODO: Implement proper const type wrapper
            // For now, just consume the const token and continue parsing
            // This allows parsing to proceed while we develop const semantics
            // The const information will be lost but parsing won't fail
        }
        else {
            break;
        }
    }
    return current_type;
}

} // namespace vyb
