#include "vyn/parser.hpp"
#include <stdexcept>

DeclarationParser::DeclarationParser(const std::vector<Token>& tokens, size_t& pos)
    : BaseParser(tokens, pos) {}

std::unique_ptr<ASTNode> DeclarationParser::parse() {
  if (peek().type == TokenType::KEYWORD_TEMPLATE) {
    return parse_template();
  } else if (peek().type == TokenType::KEYWORD_FN || peek().type == TokenType::KEYWORD_ASYNC) {
    return parse_function();
  } else if (peek().type == TokenType::KEYWORD_CLASS) {
    return parse_class();
  } else if (peek().type == TokenType::KEYWORD_VAR || peek().type == TokenType::KEYWORD_CONST ||
             peek().type == TokenType::KEYWORD_IMPORT || peek().type == TokenType::KEYWORD_SMUGGLE ||
             peek().type == TokenType::KEYWORD_SCOPED || peek().type == TokenType::KEYWORD_MATCH) {
    StatementParser stmt_parser(tokens_, pos_, indent_levels_.back());
    return stmt_parser.parse();
  } else if (peek().type == TokenType::LBRACE || peek().type == TokenType::INDENT) {
    return parse_block();
  } else {
    StatementParser stmt_parser(tokens_, pos_, indent_levels_.back());
    return stmt_parser.parse_expression_statement();
  }
}

std::unique_ptr<ASTNode> DeclarationParser::parse_template() {
  Token template_keyword_token = peek(); // Token for the TemplateDecl node itself
  expect(TokenType::KEYWORD_TEMPLATE);
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::TemplateDecl, template_keyword_token);

  Token template_name_token = expect(TokenType::IDENTIFIER); // Consume and get template name
  node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, template_name_token));

  Token lt_peek_token = peek(); // Peek before matching '<'
  if (match(TokenType::LT)) {
    auto params_node = std::make_unique<ASTNode>(ASTNode::Kind::Type, lt_peek_token); // Use the '<' token for the Type node
    if (peek().type != TokenType::GT) { // Check if there's at least one parameter
        do {
            Token param_id_token = expect(TokenType::IDENTIFIER); // Consume and get parameter identifier
            params_node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, param_id_token));
            
            if (match(TokenType::COLON)) { // Optional type constraint
                TypeParser type_parser(tokens_, pos_);
                params_node->children.push_back(type_parser.parse());
            }
        } while (match(TokenType::COMMA)); // Continue if comma is found
    }
    expect(TokenType::GT); // Expect '>' at the end of template parameters
    node->children.push_back(std::move(params_node));
  }
  node->children.push_back(parse_block()); // Parse the body of the template
  return node;
}

std::unique_ptr<ASTNode> DeclarationParser::parse_function() {
  Token func_decl_token = peek(); // This will be 'async' or 'fn'
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::FunctionDecl, func_decl_token);
  
  bool is_async = match(TokenType::KEYWORD_ASYNC);
  expect(TokenType::KEYWORD_FN); // 'fn' must follow 'async' or be present itself
  
  if (is_async) {
    // If 'async' was matched, func_decl_token is already the 'async' token.
    // The original code re-assigned node->token here.
    // node->token = Token(TokenType::KEYWORD_ASYNC, "async", func_decl_token.line, func_decl_token.column);
    // This is fine, ensures the primary token is 'async' if present.
  }

  Token func_name_token = expect(TokenType::IDENTIFIER); // Consume and get function name
  node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, func_name_token));
  
  expect(TokenType::LPAREN);
  if (peek().type != TokenType::RPAREN) {
    Token params_list_token = peek(); // Token for the parameters list ASTNode
    auto params_node = std::make_unique<ASTNode>(ASTNode::Kind::Expression, params_list_token);
    do {
      bool is_ref = match(TokenType::AMPERSAND); // Optional '&'

      Token first_ident_peek = peek(); // Peek for "mut" or param_name
      bool is_mut = false;
      // Initialize with a placeholder/invalid token
      Token param_name_token(TokenType::INVALID, "", 0, 0); 

      if (first_ident_peek.type == TokenType::IDENTIFIER && first_ident_peek.value == "mut") {
        consume(); // Consume "mut" token
        is_mut = true;
        param_name_token = expect(TokenType::IDENTIFIER); // Expect and consume parameter name after "mut"
      } else {
        param_name_token = expect(TokenType::IDENTIFIER); // Expect and consume parameter name
      }
      
      auto param_node = std::make_unique<ASTNode>(ASTNode::Kind::Identifier, param_name_token);

      // if (is_ref) {
      //   // Original line: param_node->token = Token(TokenType::AMPERSAND, "&", param_node->token.line, param_node->token.column);
      //   // This is problematic as it overwrites the identifier token and loses original token info for the name.
      //   // A better AST representation for 'ref' (e.g., a flag or a wrapper node) is needed.
      // }

      if (is_mut) {
        // 'first_ident_peek' was the "mut" token. Use its line/col for the "mut" ASTNode.
        param_node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, Token(TokenType::IDENTIFIER, "mut", first_ident_peek.line, first_ident_peek.column)));
      }
      
      if (match(TokenType::COLON)) { // Optional type annotation
        TypeParser type_parser(tokens_, pos_);
        param_node->children.push_back(type_parser.parse());
      }
      params_node->children.push_back(std::move(param_node));
    } while (match(TokenType::COMMA)); // Continue if comma is found
    node->children.push_back(std::move(params_node));
  }
  expect(TokenType::RPAREN);
  
  if (match(TokenType::ARROW)) { // Optional return type
    TypeParser type_parser(tokens_, pos_);
    node->children.push_back(type_parser.parse());
  }
  
  Token throws_peek = peek();
  if (throws_peek.type == TokenType::IDENTIFIER && throws_peek.value == "throws") {
    expect(TokenType::IDENTIFIER); // Consume "throws"
    Token error_type_token = expect(TokenType::IDENTIFIER); // Consume and get error type
    node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, error_type_token));
  }
  
  node->children.push_back(parse_block()); // Parse function body
  return node;
}

std::unique_ptr<ASTNode> DeclarationParser::parse_class() {
  Token class_keyword_token = expect(TokenType::KEYWORD_CLASS); // Consume "class" and get the token
  auto node = std::make_unique<ASTNode>(ASTNode::Kind::ClassDecl, class_keyword_token);

  Token class_name_token = expect(TokenType::IDENTIFIER);      // Consume IDENTIFIER for class name and get the token
  node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, class_name_token));
  
  node->children.push_back(parse_block());
  return node;
}

std::unique_ptr<ASTNode> DeclarationParser::parse_block() {
  StatementParser stmt_parser(tokens_, pos_, indent_levels_.back());
  return stmt_parser.parse_block();
}
