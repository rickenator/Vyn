#include "vyn/lexer.hpp"
#include <stdexcept>
#include <iostream>
#include <functional>

Lexer::Lexer(const std::string& source) : source_(source), pos_(0), line_(1), column_(1), indent_levels_({0}), nesting_level_(0) {
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;

  while (pos_ < source_.size()) {
    char c = source_[pos_];
    std::cout << "DEBUG: Processing char '" << (c == '\n' ? "\\\\n" : (c == '\r' ? "\\\\r" : std::string(1, c))) // Also show \\r for debug
              << "' at pos_ = " << pos_ << ", line = " << line_
              << ", column = " << column_ << std::endl;

    if (c == '\r') { // Skip carriage return characters
      pos_++;
      // Do not increment column_ here, as \\r doesn't usually advance cursor visually
      // or it will be immediately followed by \\n which resets column to 1.
      continue;
    }

    if (c == '\n') { // Char literal: '\\n'
      handle_newline(tokens); // handle_newline now consumes '\\n' and updates line/col/pos
      continue; 
    }

    // Skip single-line comments starting with #
    if (c == '#') {
      std::cout << "DEBUG: Skipping # comment at line " << line_ << ", column " << column_ << std::endl;
      consume_while([](char c_comment) { return c_comment != '\n'; }); // Char literal: '\\n'
      // The newline character itself will be handled by the next iteration of the main loop,
      // which will call handle_newline.
      continue;
    }

    if (c == ' ' || c == '\t') { // Char literal: '\\t'
      if (c == '\t') { // Char literal: '\\t'
        throw std::runtime_error("Tabs not allowed at line " + std::to_string(line_) +
                                 ", column " + std::to_string(column_));
      }
      pos_++;
      column_++;
      continue;
    }

    if (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '/') {
      std::cout << "DEBUG: Parsing comment at line " << line_ << ", column " << column_ << std::endl;
      std::string comment = consume_while([](char c_comment_slash) { return c_comment_slash != '\n'; }); // Char literal: '\\n'
      tokens.emplace_back(TokenType::COMMENT, "//" + comment, line_, column_);
      std::cout << "DEBUG: Emitting COMMENT (//) at line " << line_ << ", value = " << "//" + comment << std::endl;
      continue;
    }

    if (is_letter(c)) {
      std::string word = consume_while([this](char c) { return is_letter(c) || is_digit(c) || c == '_'; });
      TokenType type = get_keyword_type(word);
      tokens.emplace_back(type, word, line_, column_);
      std::cout << "DEBUG: Emitting " << token_type_to_string(type) << " at line " << line_
                << ", value = " << word << std::endl;
      column_ += word.size();
      continue;
    }

    // Explicitly check for MINUS first if that's part of a number literal,
    // but current strategy is MINUS is separate token.
    // So, numbers must start with a digit.
    if (is_digit(c)) {
      std::cout << "DEBUG: LEXER_NUMBER_BLOCK: Entered for char '" << c << "' at line " << line_ << ", column " << column_ << std::endl;
      std::string int_part_str = consume_while([this](char char_digit_pred) {
        return is_digit(char_digit_pred);
      });
      std::cout << "DEBUG: LEXER_NUMBER_BLOCK: Consumed initial digits: \\\"" << int_part_str << "\\\"" << std::endl; // Escaped quote for string: \\\"

      // Check for range operator ".."
      // pos_ is at the character immediately after int_part_str
      if (pos_ + 1 < source_.size() && source_[pos_] == '.' && source_[pos_ + 1] == '.') {
        // This is an integer followed by "..". Emit the integer.
        // The dots will be handled by the main loop as separate DOT tokens.
        tokens.emplace_back(TokenType::INT_LITERAL, int_part_str, line_, column_);
        std::cout << "DEBUG: Emitting INT_LITERAL (potential range start) at line " << line_ << ", value = " << int_part_str << std::endl;
        column_ += int_part_str.size();
        // pos_ is currently at the first '.', so the main loop will pick it up.
        continue;
      }
      // Check for float: . followed by a digit
      else if (pos_ < source_.size() && source_[pos_] == '.' &&
               pos_ + 1 < source_.size() && is_digit(source_[pos_ + 1])) {
        std::string float_str = int_part_str;
        float_str += source_[pos_]; // Add the dot
        pos_++; // Consume the dot

        std::string decimal_part_str = consume_while([this](char char_digit_pred) {
          return is_digit(char_digit_pred);
        });
        float_str += decimal_part_str;
        std::cout << "DEBUG: LEXER_NUMBER_BLOCK: Consumed decimal part: \\\"" << decimal_part_str << "\\\"" << std::endl; // Escaped quote for string: \\\"

        // Check for another dot, which would be invalid (e.g., 1.2.3)
        // pos_ is now after the decimal part.
        if (pos_ < source_.size() && source_[pos_] == '.') {
             throw std::runtime_error("Invalid number format (multiple dots in float): " + float_str + "." + " at line " + std::to_string(line_) + ", column " + std::to_string(column_ + float_str.size()));
        }

        tokens.emplace_back(TokenType::FLOAT_LITERAL, float_str, line_, column_);
        std::cout << "DEBUG: Emitting FLOAT_LITERAL at line " << line_ << ", value = " << float_str << std::endl;
        column_ += float_str.size();
        continue;
      }
      // Check for invalid trailing dot (e.g. "1.") if it's not a range or a valid float
      else if (pos_ < source_.size() && source_[pos_] == '.') {
          // This means we have digits, then a '.', but not '..' and not '.<digit>'
          // This is an invalid number like "1."
          throw std::runtime_error("Invalid number format (trailing dot): " + int_part_str + "." + " at line " + std::to_string(line_) + ", column " + std::to_string(column_ + int_part_str.size()));
      }
      // Otherwise, it's just an integer
      else {
        tokens.emplace_back(TokenType::INT_LITERAL, int_part_str, line_, column_);
        std::cout << "DEBUG: Emitting INT_LITERAL at line " << line_ << ", value = " << int_part_str << std::endl;
        column_ += int_part_str.size();
        continue;
      }
    }

    if (c == '"') {
      pos_++;
      std::string str = consume_while([](char c) { return c != '"'; });
      tokens.emplace_back(TokenType::STRING_LITERAL, str, line_, column_);
      std::cout << "DEBUG: Emitting STRING_LITERAL at line " << line_ << ", value = " << str << std::endl;
      pos_++; // Skip closing quote
      column_ += str.size() + 2;
      continue;
    }

    switch (c) {
      case '(': tokens.emplace_back(TokenType::LPAREN, "(", line_, column_); nesting_level_++; std::cout << "DEBUG: Incremented nesting_level_ to " << nesting_level_ << " after LPAREN" << std::endl; break;
      case ')': tokens.emplace_back(TokenType::RPAREN, ")", line_, column_); nesting_level_--; std::cout << "DEBUG: Decremented nesting_level_ to " << nesting_level_ << " after RPAREN" << std::endl; break;
      case '[': tokens.emplace_back(TokenType::LBRACKET, "[", line_, column_); nesting_level_++; std::cout << "DEBUG: Incremented nesting_level_ to " << nesting_level_ << " after LBRACKET" << std::endl; break;
      case ']': tokens.emplace_back(TokenType::RBRACKET, "]", line_, column_); nesting_level_--; std::cout << "DEBUG: Decremented nesting_level_ to " << nesting_level_ << " after RBRACKET" << std::endl; break;
      case '{': 
        tokens.emplace_back(TokenType::LBRACE, "{", line_, column_); 
        nesting_level_++;
        std::cout << "DEBUG: Incremented nesting_level_ to " << nesting_level_ << " after LBRACE" << std::endl;
        break;
      case '}': 
        tokens.emplace_back(TokenType::RBRACE, "}", line_, column_); 
        nesting_level_--;
        std::cout << "DEBUG: Decremented nesting_level_ to " << nesting_level_ << " after RBRACE" << std::endl;
        break;
      case ',': tokens.emplace_back(TokenType::COMMA, ",", line_, column_); break;
      // case '.': tokens.emplace_back(TokenType::DOT, ".", line_, column_); break; // OLD LINE
      case '.': // NEW BLOCK
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '.') {
          tokens.emplace_back(TokenType::DOTDOT, "..", line_, column_);
          pos_++; // Consume the second '.'
          column_++; // Account for the second '.' being consumed in addition to the first by the end-of-loop increment
        } else {
          tokens.emplace_back(TokenType::DOT, ".", line_, column_);
        }
        break;
      case ':':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == ':') {
          tokens.emplace_back(TokenType::COLONCOLON, "::", line_, column_);
          pos_++;
          column_++;
        } else {
          tokens.emplace_back(TokenType::COLON, ":", line_, column_);
        }
        break;
      case '=':
        if (pos_ + 1 < source_.size()) {
          if (source_[pos_ + 1] == '=') { // EQEQ ==
            tokens.emplace_back(TokenType::EQEQ, "==", line_, column_);
            pos_++; // Consume the second '='
            column_++; // Account for the second '='
          } else if (source_[pos_ + 1] == '>') { // FAT_ARROW =>
            tokens.emplace_back(TokenType::FAT_ARROW, "=>", line_, column_);
            pos_++; // Consume the '>'
            column_++; // Account for the '>'
          } else { // Single EQ =
            tokens.emplace_back(TokenType::EQ, "=", line_, column_);
          }
        } else { // Single EQ = at EOF
          tokens.emplace_back(TokenType::EQ, "=", line_, column_);
        }
        break;
      case '!':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          tokens.emplace_back(TokenType::NOTEQ, "!=", line_, column_);
          pos_++;
          column_++;
        } else {
          tokens.emplace_back(TokenType::BANG, "!", line_, column_);
        }
        break;
      case '<':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          tokens.emplace_back(TokenType::LTEQ, "<=", line_, column_);
          pos_++;
          column_++;
        } else {
          tokens.emplace_back(TokenType::LT, "<", line_, column_);
        }
        break;
      case '>':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          tokens.emplace_back(TokenType::GTEQ, ">=", line_, column_);
          pos_++;
          column_++;
        } else {
          tokens.emplace_back(TokenType::GT, ">", line_, column_);
        }
        break;
      case '+': tokens.emplace_back(TokenType::PLUS, "+", line_, column_); break;
      case '*': tokens.emplace_back(TokenType::MULTIPLY, "*", line_, column_); break;
      case '/': tokens.emplace_back(TokenType::DIVIDE, "/", line_, column_); break;
      case '&':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '&') {
          tokens.emplace_back(TokenType::AND, "&&", line_, column_);
          std::cout << "DEBUG: Emitting AND at line " << line_ << ", column " << column_ << std::endl; // DEBUG
          pos_++;
          column_++;
        } else {
          tokens.emplace_back(TokenType::AMPERSAND, "&", line_, column_);
          std::cout << "DEBUG: Emitting AMPERSAND at line " << line_ << ", column " << column_ << std::endl; // DEBUG
        }
        break;
      case '-':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '>') {
          tokens.emplace_back(TokenType::ARROW, "->", line_, column_);
          std::cout << "DEBUG: Emitting ARROW at line " << line_ << ", column " << column_ << std::endl; // DEBUG
          pos_++;
          column_++;
        } else {
          // Tokenize '-' as MINUS. Parser will handle unary negation.
          tokens.emplace_back(TokenType::MINUS, "-", line_, column_);
          std::cout << "DEBUG: Emitting MINUS at line " << line_ << ", column " << column_ << std::endl; // DEBUG
        }
        break;
      case ';': tokens.emplace_back(TokenType::SEMICOLON, ";", line_, column_); std::cout << "DEBUG: Emitting SEMICOLON at line " << line_ << ", column " << column_ << std::endl; break; // DEBUG
      case '@': tokens.emplace_back(TokenType::AT, "@", line_, column_); std::cout << "DEBUG: Emitting AT at line " << line_ << ", column " << column_ << std::endl; break; // DEBUG
      case '_': tokens.emplace_back(TokenType::UNDERSCORE, "_", line_, column_); std::cout << "DEBUG: Emitting UNDERSCORE at line " << line_ << ", column " << column_ << std::endl; break; // DEBUG
      default:
        throw std::runtime_error("Unexpected character: " + std::string(1, c) +
                                 " at line " + std::to_string(line_) +
                                 ", column " + std::to_string(column_));
    }
    std::cout << "DEBUG: Emitting " << token_type_to_string(tokens.back().type)
              << " at line " << line_ << ", column " << column_ << std::endl;
    pos_++;
    column_++;
  }

  std::cout << "DEBUG: Finalizing indentation, indent_levels_.size() = " << indent_levels_.size() << std::endl;
  while (indent_levels_.size() > 1) {
    tokens.emplace_back(TokenType::DEDENT, "", line_, column_);
    std::cout << "DEBUG: Emitting final DEDENT at line " << line_ << ", column " << column_ << std::endl;
    indent_levels_.pop_back();
  }

  tokens.emplace_back(TokenType::EOF_TOKEN, "", line_, column_);
  std::cout << "DEBUG: Emitting EOF_TOKEN at line " << line_ << ", column " << column_ << std::endl;

  return tokens;
}

void Lexer::handle_newline(std::vector<Token>& tokens) {
  // Consume the newline character itself.
  pos_++; // Consumed '\\n'
  line_++;
  column_ = 1;
  std::cout << "DEBUG: handle_newline: Consumed '\\\\n'. New line: L" << line_ << "C" << column_ << ". pos_=" << pos_ << std::endl;

  if (nesting_level_ > 0) {
    std::cout << "DEBUG: handle_newline: Inside nested block (depth " << nesting_level_ << "). Checking for NEWLINE." << std::endl;
    
    size_t original_column = column_; // Should be 1 at this point

    // Skip leading spaces on the new line to check if it's blank or comment
    // We need to count how many spaces we skip to correctly advance pos_ and column_ later.
    size_t spaces_skipped = 0;
    size_t temp_pos_for_check = pos_; 

    while (temp_pos_for_check < source_.size() && source_[temp_pos_for_check] == ' ') {
        temp_pos_for_check++;
        spaces_skipped++;
    }

    // Check for tabs after spaces
    if (temp_pos_for_check < source_.size() && source_[temp_pos_for_check] == '\t') { // Char literal: '\t'
        throw std::runtime_error("Tabs not allowed at line " + std::to_string(line_) +
                                 ", column " + std::to_string(original_column + spaces_skipped));
    }

    bool is_nested_blank_or_comment = true;
    if (temp_pos_for_check < source_.size()) { 
        char first_char_after_spaces = source_[temp_pos_for_check];
        if (first_char_after_spaces == '#') { 
            is_nested_blank_or_comment = true;
        } else if (first_char_after_spaces == '/' && temp_pos_for_check + 1 < source_.size() && source_[temp_pos_for_check + 1] == '/') { 
            is_nested_blank_or_comment = true;
        } else if (first_char_after_spaces == '\n' || first_char_after_spaces == '\r') { // Char literal: '\n', '\r'
             is_nested_blank_or_comment = true;
        }
        else {
            is_nested_blank_or_comment = false;
        }
    } else { // Reached EOF after skipping spaces, so line is blank.
        is_nested_blank_or_comment = true;
    }

    if (!is_nested_blank_or_comment) {
        tokens.emplace_back(TokenType::NEWLINE, "", line_, 1); // NEWLINE for the line just started, col 1 (original line start)
        std::cout << "DEBUG: Emitting NEWLINE (nested) at line " << line_ << ", column " << 1 << std::endl;
    } else {
        std::cout << "DEBUG: Skipping NEWLINE token (nested, blank/comment) at line " << line_ << std::endl;
    }
    
    // Advance main lexer position and column past the leading whitespace found on this new nested line.
    pos_ += spaces_skipped;
    column_ += spaces_skipped; 
    // The main loop will continue from this new pos_ and column_.
    std::cout << "DEBUG: handle_newline (nested): Advanced pos_ and column_ past indent. Now at pos_ " << pos_ << ", column " << column_ << std::endl;
    return; 
  }

  // --- Logic for non-nested blocks (nesting_level_ == 0) ---
  // pos_ is at the beginning of the new line (char after '\\n'). column_ is 1.

  size_t indent_count = 0;
  size_t temp_pos_for_indent_check = pos_; // Use a temporary pos to lookahead for indent spaces from current pos_

  while (temp_pos_for_indent_check < source_.size()) {
    char char_on_this_line = source_[temp_pos_for_indent_check];
    if (char_on_this_line == ' ') {
      indent_count++;
    } else if (char_on_this_line == '\t') { // Char literal: '\t'
      throw std::runtime_error("Tabs not allowed at line " + std::to_string(line_) +
                               ", column " + std::to_string(1 + indent_count)); // column is 1 + spaces before tab
    } else {
      break; // Not a space or tab, stop counting indentation.
    }
    temp_pos_for_indent_check++;
  }
  // `indent_count` is now the number of leading spaces on the current line.
  // `temp_pos_for_indent_check` points to the first non-indent char or source_.size().

  std::cout << "DEBUG: NL processed. On new line L" << line_ << "C1" // Original column_ was 1
            << ". Calculated indent_count = " << indent_count
            << ". Current indent stack top: " << indent_levels_.back() << std::endl;

  bool emitted_indent_dedent = false;
  if (indent_count > indent_levels_.back()) {
    indent_levels_.push_back(indent_count);
    tokens.emplace_back(TokenType::INDENT, "", line_, 1); // INDENT is for the current line, col 1
    std::cout << "DEBUG: Emitting INDENT (to " << indent_count << ") at line " << line_ << std::endl;
    emitted_indent_dedent = true;
  } else if (indent_count < indent_levels_.back()) {
    bool actually_dedented_something = false;
    // Loop while the current top of the indent stack is greater than the target indent count,
    // and ensure we don't pop the base level 0.
    while (indent_levels_.back() > indent_count && indent_levels_.size() > 1) {
      indent_levels_.pop_back();
      tokens.emplace_back(TokenType::DEDENT, "", line_, 1); 
      std::cout << "DEBUG: Emitting DEDENT (target: " << indent_count << ", now at level " << indent_levels_.back() << ") at line " << line_ << std::endl;
      actually_dedented_something = true;
      // If we've popped to exactly the target indent level, we can stop early.
      if (indent_levels_.back() == indent_count) {
          break;
      }
    }

    // After the loop, the top of the indent stack must exactly match the current line's indent count.
    // If not, it means the current line's indentation is not a previously established indent level.
    if (indent_levels_.back() != indent_count) {
      throw std::runtime_error("Indentation error: line " + std::to_string(line_) +
                               " has indent " + std::to_string(indent_count) +
                               ", which is not a valid dedent level. Current indent stack top: " +
                               std::to_string(indent_levels_.back()) + ".");
    }

    if (actually_dedented_something) {
        emitted_indent_dedent = true; // Suppress NEWLINE if DEDENT was emitted
    }
  }

  bool is_blank_or_comment_line = true;
  // temp_pos_for_indent_check is at the first non-indent char, or source_.size()
  if (temp_pos_for_indent_check < source_.size()) {
      char first_content_char = source_[temp_pos_for_indent_check];
      if (first_content_char == '#') { // Hash comments
          is_blank_or_comment_line = true;
      } else if (first_content_char == '/' && temp_pos_for_indent_check + 1 < source_.size() && source_[temp_pos_for_indent_check + 1] == '/') { // Slash comments
          is_blank_or_comment_line = true;
      } else if (first_content_char == '\n' || first_content_char == '\r') { // Line is effectively blank if it's just indent then another newline
          is_blank_or_comment_line = true;
      } else {
          is_blank_or_comment_line = false;
      }
  } else { // Reached EOF after indent, so line is blank.
      is_blank_or_comment_line = true;
  }

  if (!emitted_indent_dedent && !is_blank_or_comment_line) {
    tokens.emplace_back(TokenType::NEWLINE, "", line_, 1); // NEWLINE for the line just started, col 1
    std::cout << "DEBUG: Emitting NEWLINE at line " << line_ << ", column " << 1 
              << " (indent " << indent_count << " vs stack top " << indent_levels_.back() << ")" << std::endl;
  } else {
      std::cout << "DEBUG: Skipping NEWLINE token. emitted_indent_dedent=" << emitted_indent_dedent
                << ", is_blank_or_comment_line=" << is_blank_or_comment_line << std::endl;
  }

  // Advance main lexer pos_ and column_ to skip the indent characters that were just processed for this line.
  // pos_ was at the start of the line's characters (after '\\n' was consumed). column_ was 1.
  pos_ += indent_count; // Advance pos_ by the number of space characters counted as indent
  column_ += indent_count; // column_ becomes 1 + indent_count, which is the start of non-indent content.
  std::cout << "DEBUG: handle_newline: Advanced pos_ and column_ past indent. Now at pos_ " << pos_ << ", column " << column_ << std::endl;
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

TokenType Lexer::get_keyword_type(const std::string& word) {
  static const std::unordered_map<std::string, TokenType> keywords = {
    {"if", TokenType::KEYWORD_IF},
    {"else", TokenType::KEYWORD_ELSE},
    {"while", TokenType::KEYWORD_WHILE},
    {"for", TokenType::KEYWORD_FOR},
    {"in", TokenType::KEYWORD_IN},
    {"return", TokenType::KEYWORD_RETURN},
    {"break", TokenType::KEYWORD_BREAK},
    {"continue", TokenType::KEYWORD_CONTINUE},
    {"var", TokenType::KEYWORD_VAR},
    {"const", TokenType::KEYWORD_CONST},
    {"fn", TokenType::KEYWORD_FN},
    {"class", TokenType::KEYWORD_CLASS},
    {"template", TokenType::KEYWORD_TEMPLATE},
    {"import", TokenType::KEYWORD_IMPORT},
    {"smuggle", TokenType::KEYWORD_SMUGGLE},
    {"try", TokenType::KEYWORD_TRY},
    {"catch", TokenType::KEYWORD_CATCH},
    {"finally", TokenType::KEYWORD_FINALLY},
    {"defer", TokenType::KEYWORD_DEFER},
    {"await", TokenType::KEYWORD_AWAIT},
    {"async", TokenType::KEYWORD_ASYNC},
    {"match", TokenType::KEYWORD_MATCH},
    {"operator", TokenType::KEYWORD_OPERATOR},
    {"ref", TokenType::KEYWORD_REF},
    {"scoped", TokenType::KEYWORD_SCOPED},
    {"throw", TokenType::IDENTIFIER},
    {"throws", TokenType::IDENTIFIER},
    {"println", TokenType::IDENTIFIER},
    {"new", TokenType::IDENTIFIER}
  };
  auto it = keywords.find(word);
  return it != keywords.end() ? it->second : TokenType::IDENTIFIER;
}

std::string Lexer::token_type_to_string(TokenType type) {
  switch (type) {
    case TokenType::IDENTIFIER: return "IDENTIFIER";
    case TokenType::INT_LITERAL: return "INT_LITERAL";
    case TokenType::FLOAT_LITERAL: return "FLOAT_LITERAL";
    case TokenType::STRING_LITERAL: return "STRING_LITERAL";
    case TokenType::KEYWORD_IF: return "KEYWORD_IF";
    case TokenType::KEYWORD_ELSE: return "KEYWORD_ELSE";
    case TokenType::KEYWORD_WHILE: return "KEYWORD_WHILE";
    case TokenType::KEYWORD_FOR: return "KEYWORD_FOR";
    case TokenType::KEYWORD_IN: return "KEYWORD_IN";
    case TokenType::KEYWORD_RETURN: return "KEYWORD_RETURN";
    case TokenType::KEYWORD_BREAK: return "KEYWORD_BREAK";
    case TokenType::KEYWORD_CONTINUE: return "KEYWORD_CONTINUE";
    case TokenType::KEYWORD_VAR: return "KEYWORD_VAR";
    case TokenType::KEYWORD_CONST: return "KEYWORD_CONST";
    case TokenType::KEYWORD_FN: return "KEYWORD_FN";
    case TokenType::KEYWORD_CLASS: return "KEYWORD_CLASS";
    case TokenType::KEYWORD_TEMPLATE: return "KEYWORD_TEMPLATE";
    case TokenType::KEYWORD_IMPORT: return "KEYWORD_IMPORT";
    case TokenType::KEYWORD_SMUGGLE: return "KEYWORD_SMUGGLE";
    case TokenType::KEYWORD_TRY: return "KEYWORD_TRY";
    case TokenType::KEYWORD_CATCH: return "KEYWORD_CATCH";
    case TokenType::KEYWORD_FINALLY: return "KEYWORD_FINALLY";
    case TokenType::KEYWORD_DEFER: return "KEYWORD_DEFER";
    case TokenType::KEYWORD_AWAIT: return "KEYWORD_AWAIT";
    case TokenType::KEYWORD_ASYNC: return "KEYWORD_ASYNC";
    case TokenType::KEYWORD_MATCH: return "KEYWORD_MATCH";
    case TokenType::KEYWORD_OPERATOR: return "KEYWORD_OPERATOR";
    case TokenType::KEYWORD_REF: return "KEYWORD_REF";
    case TokenType::KEYWORD_SCOPED: return "KEYWORD_SCOPED";
    case TokenType::LPAREN: return "LPAREN";
    case TokenType::RPAREN: return "RPAREN";
    case TokenType::LBRACE: return "LBRACE";
    case TokenType::RBRACE: return "RBRACE";
    case TokenType::LBRACKET: return "LBRACKET";
    case TokenType::RBRACKET: return "RBRACKET";
    case TokenType::COMMA: return "COMMA";
    case TokenType::DOT: return "DOT";
    case TokenType::COLON: return "COLON";
    case TokenType::COLONCOLON: return "COLONCOLON";
    case TokenType::SEMICOLON: return "SEMICOLON";
    case TokenType::EQ: return "EQ";
    case TokenType::EQEQ: return "EQEQ";
    case TokenType::NOTEQ: return "NOTEQ";
    case TokenType::LT: return "LT";
    case TokenType::GT: return "GT";
    case TokenType::LTEQ: return "LTEQ";
    case TokenType::GTEQ: return "GTEQ";
    case TokenType::PLUS: return "PLUS";
    case TokenType::MINUS: return "MINUS";
    case TokenType::MULTIPLY: return "MULTIPLY";
    case TokenType::DIVIDE: return "DIVIDE";
    case TokenType::AND: return "AND";
    case TokenType::BANG: return "BANG";
    case TokenType::AMPERSAND: return "AMPERSAND";
    case TokenType::ARROW: return "ARROW";
    case TokenType::FAT_ARROW: return "FAT_ARROW";
    case TokenType::DOTDOT: return "DOTDOT"; // ADDED
    case TokenType::NEWLINE: return "NEWLINE";
    case TokenType::INDENT: return "INDENT";
    case TokenType::DEDENT: return "DEDENT";
    case TokenType::EOF_TOKEN: return "EOF_TOKEN";
    case TokenType::COMMENT: return "COMMENT";
    case TokenType::AT: return "AT";
    case TokenType::UNDERSCORE: return "UNDERSCORE";
    default: return "UNKNOWN";
  }
}
