#include "vyn/lexer.hpp"
#include "vyn/token.hpp" // Ensure Vyn::token_type_to_string is available
#include "vyn/ast.hpp"   // Required for Vyn::AST::SourceLocation
#include <stdexcept>
#include <iostream>
#include <functional>

Lexer::Lexer(const std::string& source) : source_(source), pos_(0), line_(1), column_(1), indent_levels_({0}), nesting_level_(0) {
}

std::vector<Vyn::Token> Lexer::tokenize() {
  std::vector<Vyn::Token> tokens;

  while (pos_ < source_.size()) {
    char c = source_[pos_];
    int current_line_start_for_token = line_;
    int current_column_start_for_token = column_;

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
      tokens.emplace_back(Vyn::TokenType::COMMENT, "//" + comment_text, current_line_start_for_token, current_column_start_for_token, Vyn::AST::SourceLocation{source_, current_line_start_for_token, current_column_start_for_token});
      column_ += (2 + comment_text.size()); // Advance column for // and comment text
      continue;
    }

    if (is_letter(c)) {
      std::string word = consume_while([this](char c_id) { return is_letter(c_id) || is_digit(c_id) || c_id == '_'; });
      Vyn::TokenType type = get_keyword_type(word);
      tokens.emplace_back(type, word, current_line_start_for_token, current_column_start_for_token, Vyn::AST::SourceLocation{source_, current_line_start_for_token, current_column_start_for_token});
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
        tokens.emplace_back(Vyn::TokenType::INT_LITERAL, int_part_str, current_line_start_for_token, current_column_start_for_token, Vyn::AST::SourceLocation{source_, current_line_start_for_token, current_column_start_for_token});
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

        tokens.emplace_back(Vyn::TokenType::FLOAT_LITERAL, float_str, current_line_start_for_token, current_column_start_for_token, Vyn::AST::SourceLocation{source_, current_line_start_for_token, current_column_start_for_token});
        column_ += float_str.size();
        continue;
      }
      else if (pos_ < source_.size() && source_[pos_] == '.') {
          throw std::runtime_error("Invalid number format (trailing dot): " + int_part_str + "." + " at line " + std::to_string(line_) + ", column " + std::to_string(column_ + int_part_str.size()));
      }
      else {
        tokens.emplace_back(Vyn::TokenType::INT_LITERAL, int_part_str, current_line_start_for_token, current_column_start_for_token, Vyn::AST::SourceLocation{source_, current_line_start_for_token, current_column_start_for_token});
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
      tokens.emplace_back(Vyn::TokenType::STRING_LITERAL, str_value, current_line_start_for_token, current_column_start_for_token, Vyn::AST::SourceLocation{source_, current_line_start_for_token, current_column_start_for_token});
      pos_++; // Consume closing quote
      column_ += str_value.size() + 2; // +2 for the quotes
      continue;
    }

    // Helper for single/double char tokens
    auto emit_token = [&](Vyn::TokenType type, const std::string& lexeme_val) {
        tokens.emplace_back(type, lexeme_val, current_line_start_for_token, current_column_start_for_token, Vyn::AST::SourceLocation{source_, current_line_start_for_token, current_column_start_for_token});
        pos_ += lexeme_val.size();
        column_ += lexeme_val.size();
    };
    
    // Store pos and column before potential multi-char token parsing
    size_t original_pos = pos_;
    int original_column = column_;

    switch (c) {
      case '(': emit_token(Vyn::TokenType::LPAREN, "("); nesting_level_++; break; // Qualified TokenType
      case ')': emit_token(Vyn::TokenType::RPAREN, ")"); nesting_level_--; break; // Qualified TokenType
      case '[': emit_token(Vyn::TokenType::LBRACKET, "["); nesting_level_++; break; // Qualified TokenType
      case ']': emit_token(Vyn::TokenType::RBRACKET, "]"); nesting_level_--; break; // Qualified TokenType
      case '{': emit_token(Vyn::TokenType::LBRACE, "{"); nesting_level_++; break; // Qualified TokenType
      case '}': emit_token(Vyn::TokenType::RBRACE, "}"); nesting_level_--; break; // Qualified TokenType
      case ',': emit_token(Vyn::TokenType::COMMA, ","); break; // Qualified TokenType
      case '.': 
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '.') {
          emit_token(Vyn::TokenType::DOTDOT, "..");
        } else {
          emit_token(Vyn::TokenType::DOT, ".");
        }
        break;
      case ':':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == ':') {
          emit_token(Vyn::TokenType::COLONCOLON, "::");
        } else {
          emit_token(Vyn::TokenType::COLON, ":");
        }
        break;
      case '=':
        if (pos_ + 1 < source_.size()) {
          if (source_[pos_ + 1] == '=') { 
            emit_token(Vyn::TokenType::EQEQ, "==");
          } else if (source_[pos_ + 1] == '>') { 
            emit_token(Vyn::TokenType::FAT_ARROW, "=>");
          } else { 
            emit_token(Vyn::TokenType::EQ, "=");
          }
        } else { 
          emit_token(Vyn::TokenType::EQ, "=");
        }
        break;
      case '!':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          emit_token(Vyn::TokenType::NOTEQ, "!=");
        } else {
          emit_token(Vyn::TokenType::BANG, "!");
        }
        break;
      case '<':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          emit_token(Vyn::TokenType::LTEQ, "<=");
        } else {
          emit_token(Vyn::TokenType::LT, "<");
        }
        break;
      case '>':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          emit_token(Vyn::TokenType::GTEQ, ">=");
        } else {
          emit_token(Vyn::TokenType::GT, ">");
        }
        break;
      case '+': emit_token(Vyn::TokenType::PLUS, "+"); break; // Qualified TokenType
      case '*': emit_token(Vyn::TokenType::MULTIPLY, "*"); break; // Qualified TokenType
      case '/': 
        // This case is for division. Comments (//) are handled earlier.
        emit_token(Vyn::TokenType::DIVIDE, "/"); 
        break;
      case '&':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '&') {
          emit_token(Vyn::TokenType::AND, "&&");
        } else {
          emit_token(Vyn::TokenType::AMPERSAND, "&");
        }
        break;
      case '-':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '>') {
          emit_token(Vyn::TokenType::ARROW, "->");
        } else {
          emit_token(Vyn::TokenType::MINUS, "-");
        }
        break;
      case ';': emit_token(Vyn::TokenType::SEMICOLON, ";"); break; // Qualified TokenType
      case '@': emit_token(Vyn::TokenType::AT, "@"); break; // Qualified TokenType
      case '_': emit_token(Vyn::TokenType::UNDERSCORE, "_"); break; // Qualified TokenType
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

  int last_line_for_dedent = line_; // Use the line number of the last actual content or EOF
  int last_col_for_dedent = column_;
  if (!tokens.empty() && tokens.back().type != Vyn::TokenType::NEWLINE && tokens.back().type != Vyn::TokenType::INDENT && tokens.back().type != Vyn::TokenType::DEDENT) {
      last_line_for_dedent = tokens.back().location.line;
      last_col_for_dedent = tokens.back().location.column + tokens.back().lexeme.length(); // End of last token
  }


  while (indent_levels_.size() > 1) {
    tokens.emplace_back(Vyn::TokenType::DEDENT, "", last_line_for_dedent, 1, Vyn::AST::SourceLocation{source_, last_line_for_dedent, 1}); // DEDENTs are at column 1 of their effective line
    indent_levels_.pop_back();
  }

  tokens.emplace_back(Vyn::TokenType::END_OF_FILE, "", line_, column_, Vyn::AST::SourceLocation{source_, line_, column_});

  return tokens;
}

void Lexer::handle_newline(std::vector<Vyn::Token>& tokens) {
  // Current pos_ is at '\n'. line_ and column_ are for the '\n' itself.
  int newline_char_line = line_;
  int newline_char_col = column_;

  pos_++; 
  line_++;
  column_ = 1;

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
        tokens.emplace_back(Vyn::TokenType::NEWLINE, "", line_, 1, Vyn::AST::SourceLocation{source_, line_, 1});
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
    temp_pos_for_indent_check++;
  }

  bool emitted_indent_dedent = false;
  if (indent_count > indent_levels_.back()) {
    indent_levels_.push_back(indent_count);
    // INDENT token is for the current line (line_) at column 1
    tokens.emplace_back(Vyn::TokenType::INDENT, "", line_, 1, Vyn::AST::SourceLocation{source_, line_, 1});
    emitted_indent_dedent = true;
  } else if (indent_count < indent_levels_.back()) {
    bool actually_dedented_something = false;
    while (indent_levels_.back() > indent_count && indent_levels_.size() > 1) {
      indent_levels_.pop_back();
      // DEDENT token is for the current line (line_) at column 1
      tokens.emplace_back(Vyn::TokenType::DEDENT, "", line_, 1, Vyn::AST::SourceLocation{source_, line_, 1});
      actually_dedented_something = true;
      if (indent_levels_.back() == indent_count) {
          break;
      }
    }

    if (indent_levels_.back() != indent_count) {
      throw std::runtime_error("Indentation error: line " + std::to_string(line_) +
                               " has indent " + std::to_string(indent_count) +
                               ", which is not a valid dedent level. Current indent stack top: " +
                               std::to_string(indent_levels_.back()) + ".");
    }

    if (actually_dedented_something) {
        emitted_indent_dedent = true; 
    }
  }

  bool is_blank_or_comment_line = true;
  if (temp_pos_for_indent_check < source_.size()) {
      char first_content_char = source_[temp_pos_for_indent_check];
      if (first_content_char == '#') { 
          is_blank_or_comment_line = true;
      } else if (first_content_char == '/' && temp_pos_for_indent_check + 1 < source_.size() && source_[temp_pos_for_indent_check + 1] == '/') { 
          is_blank_or_comment_line = true;
      } else if (first_content_char == '\n' || first_content_char == '\r') { 
          is_blank_or_comment_line = true;
      } else {
          is_blank_or_comment_line = false;
      }
  } else { 
      is_blank_or_comment_line = true;
  }

  if (!emitted_indent_dedent && !is_blank_or_comment_line) {
    // NEWLINE token is for the current line (line_) at column 1
    tokens.emplace_back(Vyn::TokenType::NEWLINE, "", line_, 1, Vyn::AST::SourceLocation{source_, line_, 1});
  } 

  pos_ += indent_count; 
  column_ += indent_count; 
}

std::string Lexer::consume_while(std::function<bool(char)> pred) {
  std::string result;
  while (pos_ < source_.size() && pred(source_[pos_])) {
    result += source_[pos_];
    pos_++;
  }
  return result;
}

bool Lexer::is_letter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool Lexer::is_digit(char c) {
  return c >= '0' && c <= '9';
}

Vyn::TokenType Lexer::get_keyword_type(const std::string& word) {
  static const std::unordered_map<std::string, Vyn::TokenType> keywords = {
    {"if", Vyn::TokenType::KEYWORD_IF},
    {"else", Vyn::TokenType::KEYWORD_ELSE},
    {"while", Vyn::TokenType::KEYWORD_WHILE},
    {"for", Vyn::TokenType::KEYWORD_FOR},
    {"in", Vyn::TokenType::KEYWORD_IN},
    {"return", Vyn::TokenType::KEYWORD_RETURN},
    {"break", Vyn::TokenType::KEYWORD_BREAK},
    {"continue", Vyn::TokenType::KEYWORD_CONTINUE},
    {"var", Vyn::TokenType::KEYWORD_VAR},
    {"const", Vyn::TokenType::KEYWORD_CONST},
    {"fn", Vyn::TokenType::KEYWORD_FN},
    {"class", Vyn::TokenType::KEYWORD_CLASS},
    {"template", Vyn::TokenType::KEYWORD_TEMPLATE},
    {"import", Vyn::TokenType::KEYWORD_IMPORT},
    {"smuggle", Vyn::TokenType::KEYWORD_SMUGGLE},
    {"try", Vyn::TokenType::KEYWORD_TRY},
    {"catch", Vyn::TokenType::KEYWORD_CATCH},
    {"finally", Vyn::TokenType::KEYWORD_FINALLY},
    {"defer", Vyn::TokenType::KEYWORD_DEFER},
    {"await", Vyn::TokenType::KEYWORD_AWAIT},
    {"async", Vyn::TokenType::KEYWORD_ASYNC},
    {"match", Vyn::TokenType::KEYWORD_MATCH},
    {"operator", Vyn::TokenType::KEYWORD_OPERATOR},
    {"ref", Vyn::TokenType::KEYWORD_REF},
    {"scoped", Vyn::TokenType::KEYWORD_SCOPED},
    {"throw", Vyn::TokenType::IDENTIFIER}, 
    {"throws", Vyn::TokenType::IDENTIFIER},
    {"println", Vyn::TokenType::IDENTIFIER},
    {"new", Vyn::TokenType::IDENTIFIER}
  };
  auto it = keywords.find(word);
  return it != keywords.end() ? it->second : Vyn::TokenType::IDENTIFIER;
}
