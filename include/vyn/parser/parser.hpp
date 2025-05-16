#ifndef VYN_PARSER_HPP
#define VYN_PARSER_HPP

// Uncomment this line to enable verbose debugging
// #define VERBOSE

#include "vyn/parser/ast.hpp" // Moved ast.hpp include to new parser/ast.hpp path
#include "vyn/parser/token.hpp"
#include <vector>
#include <memory>
#include <stdexcept> // For std::runtime_error
#include <optional>
#include <iostream> // For debug output

namespace vyn { // Changed Vyn to vyn

    // Debug output utility macro
    #ifdef VERBOSE
        #define DEBUG_PRINT(msg) std::cerr << "[DEBUG] " << __FUNCTION__ << ": " << msg << std::endl
        #define DEBUG_TOKEN(token) std::cerr << "[TOKEN] " << vyn::token_type_to_string(token.type) \
                                            << " (" << token.lexeme << ") at " \
                                            << token.location.filePath << ":" \
                                            << token.location.line << ":" \
                                            << token.location.column << std::endl
    #else
        #define DEBUG_PRINT(msg)
        #define DEBUG_TOKEN(token)
    #endif

    // Forward declarations for parser classes within vyn namespace
    class ExpressionParser;
    class TypeParser;
    class StatementParser;
    class DeclarationParser;
    class ModuleParser;

    class BaseParser {
        friend class Parser;
        friend class ExpressionParser;
    protected:
        const std::vector<vyn::token::Token>& tokens_; 
        size_t& pos_;
        std::vector<int> indent_levels_;
        std::string current_file_path_;

        // Constructor for direct use by parsers that own their token stream (like the main Parser class)
        BaseParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, std::string file_path)
            : tokens_(tokens), pos_(pos), indent_levels_{0}, current_file_path_(std::move(file_path)) {}

        // Copy constructor (or similar) for use by parsers that delegate (like ExpressionParser from its parent)
        BaseParser(const BaseParser& other) = default; // Or implement custom copy logic if needed

        vyn::SourceLocation current_location() const; 
        void skip_comments_and_newlines();
        const vyn::token::Token& peek() const; // Changed Vyn::Token to vyn::token::Token
        const vyn::token::Token& peekNext() const; // Peek ahead by 2 tokens (skipping comments/newlines)
        const vyn::token::Token& previous_token() const; // Added
        void put_back_token(); // Added
        vyn::token::Token consume(); // Changed Vyn::Token to vyn::token::Token
        vyn::token::Token expect(vyn::TokenType type); // Changed Vyn::Token/TokenType
        std::optional<vyn::token::Token> match(vyn::TokenType type); // Changed Vyn::Token/TokenType
        std::optional<vyn::token::Token> match(const std::vector<vyn::TokenType>& types); // Changed Vyn::Token/TokenType
        bool check(vyn::TokenType type) const; // Changed Vyn::TokenType
        bool check(const std::vector<vyn::TokenType>& types) const; // Changed Vyn::TokenType
        bool IsAtEnd() const;

        void skip_indents_dedents();

        // Helper method to report errors
        [[noreturn]] std::runtime_error error(const vyn::token::Token& token, const std::string& message) const; // Removed [[noreturn]]

        // Helper methods
        bool IsDataType(const vyn::token::Token &token) const; 
        bool IsLiteral(const vyn::token::Token &token) const; 
        bool IsOperator(const vyn::token::Token &token) const; 
        bool IsUnaryOperator(const vyn::token::Token &token) const; 
    public:
        size_t get_current_pos() const;
        // Make the constructor protected so only derived classes can call it, or public if needed by Parser itself.
        // For the refactor, ExpressionParser will not directly use this constructor.
    };

    class ExpressionParser : public BaseParser { // ExpressionParser will inherit from BaseParser
    public:
        // Constructor now takes a reference to a BaseParser instance (e.g., from TypeParser or another parent parser)
        ExpressionParser(BaseParser& parent_parser);
        vyn::ExprPtr parse();

    private:
        // Reference to the parent parser's token stream and methods
        BaseParser& parent_parser_ref_;

        // Helper methods to delegate to parent_parser_ref_ for token operations
        // This avoids direct access to tokens_, pos_ etc. and uses the parent's state.
        const vyn::token::Token& peek() const { return parent_parser_ref_.peek(); }
        vyn::token::Token consume() { return parent_parser_ref_.consume(); }
        vyn::token::Token expect(vyn::TokenType type) { return parent_parser_ref_.expect(type); }
        std::optional<vyn::token::Token> match(vyn::TokenType type) { return parent_parser_ref_.match(type); }
        std::optional<vyn::token::Token> match(const std::vector<vyn::TokenType>& types) { return parent_parser_ref_.match(types); }
        bool check(vyn::TokenType type) const { return parent_parser_ref_.check(type); }
        bool IsAtEnd() const { return parent_parser_ref_.IsAtEnd(); }
        void skip_comments_and_newlines() { parent_parser_ref_.skip_comments_and_newlines(); }
        vyn::SourceLocation current_location() const { return parent_parser_ref_.current_location(); }
        const vyn::token::Token& previous_token() const { return parent_parser_ref_.previous_token(); } 
        std::runtime_error error(const vyn::token::Token& token, const std::string& message) const { 
            return parent_parser_ref_.error(token, message);
        }
        // Add any other BaseParser methods that ExpressionParser needs to call

        vyn::ExprPtr parse_expression(); 
        vyn::ExprPtr parse_assignment_expr(); 
        vyn::ExprPtr parse_range_expr(); // Add this line for range expression parsing
        vyn::ExprPtr parse_logical_or_expr(); 
        vyn::ExprPtr parse_logical_and_expr(); 
        vyn::ExprPtr parse_bitwise_or_expr(); 
        vyn::ExprPtr parse_bitwise_xor_expr(); 
        vyn::ExprPtr parse_bitwise_and_expr(); 
        vyn::ExprPtr parse_equality_expr(); 
        vyn::ExprPtr parse_relational_expr(); 
        vyn::ExprPtr parse_shift_expr(); 
        vyn::ExprPtr parse_additive_expr(); 
        vyn::ExprPtr parse_multiplicative_expr(); 
        vyn::ExprPtr parse_unary_expr(); 
        vyn::ExprPtr parse_postfix_expr();
        vyn::ExprPtr parse_primary_expr();
        vyn::ExprPtr parse_atom(); 
    };

    class TypeParser : public BaseParser {
    private:
        ExpressionParser& expr_parser_; // Added ExpressionParser reference

    public:
        // Constructor
        TypeParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, const std::string& file_path, ExpressionParser& expr_parser);
        vyn::TypeNodePtr parse(); 

    private:
        // Renamed from parse_base_type and added parse_atomic_or_group_type
        vyn::TypeNodePtr parse_base_or_ownership_wrapped_type(); 
        vyn::TypeNodePtr parse_atomic_or_group_type();
        vyn::TypeNodePtr parse_postfix_type(vyn::TypeNodePtr base_type); // Changed Vyn::AST::TypePtr to vyn::TypeNodePtr
    };

    class StatementParser : public BaseParser {
        int indent_level_;
        TypeParser& type_parser_;
        ExpressionParser& expr_parser_;
    public:
        StatementParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, int indent_level, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser); // Changed Vyn::Token
        vyn::StmtPtr parse(); // Changed Vyn::AST::StmtPtr
        std::unique_ptr<vyn::ExpressionStatement> parse_expression_statement(); // Changed Vyn::AST::ExpressionStmtNode to vyn::ExpressionStatement
        std::unique_ptr<vyn::BlockStatement> parse_block(); // Changed Vyn::AST::BlockStmtNode to vyn::BlockStatement
        vyn::ExprPtr parse_pattern(); // Changed return type to vyn::ExprPtr and removed is_binding_mutable parameter
    private:
        std::unique_ptr<vyn::IfStatement> parse_if(); // Changed Vyn::AST::IfStmtNode to vyn::IfStatement
        std::unique_ptr<vyn::WhileStatement> parse_while(); // Changed Vyn::AST::WhileStmtNode to vyn::WhileStatement
        std::unique_ptr<vyn::ForStatement> parse_for(); // Changed Vyn::AST::ForStmtNode to vyn::ForStatement
        std::unique_ptr<vyn::ReturnStatement> parse_return(); // Changed Vyn::AST::ReturnStmtNode to vyn::ReturnStatement
        std::unique_ptr<vyn::BreakStatement> parse_break(); // Changed Vyn::AST::BreakStmtNode to vyn::BreakStatement
        std::unique_ptr<vyn::ContinueStatement> parse_continue(); // Changed Vyn::AST::ContinueStmtNode to vyn::ContinueStatement
        std::unique_ptr<vyn::VariableDeclaration> parse_var_decl(); // Changed Vyn::AST::VarDeclStmtNode to vyn::VariableDeclaration
        std::unique_ptr<vyn::Node> parse_struct_pattern(); // Changed AST::PatternNode to vyn::Node
        std::unique_ptr<vyn::Node> parse_tuple_pattern(); // Changed AST::PatternNode to vyn::Node
    };

    class DeclarationParser : public BaseParser {
        TypeParser& type_parser_;
        ExpressionParser& expr_parser_;
        StatementParser& stmt_parser_;
    public:
        TypeParser& get_type_parser() { return type_parser_; }
        ExpressionParser& get_expr_parser() { return expr_parser_; }
    public:
        DeclarationParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser, StatementParser& stmt_parser);
        vyn::DeclPtr parse();
        std::unique_ptr<vyn::FunctionDeclaration> parse_function();
        std::unique_ptr<vyn::Declaration> parse_struct();
        std::unique_ptr<vyn::Declaration> parse_impl();
        std::unique_ptr<vyn::Declaration> parse_class_declaration();
        std::unique_ptr<vyn::Declaration> parse_field_declaration();
        std::unique_ptr<vyn::Declaration> parse_enum_declaration();
        std::unique_ptr<vyn::TypeAliasDeclaration> parse_type_alias_declaration();
        std::unique_ptr<vyn::VariableDeclaration> parse_global_var_declaration();
        std::vector<std::unique_ptr<vyn::GenericParamNode>> parse_generic_params();
        std::unique_ptr<vyn::Node> parse_param();
        std::unique_ptr<vyn::Declaration> parse_template_declaration();
        std::unique_ptr<vyn::ImportDeclaration> parse_import_declaration();
        std::unique_ptr<vyn::ImportDeclaration> parse_smuggle_declaration();
        
    private:
        bool IsOperator(const vyn::token::Token& token) const;

    private:
        // Changed Vyn::AST::EnumVariantNode to vyn::EnumVariantNode
        std::unique_ptr<vyn::EnumVariantNode> parse_enum_variant(); 
        // Added declaration for parse_function_parameter_struct
        vyn::FunctionParameter parse_function_parameter_struct(); 
    };

    class ModuleParser : public BaseParser {
        DeclarationParser& declaration_parser_;
    public:
        ModuleParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, const std::string& file_path, DeclarationParser& declaration_parser); // Changed Vyn::Token
        std::unique_ptr<vyn::Module> parse(); // Changed Vyn::AST::ModuleNode to vyn::Module
    };

    class Parser {
        private:
        // Order of declaration matters for initialization order if not explicitly managed
        // It's generally good practice to declare them in the order they'll be initialized.
        std::vector<vyn::token::Token> tokens_; // Store tokens if Parser owns them
        size_t current_pos_;                  // Store current_pos_ if Parser manages it directly
        std::string file_path_;               // Store file_path_ if Parser manages it directly

        BaseParser base_parser_; // This is the primary BaseParser instance

        ExpressionParser expression_parser_;
        TypeParser type_parser_;
        StatementParser statement_parser_;
        DeclarationParser declaration_parser_;
        ModuleParser module_parser_;

    public:
        Parser(const std::vector<vyn::token::Token>& tokens, std::string file_path);
        std::unique_ptr<vyn::Module> parse_module(); 

        ExpressionParser& get_expression_parser() { return expression_parser_; }
        TypeParser& get_type_parser() { return type_parser_; } 
        StatementParser& get_statement_parser() { return statement_parser_; } 
        DeclarationParser& get_declaration_parser() { return declaration_parser_; } 
        ModuleParser& get_module_parser() { return module_parser_; } 
    };

} // namespace vyn

#endif