#ifndef VYN_AST_HPP
#define VYN_AST_HPP

#include "vyn/token.hpp"
#include <vector>
#include <memory>

struct ASTNode {
    enum class Kind { Module, Template, Class, Function, Statement, Expression, Type };
    Kind kind;
    Token token;
    std::vector<std::unique_ptr<ASTNode>> children;

    ASTNode(Kind k) : kind(k) {}
    ASTNode(Kind k, const Token& t) : kind(k), token(t) {}
};

#endif // VYN_AST_HPP