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
        FloatLiteral,   // Added FloatLiteral
        StringLiteral,
        CallExpr,
        MemberExpr,
        ArrayExpr,      // For [Type; Size]() or [elem1, elem2]
        ListComprehensionExpr, // Added ListComprehensionExpr
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
    bool is_async; // Added for FunctionDecl

    explicit ASTNode(Kind kind, const Token& token = Token(TokenType::EOF_TOKEN, "", 0, 0), bool async_status = false)
        : kind(kind), token(token), is_async(kind == Kind::FunctionDecl ? async_status : false) {}

    // Default constructor for ASTNode, if needed by other parts of the code,
    // though explicit initialization is generally preferred.
    // ASTNode() : kind(Kind::Module), token(Token(TokenType::EOF_TOKEN, "", 0, 0)), is_async(false) {}
};

#endif