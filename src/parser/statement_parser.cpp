#include "vyn/parser/parser.hpp"
#include "vyn/parser/ast.hpp"
#include <stdexcept>
#include "vyn/parser/token.hpp" // Required for vyn::token::Token
#include "vyn/parser/statement_parser.hpp" // Added include for the header

namespace vyn {

StatementParser::StatementParser(const std::vector<token::Token>& tokens, size_t& pos, int indent_level, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser)
    : BaseParser(tokens, pos, file_path), type_parser_(type_parser), expr_parser_(expr_parser) {
    // indent_level is part of BaseParser or handled there if needed.
    // If BaseParser doesn't take indent_level, and it's needed here, add a member: this->indent_level_ = indent_level;
}

// Helper function to convert SourceLocation to string for error messages
std::string location_to_string(const SourceLocation& loc) {
    return "line " + std::to_string(loc.line) + ", column " + std::to_string(loc.column);
}

vyn::ast::StmtPtr StatementParser::parse() {
    while (!this->IsAtEnd() && this->peek().type == vyn::TokenType::NEWLINE) {
        this->consume(); // Skip empty lines
    }

    if (this->IsAtEnd()) {
        return nullptr;
    }

    vyn::token::Token current_token = this->peek();
    switch (current_token.type) {
        case vyn::TokenType::KEYWORD_LET:
        case vyn::TokenType::KEYWORD_MUT:
            return parse_var_decl();
        case vyn::TokenType::KEYWORD_IF:
            return parse_if();
        case vyn::TokenType::KEYWORD_WHILE:
            return parse_while();
        case vyn::TokenType::KEYWORD_FOR:
            return parse_for();
        case vyn::TokenType::KEYWORD_RETURN:
            return parse_return();
        case vyn::TokenType::LBRACE: // Block statement
            return parse_block();
        case vyn::TokenType::KEYWORD_BREAK:
            // TODO: Implement break statement parsing
            throw std::runtime_error("Break statement parsing not yet implemented at " + location_to_string(current_token.location));
        case vyn::TokenType::KEYWORD_CONTINUE:
            // TODO: Implement continue statement parsing
            throw std::runtime_error("Continue statement parsing not yet implemented at " + location_to_string(current_token.location));
        default:
            // Could be an expression statement or an error
            return parse_expression_statement();
    }
}

std::unique_ptr<vyn::ast::ExpressionStatement> StatementParser::parse_expression_statement() {
    auto expr = this->expr_parser_.parse_expression();
    // Assuming expr_parser_.parse_expression() throws or returns a valid expression,
    // and does not return nullptr on failing to parse an expression that should be there.
    SourceLocation expr_loc = expr->loc; // Location of the statement is the location of the expression.

    // Check for optional semicolon or other valid terminators
    if (this->match(vyn::TokenType::SEMICOLON)) {
        // Semicolon consumed, all good.
    } else if (this->peek().type == vyn::TokenType::NEWLINE ||
               this->IsAtEnd() ||
               this->peek().type == vyn::TokenType::RBRACE) {
        // Optional semicolon: if it's a newline, end of file, or start of a closing brace, it's fine.
        // Do not consume newline or RBRACE here, they might be significant for the outer structure.
    } else {
        throw std::runtime_error("Expected semicolon, newline, or '}' after expression statement at " +
                                 location_to_string(this->peek().location) + ", got " +
                                 token_type_to_string(this->peek().type));
    }
    return std::make_unique<vyn::ast::ExpressionStatement>(expr_loc, std::move(expr));
}


std::unique_ptr<vyn::ast::BlockStatement> StatementParser::parse_block() {
    SourceLocation start_loc = this->expect(vyn::TokenType::LBRACE, "Expected '{' to start a block.").location;
    std::vector<vyn::ast::StmtPtr> statements;
    SourceLocation end_loc = start_loc;

    while (!this->IsAtEnd() && this->peek().type != vyn::TokenType::RBRACE) {
        // Skip newlines within the block if they are not separating statements meaningfully
        while (!this->IsAtEnd() && this->peek().type == vyn::TokenType::NEWLINE) {
            this->consume();
        }
        if (this->IsAtEnd() || this->peek().type == vyn::TokenType::RBRACE) {
            break; // End of block or file
        }
        statements.push_back(parse()); // Parse the statement within the block
        // Ensure that after a statement, we either have another statement, a newline, or the end of the block
        if (!this->IsAtEnd() && this->peek().type != vyn::TokenType::RBRACE) {
            if (this->peek().type == vyn::TokenType::SEMICOLON) {
                this->consume(); // Consume optional semicolon
            } else if (this->peek().type == vyn::TokenType::NEWLINE) {
                // Fine, next statement or end of block might be on new line
            } else if (this->expr_parser_.is_expression_start(this->peek().type) || this->is_statement_start(this->peek().type)) { // Changed here
                // Next statement starts immediately, this is fine.
            }
             else if (this->peek().type != vyn::TokenType::RBRACE && !this->is_statement_start(this->peek().type) && !this->expr_parser_.is_expression_start(this->peek().type)) { // Changed here
                 throw std::runtime_error("Expected newline, semicolon, or end of block after statement at " + location_to_string(this->peek().location) + ", got " + token_type_to_string(this->peek().type));
             }
        }
    }

    end_loc = this->expect(vyn::TokenType::RBRACE, "Expected '}' to end a block.").location;
    return std::make_unique<vyn::ast::BlockStatement>(start_loc, std::move(statements));
}


std::unique_ptr<vyn::ast::IfStatement> StatementParser::parse_if() {
    SourceLocation if_loc = this->expect(vyn::TokenType::KEYWORD_IF, "Expected 'if'.").location;
    this->expect(vyn::TokenType::LPAREN, "Expected '(' after 'if'.");
    auto condition = this->expr_parser_.parse_expression();
    this->expect(vyn::TokenType::RPAREN, "Expected ')' after if condition.");
    auto then_branch = parse_block(); // 'if' body must be a block
    vyn::ast::StmtPtr else_branch = nullptr;
    SourceLocation end_loc = then_branch->loc; // Use loc member

    if (this->match(vyn::TokenType::KEYWORD_ELSE)) {
        if (this->peek().type == vyn::TokenType::KEYWORD_IF) { // 'else if'
            else_branch = parse_if(); // Recursively parse the 'else if'
        } else { // 'else'
            else_branch = parse_block(); // 'else' body must be a block
        }
        if (else_branch) {
            end_loc = else_branch->loc; // Use loc member
        }
    }

    return std::make_unique<vyn::ast::IfStatement>(if_loc, std::move(condition), std::move(then_branch), std::move(else_branch));
}

std::unique_ptr<vyn::ast::WhileStatement> StatementParser::parse_while() {
    SourceLocation while_loc = this->expect(vyn::TokenType::KEYWORD_WHILE, "Expected 'while'.").location;
    this->expect(vyn::TokenType::LPAREN, "Expected '(' after 'while'.");
    auto condition = this->expr_parser_.parse_expression();
    this->expect(vyn::TokenType::RPAREN, "Expected ')' after while condition.");
    auto body = parse_block(); // 'while' body must be a block
    SourceLocation end_loc = body->loc; // Use loc member
    return std::make_unique<vyn::ast::WhileStatement>(while_loc, std::move(condition), std::move(body));
}

std::unique_ptr<vyn::ast::ForStatement> StatementParser::parse_for() {
    SourceLocation for_loc = this->expect(vyn::TokenType::KEYWORD_FOR, "Expected 'for'.").location;
    this->expect(vyn::TokenType::LPAREN, "Expected '(' after 'for'.");

    vyn::ast::StmtPtr initializer = nullptr;
    if (this->peek().type == vyn::TokenType::KEYWORD_LET || this->peek().type == vyn::TokenType::KEYWORD_MUT) {
        initializer = parse_var_decl(); // Parses the full variable declaration including semicolon
    } else if (this->peek().type != vyn::TokenType::SEMICOLON) {
        initializer = parse_expression_statement(); // Parses expression then expects semicolon
    } else {
        this->expect(vyn::TokenType::SEMICOLON, "Expected semicolon after empty for-loop initializer."); // Consume semicolon for empty initializer
    }

    vyn::ast::ExprPtr condition = nullptr;
    if (this->peek().type != vyn::TokenType::SEMICOLON) {
        condition = this->expr_parser_.parse_expression();
    }
    this->expect(vyn::TokenType::SEMICOLON, "Expected semicolon after for-loop condition.");

    vyn::ast::ExprPtr increment = nullptr;
    if (this->peek().type != vyn::TokenType::RPAREN) {
        increment = this->expr_parser_.parse_expression();
    }
    this->expect(vyn::TokenType::RPAREN, "Expected ')' after for-loop clauses.");

    auto body = parse_block(); // 'for' body must be a block
    SourceLocation end_loc = body->loc; // Use loc member

    return std::make_unique<vyn::ast::ForStatement>(for_loc, std::move(initializer), std::move(condition), std::move(increment), std::move(body));
}

std::unique_ptr<vyn::ast::ReturnStatement> StatementParser::parse_return() {
    SourceLocation return_loc = this->expect(vyn::TokenType::KEYWORD_RETURN, "Expected 'return'.").location;
    vyn::ast::ExprPtr value = nullptr;
    SourceLocation end_loc = return_loc;

    if (this->peek().type != vyn::TokenType::SEMICOLON && this->peek().type != vyn::TokenType::NEWLINE && this->peek().type != vyn::TokenType::RBRACE) {
        value = this->expr_parser_.parse_expression();
        end_loc = value->loc; // Use loc member
    }

    if (this->peek().type == vyn::TokenType::SEMICOLON) {
        end_loc = this->peek().location;
        this->consume(); // Consume semicolon
    } else if (this->peek().type == vyn::TokenType::NEWLINE || this->IsAtEnd() || this->peek().type == vyn::TokenType::RBRACE) {
        // Optional semicolon at the end of a line or before closing brace
    } else {
        throw std::runtime_error("Expected semicolon or newline after return statement at " + location_to_string(this->peek().location));
    }

    return std::make_unique<vyn::ast::ReturnStatement>(return_loc, std::move(value));
}

std::unique_ptr<vyn::ast::VariableDeclaration> StatementParser::parse_var_decl() {
    SourceLocation keyword_loc = this->peek().location;
    bool is_mutable = false;
    if (this->match(vyn::TokenType::KEYWORD_MUT)) {
        is_mutable = true;
    } else {
        this->expect(vyn::TokenType::KEYWORD_LET, "Expected 'let' or 'mut' for variable declaration.");
    }

    vyn::token::Token name_token = this->expect(vyn::TokenType::IDENTIFIER, "Expected variable name.");
    std::string var_name = name_token.lexeme; // Use lexeme for value
    auto identifier_node = std::make_unique<vyn::ast::Identifier>(name_token.location, var_name);

    std::unique_ptr<vyn::ast::TypeNode> type_expr = nullptr; // Changed from TypeIdentifier / TypeExpression
    if (this->match(vyn::TokenType::COLON)) { // Optional type annotation
        type_expr = this->type_parser_.parse(); // Changed from parse_type_identifier / parse_type_expression
    }

    vyn::ast::ExprPtr initializer = nullptr;
    SourceLocation end_loc = name_token.location; // Default end_loc if no initializer

    if (this->match(vyn::TokenType::EQ)) {
        initializer = this->expr_parser_.parse_expression();
        if (initializer) {
            end_loc = initializer->loc; // Use loc member
        }
    } else if (!type_expr) {
        // If there's no type annotation, an initializer is required.
        // However, this rule might be better enforced in a semantic analysis phase.
        // For now, the parser allows it, assuming it might be valid in some contexts (e.g. extern).
        // Consider adding a check or relying on semantic analysis.
    }


    if (this->peek().type == vyn::TokenType::SEMICOLON) {
        end_loc = this->peek().location;
        this->consume(); // Consume semicolon
    } else if (this->peek().type == vyn::TokenType::NEWLINE || this->IsAtEnd() || this->peek().type == vyn::TokenType::RBRACE) {
        // Optional semicolon at the end of a line or before closing brace
    }
    else {
        throw std::runtime_error("Expected semicolon or newline after variable declaration at " + location_to_string(this->peek().location) + ", got " + token_type_to_string(this->peek().type));
    }

    // Create Identifier node for the variable name
    auto var_id_node = std::make_unique<vyn::ast::Identifier>(name_token.location, var_name);

    return std::make_unique<vyn::ast::VariableDeclaration>(
        keyword_loc, // start location is 'let' or 'mut'
        std::move(var_id_node), // Pass the Identifier node
        !is_mutable, // isConst is the opposite of is_mutable
        std::move(type_expr),
        std::move(initializer)
        // end_loc is implicitly handled by the base Node class or should be set if VariableDeclaration needs it explicitly
    );
}

vyn::ast::ExprPtr StatementParser::parse_pattern() {
    // For now, a simple pattern is just an identifier.
    // This will be expanded for destructuring, etc.
    vyn::token::Token id_token = this->expect(vyn::TokenType::IDENTIFIER, "Expected identifier in pattern.");
    return std::make_unique<vyn::ast::Identifier>(id_token.location, id_token.lexeme); // Use lexeme
}


bool StatementParser::is_statement_start(vyn::TokenType type) const {
    switch (type) {
        case vyn::TokenType::KEYWORD_LET:
        case vyn::TokenType::KEYWORD_MUT:
        case vyn::TokenType::KEYWORD_IF:
        case vyn::TokenType::KEYWORD_WHILE:
        case vyn::TokenType::KEYWORD_FOR:
        case vyn::TokenType::KEYWORD_RETURN:
        case vyn::TokenType::LBRACE: // Start of a block statement
        case vyn::TokenType::KEYWORD_BREAK:
        case vyn::TokenType::KEYWORD_CONTINUE:
        // Expression statements can start with anything an expression can start with
        // This is handled by falling through to is_expression_start or by the default case in parse()
            return true;
        default:
            return this->expr_parser_.is_expression_start(type); // Changed: An expression can also be a statement
    }
}

} // namespace vyn
