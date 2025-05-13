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
        {TokenType::EOF_TOKEN, "EOF_TOKEN"},
        {TokenType::IDENTIFIER, "IDENTIFIER"},
        {TokenType::INT_LITERAL, "INT_LITERAL"},
        {TokenType::FLOAT_LITERAL, "FLOAT_LITERAL"},
        {TokenType::STRING_LITERAL, "STRING_LITERAL"},
        {TokenType::PLUS, "PLUS"},
        {TokenType::MINUS, "MINUS"},
        {TokenType::MULTIPLY, "MULTIPLY"},
        {TokenType::DIVIDE, "DIVIDE"},
        {TokenType::EQ, "EQ"},
        {TokenType::EQEQ, "EQEQ"},
        {TokenType::NOTEQ, "NOTEQ"},
        {TokenType::LT, "LT"},
        {TokenType::GT, "GT"},
        {TokenType::LTEQ, "LTEQ"},
        {TokenType::GTEQ, "GTEQ"},
        {TokenType::AND, "AND"},
        {TokenType::BANG, "BANG"},
        {TokenType::LPAREN, "LPAREN"},
        {TokenType::RPAREN, "RPAREN"},
        {TokenType::LBRACE, "LBRACE"},
        {TokenType::RBRACE, "RBRACE"},
        {TokenType::LBRACKET, "LBRACKET"},
        {TokenType::RBRACKET, "RBRACKET"},
        {TokenType::COLON, "COLON"},
        {TokenType::SEMICOLON, "SEMICOLON"},
        {TokenType::COMMA, "COMMA"},
        {TokenType::DOT, "DOT"},
        {TokenType::COLONCOLON, "COLONCOLON"},
        {TokenType::ARROW, "ARROW"},
        {TokenType::FAT_ARROW, "FAT_ARROW"},
        {TokenType::DOTDOT, "DOTDOT"},
        {TokenType::AMPERSAND, "AMPERSAND"},
        {TokenType::AT, "AT"},
        {TokenType::KEYWORD_IF, "KEYWORD_IF"},
        {TokenType::KEYWORD_ELSE, "KEYWORD_ELSE"},
        {TokenType::KEYWORD_WHILE, "KEYWORD_WHILE"},
        {TokenType::KEYWORD_FOR, "KEYWORD_FOR"},
        {TokenType::KEYWORD_IN, "KEYWORD_IN"},
        {TokenType::KEYWORD_RETURN, "KEYWORD_RETURN"},
        {TokenType::KEYWORD_BREAK, "KEYWORD_BREAK"},
        {TokenType::KEYWORD_CONTINUE, "KEYWORD_CONTINUE"},
        {TokenType::KEYWORD_VAR, "KEYWORD_VAR"},
        {TokenType::KEYWORD_CONST, "KEYWORD_CONST"},
        {TokenType::KEYWORD_FN, "KEYWORD_FN"},
        {TokenType::KEYWORD_CLASS, "KEYWORD_CLASS"},
        {TokenType::KEYWORD_TEMPLATE, "KEYWORD_TEMPLATE"},
        {TokenType::KEYWORD_IMPORT, "KEYWORD_IMPORT"},
        {TokenType::KEYWORD_SMUGGLE, "KEYWORD_SMUGGLE"},
        {TokenType::KEYWORD_OPERATOR, "KEYWORD_OPERATOR"},
        {TokenType::KEYWORD_ASYNC, "KEYWORD_ASYNC"},
        {TokenType::KEYWORD_AWAIT, "KEYWORD_AWAIT"},
        {TokenType::KEYWORD_TRY, "KEYWORD_TRY"},
        {TokenType::KEYWORD_CATCH, "KEYWORD_CATCH"},
        {TokenType::KEYWORD_FINALLY, "KEYWORD_FINALLY"},
        {TokenType::KEYWORD_DEFER, "KEYWORD_DEFER"},
        {TokenType::KEYWORD_MATCH, "KEYWORD_MATCH"},
        {TokenType::KEYWORD_SCOPED, "KEYWORD_SCOPED"},
        {TokenType::KEYWORD_REF, "KEYWORD_REF"},
        {TokenType::UNDERSCORE, "UNDERSCORE"},
        {TokenType::INDENT, "INDENT"},
        {TokenType::DEDENT, "DEDENT"},
        {TokenType::COMMENT, "COMMENT"},
        {TokenType::NEWLINE, "NEWLINE"}
    };
    auto it = token_map.find(type);
    return it != token_map.end() ? it->second : "UNKNOWN";
}

} // namespace Vyn