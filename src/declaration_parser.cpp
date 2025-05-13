#include "vyn/parser.hpp"
#include <stdexcept>

DeclarationParser::DeclarationParser(const std::vector<Token>& tokens, size_t& pos)
    : BaseParser(tokens, pos) {}

// Helper function to identify overloadable operator tokens
bool is_operator_token(TokenType type) {
    switch (type) {
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::MULTIPLY:
        case TokenType::DIVIDE:
        case TokenType::MODULO:
        case TokenType::EQEQ:
        case TokenType::NOTEQ:
        case TokenType::LT:
        case TokenType::GT:
        case TokenType::LTEQ:
        case TokenType::GTEQ:
        // TODO: Add other overloadable operators like LBRACKET (for []), LPAREN (for ()), etc.
            return true;
        default:
            return false;
    }
}

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
    bool is_async = false;
    if (peek().type == TokenType::KEYWORD_ASYNC) {
        consume(); // consume 'async'
        is_async = true;
    }

    Token func_keyword_token = expect(TokenType::KEYWORD_FN); // Consume 'fn'
    
    std::unique_ptr<ASTNode> node;
    Token func_name_or_operator_token = peek();

    if (func_name_or_operator_token.type == TokenType::KEYWORD_OPERATOR) {
        consume(); // Consume KEYWORD_OPERATOR
        Token operator_actual_token = peek();
        if (is_operator_token(operator_actual_token.type)) {
            consume(); // Consume the actual operator token (e.g., PLUS, MINUS)
            // Use the operator token itself (e.g. PLUS) for the FunctionDecl node's primary token information
            node = std::make_unique<ASTNode>(ASTNode::Kind::FunctionDecl, operator_actual_token);
            // Store "operator" keyword token or a flag if needed, e.g., in attributes or as a child.
            // For now, the name/identity is the operator symbol itself.
        } else {
            throw std::runtime_error("Expected an operator symbol (e.g., '+', '==') after 'operator' keyword at line " + 
                                     std::to_string(operator_actual_token.line) + ", column " + std::to_string(operator_actual_token.column));
        }
    } else {
        // If not KEYWORD_OPERATOR, it must be an IDENTIFIER (or it's an error that expect() will handle)
        Token actual_func_name_token = expect(TokenType::IDENTIFIER); 
        node = std::make_unique<ASTNode>(ASTNode::Kind::FunctionDecl, actual_func_name_token);
    }

    if (is_async) {
      node->is_async = true;
    }
  
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
  expect(TokenType::RPAREN); // Ensures closing parenthesis of parameters is consumed.
  
  // Optional: throws clause (e.g., "throws NetworkError")
  // Assumes "throws" is lexed as an IDENTIFIER token with the value "throws".
  if (peek().type == TokenType::IDENTIFIER && peek().value == "throws") {
    consume(); // Consume the "throws" keyword token.
    
    // Expect an identifier for the error type.
    if (peek().type == TokenType::IDENTIFIER) {
        Token error_type_token = consume(); // Consume the error type token.
        // Add the error type as a child to the function node.
        // A more structured AST might use a specific ASTNode::Kind for the throws clause or error type.
        node->children.push_back(std::make_unique<ASTNode>(ASTNode::Kind::Identifier, error_type_token));
    } else {
        // "throws" keyword was present, but no error type identifier followed.
        throw std::runtime_error("Expected error type identifier after 'throws' at line " + std::to_string(peek().line));
    }
  }

  // Optional: return type (e.g., "-> String")
  if (match(TokenType::ARROW)) {
    TypeParser type_parser(tokens_, pos_); // pos_ is current position after ARROW
    std::unique_ptr<ASTNode> return_type_node = type_parser.parse();
    pos_ = type_parser.get_current_pos(); // Explicitly update DeclarationParser's position
    node->children.push_back(std::move(return_type_node));
  }
  
  // Parse the function body (block of statements).
  // pos_ should now be at the start of the function body (e.g., '{' or INDENT).
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
