#ifndef VYN_PARSER_LEXER_HPP
#define VYN_PARSER_LEXER_HPP

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include "token.hpp" // Provides vyn::token::Token and vyn::TokenType
#include "source_location.hpp" // Provides vyn::SourceLocation

// class Lexer is in the global namespace
class Lexer {
public:
  explicit Lexer(const std::string& source, const std::string& filePath); // Added filePath
  std::vector<vyn::token::Token> tokenize(); // Changed Token to vyn::token::Token

private:
  std::string consume_while(std::function<bool(char)> pred) {
    std::string result;
    while (pos_ < source_.size() && pred(source_[pos_])) {
      result += source_[pos_];
      pos_++;
      // Do not increment column_ here, it's handled by the caller
      // or by the specific logic within tokenize() after calling consume_while.
    }
    return result;
  }
  bool is_letter(char c);
  bool is_digit(char c);
  vyn::TokenType get_keyword_type(const std::string& word); // Corrected namespace
  // Removed: std::string token_type_to_string(vyn::TokenType type); - Use vyn::token_type_to_string from token.hpp/token.cpp
  void handle_newline(std::vector<vyn::token::Token>& tokens); // Changed Token to vyn::token::Token

  std::string source_;
  std::string current_file_path_; // Added filePath member
  size_t pos_;
  int line_;
  int column_;
  std::vector<int> indent_levels_;
  int nesting_level_; 
};

#endif // VYN_PARSER_LEXER_HPP