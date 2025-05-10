#ifndef VYN_PARSER_HPP
#define VYN_PARSER_HPP

#include <vector>
#include <memory>
#include "ast.hpp"
#include "token.hpp"

class BaseParser {
protected:
    BaseParser(const std::vector<Token>& tokens);
    Token peek() const;
    void expect(TokenType type);
    void skip_comments();
    void skip_indents();

    std::vector<Token> tokens_;
    size_t pos_;
};

class ExpressionParser : public BaseParser {
public:
    ExpressionParser(const std::vector<Token>& tokens, size_t& pos);
    std::unique_ptr<ASTNode> parse();

private:
    std::unique_ptr<ASTNode> parse_primary();
    size_t& shared_pos_;
};

class TypeParser : public BaseParser {
public:
    TypeParser(const std::vector<Token>& tokens, size_t& pos);
    std::unique_ptr<ASTNode> parse();

private:
    size_t& shared_pos_;
};

class StatementParser : public BaseParser {
public:
    StatementParser(const std::vector<Token>& tokens, size_t& pos);
    std::unique_ptr<ASTNode> parse();
    std::unique_ptr<ASTNode> parse_block();

private:
    void parse_match(std::unique_ptr<ASTNode>& node);
    void parse_pattern(std::unique_ptr<ASTNode>& node);
    size_t& shared_pos_;
};

class DeclarationParser : public BaseParser {
public:
    DeclarationParser(const std::vector<Token>& tokens, size_t& pos);
    std::unique_ptr<ASTNode> parse_template();
    std::unique_ptr<ASTNode> parse_class();
    std::unique_ptr<ASTNode> parse_function();

private:
    size_t& shared_pos_;
};

class ModuleParser : public BaseParser {
public:
    ModuleParser(const std::vector<Token>& tokens);
    std::unique_ptr<ASTNode> parse();
};

#endif // VYN_PARSER_HPP