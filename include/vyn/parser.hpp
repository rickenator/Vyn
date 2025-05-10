#ifndef VYN_PARSER_HPP
#define VYN_PARSER_HPP

#include "vyn/token.hpp"
#include "vyn/ast.hpp"
#include <vector>
#include <memory>
#include <string>

class BaseParser {
protected:
    const std::vector<Token>& tokens_;
    size_t& pos_;
    std::vector<int> indent_levels_;

public:
    BaseParser(const std::vector<Token>& tokens, size_t& pos);
    Token peek() const;
    void expect(TokenType type);
    void skip_comments();
    void skip_indents();
};

class ExpressionParser : public BaseParser {
public:
    ExpressionParser(const std::vector<Token>& tokens, size_t& pos);
    std::unique_ptr<ASTNode> parse();
    std::unique_ptr<ASTNode> parse_expression();
    std::unique_ptr<ASTNode> parse_primary();
};

class TypeParser : public BaseParser {
public:
    TypeParser(const std::vector<Token>& tokens, size_t& pos);
    std::unique_ptr<ASTNode> parse();
};

class StatementParser : public BaseParser {
public:
    StatementParser(const std::vector<Token>& tokens, size_t& pos);
    std::unique_ptr<ASTNode> parse();
    std::unique_ptr<ASTNode> parse_block();
    void parse_match(std::unique_ptr<ASTNode>& node);
    void parse_pattern(std::unique_ptr<ASTNode>& node);
};

class DeclarationParser : public BaseParser {
public:
    DeclarationParser(const std::vector<Token>& tokens, size_t& pos);
    std::unique_ptr<ASTNode> parse_template();
    std::unique_ptr<ASTNode> parse_class();
    std::unique_ptr<ASTNode> parse_function();
};

class ModuleParser : public BaseParser {
public:
    ModuleParser(const std::vector<Token>& tokens, size_t& pos);
    std::unique_ptr<ASTNode> parse();
};

#endif // VYN_PARSER_HPP