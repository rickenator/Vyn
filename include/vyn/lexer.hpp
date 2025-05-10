#ifndef VYN_LEXER_HPP
#define VYN_LEXER_HPP

#include <string>
#include <vector>
#include <stack>
#include "token.hpp"

class Lexer {
public:
    Lexer(const std::string& source);
    std::vector<Token> tokenize();

private:
    void handle_whitespace(std::vector<Token>& tokens);
    Token read_identifier_or_keyword();
    Token read_number();
    Token read_string();
    Token read_comment();
    Token read_hash_comment();

    std::string source_;
    size_t pos_;
    int line_;
    int column_;
    int brace_depth_;
    std::stack<int> indent_stack_;
};

#endif // VYN_LEXER_HPP