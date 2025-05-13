#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include <stdexcept> // For std::runtime_error
#include <vector>
#include <memory>
#include <variant> // Added for std::variant

namespace Vyn { // Added namespace

// Constructor updated to accept TypeParser, ExpressionParser, and StatementParser references
DeclarationParser::DeclarationParser(const std::vector<Vyn::Token>& tokens, size_t& pos, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser, StatementParser& stmt_parser)
    : BaseParser(tokens, pos, file_path), type_parser_(type_parser), expr_parser_(expr_parser), stmt_parser_(stmt_parser) {}

std::unique_ptr<Vyn::AST::DeclNode> DeclarationParser::parse() {
    this->skip_comments_and_newlines();
    Vyn::Token current_token = this->peek();

    if (current_token.type == Vyn::TokenType::KEYWORD_FN) {
        return this->parse_function();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_STRUCT) {
        return this->parse_struct();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_IMPL) {
        return this->parse_impl();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_CLASS) { // Added
        return this->parse_class_declaration(); // Added
    } else if (current_token.type == Vyn::TokenType::KEYWORD_ENUM) { // Added
        return this->parse_enum_declaration(); // Added
    } else if (current_token.type == Vyn::TokenType::KEYWORD_TYPE) { // Added
        return this->parse_type_alias_declaration(); // Added
    } else if (current_token.type == Vyn::TokenType::KEYWORD_LET ||
               current_token.type == Vyn::TokenType::KEYWORD_VAR ||
               current_token.type == Vyn::TokenType::KEYWORD_CONST) {
        return this->parse_global_var_declaration();
    }
    // Add other declaration types here (e.g., const, static)
    
    if (current_token.type != Vyn::TokenType::END_OF_FILE) { // Corrected EOF_TOKEN
         throw std::runtime_error("Unexpected token in DeclarationParser: " + current_token.lexeme + " at " + this->current_location().toString()); // Corrected .value and current_location call
    }
    return nullptr; 
}

std::vector<std::unique_ptr<Vyn::AST::GenericParamNode>> DeclarationParser::parse_generic_params() {
    std::vector<std::unique_ptr<Vyn::AST::GenericParamNode>> generic_params;
    if (this->match(Vyn::TokenType::LT)) { // <
        do {
            Vyn::AST::SourceLocation param_loc = this->current_location();
            if (this->peek().type != Vyn::TokenType::IDENTIFIER) {
                throw std::runtime_error("Expected identifier for generic parameter name at " + param_loc.toString());
            }
            auto param_name = std::make_unique<Vyn::AST::IdentifierNode>(param_loc, this->consume().lexeme); // Corrected .value

            std::vector<std::unique_ptr<Vyn::AST::TypeNode>> bounds;
            if (this->match(Vyn::TokenType::COLON)) { // Trait bounds, e.g., T: Bound1 + Bound2
                do {
                    auto bound_type = this->type_parser_.parse();
                    if (!bound_type) {
                        throw std::runtime_error("Expected trait bound type after ':' for generic parameter '" + param_name->name + "' at " + this->current_location().toString());
                    }
                    bounds.push_back(std::move(bound_type));
                } while (this->match(Vyn::TokenType::PLUS));
            }
            generic_params.push_back(std::make_unique<Vyn::AST::GenericParamNode>(param_loc, std::move(param_name), std::move(bounds)));
        } while (this->match(Vyn::TokenType::COMMA));
        this->expect(Vyn::TokenType::GT); // >
    }
    return generic_params;
}

// Forward declaration for parse_param if it's a private helper method of DeclarationParser
// If it's defined elsewhere or part of another class, this might not be needed here.
// For now, assuming it's a helper that will be defined within this class.
std::unique_ptr<Vyn::AST::ParamNode> DeclarationParser::parse_param() {
    Vyn::AST::SourceLocation loc = this->current_location();
    bool is_mutable = this->match(Vyn::TokenType::KEYWORD_MUT).has_value();

    if (this->peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected parameter name (identifier) at " + loc.toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(this->current_location(), this->consume().lexeme);

    this->expect(Vyn::TokenType::COLON);

    auto type = this->type_parser_.parse();
    if (!type) {
        throw std::runtime_error("Expected type annotation for parameter '" + name->name + "' at " + this->current_location().toString());
    }
    
    // Optional default value - NOTE: ParamNode AST currently does not store this.
    // Parsing it here but not using it in the constructor for now.
    std::unique_ptr<Vyn::AST::ExprNode> default_value = nullptr;
    if (this->match(Vyn::TokenType::EQ)) { 
        default_value = this->expr_parser_.parse();
        if (!default_value) {
            throw std::runtime_error("Expected expression for default value of parameter '" + name->name + "' at " + this->current_location().toString());
        }
    }
    // Corrected constructor call: (loc, isRef, isMut, name, typeAnnotation)
    // Assuming isRef is false for now. is_mutable corresponds to isMut.
    return std::make_unique<Vyn::AST::ParamNode>(loc, false, is_mutable, std::move(name), std::move(type));
}


std::unique_ptr<Vyn::AST::FuncDeclNode> DeclarationParser::parse_function() {
    Vyn::AST::SourceLocation loc = this->current_location();
    bool is_async = this->match(Vyn::TokenType::KEYWORD_ASYNC).has_value();
    bool is_extern = this->match(Vyn::TokenType::KEYWORD_EXTERN).has_value();
    this->expect(Vyn::TokenType::KEYWORD_FN);

    if (this->peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected function name at " + this->current_location().toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(this->current_location(), this->consume().lexeme); // Corrected .value

    auto generic_params = this->parse_generic_params(); // Parse generic parameters

    this->expect(Vyn::TokenType::LPAREN);
    std::vector<std::unique_ptr<Vyn::AST::ParamNode>> params;
    if (this->peek().type != Vyn::TokenType::RPAREN) {
        do {
            params.push_back(this->parse_param()); // Corrected: call as member
        } while (this->match(Vyn::TokenType::COMMA));
    }
    this->expect(Vyn::TokenType::RPAREN);

    std::unique_ptr<Vyn::AST::TypeNode> return_type = nullptr;
    if (this->match(Vyn::TokenType::ARROW)) {
        return_type = this->type_parser_.parse();
        if (!return_type) {
            throw std::runtime_error("Expected return type after \'->\' at " + this->current_location().toString());
        }
    }

    std::unique_ptr<Vyn::AST::BlockStmtNode> body = nullptr;
    if (this->peek().type == Vyn::TokenType::LBRACE) {
        body = this->stmt_parser_.parse_block();
        if (!body) {
             throw std::runtime_error("Failed to parse function body for '" + name->name + "' at " + this->current_location().toString());
        }
    } else if (!is_extern) {
        throw std::runtime_error("Function '" + name->name + "' must have a body or be declared extern. At " + loc.toString());
    } else {
        this->expect(Vyn::TokenType::SEMICOLON); // Extern function declaration without body
    }

    return std::make_unique<Vyn::AST::FuncDeclNode>(loc, std::move(name), std::move(generic_params), std::move(params), std::move(return_type), std::move(body), is_async, is_extern);
}

std::unique_ptr<Vyn::AST::StructDeclNode> DeclarationParser::parse_struct() {
    Vyn::AST::SourceLocation loc = this->current_location();
    this->expect(Vyn::TokenType::KEYWORD_STRUCT);

    if (this->peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected struct name at " + this->current_location().toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(this->current_location(), this->consume().lexeme); // Corrected .value

    auto generic_params = this->parse_generic_params(); // Parse generic parameters

    this->expect(Vyn::TokenType::LBRACE);
    std::vector<std::pair<std::unique_ptr<Vyn::AST::IdentifierNode>, std::unique_ptr<Vyn::AST::TypeNode>>> fields_data;
    while (this->peek().type != Vyn::TokenType::RBRACE && this->peek().type != Vyn::TokenType::END_OF_FILE) { // Corrected EOF_TOKEN
        if (this->peek().type != Vyn::TokenType::IDENTIFIER) {
            throw std::runtime_error("Expected field name in struct '" + name->name + "' at " + this->current_location().toString());
        }
        auto field_name = std::make_unique<Vyn::AST::IdentifierNode>(this->current_location(), this->consume().lexeme); // Corrected .value
        
        this->expect(Vyn::TokenType::COLON);
        
        auto field_type = this->type_parser_.parse();
        if (!field_type) {
            throw std::runtime_error("Expected type for field '" + field_name->name + "' in struct '" + name->name + "' at " + this->current_location().toString());
        }
        fields_data.emplace_back(std::move(field_name), std::move(field_type));

        this->skip_comments_and_newlines(); 
    }
    this->expect(Vyn::TokenType::RBRACE);

    std::vector<std::unique_ptr<Vyn::AST::FieldDeclNode>> fields;
    for (auto& field_pair : fields_data) {
        fields.push_back(std::make_unique<Vyn::AST::FieldDeclNode>(
            field_pair.first->location, // Corrected: loc to location
            std::move(field_pair.first), 
            std::move(field_pair.second), 
            nullptr, 
            false    
        ));
    }

    return std::make_unique<Vyn::AST::StructDeclNode>(loc, std::move(name), std::move(generic_params), std::move(fields));
}

std::unique_ptr<Vyn::AST::ImplDeclNode> DeclarationParser::parse_impl() {
    Vyn::AST::SourceLocation loc = this->current_location();
    this->expect(Vyn::TokenType::KEYWORD_IMPL);

    auto generic_params_impl = this->parse_generic_params(); 

    std::unique_ptr<Vyn::AST::TypeNode> trait_type = nullptr;
    std::unique_ptr<Vyn::AST::TypeNode> self_type = this->type_parser_.parse();
    if (!self_type) {
        throw std::runtime_error("Expected type name in impl block at " + this->current_location().toString());
    }

    if (this->match(Vyn::TokenType::KEYWORD_FOR)) {
        trait_type = std::move(self_type); 
        self_type = this->type_parser_.parse(); 
        if (!self_type) {
            throw std::runtime_error("Expected type name after \'for\' in impl block at " + this->current_location().toString());
        }
    }

    this->expect(Vyn::TokenType::LBRACE);
    std::vector<std::unique_ptr<Vyn::AST::FuncDeclNode>> methods;
    while (this->peek().type != Vyn::TokenType::RBRACE && this->peek().type != Vyn::TokenType::END_OF_FILE) { // Corrected EOF_TOKEN
        Vyn::Token current_token = this->peek();
        if (current_token.type == Vyn::TokenType::KEYWORD_FN || 
            current_token.type == Vyn::TokenType::KEYWORD_ASYNC || 
            current_token.type == Vyn::TokenType::KEYWORD_EXTERN) {
            methods.push_back(this->parse_function());
        } else {
            throw std::runtime_error("Expected function definition (fn, async fn, extern fn) inside impl block at " + this->current_location().toString());
        }
        this->skip_comments_and_newlines();
    }
    this->expect(Vyn::TokenType::RBRACE);

    return std::make_unique<Vyn::AST::ImplDeclNode>(loc, std::move(trait_type), std::move(self_type), std::move(generic_params_impl), std::move(methods));
}

std::unique_ptr<Vyn::AST::FieldDeclNode> DeclarationParser::parse_field_declaration() {
    Vyn::AST::SourceLocation loc = this->current_location();
    bool is_mutable = false;
    
    if (this->match(Vyn::TokenType::KEYWORD_VAR)) {
        is_mutable = true;
    } else if (this->peek().type == Vyn::TokenType::KEYWORD_LET) {
        this->consume(); 
        is_mutable = false;
    }

    if (this->peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected field name (identifier) at " + this->current_location().toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(this->current_location(), this->consume().lexeme); // Corrected .value

    this->expect(Vyn::TokenType::COLON);

    auto type_annotation = this->type_parser_.parse();
    if (!type_annotation) {
        throw std::runtime_error("Expected type annotation for field '" + name->name + "' at " + this->current_location().toString());
    }

    std::unique_ptr<Vyn::AST::ExprNode> initializer = nullptr;
    if (this->match(Vyn::TokenType::EQ)) { // Corrected ASSIGN to EQ
        initializer = this->expr_parser_.parse();
        if (!initializer) {
            throw std::runtime_error("Expected initializer expression for field '" + name->name + "' after '=' at " + this->current_location().toString());
        }
    }
    return std::make_unique<Vyn::AST::FieldDeclNode>(loc, std::move(name), std::move(type_annotation), std::move(initializer), is_mutable);
}

std::unique_ptr<Vyn::AST::ClassDeclNode> DeclarationParser::parse_class_declaration() {
    Vyn::AST::SourceLocation loc = this->current_location();
    this->expect(Vyn::TokenType::KEYWORD_CLASS);

    if (this->peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected class name (identifier) at " + this->current_location().toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(this->current_location(), this->consume().lexeme); // Corrected .value

    auto generic_params = this->parse_generic_params();

    this->expect(Vyn::TokenType::LBRACE);
    this->skip_comments_and_newlines();

    std::vector<std::variant<std::unique_ptr<Vyn::AST::FieldDeclNode>, std::unique_ptr<Vyn::AST::FuncDeclNode>>> members_variant;

    while (this->peek().type != Vyn::TokenType::RBRACE && this->peek().type != Vyn::TokenType::END_OF_FILE) { // Corrected EOF_TOKEN
        this->skip_comments_and_newlines();
        Vyn::Token member_peek = this->peek();

        if (member_peek.type == Vyn::TokenType::KEYWORD_FN ||
            member_peek.type == Vyn::TokenType::KEYWORD_ASYNC || 
            member_peek.type == Vyn::TokenType::KEYWORD_EXTERN) { 
            auto func_decl = this->parse_function();
            if (!func_decl) { 
                 throw std::runtime_error("Failed to parse function member in class '" + name->name + "' at " + this->current_location().toString());
            }
            members_variant.emplace_back(std::move(func_decl));
        } else if (member_peek.type == Vyn::TokenType::KEYWORD_VAR || 
                   member_peek.type == Vyn::TokenType::KEYWORD_LET ||
                   member_peek.type == Vyn::TokenType::IDENTIFIER) {
            auto field_decl = this->parse_field_declaration();
             if (!field_decl) { 
                 throw std::runtime_error("Failed to parse field member in class '" + name->name + "' at " + this->current_location().toString());
            }
            members_variant.emplace_back(std::move(field_decl));
            
            if (this->peek().type == Vyn::TokenType::COMMA) {
                this->consume();
            }

        } else if (member_peek.type == Vyn::TokenType::RBRACE) {
            break; 
        } else {
            throw std::runtime_error("Unexpected token '" + member_peek.lexeme + "' in class body for '" + name->name + "' at " + this->current_location().toString()); // Corrected .value
        }
        this->skip_comments_and_newlines(); 
    }

    this->expect(Vyn::TokenType::RBRACE);

    std::vector<std::unique_ptr<Vyn::AST::DeclNode>> members;
    for (auto& member_var : members_variant) {
        std::visit([&members](auto&& arg) {
            members.push_back(std::move(arg));
        }, std::move(member_var));
    }

    return std::make_unique<Vyn::AST::ClassDeclNode>(loc, std::move(name), std::move(generic_params), std::move(members));
}

std::unique_ptr<Vyn::AST::EnumVariantNode> DeclarationParser::parse_enum_variant() {
    Vyn::AST::SourceLocation loc = this->current_location();
    if (this->peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected enum variant name (identifier) at " + loc.toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(loc, this->consume().lexeme); // Corrected .value

    std::vector<std::unique_ptr<Vyn::AST::TypeNode>> types;
    if (this->match(Vyn::TokenType::LPAREN)) {
        if (this->peek().type != Vyn::TokenType::RPAREN) { 
            do {
                auto type_node = this->type_parser_.parse();
                if (!type_node) {
                    throw std::runtime_error("Expected type in enum variant parameter list for '" + name->name + "' at " + this->current_location().toString());
                }
                types.push_back(std::move(type_node));
            } while (this->match(Vyn::TokenType::COMMA));
        }
        this->expect(Vyn::TokenType::RPAREN);
    }
    return std::make_unique<Vyn::AST::EnumVariantNode>(loc, std::move(name), std::move(types));
}

std::unique_ptr<Vyn::AST::EnumDeclNode> DeclarationParser::parse_enum_declaration() {
    Vyn::AST::SourceLocation loc = this->current_location();
    this->expect(Vyn::TokenType::KEYWORD_ENUM);

    if (this->peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected enum name (identifier) at " + this->current_location().toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(this->current_location(), this->consume().lexeme); // Corrected .value

    auto generic_params = this->parse_generic_params();

    this->expect(Vyn::TokenType::LBRACE);
    this->skip_comments_and_newlines();

    std::vector<std::unique_ptr<Vyn::AST::EnumVariantNode>> variants;
    while (this->peek().type != Vyn::TokenType::RBRACE && this->peek().type != Vyn::TokenType::END_OF_FILE) { // Corrected EOF_TOKEN
        auto variant_node = this->parse_enum_variant();
        if (!variant_node) { 
            throw std::runtime_error("Failed to parse enum variant for enum '" + name->name + "' at " + this->current_location().toString());
        }
        variants.push_back(std::move(variant_node));

        this->skip_comments_and_newlines();
        if (this->match(Vyn::TokenType::COMMA)) {
            this->skip_comments_and_newlines();
            if (this->peek().type == Vyn::TokenType::RBRACE) { 
                break;
            }
        } else if (this->peek().type != Vyn::TokenType::RBRACE) {
             if (this->peek().type != Vyn::TokenType::IDENTIFIER && this->peek().type != Vyn::TokenType::RBRACE) { // Check if next is identifier for next variant
                  throw std::runtime_error("Expected comma or closing brace after enum variant in enum '" + name->name + "' at " + this->current_location().toString());
             }
        }
    }

    this->expect(Vyn::TokenType::RBRACE);

    return std::make_unique<Vyn::AST::EnumDeclNode>(loc, std::move(name), std::move(generic_params), std::move(variants));
}

std::unique_ptr<Vyn::AST::TypeAliasDeclNode> DeclarationParser::parse_type_alias_declaration() {
    Vyn::AST::SourceLocation loc = this->current_location();
    this->expect(Vyn::TokenType::KEYWORD_TYPE);

    if (this->peek().type != Vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected type alias name (identifier) at " + this->current_location().toString());
    }
    auto name = std::make_unique<Vyn::AST::IdentifierNode>(this->current_location(), this->consume().lexeme); // Corrected .value

    auto generic_params = this->parse_generic_params();

    this->expect(Vyn::TokenType::EQ); // Corrected ASSIGN to EQ

    auto aliased_type = this->type_parser_.parse();
    if (!aliased_type) {
        throw std::runtime_error("Expected type definition after '=' for type alias '" + name->name + "' at " + this->current_location().toString());
    }

    this->expect(Vyn::TokenType::SEMICOLON);

    return std::make_unique<Vyn::AST::TypeAliasDeclNode>(loc, std::move(name), std::move(generic_params), std::move(aliased_type));
}

std::unique_ptr<Vyn::AST::GlobalVarDeclNode> DeclarationParser::parse_global_var_declaration() {
    Vyn::AST::SourceLocation loc = this->current_location();
    bool is_mutable = false;
    bool is_const = false;

    if (this->match(Vyn::TokenType::KEYWORD_VAR)) {
        is_mutable = true;
    } else if (this->match(Vyn::TokenType::KEYWORD_CONST)) {
        is_const = true;
    } else {
        this->expect(Vyn::TokenType::KEYWORD_LET);
    }

    bool pattern_context_is_mutable = is_mutable; 

    auto pattern = this->stmt_parser_.parse_pattern(pattern_context_is_mutable);
    if (!pattern) {
        throw std::runtime_error("Expected pattern in global variable/constant declaration at " + loc.toString());
    }

    std::unique_ptr<Vyn::AST::TypeNode> type_annotation = nullptr;
    if (this->match(Vyn::TokenType::COLON)) {
        type_annotation = this->type_parser_.parse();
        if (!type_annotation) {
            throw std::runtime_error("Expected type annotation after ':' in global variable/constant declaration at " + this->current_location().toString());
        }
    }

    std::unique_ptr<Vyn::AST::ExprNode> initializer = nullptr;
    if (this->match(Vyn::TokenType::EQ)) { // Corrected ASSIGN to EQ
        initializer = this->expr_parser_.parse();
        if (!initializer) {
            throw std::runtime_error("Expected initializer expression after '=' in global variable/constant declaration at " + this->current_location().toString());
        }
    }

    this->expect(Vyn::TokenType::SEMICOLON);

    return std::make_unique<Vyn::AST::GlobalVarDeclNode>(loc, std::move(pattern), std::move(type_annotation), std::move(initializer), is_mutable, is_const);
}

} // End namespace Vyn
