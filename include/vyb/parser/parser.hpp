#ifndef VYB_PARSER_PARSER_HPP
#define VYB_PARSER_PARSER_HPP

// Uncomment this line to enable verbose debugging
// #define VERBOSE

#include "ast.hpp"
#include "token.hpp"
#include <vector>
#include <memory>
#include <stdexcept> // For std::runtime_error
#include <optional>
#include <iostream> // For debug output
#include <functional> // Added for std::function
#include <set> // For parser verbose specifiers
#include <catch2/interfaces/catch_interfaces_capture.hpp> // For accessing current test name

namespace vyb { // Changed Vyb to vyb

    // Forward declaration for SourceLocation if not already included via ast.hpp indirectly
    // struct SourceLocation; // Assuming SourceLocation is in the vyb namespace

    // Helper function for converting SourceLocation to string
    // Moved from declaration_parser.cpp to be available more broadly
    inline std::string location_to_string(const vyb::SourceLocation& loc) {
        // Ensure <string> and <iostream> or similar for std::to_string are included
        // by files that include parser.hpp, or include them here.
        // For now, assuming ast.hpp or token.hpp (included above) bring in <string>.
        // If not, #include <string> might be needed here.
        return loc.filePath + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column);
    }

    // Runtime parser debug control globals (defined in main.cpp)
    extern std::set<std::string> g_verbose_parser_test_specifiers;
    extern bool g_make_all_parser_verbose;
    extern bool g_suppress_all_parser_debug_output;

    // Helper to determine if current test should show parser verbose output
    inline bool should_current_test_be_parser_verbose() {
        if (g_suppress_all_parser_debug_output) return false;
        if (g_make_all_parser_verbose) return true;
        if (g_verbose_parser_test_specifiers.empty()) return false;
        std::string current_test_name = Catch::getResultCapture().getCurrentTestName();
        if (!current_test_name.empty()) {
            if (g_verbose_parser_test_specifiers.count(current_test_name)) return true;
            for (const auto& spec : g_verbose_parser_test_specifiers) {
                if (current_test_name.find(spec) != std::string::npos) return true;
            }
        }
        return false;
    }

    // Debug output utility macros for parser
    #undef DEBUG_PRINT
    #undef DEBUG_TOKEN
    #define DEBUG_PRINT(msg) do { if (should_current_test_be_parser_verbose()) std::cerr << "[PDEBUG] " << __FUNCTION__ << ": " << msg << std::endl; } while(0)
    #define DEBUG_TOKEN(token) do { if (should_current_test_be_parser_verbose()) std::cerr << "[PTOKEN] " << vyb::token_type_to_string(token.type) \
                                                << " (" << token.lexeme << ") at " \
                                                << token.location.filePath << ":" \
                                                << token.location.line << ":" \
                                                << token.location.column << std::endl; } while(0)

    // Forward declarations for parser classes within vyb namespace
    class ExpressionParser;
    class TypeParser;
    class StatementParser;
    class DeclarationParser;
    class ModuleParser;

    class BaseParser {
        friend class Parser;
        friend class ExpressionParser;
    protected:
        const std::vector<vyb::token::Token>& tokens_;
        size_t& pos_;
        std::vector<int> indent_levels_;
        std::string current_file_path_;

        // Constructor for direct use by parsers that own their token stream (like the main Parser class)
        BaseParser(const std::vector<vyb::token::Token>& tokens, size_t& pos, std::string file_path)
            : tokens_(tokens), pos_(pos), indent_levels_{0}, current_file_path_(std::move(file_path)) {}

        // Copy constructor (or similar) for use by parsers that delegate (like ExpressionParser from its parent)
        BaseParser(const BaseParser& other) = default; // Or implement custom copy logic if needed

        vyb::SourceLocation current_location() const;
        void skip_comments_and_newlines();
        const vyb::token::Token& peek() const;
        const vyb::token::Token& peekNext() const;
        const vyb::token::Token& previous_token() const;
        void put_back_token();
        vyb::token::Token consume();
        vyb::token::Token expect(vyb::TokenType type);
        vyb::token::Token expect(vyb::TokenType type, const std::string& lexeme); // Checks type AND lexeme
        vyb::token::Token expect(vyb::TokenType type, const char* customErrorMessage); // Corrected: checks type, uses custom message on failure
        std::optional<vyb::token::Token> match(vyb::TokenType type);
        std::optional<vyb::token::Token> match(vyb::TokenType type, const std::string& lexeme);
        std::optional<vyb::token::Token> match(const std::vector<vyb::TokenType>& types);
        bool check(vyb::TokenType type) const;
        bool check(const std::vector<vyb::TokenType>& types) const;
        bool IsAtEnd() const;

        void skip_indents_dedents();

        // Helper method to report errors
        [[noreturn]] std::runtime_error error(const vyb::token::Token& token, const std::string& message) const;

        // Helper methods
        bool IsDataType(const vyb::token::Token &token) const;
        bool IsLiteral(const vyb::token::Token &token) const;
        bool IsOperator(const vyb::token::Token &token) const;
        bool IsUnaryOperator(const vyb::token::Token &token) const;
    public:
        size_t get_current_pos() const;
    };

    class ExpressionParser : public BaseParser {
        StatementParser* stmt_parser_ = nullptr; // For parsing blocks in select expressions
    public:
        ExpressionParser(const std::vector<token::Token>& tokens, size_t& pos, const std::string& file_path);
        void set_statement_parser(StatementParser* sp) { stmt_parser_ = sp; }
        vyb::ast::ExprPtr parse_expression(); // Removed override
        vyb::ast::ExprPtr parse_primary(); // For match patterns - parses literals, identifiers without binary ops
        bool is_expression_start(vyb::TokenType type) const; // Added declaration

        // Helpers for trap and ensure clauses
        std::unique_ptr<vyb::ast::TrapClause> parse_trap_clause();
        std::unique_ptr<vyb::ast::EnsureClause> parse_ensure_clause();

    private:
        // Add declarations for all private helper methods used in expression_parser.cpp
        vyb::ast::ExprPtr parse_assignment_expr();
        vyb::ast::ExprPtr parse_logical_or_expr();
        vyb::ast::ExprPtr parse_logical_and_expr();
        vyb::ast::ExprPtr parse_bitwise_or_expr();
        vyb::ast::ExprPtr parse_bitwise_xor_expr();
        vyb::ast::ExprPtr parse_bitwise_and_expr();
        vyb::ast::ExprPtr parse_equality_expr();
        vyb::ast::ExprPtr parse_relational_expr();
        vyb::ast::ExprPtr parse_shift_expr();
        vyb::ast::ExprPtr parse_additive_expr();
        vyb::ast::ExprPtr parse_multiplicative_expr();
        vyb::ast::ExprPtr parse_unary_expr();
        vyb::ast::ExprPtr parse_postfix_expr();
        vyb::ast::ExprPtr parse_primary_expr(); // If this is different from parse_atom/parse_primary
        vyb::ast::ExprPtr parse_atom();
        vyb::ast::ExprPtr parse_literal();
        vyb::ast::ExprPtr parse_call_expression(vyb::ast::ExprPtr callee_expr);
        vyb::ast::ExprPtr parse_member_access(vyb::ast::ExprPtr object);

        // Helper for binary expressions
        vyb::ast::ExprPtr parse_binary_expression(std::function<vyb::ast::ExprPtr()> parse_higher_precedence, const std::vector<TokenType>& operators);

        // Helper to check if a token type is a literal
        bool is_literal(TokenType type) const; // Added const
    };

    class TypeParser : public BaseParser {
    private:
        ExpressionParser& expr_parser_;

    public:
        TypeParser(const std::vector<vyb::token::Token>& tokens, size_t& pos, const std::string& file_path, ExpressionParser& expr_parser);
        vyb::ast::TypeNodePtr parse();

    private:
        vyb::ast::TypeNodePtr parse_base_or_ownership_wrapped_type();
        vyb::ast::TypeNodePtr parse_atomic_or_group_type();
        vyb::ast::TypeNodePtr parse_postfix_type(vyb::ast::TypeNodePtr base_type);
    };

    class StatementParser : public BaseParser {
        int indent_level_;
        TypeParser& type_parser_;
        ExpressionParser& expr_parser_;
        DeclarationParser* decl_parser_; // Keep as is, will be set by constructor or setter
    public:
        StatementParser(const std::vector<token::Token>& tokens, size_t& pos, int indent_level, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser, DeclarationParser* decl_parser = nullptr);
        void set_declaration_parser(DeclarationParser* dp); // Setter method
        vyb::ast::StmtPtr parse();
        std::unique_ptr<vyb::ast::ExpressionStatement> parse_expression_statement();
        std::unique_ptr<vyb::ast::BlockStatement> parse_block();
        vyb::ast::ExprPtr parse_pattern();
        vyb::ast::StmtPtr parse_try();
        vyb::ast::StmtPtr parse_match();
        vyb::ast::StmtPtr parse_defer();
        vyb::ast::StmtPtr parse_await();
        std::unique_ptr<vyb::ast::UnsafeStatement> parse_unsafe(); // Added declaration
    private:
        bool is_statement_start(vyb::TokenType type) const; // Added declaration
        std::unique_ptr<vyb::ast::IfStatement> parse_if();
        std::unique_ptr<vyb::ast::WhileStatement> parse_while();
        std::unique_ptr<vyb::ast::ForStatement> parse_for();
        std::unique_ptr<vyb::ast::ReturnStatement> parse_return();
        std::unique_ptr<vyb::ast::PassStatement> parse_pass();
        std::unique_ptr<vyb::ast::BreakStatement> parse_break();
        std::unique_ptr<vyb::ast::ContinueStatement> parse_continue();
        std::unique_ptr<vyb::ast::FailStatement> parse_fail();
        std::unique_ptr<vyb::ast::PanicStatement> parse_panic();
        std::unique_ptr<vyb::ast::ExitStatement> parse_exit();
        std::unique_ptr<vyb::ast::RethrowStatement> parse_rethrow();
        vyb::ast::StmtPtr parse_var_decl();
        std::unique_ptr<vyb::ast::Node> parse_struct_pattern();
        std::unique_ptr<vyb::ast::Node> parse_tuple_pattern();
    public:
        using BaseParser::IsAtEnd;
        using BaseParser::peek;
        using BaseParser::consume;
        using BaseParser::skip_indents_dedents;
    };

    class DeclarationParser : public BaseParser {
        TypeParser& type_parser_;
        ExpressionParser& expr_parser_;
        StatementParser& stmt_parser_;
    public:
        TypeParser& get_type_parser() { return type_parser_; }
        ExpressionParser& get_expr_parser() { return expr_parser_; }
    public:
        DeclarationParser(const std::vector<vyb::token::Token>& tokens, size_t& pos, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser, StatementParser& stmt_parser);
        vyb::ast::DeclPtr parse();
        std::unique_ptr<vyb::ast::FunctionDeclaration> parse_function();
        std::unique_ptr<vyb::ast::Declaration> parse_struct();
        std::unique_ptr<vyb::ast::Declaration> parse_trait_declaration();
        std::unique_ptr<vyb::ast::Declaration> parse_impl();
        std::unique_ptr<vyb::ast::Declaration> parse_class_declaration();
        std::unique_ptr<vyb::ast::Declaration> parse_field_declaration();
        std::unique_ptr<vyb::ast::Declaration> parse_enum_declaration();
        std::unique_ptr<vyb::ast::TypeAliasDeclaration> parse_type_alias_declaration();
        std::unique_ptr<vyb::ast::VariableDeclaration> parse_global_var_declaration();
        std::vector<std::unique_ptr<vyb::ast::GenericParameter>> parse_generic_params();
        std::unique_ptr<vyb::ast::Node> parse_param();
        std::unique_ptr<vyb::ast::Declaration> parse_template_declaration();
        std::unique_ptr<vyb::ast::ImportDeclaration> parse_import_declaration();
        std::unique_ptr<vyb::ast::ImportDeclaration> parse_smuggle_declaration();
        std::unique_ptr<vyb::ast::Declaration> parse_extern_block();

    private:
        bool IsOperator(const vyb::token::Token& token) const;
        bool is_function_declaration_context() const;

    private:
        std::unique_ptr<vyb::ast::EnumVariant> parse_enum_variant();
        vyb::ast::FunctionParameter parse_function_parameter_struct();
    };

    class ModuleParser : public BaseParser {
        DeclarationParser& declaration_parser_;
    public:
        ModuleParser(const std::vector<vyb::token::Token>& tokens, size_t& pos, const std::string& file_path, DeclarationParser& declaration_parser);
        std::unique_ptr<vyb::ast::Module> parse();
    };

    class Parser {
        private:
        // Order of declaration matters for initialization order if not explicitly managed
        // It's generally good practice to declare them in the order they'll be initialized.
        std::vector<vyb::token::Token> tokens_; // Store tokens if Parser owns them
        size_t current_pos_;                  // Store current_pos_ if Parser manages it directly
        std::string file_path_;               // Store file_path_ if Parser manages it directly

        BaseParser base_parser_; // This is the primary BaseParser instance

        ExpressionParser expression_parser_;
        TypeParser type_parser_;
        StatementParser statement_parser_;
        DeclarationParser declaration_parser_;
        ModuleParser module_parser_;

    public:
        Parser(const std::vector<vyb::token::Token>& tokens, std::string file_path);
        std::unique_ptr<vyb::ast::Module> parse_module();

        ExpressionParser& get_expression_parser() { return expression_parser_; }
        TypeParser& get_type_parser() { return type_parser_; }
        StatementParser& get_statement_parser() { return statement_parser_; }
        DeclarationParser& get_declaration_parser() { return declaration_parser_; }
        ModuleParser& get_module_parser() { return module_parser_; }
    };

} // namespace vyb

#endif // VYB_PARSER_PARSER_HPP
