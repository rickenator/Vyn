#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include <stdexcept>
#include <vector>
#include <memory>
#include <string>

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
         throw std::runtime_error("Failed to parse type at " + start_loc.toString());
    }

    // Then, parse any postfix operators (generics, array, optional, const)
    return this->parse_postfix_type(std::move(type));
}

vyn::TypeNodePtr TypeParser::parse_base_or_ownership_wrapped_type() {
    this->skip_comments_and_newlines();
    vyn::SourceLocation loc = this->current_location();

    // Check for ownership keywords
    OwnershipKind ownership_kind;
    bool is_ownership_keyword = false;

    if (this->check(TokenType::KEYWORD_MY)) {
        ownership_kind = OwnershipKind::MY;
        is_ownership_keyword = true;
    } else if (this->check(TokenType::KEYWORD_OUR)) {
        ownership_kind = OwnershipKind::OUR;
        is_ownership_keyword = true;
    } else if (this->check(TokenType::KEYWORD_THEIR)) {
        ownership_kind = OwnershipKind::THEIR;
        is_ownership_keyword = true;
    } else if (this->check(TokenType::KEYWORD_PTR)) {
        ownership_kind = OwnershipKind::PTR;
        is_ownership_keyword = true;
    }

    if (is_ownership_keyword) {
        this->consume(); // Consume the ownership keyword
        this->expect(TokenType::LT);
        // The inner type of an ownership wrapper cannot itself be another ownership-wrapped type directly.
        // It must be a base type which can then have postfix operators.
        // So, we call parse_atomic_or_group_type, then parse_postfix_type for its content.
        vyn::TypeNodePtr wrapped_base_type = this->parse_atomic_or_group_type();
        if (!wrapped_base_type) {
            throw std::runtime_error("Expected a type inside ownership wrapper < > at " + this->current_location().toString());
        }
        vyn::TypeNodePtr full_wrapped_type = this->parse_postfix_type(std::move(wrapped_base_type));
        this->expect(TokenType::GT);
        // Ownership wrappers are neither const nor optional themselves; those apply to the wrapped type.
        return TypeNode::newOwnershipWrapped(loc, ownership_kind, std::move(full_wrapped_type), false, false);
    }

    // If not an ownership keyword, parse other base types
    return this->parse_atomic_or_group_type();
}


vyn::TypeNodePtr TypeParser::parse_atomic_or_group_type() {
    this->skip_comments_and_newlines();
    vyn::SourceLocation loc = this->current_location();

    if (this->peek().type == vyn::TokenType::IDENTIFIER) {
        vyn::SourceLocation path_loc = this->current_location();
        // For now, assume a simple identifier. Path resolution (e.g., module::type)
        // would involve parsing multiple IDENTIFIER and COLONCOLON tokens.
        std::string name_str = this->consume().lexeme;
        auto type_name_identifier = std::make_unique<vyn::Identifier>(path_loc, name_str);
        // Default: not const, not optional. Postfix parsing will handle these.
        // Generics are also handled by postfix parsing.
        return TypeNode::newIdentifier(loc, std::move(type_name_identifier), {}, false, false);

    } else if (this->match(vyn::TokenType::LPAREN)) { // Tuple type or grouped type
        std::vector<vyn::TypeNodePtr> member_types_parsed;
        if (this->peek().type != vyn::TokenType::RPAREN) {
            do {
                // Each element of a tuple is a full type
                member_types_parsed.push_back(this->parse());
            } while (this->match(vyn::TokenType::COMMA));
        }
        this->expect(vyn::TokenType::RPAREN);

        // According to RFC, (T) is not a tuple, but a grouping.
        // However, the EBNF `type_atom = IDENTIFIER | "(" type ")" | tuple_type | array_type | function_type;`
        // and `tuple_type = "(" type ("," type)* ","? ")" ;`
        // This implies `(T)` would be parsed by `parse()` recursively.
        // If `member_types_parsed.size() == 1` and the original tokens were `(T)`,
        // it should be treated as just `T` (parenthesized type).
        // The current AST TypeNode doesn't have a "GROUPED" category.
        // For now, we assume that if `(T)` is encountered, `parse()` will correctly return the TypeNode for `T`.
        // If `()` or `(T, U, ...)` then it's a tuple.
        // The EBNF `type_tuple = "(" ( type ( "," type )* ","? )? ")" ;` means `()` is an empty tuple.
        // `(T,)` is a tuple of one element.
        // If `(T)` was meant to be a grouped type, the expression parser should handle it, or
        // the type parser should return the inner type directly if it's unambiguous.
        // Let's assume for now that `(T)` without a comma is not creating a tuple here,
        // and that parenthesized expressions are handled at a higher level or simplified away.
        // The EBNF `type_primary = type_path | type_tuple | type_array | type_function | "(" type ")" ;`
        // suggests `(type)` should just result in `type`.
        // This function `parse_atomic_or_group_type` is for the non-parenthesized versions.
        // So, if we are here after a LPAREN, it must be a tuple.
        return TypeNode::newTuple(loc, std::move(member_types_parsed), false, false);

    } else if (this->match(vyn::TokenType::LBRACKET)) { // Array type [Type]
        vyn::SourceLocation array_loc = loc; // Location of the '['
        // The element type is a full type
        auto element_type = this->parse();
        if (!element_type) {
            throw std::runtime_error("Expected element type in array type definition at " + this->current_location().toString());
        }
        this->expect(vyn::TokenType::RBRACKET);
        // Default: not const, not optional. Postfix parsing will handle these for the array itself.
        return TypeNode::newArray(array_loc, std::move(element_type), false, false);

    } else if (this->match(vyn::TokenType::KEYWORD_FN)) { // Function signature type fn(T1, T2) -> Ret
        vyn::SourceLocation fn_loc = loc; // Location of 'fn'
        this->expect(vyn::TokenType::LPAREN);
        std::vector<vyn::TypeNodePtr> param_types_parsed;
        if (this->peek().type != vyn::TokenType::RPAREN) {
            do {
                // Each parameter type is a full type
                param_types_parsed.push_back(this->parse());
            } while (this->match(vyn::TokenType::COMMA));
        }
        this->expect(vyn::TokenType::RPAREN);

        vyn::TypeNodePtr return_type_parsed = nullptr;
        if (this->match(vyn::TokenType::ARROW)) {
            // The return type is a full type
            return_type_parsed = this->parse();
            if (!return_type_parsed) {
                 throw std::runtime_error("Expected return type after '->' in function type at " + this->current_location().toString());
            }
        }
        // Default: not const, not optional.
        return TypeNode::newFunctionSignature(fn_loc, std::move(param_types_parsed), std::move(return_type_parsed), false, false);
    }

    // If none of the above, it's an error.
    throw std::runtime_error("Expected a type identifier, '(', '[', or 'fn' to start a base type, found " + this->peek().lexeme + " (" + vyn::token_type_to_string(this->peek().type) + ") at " + loc.toString());
}

vyn::TypeNodePtr TypeParser::parse_postfix_type(vyn::TypeNodePtr base_type) {
    vyn::TypeNodePtr current_type = std::move(base_type);

    while (true) {
        this->skip_comments_and_newlines();
        // The location for postfix operators should ideally span the original type + operator.
        // For simplicity, we can use the location of the operator or the start of the base type.
        // current_type will not be null here due to check in parse().
        vyn::SourceLocation postfix_op_loc = this->current_location();


        if (this->peek().type == vyn::TokenType::LT && current_type->category == TypeCategory::IDENTIFIER) {
            // Generic arguments: Type<Arg1, Arg2>
            // Check if current_type is an IDENTIFIER and has no generic arguments yet.
            // The RFC implies `my<T><U>` is not valid; generics apply to the inner name.
            // `my<T<U>>` is valid. `T<U><V>` is not valid.
            if (!current_type->genericArguments.empty()) {
                 throw std::runtime_error("Type already has generic arguments at " + postfix_op_loc.toString());
            }

            this->consume(); // Consume '<'
            std::vector<vyn::TypeNodePtr> type_args_parsed;
            if (this->peek().type != vyn::TokenType::GT) {
                do {
                    // Each generic argument is a full type
                    type_args_parsed.push_back(this->parse());
                } while (this->match(vyn::TokenType::COMMA));
            }
            this->expect(vyn::TokenType::GT);

            // Modify the existing IDENTIFIER TypeNode to include generic arguments.
            current_type->genericArguments = std::move(type_args_parsed);
            // Update location to span from base_type->loc.start to current_location().end (after '>')
            // For now, we keep current_type->loc as the start of the identifier.
            // A more accurate location would be:
            // current_type->loc = vyn::SourceLocation{current_type->loc.filePath, current_type->loc.line, current_type->loc.column, current_location().line, current_location().column};

        } else if (this->peek().type == vyn::TokenType::LBRACKET) {
            // Array from base type: BaseType[]
            // This is different from `[ElementType]` which is parsed by `parse_atomic_or_group_type`.
            // Example: `Id[]` becomes `Array(Id, false, false)`
            // Example: `my<T>[]` becomes `Array(OwnershipWrapped(MY, T), false, false)`
            this->consume(); // Consume '['
            this->expect(vyn::TokenType::RBRACKET); // Expect ']'

            // The new array type wraps the current_type.
            // Const-ness and optionality of the *array itself* are false by default here.
            // They can be applied later, e.g., `T[] const` or `T[]?`.
            // The const-ness/optionality of `current_type` (the element) is preserved.
            current_type = TypeNode::newArray(current_type->loc, std::move(current_type), false, false);
            // The location `current_type->loc` is currently that of the original `current_type`.
            // A more accurate location would span from original start to ']' end.

        } else if (this->match(vyn::TokenType::QUESTION_MARK)) { // Optional type: Type?
            if (current_type->isOptional) {
                throw std::runtime_error("Type is already optional: " + current_type->loc.toString());
            }
            current_type->isOptional = true;
            // Update location to span original type + '?'
            // current_type->loc.endLine = postfix_op_loc.line; (if SourceLocation supports end pos)
            // current_type->loc.endColumn = postfix_op_loc.column;

        } else if (this->match(vyn::TokenType::KEYWORD_CONST)) { // Const type: Type const
            if (current_type->dataIsConst) {
                 throw std::runtime_error("Type is already const: " + current_type->loc.toString());
            }
            current_type->dataIsConst = true;
            // Update location to span original type + 'const'
        }
        else {
            break; // No more postfix operators
        }
    }
    return current_type;
}

} // namespace vyn
