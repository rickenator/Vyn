#ifndef VYN_PARSER_HPP
#define VYN_PARSER_HPP

#include "ast.hpp"
#include <vector>
#include <memory>
#include <stdexcept>

class BaseParser {
protected:
    const std::vector<Token>& tokens_;
    size_t& pos_;
    std::vector<int> indent_levels_;

    BaseParser(const std::vector<Token>& tokens, size_t& pos)
        : tokens_(tokens), pos_(pos), indent_levels_{0} {}

    void skip_comments_and_newlines(); // Advances pos_ past comments/newlines
    const Token& peek() const;         // Skips comments/newlines with a temp var, then returns tokens_[temp_pos]
    Token consume();                   // Calls skip_comments_and_newlines, then returns tokens_[pos_] and advances pos_
    Token expect(TokenType type);      // Uses peek, then consumes if type matches, returns consumed token
    bool match(TokenType type);        // Uses peek, then consumes if type matches, returns bool

    void skip_indents_dedents(); // Handles INDENT/DEDENT, may call skip_comments_and_newlines
};

class ExpressionParser : public BaseParser {
public:
    ExpressionParser(const std::vector<Token>& tokens, size_t& pos);
    std::unique_ptr<ASTNode> parse(); // Entry point for parsing an expression

private:
    std::unique_ptr<ASTNode> parse_logical_expr();
    std::unique_ptr<ASTNode> parse_equality_expr();
    std::unique_ptr<ASTNode> parse_relational_expr();
    std::unique_ptr<ASTNode> parse_additive_expr();
    std::unique_ptr<ASTNode> parse_multiplicative_expr();
    std::unique_ptr<ASTNode> parse_unary_expr();
    std::unique_ptr<ASTNode> parse_primary();
    std::unique_ptr<ASTNode> parse_call_expr(std::unique_ptr<ASTNode> callee);
    std::unique_ptr<ASTNode> parse_member_expr(std::unique_ptr<ASTNode> object);
    std::unique_ptr<ASTNode> parse_array_expr();
};

class TypeParser : public BaseParser {
public:
    TypeParser(const std::vector<Token>& tokens, size_t& pos);
    std::unique_ptr<ASTNode> parse();
};

class StatementParser : public BaseParser {
    int indent_level_;
public:
    StatementParser(const std::vector<Token>& tokens, size_t& pos, int indent_level);
    std::unique_ptr<ASTNode> parse();
    std::unique_ptr<ASTNode> parse_expression_statement(); // Moved to public
    std::unique_ptr<ASTNode> parse_block(); // Moved to public
private:
    std::unique_ptr<ASTNode> parse_if();
    std::unique_ptr<ASTNode> parse_while();
    std::unique_ptr<ASTNode> parse_for();
    std::unique_ptr<ASTNode> parse_return();
    std::unique_ptr<ASTNode> parse_break();
    std::unique_ptr<ASTNode> parse_continue();
    std::unique_ptr<ASTNode> parse_var_decl(bool is_scoped = false);
    std::unique_ptr<ASTNode> parse_const_decl(bool is_scoped = false);
    std::unique_ptr<ASTNode> parse_import();
    std::unique_ptr<ASTNode> parse_smuggle();
    std::unique_ptr<ASTNode> parse_class();
    std::unique_ptr<ASTNode> parse_template();
    std::unique_ptr<ASTNode> parse_try();
    std::unique_ptr<ASTNode> parse_defer();
    std::unique_ptr<ASTNode> parse_await();
    std::unique_ptr<ASTNode> parse_match();
    std::unique_ptr<ASTNode> parse_var();
    std::unique_ptr<ASTNode> parse_const();
    std::unique_ptr<ASTNode> parse_scoped();
    std::unique_ptr<ASTNode> parse_pattern(); // Added declaration
};

class DeclarationParser : public BaseParser {
public:
    DeclarationParser(const std::vector<Token>& tokens, size_t& pos);
    std::unique_ptr<ASTNode> parse();
    std::unique_ptr<ASTNode> parse_function();
    std::unique_ptr<ASTNode> parse_class(); // Make public
    std::unique_ptr<ASTNode> parse_template(); // Make public

private:
    std::unique_ptr<ASTNode> parse_block(); // Keep private or remove if StatementParser::parse_block is used
};

class ModuleParser : public BaseParser {
public:
    ModuleParser(const std::vector<Token>& tokens, size_t& pos);
    std::unique_ptr<ASTNode> parse();
};

#endif