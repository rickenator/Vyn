#ifndef VYN_AST_HPP
#define VYN_AST_HPP

#include <memory>
#include <vector>
#include "token.hpp"

struct ASTNode {
    enum class Kind { Module, Template, Class, Function, Statement, Expression, Type };
    Kind kind;
    std::vector<std::unique_ptr<ASTNode>> children;
    Token token;
    ASTNode(Kind k, Token t = {}) : kind(k), token(t) {}
};

#endif // VYN_AST_HPP