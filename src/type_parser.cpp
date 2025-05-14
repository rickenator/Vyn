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
vyn::TypeNodePtr TypeParser::parse() {
    this->skip_comments_and_newlines();
    vyn::SourceLocation start_loc = this->current_location();
    vyn::TypeNodePtr type;

    // This logic needs to be completely rewritten for the new TypeNode structure.
    // For now, just returning a placeholder or attempting a very basic parse.
    // Placeholder:
    // if (peek().type == vyn::TokenType::IDENTIFIER) {
    //     auto ident = std::make_unique<Identifier>(consume().loc, consume().lexeme);
    //     type = TypeNode::newIdentifier(start_loc, std::move(ident), {}, false, false);
    // } else {
    //     throw std::runtime_error("Type parsing not yet fully implemented for new TypeNode at " + start_loc.toString());
    // }
    // The old logic is left here for reference during the upcoming refactor,
    // but it will mostly be replaced.

    if (this->peek().type == vyn::TokenType::AMPERSAND) {
        this->consume(); // Consume '&'
        // bool is_mutable = false; // Mutable info lost for now
        if (this->match(vyn::TokenType::KEYWORD_MUT)) {
            // is_mutable = true;
        }
        auto referenced_type = this->parse(); // Recursively parse the type being referenced
        if (!referenced_type) {
            throw std::runtime_error("Expected type after \'&\' or \'&mut\' at " + start_loc.filePath + ":" + std::to_string(start_loc.line) + ":" + std::to_string(start_loc.column));
        }
        // Simplification: return the referenced type, losing reference information
        // This needs to be updated to create a Ptr or Borrow type node
        type = std::move(referenced_type); // Placeholder
    } else if (this->peek().type == vyn::TokenType::KEYWORD_FN) {
        this->consume(); // Consume \'fn\'
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
                 throw std::runtime_error("Expected return type after \'->\' in function type at " + this->current_location().filePath + ":" + std::to_string(this->current_location().line) + ":" + std::to_string(this->current_location().column));
            }
        }
        type = TypeNode::newFunctionSignature(start_loc, std::move(param_types_parsed), std::move(return_type_parsed), false, false);
    } else {
        type = this->parse_base_type();
    }

    if (!type) {
         throw std::runtime_error("Failed to parse type at " + start_loc.filePath + ":" + std::to_string(start_loc.line) + ":" + std::to_string(start_loc.column));
    }

    return this->parse_postfix_type(std::move(type));
}

vyn::TypeNodePtr TypeParser::parse_base_type() {
    this->skip_comments_and_newlines();
    vyn::SourceLocation loc = this->current_location();

    // This function will need significant changes to support ownership keywords (my, our, their, ptr)
    // and to correctly create different TypeNode categories.

    if (this->peek().type == vyn::TokenType::IDENTIFIER) {
        vyn::SourceLocation path_loc = this->current_location();
        std::string full_path_name_str = this->consume().lexeme;

        // Path resolution (e.g., module::type) can be handled here if needed,
        // for now, assume a simple identifier.
        // while (this->peek().type == vyn::TokenType::COLONCOLON) { ... }

        auto type_name_identifier = std::make_unique<vyn::Identifier>(path_loc, full_path_name_str);
        // Basic identifier type, no generics, not const, not optional by default here.
        // Postfix parsing will handle generics, const, optional.
        return TypeNode::newIdentifier(loc, std::move(type_name_identifier), {}, false, false);

    } else if (this->match(vyn::TokenType::LPAREN)) { // Tuple type or grouped type
        std::vector<vyn::TypeNodePtr> member_types_parsed;
        bool is_empty_tuple = true;
        if (this->peek().type != vyn::TokenType::RPAREN) {
            is_empty_tuple = false;
            do {
                member_types_parsed.push_back(this->parse()); 
            } while (this->match(vyn::TokenType::COMMA));
        }
        this->expect(vyn::TokenType::RPAREN);
        
        // If (T), it's a grouped type, not a tuple. The EBNF and RFC need to be clear.
        // Assuming (T) is handled by expression parser or simplified here.
        // For now, (T) will become a tuple of one element.
        // () is a unit tuple. (T, U) is a tuple.
        return TypeNode::newTuple(loc, std::move(member_types_parsed), false, false);

    } else if (this->match(vyn::TokenType::LBRACKET)) { // Array type [T] or [T; N]
        vyn::SourceLocation array_loc = loc;
        auto element_type = this->parse(); 
        if (!element_type) {
            throw std::runtime_error("Expected element type in array type definition at " + this->current_location().toString());
        }
        
        // Array with size [T; N] - Vyn doesn't have this in EBNF yet, assuming simple [T]
        // if (this->match(vyn::TokenType::SEMICOLON)) {
        //     auto size_expr_val = expr_parser_.parse(); 
        //     if (!size_expr_val) { 
        //          throw std::runtime_error("Expected size expression after \';\' in array type at " + this->current_location().toString());
        //     }
        // }
        this->expect(vyn::TokenType::RBRACKET);
        return TypeNode::newArray(array_loc, std::move(element_type), false, false);
    }
    
    // Handle ownership keywords: my, our, their, ptr
    // This needs to be integrated carefully with the IDENTIFIER case, as it's `my<T>` etc.
    // For now, this is a simplified placeholder.
    OwnershipKind ownership_kind;
    bool is_ownership_keyword = false;
    if (match(TokenType::KEYWORD_MY)) { ownership_kind = OwnershipKind::MY; is_ownership_keyword = true;}
    else if (match(TokenType::KEYWORD_OUR)) { ownership_kind = OwnershipKind::OUR; is_ownership_keyword = true;}
    else if (match(TokenType::KEYWORD_THEIR)) { ownership_kind = OwnershipKind::THEIR; is_ownership_keyword = true;}
    else if (match(TokenType::KEYWORD_PTR)) { ownership_kind = OwnershipKind::PTR; is_ownership_keyword = true;}

    if (is_ownership_keyword) {
        expect(TokenType::LT);
        TypeNodePtr wrapped_type = parse();
        expect(TokenType::GT);
        return TypeNode::newOwnershipWrapped(loc, ownership_kind, std::move(wrapped_type), false, false);
    }
    
    throw std::runtime_error("Expected a type identifier, '(', '[', or ownership keyword to start a type, found " + this->peek().lexeme + " (" + vyn::token_type_to_string(this->peek().type) + ") at " + loc.toString());
}

vyn::TypeNodePtr TypeParser::parse_postfix_type(vyn::TypeNodePtr base_type) {
    vyn::TypeNodePtr current_type = std::move(base_type);

    while (true) {
        this->skip_comments_and_newlines();
        vyn::SourceLocation postfix_loc = current_type ? current_type->loc : this->current_location();

        if (this->peek().type == vyn::TokenType::LT) { 
            if (!current_type) { 
                 throw std::runtime_error("Cannot apply generic arguments to a null base type at " + postfix_loc.toString());
            }
            // Ensure current_type is an IDENTIFIER category before adding generic args
            if (current_type->category != TypeCategory::IDENTIFIER) {
                throw std::runtime_error("Generic arguments can only be applied to identifier types at " + postfix_loc.toString());
            }

            this->consume(); // Consume \'<\'
            std::vector<vyn::TypeNodePtr> type_args_parsed;
            if (this->peek().type != vyn::TokenType::GT) {
                do {
                    type_args_parsed.push_back(this->parse()); 
                } while (this->match(vyn::TokenType::COMMA));
            }
            this->expect(vyn::TokenType::GT);

            // Modify the existing IDENTIFIER TypeNode to include generic arguments.
            // This assumes current_type is already a TypeNode::newIdentifier result.
            current_type->genericArguments = std::move(type_args_parsed);
            // The location might need to span the whole generic type `Id<T>`
            // current_type->loc = postfix_loc; // Or a new loc spanning start to end of GT

        } else if (this->peek().type == vyn::TokenType::LBRACKET) { 
             if (!current_type) { 
                 throw std::runtime_error("Cannot apply array operator to a null base type at " + postfix_loc.toString());
            }
            this->consume(); // LBRACKET
            this->expect(vyn::TokenType::RBRACKET);
            // Wrap the current_type in an Array TypeNode
            current_type = TypeNode::newArray(postfix_loc, std::move(current_type), current_type->dataIsConst, current_type->isOptional);
            // Note: const/optional from base_type are carried to the new array type wrapper.
            // This might need refinement based on RFC (e.g. (T const)[] vs (T[]) const)

        } else if (this->match(vyn::TokenType::QUESTION_MARK)) { // Optional type T?
            if (!current_type) {
                throw std::runtime_error("Cannot apply optional operator to a null base type at " + postfix_loc.toString());
            }
            current_type->isOptional = true;
            // The location should ideally be updated to include the '?'
            // current_type->loc = SourceLocation(...) // spanning original type + '?'

        } else if (this->match(vyn::TokenType::KEYWORD_CONST)) { // Const type T const
             if (!current_type) {
                throw std::runtime_error("Cannot apply const qualifier to a null base type at " + postfix_loc.toString());
            }
            current_type->dataIsConst = true;
            // The location should ideally be updated to include the 'const'
            // current_type->loc = SourceLocation(...) // spanning original type + 'const'
        }
        else {
            break; 
        }
    }
    return current_type;
}

} // namespace vyn
