#include "vyn/token.hpp"

std::string token_type_to_string(TokenType type) {
    switch (type) {
        case TokenType::EOF_TOKEN: return "EOF_TOKEN";
        case TokenType::INDENT: return "INDENT";
        case TokenType::DEDENT: return "DEDENT";
        case TokenType::KEYWORD_FN: return "KEYWORD_FN";
        case TokenType::KEYWORD_IF: return "KEYWORD_IF";
        case TokenType::KEYWORD_ELSE: return "KEYWORD_ELSE";
        case TokenType::KEYWORD_VAR: return "KEYWORD_VAR";
        case TokenType::KEYWORD_CONST: return "KEYWORD_CONST";
        case TokenType::KEYWORD_TEMPLATE: return "KEYWORD_TEMPLATE";
        case TokenType::KEYWORD_CLASS: return "KEYWORD_CLASS";
        case TokenType::KEYWORD_RETURN: return "KEYWORD_RETURN";
        case TokenType::KEYWORD_FOR: return "KEYWORD_FOR";
        case TokenType::KEYWORD_IN: return "KEYWORD_IN";
        case TokenType::KEYWORD_SCOPED: return "KEYWORD_SCOPED";
        case TokenType::KEYWORD_IMPORT: return "KEYWORD_IMPORT";
        case TokenType::KEYWORD_SMUGGLE: return "KEYWORD_SMUGGLE";
        case TokenType::KEYWORD_TRY: return "KEYWORD_TRY";
        case TokenType::KEYWORD_CATCH: return "KEYWORD_CATCH";
        case TokenType::KEYWORD_FINALLY: return "KEYWORD_FINALLY";
        case TokenType::KEYWORD_DEFER: return "KEYWORD_DEFER";
        case TokenType::KEYWORD_ASYNC: return "KEYWORD_ASYNC";
        case TokenType::KEYWORD_AWAIT: return "KEYWORD_AWAIT";
        case TokenType::KEYWORD_MATCH: return "KEYWORD_MATCH";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::INT_LITERAL: return "INT_LITERAL";
        case TokenType::FLOAT_LITERAL: return "FLOAT_LITERAL";
        case TokenType::STRING_LITERAL: return "STRING_LITERAL";
        case TokenType::COMMENT: return "COMMENT";
        case TokenType::LBRACE: return "LBRACE";
        case TokenType::RBRACE: return "RBRACE";
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::LBRACKET: return "LBRACKET";
        case TokenType::RBRACKET: return "RBRACKET";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::COLON: return "COLON";
        case TokenType::COLONCOLON: return "COLONCOLON";
        case TokenType::COMMA: return "COMMA";
        case TokenType::EQ: return "EQ";
        case TokenType::EQEQ: return "EQEQ";
        case TokenType::FAT_ARROW: return "FAT_ARROW";
        case TokenType::LT: return "LT";
        case TokenType::GT: return "GT";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::DIVIDE: return "DIVIDE";
        case TokenType::MULTIPLY: return "MULTIPLY";
        case TokenType::ARROW: return "ARROW";
        case TokenType::DOT: return "DOT";
        case TokenType::DOTDOT: return "DOTDOT";
        case TokenType::AND: return "AND";
        case TokenType::AMPERSAND: return "AMPERSAND";
        case TokenType::BANG: return "BANG";
        case TokenType::AT: return "AT";
        default: return "UNKNOWN";
    }
}