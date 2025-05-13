#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include <stdexcept> // For std::runtime_error
#include <vector> // for std::vector
#include <memory> // for std::unique_ptr

// Constructor updated
StatementParser::StatementParser(const std::vector<Vyn::Token>& tokens, size_t& pos, int indent_level, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser)
    : BaseParser(tokens, pos, file_path), indent_level_(indent_level), type_parser_(type_parser), expr_parser_(expr_parser) {}

std::unique_ptr<Vyn::AST::StmtNode> StatementParser::parse() {
    skip_comments_and_newlines();
    Vyn::Token current_token = peek();
    Vyn::AST::SourceLocation loc = current_location();

    // Dispatch based on the token type
    // Example:
    if (current_token.type == Vyn::TokenType::KEYWORD_IF) {
        return parse_if();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_WHILE) {
        return parse_while();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_FOR) {
        return parse_for();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_RETURN) {
        return parse_return();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_LET || current_token.type == Vyn::TokenType::KEYWORD_VAR) { // Assuming 'var' also for mut variables
        return parse_var_decl();
    } else if (current_token.type == Vyn::TokenType::LBRACE) { // Block statement
        return parse_block();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_BREAK) {
        consume(); // consume 'break'
        return std::make_unique<Vyn::AST::BreakStmtNode>(loc);
    } else if (current_token.type == Vyn::TokenType::KEYWORD_CONTINUE) {
        consume(); // consume 'continue'
        return std::make_unique<Vyn::AST::ContinueStmtNode>(loc);
    }
    // ... other statement types ...
    
    // Default to expression statement if no other keyword matches
    // This needs to be careful not to consume tokens that start another valid statement type
    // that isn't explicitly checked above yet.
    auto expr_stmt = parse_expression_statement();
    if (expr_stmt && expr_stmt->expression) { // Ensure it's a valid expression statement
        return expr_stmt;
    }

    // If it's not a recognized statement and not a valid expression statement that could be parsed
    if (current_token.type != Vyn::TokenType::EOF_TOKEN && current_token.type != Vyn::TokenType::DEDENT && current_token.type != Vyn::TokenType::RBRACE) {
        // Avoid erroring on RBRACE or DEDENT as those terminate blocks.
        throw std::runtime_error("Unexpected token in StatementParser: " + current_token.value + " at " + loc.toString());
    }
    
    return nullptr; // No statement found (e.g. EOF, or end of block)
}

std::unique_ptr<Vyn::AST::ExpressionStmtNode> StatementParser::parse_expression_statement() {
    Vyn::AST::SourceLocation loc = current_location();
    // Try to parse an expression. If it succeeds, wrap it in an ExpressionStmtNode.
    // Need to handle semicolon for languages that require it. Vyn's policy?
    // Assuming Vyn uses newlines or semicolons to terminate expression statements.
    // The skip_comments_and_newlines() at the start of parse() handles leading newlines.
    
    // Check if the current token can start an expression.
    // This is a heuristic; a more robust way is to try parsing and catch failure,
    // or have ExpressionParser::peek_is_start_of_expression().
    if (peek().type == Vyn::TokenType::RBRACE || peek().type == Vyn::TokenType::EOF_TOKEN || peek().type == Vyn::TokenType::DEDENT) {
        return nullptr; // Not an expression statement.
    }

    auto expr = expr_parser_.parse(); // Use the injected expression parser
    if (expr) {
        // Optional: expect(Vyn::TokenType::SEMICOLON); if Vyn requires semicolons for expression statements.
        // If semicolons are optional (e.g. Python-like), this is fine.
        return std::make_unique<Vyn::AST::ExpressionStmtNode>(loc, std::move(expr));
    }
    return nullptr; // Failed to parse a valid expression
}

std::unique_ptr<Vyn::AST::BlockStmtNode> StatementParser::parse_block() {
    Vyn::AST::SourceLocation loc = current_location();
    expect(Vyn::TokenType::LBRACE);
    std::vector<std::unique_ptr<Vyn::AST::StmtNode>> statements;
    // TODO: Handle indentation if Vyn is indentation-sensitive within { } or only uses { }
    // Assuming { } delimited blocks for now.
    // The indent_level_ member might be used if Vyn supports Python-style blocks without braces.

    skip_comments_and_newlines();
    while (peek().type != Vyn::TokenType::RBRACE && peek().type != Vyn::TokenType::EOF_TOKEN) {
        auto stmt = parse(); // Recursive call to parse statements within the block
        if (stmt) {
            statements.push_back(std::move(stmt));
        } else {
            // parse() returned nullptr, but we are not at RBRACE or EOF.
            // This could mean an error was already thrown by a sub-parser,
            // or parse() decided it's not a statement (e.g. only a comment line).
            // If parse() can return nullptr for non-errors (like only comments),
            // we might need to advance past such tokens if no statement was parsed.
            // For now, assume parse() throws or returns a valid statement if one is present.
            // If it returns nullptr and it's not RBRACE/EOF, it's likely an issue.
            // However, parse() itself now throws if it can't determine a statement.
            // So, if we are here, it means parse() returned nullptr due to EOF/DEDENT/RBRACE,
            // which shouldn't happen inside a LBRACE...RBRACE block unless parse() logic is flawed.
            // Let's assume if parse() returns nullptr, we should break or error.
            // Given parse() now throws on unexpected token, a nullptr here means it hit a terminator.
            break; 
        }
        skip_comments_and_newlines(); // After each statement
    }
    expect(Vyn::TokenType::RBRACE);
    return std::make_unique<Vyn::AST::BlockStmtNode>(loc, std::move(statements));
}


// Placeholders for other statement parsing methods
std::unique_ptr<Vyn::AST::IfStmtNode> StatementParser::parse_if() {
    Vyn::AST::SourceLocation loc = current_location();
    expect(Vyn::TokenType::KEYWORD_IF);

    // Condition
    expect(Vyn::TokenType::LPAREN); // Assuming Vyn 'if' conditions are in parentheses like C/Java/Rust
    auto condition = expr_parser_.parse();
    if (!condition) {
        throw std::runtime_error("Expected condition in if statement at " + current_location().toString());
    }
    expect(Vyn::TokenType::RPAREN);

    // Then branch (must be a block)
    if (peek().type != Vyn::TokenType::LBRACE) {
        throw std::runtime_error("Expected block statement for 'then' branch of if statement at " + current_location().toString());
    }
    auto then_branch = parse_block();
    if (!then_branch) {
        // parse_block should throw on failure, but as a safeguard:
        throw std::runtime_error("Failed to parse 'then' block in if statement at " + loc.toString());
    }

    // Else branch
    std::unique_ptr<Vyn::AST::StmtNode> else_branch = nullptr;
    if (match(Vyn::TokenType::KEYWORD_ELSE)) {
        if (peek().type == Vyn::TokenType::KEYWORD_IF) { // Else if
            else_branch = parse_if(); // Recursive call for 'else if'
        } else if (peek().type == Vyn::TokenType::LBRACE) { // Else block
            else_branch = parse_block();
            if (!else_branch) {
                 throw std::runtime_error("Failed to parse 'else' block in if statement at " + current_location().toString());
            }
        } else {
            throw std::runtime_error("Expected 'if' or block after 'else' at " + current_location().toString());
        }
    }

    return std::make_unique<Vyn::AST::IfStmtNode>(loc, std::move(condition), std::move(then_branch), std::move(else_branch));
}

std::unique_ptr<Vyn::AST::WhileStmtNode> StatementParser::parse_while() {
    Vyn::AST::SourceLocation loc = current_location();
    expect(Vyn::TokenType::KEYWORD_WHILE);

    // Condition
    expect(Vyn::TokenType::LPAREN); // Assuming Vyn 'while' conditions are in parentheses
    auto condition = expr_parser_.parse();
    if (!condition) {
        throw std::runtime_error("Expected condition in while statement at " + current_location().toString());
    }
    expect(Vyn::TokenType::RPAREN);

    // Body (must be a block)
    if (peek().type != Vyn::TokenType::LBRACE) {
        throw std::runtime_error("Expected block statement for while loop body at " + current_location().toString());
    }
    auto body = parse_block();
    if (!body) {
        throw std::runtime_error("Failed to parse while loop body at " + loc.toString());
    }

    return std::make_unique<Vyn::AST::WhileStmtNode>(loc, std::move(condition), std::move(body));
}

std::unique_ptr<Vyn::AST::ForStmtNode> StatementParser::parse_for() {
    Vyn::AST::SourceLocation loc = current_location();
    expect(Vyn::TokenType::KEYWORD_FOR);

    expect(Vyn::TokenType::LPAREN); // Assuming Vyn 'for' loops use parentheses for setup

    // Pattern
    auto pattern = parse_pattern();
    if (!pattern) {
        throw std::runtime_error("Expected pattern in for loop at " + current_location().toString());
    }

    expect(Vyn::TokenType::KEYWORD_IN); // Or similar keyword like COLON in Python for item : iterable

    // Iterable
    auto iterable = expr_parser_.parse();
    if (!iterable) {
        throw std::runtime_error("Expected iterable expression in for loop at " + current_location().toString());
    }
    
    expect(Vyn::TokenType::RPAREN);


    // Body (must be a block)
    if (peek().type != Vyn::TokenType::LBRACE) {
        throw std::runtime_error("Expected block statement for for loop body at " + current_location().toString());
    }
    auto body = parse_block();
    if (!body) {
        throw std::runtime_error("Failed to parse for loop body at " + loc.toString());
    }

    return std::make_unique<Vyn::AST::ForStmtNode>(loc, std::move(pattern), std::move(iterable), std::move(body));
}

std::unique_ptr<Vyn::AST::ReturnStmtNode> StatementParser::parse_return() {
    Vyn::AST::SourceLocation loc = current_location();
    expect(Vyn::TokenType::KEYWORD_RETURN);
    std::unique_ptr<Vyn::AST::ExprNode> value = nullptr;
    // Check if there's an expression to return (e.g., not followed by ';', '}', or newline if expression is optional)
    // This depends on Vyn's grammar for `return;` vs `return expr;`
    if (peek().type != Vyn::TokenType::SEMICOLON && peek().type != Vyn::TokenType::RBRACE && peek().type != Vyn::TokenType::NEWLINE && peek().type != Vyn::TokenType::EOF_TOKEN && peek().type != Vyn::TokenType::DEDENT) {
        value = expr_parser_.parse();
    }
    // Optional: expect(Vyn::TokenType::SEMICOLON);
    return std::make_unique<Vyn::AST::ReturnStmtNode>(loc, std::move(value));
}


std::unique_ptr<Vyn::AST::VarDeclStmtNode> StatementParser::parse_var_decl() {
    Vyn::AST::SourceLocation loc = current_location();
    bool is_mutable_decl = false; // Renamed to avoid conflict with pattern's mutability
    if (peek().type == Vyn::TokenType::KEYWORD_LET) {
        consume(); // 'let'
        is_mutable_decl = false;
    } else if (peek().type == Vyn::TokenType::KEYWORD_VAR) {
        consume(); // 'var'
        is_mutable_decl = true;
    } else {
        throw std::runtime_error("Expected 'let' or 'var' for variable declaration at " + loc.toString());
    }

    // Pass the declaration's mutability to parse_pattern
    auto pattern = parse_pattern(is_mutable_decl);
    if (!pattern) {
        throw std::runtime_error("Expected pattern after 'let'/'var' at " + current_location().toString());
    }

    std::unique_ptr<Vyn::AST::TypeNode> type_annotation = nullptr;
    if (match(Vyn::TokenType::COLON)) {
        type_annotation = type_parser_.parse();
        if (!type_annotation) {
            throw std::runtime_error("Expected type annotation after ':' in variable declaration at " + current_location().toString());
        }
    }

    std::unique_ptr<Vyn::AST::ExprNode> initializer = nullptr;
    if (match(Vyn::TokenType::EQ)) {
        initializer = expr_parser_.parse();
        if (!initializer) {
            throw std::runtime_error("Expected initializer expression after '=' in variable declaration at " + current_location().toString());
        }
    } else {
        // Vyn language rule: if no type annotation, initializer is mandatory.
        // If type annotation is present, initializer can be optional (value will be default/uninitialized based on type system).
        // For now, let's enforce initializer if no type annotation.
        if (!type_annotation && !is_mutable_decl) { // `let` requires initializer if no type, `var` could be uninit if allowed by type system
            // This rule might need refinement based on language spec for `var`.
            // For now, keeping the original logic: initializer mandatory if no type annotation.
             throw std::runtime_error("Variable declaration requires an initializer if type is not specified, at " + loc.toString());
        }
    }
    
    expect(Vyn::TokenType::SEMICOLON); // Enforce semicolon for variable declarations

    return std::make_unique<Vyn::AST::VarDeclStmtNode>(loc, std::move(pattern), std::move(type_annotation), std::move(initializer), is_mutable_decl);
}

std::unique_ptr<Vyn::AST::PatternNode> StatementParser::parse_pattern(bool is_context_mutable) {
    Vyn::AST::SourceLocation loc = current_location();
    skip_comments_and_newlines();
    Vyn::Token token = peek();

    // Wildcard: _
    if (match(Vyn::TokenType::UNDERSCORE)) {
        return std::make_unique<Vyn::AST::WildcardPatternNode>(loc);
    }

    // Tuple Pattern: (p1, p2, ...)
    if (token.type == Vyn::TokenType::LPAREN) {
        consume(); // LPAREN
        std::vector<std::unique_ptr<Vyn::AST::PatternNode>> elements;
        if (peek().type != Vyn::TokenType::RPAREN) {
            do {
                // Propagate mutability context to sub-patterns
                auto elem_pattern = parse_pattern(is_context_mutable);
                if (!elem_pattern) {
                    throw std::runtime_error("Expected pattern inside tuple pattern at " + current_location().toString());
                }
                elements.push_back(std::move(elem_pattern));
            } while (match(Vyn::TokenType::COMMA));
        }
        expect(Vyn::TokenType::RPAREN);
        return std::make_unique<Vyn::AST::TuplePatternNode>(loc, std::move(elements));
    }

    // Check for 'mut' keyword before an identifier pattern
    bool is_explicitly_mut_pattern = false;
    Vyn::AST::SourceLocation mut_keyword_loc = current_location(); // For potential 'mut' keyword
    if (match(Vyn::TokenType::KEYWORD_MUT)) {
        is_explicitly_mut_pattern = true;
        // After 'mut', we expect an identifier or a pattern that can be made mutable.
        // For now, assuming 'mut' only applies to simple identifier patterns directly.
        // If 'mut' can prefix other patterns like 'mut (a,b)' or 'mut Point{x,y}', this logic needs expansion.
        token = peek(); // Update token after consuming 'mut'
        loc = current_location(); // Update loc to the start of the actual pattern after 'mut'
    }

    // Identifier, EnumVariant, or Struct Pattern
    if (token.type == Vyn::TokenType::IDENTIFIER) {
        Vyn::AST::SourceLocation path_start_loc = loc; // Use updated loc if 'mut' was present
        std::vector<std::unique_ptr<Vyn::AST::IdentifierNode>> segments;
        
        segments.push_back(std::make_unique<Vyn::AST::IdentifierNode>(current_location(), consume().value));

        while (peek().type == Vyn::TokenType::DOUBLE_COLON) {
            consume(); // Consume '::'
            if (peek().type != Vyn::TokenType::IDENTIFIER) {
                throw std::runtime_error("Expected identifier after '::' in qualified name pattern at " + current_location().toString());
            }
            segments.push_back(std::make_unique<Vyn::AST::IdentifierNode>(current_location(), consume().value));
        }
        
        auto qualified_path = std::make_unique<Vyn::AST::PathNode>(path_start_loc, std::move(segments));

        if (peek().type == Vyn::TokenType::LPAREN) {
            if (is_explicitly_mut_pattern) {
                 throw std::runtime_error("'mut' keyword cannot precede an enum variant pattern: " + qualified_path->getPathString() + " at " + mut_keyword_loc.toString());
            }
            consume(); // LPAREN
            std::vector<std::unique_ptr<Vyn::AST::PatternNode>> arguments;
            if (peek().type != Vyn::TokenType::RPAREN) {
                do {
                    auto arg_pattern = parse_pattern(is_context_mutable); // Propagate context mutability
                    if (!arg_pattern) {
                        throw std::runtime_error("Expected pattern argument for enum variant '" + qualified_path->getPathString() + "' at " + current_location().toString());
                    }
                    arguments.push_back(std::move(arg_pattern));
                } while (match(Vyn::TokenType::COMMA));
            }
            expect(Vyn::TokenType::RPAREN);
            return std::make_unique<Vyn::AST::EnumVariantPatternNode>(qualified_path->location, std::move(qualified_path), std::move(arguments));
        }
        else if (peek().type == Vyn::TokenType::LBRACE) {
            if (is_explicitly_mut_pattern) {
                 throw std::runtime_error("'mut' keyword cannot precede a struct pattern: " + qualified_path->getPathString() + " at " + mut_keyword_loc.toString());
            }
            consume(); // LBRACE
            std::vector<Vyn::AST::StructPatternField> fields;
            if (peek().type != Vyn::TokenType::RBRACE) {
                do {
                    Vyn::AST::SourceLocation field_loc = current_location();
                    if (peek().type != Vyn::TokenType::IDENTIFIER) {
                        throw std::runtime_error("Expected field name in struct pattern for '" + qualified_path->getPathString() + "' at " + current_location().toString());
                    }
                    auto field_name_node = std::make_unique<Vyn::AST::IdentifierNode>(current_location(), consume().value);
                    
                    expect(Vyn::TokenType::COLON);
                    
                    auto field_pattern = parse_pattern(is_context_mutable); // Propagate context mutability
                    if (!field_pattern) {
                        throw std::runtime_error("Expected pattern for field '" + field_name_node->name + "' in struct pattern for '" + qualified_path->getPathString() + "' at " + current_location().toString());
                    }
                    fields.emplace_back(field_loc, std::move(field_name_node), std::move(field_pattern));
                } while (match(Vyn::TokenType::COMMA));
            }
            expect(Vyn::TokenType::RBRACE);
            return std::make_unique<Vyn::AST::StructPatternNode>(qualified_path->location, std::move(qualified_path), std::move(fields));
        }
        
        if (qualified_path->segments.size() > 1) {
            if (is_explicitly_mut_pattern) {
                 throw std::runtime_error("'mut' keyword cannot precede a qualified path: " + qualified_path->getPathString() + " at " + mut_keyword_loc.toString());
            }
            throw std::runtime_error("Unexpected '::' in simple identifier pattern '" + qualified_path->getPathString() + "' at " + qualified_path->location.toString());
        }
        Vyn::AST::SourceLocation identifier_loc = qualified_path->segments[0]->location; 
        // Determine mutability: 'mut' keyword takes precedence. Otherwise, use context from 'let'/'var'.
        bool final_is_mutable = is_explicitly_mut_pattern || is_context_mutable;
        return std::make_unique<Vyn::AST::IdentifierPatternNode>(identifier_loc, std::move(qualified_path->segments[0]), final_is_mutable);
    }

    // If 'mut' was present but not followed by an identifier, it's an error.
    if (is_explicitly_mut_pattern) {
        throw std::runtime_error("Expected identifier after 'mut' keyword in pattern at " + mut_keyword_loc.toString());
    }

    // Literal Pattern: try to parse an atomic expression, check if it's a literal
    // This is a bit indirect. A direct literal token check might be cleaner.
    // For now, leveraging ExpressionParser::parse_atom()
    // We need to be careful: parse_atom() consumes tokens. If it's not a literal, we might have issues.
    // A safer way: peek for literal tokens directly.
    if (token.type == Vyn::TokenType::INT_LITERAL ||
        token.type == Vyn::TokenType::FLOAT_LITERAL ||
        token.type == Vyn::TokenType::STRING_LITERAL ||
        token.type == Vyn::TokenType::CHAR_LITERAL ||
        token.type == Vyn::TokenType::KEYWORD_TRUE ||
        token.type == Vyn::TokenType::KEYWORD_FALSE ||
        token.type == Vyn::TokenType::KEYWORD_NULL) {
        
        // Use expr_parser_.parse_atom() to create the literal expression node
        // This requires ExpressionParser to be able to parse just one atom without higher precedence.
        // parse_atom() is suitable for this.
        // We need to "unconsume" if parse_atom fails or isn't what we want, which is tricky.
        // Alternative: duplicate literal parsing logic here for patterns.
        // Let's try direct construction for patterns for clarity and control.

        std::unique_ptr<Vyn::AST::ExprNode> literal_expr;
        consume(); // Consume the literal token identified by peek()

        if (token.type == Vyn::TokenType::INT_LITERAL) {
            literal_expr = std::make_unique<Vyn::AST::IntegerLiteralNode>(loc, std::stoll(token.value));
        } else if (token.type == Vyn::TokenType::FLOAT_LITERAL) {
            literal_expr = std::make_unique<Vyn::AST::FloatLiteralNode>(loc, std::stod(token.value));
        } else if (token.type == Vyn::TokenType::STRING_LITERAL) {
            literal_expr = std::make_unique<Vyn::AST::StringLiteralNode>(loc, token.value);
        } else if (token.type == Vyn::TokenType::CHAR_LITERAL) {
            literal_expr = std::make_unique<Vyn::AST::CharLiteralNode>(loc, token.value.empty() ? '\\0' : token.value[0]);
        } else if (token.type == Vyn::TokenType::KEYWORD_TRUE) {
            literal_expr = std::make_unique<Vyn::AST::BoolLiteralNode>(loc, true);
        } else if (token.type == Vyn::TokenType::KEYWORD_FALSE) {
            literal_expr = std::make_unique<Vyn::AST::BoolLiteralNode>(loc, false);
        } else if (token.type == Vyn::TokenType::KEYWORD_NULL) {
            literal_expr = std::make_unique<Vyn::AST::NullLiteralNode>(loc);
        }
        
        if (literal_expr) {
            return std::make_unique<Vyn::AST::LiteralPatternNode>(loc, std::move(literal_expr));
        }
    }

    throw std::runtime_error("Unexpected token in pattern at " + loc.toString() + ". Token: " + token.value + " (" + Vyn::token_type_to_string(token.type) + ")");
}
