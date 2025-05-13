#ifndef VYN_TOKEN_HPP
#define VYN_TOKEN_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include "ast.hpp" // Required for Vyn::AST::SourceLocation

namespace Vyn {

enum class TokenType {
    // Literals
    IDENTIFIER,
    INT_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    CHAR_LITERAL,

    // Keywords
    KEYWORD_LET,
    KEYWORD_VAR,
    KEYWORD_CONST,
    KEYWORD_IF,
    KEYWORD_ELSE,
    KEYWORD_WHILE,
    KEYWORD_FOR,
    KEYWORD_RETURN,
    KEYWORD_BREAK,
    KEYWORD_CONTINUE,
    KEYWORD_NULL,
    KEYWORD_TRUE,
    KEYWORD_FALSE,
    KEYWORD_FN,
    KEYWORD_STRUCT,
    KEYWORD_ENUM,
    KEYWORD_TRAIT,
    KEYWORD_IMPL,
    KEYWORD_TYPE,
    KEYWORD_MODULE,
    KEYWORD_USE,
    KEYWORD_PUB,
    KEYWORD_MUT,
    KEYWORD_TRY,
    KEYWORD_CATCH,
    KEYWORD_FINALLY,
    KEYWORD_DEFER,
    KEYWORD_MATCH,
    KEYWORD_SCOPED,
    KEYWORD_REF,
    KEYWORD_EXTERN,
    KEYWORD_AS,
    KEYWORD_IN,
    KEYWORD_CLASS,    // Added back
    KEYWORD_TEMPLATE, // Added back
    KEYWORD_IMPORT,   // Added back
    KEYWORD_SMUGGLE,  // Added back
    KEYWORD_AWAIT,    // Added back
    KEYWORD_ASYNC,    // Added back
    KEYWORD_OPERATOR, // Added back


    // Operators
    PLUS,           // +
    MINUS,          // -
    MULTIPLY,       // *
    DIVIDE,         // /
    MODULO,         // %
    EQ,             // =
    EQEQ,           // ==
    NOTEQ,          // !=
    LT,             // <
    GT,             // >
    LTEQ,           // <=
    GTEQ,           // >=
    AND,            // && (logical and)
    OR,             // || (logical or)
    BANG,           // !
    AMPERSAND,      // & (bitwise and)
    PIPE,           // | (bitwise or)
    CARET,          // ^ (bitwise xor)
    TILDE,          // ~ (bitwise not)
    LSHIFT,         // <<
    RSHIFT,         // >>
    DOTDOT,         // .. (Added back)

    // Punctuation
    LPAREN,         // (
    RPAREN,         // )
    LBRACE,         // {
    RBRACE,         // }
    LBRACKET,       // [
    RBRACKET,       // ]
    COMMA,          // ,
    DOT,            // .
    COLON,          // :
    SEMICOLON,      // ;
    ARROW,          // ->
    FAT_ARROW,      // =>
    COLONCOLON,     // ::
    AT,             // @ (Added back)
    UNDERSCORE,     // _ (Added back)

    // Misc
    UNKNOWN,
    END_OF_FILE,    // Replaces EOF_TOKEN
    COMMENT,        // Added back
    NEWLINE,        // Added back
    INDENT,         // Added back
    DEDENT,         // Added back
    // EOF_TOKEN, // Removed, use END_OF_FILE
};

struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int column;
    AST::SourceLocation location; // Added

    Token(TokenType type, const std::string& lexeme, int line, int column, AST::SourceLocation location) // Updated
        : type(type), lexeme(lexeme), line(line), column(column), location(location) {} // Updated

    std::string to_string() const;
};

std::string token_type_to_string(TokenType type);

}

#endif