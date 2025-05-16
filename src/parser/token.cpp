#include "vyn/parser/token.hpp"
#include "vyn/parser/ast.hpp" // Required for the definition of SourceLocation
#include <unordered_map> // Added for std::unordered_map
#include <string> // Added for std::string
#include <sstream> // For Token::toString

// The function is defined within the Vyn namespace if the header declares it so.
// If token.hpp declares `namespace Vyn { std::string token_type_to_string(TokenType type); }`
// then this definition is correct.
// If token.hpp declares `std::string token_type_to_string(Vyn::TokenType type);` at global scope,
// then this definition is also correct.
// Assuming it's part of the Vyn namespace as per previous discussions.

namespace vyn { // Changed from Vyn to vyn to match token.hpp

std::string token_type_to_string(vyn::TokenType type) { // Corrected namespace for TokenType
    static const std::unordered_map<vyn::TokenType, std::string> token_map = { // Corrected namespace for TokenType
        // Literals
        {vyn::TokenType::IDENTIFIER, "IDENTIFIER"},
        {vyn::TokenType::INT_LITERAL, "INT_LITERAL"},
        {vyn::TokenType::FLOAT_LITERAL, "FLOAT_LITERAL"},
        {vyn::TokenType::STRING_LITERAL, "STRING_LITERAL"},
        {vyn::TokenType::CHAR_LITERAL, "CHAR_LITERAL"},

        // Keywords
        {vyn::TokenType::KEYWORD_LET, "KEYWORD_LET"},
        {vyn::TokenType::KEYWORD_VAR, "KEYWORD_VAR"},
        {vyn::TokenType::KEYWORD_CONST, "KEYWORD_CONST"},
        {vyn::TokenType::KEYWORD_IF, "KEYWORD_IF"},
        {vyn::TokenType::KEYWORD_ELSE, "KEYWORD_ELSE"},
        {vyn::TokenType::KEYWORD_WHILE, "KEYWORD_WHILE"},
        {vyn::TokenType::KEYWORD_FOR, "KEYWORD_FOR"},
        {vyn::TokenType::KEYWORD_RETURN, "KEYWORD_RETURN"},
        {vyn::TokenType::KEYWORD_BREAK, "KEYWORD_BREAK"},
        {vyn::TokenType::KEYWORD_CONTINUE, "KEYWORD_CONTINUE"},
        {vyn::TokenType::KEYWORD_NULL, "KEYWORD_NULL"},
        {vyn::TokenType::KEYWORD_TRUE, "KEYWORD_TRUE"},
        {vyn::TokenType::KEYWORD_FALSE, "KEYWORD_FALSE"},
        {vyn::TokenType::KEYWORD_FN, "KEYWORD_FN"},
        {vyn::TokenType::KEYWORD_STRUCT, "KEYWORD_STRUCT"},
        {vyn::TokenType::KEYWORD_ENUM, "KEYWORD_ENUM"},
        {vyn::TokenType::KEYWORD_TRAIT, "KEYWORD_TRAIT"},
        {vyn::TokenType::KEYWORD_IMPL, "KEYWORD_IMPL"},
        {vyn::TokenType::KEYWORD_TYPE, "KEYWORD_TYPE"},
        {vyn::TokenType::KEYWORD_MODULE, "KEYWORD_MODULE"},
        {vyn::TokenType::KEYWORD_USE, "KEYWORD_USE"},
        {vyn::TokenType::KEYWORD_PUB, "KEYWORD_PUB"},
        {vyn::TokenType::KEYWORD_MUT, "KEYWORD_MUT"},
        {vyn::TokenType::KEYWORD_TRY, "KEYWORD_TRY"},
        {vyn::TokenType::KEYWORD_CATCH, "KEYWORD_CATCH"},
        {vyn::TokenType::KEYWORD_FINALLY, "KEYWORD_FINALLY"},
        {vyn::TokenType::KEYWORD_DEFER, "KEYWORD_DEFER"},
        {vyn::TokenType::KEYWORD_MATCH, "KEYWORD_MATCH"},
        {vyn::TokenType::KEYWORD_SCOPED, "KEYWORD_SCOPED"},
        {vyn::TokenType::KEYWORD_REF, "KEYWORD_REF"},
        {vyn::TokenType::KEYWORD_EXTERN, "KEYWORD_EXTERN"},
        {vyn::TokenType::KEYWORD_AS, "KEYWORD_AS"},
        {vyn::TokenType::KEYWORD_IN, "KEYWORD_IN"},
        {vyn::TokenType::KEYWORD_CLASS, "KEYWORD_CLASS"},
        {vyn::TokenType::KEYWORD_TEMPLATE, "KEYWORD_TEMPLATE"},
        {vyn::TokenType::KEYWORD_IMPORT, "KEYWORD_IMPORT"},
        {vyn::TokenType::KEYWORD_SMUGGLE, "KEYWORD_SMUGGLE"},
        {vyn::TokenType::KEYWORD_AWAIT, "KEYWORD_AWAIT"},
        {vyn::TokenType::KEYWORD_ASYNC, "KEYWORD_ASYNC"},
        {vyn::TokenType::KEYWORD_OPERATOR, "KEYWORD_OPERATOR"},

        // Operators
        {vyn::TokenType::PLUS, "PLUS"},
        {vyn::TokenType::MINUS, "MINUS"},
        {vyn::TokenType::MULTIPLY, "MULTIPLY"},
        {vyn::TokenType::DIVIDE, "DIVIDE"},
        {vyn::TokenType::MODULO, "MODULO"},
        {vyn::TokenType::EQ, "EQ"},
        {vyn::TokenType::EQEQ, "EQEQ"},
        {vyn::TokenType::NOTEQ, "NOTEQ"},
        {vyn::TokenType::LT, "LT"},
        {vyn::TokenType::GT, "GT"},
        {vyn::TokenType::LTEQ, "LTEQ"},
        {vyn::TokenType::GTEQ, "GTEQ"},
        {vyn::TokenType::AND, "AND"}, // Logical AND
        {vyn::TokenType::OR, "OR"},   // Logical OR
        {vyn::TokenType::BANG, "BANG"},
        {vyn::TokenType::AMPERSAND, "AMPERSAND"}, // Bitwise AND
        {vyn::TokenType::PIPE, "PIPE"},           // Bitwise OR
        {vyn::TokenType::CARET, "CARET"},         // Bitwise XOR
        {vyn::TokenType::TILDE, "TILDE"},         // Bitwise NOT
        {vyn::TokenType::LSHIFT, "LSHIFT"},
        {vyn::TokenType::RSHIFT, "RSHIFT"},
        {vyn::TokenType::DOTDOT, "DOTDOT"},

        // Punctuation
        {vyn::TokenType::LPAREN, "LPAREN"},
        {vyn::TokenType::RPAREN, "RPAREN"},
        {vyn::TokenType::LBRACE, "LBRACE"},
        {vyn::TokenType::RBRACE, "RBRACE"},
        {vyn::TokenType::LBRACKET, "LBRACKET"},
        {vyn::TokenType::RBRACKET, "RBRACKET"},
        {vyn::TokenType::COMMA, "COMMA"},
        {vyn::TokenType::DOT, "DOT"},
        {vyn::TokenType::COLON, "COLON"},
        {vyn::TokenType::SEMICOLON, "SEMICOLON"},
        {vyn::TokenType::ARROW, "ARROW"},
        {vyn::TokenType::FAT_ARROW, "FAT_ARROW"},
        {vyn::TokenType::COLONCOLON, "COLONCOLON"},
        {vyn::TokenType::AT, "AT"},
        {vyn::TokenType::UNDERSCORE, "UNDERSCORE"},

        // Misc
        {vyn::TokenType::UNKNOWN, "UNKNOWN"},
        {vyn::TokenType::END_OF_FILE, "END_OF_FILE"},
        {vyn::TokenType::COMMENT, "COMMENT"},
        {vyn::TokenType::NEWLINE, "NEWLINE"},
        {vyn::TokenType::INDENT, "INDENT"},
        {vyn::TokenType::DEDENT, "DEDENT"}
    };
    auto it = token_map.find(type);
    return it != token_map.end() ? it->second : "UNKNOWN"; // Default to "UNKNOWN"
}

namespace token { // Added to match token.hpp for class Token

Token::Token(vyn::TokenType type, const std::string& lexeme, const vyn::SourceLocation& loc)
    : type(type), lexeme(lexeme), location(loc) {}

std::string Token::toString() const {
    std::stringstream ss;
    ss << "Token(" << vyn::token_type_to_string(type) << ", \\\"" << lexeme << "\\\", "
       << location.line << ":" << location.column << ")";
    return ss.str();
}

} // namespace token
} // namespace vyn