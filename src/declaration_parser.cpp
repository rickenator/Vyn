#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include <stdexcept> // For std::runtime_error
#include <vector>
#include <memory>
#include <variant> // Added for std::variant
#include <string> // Required for std::to_string

// Helper function for converting SourceLocation to string
// This should ideally be part of SourceLocation or a utility header
namespace {
    std::string location_to_string(const vyn::SourceLocation& loc) {
        return loc.filePath + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column);
    }
}

namespace vyn { // Changed Vyn to vyn

// Constructor updated to accept TypeParser, ExpressionParser, and StatementParser references
DeclarationParser::DeclarationParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser, StatementParser& stmt_parser)
    : BaseParser(tokens, pos, file_path), type_parser_(type_parser), expr_parser_(expr_parser), stmt_parser_(stmt_parser) {}

vyn::DeclPtr DeclarationParser::parse() { // Changed Vyn::AST::DeclNode to vyn::DeclPtr
    this->skip_comments_and_newlines();
    vyn::token::Token current_token = this->peek(); // Changed Vyn::Token

    if (current_token.type == vyn::TokenType::KEYWORD_FN) { // Changed Vyn::TokenType
        return this->parse_function();
    } else if (current_token.type == vyn::TokenType::KEYWORD_STRUCT) { // Changed Vyn::TokenType
        // Assuming parse_struct returns a type compatible with DeclPtr, might need adjustment
        // For now, let's assume StructDeclaration is a Declaration
        auto struct_decl = this->parse_struct();
        // return std::unique_ptr<Declaration>(static_cast<Declaration*>(struct_decl.release()));
        return struct_decl; // Direct return if StructDeclaration derives from Declaration
    } else if (current_token.type == vyn::TokenType::KEYWORD_IMPL) { // Changed Vyn::TokenType
        // Similar assumption for ImplDeclaration
        auto impl_decl = this->parse_impl();
        // return std::unique_ptr<Declaration>(static_cast<Declaration*>(impl_decl.release()));
        return impl_decl; // Direct return if ImplDeclaration derives from Declaration
    } else if (current_token.type == vyn::TokenType::KEYWORD_CLASS) { // Added & Changed Vyn::TokenType
        auto class_decl = this->parse_class_declaration();
        // return std::unique_ptr<Declaration>(static_cast<Declaration*>(class_decl.release()));
        return class_decl; // Direct return if ClassDeclaration derives from Declaration
    } else if (current_token.type == vyn::TokenType::KEYWORD_ENUM) { // Added & Changed Vyn::TokenType
        auto enum_decl = this->parse_enum_declaration();
        // return std::unique_ptr<Declaration>(static_cast<Declaration*>(enum_decl.release()));
        return enum_decl; // Direct return if EnumDeclaration derives from Declaration
    } else if (current_token.type == vyn::TokenType::KEYWORD_TYPE) { // Added & Changed Vyn::TokenType
        return this->parse_type_alias_declaration();
    } else if (current_token.type == vyn::TokenType::KEYWORD_LET || // Changed Vyn::TokenType
               current_token.type == vyn::TokenType::KEYWORD_VAR || // Changed Vyn::TokenType
               current_token.type == vyn::TokenType::KEYWORD_CONST) { // Changed Vyn::TokenType
        return this->parse_global_var_declaration();
    }
    
    if (current_token.type != vyn::TokenType::END_OF_FILE) { // Changed Vyn::TokenType
         throw std::runtime_error("Unexpected token in DeclarationParser: " + current_token.lexeme + " at " + location_to_string(this->current_location()));
    }
    return nullptr; 
}

// Note: ast.hpp does not have GenericParamNode, ParamNode, FuncDeclNode, StructDeclNode, ImplDeclNode, FieldDeclNode, ClassDeclNode, EnumVariantNode, EnumDeclNode, GlobalVarDeclNode
// These will need to be added to ast.hpp or the parser needs to be updated to use existing/general types.
// For now, assuming these types *will* exist as specialized Declaration nodes or similar.
// The return types and make_unique calls will reflect this assumption.
// If they are meant to be generic vyn::NodePtr, the parser.hpp and these definitions need to change.

// Assuming GenericParameter is a struct/class that can be held in a vector, not necessarily a Node.
// For now, let's assume it's a structure specific to FunctionDeclaration or similar, not a generic AST Node.
// If GenericParamNode is intended to be an AST node, it should derive from vyn::Node.
// For now, changing return type to std::vector<std::unique_ptr<vyn::GenericParamNode>> as per parser.hpp
std::vector<std::unique_ptr<vyn::GenericParamNode>> DeclarationParser::parse_generic_params() {
    std::vector<std::unique_ptr<vyn::GenericParamNode>> generic_params;
    if (this->match(vyn::TokenType::LT)) { // <
        do {
            vyn::SourceLocation param_loc = this->current_location();
            if (this->peek().type != vyn::TokenType::IDENTIFIER) {
                throw std::runtime_error("Expected identifier for generic parameter name at " + location_to_string(param_loc));
            }
            auto param_name = std::make_unique<vyn::Identifier>(param_loc, this->consume().lexeme);

            std::vector<vyn::TypeAnnotationPtr> bounds;
            if (this->match(vyn::TokenType::COLON)) { 
                do {
                    auto bound_type = this->type_parser_.parse(); // Returns TypeAnnotationPtr
                    if (!bound_type) {
                        throw std::runtime_error("Expected trait bound type after ':' for generic parameter at " + location_to_string(this->current_location()));
                    }
                    bounds.push_back(std::move(bound_type));
                } while (this->match(vyn::TokenType::PLUS)); // Assuming '+' separates multiple bounds
            }
            generic_params.push_back(std::make_unique<vyn::GenericParamNode>(param_loc, std::move(param_name), std::move(bounds)));
        } while (this->match(vyn::TokenType::COMMA));
        this->expect(vyn::TokenType::GT); // >
    }
    return generic_params;
}


// parse_param returns vyn::NodePtr as per parser.hpp
// ast.hpp has FunctionParameter struct, but not as a Node.
// This implies parse_param should create a structure that's then used by parse_function,
// rather than returning a generic NodePtr directly into a list of Nodes for generic_params.
// For now, adhering to parser.hpp return type.
// This will likely require creating a wrapper Node for FunctionParameter or changing parser.hpp.
// Let's assume for now it creates an Identifier node for the parameter name.
std::unique_ptr<vyn::Node> DeclarationParser::parse_param() {
    vyn::SourceLocation loc = this->current_location();
    // bool is_mutable = this->match(vyn::TokenType::KEYWORD_MUT).has_value(); // 'mut' keyword for params?

    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected parameter name (identifier) at " + location_to_string(loc));
    }
    auto name_ident = std::make_unique<vyn::Identifier>(this->current_location(), this->consume().lexeme);

    this->expect(vyn::TokenType::COLON);

    auto type_annot = this->type_parser_.parse(); // Returns TypeAnnotationPtr
    if (!type_annot) {
        throw std::runtime_error("Expected type annotation for parameter '" + name_ident->name + "' at " + location_to_string(this->current_location()));
    }
    
    // Default value parsing - not stored in FunctionParameter struct in ast.hpp currently.
    if (this->match(vyn::TokenType::EQ)) { 
        auto default_value = this->expr_parser_.parse();
        if (!default_value) {
            throw std::runtime_error("Expected expression for default value of parameter '" + name_ident->name + "' at " + location_to_string(this->current_location()));
        }
    }
    // Returning Identifier as the Node. TypeAnnotation is associated but not part of this single Node.
    // This is a simplification. A proper ParameterNode would be better.
    // For now, to satisfy NodePtr, we return the name. The type is parsed but not directly part of this return.
    // This means parse_function will need to call parse_param and then separately parse_type or this needs to return a more complex node.
    // Given parser.hpp, we return NodePtr. Let's assume FunctionParameter struct is built inside parse_function.
    // So, parse_param should probably return a struct or pair, not a NodePtr directly for the function's param list.
    // Re-evaluating: parse_function uses FunctionParameter struct. So parse_param should probably return that struct.
    // But parser.hpp says parse_param returns vyn::NodePtr. This is a contradiction.
    // For now, let's assume parse_param is a helper that returns FunctionParameter struct, and parser.hpp is outdated for this specific case or refers to a different context of "param".
    // Let's change parse_param to return FunctionParameter for internal use by parse_function.
    // This means parser.hpp needs an update if parse_param is public API returning NodePtr.
    // For now, this function will not be used directly based on parser.hpp's return type.
    // The code below will be for a hypothetical internal helper.
    // ---
    // Reverting to parser.hpp signature for now and returning Identifier.
    // The type annotation will be parsed but not directly part of the returned Identifier node.
    // The caller (parse_function) will need to manage this.
    return name_ident; // This is problematic as type info is lost.
}

// Actual internal helper to build FunctionParameter struct
vyn::FunctionParameter DeclarationParser::parse_function_parameter_struct() {
    vyn::SourceLocation loc = this->current_location();
    // mutability for parameters?
    // bool is_mutable = this->match(vyn::TokenType::KEYWORD_MUT).has_value();

    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected parameter name (identifier) at " + location_to_string(loc));
    }
    auto name_ident = std::make_unique<vyn::Identifier>(this->current_location(), this->consume().lexeme);

    this->expect(vyn::TokenType::COLON);

    auto type_annot = this->type_parser_.parse(); // Returns TypeAnnotationPtr
    if (!type_annot) {
        throw std::runtime_error("Expected type annotation for parameter '" + name_ident->name + "' at " + location_to_string(this->current_location()));
    }
    
    // Default value parsing - not stored in FunctionParameter struct in ast.hpp currently.
    if (this->match(vyn::TokenType::EQ)) { 
        auto default_value = this->expr_parser_.parse();
        if (!default_value) {
            throw std::runtime_error("Expected expression for default value of parameter '" + name_ident->name + "' at " + location_to_string(this->current_location()));
        }
        // default_value is parsed but not stored in FunctionParameter
    }
    return vyn::FunctionParameter(loc, std::move(name_ident), std::move(type_annot));
}


// Returns vyn::FunctionDeclaration as per parser.hpp (assuming it's a DeclPtr compatible type)
std::unique_ptr<vyn::FunctionDeclaration> DeclarationParser::parse_function() {
    vyn::SourceLocation loc = this->current_location();
    bool is_async = this->match(vyn::TokenType::KEYWORD_ASYNC).has_value();
    bool is_extern = this->match(vyn::TokenType::KEYWORD_EXTERN).has_value(); // Assuming extern functions are a concept
    this->expect(vyn::TokenType::KEYWORD_FN);

    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected function name at " + location_to_string(this->current_location()));
    }
    auto name = std::make_unique<vyn::Identifier>(this->current_location(), this->consume().lexeme);

    // auto generic_params_nodes = this->parse_generic_params(); // Returns std::vector<std::unique_ptr<vyn::Node>>
    // FunctionDeclaration in ast.hpp does not take generic_params. This needs to be added or handled differently.
    // For now, parsing them but not passing to constructor.
    // This parse_generic_params is for struct/class/enum/impl generics, not function generics yet.
    // If function generics are different, a new parsing function might be needed.
    // For now, we assume parse_generic_params here is for the declaration-level generics.
    // FunctionDeclaration does not currently support generic parameters in its AST node.
    // So, we parse them if they appear but don't use them for FunctionDeclaration.
    // This might be an error or a feature to be added to FunctionDeclaration later.
    // For now, let's assume function generics are not parsed by *this* parse_generic_params call.
    // If they were, they'd need to be stored.
    // std::vector<std::unique_ptr<vyn::GenericParamNode>> func_generic_params = this->parse_generic_params();


    this->expect(vyn::TokenType::LPAREN);
    std::vector<vyn::FunctionParameter> params_structs;
    if (this->peek().type != vyn::TokenType::RPAREN) {
        do {
            params_structs.push_back(this->parse_function_parameter_struct());
        } while (this->match(vyn::TokenType::COMMA));
    }
    this->expect(vyn::TokenType::RPAREN);

    vyn::TypeAnnotationPtr return_type = nullptr;
    if (this->match(vyn::TokenType::ARROW)) {
        return_type = this->type_parser_.parse();
        if (!return_type) {
            throw std::runtime_error("Expected return type after '->' at " + location_to_string(this->current_location()));
        }
    }

    std::unique_ptr<vyn::BlockStatement> body = nullptr;
    if (this->peek().type == vyn::TokenType::LBRACE) {
        body = this->stmt_parser_.parse_block();
        if (!body) {
             throw std::runtime_error("Failed to parse function body for '" + name->name + "' at " + location_to_string(this->current_location()));
        }
    } else if (!is_extern) { // If not extern, a body is expected
        throw std::runtime_error("Function '" + name->name + "' must have a body or be declared extern. At " + location_to_string(loc));
    } else { // Extern function without a body, expect a semicolon
        this->expect(vyn::TokenType::SEMICOLON);
    }
    // vyn::FunctionDeclaration constructor: loc, id, params, body, isAsync, returnType
    return std::make_unique<vyn::FunctionDeclaration>(loc, std::move(name), std::move(params_structs), std::move(body), is_async, std::move(return_type));
}

// StructDeclNode not in ast.hpp. Assuming a Declaration type for it.
// parser.hpp: std::unique_ptr<vyn::Declaration> parse_struct();
// Similar to struct, this needs a vyn::StructDeclaration in ast.hpp.
std::unique_ptr<vyn::Declaration> DeclarationParser::parse_struct() {
    vyn::SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_STRUCT);

    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected struct name at " + location_to_string(this->current_location()));
    }
    auto name = std::make_unique<vyn::Identifier>(this->current_location(), this->consume().lexeme);

    auto generic_params = this->parse_generic_params(); // Now returns std::vector<std::unique_ptr<vyn::GenericParamNode>>

    this->expect(vyn::TokenType::LBRACE);
    
    std::vector<std::unique_ptr<vyn::FieldDeclaration>> fields; // Changed type

    while (this->peek().type != vyn::TokenType::RBRACE && this->peek().type != vyn::TokenType::END_OF_FILE) {
        vyn::SourceLocation field_loc = this->current_location();
        
        if (this->peek().type != vyn::TokenType::IDENTIFIER) {
            throw std::runtime_error("Expected field name in struct '" + name->name + "' at " + location_to_string(this->current_location()));
        }
        auto field_name = std::make_unique<vyn::Identifier>(this->current_location(), this->consume().lexeme);
        
        this->expect(vyn::TokenType::COLON);
        
        auto field_type_annotation = this->type_parser_.parse();
        if (!field_type_annotation) {
            throw std::runtime_error("Expected type for field '" + field_name->name + "' in struct '" + name->name + "' at " + location_to_string(this->current_location()));
        }
        
        // Struct fields are immutable by default (isMutable=false) and have no initializers in their definition here.
        fields.push_back(std::make_unique<vyn::FieldDeclaration>(field_loc, std::move(field_name), std::move(field_type_annotation), nullptr, false));

        this->skip_comments_and_newlines(); 
        if (this->match(vyn::TokenType::COMMA)) {
             this->skip_comments_and_newlines();
             if (this->peek().type == vyn::TokenType::RBRACE) break; // Trailing comma
        } else if (this->peek().type != vyn::TokenType::RBRACE) {
            throw std::runtime_error("Expected comma or closing brace after struct field in '" + name->name + "' at " + location_to_string(this->current_location()));
        }
    }
    this->expect(vyn::TokenType::RBRACE);

    return std::make_unique<vyn::StructDeclaration>(loc, std::move(name), std::move(generic_params), std::move(fields));
}


// ImplDeclNode not in ast.hpp. Assuming a Declaration type for it.
// parser.hpp: std::unique_ptr<vyn::Declaration> parse_impl();
// Similar to struct, this needs a vyn::ImplDeclaration in ast.hpp.
std::unique_ptr<vyn::Declaration> DeclarationParser::parse_impl() {
    vyn::SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_IMPL);

    auto generic_params = this->parse_generic_params(); // Now returns std::vector<std::unique_ptr<vyn::GenericParamNode>>

    vyn::TypeAnnotationPtr trait_type = nullptr; 
    vyn::TypeAnnotationPtr self_type = this->type_parser_.parse(); 
    if (!self_type) {
        throw std::runtime_error("Expected type name in impl block at " + location_to_string(this->current_location()));
    }

    if (this->match(vyn::TokenType::KEYWORD_FOR)) {
        trait_type = std::move(self_type); 
        self_type = this->type_parser_.parse(); 
        if (!self_type) {
            throw std::runtime_error("Expected type name after 'for' in impl block at " + location_to_string(this->current_location()));
        }
    }

    this->expect(vyn::TokenType::LBRACE);
    std::vector<std::unique_ptr<vyn::FunctionDeclaration>> methods;
    while (this->peek().type != vyn::TokenType::RBRACE && this->peek().type != vyn::TokenType::END_OF_FILE) {
        vyn::token::Token current_token = this->peek();
        if (current_token.type == vyn::TokenType::KEYWORD_FN || 
            current_token.type == vyn::TokenType::KEYWORD_ASYNC || 
            current_token.type == vyn::TokenType::KEYWORD_EXTERN) {
            methods.push_back(this->parse_function());
        } else {
            throw std::runtime_error("Expected function definition (fn, async fn, extern fn) inside impl block at " + location_to_string(this->current_location()));
        }
        this->skip_comments_and_newlines();
    }
    this->expect(vyn::TokenType::RBRACE);

    return std::make_unique<vyn::ImplDeclaration>(loc, std::move(generic_params), std::move(self_type), std::move(methods), std::move(trait_type));
}


// FieldDeclNode not in ast.hpp. VariableDeclaration is the closest.
// parser.hpp: std::unique_ptr<vyn::Declaration> parse_field_declaration();
// This should likely return a vyn::VariableDeclaration.
std::unique_ptr<vyn::Declaration> DeclarationParser::parse_field_declaration() {
    vyn::SourceLocation loc = this->current_location();
    bool is_const_decl = false; // 'let' implies const
    
    if (this->match(vyn::TokenType::KEYWORD_VAR)) {
        is_const_decl = false;
    } else if (this->match(vyn::TokenType::KEYWORD_LET)) {
        is_const_decl = true;
    } else {
        // If no keyword, assume it's like 'let' (immutable) for a field context.
        // This path might be hit if called for class fields without explicit let/var.
        // Defaulting to immutable if no keyword.
        is_const_decl = true; 
    }


    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected field name (identifier) at " + location_to_string(this->current_location()));
    }
    auto name = std::make_unique<vyn::Identifier>(this->current_location(), this->consume().lexeme);

    this->expect(vyn::TokenType::COLON);

    auto type_annotation = this->type_parser_.parse();
    if (!type_annotation) {
        throw std::runtime_error("Expected type annotation for field '" + name->name + "' at " + location_to_string(this->current_location()));
    }

    vyn::ExprPtr initializer = nullptr;
    if (this->match(vyn::TokenType::EQ)) { 
        initializer = this->expr_parser_.parse();
        if (!initializer) {
            throw std::runtime_error("Expected initializer expression for field '" + name->name + "' after '=' at " + location_to_string(this->current_location()));
        }
    }
    // Using FieldDeclaration. isMutable is the opposite of isConst.
    return std::make_unique<vyn::FieldDeclaration>(loc, std::move(name), std::move(type_annotation), std::move(initializer), !is_const_decl);
}

// ClassDeclNode not in ast.hpp.
// parser.hpp: std::unique_ptr<vyn::Declaration> parse_class_declaration();
std::unique_ptr<vyn::Declaration> DeclarationParser::parse_class_declaration() {
    vyn::SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_CLASS);

    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected class name (identifier) at " + location_to_string(this->current_location()));
    }
    auto name = std::make_unique<vyn::Identifier>(this->current_location(), this->consume().lexeme);

    auto generic_params = this->parse_generic_params(); // Now returns std::vector<std::unique_ptr<vyn::GenericParamNode>>

    this->expect(vyn::TokenType::LBRACE);
    this->skip_comments_and_newlines();

    std::vector<vyn::DeclPtr> members;

    while (this->peek().type != vyn::TokenType::RBRACE && this->peek().type != vyn::TokenType::END_OF_FILE) {
        this->skip_comments_and_newlines();
        vyn::token::Token member_peek = this->peek();

        if (member_peek.type == vyn::TokenType::KEYWORD_FN ||
            member_peek.type == vyn::TokenType::KEYWORD_ASYNC || 
            member_peek.type == vyn::TokenType::KEYWORD_EXTERN) { 
            auto func_decl = this->parse_function();
            if (!func_decl) { 
                 throw std::runtime_error("Failed to parse function member in class '" + name->name + "' at " + location_to_string(this->current_location()));
            }
            members.push_back(std::move(func_decl));
        } else if (member_peek.type == vyn::TokenType::KEYWORD_VAR || 
                   member_peek.type == vyn::TokenType::KEYWORD_LET ||
                   member_peek.type == vyn::TokenType::IDENTIFIER) { 
            auto field_decl = this->parse_field_declaration(); // Returns DeclPtr (FieldDeclaration)
             if (!field_decl) { 
                 throw std::runtime_error("Failed to parse field member in class '" + name->name + "' at " + location_to_string(this->current_location()));
            }
            members.push_back(std::move(field_decl));
            
            if (this->match(vyn::TokenType::SEMICOLON)) {
                // Consumed semicolon
            } else if (this->match(vyn::TokenType::COMMA)) {
                // Consumed comma
            }
        } else if (member_peek.type == vyn::TokenType::RBRACE) {
            break; 
        } else {
            throw std::runtime_error("Unexpected token '" + member_peek.lexeme + "' in class body for '" + name->name + "' at " + location_to_string(this->current_location()));
        }
        this->skip_comments_and_newlines(); 
    }

    this->expect(vyn::TokenType::RBRACE);

    return std::make_unique<vyn::ClassDeclaration>(loc, std::move(name), std::move(generic_params), std::move(members));
}


// EnumVariantNode not in ast.hpp.
// This is a helper, its return type is not in parser.hpp's public API.
// Let's assume it should return a struct or a specific Node type if EnumVariant is an AST concept.
// For now, let's assume an EnumVariant is represented by an Identifier (its name) and a list of TypeAnnotations for associated types.
// This structure needs to be defined in ast.hpp.
// For now, parse_enum_variant will return a vyn::Identifier for the variant name.
// The associated types will be parsed but need to be stored in a proper EnumVariant AST node.
// This is a temporary simplification.
// Let's assume an EnumVariant is an Identifier for now.
std::unique_ptr<vyn::EnumVariantNode> DeclarationParser::parse_enum_variant() { // Changed return type
    vyn::SourceLocation loc = this->current_location();
    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected enum variant name (identifier) at " + location_to_string(loc));
    }
    auto name = std::make_unique<vyn::Identifier>(loc, this->consume().lexeme);

    std::vector<vyn::TypeAnnotationPtr> types; // For tuple-like variants: Variant(Type1, Type2)
    if (this->match(vyn::TokenType::LPAREN)) {
        if (this->peek().type != vyn::TokenType::RPAREN) { 
            do {
                auto type_node = this->type_parser_.parse();
                if (!type_node) {
                    throw std::runtime_error("Expected type in enum variant parameter list for '" + name->name + "' at " + location_to_string(this->current_location()));
                }
                types.push_back(std::move(type_node));
            } while (this->match(vyn::TokenType::COMMA));
        }
        this->expect(vyn::TokenType::RPAREN);
    }
    return std::make_unique<vyn::EnumVariantNode>(loc, std::move(name), std::move(types));
}

// EnumDeclNode not in ast.hpp.
// parser.hpp: std::unique_ptr<vyn::Declaration> parse_enum_declaration();
std::unique_ptr<vyn::Declaration> DeclarationParser::parse_enum_declaration() {
    vyn::SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_ENUM);

    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected enum name (identifier) at " + location_to_string(this->current_location()));
    }
    auto name = std::make_unique<vyn::Identifier>(this->current_location(), this->consume().lexeme);

    auto generic_params = this->parse_generic_params(); // Now returns std::vector<std::unique_ptr<vyn::GenericParamNode>>

    this->expect(vyn::TokenType::LBRACE);
    this->skip_comments_and_newlines();

    std::vector<std::unique_ptr<vyn::EnumVariantNode>> variants; // Changed type

    while (this->peek().type != vyn::TokenType::RBRACE && this->peek().type != vyn::TokenType::END_OF_FILE) {
        auto variant_node = this->parse_enum_variant(); // Now returns EnumVariantNodePtr
        if (!variant_node) { 
            throw std::runtime_error("Failed to parse enum variant for enum '" + name->name + "' at " + location_to_string(this->current_location()));
        }
        variants.push_back(std::move(variant_node));

        this->skip_comments_and_newlines();
        if (this->match(vyn::TokenType::COMMA)) {
            this->skip_comments_and_newlines();
            if (this->peek().type == vyn::TokenType::RBRACE) { 
                break; // Allow trailing comma
            }
        } else if (this->peek().type != vyn::TokenType::RBRACE) {
             // If not a comma and not a closing brace, it must be another variant identifier (if syntax allows)
             // or an error. The original code checked for IDENTIFIER here.
             // A new variant must start with an IDENTIFIER.
             if (this->peek().type != vyn::TokenType::IDENTIFIER && this->peek().type != vyn::TokenType::RBRACE) {
                  throw std::runtime_error("Expected comma, closing brace, or next variant identifier after enum variant in enum '" + name->name + "' at " + location_to_string(this->current_location()));
             }
        }
    }

    this->expect(vyn::TokenType::RBRACE);

    return std::make_unique<vyn::EnumDeclaration>(loc, std::move(name), std::move(generic_params), std::move(variants));
}


// Returns vyn::TypeAliasDeclaration as per parser.hpp (DeclPtr compatible)
std::unique_ptr<vyn::TypeAliasDeclaration> DeclarationParser::parse_type_alias_declaration() {
    vyn::SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_TYPE);

    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected type alias name (identifier) at " + location_to_string(this->current_location()));
    }
    auto name = std::make_unique<vyn::Identifier>(this->current_location(), this->consume().lexeme);

    auto generic_params = this->parse_generic_params(); // TypeAliasDeclaration in ast.hpp does not take generic_params.

    this->expect(vyn::TokenType::EQ);

    auto aliased_type = this->type_parser_.parse(); // Returns TypeAnnotationPtr
    if (!aliased_type) {
        throw std::runtime_error("Expected type definition after '=' for type alias '" + name->name + "' at " + location_to_string(this->current_location()));
    }

    this->expect(vyn::TokenType::SEMICOLON);

    // vyn::TypeAliasDeclaration constructor: loc, name, typeAnnotation
    // The generic_params are parsed but not used here.
    return std::make_unique<vyn::TypeAliasDeclaration>(loc, std::move(name), std::move(aliased_type));
}

// GlobalVarDeclNode not in ast.hpp. VariableDeclaration is the one.
// parser.hpp: std::unique_ptr<vyn::VariableDeclaration> parse_global_var_declaration();
std::unique_ptr<vyn::VariableDeclaration> DeclarationParser::parse_global_var_declaration() {
    vyn::SourceLocation loc = this->current_location();
    bool is_const_decl = false; // 'let' or 'const' implies const
    // VariableDeclaration has 'isConst'. 'var' means !isConst. 'let' or 'const' means isConst.

    if (this->match(vyn::TokenType::KEYWORD_VAR)) {
        is_const_decl = false;
    } else if (this->match(vyn::TokenType::KEYWORD_CONST)) {
        is_const_decl = true;
    } else {
        this->expect(vyn::TokenType::KEYWORD_LET); // Default to 'let' if no other keyword
        is_const_decl = true;
    }

    // The pattern itself. parse_pattern now returns ExprPtr and takes no bool.
    // The 'is_const_decl' applies to the VariableDeclaration, not the pattern parsing itself.
    vyn::ExprPtr pattern_expr = this->stmt_parser_.parse_pattern(); 
    if (!pattern_expr) {
        throw std::runtime_error("Expected pattern in global variable/constant declaration at " + location_to_string(loc));
    }

    // A VariableDeclaration expects a single Identifier for its 'id' field.
    // If parse_pattern returns a complex pattern, it cannot directly be used.
    // This was a known issue from statement_parser.cpp.
    // For global variables, the pattern is usually just an identifier.
    // We need to dynamic_cast pattern_expr to Identifier.
    vyn::Identifier* id_ptr = dynamic_cast<vyn::Identifier*>(pattern_expr.get());
    if (!id_ptr) {
        // Release ownership if dynamic_cast fails and we are about to throw, to prevent double free if pattern_expr was moved.
        // However, we need to move from pattern_expr if cast is successful.
        // This indicates a mismatch: parse_pattern returns ExprPtr, but VariableDeclaration needs Identifier.
        // This implies global variable declarations might not support complex patterns directly in the VariableDeclaration AST node.
        // Or, VariableDeclaration needs to be more flexible (e.g. take ExprPtr for the pattern).
        // For now, assume global var must be a simple identifier.
        pattern_expr.release(); // Avoid deleting if we throw
        throw std::runtime_error("Expected a simple identifier for global variable name at " + location_to_string(loc) + ". Complex patterns not supported here.");
    }
    // If cast is successful, we need to transfer ownership of the underlying object.
    // Since pattern_expr is unique_ptr<Expression> and we need unique_ptr<Identifier>,
    // we make a new unique_ptr for the Identifier.
    std::unique_ptr<vyn::Identifier> identifier(static_cast<vyn::Identifier*>(pattern_expr.release()));


    vyn::TypeAnnotationPtr type_annotation = nullptr;
    if (this->match(vyn::TokenType::COLON)) {
        type_annotation = this->type_parser_.parse();
        if (!type_annotation) {
            throw std::runtime_error("Expected type annotation after ':' in global variable/constant declaration at " + location_to_string(this->current_location()));
        }
    }

    vyn::ExprPtr initializer = nullptr;
    if (this->match(vyn::TokenType::EQ)) { 
        initializer = this->expr_parser_.parse();
        if (!initializer) {
            throw std::runtime_error("Expected initializer expression after '=' in global variable/constant declaration at " + location_to_string(this->current_location()));
        }
    } else if (is_const_decl && !initializer) {
        // Constants usually require an initializer.
        // Depending on language rules. For now, let's not enforce it strictly here.
    }


    this->expect(vyn::TokenType::SEMICOLON);

    // vyn::VariableDeclaration constructor: loc, id, isConst, typeAnnotation, init
    return std::make_unique<vyn::VariableDeclaration>(loc, std::move(identifier), is_const_decl, std::move(type_annotation), std::move(initializer));
}

} // End namespace vyn
