#ifndef VYN_LEXER_HPP
#define VYN_LEXER_HPP

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include "token.hpp" // Provides Vyn::Token and Vyn::TokenType

// class Lexer is in the global namespace
class Lexer {
public:
  explicit Lexer(const std::string& source);
  std::vector<Vyn::Token> tokenize(); // Changed Token to Vyn::Token

private:
  std::string consume_while(std::function<bool(char)> pred);
  bool is_letter(char c);
  bool is_digit(char c);
  Vyn::TokenType get_keyword_type(const std::string& word); // Changed TokenType to Vyn::TokenType
  // Removed: std::string token_type_to_string(Vyn::TokenType type); - Use Vyn::token_type_to_string from token.hpp/token.cpp
  void handle_newline(std::vector<Vyn::Token>& tokens); // Changed Token to Vyn::Token

  std::string source_;
  size_t pos_;
  int line_;
  int column_;
  std::vector<int> indent_levels_;
  int nesting_level_; 
};

#endif