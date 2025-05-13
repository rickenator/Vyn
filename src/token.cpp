#include "vyn/token.hpp"
#include <unordered_map> // Added for std::unordered_map
#include <string> // Added for std::string

// The function is defined within the Vyn namespace if the header declares it so.
// If token.hpp declares `namespace Vyn { std::string token_type_to_string(TokenType type); }`
// then this definition is correct.
// If token.hpp declares `std::string token_type_to_string(Vyn::TokenType type);` at global scope,
// then this definition is also correct.
// Assuming it's part of the Vyn namespace as per previous discussions.

namespace Vyn {

std::string token_type_to_string(TokenType type) {
    static const std::unordered_map<TokenType, std::string> token_map = {
        // Literals
        {TokenType::IDENTIFIER, "IDENTIFIER"},
        {TokenType::INT_LITERAL, "INT_LITERAL"},
        {TokenType::FLOAT_LITERAL, "FLOAT_LITERAL"},
        {TokenType::STRING_LITERAL, "STRING_LITERAL"},
        {TokenType::CHAR_LITERAL, "CHAR_LITERAL"},

        // Keywords
        {TokenType::KEYWORD_LET, "KEYWORD_LET"},
        {TokenType::KEYWORD_VAR, "KEYWORD_VAR"},
        {TokenType::KEYWORD_CONST, "KEYWORD_CONST"},
        {TokenType::KEYWORD_IF, "KEYWORD_IF"},
        {TokenType::KEYWORD_ELSE, "KEYWORD_ELSE"},
        {TokenType::KEYWORD_WHILE, "KEYWORD_WHILE"},
        {TokenType::KEYWORD_FOR, "KEYWORD_FOR"},
        {TokenType::KEYWORD_RETURN, "KEYWORD_RETURN"},
        {TokenType::KEYWORD_BREAK, "KEYWORD_BREAK"},
        {TokenType::KEYWORD_CONTINUE, "KEYWORD_CONTINUE"},
        {TokenType::KEYWORD_NULL, "KEYWORD_NULL"},
        {TokenType::KEYWORD_TRUE, "KEYWORD_TRUE"},
        {TokenType::KEYWORD_FALSE, "KEYWORD_FALSE"},
        {TokenType::KEYWORD_FN, "KEYWORD_FN"},
        {TokenType::KEYWORD_STRUCT, "KEYWORD_STRUCT"},
        {TokenType::KEYWORD_ENUM, "KEYWORD_ENUM"},
        {TokenType::KEYWORD_TRAIT, "KEYWORD_TRAIT"},
        {TokenType::KEYWORD_IMPL, "KEYWORD_IMPL"},
        {TokenType::KEYWORD_TYPE, "KEYWORD_TYPE"},
        {TokenType::KEYWORD_MODULE, "KEYWORD_MODULE"},
        {TokenType::KEYWORD_USE, "KEYWORD_USE"},
        {TokenType::KEYWORD_PUB, "KEYWORD_PUB"},
        {TokenType::KEYWORD_MUT, "KEYWORD_MUT"},
        {TokenType::KEYWORD_TRY, "KEYWORD_TRY"},
        {TokenType::KEYWORD_CATCH, "KEYWORD_CATCH"},
        {TokenType::KEYWORD_FINALLY, "KEYWORD_FINALLY"},
        {TokenType::KEYWORD_DEFER, "KEYWORD_DEFER"},
        {TokenType::KEYWORD_MATCH, "KEYWORD_MATCH"},
        {TokenType::KEYWORD_SCOPED, "KEYWORD_SCOPED"},
        {TokenType::KEYWORD_REF, "KEYWORD_REF"},
        {TokenType::KEYWORD_EXTERN, "KEYWORD_EXTERN"},
        {TokenType::KEYWORD_AS, "KEYWORD_AS"},
        {TokenType::KEYWORD_IN, "KEYWORD_IN"},
        {TokenType::KEYWORD_CLASS, "KEYWORD_CLASS"},
        {TokenType::KEYWORD_TEMPLATE, "KEYWORD_TEMPLATE"},
        {TokenType::KEYWORD_IMPORT, "KEYWORD_IMPORT"},
        {TokenType::KEYWORD_SMUGGLE, "KEYWORD_SMUGGLE"},
        {TokenType::KEYWORD_AWAIT, "KEYWORD_AWAIT"},
        {TokenType::KEYWORD_ASYNC, "KEYWORD_ASYNC"},
        {TokenType::KEYWORD_OPERATOR, "KEYWORD_OPERATOR"},

        // Operators
        {TokenType::PLUS, "PLUS"},
        {TokenType::MINUS, "MINUS"},
        {TokenType::MULTIPLY, "MULTIPLY"},
        {TokenType::DIVIDE, "DIVIDE"},
        {TokenType::MODULO, "MODULO"},
        {TokenType::EQ, "EQ"},
        {TokenType::EQEQ, "EQEQ"},
        {TokenType::NOTEQ, "NOTEQ"},
        {TokenType::LT, "LT"},
        {TokenType::GT, "GT"},
        {TokenType::LTEQ, "LTEQ"},
        {TokenType::GTEQ, "GTEQ"},
        {TokenType::AND, "AND"}, // Logical AND
        {TokenType::OR, "OR"},   // Logical OR
        {TokenType::BANG, "BANG"},
        {TokenType::AMPERSAND, "AMPERSAND"}, // Bitwise AND
        {TokenType::PIPE, "PIPE"},           // Bitwise OR
        {TokenType::CARET, "CARET"},         // Bitwise XOR
        {TokenType::TILDE, "TILDE"},         // Bitwise NOT
        {TokenType::LSHIFT, "LSHIFT"},
        {TokenType::RSHIFT, "RSHIFT"},
        {TokenType::DOTDOT, "DOTDOT"},

        // Punctuation
        {TokenType::LPAREN, "LPAREN"},
        {TokenType::RPAREN, "RPAREN"},
        {TokenType::LBRACE, "LBRACE"},
        {TokenType::RBRACE, "RBRACE"},
        {TokenType::LBRACKET, "LBRACKET"},
        {TokenType::RBRACKET, "RBRACKET"},
        {TokenType::COMMA, "COMMA"},
        {TokenType::DOT, "DOT"},
        {TokenType::COLON, "COLON"},
        {TokenType::SEMICOLON, "SEMICOLON"},
        {TokenType::ARROW, "ARROW"},
        {TokenType::FAT_ARROW, "FAT_ARROW"},
        {TokenType::COLONCOLON, "COLONCOLON"},
        {TokenType::AT, "AT"},
        {TokenType::UNDERSCORE, "UNDERSCORE"},

        // Misc
        {TokenType::UNKNOWN, "UNKNOWN"},
        {TokenType::END_OF_FILE, "END_OF_FILE"},
        {TokenType::COMMENT, "COMMENT"},
        {TokenType::NEWLINE, "NEWLINE"},
        {TokenType::INDENT, "INDENT"},
        {TokenType::DEDENT, "DEDENT"}
    };
    auto it = token_map.find(type);
    return it != token_map.end() ? it->second : "UNKNOWN"; // Default to "UNKNOWN"
}

} // namespace Vyn