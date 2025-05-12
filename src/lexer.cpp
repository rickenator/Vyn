#include "vyn/lexer.hpp"
#include <stdexcept>
#include <iostream>
#include <functional>

Lexer::Lexer(const std::string& source) : source_(source), pos_(0), line_(1), column_(1) {
  indent_levels_.push_back(0);
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;

  while (pos_ < source_.size()) {
    char c = source_[pos_];
    std::cout << "DEBUG: Processing char '" << (c == '\n' ? "\\n" : std::string(1, c))
              << "' at pos_ = " << pos_ << ", line = " << line_
              << ", column = " << column_ << std::endl;

    if (c == '\n') {
      handle_newline(tokens);
      pos_++;
      line_++;
      column_ = 1;
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
      std::cout << "DEBUG: Parsing comment at line " << line_ << ", column " << column_ << std::endl;
      std::string comment = consume_while([](char c) { return c != '\n'; });
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

    if (is_digit(c) || c == '-') {
      std::string num = consume_while([this](char c) { return is_digit(c) || c == '.' || c == '-'; });
      tokens.emplace_back(TokenType::INT_LITERAL, num, line_, column_);
      std::cout << "DEBUG: Emitting INT_LITERAL at line " << line_ << ", value = " << num << std::endl;
      column_ += num.size();
      continue;
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
      case '(': tokens.emplace_back(TokenType::LPAREN, "(", line_, column_); break;
      case ')': tokens.emplace_back(TokenType::RPAREN, ")", line_, column_); break;
      case '{': tokens.emplace_back(TokenType::LBRACE, "{", line_, column_); break;
      case '}': tokens.emplace_back(TokenType::RBRACE, "}", line_, column_); break;
      case '[': tokens.emplace_back(TokenType::LBRACKET, "[", line_, column_); break;
      case ']': tokens.emplace_back(TokenType::RBRACKET, "]", line_, column_); break;
      case ',': tokens.emplace_back(TokenType::COMMA, ",", line_, column_); break;
      case '.': tokens.emplace_back(TokenType::DOT, ".", line_, column_); break;
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
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
          tokens.emplace_back(TokenType::EQEQ, "==", line_, column_);
          pos_++;
          column_++;
        } else {
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
          pos_++;
          column_++;
        } else {
          tokens.emplace_back(TokenType::AMPERSAND, "&", line_, column_);
        }
        break;
      case '-':
        if (pos_ + 1 < source_.size() && source_[pos_ + 1] == '>') {
          tokens.emplace_back(TokenType::ARROW, "->", line_, column_);
          pos_++;
          column_++;
        } else {
          tokens.emplace_back(TokenType::MINUS, "-", line_, column_);
        }
        break;
      case ';': tokens.emplace_back(TokenType::SEMICOLON, ";", line_, column_); break;
      case '@': tokens.emplace_back(TokenType::AT, "@", line_, column_); break;
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
  tokens.emplace_back(TokenType::NEWLINE, "", line_, column_);
  std::cout << "DEBUG: Emitting NEWLINE at line " << line_ << ", column " << column_ << std::endl;

  size_t indent_count = 0;
  while (pos_ + 1 < source_.size() && (source_[pos_ + 1] == ' ' || source_[pos_ + 1] == '\t')) {
    if (source_[pos_ + 1] == '\t') {
      throw std::runtime_error("Tabs not allowed at line " + std::to_string(line_ + 1) +
                               ", column " + std::to_string(indent_count + 1));
    }
    indent_count++;
    pos_++;
  }

  std::cout << "DEBUG: Handling new line, indent_count = " << indent_count
            << ", indent_levels_.size() = " << indent_levels_.size() << std::endl;

  if (indent_count > indent_levels_.back()) {
    indent_levels_.push_back(indent_count);
    tokens.emplace_back(TokenType::INDENT, "", line_ + 1, 1);
    std::cout << "DEBUG: Emitting INDENT at line " << line_ + 1 << ", column 1" << std::endl;
  } else if (indent_count < indent_levels_.back()) {
    while (indent_count < indent_levels_.back()) {
      tokens.emplace_back(TokenType::DEDENT, "", line_ + 1, 1);
      std::cout << "DEBUG: Emitting DEDENT at line " << line_ + 1 << ", column 1" << std::endl;
      indent_levels_.pop_back();
    }
  }
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
