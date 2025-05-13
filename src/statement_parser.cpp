#include "vyn/parser.hpp"
#include <stdexcept>
#include <iostream>

StatementParser::StatementParser(const std::vector<Token>& tokens, size_t& pos, int indent_level)
    : BaseParser(tokens, pos), indent_level_(indent_level) {}

std::unique_ptr<ASTNode> StatementParser::parse() {
  // skip_comments(); // Removed: BaseParser::peek() and consume() now handle this.

  // Handle indent/dedent first as they define block structure relative to current indent_level_
  // This logic might need refinement based on how overall indentation is managed
  if (peek().type == TokenType::INDENT) {
    expect(TokenType::INDENT);
    indent_level_++;
    auto node = parse();
    if (peek().type == TokenType::DEDENT) {
      expect(TokenType::DEDENT);
      indent_level_--;
    }
    return node;
  }

  if (peek().type == TokenType::DEDENT) {
    expect(TokenType::DEDENT);
    indent_level_--;
    return nullptr;
  }

  if (peek().type == TokenType::KEYWORD_IF) {
    return parse_if();
  } else if (peek().type == TokenType::KEYWORD_WHILE) {
    return parse_while();
  } else if (peek().type == TokenType::KEYWORD_FOR) {
    return parse_for();
  } else if (peek().type == TokenType::KEYWORD_RETURN) {
    return parse_return();
  } else if (peek().type == TokenType::KEYWORD_BREAK) {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::BreakStmt, peek());
    expect(TokenType::KEYWORD_BREAK);
    return node;
  } else if (peek().type == TokenType::KEYWORD_CONTINUE) {
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::ContinueStmt, peek());
    expect(TokenType::KEYWORD_CONTINUE);
    return node;
  } else if (peek().type == TokenType::KEYWORD_VAR) {
    return parse_var();
  } else if (peek().type == TokenType::KEYWORD_CONST) {
    return parse_const();
  } else if (peek().type == TokenType::KEYWORD_SCOPED) {
    return parse_scoped();
  } else if (peek().type == TokenType::KEYWORD_AWAIT) {
    return parse_await();
  } else if (peek().type == TokenType::KEYWORD_DEFER) {
    return parse_defer();
  } else if (peek().type == TokenType::KEYWORD_TRY) {
    return parse_try();
  } else if (peek().type == TokenType::KEYWORD_IMPORT) {
    return parse_import();
  } else if (peek().type == TokenType::KEYWORD_SMUGGLE) {
    return parse_smuggle();
  } else if (peek().type == TokenType::KEYWORD_CLASS) {
    return parse_class();
  } else if (peek().type == TokenType::KEYWORD_FN) {
    DeclarationParser decl_parser(tokens_, pos_);
    return decl_parser.parse_function();
  } else if (peek().type == TokenType::KEYWORD_MATCH) {
    return parse_match();
  } else if (peek().type == TokenType::SEMICOLON) {
    expect(TokenType::SEMICOLON);
    return nullptr;
  } else {
    return parse_expression_statement();
  }
}

std::unique_ptr<ASTNode> StatementParser::parse_if() {
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::IfStmt, peek());
  expect(TokenType::KEYWORD_IF);
  ExpressionParser expr_parser(tokens_, pos_);
  node->children.push_back(expr_parser.parse());
  node->children.push_back(parse_block());
  if (peek().type == TokenType::KEYWORD_ELSE) {
    expect(TokenType::KEYWORD_ELSE);
    if (peek().type == TokenType::KEYWORD_IF) {
      node->children.push_back(parse_if());
    } else {
      node->children.push_back(parse_block());
    }
  }
  return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_while() {
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::WhileStmt, peek());
  expect(TokenType::KEYWORD_WHILE);
  ExpressionParser expr_parser(tokens_, pos_);
  node->children.push_back(expr_parser.parse());
  node->children.push_back(parse_block());
  return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_for() {
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::ForStmt, peek());
  expect(TokenType::KEYWORD_FOR);
  auto identifier_node = std::make_unique<ASTNode>(ASTNode::Kind::Identifier, peek());
  expect(TokenType::IDENTIFIER);
  node->children.push_back(std::move(identifier_node));
  expect(TokenType::KEYWORD_IN);
  ExpressionParser expr_parser(tokens_, pos_);
  node->children.push_back(expr_parser.parse());
  node->children.push_back(parse_block());
  return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_return() {
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::ReturnStmt, peek());
  expect(TokenType::KEYWORD_RETURN);
  if (peek().type != TokenType::NEWLINE && peek().type != TokenType::EOF_TOKEN &&
      peek().type != TokenType::DEDENT) {
    ExpressionParser expr_parser(tokens_, pos_);
    node->children.push_back(expr_parser.parse());
  }
  return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_var() {
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::VarDecl, peek());
  expect(TokenType::KEYWORD_VAR);
  node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, peek()));
  expect(TokenType::IDENTIFIER);
  if (peek().type == TokenType::COLON) {
    expect(TokenType::COLON);
    TypeParser type_parser(tokens_, pos_);
    node->children.push_back(type_parser.parse());
  }
  if (peek().type == TokenType::EQ) {
    expect(TokenType::EQ);
    ExpressionParser expr_parser(tokens_, pos_);
    node->children.push_back(expr_parser.parse());
  }
  return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_const() {
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::ConstDecl, peek());
  expect(TokenType::KEYWORD_CONST);
  node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, peek()));
  expect(TokenType::IDENTIFIER);
  if (peek().type == TokenType::COLON) {
    expect(TokenType::COLON);
    TypeParser type_parser(tokens_, pos_);
    node->children.push_back(type_parser.parse());
  }
  expect(TokenType::EQ);
  ExpressionParser expr_parser(tokens_, pos_);
  node->children.push_back(expr_parser.parse());
  return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_scoped() {
  auto scoped_token = peek();
  expect(TokenType::KEYWORD_SCOPED);

  if (peek().type == TokenType::KEYWORD_VAR) {
    auto var_decl_node = parse_var(); // parse_var itself consumes KEYWORD_VAR
    expect(TokenType::SEMICOLON);
    // Create a ScopedStmt node that wraps the VarDecl
    auto scoped_node = std::make_unique<ASTNode>(ASTNode::Kind::ScopedStmt, scoped_token);
    scoped_node->children.push_back(std::move(var_decl_node));
    return scoped_node;
  } else if (peek().type == TokenType::KEYWORD_CONST) {
    auto const_decl_node = parse_const(); // parse_const itself consumes KEYWORD_CONST
    expect(TokenType::SEMICOLON);
    // Create a ScopedStmt node that wraps the ConstDecl
    auto scoped_node = std::make_unique<ASTNode>(ASTNode::Kind::ScopedStmt, scoped_token);
    scoped_node->children.push_back(std::move(const_decl_node));
    return scoped_node;
  } else {
    // Original behavior: parse a block
    auto node = std::make_unique<ASTNode>(ASTNode::Kind::ScopedStmt, scoped_token);
    node->children.push_back(parse_block());
    return node;
  }
}

std::unique_ptr<ASTNode> StatementParser::parse_await() {
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::AwaitStmt, peek());
  expect(TokenType::KEYWORD_AWAIT);
  ExpressionParser expr_parser(tokens_, pos_);
  node->children.push_back(expr_parser.parse());
  return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_defer() {
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::DeferStmt, peek());
  expect(TokenType::KEYWORD_DEFER);
  ExpressionParser expr_parser(tokens_, pos_);
  node->children.push_back(expr_parser.parse());
  return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_try() {
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::TryStmt, peek());
  expect(TokenType::KEYWORD_TRY);
  node->children.push_back(parse_block());
  while (peek().type == TokenType::KEYWORD_CATCH) {
    expect(TokenType::KEYWORD_CATCH);
    auto catch_node = std::make_unique<ASTNode>(ASTNode::Kind::CatchStmt, peek());
    if (peek().type == TokenType::LPAREN) {
      expect(TokenType::LPAREN);
      catch_node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, peek()));
      expect(TokenType::IDENTIFIER);
      if (peek().type == TokenType::COLON) {
        expect(TokenType::COLON);
        TypeParser type_parser(tokens_, pos_);
        catch_node->children.push_back(type_parser.parse());
      }
      expect(TokenType::RPAREN);
    }
    catch_node->children.push_back(parse_block());
    node->children.push_back(std::move(catch_node));
  }
  if (peek().type == TokenType::KEYWORD_FINALLY) {
    expect(TokenType::KEYWORD_FINALLY);
    node->children.push_back(parse_block());
  }
  return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_block() {
  auto block_node = std::make_unique<ASTNode>(ASTNode::Kind::Block);
  if (peek().type == TokenType::LBRACE) {
    expect(TokenType::LBRACE);
    while (peek().type != TokenType::RBRACE && peek().type != TokenType::EOF_TOKEN) {
      auto stmt = parse();
      if (stmt) {
        block_node->children.push_back(std::move(stmt));
      }
      if (peek().type == TokenType::SEMICOLON) {
        expect(TokenType::SEMICOLON);
      }
    }
    expect(TokenType::RBRACE);
  } else if (peek().type == TokenType::INDENT) {
    expect(TokenType::INDENT);
    indent_level_++;
    while (peek().type != TokenType::DEDENT && peek().type != TokenType::EOF_TOKEN) {
      auto stmt = parse();
      if (stmt) {
        block_node->children.push_back(std::move(stmt));
      }
    }
    if (peek().type == TokenType::DEDENT) {
      expect(TokenType::DEDENT);
      indent_level_--;
    }
  }
  return block_node;
}

std::unique_ptr<ASTNode> StatementParser::parse_import() {
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::ImportStmt, peek());
  expect(TokenType::KEYWORD_IMPORT);
  node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, peek()));
  expect(TokenType::IDENTIFIER);
  while (peek().type == TokenType::COLONCOLON) {
    expect(TokenType::COLONCOLON);
    node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, peek()));
    expect(TokenType::IDENTIFIER);
  }
  if (peek().type == TokenType::SEMICOLON) {
    expect(TokenType::SEMICOLON);
  }
  return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_smuggle() {
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::SmuggleStmt, peek());
  expect(TokenType::KEYWORD_SMUGGLE);
  node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, peek()));
  expect(TokenType::IDENTIFIER);
  while (peek().type == TokenType::COLONCOLON) {
    expect(TokenType::COLONCOLON);
    node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, peek()));
    expect(TokenType::IDENTIFIER);
  }
  if (peek().type == TokenType::SEMICOLON) {
    expect(TokenType::SEMICOLON);
  }
  return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_class() {
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::ClassDecl, peek());
  expect(TokenType::KEYWORD_CLASS);
  node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, peek()));
  expect(TokenType::IDENTIFIER);
  node->children.push_back(parse_block());
  return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_match() {
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::MatchStmt, peek());
  expect(TokenType::KEYWORD_MATCH);
  ExpressionParser expr_parser(tokens_, pos_);
  node->children.push_back(expr_parser.parse());
  if (peek().type == TokenType::LBRACE) {
    expect(TokenType::LBRACE);
    while (peek().type != TokenType::RBRACE && peek().type != TokenType::EOF_TOKEN) {
      auto pattern_node = parse_pattern();
      expect(TokenType::FAT_ARROW);
      ExpressionParser expr_parser_inner(tokens_, pos_);
      auto expr_node = expr_parser_inner.parse();
      auto arm_node = std::make_unique<ASTNode>(ASTNode::Kind::MatchArm, peek());
      arm_node->children.push_back(std::move(pattern_node));
      arm_node->children.push_back(std::move(expr_node));
      node->children.push_back(std::move(arm_node));
      if (peek().type == TokenType::COMMA || peek().type == TokenType::SEMICOLON) {
        expect(peek().type);
      }
    }
    expect(TokenType::RBRACE);
  } else if (peek().type == TokenType::INDENT) {
    expect(TokenType::INDENT);
    indent_level_++;
    while (peek().type != TokenType::DEDENT && peek().type != TokenType::EOF_TOKEN) {
      auto pattern_node = parse_pattern();
      expect(TokenType::FAT_ARROW);
      ExpressionParser expr_parser_inner(tokens_, pos_);
      auto expr_node = expr_parser_inner.parse();
      auto arm_node = std::make_unique<ASTNode>(ASTNode::Kind::MatchArm, peek());
      arm_node->children.push_back(std::move(pattern_node));
      arm_node->children.push_back(std::move(expr_node));
      node->children.push_back(std::move(arm_node));
      if (peek().type == TokenType::COMMA || peek().type == TokenType::SEMICOLON) {
        expect(peek().type);
      }
    }
    expect(TokenType::DEDENT);
    indent_level_--;
  }
  return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_pattern() {
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::Pattern, peek());
  if (peek().type == TokenType::IDENTIFIER) {
    node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, peek()));
    expect(TokenType::IDENTIFIER);
  } else if (peek().type == TokenType::INT_LITERAL) {
    node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Expression, peek()));
    expect(TokenType::INT_LITERAL);
  } else if (peek().type == TokenType::UNDERSCORE) {
    node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Pattern, peek()));
    expect(TokenType::UNDERSCORE);
  } else if (peek().type == TokenType::LPAREN) {
    expect(TokenType::LPAREN);
    auto pattern_list = std::make_unique<ASTNode>(ASTNode::Kind::PatternList, peek());
    while (peek().type != TokenType::RPAREN && peek().type != TokenType::EOF_TOKEN) {
      pattern_list->children.push_back(parse_pattern());
      if (peek().type == TokenType::COMMA) {
        expect(TokenType::COMMA);
      }
    }
    expect(TokenType::RPAREN);
    node->children.push_back(std::move(pattern_list));
  } else {
    throw std::runtime_error("Unexpected token in pattern: " + token_type_to_string(peek().type) +
                             ", value = " + peek().value + ", line = " + std::to_string(peek().line));
  }
  return node;
}

std::unique_ptr<ASTNode> StatementParser::parse_expression_statement() {
  ExpressionParser expr_parser(tokens_, pos_);
  auto node = expr_parser.parse();
  if (peek().type == TokenType::SEMICOLON) {
    expect(TokenType::SEMICOLON);
  }
  return node;
}
