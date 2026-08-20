// SPDX-License-Identifier: Apache-2.0

#include "vyb/parser/lexer.hpp"
#include "vyb/parser/token.hpp" // Ensure vyb::token_type_to_string is available
#include "vyb/parser/source_location.hpp"   // Required for vyb::SourceLocation
#include <stdexcept>
#include <iostream>
#include <functional>
#include <algorithm> // Required for std::find
#include <unordered_map> // Required for std::unordered_map

Lexer::Lexer(const std::string& source, const std::string& filePath)
    : source_(source), current_file_path_(filePath), pos_(0), line_(1), column_(1), indent_levels_({0}), nesting_level_(0) {
}

std::vector<vyb::token::Token> Lexer::tokenize() {
  std::vector<vyb::token::Token> tokens;

  auto maybe_print_token = [this](const vyb::token::Token& token) {
    if (verbose_) {
      std::cout << "[TOKEN] " << vyb::token_type_to_string(token.type)
                << " : '" << token.lexeme << "' @ "
                << token.location.filePath << ":" << token.location.line << ":" << token.location.column << std::endl;
    }
  };

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

    // Skip single-line comments starting with #. Attribute syntax starts with #[...].
    if (c == '#' && !(pos_ + 1 < source_.size() && source_[pos_ + 1] == '[')) {
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
      tokens.emplace_back(vyb::TokenType::COMMENT, "//" + comment_text, vyb::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
      maybe_print_token(tokens.back());
      column_ += (2 + comment_text.size()); // Advance column for // and comment text
      continue;
    }

    if (is_letter(c)) {
      std::string word = consume_while([this](char c_id) { return is_letter(c_id) || is_digit(c_id) || c_id == '_'; });
      vyb::TokenType type = get_keyword_type(word);
      tokens.emplace_back(type, word, vyb::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
      maybe_print_token(tokens.back());
      column_ += word.size();
      continue;
    }

    if (is_digit(c)) {
      std::string int_part_str;
      if (c == '0' && pos_ + 1 < source_.size()) {
        char next = source_[pos_ + 1];
        if (next == 'x' || next == 'X') {
          // Hexadecimal
          int_part_str += c;
          int_part_str += next;
          pos_ += 2;
          column_ += 2;
          bool has_digits = false;
          while (pos_ < source_.size() && ((source_[pos_] >= '0' && source_[pos_] <= '9') ||
                                         (source_[pos_] >= 'a' && source_[pos_] <= 'f') ||
                                         (source_[pos_] >= 'A' && source_[pos_] <= 'F'))) {
            int_part_str += source_[pos_];
            pos_++;
            column_++;
            has_digits = true;
          }
          if (!has_digits) {
            throw std::runtime_error("Invalid hexadecimal literal: missing digits after 0x at line " + std::to_string(line_) + ", column " + std::to_string(column_));
          }
          // Unsigned suffix: `0xffu`
          if (pos_ < source_.size() && (source_[pos_] == 'u' || source_[pos_] == 'U')) {
            int_part_str += source_[pos_];
            pos_++;
            column_++;
          }
          tokens.emplace_back(vyb::TokenType::INT_LITERAL, int_part_str, vyb::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
          maybe_print_token(tokens.back());
          continue;
        } else if (next == 'b' || next == 'B') {
          // Binary
          int_part_str += c;
          int_part_str += next;
          pos_ += 2;
          column_ += 2;
          bool has_digits = false;
          while (pos_ < source_.size() && (source_[pos_] == '0' || source_[pos_] == '1')) {
            int_part_str += source_[pos_];
            pos_++;
            column_++;
            has_digits = true;
          }
          if (!has_digits) {
            throw std::runtime_error("Invalid binary literal: missing digits after 0b at line " + std::to_string(line_) + ", column " + std::to_string(column_));
          }
          // Unsigned suffix: `0b1010u`
          if (pos_ < source_.size() && (source_[pos_] == 'u' || source_[pos_] == 'U')) {
            int_part_str += source_[pos_];
            pos_++;
            column_++;
          }
          tokens.emplace_back(vyb::TokenType::INT_LITERAL, int_part_str, vyb::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
          maybe_print_token(tokens.back());
          continue;
        }
      }
      // Regular decimal integer
      int_part_str = consume_while([this](char char_digit_pred) {
        return is_digit(char_digit_pred);
      });
      // Unsigned suffix: `42u`
      if (pos_ < source_.size() && (source_[pos_] == 'u' || source_[pos_] == 'U')) {
        int_part_str += source_[pos_];
        pos_++;
        column_++;
      }
      // Check for range operator ".."
      // pos_ is at the character immediately after int_part_str
      if (pos_ + 1 < source_.size() && source_[pos_] == '.' && source_[pos_ + 1] == '.') {
        tokens.emplace_back(vyb::TokenType::INT_LITERAL, int_part_str, vyb::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
        maybe_print_token(tokens.back());
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

        tokens.emplace_back(vyb::TokenType::FLOAT_LITERAL, float_str, vyb::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
        maybe_print_token(tokens.back());
        column_ += float_str.size();
        continue;
      }
      else if (pos_ < source_.size() && source_[pos_] == '.') {
          throw std::runtime_error("Invalid number format (trailing dot): " + int_part_str + "." + " at line " + std::to_string(line_) + ", column " + std::to_string(column_ + int_part_str.size()));
      }
      else {
        tokens.emplace_back(vyb::TokenType::INT_LITERAL, int_part_str, vyb::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
        maybe_print_token(tokens.back());
        column_ += int_part_str.size();
        continue;
      }
    }

    if (c == '"') {
      pos_++; // Consume opening quote
      std::string str_value;
      // Number of source characters the literal body spans (escapes count as
      // their full source spelling), used to keep column_ accurate for the
      // Compiler that follows this token.
      size_t src_chars = 0;
      while (pos_ < source_.size() && source_[pos_] != '"') {
        char ch = source_[pos_];
        if (ch != '\\') {
          str_value.push_back(ch);
          pos_++;
          src_chars++;
          continue;
        }
        // Backslash escape sequence: consume the backslash, then one more char.
        pos_++;
        src_chars++;
        if (pos_ >= source_.size()) {
          throw std::runtime_error("Unterminated string literal at line " +
            std::to_string(current_line_start_for_token) + ", column " +
            std::to_string(current_column_start_for_token));
        }
        char esc = source_[pos_];
        pos_++;
        src_chars++;
        switch (esc) {
          case 'n': str_value.push_back('\n'); break;
          case 'r': str_value.push_back('\r'); break;
          case 't': str_value.push_back('\t'); break;
          case '\\': str_value.push_back('\\'); break;
          case '"': str_value.push_back('"'); break;
          case '\'': str_value.push_back('\''); break;
          case '0': str_value.push_back('\0'); break;
          case 'x': {
            // \xHH - exactly two hexadecimal digits as a literal byte.
            auto hex_val = [](char h) -> int {
              if (h >= '0' && h <= '9') return h - '0';
              if (h >= 'a' && h <= 'f') return h - 'a' + 10;
              if (h >= 'A' && h <= 'F') return h - 'A' + 10;
              return -1;
            };
            if (pos_ + 2 <= source_.size()) {
              int hi = hex_val(source_[pos_]);
              int lo = hex_val(source_[pos_ + 1]);
              if (hi >= 0 && lo >= 0) {
                str_value.push_back(static_cast<char>((hi << 4) | lo));
                pos_ += 2;
                src_chars += 2;
                break;
              }
            }
            // Not a valid \xHH: preserve the backslash-x literally so regex /
            // path strings stay intact.
            str_value.push_back('\\');
            str_value.push_back('x');
            break;
          }
          default:
            // Unrecognized escape: keep the backslash + character verbatim.
            str_value.push_back('\\');
            str_value.push_back(esc);
            break;
        }
      }
      if (pos_ >= source_.size()) {
          throw std::runtime_error("Unterminated string literal at line " +
            std::to_string(current_line_start_for_token) + ", column " +
            std::to_string(current_column_start_for_token));
      }
      tokens.emplace_back(vyb::TokenType::STRING_LITERAL, str_value, vyb::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
      maybe_print_token(tokens.back());
      pos_++; // Consume closing quote
      column_ += src_chars + 2; // +2 for the quotes
      continue;
    }

    // Helper for single/double char tokens
    auto emit_token = [&](vyb::TokenType type, const std::string& lexeme_val) {
        tokens.emplace_back(type, lexeme_val, vyb::SourceLocation{current_file_path_, current_line_start_for_token, current_column_start_for_token});
        maybe_print_token(tokens.back());
        pos_ += lexeme_val.size();
        column_ += lexeme_val.size();
    };

    // Store pos and column before potential multi-char token parsing
    size_t original_pos = pos_;
    int original_column = column_;

    switch (c) {
      case '(': emit_token(vyb::TokenType::LPAREN, "("); nesting_level_++; break;
      case ')': emit_token(vyb::TokenType::RPAREN, ")"); nesting_level_--; break;
      case '[': emit_token(vyb::TokenType::LBRACKET, "["); nesting_level_++; break;
      case ']': emit_token(vyb::TokenType::RBRACKET, "]"); nesting_level_--; break;
      case '{': emit_token(vyb::TokenType::LBRACE, "{"); nesting_level_++; break;
      case '}': emit_token(vyb::TokenType::RBRACE, "}"); nesting_level_--; break;
      case ',': emit_token(vyb::TokenType::COMMA, ","); break;
      case '.':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '.') {
          if (pos_ + 2 < source_.size() && source_[pos_ + 2] == '.') {
            emit_token(vyb::TokenType::ELLIPSIS, "...");
          } else {
            emit_token(vyb::TokenType::DOTDOT, "..");
          }
        } else {
          emit_token(vyb::TokenType::DOT, ".");
        }
        break;
      case ':':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == ':') {
          emit_token(vyb::TokenType::COLONCOLON, "::");
        } else {
          emit_token(vyb::TokenType::COLON, ":");
        }
        break;
      case '=':
        if (pos_ + 1 < source_.size()) {
          if (source_[pos_ + 1] == '=') {
            emit_token(vyb::TokenType::EQEQ, "==");
          } else if (source_[pos_ + 1] == '>') {
            emit_token(vyb::TokenType::FAT_ARROW, "=>");
          } else {
            emit_token(vyb::TokenType::EQ, "=");
          }
        } else {
          emit_token(vyb::TokenType::EQ, "=");
        }
        break;
      case '!':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          emit_token(vyb::TokenType::NOTEQ, "!=");
        } else {
          emit_token(vyb::TokenType::BANG, "!");
        }
        break;
      case '<':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '<') {
          if (pos_ + 2 < source_.size() && source_[pos_ + 2] == '=') {
            emit_token(vyb::TokenType::LSHIFTEQ, "<<=");
          } else {
            // `<<` is lexed as two `LT` tokens so nested generic closes (which are
            // also `>>`) stay unambiguous; the expression parser recognizes the
            // adjacent-pair form as a shift operator.
            emit_token(vyb::TokenType::LT, "<");
            emit_token(vyb::TokenType::LT, "<");
          }
        } else if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          emit_token(vyb::TokenType::LTEQ, "<=");
        } else {
          emit_token(vyb::TokenType::LT, "<");
        }
        break;
      case '>':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '>') {
          if (pos_ + 2 < source_.size() && source_[pos_ + 2] == '=') {
            emit_token(vyb::TokenType::RSHIFTEQ, ">>=");
          } else {
            // `>>` is lexed as two `GT` tokens so nested generic closes (which are
            // also `>>`) stay unambiguous; the expression parser recognizes the
            // adjacent-pair form as a shift operator.
            emit_token(vyb::TokenType::GT, ">");
            emit_token(vyb::TokenType::GT, ">");
          }
        } else if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          emit_token(vyb::TokenType::GTEQ, ">=");
        } else {
          emit_token(vyb::TokenType::GT, ">");
        }
        break;
      case '+':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            emit_token(vyb::TokenType::PLUSEQ, "+=");
        } else {
            emit_token(vyb::TokenType::PLUS, "+");
        }
        break;
      case '*':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            emit_token(vyb::TokenType::MULTIPLYEQ, "*=");
        } else {
            emit_token(vyb::TokenType::MULTIPLY, "*");
        }
        break;
      case '/':
        // This case is for division. Comments (//) are handled earlier.
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            emit_token(vyb::TokenType::DIVEQ, "/=");
        } else {
            emit_token(vyb::TokenType::DIVIDE, "/");
        }
        break;
      case '%':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            emit_token(vyb::TokenType::MODEQ, "%=");
        } else {
            emit_token(vyb::TokenType::MODULO, "%");
        }
        break;
      case '&':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '&') {
          emit_token(vyb::TokenType::AND, "&&");
        } else if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          emit_token(vyb::TokenType::BITWISEANDEQ, "&=");
        } else {
          emit_token(vyb::TokenType::AMPERSAND, "&");
        }
        break;
      case '|':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '|') {
          emit_token(vyb::TokenType::OR, "||");
        } else if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          emit_token(vyb::TokenType::BITWISEOREQ, "|=");
        } else {
          emit_token(vyb::TokenType::PIPE, "|");
        }
        break;
      case '^':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
            emit_token(vyb::TokenType::BITWISEXOREQ, "^=");
        } else {
            emit_token(vyb::TokenType::CARET, "^");
        }
        break;
      case '~':
        emit_token(vyb::TokenType::TILDE, "~");
        break;
      case '-':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '>') {
          emit_token(vyb::TokenType::ARROW, "->");
        } else {
          emit_token(vyb::TokenType::MINUS, "-");
        }
        break;
      case ';': emit_token(vyb::TokenType::SEMICOLON, ";"); break;
      case '@': emit_token(vyb::TokenType::AT, "@"); break;
      case '#': emit_token(vyb::TokenType::HASH, "#"); break;
      case '_': emit_token(vyb::TokenType::UNDERSCORE, "_"); break;
      case '?': emit_token(vyb::TokenType::QUESTION_MARK, "?"); break;
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
  if (!tokens.empty() && tokens.back().type != vyb::TokenType::NEWLINE && tokens.back().type != vyb::TokenType::INDENT && tokens.back().type != vyb::TokenType::DEDENT) {
      last_line_for_dedent = tokens.back().location.line;
      // The column for DEDENT should be 1, but the location object itself might need to reflect the end of the last token or start of the new line.
      // For simplicity, using 1 as the column for the DEDENT token itself.
      // The SourceLocation for DEDENT will point to the start of the line where the DEDENT is conceptually inserted.
  }


  while (indent_levels_.size() > 1) {
    tokens.emplace_back(vyb::TokenType::DEDENT, "", vyb::SourceLocation{current_file_path_, last_line_for_dedent, 1}); // DEDENTs are at column 1 of their effective line
    maybe_print_token(tokens.back());
    indent_levels_.pop_back();
  }

  tokens.emplace_back(vyb::TokenType::END_OF_FILE, "", vyb::SourceLocation{current_file_path_, static_cast<unsigned int>(line_), static_cast<unsigned int>(column_)});
  maybe_print_token(tokens.back());

  return tokens;
}

void Lexer::handle_newline(std::vector<vyb::token::Token>& tokens) {
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
        if (first_char_after_spaces == '#' &&
            !(temp_pos_for_check + 1 < source_.size() && source_[temp_pos_for_check + 1] == '[')) {
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
        tokens.emplace_back(vyb::TokenType::NEWLINE, "", vyb::SourceLocation{current_file_path_, current_line_unsigned, 1});
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
      (source_[temp_pos_for_indent_check] == '#' &&
       !(temp_pos_for_indent_check + 1 < source_.size() && source_[temp_pos_for_indent_check + 1] == '[')) || // Hash comment
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
  tokens.emplace_back(vyb::TokenType::NEWLINE, "", vyb::SourceLocation{current_file_path_, current_line_unsigned, 1});


  if (indent_count > indent_levels_.back()) {
    indent_levels_.push_back(indent_count);
    tokens.emplace_back(vyb::TokenType::INDENT, "", vyb::SourceLocation{current_file_path_, current_line_unsigned, 1});
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
      tokens.emplace_back(vyb::TokenType::DEDENT, "", vyb::SourceLocation{current_file_path_, current_line_unsigned, 1});
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

vyb::TokenType Lexer::get_keyword_type(const std::string& word) {
    static const std::unordered_map<std::string, vyb::TokenType> keywords = {
        {"let", vyb::TokenType::KEYWORD_LET},
        {"var", vyb::TokenType::KEYWORD_VAR},
        {"const", vyb::TokenType::KEYWORD_CONST},
        {"if", vyb::TokenType::KEYWORD_IF},
        {"else", vyb::TokenType::KEYWORD_ELSE},
        {"while", vyb::TokenType::KEYWORD_WHILE},
        {"for", vyb::TokenType::KEYWORD_FOR},
        {"return", vyb::TokenType::KEYWORD_RETURN},
        {"break", vyb::TokenType::KEYWORD_BREAK},
        {"continue", vyb::TokenType::KEYWORD_CONTINUE},
        {"null", vyb::TokenType::KEYWORD_NULL},
        {"true", vyb::TokenType::KEYWORD_TRUE},
        {"false", vyb::TokenType::KEYWORD_FALSE},
        {"fn", vyb::TokenType::KEYWORD_FN},
        {"struct", vyb::TokenType::KEYWORD_STRUCT},
        {"enum", vyb::TokenType::KEYWORD_ENUM},
        {"aspect", vyb::TokenType::KEYWORD_ASPECT},
        {"bind", vyb::TokenType::KEYWORD_BIND},
        {"type", vyb::TokenType::KEYWORD_TYPE},
        {"module", vyb::TokenType::KEYWORD_MODULE},
        {"use", vyb::TokenType::KEYWORD_USE},
        {"pub", vyb::TokenType::KEYWORD_PUB},
        {"mut", vyb::TokenType::KEYWORD_MUT},
        {"try", vyb::TokenType::KEYWORD_TRY},
        {"catch", vyb::TokenType::KEYWORD_CATCH},
        {"finally", vyb::TokenType::KEYWORD_FINALLY},
        {"defer", vyb::TokenType::KEYWORD_DEFER},
        {"match", vyb::TokenType::KEYWORD_MATCH},
        {"select", vyb::TokenType::KEYWORD_SELECT},
        {"pass", vyb::TokenType::KEYWORD_PASS},
        {"scoped", vyb::TokenType::KEYWORD_SCOPED},
        {"ref", vyb::TokenType::KEYWORD_REF},
        {"extern", vyb::TokenType::KEYWORD_EXTERN},
        {"as", vyb::TokenType::KEYWORD_AS},
        {"in", vyb::TokenType::KEYWORD_IN},
        {"class", vyb::TokenType::KEYWORD_CLASS},
        {"template", vyb::TokenType::KEYWORD_TEMPLATE},
        {"import", vyb::TokenType::KEYWORD_IMPORT},
        {"smuggle", vyb::TokenType::KEYWORD_SMUGGLE},
        {"from", vyb::TokenType::KEYWORD_FROM},
        {"await", vyb::TokenType::KEYWORD_AWAIT},
        {"async", vyb::TokenType::KEYWORD_ASYNC},
        {"operator", vyb::TokenType::KEYWORD_OPERATOR},
        {"my", vyb::TokenType::KEYWORD_MY},
        {"our", vyb::TokenType::KEYWORD_OUR},
        {"their", vyb::TokenType::KEYWORD_THEIR},
        {"mild", vyb::TokenType::KEYWORD_MILD},
        {"freedom", vyb::TokenType::KEYWORD_FREEDOM},
        {"ptr", vyb::TokenType::KEYWORD_PTR},
        {"borrow", vyb::TokenType::KEYWORD_BORROW},
        {"view", vyb::TokenType::KEYWORD_VIEW},
        {"nil", vyb::TokenType::KEYWORD_NIL},
        {"auto", vyb::TokenType::KEYWORD_AUTO},
        {"fail", vyb::TokenType::KEYWORD_FAIL},
        {"trap", vyb::TokenType::KEYWORD_TRAP},
        {"refail", vyb::TokenType::KEYWORD_REFAIL},
        {"ensure", vyb::TokenType::KEYWORD_ENSURE},
        {"panic", vyb::TokenType::KEYWORD_PANIC},
        {"exit", vyb::TokenType::KEYWORD_EXIT},
        {"typeof", vyb::TokenType::KEYWORD_TYPEOF},
        {"typename", vyb::TokenType::KEYWORD_TYPENAME}
    };

    auto it = keywords.find(word);
    if (it != keywords.end()) {
        return it->second;
    }
    return vyb::TokenType::IDENTIFIER;
}

bool Lexer::is_letter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool Lexer::is_digit(char c) {
  return c >= '0' && c <= '9';
}

/*
std::string Lexer::token_type_to_string(Vyb::TokenType type)
This function is now globally available as vyb::token_type_to_string
from "vyb/token.hpp" and implemented in "vyb/token.cpp".
The Lexer should use that one if it ever needs to generate a TokenType to a string,
though typically it just produces tokens with TokenType, not their string representations.
*/
