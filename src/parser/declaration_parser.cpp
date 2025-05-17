#include "vyn/parser/parser.hpp"
#include "vyn/parser/ast.hpp"
#include <stdexcept> // For std::runtime_error
#include <vector>
#include <memory>
#include <string> // Required for std::to_string
#include <iostream> // Required for std::cerr


// Global helper function for converting SourceLocation to string
inline std::string location_to_string(const vyn::SourceLocation& loc) {
    return loc.filePath + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column);
}


namespace vyn {

// Constructor updated to accept TypeParser, ExpressionParser, and StatementParser references
DeclarationParser::DeclarationParser(const std::vector<token::Token>& tokens, size_t& pos, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser, StatementParser& stmt_parser)
    : BaseParser(tokens, pos, file_path), type_parser_(type_parser), expr_parser_(expr_parser), stmt_parser_(stmt_parser) {}

// Returns a declaration node.
// Expects the current token to be the start of a declaration.
// A declaration can be a function, struct, enum, impl, type alias, or variable declaration.
// It can also be a template declaration.
vyn::ast::DeclPtr DeclarationParser::parse() {
    while (this->peek().type == vyn::TokenType::COMMENT) {
        this->consume();
    }
    token::Token current_token = this->peek();
    token::Token next_token = this->peekNext();

    if (current_token.type == vyn::TokenType::KEYWORD_FN ||
        (current_token.type == vyn::TokenType::IDENTIFIER && current_token.lexeme == "async" && next_token.type == vyn::TokenType::KEYWORD_FN)) {
        return this->parse_function();
    } else if (current_token.type == vyn::TokenType::KEYWORD_STRUCT) {
        auto struct_decl = this->parse_struct();
        return struct_decl;
    } else if (current_token.type == vyn::TokenType::KEYWORD_IMPL) {
        auto impl_decl = this->parse_impl();
        return impl_decl;
    } else if (current_token.type == vyn::TokenType::IDENTIFIER && current_token.lexeme == "class") { // KEYWORD_CLASS
        auto class_decl = this->parse_class_declaration();
        return class_decl;
    } else if (current_token.type == vyn::TokenType::KEYWORD_ENUM) {
        auto enum_decl = this->parse_enum_declaration();
        return enum_decl;
    } else if (current_token.type == vyn::TokenType::IDENTIFIER && current_token.lexeme == "type") { // KEYWORD_TYPE
        return this->parse_type_alias_declaration();
    } else if (current_token.type == vyn::TokenType::KEYWORD_LET ||
               current_token.type == vyn::TokenType::KEYWORD_MUT || // Changed from KEYWORD_VAR
               current_token.type == vyn::TokenType::KEYWORD_CONST) {
        return this->parse_global_var_declaration();
    } else if (current_token.type == vyn::TokenType::IDENTIFIER && current_token.lexeme == "template") { // KEYWORD_TEMPLATE
        return this->parse_template_declaration();
    } else if (current_token.type == vyn::TokenType::IDENTIFIER && current_token.lexeme == "import") { // KEYWORD_IMPORT
        return this->parse_import_declaration();
    } else if (current_token.type == vyn::TokenType::IDENTIFIER && current_token.lexeme == "smuggle") { // KEYWORD_SMUGGLE
        return this->parse_smuggle_declaration();
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
// For now, changing return type to std::vector<std::unique_ptr<vyn::ast::GenericParamNode>> as per parser.hpp
std::vector<std::unique_ptr<vyn::ast::GenericParamNode>> DeclarationParser::parse_generic_params() {
    std::vector<std::unique_ptr<vyn::ast::GenericParamNode>> generic_params;
    if (this->match(vyn::TokenType::LT)) { // <
        do {
            SourceLocation param_loc = this->current_location();
            if (this->peek().type != vyn::TokenType::IDENTIFIER) {
                throw std::runtime_error("Expected identifier for generic parameter name at " + location_to_string(param_loc));
            }
            auto param_name = std::make_unique<ast::Identifier>(param_loc, this->consume().lexeme);

            std::vector<ast::TypeNodePtr> bounds;
            if (this->match(vyn::TokenType::COLON)) {
                do {
                    auto bound_type = this->type_parser_.parse();
                    if (!bound_type) {
                        throw std::runtime_error("Expected trait bound type after \':\' for generic parameter at " + location_to_string(this->current_location()));
                    }
                    bounds.push_back(std::move(bound_type));
                } while (this->match(vyn::TokenType::PLUS));
            }
            generic_params.push_back(std::make_unique<vyn::ast::GenericParamNode>(param_loc, std::move(param_name), std::move(bounds)));
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
std::unique_ptr<vyn::ast::Node> DeclarationParser::parse_param() {
    SourceLocation loc = this->current_location();
    // bool is_mutable = this->match(vyn::TokenType::KEYWORD_MUT).has_value(); // \'mut\' keyword for params?

    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected parameter name (identifier) at " + location_to_string(loc));
    }
    auto name_ident = std::make_unique<ast::Identifier>(this->current_location(), this->consume().lexeme);

    this->expect(vyn::TokenType::COLON);

    auto type_annot = this->type_parser_.parse(); // Returns TypeNodePtr
    if (!type_annot) {
        throw std::runtime_error("Expected type annotation for parameter \'" + name_ident->name + "\' at " + location_to_string(this->current_location()));
    }
    
    if (this->match(vyn::TokenType::EQ)) {
        auto default_value = this->expr_parser_.parse_expression();
        if (!default_value) {
            throw std::runtime_error("Expected expression for default value of parameter \\\'" + name_ident->name + "\\\' at " + location_to_string(this->current_location()));
        }
    }
    // Returning Identifier as the Node. TypeAnnotation is associated but not part of this single Node.
    // This is a simplification. A proper ParameterNode would be better.
    // For now, to satisfy NodePtr, let's return the name. The type is parsed but not directly part of this return.
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
vyn::ast::FunctionParameter DeclarationParser::parse_function_parameter_struct() {
    SourceLocation loc = this->current_location();
    // mutability for parameters?
    // bool is_mutable = this->match(vyn::TokenType::KEYWORD_MUT).has_value();

    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected parameter name (identifier) at " + location_to_string(loc));
    }
    auto name_ident = std::make_unique<ast::Identifier>(this->current_location(), this->consume().lexeme);

    this->expect(vyn::TokenType::COLON);

    auto type_annot = this->type_parser_.parse(); // Returns TypeNodePtr
    if (!type_annot) {
        throw std::runtime_error("Expected type annotation for parameter \'" + name_ident->name + "\' at " + location_to_string(this->current_location()));
    }
    
    if (this->match(vyn::TokenType::EQ)) {
        auto default_value = this->expr_parser_.parse_expression();
        if (!default_value) {
            throw std::runtime_error("Expected expression for default value of parameter \\\'" + name_ident->name + "\\\' at " + location_to_string(this->current_location()));
        }
        // default_value is parsed but not stored in FunctionParameter
    }
    return vyn::ast::FunctionParameter(std::move(name_ident), std::move(type_annot));
}


// Returns vyn::FunctionDeclaration as per parser.hpp (assuming it's a DeclPtr compatible type)
std::unique_ptr<vyn::ast::FunctionDeclaration> DeclarationParser::parse_function() {
    SourceLocation loc = this->current_location();
    bool is_async = this->match(vyn::TokenType::KEYWORD_ASYNC).has_value();
    #ifdef VERBOSE
    std::cerr << "[DECL_PARSER] Function is " << (is_async ? "async" : "sync") << std::endl;
    #endif
    
    bool is_extern = this->match(vyn::TokenType::KEYWORD_EXTERN).has_value();
    #ifdef VERBOSE
    if (is_extern) {
        std::cerr << "[DECL_PARSER] Function is extern" << std::endl;
    }
    #endif
    
    this->expect(vyn::TokenType::KEYWORD_FN);

    // Handle normal function names, \'operator+\' syntax, and keyword \'operator\' followed by operator token
    std::unique_ptr<ast::Identifier> name;
    
    if (this->peek().type == vyn::TokenType::IDENTIFIER) {
        std::string name_lexeme = this->peek().lexeme;
        SourceLocation name_loc = this->peek().location; 
        this->consume();
        
        // Check for operator keyword and operator symbol pattern
        if (name_lexeme == "operator" && this->IsOperator(this->peek())) {
            auto op_token = this->consume();
            name = std::make_unique<ast::Identifier>(name_loc, name_lexeme + op_token.lexeme);
        } else {
            name = std::make_unique<ast::Identifier>(name_loc, name_lexeme);
        }
    } else if (this->peek().type == vyn::TokenType::KEYWORD_OPERATOR) {
        SourceLocation op_loc = this->peek().location; 
        this->consume();
        
        if (!this->IsOperator(this->peek())) {
            throw std::runtime_error("Expected operator symbol after \'operator\' keyword at " + 
                                     location_to_string(this->current_location()));
        }
        auto op_token = this->consume();
        name = std::make_unique<ast::Identifier>(op_loc, "operator" + op_token.lexeme);
    } else {
        throw std::runtime_error("Expected function name at " + location_to_string(this->current_location()));
    }

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
    // std::vector<std::unique_ptr<vyn::ast::GenericParamNode>> func_generic_params = this->parse_generic_params();


    this->expect(vyn::TokenType::LPAREN);
    std::vector<vyn::ast::FunctionParameter> params_structs;
    if (this->peek().type != vyn::TokenType::RPAREN) {
        do {
            params_structs.push_back(this->parse_function_parameter_struct());
        } while (this->match(vyn::TokenType::COMMA));
    }
    this->expect(vyn::TokenType::RPAREN);

    // Variables to hold type information
    ast::TypeNodePtr return_type_node = nullptr;
    ast::TypeNodePtr throws_type = nullptr;

    // Check for return type arrow first
    if (this->match(vyn::TokenType::ARROW)) {
        return_type_node = this->type_parser_.parse();
        if (!return_type_node) {
            throw std::runtime_error("Expected return type after \'->\' at " + location_to_string(this->current_location()));
        }
    }

    // Support for \'throws\' keyword after the return type
    if (this->peek().type == vyn::TokenType::IDENTIFIER && this->peek().lexeme == "throws") { // KEYWORD_THROWS if it exists, else IDENTIFIER
        this->consume();
        
        // Parse the error type that can be thrown
        if (this->peek().type == vyn::TokenType::IDENTIFIER) {
            auto error_type_name = std::make_unique<ast::Identifier>(this->current_location(), this->consume().lexeme);
            throws_type = ast::TypeNode::newIdentifier(this->current_location(), std::move(error_type_name), {}, false, false);
        } else {
            throw std::runtime_error("Expected error type after \'throws\' at " + location_to_string(this->current_location()));
        }
    }

    std::unique_ptr<ast::BlockStatement> body = nullptr;
    // Accept function declarations without a body (forward declarations)
    if (this->peek().type == vyn::TokenType::IDENTIFIER) {
        // Handle constructor return expressions like: Node { is_leaf: is_leaf_param }
        ast::Identifier* id_node = nullptr;
        if (return_type_node && return_type_node->category == ast::TypeNode::Category::IDENTIFIER) { // Changed TypeCategory to Category
            // Extract identifier name from return type
            const auto& id_name = return_type_node->name->name;
            if (this->peek().lexeme == id_name) {
            SourceLocation expr_loc = this->current_location();
            ast::ExprPtr init_expr = this->expr_parser_.parse_expression();
            if (init_expr) {
                // Create a block statement with a single expression statement for the constructor style return
                std::vector<ast::StmtPtr> statements;
                statements.push_back(std::make_unique<ast::ExpressionStatement>(expr_loc, std::move(init_expr)));
                body = std::make_unique<ast::BlockStatement>(expr_loc, std::move(statements));
            }
        }
        }
    } else if (this->peek().type == vyn::TokenType::LBRACE) {
        vyn::StatementParser stmt_parser(this->tokens_, this->pos_, 0, this->current_file_path_, this->type_parser_, this->expr_parser_);
        body = stmt_parser.parse_block();
        this->pos_ = stmt_parser.get_current_pos();
    } else {
        // Forward declaration or no body (e.g. for extern functions).
        // 'body' remains nullptr, which is correct for these cases.
    }

    return std::make_unique<vyn::ast::FunctionDeclaration>(loc, std::move(name), std::move(params_structs), std::move(body), is_async, std::move(return_type_node));
}

// StructDeclNode not in ast.hpp. Assuming a Declaration type for it.
// parser.hpp: std::unique_ptr<vyn::Declaration> parse_struct();
// Similar to struct, this needs a vyn::StructDeclaration in ast.hpp.
std::unique_ptr<vyn::ast::Declaration> DeclarationParser::parse_struct() {
    SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_STRUCT);

    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected struct name at " + location_to_string(this->current_location()));
    }
    auto name = std::make_unique<ast::Identifier>(this->current_location(), this->consume().lexeme);

    auto generic_params = this->parse_generic_params();

    this->expect(vyn::TokenType::LBRACE);
    
    std::vector<std::unique_ptr<ast::FieldDeclaration>> fields;

    while (this->peek().type != vyn::TokenType::RBRACE && this->peek().type != vyn::TokenType::END_OF_FILE) {
        SourceLocation field_loc = this->current_location();
        
        if (this->peek().type != vyn::TokenType::IDENTIFIER) {
            throw std::runtime_error("Expected field name in struct \'" + name->name + "\' at " + location_to_string(this->current_location()));
        }
        auto field_name = std::make_unique<ast::Identifier>(this->current_location(), this->consume().lexeme);
        
        this->expect(vyn::TokenType::COLON);
        
        auto field_type_node = this->type_parser_.parse();
        if (!field_type_node) {
            throw std::runtime_error("Expected type for field \'" + field_name->name + "\' in struct \'" + name->name + "\' at " + location_to_string(this->current_location()));
        }
        
        fields.push_back(std::make_unique<ast::FieldDeclaration>(field_loc, std::move(field_name), std::move(field_type_node), nullptr, false));

        this->skip_comments_and_newlines(); 
        if (this->match(vyn::TokenType::COMMA)) {
             this->skip_comments_and_newlines();
             if (this->peek().type == vyn::TokenType::RBRACE) break;
        } else if (this->peek().type != vyn::TokenType::RBRACE) {
            throw std::runtime_error("Expected comma or closing brace after struct field in \'" + name->name + "\' at " + location_to_string(this->current_location()));
        }
    }
    this->expect(vyn::TokenType::RBRACE);

    return std::make_unique<ast::StructDeclaration>(loc, std::move(name), std::move(generic_params), std::move(fields));
}


// ImplDeclNode not in ast.hpp. Assuming a Declaration type for it.
// parser.hpp: std::unique_ptr<vyn::Declaration> parse_impl();
// Similar to struct, this needs a vyn::ImplDeclaration in ast.hpp.
std::unique_ptr<vyn::ast::Declaration> DeclarationParser::parse_impl() {
    SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_IMPL);

    auto generic_params = this->parse_generic_params();

    ast::TypeNodePtr trait_type_node = nullptr;
    ast::TypeNodePtr self_type_node = this->type_parser_.parse();
    if (!self_type_node) {
        throw std::runtime_error("Expected type name in impl block at " + location_to_string(this->current_location()));
    }

    if (this->match(vyn::TokenType::KEYWORD_FOR)) {
        trait_type_node = std::move(self_type_node);
        self_type_node = this->type_parser_.parse();
        if (!self_type_node) {
            throw std::runtime_error("Expected type name after \'for\' in impl block at " + location_to_string(this->current_location()));
        }
    }

    this->expect(vyn::TokenType::LBRACE);
    std::vector<std::unique_ptr<ast::FunctionDeclaration>> methods;
    while (!this->check(vyn::TokenType::RBRACE) && !this->IsAtEnd()) {
        methods.push_back(this->parse_function()); 
    }
    this->expect(vyn::TokenType::RBRACE);

    return std::make_unique<ast::ImplDeclaration>(loc, std::move(self_type_node), std::move(methods), nullptr, std::move(generic_params), std::move(trait_type_node));
}


std::unique_ptr<vyn::ast::Declaration> DeclarationParser::parse_enum_declaration() {
    SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_ENUM);

    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected enum name (identifier) at " + location_to_string(this->current_location()));
    }
    auto name = std::make_unique<ast::Identifier>(this->current_location(), this->consume().lexeme);

    auto generic_params = this->parse_generic_params();

    this->expect(vyn::TokenType::LBRACE);
    this->skip_comments_and_newlines();

    std::vector<std::unique_ptr<ast::EnumVariantNode>> variants;

    while (this->peek().type != vyn::TokenType::RBRACE && this->peek().type != vyn::TokenType::END_OF_FILE) {
        auto variant_node = this->parse_enum_variant();
        if (!variant_node) { 
            throw std::runtime_error("Failed to parse enum variant for enum \'" + name->name + "\' at " + location_to_string(this->current_location()));
        }
        variants.push_back(std::move(variant_node));

        this->skip_comments_and_newlines();
        if (this->match(vyn::TokenType::COMMA)) {
            this->skip_comments_and_newlines();
            if (this->peek().type == vyn::TokenType::RBRACE) {
                break;
            }
        } else if (this->peek().type != vyn::TokenType::RBRACE) {
             if (this->peek().type != vyn::TokenType::IDENTIFIER && this->peek().type != vyn::TokenType::RBRACE) {
                  throw std::runtime_error("Expected comma, closing brace, or next variant identifier after enum variant in enum \'" + name->name + "\' at " + location_to_string(this->current_location()));
             }
        }
    }

    this->expect(vyn::TokenType::RBRACE);

    return std::make_unique<ast::EnumDeclaration>(loc, std::move(name), std::move(generic_params), std::move(variants));
}


// Returns vyn::TypeAliasDeclaration as per parser.hpp (DeclPtr compatible)
std::unique_ptr<vyn::ast::TypeAliasDeclaration> DeclarationParser::parse_type_alias_declaration() {
    SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_TYPE);

    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected type alias name (identifier) at " + location_to_string(this->current_location()));
    }
    auto name = std::make_unique<ast::Identifier>(this->current_location(), this->consume().lexeme);

    auto generic_params = this->parse_generic_params();

    this->expect(vyn::TokenType::EQ);

    auto aliased_type_node = this->type_parser_.parse();
    if (!aliased_type_node) {
        throw std::runtime_error("Expected type definition after \'=\' for type alias \'" + name->name + "\' at " + location_to_string(this->current_location()));
    }

    this->expect(vyn::TokenType::SEMICOLON);
    return std::make_unique<vyn::ast::TypeAliasDeclaration>(loc, std::move(name), std::move(aliased_type_node));
}

// GlobalVarDeclNode not in ast.hpp. VariableDeclaration is the one.
// parser.hpp: std::unique_ptr<vyn::VariableDeclaration> parse_global_var_declaration();
std::unique_ptr<vyn::ast::VariableDeclaration> DeclarationParser::parse_global_var_declaration() {
    SourceLocation loc = this->current_location();
    bool is_const_decl = false;

    if (this->match(vyn::TokenType::KEYWORD_MUT)) { // Changed from KEYWORD_VAR
        is_const_decl = false;
    } else if (this->match(vyn::TokenType::KEYWORD_CONST)) {
        is_const_decl = true;
    } else {
        this->expect(vyn::TokenType::KEYWORD_LET);
        is_const_decl = true;
    }

    // The pattern itself. parse_pattern now returns ExprPtr and takes no bool.
    // The 'is_const_decl' applies to the VariableDeclaration, not the pattern parsing itself.
    ast::ExprPtr pattern_expr = this->stmt_parser_.parse_pattern(); 
    if (!pattern_expr) {
        throw std::runtime_error("Expected pattern in global variable/constant declaration at " + location_to_string(loc));
    }

    // A VariableDeclaration expects a single Identifier for its 'id' field.
    // If parse_pattern returns a complex pattern, it cannot directly be used.
    // This was a known issue from statement_parser.cpp.
    // For global variables, the pattern is usually just an identifier.
    // Check if the expression is an Identifier by checking its NodeType
    auto* expr_ptr = pattern_expr.get();
    if (!expr_ptr || expr_ptr->getType() != vyn::ast::NodeType::IDENTIFIER) {
        throw std::runtime_error("Expected a simple identifier for global variable name at " + location_to_string(loc) + ". Complex patterns not supported here.");
    }
    // If pattern is an Identifier, we need to transfer ownership of the underlying object.
    // Since pattern_expr is unique_ptr<Expression> and we need unique_ptr<Identifier>,
    // we make a new unique_ptr for the Identifier.
    std::unique_ptr<ast::Identifier> identifier(static_cast<ast::Identifier*>(pattern_expr.release()));


    ast::TypeNodePtr type_node = nullptr;
    if (this->match(vyn::TokenType::COLON)) {
        type_node = this->type_parser_.parse();
        if (!type_node) {
            throw std::runtime_error("Expected type annotation after \':\' in global variable/constant declaration at " + location_to_string(this->current_location()));
        }
    }

    ast::ExprPtr initializer = nullptr;
    if (this->match(vyn::TokenType::EQ)) {
        initializer = this->expr_parser_.parse_expression();
        if (!initializer) {
            throw std::runtime_error("Expected initializer expression after \\\'=\\\' in global variable/constant declaration at " + location_to_string(this->current_location()));
        }
    } else if (is_const_decl && !initializer) {
        // Constants usually require an initializer.
        // Depending on language rules. For now, let\'s not enforce it strictly here.
    }


    this->expect(vyn::TokenType::SEMICOLON);
    return std::make_unique<vyn::ast::VariableDeclaration>(loc, std::move(identifier), is_const_decl, std::move(type_node), std::move(initializer));
}

// New: Parse Template Declaration
std::unique_ptr<vyn::ast::Declaration> DeclarationParser::parse_template_declaration() {
    vyn::SourceLocation loc = peek().location; 
    expect(vyn::TokenType::KEYWORD_TEMPLATE);

    if (peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Error at " + location_to_string(loc) + ": Expected an identifier after \'template\' keyword.");
    }
    std::unique_ptr<ast::Identifier> name = std::make_unique<ast::Identifier>(peek().location, peek().lexeme); 
    consume(); 

    std::vector<std::unique_ptr<ast::GenericParamNode>> generic_params;
    if (peek().type == vyn::TokenType::LT) {
        generic_params = this->parse_generic_params();
    }

    expect(vyn::TokenType::LBRACE);
    skip_comments_and_newlines();
    
    std::unique_ptr<ast::Declaration> declaration = nullptr;
    
    if (peek().type == vyn::TokenType::KEYWORD_CLASS) {
        declaration = parse_class_declaration();
    } else if (peek().type == vyn::TokenType::KEYWORD_STRUCT) {
        declaration = parse_struct();
    } else if (peek().type == vyn::TokenType::KEYWORD_ENUM) {
        declaration = parse_enum_declaration();
    } else if (peek().type == vyn::TokenType::KEYWORD_FN) {
        declaration = parse_function();
    } else {
        declaration = this->parse();
    }

    if (!declaration) {
        vyn::SourceLocation err_loc = current_location(); 
        throw std::runtime_error("Error at " + location_to_string(err_loc) +
                                 ": Expected a declaration inside the template body.");
    }

    skip_comments_and_newlines();
    
    expect(vyn::TokenType::RBRACE);

    return std::make_unique<ast::TemplateDeclarationNode>(loc, std::move(name), std::move(generic_params), std::move(declaration));
}

// --- Import and Smuggle Declarations ---
std::unique_ptr<vyn::ast::ImportDeclaration> DeclarationParser::parse_import_declaration() {
    vyn::SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_IMPORT);
    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected identifier after \'import\' at " + location_to_string(this->current_location()));
    }
    std::string path = this->consume().lexeme;
    while (this->peek().type == vyn::TokenType::COLONCOLON || this->peek().type == vyn::TokenType::DOT) {
        this->consume();
        if (this->peek().type != vyn::TokenType::IDENTIFIER) {
            throw std::runtime_error("Expected identifier in import path at " + location_to_string(this->current_location()));
        }
        path += "::" + this->consume().lexeme;
    }
    std::unique_ptr<ast::Identifier> alias = nullptr;
    if (this->match(vyn::TokenType::KEYWORD_AS)) {
        if (this->peek().type != vyn::TokenType::IDENTIFIER) {
            throw std::runtime_error("Expected identifier after \'as\' in import at " + location_to_string(this->current_location()));
        }
        alias = std::make_unique<ast::Identifier>(this->current_location(), this->consume().lexeme);
    }
    this->match(vyn::TokenType::SEMICOLON);
    auto source = std::make_unique<ast::StringLiteral>(loc, path);
    std::vector<ast::ImportSpecifier> specifiers;
    if (alias) {
        specifiers.emplace_back(nullptr, std::move(alias));
    }
    return std::make_unique<vyn::ast::ImportDeclaration>(loc, std::move(source), std::move(specifiers));
}

std::unique_ptr<vyn::ast::ImportDeclaration> DeclarationParser::parse_smuggle_declaration() {
    vyn::SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_SMUGGLE);
    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected identifier after \'smuggle\' at " + location_to_string(this->current_location()));
    }
    std::string path = this->consume().lexeme;
    while (this->peek().type == vyn::TokenType::COLONCOLON || this->peek().type == vyn::TokenType::DOT) {
        this->consume();
        if (this->peek().type != vyn::TokenType::IDENTIFIER) {
            throw std::runtime_error("Expected identifier in smuggle path at " + location_to_string(this->current_location()));
        }
        path += "::" + this->consume().lexeme;
    }
    std::unique_ptr<ast::Identifier> alias = nullptr;
    if (this->match(vyn::TokenType::KEYWORD_AS)) {
        if (this->peek().type != vyn::TokenType::IDENTIFIER) {
            throw std::runtime_error("Expected identifier after \'as\' in smuggle at " + location_to_string(this->current_location()));
        }
        alias = std::make_unique<ast::Identifier>(this->current_location(), this->consume().lexeme);
    }
    this->match(vyn::TokenType::SEMICOLON);
    auto source = std::make_unique<ast::StringLiteral>(loc, path);
    std::vector<ast::ImportSpecifier> specifiers;
    if (alias) {
        specifiers.emplace_back(nullptr, std::move(alias));
    }
    return std::make_unique<vyn::ast::ImportDeclaration>(loc, std::move(source), std::move(specifiers));
}

std::unique_ptr<vyn::ast::Declaration> DeclarationParser::parse_class_declaration() {
    vyn::SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_CLASS);

    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected class name at " + location_to_string(this->current_location()));
    }
    auto class_name = std::make_unique<ast::Identifier>(this->current_location(), this->consume().lexeme);

    auto generic_params = this->parse_generic_params();

    this->expect(vyn::TokenType::LBRACE);
    
    std::vector<ast::DeclPtr> members;

    while (this->peek().type != vyn::TokenType::RBRACE && this->peek().type != vyn::TokenType::END_OF_FILE) {
        this->skip_comments_and_newlines();
        
        // Parse fields and methods
        if (this->peek().type == vyn::TokenType::KEYWORD_MUT || // Changed from KEYWORD_VAR
            this->peek().type == vyn::TokenType::KEYWORD_CONST ||
            this->peek().type == vyn::TokenType::KEYWORD_LET ||
            this->peek().type == vyn::TokenType::IDENTIFIER) {
            
            // Handle variable field declaration with var/const/let keywords
            bool is_mutable = false;
            if (this->peek().type == vyn::TokenType::KEYWORD_MUT) { // Changed from KEYWORD_VAR
                is_mutable = true;
                this->consume(); // consume \'var\'
            } else if (this->peek().type == vyn::TokenType::KEYWORD_CONST || this->peek().type == vyn::TokenType::KEYWORD_LET) { // Corrected TokenType, KEYWORD_CONST to CONST, KEYWORD_LET to LET
                is_mutable = false;
                this->consume(); // consume \'const\' or \'let\'
            }
            
            // This is a field declaration
            SourceLocation field_loc = this->current_location();
            
            if (this->peek().type != vyn::TokenType::IDENTIFIER) {
                throw std::runtime_error("Expected field name in class \'" + class_name->name + "\' at " + location_to_string(this->current_location()));
            }
            auto field_name = std::make_unique<ast::Identifier>(field_loc, this->consume().lexeme);
            
            this->expect(vyn::TokenType::COLON);
            
            auto field_type = this->type_parser_.parse();
            if (!field_type) {
                throw std::runtime_error("Expected type for field \'" + field_name->name + "\' in class \'" + class_name->name + "\' at " + location_to_string(this->current_location()));
            }
            
            ast::ExprPtr initializer = nullptr;
            if (this->match(vyn::TokenType::EQ)) {
                initializer = this->expr_parser_.parse_expression();
                if (!initializer) {
                    throw std::runtime_error("Expected initializer for field \\\'" + field_name->name + "\\\' in class \\\'" + class_name->name + "\\\' at " + location_to_string(this->current_location()));
                }
            }
            
            members.push_back(std::make_unique<ast::FieldDeclaration>(field_loc, std::move(field_name), std::move(field_type), std::move(initializer), is_mutable));
            
            // Optional comma or semicolon after field declaration - not required
            this->match(vyn::TokenType::COMMA);
            this->match(vyn::TokenType::SEMICOLON);
        }
        else if (this->peek().type == vyn::TokenType::KEYWORD_FN) {
             members.push_back(this->parse_function());
        }
        else if (this->peek().type == vyn::TokenType::KEYWORD_OPERATOR ) {
             members.push_back(this->parse_function());
        }
        else {
            // If it's not a recognized member, skip the token and report an error or break.
            // For now, let's assume it's an error to find an unrecognized token here.
            throw std::runtime_error("Unexpected token in class body: " + 
                                     vyn::token_type_to_string(this->peek().type) + 
                                     " (\'" + this->peek().lexeme + "\') at " + 
                                     location_to_string(this->current_location()));
        }
    }

    this->expect(vyn::TokenType::RBRACE);
    return std::make_unique<ast::ClassDeclaration>(loc, std::move(class_name), std::move(generic_params), std::move(members));
}


bool DeclarationParser::IsOperator(const vyn::token::Token& token) const {
    return token.type == vyn::TokenType::PLUS ||
           token.type == vyn::TokenType::MINUS ||
           token.type == vyn::TokenType::MULTIPLY ||
           token.type == vyn::TokenType::DIVIDE ||
           token.type == vyn::TokenType::MODULO ||
           token.type == vyn::TokenType::EQEQ ||
           token.type == vyn::TokenType::NOTEQ ||
           token.type == vyn::TokenType::LT ||
           token.type == vyn::TokenType::GT ||
           token.type == vyn::TokenType::LTEQ ||
           token.type == vyn::TokenType::GTEQ ||
           token.type == vyn::TokenType::AND ||
           token.type == vyn::TokenType::OR ||
           token.type == vyn::TokenType::AMPERSAND ||
           token.type == vyn::TokenType::PIPE ||
           token.type == vyn::TokenType::CARET ||
           token.type == vyn::TokenType::LSHIFT ||
           token.type == vyn::TokenType::RSHIFT ||
           token.type == vyn::TokenType::TILDE ||
           token.type == vyn::TokenType::LBRACKET; // For indexing operator []
}

std::unique_ptr<ast::EnumVariantNode> DeclarationParser::parse_enum_variant() {
    SourceLocation loc = this->current_location();
    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected enum variant name (identifier) at " + location_to_string(loc));
    }
    auto name = std::make_unique<ast::Identifier>(this->current_location(), this->consume().lexeme);
    
    std::vector<ast::TypeNodePtr> associated_types;
    if (this->match(vyn::TokenType::LPAREN)) {
        if (this->peek().type != vyn::TokenType::RPAREN) {
            do {
                auto type_node = this->type_parser_.parse();
                if (!type_node) {
                    throw std::runtime_error("Expected type for enum variant parameter at " + location_to_string(this->current_location()));
                }
                associated_types.push_back(std::move(type_node));
            } while (this->match(vyn::TokenType::COMMA));
        }
        this->expect(vyn::TokenType::RPAREN);
    }
    return std::make_unique<ast::EnumVariantNode>(loc, std::move(name), std::move(associated_types));
}

} // namespace vyn
