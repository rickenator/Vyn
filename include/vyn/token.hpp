#ifndef VYN_TOKEN_HPP
#define VYN_TOKEN_HPP

#include <string>
#include <unordered_map>

enum class TokenType {
    EOF_TOKEN,
    IDENTIFIER,
    INT_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    PLUS,
    MINUS,
    MULTIPLY,
    DIVIDE,
    EQ,
    EQEQ,
    NOTEQ,
    LT,
    GT,
    LTEQ,
    GTEQ,
    AND,
    OR,             // Added
    BANG,
    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    LBRACKET,
    RBRACKET,
    COLON,
    SEMICOLON,
    COMMA,
    DOT,
    COLONCOLON,
    ARROW,
    FAT_ARROW,
    DOTDOT,
    MODULO,         // Added
    AMPERSAND,
    AT,
    KEYWORD_IF,
    KEYWORD_ELSE,
    KEYWORD_WHILE,
    KEYWORD_FOR,
    KEYWORD_IN,
    KEYWORD_RETURN,
    KEYWORD_BREAK,
    KEYWORD_CONTINUE,
    KEYWORD_VAR,
    KEYWORD_CONST,
    KEYWORD_FN,
    KEYWORD_CLASS,
    KEYWORD_TEMPLATE,
    KEYWORD_IMPORT,
    KEYWORD_SMUGGLE,
    KEYWORD_OPERATOR,
    KEYWORD_ASYNC,
    KEYWORD_AWAIT,
    KEYWORD_TRY,
    KEYWORD_CATCH,
    KEYWORD_FINALLY,
    KEYWORD_DEFER,
    KEYWORD_MATCH,
    KEYWORD_SCOPED,
    KEYWORD_REF,
    UNDERSCORE,
    INDENT,
    DEDENT,
    COMMENT,
    NEWLINE,
    INVALID         // Added
};

struct Token {
    TokenType type;
    std::string value;
    size_t line;
    size_t column;

    Token(TokenType type, std::string value, size_t line, size_t column)
        : type(type), value(std::move(value)), line(line), column(column) {}
};

std::string token_type_to_string(TokenType type);

#endif