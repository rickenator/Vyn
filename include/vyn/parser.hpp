#ifndef VYN_PARSER_HPP
#define VYN_PARSER_HPP

#include "vyn/ast.hpp" // Moved ast.hpp include to the top
#include "vyn/token.hpp"
#include <vector>
#include <memory>
#include <stdexcept> // For std::runtime_error
#include <optional> 

namespace vyn { // Changed Vyn to vyn

    // Forward declarations for parser classes within vyn namespace
    class ExpressionParser;
    class TypeParser;
    class StatementParser;
    class DeclarationParser;
    class ModuleParser;

    class BaseParser {
    protected:
        const std::vector<vyn::token::Token>& tokens_; // Changed Vyn::Token to vyn::token::Token
        size_t& pos_;
        std::vector<int> indent_levels_;
        std::string current_file_path_;

        BaseParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, std::string file_path) // Changed Vyn::Token to vyn::token::Token
            : tokens_(tokens), pos_(pos), indent_levels_{0}, current_file_path_(std::move(file_path)) {}

        vyn::SourceLocation current_location() const; // Changed Vyn::AST::SourceLocation to vyn::SourceLocation
        void skip_comments_and_newlines();
        const vyn::token::Token& peek() const; // Changed Vyn::Token to vyn::token::Token
        vyn::token::Token consume(); // Changed Vyn::Token to vyn::token::Token
        vyn::token::Token expect(vyn::TokenType type); // Changed Vyn::Token/TokenType
        std::optional<vyn::token::Token> match(vyn::TokenType type); // Changed Vyn::Token/TokenType
        std::optional<vyn::token::Token> match(const std::vector<vyn::TokenType>& types); // Changed Vyn::Token/TokenType
        bool check(vyn::TokenType type) const; // Changed Vyn::TokenType
        bool check(const std::vector<vyn::TokenType>& types) const; // Changed Vyn::TokenType
        bool IsAtEnd() const;

        void skip_indents_dedents();

        // Helper methods
        bool IsDataType(const vyn::token::Token &token) const; // Changed Vyn::Token
        bool IsLiteral(const vyn::token::Token &token) const; // Changed Vyn::Token
        bool IsOperator(const vyn::token::Token &token) const; // Changed Vyn::Token
        bool IsUnaryOperator(const vyn::token::Token &token) const; // Changed Vyn::Token
    public:
        size_t get_current_pos() const;
    };

    class ExpressionParser : public BaseParser {
    public:
        ExpressionParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, const std::string& file_path); // Changed Vyn::Token
        vyn::ExprPtr parse(); // Changed Vyn::AST::ExprPtr to vyn::ExprPtr

    private:
        vyn::ExprPtr parse_expression(); // Changed Vyn::AST::ExprPtr
        vyn::ExprPtr parse_assignment_expr(); // Changed Vyn::AST::ExprPtr
        vyn::ExprPtr parse_logical_or_expr(); // Changed Vyn::AST::ExprPtr
        vyn::ExprPtr parse_logical_and_expr(); // Changed Vyn::AST::ExprPtr
        vyn::ExprPtr parse_bitwise_or_expr(); // Changed Vyn::AST::ExprPtr
        vyn::ExprPtr parse_bitwise_xor_expr(); // Changed Vyn::AST::ExprPtr
        vyn::ExprPtr parse_bitwise_and_expr(); // Changed Vyn::AST::ExprPtr
        vyn::ExprPtr parse_equality_expr(); // Changed Vyn::AST::ExprPtr
        vyn::ExprPtr parse_relational_expr(); // Changed Vyn::AST::ExprPtr
        vyn::ExprPtr parse_shift_expr(); // Changed Vyn::AST::ExprPtr
        vyn::ExprPtr parse_additive_expr(); // Changed Vyn::AST::ExprPtr
        vyn::ExprPtr parse_multiplicative_expr(); // Changed Vyn::AST::ExprPtr
        vyn::ExprPtr parse_unary_expr(); // Changed Vyn::AST::ExprPtr
        vyn::ExprPtr parse_postfix_expr(); // Changed Vyn::AST::ExprPtr
        vyn::ExprPtr parse_atom(); // Changed Vyn::AST::ExprPtr
    };

    class TypeParser : public BaseParser {
        ExpressionParser& expr_parser_; 
    public:
        TypeParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, const std::string& file_path, ExpressionParser& expr_parser); // Changed Vyn::Token
        vyn::TypeNodePtr parse(); // Changed Vyn::AST::TypePtr to vyn::TypeNodePtr

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
        DeclarationParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser, StatementParser& stmt_parser); // Changed Vyn::Token
        vyn::DeclPtr parse(); // Changed Vyn::AST::DeclPtr
        std::unique_ptr<vyn::FunctionDeclaration> parse_function(); // Changed Vyn::AST::FuncDeclNode to vyn::FunctionDeclaration
        std::unique_ptr<vyn::Declaration> parse_struct(); // Changed Vyn::AST::StructDeclNode to vyn::Declaration (StructDeclNode not in ast.hpp)
        std::unique_ptr<vyn::Declaration> parse_impl(); // Changed Vyn::AST::ImplDeclNode to vyn::Declaration (ImplDeclNode not in ast.hpp)
        std::unique_ptr<vyn::Declaration> parse_class_declaration(); // Changed Vyn::AST::ClassDeclNode to vyn::Declaration
        std::unique_ptr<vyn::Declaration> parse_field_declaration(); // Changed Vyn::AST::FieldDeclNode to vyn::Declaration
        std::unique_ptr<vyn::Declaration> parse_enum_declaration(); // Changed Vyn::AST::EnumDeclNode to vyn::Declaration
        std::unique_ptr<vyn::TypeAliasDeclaration> parse_type_alias_declaration(); // Changed Vyn::AST::TypeAliasDeclNode to vyn::TypeAliasDeclaration
        std::unique_ptr<vyn::VariableDeclaration> parse_global_var_declaration(); // Changed Vyn::AST::GlobalVarDeclNode to vyn::VariableDeclaration
        std::vector<std::unique_ptr<vyn::GenericParamNode>> parse_generic_params(); // Changed to GenericParamNode
        std::unique_ptr<vyn::Node> parse_param(); // Changed Vyn::AST::ParamNode to vyn::Node 

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
        std::vector<vyn::token::Token> tokens_; // Changed Vyn::Token
        size_t pos_ = 0;
        std::string file_path_;

        ExpressionParser expression_parser_;
        TypeParser type_parser_;
        StatementParser statement_parser_; 
        DeclarationParser declaration_parser_;
        ModuleParser module_parser_;

    public:
        Parser(const std::vector<vyn::token::Token>& tokens, std::string file_path); // Changed Vyn::Token
        std::unique_ptr<vyn::Module> parse_module(); // Changed Vyn::AST::ModuleNode to vyn::Module

        ExpressionParser& get_expression_parser() { return expression_parser_; }
        TypeParser& get_type_parser() { return type_parser_; } 
        StatementParser& get_statement_parser() { return statement_parser_; } 
        DeclarationParser& get_declaration_parser() { return declaration_parser_; } 
        ModuleParser& get_module_parser() { return module_parser_; } 
    };

} // namespace vyn

#endif