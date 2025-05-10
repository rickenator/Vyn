#ifndef VYN_HPP
#define VYN_HPP

#include "lexer.hpp"
#include "parser.hpp"
#include "ast.hpp"
#include "token.hpp"

/*
 * EBNF Grammar for Vyn Language
 *
 * Non-terminals are in CamelCase, terminals are in UPPER_CASE or quoted strings.
 * {X} means zero or more repetitions of X.
 * [X] means X is optional.
 * X | Y means X or Y.
 * X Y means X followed by Y.
 *
 * Program ::= {ModuleItem}
 * ModuleItem ::= ImportStmt | TemplateDecl | ClassDecl | FunctionDecl | VarDecl | ConstDecl
 *
 * ImportStmt ::= (KEYWORD_IMPORT | KEYWORD_SMUGGLE) IDENTIFIER {COLONCOLON IDENTIFIER} [SEMICOLON]
 * TemplateDecl ::= KEYWORD_TEMPLATE IDENTIFIER [LT TypeParamList GT] Block
 * TypeParamList ::= TypeParam {COMMA TypeParam}
 * TypeParam ::= IDENTIFIER [COLON IDENTIFIER]
 * ClassDecl ::= KEYWORD_CLASS IDENTIFIER Block
 * FunctionDecl ::= [KEYWORD_ASYNC] KEYWORD_FN IDENTIFIER LPAREN [ParamList] RPAREN [ARROW Type] (Block | INDENT StmtList DEDENT)
 * ParamList ::= Param {COMMA Param}
 * Param ::= [AMPERSAND [IDENTIFIER("mut")]] IDENTIFIER [COLON Type]
 * VarDecl ::= KEYWORD_VAR IDENTIFIER [COLON Type] [EQ Expression] [SEMICOLON]
 * ConstDecl ::= KEYWORD_CONST IDENTIFIER [COLON Type] EQ Expression [SEMICOLON]
 *
 * Block ::= LBRACE {Stmt} RBRACE | INDENT StmtList DEDENT
 * StmtList ::= Stmt {Stmt}
 * Stmt ::= VarDecl | ConstDecl | IfStmt | ForStmt | ReturnStmt | DeferStmt | TryStmt | MatchStmt | Expression [SEMICOLON]
 * IfStmt ::= KEYWORD_IF Expression Block [KEYWORD_ELSE (Block | Expression)]
 * ForStmt ::= KEYWORD_FOR IDENTIFIER KEYWORD_IN Expression Block
 * ReturnStmt ::= KEYWORD_RETURN [Expression] [SEMICOLON]
 * DeferStmt ::= KEYWORD_DEFER Expression [SEMICOLON]
 * TryStmt ::= KEYWORD_TRY Block {CatchClause} [KEYWORD_FINALLY Block]
 * CatchClause ::= KEYWORD_CATCH LPAREN IDENTIFIER COLON Type RPAREN Block
 * MatchStmt ::= KEYWORD_MATCH Expression (LBRACE {MatchArm} RBRACE | INDENT {MatchArm} DEDENT)
 * MatchArm ::= Pattern FAT_ARROW Expression [COMMA | SEMICOLON]
 * Pattern ::= IDENTIFIER [LPAREN IDENTIFIER RPAREN]
 *
 * Expression ::= IfExpr | ListComp | PrimaryExpr {BinaryOp PrimaryExpr}
 * IfExpr ::= KEYWORD_IF Expression Block [KEYWORD_ELSE (Block | Expression)]
 * ListComp ::= LBRACKET Expression KEYWORD_FOR IDENTIFIER KEYWORD_IN Expression [DOTDOT Expression] RBRACKET
 * PrimaryExpr ::= BANG PrimaryExpr | KEYWORD_AWAIT PrimaryExpr | IDENTIFIER {Access} | INT_LITERAL | STRING_LITERAL | ArrayExpr
 * Access ::= DOT IDENTIFIER | LPAREN [ExprList] RPAREN | LBRACKET Expression RBRACKET | LBRACE {Field} RBRACE | LT Type GT [COLONCOLON IDENTIFIER [LPAREN [ExprList] RPAREN]]
 * ExprList ::= Expression {COMMA Expression}
 * Field ::= IDENTIFIER [EQ | COLON Expression] [COMMA | SEMICOLON]
 * ArrayExpr ::= LBRACKET Type SEMICOLON Expression RBRACKET [LPAREN RPAREN]
 * BinaryOp ::= LT | GT | EQEQ | PLUS | MINUS | DIVIDE | AND
 *
 * Type ::= IDENTIFIER("ref") LT Type GT | LBRACKET Type [SEMICOLON Expression] RBRACKET | AMPERSAND [IDENTIFIER("mut")] Type | IDENTIFIER {TypeAccess}
 * TypeAccess ::= LBRACKET [SEMICOLON Expression | Expression] RBRACKET | LT TypeList GT
 * TypeList ::= Type {COMMA Type}
 */

#endif // VYN_HPP