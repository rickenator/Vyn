#include "vyn/lexer.hpp"
#include "vyn/token.hpp" // Ensure vyn::token_type_to_string is available
#include "vyn/source_location.hpp"   // Required for vyn::SourceLocation
#include <stdexcept>
#include <iostream>
#include <functional>
#include <algorithm> // Required for std::find
#include <unordered_map> // Required for std::unordered_map

Lexer::Lexer(const std::string& source, const std::string& filePath) 
    : source_(source), current_file_path_(filePath), pos_(0), line_(1), column_(1), indent_levels_({0}), nesting_level_(0) {
}

std::vector<vyn::token::Token> Lexer::tokenize() {
  std::vector<vyn::token::Token> tokens;

  while (pos_ < source_.size()) {
    char c = source_[pos_];
    unsigned int current_line_start_for_token = static_cast<unsigned int>(line_);
    unsigned int current_column_start_for_token = static_cast<unsigned int>(column_);

    if (c == '\r') { 
      pos_++;
      continue;
    }

    if (c == '\n') { 
      handle_newline(tokens); // handle_newline will use its own line_ and column_ for tokens it creates
      continue; 
    }

    // Skip single-line comments starting with #
    if (c == '#') {
      consume_while([](char c_comment) { return c_comment != '\n'; });
      // No token emitted for # comments, column advances with pos_ in consume_while
      // The newline will be handled by the next loop iteration.
      continue;
    }

    if (c == ' ' || c == '\t') { 
      if (c == '\t') { 
        throw std::runtime_error("Tabs not allowed at line " + std::to_string(line_) +
                                 ", column " + std::to_string(column_));
      }
      pos_++;
      column_++;
      continue;
    }

    if (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '/') {
      std::string comment_text = consume_while([](char c_comment_slash) { return c_comment_slash != '\n'; }); 
      tokens.emplace_back(vyn::TokenType::COMMENT, "//" + comment_text, vyn::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
      column_ += (2 + comment_text.size()); // Advance column for // and comment text
      continue;
    }

    if (is_letter(c)) {
      std::string word = consume_while([this](char c_id) { return is_letter(c_id) || is_digit(c_id) || c_id == '_'; });
      vyn::TokenType type = get_keyword_type(word);
      tokens.emplace_back(type, word, vyn::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
      column_ += word.size();
      continue;
    }

    if (is_digit(c)) {
      std::string int_part_str = consume_while([this](char char_digit_pred) {
        return is_digit(char_digit_pred);
      });

      // Check for range operator ".."
      // pos_ is at the character immediately after int_part_str
      if (pos_ + 1 < source_.size() && source_[pos_] == '.' && source_[pos_ + 1] == '.') {
        tokens.emplace_back(vyn::TokenType::INT_LITERAL, int_part_str, vyn::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
        column_ += int_part_str.size();
        continue;
      }
      // Check for float: . followed by a digit
      else if (pos_ < source_.size() && source_[pos_] == '.' &&
               pos_ + 1 < source_.size() && is_digit(source_[pos_ + 1])) {
        std::string float_str = int_part_str;
        float_str += source_[pos_]; 
        pos_++; 

        std::string decimal_part_str = consume_while([this](char char_digit_pred_float) {
          return is_digit(char_digit_pred_float);
        });
        float_str += decimal_part_str;

        // Check for another dot, which would be invalid (e.g., 1.2.3)
        // pos_ is now after the decimal part.
        if (pos_ < source_.size() && source_[pos_] == '.') {
             throw std::runtime_error("Invalid number format (multiple dots in float): " + float_str + "." + " at line " + std::to_string(line_) + ", column " + std::to_string(column_ + float_str.size()));
        }

        tokens.emplace_back(vyn::TokenType::FLOAT_LITERAL, float_str, vyn::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
        column_ += float_str.size();
        continue;
      }
      else if (pos_ < source_.size() && source_[pos_] == '.') {
          throw std::runtime_error("Invalid number format (trailing dot): " + int_part_str + "." + " at line " + std::to_string(line_) + ", column " + std::to_string(column_ + int_part_str.size()));
      }
      else {
        tokens.emplace_back(vyn::TokenType::INT_LITERAL, int_part_str, vyn::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
        column_ += int_part_str.size();
        continue;
      }
    }

    if (c == '\"') { // Escaped quote: \"
      pos_++; // Consume opening quote
      // current_column_start_for_token is before the opening quote
      std::string str_value = consume_while([](char c_str) { return c_str != '\"'; });
      if (pos_ >= source_.size() || source_[pos_] != '\"') {
          throw std::runtime_error("Unterminated string literal at line " + std::to_string(current_line_start_for_token) + ", column " + std::to_string(current_column_start_for_token));
      }
      tokens.emplace_back(vyn::TokenType::STRING_LITERAL, str_value, vyn::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
      pos_++; // Consume closing quote
      column_ += str_value.size() + 2; // +2 for the quotes
      continue;
    }

    // Helper for single/double char tokens
    auto emit_token = [&](vyn::TokenType type, const std::string& lexeme_val) {
        tokens.emplace_back(type, lexeme_val, vyn::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
        pos_ += lexeme_val.size();
        column_ += lexeme_val.size();
    };
    
    // Store pos and column before potential multi-char token parsing
    size_t original_pos = pos_;
    int original_column = column_;

    switch (c) {
      case '(': emit_token(vyn::TokenType::LPAREN, "("); nesting_level_++; break; 
      case ')': emit_token(vyn::TokenType::RPAREN, ")"); nesting_level_--; break; 
      case '[': emit_token(vyn::TokenType::LBRACKET, "["); nesting_level_++; break; 
      case ']': emit_token(vyn::TokenType::RBRACKET, "]"); nesting_level_--; break; 
      case '{': emit_token(vyn::TokenType::LBRACE, "{"); nesting_level_++; break; 
      case '}': emit_token(vyn::TokenType::RBRACE, "}"); nesting_level_--; break; 
      case ',': emit_token(vyn::TokenType::COMMA, ","); break; 
      case '.': 
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '.') {
          emit_token(vyn::TokenType::DOTDOT, "..");
        } else {
          emit_token(vyn::TokenType::DOT, ".");
        }
        break;
      case ':':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == ':') {
          emit_token(vyn::TokenType::COLONCOLON, "::");
        } else {
          emit_token(vyn::TokenType::COLON, ":");
        }
        break;
      case '=':
        if (pos_ + 1 < source_.size()) {
          if (source_[pos_ + 1] == '=') { 
            emit_token(vyn::TokenType::EQEQ, "==");
          } else if (source_[pos_ + 1] == '>') { 
            emit_token(vyn::TokenType::FAT_ARROW, "=>");
          } else { 
            emit_token(vyn::TokenType::EQ, "=");
          }
        } else { 
          emit_token(vyn::TokenType::EQ, "=");
        }
        break;
      case '!':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          emit_token(vyn::TokenType::NOTEQ, "!=");
        } else {
          emit_token(vyn::TokenType::BANG, "!");
        }
        break;
      case '<':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          emit_token(vyn::TokenType::LTEQ, "<=");
        } else {
          emit_token(vyn::TokenType::LT, "<");
        }
        break;
      case '>':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          emit_token(vyn::TokenType::GTEQ, ">=");
        } else {
          emit_token(vyn::TokenType::GT, ">");
        }
        break;
      case '+': emit_token(vyn::TokenType::PLUS, "+"); break; 
      case '*': emit_token(vyn::TokenType::MULTIPLY, "*"); break; 
      case '/': 
        // This case is for division. Comments (//) are handled earlier.
        emit_token(vyn::TokenType::DIVIDE, "/"); 
        break;
      case '&':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '&') {
          emit_token(vyn::TokenType::AND, "&&");
        } else {
          emit_token(vyn::TokenType::AMPERSAND, "&");
        }
        break;
      case '-':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '>') {
          emit_token(vyn::TokenType::ARROW, "->");
        } else {
          emit_token(vyn::TokenType::MINUS, "-");
        }
        break;
      case ';': emit_token(vyn::TokenType::SEMICOLON, ";"); break; 
      case '@': emit_token(vyn::TokenType::AT, "@"); break; 
      case '_': emit_token(vyn::TokenType::UNDERSCORE, "_"); break; 
      default:
        // If no token was emitted by the switch, pos_ and column_ were not advanced by emit_token
        // Restore them to ensure the error message is correct and to avoid infinite loops if pos_ didn't change.
        pos_ = original_pos;
        column_ = original_column;
        throw std::runtime_error("Unexpected character: " + std::string(1, source_[pos_]) +
                                 " at line " + std::to_string(line_) +
                                 ", column " + std::to_string(column_));
    }
  }

  unsigned int last_line_for_dedent = static_cast<unsigned int>(line_); // Use the line number of the last actual content or EOF
  // unsigned int last_col_for_dedent = static_cast<unsigned int>(column_); // Not directly used for DEDENT token's own location column.
  if (!tokens.empty() && tokens.back().type != vyn::TokenType::NEWLINE && tokens.back().type != vyn::TokenType::INDENT && tokens.back().type != vyn::TokenType::DEDENT) {
      last_line_for_dedent = tokens.back().location.line;
      // The column for DEDENT should be 1, but the location object itself might need to reflect the end of the last token or start of the new line.
      // For simplicity, using 1 as the column for the DEDENT token itself.
      // The SourceLocation for DEDENT will point to the start of the line where the DEDENT is conceptually inserted.
  }


  while (indent_levels_.size() > 1) {
    tokens.emplace_back(vyn::TokenType::DEDENT, "", vyn::SourceLocation{current_file_path_, last_line_for_dedent, 1}); // DEDENTs are at column 1 of their effective line
    indent_levels_.pop_back();
  }

  tokens.emplace_back(vyn::TokenType::END_OF_FILE, "", vyn::SourceLocation{current_file_path_, static_cast<unsigned int>(line_), static_cast<unsigned int>(column_)});

  return tokens;
}

void Lexer::handle_newline(std::vector<vyn::token::Token>& tokens) {
  // Current pos_ is at '\n'. line_ and column_ are for the '\n' itself.
  // int newline_char_line = line_; // unused
  // int newline_char_col = column_; // unused

  pos_++; 
  line_++;
  column_ = 1;
  unsigned int current_line_unsigned = static_cast<unsigned int>(line_);


  if (nesting_level_ > 0) {
    
    size_t original_column_on_new_line = column_; // Should be 1
    size_t spaces_skipped = 0;
    size_t temp_pos_for_check = pos_; 

    while (temp_pos_for_check < source_.size() && source_[temp_pos_for_check] == ' ') {
        temp_pos_for_check++;
        spaces_skipped++;
    }

    if (temp_pos_for_check < source_.size() && source_[temp_pos_for_check] == '\t') { 
        throw std::runtime_error("Tabs not allowed at line " + std::to_string(line_) +
                                 ", column " + std::to_string(original_column_on_new_line + spaces_skipped));
    }

    bool is_nested_blank_or_comment = true;
    if (temp_pos_for_check < source_.size()) { 
        char first_char_after_spaces = source_[temp_pos_for_check];
        if (first_char_after_spaces == '#') { 
            is_nested_blank_or_comment = true;
        } else if (first_char_after_spaces == '/' && temp_pos_for_check + 1 < source_.size() && source_[temp_pos_for_check + 1] == '/') { 
            is_nested_blank_or_comment = true;
        } else if (first_char_after_spaces == '\n' || first_char_after_spaces == '\r') { 
             is_nested_blank_or_comment = true;
        }
        else {
            is_nested_blank_or_comment = false;
        }
    } else { 
        is_nested_blank_or_comment = true;
    }

    if (!is_nested_blank_or_comment) {
        // NEWLINE token refers to the line it *introduces*
        tokens.emplace_back(vyn::TokenType::NEWLINE, "", vyn::SourceLocation{current_file_path_, current_line_unsigned, 1});
    } 
    
    pos_ += spaces_skipped; // Advance main lexer position past the leading spaces
    column_ += spaces_skipped; // Advance main lexer column past the leading spaces
    return; 
  }

  size_t indent_count = 0;
  size_t temp_pos_for_indent_check = pos_; 

  while (temp_pos_for_indent_check < source_.size()) {
    char char_on_this_line = source_[temp_pos_for_indent_check];
    if (char_on_this_line == ' ') {
      indent_count++;
    } else if (char_on_this_line == '\t') { 
      throw std::runtime_error("Tabs not allowed at line " + std::to_string(line_) +
                               ", column " + std::to_string(1 + indent_count)); 
    } else {
      break;
    }
    temp_pos_for_indent_check++; // Consume the space/tab
  }
  
  // After counting spaces, check if the line is blank or starts with a comment
  bool line_is_blank_or_comment = false;
  if (temp_pos_for_indent_check == source_.size() || // EOF
      source_[temp_pos_for_indent_check] == '\n' || // Another newline (blank line)
      source_[temp_pos_for_indent_check] == '\r' || // Carriage return
      source_[temp_pos_for_indent_check] == '#' || // Hash comment
      (source_[temp_pos_for_indent_check] == '/' && temp_pos_for_indent_check + 1 < source_.size() && source_[temp_pos_for_indent_check+1] == '/') // Slash comment
     ) {
      line_is_blank_or_comment = true;
  }


  if (line_is_blank_or_comment) {
      // For blank lines or lines with only comments, we don\'t emit NEWLINE, INDENT, or DEDENT.
      // We just advance pos_ and column_ past the counted indent spaces.
      pos_ += indent_count;
      column_ += indent_count;
      // The rest of the line (comment or newline char) will be handled by the main loop.
      return;
  }

  // If not blank or comment, then this line has actual code.
  // Emit NEWLINE for the previous line ending.
  // The NEWLINE token\'s location is the start of the new line it introduces.
  tokens.emplace_back(vyn::TokenType::NEWLINE, "", vyn::SourceLocation{current_file_path_, current_line_unsigned, 1});


  if (indent_count > indent_levels_.back()) {
    indent_levels_.push_back(indent_count);
    tokens.emplace_back(vyn::TokenType::INDENT, "", vyn::SourceLocation{current_file_path_, current_line_unsigned, 1});
  } else if (indent_count < indent_levels_.back()) {
    while (indent_count < indent_levels_.back()) {
      if (std::find(indent_levels_.begin(), indent_levels_.end(), indent_count) == indent_levels_.end()) {
          throw std::runtime_error("Indentation error: inconsistent dedent to level " + 
                                   std::to_string(indent_count) + " at line " + std::to_string(line_) +
                                   ". Valid previous indent levels: " + [&](){
                                       std::string s;
                                       for(size_t l_idx = 0; l_idx < indent_levels_.size(); ++l_idx) {
                                           s += std::to_string(indent_levels_[l_idx]) + (l_idx < indent_levels_.size() - 1 ? " " : "");
                                       }
                                       return s;
                                   }());
      }
      indent_levels_.pop_back();
      tokens.emplace_back(vyn::TokenType::DEDENT, "", vyn::SourceLocation{current_file_path_, current_line_unsigned, 1});
    }
    if (indent_count != indent_levels_.back()) { // Should not happen if previous check is correct
        throw std::runtime_error("Indentation error: dedent to unaligned level " + 
                                 std::to_string(indent_count) + " at line " + std::to_string(line_));
    }
  }
  // If indent_count == indent_levels_.back(), no change in indentation level.

  pos_ += indent_count; // Consume the leading spaces
  column_ += indent_count; 
}

vyn::TokenType Lexer::get_keyword_type(const std::string& word) {
    static const std::unordered_map<std::string, vyn::TokenType> keywords = {
        {"let", vyn::TokenType::KEYWORD_LET},
        {"var", vyn::TokenType::KEYWORD_VAR},
        {"const", vyn::TokenType::KEYWORD_CONST},
        {"if", vyn::TokenType::KEYWORD_IF},
        {"else", vyn::TokenType::KEYWORD_ELSE},
        {"while", vyn::TokenType::KEYWORD_WHILE},
        {"for", vyn::TokenType::KEYWORD_FOR},
        {"return", vyn::TokenType::KEYWORD_RETURN},
        {"break", vyn::TokenType::KEYWORD_BREAK},
        {"continue", vyn::TokenType::KEYWORD_CONTINUE},
        {"null", vyn::TokenType::KEYWORD_NULL},
        {"true", vyn::TokenType::KEYWORD_TRUE},
        {"false", vyn::TokenType::KEYWORD_FALSE},
        {"fn", vyn::TokenType::KEYWORD_FN},
        {"struct", vyn::TokenType::KEYWORD_STRUCT},
        {"enum", vyn::TokenType::KEYWORD_ENUM},
        {"trait", vyn::TokenType::KEYWORD_TRAIT},
        {"impl", vyn::TokenType::KEYWORD_IMPL},
        {"type", vyn::TokenType::KEYWORD_TYPE},
        {"module", vyn::TokenType::KEYWORD_MODULE},
        {"use", vyn::TokenType::KEYWORD_USE},
        {"pub", vyn::TokenType::KEYWORD_PUB},
        {"mut", vyn::TokenType::KEYWORD_MUT},
        {"try", vyn::TokenType::KEYWORD_TRY},
        {"catch", vyn::TokenType::KEYWORD_CATCH},
        {"finally", vyn::TokenType::KEYWORD_FINALLY},
        {"defer", vyn::TokenType::KEYWORD_DEFER},
        {"match", vyn::TokenType::KEYWORD_MATCH},
        {"scoped", vyn::TokenType::KEYWORD_SCOPED},
        {"ref", vyn::TokenType::KEYWORD_REF},
        {"extern", vyn::TokenType::KEYWORD_EXTERN},
        {"as", vyn::TokenType::KEYWORD_AS},
        {"in", vyn::TokenType::KEYWORD_IN},
        {"class", vyn::TokenType::KEYWORD_CLASS},
        {"template", vyn::TokenType::KEYWORD_TEMPLATE},
        {"import", vyn::TokenType::KEYWORD_IMPORT},
        {"smuggle", vyn::TokenType::KEYWORD_SMUGGLE},
        {"await", vyn::TokenType::KEYWORD_AWAIT},
        {"async", vyn::TokenType::KEYWORD_ASYNC},
        {"operator", vyn::TokenType::KEYWORD_OPERATOR},
        {"my", vyn::TokenType::KEYWORD_MY},
        {"our", vyn::TokenType::KEYWORD_OUR},
        {"their", vyn::TokenType::KEYWORD_THEIR},
        {"ptr", vyn::TokenType::KEYWORD_PTR},
        {"borrow", vyn::TokenType::KEYWORD_BORROW},
        {"view", vyn::TokenType::KEYWORD_VIEW},
        {"nil", vyn::TokenType::KEYWORD_NIL}
    };

    auto it = keywords.find(word);
    if (it != keywords.end()) {
        return it->second;
    }
    return vyn::TokenType::IDENTIFIER;
}

bool Lexer::is_letter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool Lexer::is_digit(char c) {
  return c >= '0' && c <= '9';
}

/*
std::string Lexer::token_type_to_string(Vyn::TokenType type)
This function is now globally available as vyn::token_type_to_string
from "vyn/token.hpp" and implemented in "vyn/token.cpp".
The Lexer should use that one if it ever needs to generate a TokenType to a string,
though typically it just produces tokens with TokenType, not their string representations.
*/
