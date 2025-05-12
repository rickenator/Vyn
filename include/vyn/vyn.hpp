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
 * ModuleItem ::= ImportStmt | SmuggleStmt | TemplateDecl | ClassDecl | FunctionDecl | VarDecl | ConstDecl
 *
 * ImportStmt ::= KEYWORD_IMPORT IDENTIFIER {COLONCOLON IDENTIFIER} [SEMICOLON]
 * SmuggleStmt ::= KEYWORD_SMUGGLE IDENTIFIER {COLONCOLON IDENTIFIER} [SEMICOLON]
 * TemplateDecl ::= KEYWORD_TEMPLATE IDENTIFIER [LT TypeParamList GT] Block
 * TypeParamList ::= TypeParam {COMMA TypeParam}
 * TypeParam ::= IDENTIFIER [COLON IDENTIFIER]
 * ClassDecl ::= KEYWORD_CLASS IDENTIFIER Block
 * FunctionDecl ::= [KEYWORD_ASYNC] KEYWORD_FN IDENTIFIER LPAREN [ParamList] RPAREN [ARROW Type] Block
 * ParamList ::= Param {COMMA Param}
 * Param ::= [AMPERSAND [IDENTIFIER("mut")]] IDENTIFIER [TypeAnnotation]
 * VarDecl ::= KEYWORD_VAR IDENTIFIER [TypeAnnotation] [EQ Expression] [SEMICOLON]
 * ConstDecl ::= KEYWORD_CONST IDENTIFIER [TypeAnnotation] EQ Expression [SEMICOLON]
 * TypeAnnotation ::= COLON Type
 *
 * Type ::= KEYWORD_REF LT Type GT | LBRACKET Type [SEMICOLON Expression] RBRACKET | AMPERSAND [IDENTIFIER("mut")] Type | IDENTIFIER {TypeAccess}
 * TypeAccess ::= LBRACKET [SEMICOLON Expression | Expression] RBRACKET | LT TypeList GT
 * TypeList ::= Type {COMMA Type}
 *
 * Block ::= LBRACE {Stmt} RBRACE | INDENT StmtList DEDENT
 * StmtList ::= Stmt {Stmt}
 * Stmt ::= VarDecl | ConstDecl | IfStmt | ForStmt | ReturnStmt | DeferStmt | TryStmt | MatchStmt | ImportStmt | SmuggleStmt | Expression [SEMICOLON]
 * IfStmt ::= KEYWORD_IF Expression Block [KEYWORD_ELSE (Block | Expression)]
 * ForStmt ::= KEYWORD_FOR IDENTIFIER KEYWORD_IN Expression Block
 * ReturnStmt ::= KEYWORD_RETURN [Expression] [SEMICOLON]
 * DeferStmt ::= KEYWORD_DEFER Expression [SEMICOLON]
 * TryStmt ::= KEYWORD_TRY Block {CatchClause} [KEYWORD_FINALLY Block]
 * CatchClause ::= KEYWORD_CATCH LPAREN IDENTIFIER TypeAnnotation RPAREN Block
 * MatchStmt ::= KEYWORD_MATCH Expression (LBRACE {MatchArm} RBRACE | INDENT {MatchArm} DEDENT)
 * MatchArm ::= Pattern FAT_ARROW Expression [COMMA | SEMICOLON]
 * Pattern ::= IDENTIFIER | INT_LITERAL | UNDERSCORE | LPAREN PatternList RPAREN
 * PatternList ::= Pattern {COMMA Pattern}
 *
 * Expression ::= LogicalExpr
 * LogicalExpr ::= EqualityExpr {AND EqualityExpr}
 * EqualityExpr ::= RelationalExpr {EQEQ RelationalExpr}
 * RelationalExpr ::= AdditiveExpr {(LT | GT) AdditiveExpr}
 * AdditiveExpr ::= PrimaryExpr {(PLUS | MINUS | DIVIDE | MULTIPLY) PrimaryExpr}
 * PrimaryExpr ::= BANG PrimaryExpr | KEYWORD_AWAIT PrimaryExpr | IDENTIFIER {Access} | INT_LITERAL | STRING_LITERAL | ArrayExpr | LPAREN Expression RPAREN
 * Access ::= DOT IDENTIFIER | LPAREN [ExprList] RPAREN | LBRACKET Expression RBRACKET | LBRACE {Field} RBRACE
 * ExprList ::= Expression {COMMA Expression}
 * Field ::= IDENTIFIER (EQ | COLON) Expression [COMMA | SEMICOLON]
 * ArrayExpr ::= LBRACKET Type SEMICOLON Expression RBRACKET [LPAREN RPAREN]
 *
 * identifier = letter { letter | digit | "_" }
 * int_literal = ["-"] digit { digit }
 * string_literal = "\"" { any_char - "\"" } "\""
 * letter = "a".."z" | "A".."Z"
 * digit = "0".."9"
 */

#endif // VYN_HPP