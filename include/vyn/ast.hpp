#ifndef VYN_AST_HPP
#define VYN_AST_HPP

#include "vyn/token.hpp"
#include <memory>
#include <vector>

class ASTNode {
public:
    enum class Kind {
        Module,
        TemplateDecl,
        FunctionDecl,
        ClassDecl,
        Block,
        IfStmt,
        WhileStmt,
        ForStmt,
        ReturnStmt,
        BreakStmt,
        ContinueStmt,
        VarDecl,
        ConstDecl,
        ImportStmt,
        SmuggleStmt,
        TryStmt,
        CatchStmt,
        DeferStmt,
        AwaitStmt,      // Statement form
        MatchStmt,
        ScopedStmt,
        Expression,     // Generic binary/unary operator expression
        Identifier,
        Type,
        IntLiteral,
        StringLiteral,
        CallExpr,
        MemberExpr,
        ArrayExpr,      // For [Type; Size]() or [elem1, elem2]
        RangeExpr,      // For .. operator
        SubscriptExpr,  // For array[index]
        StructInitExpr, // For MyType { field: val }
        AwaitExpr,       // Expression form of await
        MatchArm,        // For a single arm in a match statement (pattern => expression)
        Pattern,         // For a match pattern
        PatternList      // For a list of patterns (e.g. in a tuple pattern)
    };

    Kind kind;
    Token token;
    std::vector<std::unique_ptr<ASTNode>> children;

    explicit ASTNode(Kind kind, const Token& token = Token(TokenType::EOF_TOKEN, "", 0, 0))
        : kind(kind), token(token) {}
};

#endif