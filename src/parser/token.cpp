// SPDX-License-Identifier: Apache-2.0

#include "vyb/parser/token.hpp"
#include "vyb/parser/ast.hpp" // Required for the definition of SourceLocation
#include <unordered_map> // Added for std::unordered_map
#include <string> // Added for std::string
#include <sstream> // For Token::toString

// The function is defined within the Vyb namespace if the header declares it so.
// If token.hpp declares `namespace Vyb { std::string token_type_to_string(TokenType type); }`
// then this definition is correct.
// If token.hpp declares `std::string token_type_to_string(Vyb::TokenType type);` at global scope,
// then this definition is also correct.
// Assuming it's part of the Vyb namespace as per previous discussions.

namespace vyb { // Changed from Vyb to vyb to match token.hpp

std::string token_type_to_string(vyb::TokenType type) { // Corrected namespace for TokenType
    static const std::unordered_map<vyb::TokenType, std::string> token_map = { // Corrected namespace for TokenType
        // Literals
        {vyb::TokenType::IDENTIFIER, "IDENTIFIER"},
        {vyb::TokenType::INT_LITERAL, "INT_LITERAL"},
        {vyb::TokenType::FLOAT_LITERAL, "FLOAT_LITERAL"},
        {vyb::TokenType::STRING_LITERAL, "STRING_LITERAL"},
        {vyb::TokenType::CHAR_LITERAL, "CHAR_LITERAL"},

        // Keywords
        {vyb::TokenType::KEYWORD_LET, "KEYWORD_LET"},
        {vyb::TokenType::KEYWORD_VAR, "KEYWORD_VAR"},
        {vyb::TokenType::KEYWORD_CONST, "KEYWORD_CONST"},
        {vyb::TokenType::KEYWORD_IF, "KEYWORD_IF"},
        {vyb::TokenType::KEYWORD_ELSE, "KEYWORD_ELSE"},
        {vyb::TokenType::KEYWORD_WHILE, "KEYWORD_WHILE"},
        {vyb::TokenType::KEYWORD_FOR, "KEYWORD_FOR"},
        {vyb::TokenType::KEYWORD_RETURN, "KEYWORD_RETURN"},
        {vyb::TokenType::KEYWORD_BREAK, "KEYWORD_BREAK"},
        {vyb::TokenType::KEYWORD_CONTINUE, "KEYWORD_CONTINUE"},
        {vyb::TokenType::KEYWORD_NULL, "KEYWORD_NULL"},
        {vyb::TokenType::KEYWORD_TRUE, "KEYWORD_TRUE"},
        {vyb::TokenType::KEYWORD_FALSE, "KEYWORD_FALSE"},
        {vyb::TokenType::KEYWORD_FN, "KEYWORD_FN"},
        {vyb::TokenType::KEYWORD_STRUCT, "KEYWORD_STRUCT"},
        {vyb::TokenType::KEYWORD_ENUM, "KEYWORD_ENUM"},
        {vyb::TokenType::KEYWORD_ASPECT, "KEYWORD_ASPECT"},
        {vyb::TokenType::KEYWORD_BIND, "KEYWORD_BIND"},
        {vyb::TokenType::KEYWORD_TYPE, "KEYWORD_TYPE"},
        {vyb::TokenType::KEYWORD_MODULE, "KEYWORD_MODULE"},
        {vyb::TokenType::KEYWORD_USE, "KEYWORD_USE"},
        {vyb::TokenType::KEYWORD_PUB, "KEYWORD_PUB"},
        {vyb::TokenType::KEYWORD_MUT, "KEYWORD_MUT"},
        {vyb::TokenType::KEYWORD_TRY, "KEYWORD_TRY"},
        {vyb::TokenType::KEYWORD_CATCH, "KEYWORD_CATCH"},
        {vyb::TokenType::KEYWORD_FINALLY, "KEYWORD_FINALLY"},
        {vyb::TokenType::KEYWORD_DEFER, "KEYWORD_DEFER"},
        {vyb::TokenType::KEYWORD_MATCH, "KEYWORD_MATCH"},
        {vyb::TokenType::KEYWORD_SELECT, "KEYWORD_SELECT"},
        {vyb::TokenType::KEYWORD_PASS, "KEYWORD_PASS"},
        {vyb::TokenType::KEYWORD_SCOPED, "KEYWORD_SCOPED"},
        {vyb::TokenType::KEYWORD_REF, "KEYWORD_REF"},
        {vyb::TokenType::KEYWORD_EXTERN, "KEYWORD_EXTERN"},
        {vyb::TokenType::KEYWORD_AS, "KEYWORD_AS"},
        {vyb::TokenType::KEYWORD_IN, "KEYWORD_IN"},
        {vyb::TokenType::KEYWORD_CLASS, "KEYWORD_CLASS"},
        {vyb::TokenType::KEYWORD_TEMPLATE, "KEYWORD_TEMPLATE"},
        {vyb::TokenType::KEYWORD_IMPORT, "KEYWORD_IMPORT"},
        {vyb::TokenType::KEYWORD_SMUGGLE, "KEYWORD_SMUGGLE"},
        {vyb::TokenType::KEYWORD_FROM, "KEYWORD_FROM"},
        {vyb::TokenType::KEYWORD_AWAIT, "KEYWORD_AWAIT"},
        {vyb::TokenType::KEYWORD_ASYNC, "KEYWORD_ASYNC"},
        {vyb::TokenType::KEYWORD_OPERATOR, "KEYWORD_OPERATOR"},
        {vyb::TokenType::KEYWORD_MY, "KEYWORD_MY"},
        {vyb::TokenType::KEYWORD_OUR, "KEYWORD_OUR"},
        {vyb::TokenType::KEYWORD_THEIR, "KEYWORD_THEIR"},
        {vyb::TokenType::KEYWORD_MILD, "KEYWORD_MILD"},
        {vyb::TokenType::KEYWORD_PTR, "KEYWORD_PTR"},
        {vyb::TokenType::KEYWORD_BORROW, "KEYWORD_BORROW"},
        {vyb::TokenType::KEYWORD_VIEW, "KEYWORD_VIEW"},
        {vyb::TokenType::KEYWORD_NIL, "KEYWORD_NIL"},
        {vyb::TokenType::KEYWORD_FREEDOM, "KEYWORD_FREEDOM"},
        {vyb::TokenType::KEYWORD_AUTO, "KEYWORD_AUTO"},
        {vyb::TokenType::KEYWORD_FAIL, "KEYWORD_FAIL"},
        {vyb::TokenType::KEYWORD_TRAP, "KEYWORD_TRAP"},
        {vyb::TokenType::KEYWORD_REFAIL, "KEYWORD_REFAIL"},
        {vyb::TokenType::KEYWORD_ENSURE, "KEYWORD_ENSURE"},
        {vyb::TokenType::KEYWORD_PANIC, "KEYWORD_PANIC"},
        {vyb::TokenType::KEYWORD_TYPEOF, "KEYWORD_TYPEOF"},
        {vyb::TokenType::KEYWORD_TYPENAME, "KEYWORD_TYPENAME"},

        // Operators
        {vyb::TokenType::PLUS, "PLUS"},
        {vyb::TokenType::MINUS, "MINUS"},
        {vyb::TokenType::MULTIPLY, "MULTIPLY"},
        {vyb::TokenType::DIVIDE, "DIVIDE"},
        {vyb::TokenType::MODULO, "MODULO"},
        {vyb::TokenType::EQ, "EQ"},
        {vyb::TokenType::EQEQ, "EQEQ"},
        {vyb::TokenType::NOTEQ, "NOTEQ"},
        {vyb::TokenType::LT, "LT"},
        {vyb::TokenType::GT, "GT"},
        {vyb::TokenType::LTEQ, "LTEQ"},
        {vyb::TokenType::GTEQ, "GTEQ"},
        {vyb::TokenType::AND, "AND"}, // Logical AND
        {vyb::TokenType::OR, "OR"},   // Logical OR
        {vyb::TokenType::BANG, "BANG"},
        {vyb::TokenType::AMPERSAND, "AMPERSAND"}, // Bitwise AND
        {vyb::TokenType::PIPE, "PIPE"},           // Bitwise OR
        {vyb::TokenType::CARET, "CARET"},         // Bitwise XOR
        {vyb::TokenType::TILDE, "TILDE"},         // Bitwise NOT
        {vyb::TokenType::LSHIFT, "LSHIFT"},
        {vyb::TokenType::RSHIFT, "RSHIFT"},
        {vyb::TokenType::DOTDOT, "DOTDOT"},
        {vyb::TokenType::ELLIPSIS, "ELLIPSIS"},

        // Compound Assignment Operators
        {vyb::TokenType::PLUSEQ, "PLUSEQ"},
        {vyb::TokenType::MINUSEQ, "MINUSEQ"},
        {vyb::TokenType::MULTIPLYEQ, "MULTIPLYEQ"},
        {vyb::TokenType::DIVEQ, "DIVEQ"},
        {vyb::TokenType::MODEQ, "MODEQ"},
        {vyb::TokenType::LSHIFTEQ, "LSHIFTEQ"},
        {vyb::TokenType::RSHIFTEQ, "RSHIFTEQ"},
        {vyb::TokenType::BITWISEANDEQ, "BITWISEANDEQ"},
        {vyb::TokenType::BITWISEOREQ, "BITWISEOREQ"},
        {vyb::TokenType::BITWISEXOREQ, "BITWISEXOREQ"},
        {vyb::TokenType::COLONEQ, "COLONEQ"},


        // Punctuation
        {vyb::TokenType::LPAREN, "LPAREN"},
        {vyb::TokenType::RPAREN, "RPAREN"},
        {vyb::TokenType::LBRACE, "LBRACE"},
        {vyb::TokenType::RBRACE, "RBRACE"},
        {vyb::TokenType::LBRACKET, "LBRACKET"},
        {vyb::TokenType::RBRACKET, "RBRACKET"},
        {vyb::TokenType::COMMA, "COMMA"},
        {vyb::TokenType::DOT, "DOT"},
        {vyb::TokenType::COLON, "COLON"},
        {vyb::TokenType::SEMICOLON, "SEMICOLON"},
        {vyb::TokenType::ARROW, "ARROW"},
        {vyb::TokenType::FAT_ARROW, "FAT_ARROW"},
        {vyb::TokenType::COLONCOLON, "COLONCOLON"},
        {vyb::TokenType::AT, "AT"},
        {vyb::TokenType::HASH, "HASH"},
        {vyb::TokenType::UNDERSCORE, "UNDERSCORE"},
        {vyb::TokenType::QUESTION_MARK, "QUESTION_MARK"},

        // Misc
        {vyb::TokenType::UNKNOWN, "UNKNOWN"},
        {vyb::TokenType::END_OF_FILE, "END_OF_FILE"},
        {vyb::TokenType::COMMENT, "COMMENT"},
        {vyb::TokenType::NEWLINE, "NEWLINE"},
        {vyb::TokenType::INDENT, "INDENT"},
        {vyb::TokenType::DEDENT, "DEDENT"},
        {vyb::TokenType::ILLEGAL, "ILLEGAL"} // Added for illegal/error tokens
    };

    auto it = token_map.find(type);
    return it != token_map.end() ? it->second : "UNKNOWN"; // Default to "UNKNOWN"
}

namespace token { // Added to match token.hpp for class Token

Token::Token(vyb::TokenType type, const std::string& lexeme, const vyb::SourceLocation& loc)
    : type(type), lexeme(lexeme), location(loc) {}

std::string Token::toString() const {
    std::stringstream ss;
    ss << "Token(" << vyb::token_type_to_string(type) << ", \\\"" << lexeme << "\\\", "
       << location.line << ":" << location.column << ")";
    return ss.str();
}

} // namespace token
} // namespace vyb
