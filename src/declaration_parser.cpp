#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include <stdexcept> // For std::runtime_error
#include <vector>
#include <memory>

// Constructor updated to accept TypeParser, ExpressionParser, and StatementParser references
DeclarationParser::DeclarationParser(const std::vector<Vyn::Token>& tokens, size_t& pos, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser, StatementParser& stmt_parser)
    : BaseParser(tokens, pos, file_path), type_parser_(type_parser), expr_parser_(expr_parser), stmt_parser_(stmt_parser) {}

std::unique_ptr<Vyn::AST::DeclNode> DeclarationParser::parse() {
    skip_comments_and_newlines();
    Vyn::Token current_token = peek();

    if (current_token.type == Vyn::TokenType::KEYWORD_FN) {
        return parse_function();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_STRUCT) {
        return parse_struct();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_IMPL) {
        return parse_impl();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_CLASS) { // Added
        return parse_class_declaration(); // Added
    } else if (current_token.type == Vyn::TokenType::KEYWORD_ENUM) { // Added
        return parse_enum_declaration(); // Added
    } else if (current_token.type == Vyn::TokenType::KEYWORD_TYPE) { // Added
        return parse_type_alias_declaration(); // Added
    } else if (current_token.type == Vyn::TokenType::KEYWORD_LET ||
               current_token.type == Vyn::TokenType::KEYWORD_VAR ||
               current_token.type == Vyn::TokenType::KEYWORD_CONST) {
        return parse_global_var_declaration();
    }
    // Add other declaration types here (e.g., const, static)
    
    if (current_token.type != Vyn::TokenType::EOF_TOKEN) {
         throw std::runtime_error("Unexpected token in DeclarationParser: " + current_token.value + " at " + current_location().toString());
    }
    return nullptr; 
}

std::vector<std::unique_ptr<Vyn::AST::GenericParamNode>> DeclarationParser::parse_generic_params() {
    std::vector<std::unique_ptr<Vyn::AST::GenericParamNode>> generic_params;
    if (match(Vyn::TokenType::LT)) { // <
        do {
            Vyn::AST::SourceLocation param_loc = current_location();
            if (peek().type != Vyn::TokenType::IDENTIFIER) {
                throw std::runtime_error("Expected identifier for generic parameter name at " + param_loc.toString());
            }
            auto param_name = std::make_unique<Vyn::AST::IdentifierNode>(param_loc, consume().value);

            std::vector<std::unique_ptr<Vyn::AST::TypeNode>> bounds;
            if (match(Vyn::TokenType::COLON)) { // Trait bounds, e.g., T: Bound1 + Bound2
                do {
                    auto bound_type = type_parser_.parse();
                    if (!bound_type) {
                        throw std::runtime_error("Expected trait bound type after ':' for generic parameter '" + param_name->name + "' at " + current_location().toString());
                    }
                    bounds.push_back(std::move(bound_type));
                } while (match(Vyn::TokenType::PLUS));
            }
            generic_params.push_back(std::make_unique<Vyn::AST::GenericParamNode>(param_loc, std::move(param_name), std::move(bounds)));
        } while (match(Vyn::TokenType::COMMA));
        expect(Vyn::TokenType::GT); // >
    }
    return generic_params;
}

std::unique_ptr<Vyn::AST::FuncDeclNode> DeclarationParser::parse_function() {
    Vyn::AST::SourceLocation loc = current_location();
    bool is_async = match(Vyn::TokenType::KEYWORD_ASYNC);
    bool is_extern = match(Vyn::TokenType::KEYWORD_EXTERN);
    expect(Vyn::TokenType::KEYWORD_FN);

    if (peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected function name at " + current_location().toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(current_location(), consume().value);

    auto generic_params = parse_generic_params(); // Parse generic parameters

    expect(Vyn::TokenType::LPAREN);
    std::vector<std::unique_ptr<Vyn::AST::ParamNode>> params;
    if (peek().type != Vyn::TokenType::RPAREN) {
        do {
            params.push_back(parse_param());
        } while (match(Vyn::TokenType::COMMA));
    }
    expect(Vyn::TokenType::RPAREN);

    std::unique_ptr<Vyn::AST::TypeNode> return_type = nullptr;
    if (match(Vyn::TokenType::ARROW)) {
        return_type = type_parser_.parse();
        if (!return_type) {
            throw std::runtime_error("Expected return type after '->' at " + current_location().toString());
        }
    }

    std::unique_ptr<Vyn::AST::BlockStmtNode> body = nullptr;
    if (peek().type == Vyn::TokenType::LBRACE) {
        body = stmt_parser_.parse_block();
        if (!body) {
             throw std::runtime_error("Failed to parse function body for '" + name->name + "' at " + current_location().toString());
        }
    } else if (!is_extern) {
        // Non-extern functions must have a body or a semicolon for forward declaration (if supported, not now)
        // For now, expect a body if not extern.
        // Or expect a semicolon if it's just a declaration (like in an interface/trait, or extern without body)
        // Current AST/parsing expects body for non-extern.
        throw std::runtime_error("Function '" + name->name + "' must have a body or be declared extern. At " + loc.toString());
    } else {
        expect(Vyn::TokenType::SEMICOLON); // Extern function declaration without body
    }

    return std::make_unique<Vyn::AST::FuncDeclNode>(loc, std::move(name), std::move(generic_params), std::move(params), std::move(return_type), std::move(body), is_async, is_extern);
}

std::unique_ptr<Vyn::AST::StructDeclNode> DeclarationParser::parse_struct() {
    Vyn::AST::SourceLocation loc = current_location();
    expect(Vyn::TokenType::KEYWORD_STRUCT);

    if (peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected struct name at " + current_location().toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(current_location(), consume().value);

    auto generic_params = parse_generic_params(); // Parse generic parameters

    expect(Vyn::TokenType::LBRACE);
    std::vector<std::pair<std::unique_ptr<Vyn::AST::IdentifierNode>, std::unique_ptr<Vyn::AST::TypeNode>>> fields;
    while (peek().type != Vyn::TokenType::RBRACE && peek().type != Vyn::TokenType::END_OF_FILE) {
        if (peek().type != Vyn::TokenType::IDENTIFIER) {
            throw std::runtime_error("Expected field name in struct '" + name->name + "' at " + current_location().toString());
        }
        auto field_name = std::make_unique<Vyn::AST::IdentifierNode>(current_location(), consume().value);
        
        expect(Vyn::TokenType::COLON);
        
        auto field_type = type_parser_.parse();
        if (!field_type) {
            throw std::runtime_error("Expected type for field '" + field_name->name + "' in struct '" + name->name + "' at " + current_location().toString());
        }
        fields.emplace_back(std::move(field_name), std::move(field_type));

        // Fields can be separated by comma or semicolon (or just newline in some languages)
        // Vyn style: let's assume comma is optional, but if present, consume it.
        // Newlines are skipped by skip_comments_and_newlines() before peeking next token.
        // If a semicolon is required, add expect(Vyn::TokenType::SEMICOLON);
        skip_comments_and_newlines(); // Ensure we skip to the next meaningful token
    }
    expect(Vyn::TokenType::RBRACE);

    return std::make_unique<Vyn::AST::StructDeclNode>(loc, std::move(name), std::move(generic_params), std::move(fields));
}

std::unique_ptr<Vyn::AST::ImplDeclNode> DeclarationParser::parse_impl() {
    Vyn::AST::SourceLocation loc = current_location();
    expect(Vyn::TokenType::KEYWORD_IMPL);

    auto generic_params_impl = parse_generic_params(); // Parse generic parameters for the impl block itself

    std::unique_ptr<Vyn::AST::TypeNode> trait_type = nullptr;
    std::unique_ptr<Vyn::AST::TypeNode> self_type = type_parser_.parse();
    if (!self_type) {
        throw std::runtime_error("Expected type name in impl block at " + current_location().toString());
    }

    if (match(Vyn::TokenType::KEYWORD_FOR)) {
        trait_type = std::move(self_type); // The first type parsed was the trait
        self_type = type_parser_.parse(); // Now parse the actual self type
        if (!self_type) {
            throw std::runtime_error("Expected type name after 'for' in impl block at " + current_location().toString());
        }
    }

    expect(Vyn::TokenType::LBRACE);
    std::vector<std::unique_ptr<Vyn::AST::FuncDeclNode>> methods;
    while (peek().type != Vyn::TokenType::RBRACE && peek().type != Vyn::TokenType::END_OF_FILE) {
        // Could parse other items here like associated types or consts if Vyn supports them in impls
        // For now, only functions (methods)
        // Need to ensure that parse_function is called correctly, it might expect `fn` keyword.
        // We can peek for `fn`, `async fn`, or `extern fn`.
        Vyn::Token current_token = peek();
        if (current_token.type == Vyn::TokenType::KEYWORD_FN || 
            current_token.type == Vyn::TokenType::KEYWORD_ASYNC || 
            current_token.type == Vyn::TokenType::KEYWORD_EXTERN) {
            methods.push_back(parse_function());
        } else {
            throw std::runtime_error("Expected function definition (fn, async fn, extern fn) inside impl block at " + current_location().toString());
        }
        skip_comments_and_newlines();
    }
    expect(Vyn::TokenType::RBRACE);

    return std::make_unique<Vyn::AST::ImplDeclNode>(loc, std::move(trait_type), std::move(self_type), std::move(generic_params_impl), std::move(methods));
}

std::unique_ptr<Vyn::AST::FieldDeclNode> DeclarationParser::parse_field_declaration() {
    Vyn::AST::SourceLocation loc = current_location();
    bool is_mutable = false;

    // Vyn's variable declarations use 'let' (immutable) and 'var' (mutable).
    // For fields, let's assume 'var' for mutable, and absence of 'var' (or 'let') for immutable by default.
    // Or, we can require 'let' or 'var'.
    // Given FieldDeclNode has is_mutable, we need a way to determine it.
    // Let's assume 'var' means mutable, and an identifier directly means immutable (like 'let').
    // This matches 'VarDeclStmtNode' where 'let' or 'var' is explicit.
    // For fields, 'main.vyn' shows "value: T" and "next: Node<T>". This implies immutable by default.
    // Let's require 'var' for mutable fields, and no keyword for immutable.
    
    if (match(Vyn::TokenType::KEYWORD_VAR)) {
        is_mutable = true;
    } else if (peek().type == Vyn::TokenType::KEYWORD_LET) {
        // Explicit 'let' for immutable field
        consume(); // consume 'let'
        is_mutable = false;
    }
    // If neither 'var' nor 'let', it's an identifier for an immutable field.

    if (peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected field name (identifier) at " + current_location().toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(current_location(), consume().value);

    expect(Vyn::TokenType::COLON);

    auto type_annotation = type_parser_.parse();
    if (!type_annotation) {
        throw std::runtime_error("Expected type annotation for field '" + name->name + "' at " + current_location().toString());
    }

    std::unique_ptr<Vyn::AST::ExprNode> initializer = nullptr;
    if (match(Vyn::TokenType::ASSIGN)) { // '='
        initializer = expr_parser_.parse();
        if (!initializer) {
            throw std::runtime_error("Expected initializer expression for field '" + name->name + "' after '=' at " + current_location().toString());
        }
    }

    // Fields in structs/classes usually end with a semicolon or are separated by commas/newlines.
    // AST.md for StructDeclNode implies fields are just listed.
    // Let's require a semicolon for fields for now for clarity, similar to C++/Java.
    // Or, if Vyn is Python-esque, newlines might be enough.
    // The btree.vyn example shows fields without semicolons, just newlines.
    // struct Node<T> { value: T, left: Node<T>, right: Node<T> }
    // class BTree<T> { root: Node<T> ... }
    // This implies comma separation or just whitespace separation within braces.
    // Let's assume for now that after a field, we might see a comma, or the closing brace, or another field/method.
    // The loop in parse_struct and upcoming parse_class_declaration will handle this.
    // So, parse_field_declaration itself might not need to expect a semicolon if it's part of a list.
    // However, if a field can be a standalone declaration (e.g. static fields), it might.
    // For now, let's assume it's called in a context where the separator is handled by the caller.

    return std::make_unique<Vyn::AST::FieldDeclNode>(loc, std::move(name), std::move(type_annotation), std::move(initializer), is_mutable);
}

std::unique_ptr<Vyn::AST::ClassDeclNode> DeclarationParser::parse_class_declaration() {
    Vyn::AST::SourceLocation loc = current_location();
    expect(Vyn::TokenType::KEYWORD_CLASS);

    if (peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected class name (identifier) at " + current_location().toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(current_location(), consume().value);

    auto generic_params = parse_generic_params();

    expect(Vyn::TokenType::LBRACE);
    skip_comments_and_newlines();

    std::vector<std::variant<std::unique_ptr<Vyn::AST::FieldDeclNode>, std::unique_ptr<Vyn::AST::FuncDeclNode>>> members;

    while (peek().type != Vyn::TokenType::RBRACE && peek().type != Vyn::TokenType::EOF_TOKEN) {
        skip_comments_and_newlines();
        Vyn::Token member_peek = peek();

        if (member_peek.type == Vyn::TokenType::KEYWORD_FN ||
            member_peek.type == Vyn::TokenType::KEYWORD_ASYNC || // check for async fn
            member_peek.type == Vyn::TokenType::KEYWORD_EXTERN) { // check for extern fn
            
            // Potentially skip KEYWORD_ASYNC or KEYWORD_EXTERN if parse_function expects to consume them itself
            // parse_function already handles async and extern keywords.
            auto func_decl = parse_function();
            if (!func_decl) { // Should not happen if parse_function throws on error
                 throw std::runtime_error("Failed to parse function member in class '" + name->name + "' at " + current_location().toString());
            }
            members.emplace_back(std::move(func_decl));
        } else if (member_peek.type == Vyn::TokenType::KEYWORD_VAR || 
                   member_peek.type == Vyn::TokenType::KEYWORD_LET ||
                   member_peek.type == Vyn::TokenType::IDENTIFIER) {
            // This indicates a field declaration
            auto field_decl = parse_field_declaration();
             if (!field_decl) { // Should not happen if parse_field_declaration throws on error
                 throw std::runtime_error("Failed to parse field member in class '" + name->name + "' at " + current_location().toString());
            }
            members.emplace_back(std::move(field_decl));
            
            // In Vyn, struct fields are comma-separated or just newline separated.
            // class members might follow suit.
            // If a semicolon is required after a field, it should be consumed by parse_field_declaration or checked here.
            // For now, assume parse_field_declaration handles its own termination or the structure is newline/comma based.
            // Let's try consuming an optional comma, similar to struct parsing.
            if (peek().type == Vyn::TokenType::COMMA) {
                consume();
            }

        } else if (member_peek.type == Vyn::TokenType::RBRACE) {
            break; // End of class body
        } else {
            throw std::runtime_error("Unexpected token '" + member_peek.value + "' in class body for '" + name->name + "' at " + current_location().toString());
        }
        skip_comments_and_newlines(); // Skip to next member or RBRACE
    }

    expect(Vyn::TokenType::RBRACE);

    return std::make_unique<Vyn::AST::ClassDeclNode>(loc, std::move(name), std::move(generic_params), std::move(members));
}

std::unique_ptr<Vyn::AST::EnumVariantNode> DeclarationParser::parse_enum_variant() {
    Vyn::AST::SourceLocation loc = current_location();
    if (peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected enum variant name (identifier) at " + loc.toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(loc, consume().value);

    std::vector<std::unique_ptr<Vyn::AST::TypeNode>> types;
    if (match(Vyn::TokenType::LPAREN)) {
        if (peek().type != Vyn::TokenType::RPAREN) { // Check if there are types
            do {
                auto type_node = type_parser_.parse();
                if (!type_node) {
                    throw std::runtime_error("Expected type in enum variant parameter list for '" + name->name + "' at " + current_location().toString());
                }
                types.push_back(std::move(type_node));
            } while (match(Vyn::TokenType::COMMA));
        }
        expect(Vyn::TokenType::RPAREN);
    }
    // Struct-like variants are not supported in this iteration, only simple or tuple-like.

    return std::make_unique<Vyn::AST::EnumVariantNode>(loc, std::move(name), std::move(types));
}

std::unique_ptr<Vyn::AST::EnumDeclNode> DeclarationParser::parse_enum_declaration() {
    Vyn::AST::SourceLocation loc = current_location();
    expect(Vyn::TokenType::KEYWORD_ENUM);

    if (peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected enum name (identifier) at " + current_location().toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(current_location(), consume().value);

    auto generic_params = parse_generic_params();

    expect(Vyn::TokenType::LBRACE);
    skip_comments_and_newlines();

    std::vector<std::unique_ptr<Vyn::AST::EnumVariantNode>> variants;
    while (peek().type != Vyn::TokenType::RBRACE && peek().type != Vyn::TokenType::EOF_TOKEN) {
        auto variant_node = parse_enum_variant();
        if (!variant_node) { // Should not happen if parse_enum_variant throws
            throw std::runtime_error("Failed to parse enum variant for enum '" + name->name + "' at " + current_location().toString());
        }
        variants.push_back(std::move(variant_node));

        skip_comments_and_newlines();
        if (match(Vyn::TokenType::COMMA)) {
            skip_comments_and_newlines();
            if (peek().type == Vyn::TokenType::RBRACE) { // Trailing comma before closing brace
                break;
            }
        } else if (peek().type != Vyn::TokenType::RBRACE) {
            // If not a comma and not a closing brace, it must be another variant, 
            // or an error if variants must be comma-separated.
            // For now, let's assume variants can be separated by newlines (handled by skip_comments_and_newlines)
            // or commas. If it's not a comma here, the next iteration should pick up the next variant or RBRACE.
            // However, to be stricter, we might require commas if it's not the last item before RBRACE.
            // Let's enforce comma separation unless it's the last element.
             if (peek().type != Vyn::TokenType::IDENTIFIER && peek().type != Vyn::TokenType::RBRACE) {
                  throw std::runtime_error("Expected comma or closing brace after enum variant in enum '" + name->name + "' at " + current_location().toString());
             }
        }
    }

    expect(Vyn::TokenType::RBRACE);

    return std::make_unique<Vyn::AST::EnumDeclNode>(loc, std::move(name), std::move(generic_params), std::move(variants));
}

std::unique_ptr<Vyn::AST::TypeAliasDeclNode> DeclarationParser::parse_type_alias_declaration() {
    Vyn::AST::SourceLocation loc = current_location();
    expect(Vyn::TokenType::KEYWORD_TYPE);

    if (peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected type alias name (identifier) at " + current_location().toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(current_location(), consume().value);

    auto generic_params = parse_generic_params();

    expect(Vyn::TokenType::ASSIGN); // '='

    auto aliased_type = type_parser_.parse();
    if (!aliased_type) {
        throw std::runtime_error("Expected type definition after '=' for type alias '" + name->name + "' at " + current_location().toString());
    }

    expect(Vyn::TokenType::SEMICOLON);

    return std::make_unique<Vyn::AST::TypeAliasDeclNode>(loc, std::move(name), std::move(generic_params), std::move(aliased_type));
}

std::unique_ptr<Vyn::AST::GlobalVarDeclNode> DeclarationParser::parse_global_var_declaration() {
    Vyn::AST::SourceLocation loc = current_location();
    bool is_mutable = false;
    bool is_const = false;

    if (match(Vyn::TokenType::KEYWORD_VAR)) {
        is_mutable = true;
    } else if (match(Vyn::TokenType::KEYWORD_CONST)) {
        is_const = true;
    } else {
        expect(Vyn::TokenType::KEYWORD_LET);
        // is_mutable remains false, is_const remains false
    }

    // The mutability context for the pattern itself. 
    // 'const' implies immutable bindings within the pattern.
    // 'var' implies mutable bindings by default within the pattern.
    // 'let' implies immutable bindings by default within the pattern.
    bool pattern_context_is_mutable = is_mutable; 

    // StatementParser has parse_pattern, but it's not directly accessible as a public method of DeclarationParser's stmt_parser_ member in a clean way for this specific use.
    // However, StatementParser itself is a friend or accessible. Let's assume we can call it.
    // The parse_pattern method in StatementParser is public.
    // We need to pass the StatementParser instance to parse_pattern, or make parse_pattern a static utility, or duplicate logic.
    // The current structure: DeclarationParser has a stmt_parser_ reference.
    // So, stmt_parser_.parse_pattern() should work.
    // The parse_pattern in StatementParser.hpp is: std::unique_ptr<Vyn::AST::PatternNode> parse_pattern(bool is_context_mutable);
    // This is what we need.
    auto pattern = stmt_parser_.parse_pattern(pattern_context_is_mutable);
    if (!pattern) {
        throw std::runtime_error("Expected pattern in global variable/constant declaration at " + loc.toString());
    }

    std::unique_ptr<Vyn::AST::TypeNode> type_annotation = nullptr;
    if (match(Vyn::TokenType::COLON)) {
        type_annotation = type_parser_.parse();
        if (!type_annotation) {
            throw std::runtime_error("Expected type annotation after ':' in global variable/constant declaration at " + current_location().toString());
        }
    }

    std::unique_ptr<Vyn::AST::ExprNode> initializer = nullptr;
    if (match(Vyn::TokenType::ASSIGN)) { // '='
        initializer = expr_parser_.parse();
        if (!initializer) {
            throw std::runtime_error("Expected initializer expression after '=' in global variable/constant declaration at " + current_location().toString());
        }
    }

    expect(Vyn::TokenType::SEMICOLON);

    return std::make_unique<Vyn::AST::GlobalVarDeclNode>(loc, std::move(pattern), std::move(type_annotation), std::move(initializer), is_mutable, is_const);
}


// ... (parse_param and other existing methods)
