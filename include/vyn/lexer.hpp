#ifndef VYN_LEXER_HPP
#define VYN_LEXER_HPP

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include "token.hpp" // Added include for token.hpp

class Lexer {
public:
  explicit Lexer(const std::string& source);
  std::vector<Token> tokenize();

private:
  std::string consume_while(std::function<bool(char)> pred);
  bool is_letter(char c);
  bool is_digit(char c);
  TokenType get_keyword_type(const std::string& word);
  std::string token_type_to_string(TokenType type);
  void handle_newline(std::vector<Token>& tokens);

  std::string source_;
  size_t pos_;
  int line_;
  int column_;
  std::vector<int> indent_levels_;
  int nesting_level_; // Renamed from brace_depth_
};

#endif