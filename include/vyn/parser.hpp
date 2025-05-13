#ifndef VYN_PARSER_HPP
#define VYN_PARSER_HPP

#include "vyn/ast.hpp" // Moved ast.hpp include to the top
#include "vyn/token.hpp"
#include <vector>
#include <memory>
#include <stdexcept> // For std::runtime_error
#include <optional> 

namespace Vyn {

    // Forward declarations for parser classes within Vyn namespace
    class ExpressionParser;
    class TypeParser;
    class StatementParser;
    class DeclarationParser;
    class ModuleParser;

    class BaseParser {
    protected:
        const std::vector<Vyn::Token>& tokens_;
        size_t& pos_;
        std::vector<int> indent_levels_;
        std::string current_file_path_;

        BaseParser(const std::vector<Vyn::Token>& tokens, size_t& pos, std::string file_path)
            : tokens_(tokens), pos_(pos), indent_levels_{0}, current_file_path_(std::move(file_path)) {}

        Vyn::AST::SourceLocation current_location() const;
        void skip_comments_and_newlines();
        const Vyn::Token& peek() const;
        Vyn::Token consume();
        Vyn::Token expect(Vyn::TokenType type);
        std::optional<Vyn::Token> match(Vyn::TokenType type);
        std::optional<Vyn::Token> match(const std::vector<Vyn::TokenType>& types);
        bool check(Vyn::TokenType type) const;
        bool check(const std::vector<Vyn::TokenType>& types) const;
        bool IsAtEnd() const;

        void skip_indents_dedents();

        // Helper methods
        bool IsDataType(const Vyn::Token &token) const;
        bool IsLiteral(const Vyn::Token &token) const;
        bool IsOperator(const Vyn::Token &token) const;
        bool IsUnaryOperator(const Vyn::Token &token) const;
    public:
        size_t get_current_pos() const;
    };

    class ExpressionParser : public BaseParser {
    public:
        ExpressionParser(const std::vector<Vyn::Token>& tokens, size_t& pos, const std::string& file_path);
        Vyn::AST::ExprPtr parse();

    private:
        Vyn::AST::ExprPtr parse_expression();
        Vyn::AST::ExprPtr parse_assignment_expr();
        Vyn::AST::ExprPtr parse_logical_or_expr();
        Vyn::AST::ExprPtr parse_logical_and_expr();
        Vyn::AST::ExprPtr parse_bitwise_or_expr();
        Vyn::AST::ExprPtr parse_bitwise_xor_expr();
        Vyn::AST::ExprPtr parse_bitwise_and_expr();
        Vyn::AST::ExprPtr parse_equality_expr();
        Vyn::AST::ExprPtr parse_relational_expr();
        Vyn::AST::ExprPtr parse_shift_expr();
        Vyn::AST::ExprPtr parse_additive_expr();
        Vyn::AST::ExprPtr parse_multiplicative_expr();
        Vyn::AST::ExprPtr parse_unary_expr();
        Vyn::AST::ExprPtr parse_postfix_expr();
        Vyn::AST::ExprPtr parse_atom();
    };

    class TypeParser : public BaseParser {
        ExpressionParser& expr_parser_; // Reference to an ExpressionParser instance
    public:
        TypeParser(const std::vector<Vyn::Token>& tokens, size_t& pos, const std::string& file_path, ExpressionParser& expr_parser);
        Vyn::AST::TypePtr parse(); // Use TypePtr alias

    private:
        Vyn::AST::TypePtr parse_base_type(); // Use TypePtr alias
        Vyn::AST::TypePtr parse_postfix_type(Vyn::AST::TypePtr base_type); // Use TypePtr alias
    };

    class StatementParser : public BaseParser {
        int indent_level_;
        TypeParser& type_parser_;
        ExpressionParser& expr_parser_;
    public:
        StatementParser(const std::vector<Vyn::Token>& tokens, size_t& pos, int indent_level, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser);
        Vyn::AST::StmtPtr parse(); 
        std::unique_ptr<Vyn::AST::ExpressionStmtNode> parse_expression_statement(); 
        std::unique_ptr<Vyn::AST::BlockStmtNode> parse_block(); 
        Vyn::AST::PatternPtr parse_pattern(bool is_binding_mutable = false); // Moved to public
    private:
        std::unique_ptr<Vyn::AST::IfStmtNode> parse_if(); 
        std::unique_ptr<Vyn::AST::WhileStmtNode> parse_while(); 
        std::unique_ptr<Vyn::AST::ForStmtNode> parse_for(); 
        std::unique_ptr<Vyn::AST::ReturnStmtNode> parse_return(); 
        std::unique_ptr<Vyn::AST::BreakStmtNode> parse_break(); 
        std::unique_ptr<Vyn::AST::ContinueStmtNode> parse_continue(); 
        std::unique_ptr<Vyn::AST::VarDeclStmtNode> parse_var_decl(); 
        std::unique_ptr<AST::PatternNode> parse_struct_pattern();
        std::unique_ptr<AST::PatternNode> parse_tuple_pattern();
    };

    class DeclarationParser : public BaseParser {
        TypeParser& type_parser_;
        ExpressionParser& expr_parser_;
        StatementParser& stmt_parser_;
    public:
        DeclarationParser(const std::vector<Vyn::Token>& tokens, size_t& pos, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser, StatementParser& stmt_parser);
        Vyn::AST::DeclPtr parse(); 
        std::unique_ptr<Vyn::AST::FuncDeclNode> parse_function(); 
        std::unique_ptr<Vyn::AST::StructDeclNode> parse_struct(); 
        std::unique_ptr<Vyn::AST::ImplDeclNode> parse_impl(); 
        std::unique_ptr<Vyn::AST::ClassDeclNode> parse_class_declaration();
        std::unique_ptr<Vyn::AST::FieldDeclNode> parse_field_declaration();
        std::unique_ptr<Vyn::AST::EnumDeclNode> parse_enum_declaration();
        std::unique_ptr<Vyn::AST::TypeAliasDeclNode> parse_type_alias_declaration(); 
        std::unique_ptr<Vyn::AST::GlobalVarDeclNode> parse_global_var_declaration(); 
        std::vector<std::unique_ptr<Vyn::AST::GenericParamNode>> parse_generic_params();
        std::unique_ptr<Vyn::AST::ParamNode> parse_param(); // Added declaration

    private:
        std::unique_ptr<Vyn::AST::EnumVariantNode> parse_enum_variant(); 
    };

    class ModuleParser : public BaseParser {
        DeclarationParser& declaration_parser_;
    public:
        ModuleParser(const std::vector<Vyn::Token>& tokens, size_t& pos, const std::string& file_path, DeclarationParser& declaration_parser); 
        std::unique_ptr<Vyn::AST::ModuleNode> parse();
    };

    class Parser {
        std::vector<Vyn::Token> tokens_; 
        size_t pos_ = 0;
        std::string file_path_;

        ExpressionParser expression_parser_;
        TypeParser type_parser_;
        StatementParser statement_parser_; 
        DeclarationParser declaration_parser_;
        ModuleParser module_parser_;

    public:
        Parser(const std::vector<Vyn::Token>& tokens, std::string file_path); 
        std::unique_ptr<Vyn::AST::ModuleNode> parse_module();

        ExpressionParser& get_expression_parser() { return expression_parser_; }
        TypeParser& get_type_parser() { return type_parser_; } // Corrected return type
        StatementParser& get_statement_parser() { return statement_parser_; } // Corrected return type
        DeclarationParser& get_declaration_parser() { return declaration_parser_; } // Corrected return type
        ModuleParser& get_module_parser() { return module_parser_; } // Corrected return type
    };

} // namespace Vyn

#endif