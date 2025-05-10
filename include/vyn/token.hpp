#ifndef VYN_TOKEN_HPP
#define VYN_TOKEN_HPP

#include <string>

enum class TokenType {
    EOF_TOKEN,
    INDENT,
    DEDENT,
    KEYWORD_FN,
    KEYWORD_IF,
    KEYWORD_ELSE,
    KEYWORD_VAR,
    KEYWORD_CONST,
    KEYWORD_TEMPLATE,
    KEYWORD_CLASS,
    KEYWORD_RETURN,
    KEYWORD_FOR,
    KEYWORD_IN,
    KEYWORD_SCOPED,
    KEYWORD_IMPORT,
    KEYWORD_SMUGGLE,
    KEYWORD_TRY,
    KEYWORD_CATCH,
    KEYWORD_FINALLY,
    KEYWORD_DEFER,
    KEYWORD_ASYNC,
    KEYWORD_AWAIT,
    KEYWORD_MATCH,
    IDENTIFIER,
    INT_LITERAL,
    FLOAT_LITERAL,  // Added for floating-point literals
    STRING_LITERAL,
    COMMENT,
    LBRACE,
    RBRACE,
    LPAREN,
    RPAREN,
    LBRACKET,
    RBRACKET,
    SEMICOLON,
    COLON,
    COLONCOLON,
    COMMA,
    EQ,
    EQEQ,
    FAT_ARROW,
    LT,
    GT,
    PLUS,
    MINUS,
    DIVIDE,
    MULTIPLY,
    ARROW,
    DOT,
    DOTDOT,
    AND,
    AMPERSAND,
    BANG,
    AT
};

struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
};

std::string token_type_to_string(TokenType type);

#endif // VYN_TOKEN_HPP