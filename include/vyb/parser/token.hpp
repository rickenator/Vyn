// SPDX-License-Identifier: Apache-2.0

#ifndef VYB_PARSER_TOKEN_HPP
#define VYB_PARSER_TOKEN_HPP

#include <string>
#include <utility> // For std::move
#include "source_location.hpp" // Corrected path

namespace vyb {

enum class TokenType {
    // Literals
    IDENTIFIER,
    INT_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    CHAR_LITERAL,

    // Keywords
    KEYWORD_LET, KEYWORD_VAR, KEYWORD_CONST, KEYWORD_IF, KEYWORD_ELSE,
    KEYWORD_WHILE, KEYWORD_FOR, KEYWORD_RETURN, KEYWORD_BREAK, KEYWORD_CONTINUE,
    KEYWORD_NULL, KEYWORD_TRUE, KEYWORD_FALSE, KEYWORD_FN, KEYWORD_STRUCT,
    KEYWORD_ENUM, KEYWORD_ASPECT, KEYWORD_BIND, KEYWORD_TYPE, KEYWORD_MODULE,
    KEYWORD_USE, KEYWORD_PUB, KEYWORD_MUT, KEYWORD_TRY, KEYWORD_CATCH,
    KEYWORD_FINALLY, KEYWORD_DEFER, KEYWORD_MATCH, KEYWORD_SELECT, KEYWORD_PASS,
    KEYWORD_SCOPED, KEYWORD_REF, KEYWORD_EXTERN, KEYWORD_AS, KEYWORD_IN,
    KEYWORD_CLASS, KEYWORD_TEMPLATE,
    KEYWORD_IMPORT, KEYWORD_SMUGGLE, KEYWORD_FROM,
    KEYWORD_AWAIT, KEYWORD_ASYNC, KEYWORD_OPERATOR,
    KEYWORD_MY, KEYWORD_OUR, KEYWORD_THEIR, KEYWORD_MILD, KEYWORD_PTR, // New ownership keywords
    KEYWORD_BORROW, KEYWORD_VIEW, // Changed from BORROW_MUT
    KEYWORD_NIL, // Added for nil literal
    KEYWORD_FREEDOM, // Added for freedom blocks
    KEYWORD_AUTO, // Added for auto type inference
    KEYWORD_FAIL, KEYWORD_TRAP, KEYWORD_REFAIL, KEYWORD_ENSURE, KEYWORD_PANIC, KEYWORD_EXIT, // Error handling keywords
    KEYWORD_TYPEOF, KEYWORD_TYPENAME, // Introspection keywords

    // Operators
    PLUS, MINUS, MULTIPLY, DIVIDE, MODULO, EQ, EQEQ, NOTEQ, LT, GT, LTEQ, GTEQ,
    AND, OR, BANG, AMPERSAND, PIPE, CARET, TILDE, LSHIFT, RSHIFT, DOTDOT, ELLIPSIS,
    PLUSEQ, MINUSEQ, MULTIPLYEQ, DIVEQ, MODEQ, LSHIFTEQ, RSHIFTEQ,
    BITWISEANDEQ, BITWISEOREQ, BITWISEXOREQ, COLONEQ, // Compound assignment and colon-equals

    // Punctuation
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET, COMMA, DOT, COLON,
    SEMICOLON, ARROW, FAT_ARROW, COLONCOLON, AT, HASH, UNDERSCORE, QUESTION_MARK, // Added for optional type T?

    // Misc
    UNKNOWN,
    END_OF_FILE,
    COMMENT,
    NEWLINE,
    INDENT,
    DEDENT,
    ILLEGAL // Added for illegal/error tokens
};

std::string token_type_to_string(TokenType type);

namespace token {
    class Token {
    public:
        vyb::TokenType type;
        std::string lexeme;
        vyb::SourceLocation location;

        Token(vyb::TokenType type, const std::string& lexeme, const vyb::SourceLocation& loc);
        std::string toString() const;

        // Helper method to safely get the token text
        std::string getText() const {
            return lexeme;
        }
    };
} // namespace token
} // namespace vyb

#endif // VYB_PARSER_TOKEN_HPP
