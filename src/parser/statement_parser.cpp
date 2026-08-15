#include "vyb/parser/parser.hpp" // Should provide full defs for StatementParser and DeclarationParser
#include "vyb/parser/ast.hpp"
#include <stdexcept>
#include "vyb/parser/token.hpp" // Required for vyb::token::Token

namespace vyb {

StatementParser::StatementParser(const std::vector<token::Token>& tokens, size_t& pos, int indent_level, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser, DeclarationParser* decl_parser)
    : BaseParser(tokens, pos, file_path), type_parser_(type_parser), expr_parser_(expr_parser), decl_parser_(decl_parser) {
    // indent_level is part of BaseParser or handled there if needed.
    // If BaseParser doesn't take indent_level, and it's needed here, add a member: this->indent_level_ = indent_level;

    // Let expression parser access statement parser for block parsing
    expr_parser_.set_statement_parser(this);
}

void StatementParser::set_declaration_parser(DeclarationParser* dp) {
    this->decl_parser_ = dp;
}

vyb::ast::StmtPtr StatementParser::parse() {
    // Skip any INDENT/DEDENT tokens before parsing a statement
    this->skip_indents_dedents();
    while (!this->IsAtEnd() && this->peek().type == vyb::TokenType::NEWLINE) {
        this->consume(); // Skip empty lines
    }

    if (this->IsAtEnd()) {
        return nullptr;
    }

    // Handle 'throw' statements as expression statements: parse throw expression and ignore its result
    if (this->peek().type == vyb::TokenType::IDENTIFIER && this->peek().lexeme == "throw") {
        this->consume(); // consume 'throw'
        // Parse the thrown expression (e.g., NetworkError(...))
        auto thrownExpr = this->expr_parser_.parse_expression();
        // Consume optional semicolon
        this->match(vyb::TokenType::SEMICOLON);
        return nullptr; // ignore throw statement in AST
    }
    vyb::token::Token current_token = this->peek();
    switch (current_token.type) {
        case vyb::TokenType::KEYWORD_MUT:
        case vyb::TokenType::KEYWORD_CONST:
        case vyb::TokenType::KEYWORD_VAR:
        case vyb::TokenType::KEYWORD_AUTO:
            return parse_var_decl();
        case vyb::TokenType::KEYWORD_ASYNC:
            if (decl_parser_) {
                return decl_parser_->parse_function();
            } else {
                throw std::runtime_error("Async function parsing not available in this context at " + location_to_string(current_token.location));
            }
        case vyb::TokenType::KEYWORD_EXTERN:
            if (decl_parser_) {
                if (this->peekNext().type == vyb::TokenType::STRING_LITERAL) {
                    return decl_parser_->parse_extern_block();
                }
                return decl_parser_->parse_function();
            } else {
                throw std::runtime_error("Extern declaration parsing not available in this context at " + location_to_string(current_token.location));
            }
        case vyb::TokenType::KEYWORD_CLASS:
            if (decl_parser_) {
                return decl_parser_->parse_class_declaration();
            } else {
                throw std::runtime_error("Class declaration parsing not available in this context at " + location_to_string(current_token.location));
            }
        case vyb::TokenType::KEYWORD_TEMPLATE:
            if (decl_parser_) {
                return decl_parser_->parse_template_declaration();
            } else {
                throw std::runtime_error("Template declaration parsing not available in this context at " + location_to_string(current_token.location));
            }
        case vyb::TokenType::KEYWORD_IF:
            return parse_if();
        case vyb::TokenType::KEYWORD_ENSURE:
            return parse_ensure();
        case vyb::TokenType::KEYWORD_WHILE:
            return parse_while();
        case vyb::TokenType::KEYWORD_FOR:
            return parse_for();
        case vyb::TokenType::KEYWORD_MATCH:
            return parse_match();
        case vyb::TokenType::KEYWORD_RETURN:
            return parse_return();
        case vyb::TokenType::KEYWORD_PASS:
            return parse_pass();
        case vyb::TokenType::LBRACE: {
            // Parse block, but check if it's followed by trap/ensure
            auto block_stmt = parse_block();

            // Check if followed by trap or ensure - if so, it's a block expression
            if (check(vyb::TokenType::KEYWORD_TRAP) || check(vyb::TokenType::KEYWORD_ENSURE)) {
                // Wrap in BlockExpression and parse trap/ensure clauses
                std::vector<std::unique_ptr<vyb::ast::TrapClause>> trapClauses;
                while (match(vyb::TokenType::KEYWORD_TRAP)) {
                    trapClauses.push_back(expr_parser_.parse_trap_clause());
                }

                std::unique_ptr<vyb::ast::EnsureClause> ensureClause;
                if (match(vyb::TokenType::KEYWORD_ENSURE)) {
                    ensureClause = expr_parser_.parse_ensure_clause();
                }

                auto block_expr = std::make_unique<vyb::ast::BlockExpression>(
                    block_stmt->loc, std::move(block_stmt),
                    std::move(trapClauses), std::move(ensureClause)
                );

                // Return as expression statement
                return std::make_unique<vyb::ast::ExpressionStatement>(
                    block_expr->loc, std::move(block_expr)
                );
            }

            // Otherwise return as plain block statement
            return block_stmt;
        }
        case vyb::TokenType::KEYWORD_TRY:
            return parse_try();
        case vyb::TokenType::KEYWORD_FREEDOM:
            return parse_unsafe();
        case vyb::TokenType::KEYWORD_DEFER:
            return parse_defer();
        case vyb::TokenType::KEYWORD_AWAIT:
            return parse_await();
        case vyb::TokenType::KEYWORD_BREAK:
            return parse_break();
        case vyb::TokenType::KEYWORD_CONTINUE:
            return parse_continue();
        case vyb::TokenType::KEYWORD_FAIL:
            return parse_fail();
        case vyb::TokenType::KEYWORD_PANIC:
            return parse_panic();
        case vyb::TokenType::KEYWORD_EXIT:
            return parse_exit();
        case vyb::TokenType::KEYWORD_RETHROW:
            return parse_rethrow();
        default:
            // Check if this could be a variable declaration with unified syntax (name<Type>)
            if (current_token.type == vyb::TokenType::IDENTIFIER) {
                token::Token next_token = this->peekNext();

                // Check for new unified syntax pattern: name<Type>
                if (next_token.type == vyb::TokenType::LT) {
                    // If name<Type> is immediately followed by '(' it is not a variable
                    // declaration — it is a generic function call with explicit type
                    // arguments (e.g. probe<Int>(0, 0)). Route it through the expression
                    // parser instead of splitting it into a declaration + sequence.
                    if (this->looks_like_generic_call()) {
                        if (this->expr_parser_.is_expression_start(current_token.type)) {
                            return parse_expression_statement();
                        }
                    }
                    return parse_var_decl();
                }

                // Check for type-inferred lambda declaration: name = |params| -> body
                // Per LAMBDAS.md, `add = |x, y| -> x + y` is valid Vyb syntax (type inferred from RHS).
                if (next_token.type == vyb::TokenType::EQ) {
                    size_t saved_pos = this->pos_;
                    this->consume(); // consume identifier
                    this->consume(); // consume '='
                    if (this->peek().type == vyb::TokenType::PIPE) {
                        // It IS: name = |params| -> body — parse as VariableDeclaration with inferred type
                        SourceLocation decl_loc = current_token.location;
                        auto identifier_node = std::make_unique<vyb::ast::Identifier>(
                            current_token.location, current_token.lexeme);
                        auto init = this->expr_parser_.parse_expression();
                        if (this->peek().type == vyb::TokenType::SEMICOLON) {
                            this->consume();
                        }
                        return std::make_unique<vyb::ast::VariableDeclaration>(
                            decl_loc,
                            std::move(identifier_node),
                            /*isConst=*/false,
                            /*typeNode=*/nullptr,
                            std::move(init));
                    }
                    // Not a lambda — restore and fall through to expression statement parsing
                    this->pos_ = saved_pos;
                }

                // Check if this could be a legacy relaxed syntax variable declaration (Type name)
                size_t saved_pos = this->pos_;

                try {
                    // Try to parse as a type
                    auto type_node = this->type_parser_.parse();

                    if (type_node && !this->IsAtEnd() && this->peek().type == vyb::TokenType::IDENTIFIER) {
                        // This looks like a relaxed syntax declaration: Type name
                        // Rewind position and parse as variable declaration
                        this->pos_ = saved_pos;
                        return parse_var_decl();
                    }

                    // Not a type or not followed by an identifier, so restore position
                    this->pos_ = saved_pos;
                } catch (...) {
                    // Not a valid type, restore position
                    this->pos_ = saved_pos;
                }
            }

            // Try tuple destructure: x, y = expr
            if (current_token.type == vyb::TokenType::IDENTIFIER) {
                auto tuple_stmt = try_parse_tuple_destructure();
                if (tuple_stmt) {
                    return tuple_stmt;
                }
            }

            // Not a declaration, try parsing as an expression statement
            if (this->expr_parser_.is_expression_start(current_token.type)) {
                return parse_expression_statement();
            } else {
                throw std::runtime_error("Unexpected token at start of statement: '" + token_type_to_string(current_token.type) + "' at " + location_to_string(current_token.location));
            }
    }
}

std::unique_ptr<vyb::ast::ExpressionStatement> StatementParser::parse_expression_statement() {
    auto expr = this->expr_parser_.parse_expression();
    // Assuming expr_parser_.parse_expression() throws or returns a valid expression,
    // and does not return nullptr on failing to parse an expression that should be there.
    SourceLocation expr_loc = expr->loc; // Location of the statement is the location of the expression.

    // Check for optional semicolon or other valid terminators
    if (this->match(vyb::TokenType::SEMICOLON)) {
        // Semicolon consumed, all good.
    } else if (this->IsAtEnd() ||
               this->peek().type == vyb::TokenType::RBRACE ||
               this->peek().type == vyb::TokenType::DEDENT ||
               // Check if next token starts a new statement
               this->is_statement_start(this->peek().type)) {
        // Optional semicolon: acceptable statement terminators
        // Do not consume these tokens, they might be significant for the outer structure.
    } else {
        throw std::runtime_error("Expected semicolon, newline, '}', or DEDENT after expression statement at " +
                                 location_to_string(this->peek().location) + ", got " +
                                 token_type_to_string(this->peek().type));
    }
    return std::make_unique<vyb::ast::ExpressionStatement>(expr_loc, std::move(expr));
}


std::unique_ptr<vyb::ast::BlockStatement> StatementParser::parse_block() {
    SourceLocation start_loc = this->current_location();
    std::vector<vyb::ast::StmtPtr> statements;
    
    // Support both brace-style and indentation-style blocks
    if (this->peek().type == vyb::TokenType::LBRACE) {
        this->consume(); // Consume '{'
        
        while (!this->IsAtEnd() && this->peek().type != vyb::TokenType::RBRACE) {
            while (!this->IsAtEnd() && this->peek().type == vyb::TokenType::NEWLINE) {
                this->consume();
            }
            if (this->IsAtEnd() || this->peek().type == vyb::TokenType::RBRACE) {
                break;
            }
            statements.push_back(parse());
            if (!this->IsAtEnd() && this->peek().type != vyb::TokenType::RBRACE) {
                if (this->peek().type == vyb::TokenType::SEMICOLON) {
                    this->consume();
                } else if (this->peek().type == vyb::TokenType::NEWLINE) {
                    // Fine
                } else if (this->expr_parser_.is_expression_start(this->peek().type) || this->is_statement_start(this->peek().type)) {
                    // Next statement starts immediately
                }
                 else if (this->peek().type != vyb::TokenType::RBRACE && !this->is_statement_start(this->peek().type) && !this->expr_parser_.is_expression_start(this->peek().type)) {
                     throw std::runtime_error("Expected newline, semicolon, or end of block after statement at " + location_to_string(this->peek().location) + ", got " + token_type_to_string(this->peek().type));
                 }
            }
        }
        
        this->expect(vyb::TokenType::RBRACE, "Expected '}' to end a block.");
    } else if (this->peek().type == vyb::TokenType::INDENT) {
        this->consume(); // Consume INDENT
        
        while (!this->IsAtEnd() && this->peek().type != vyb::TokenType::DEDENT && this->peek().type != vyb::TokenType::END_OF_FILE) {
            while (!this->IsAtEnd() && this->peek().type == vyb::TokenType::NEWLINE) {
                this->consume();
            }
            if (this->IsAtEnd() || this->peek().type == vyb::TokenType::DEDENT) {
                break;
            }
            statements.push_back(parse());
            this->pos_ = get_current_pos();
        }
        
        if (this->peek().type == vyb::TokenType::DEDENT) {
            this->consume(); // Consume DEDENT
        }
    } else {
        throw std::runtime_error("Expected '{{' or indentation to start a block at " + location_to_string(this->current_location()));
    }
    
    return std::make_unique<vyb::ast::BlockStatement>(start_loc, std::move(statements));
}


std::unique_ptr<vyb::ast::IfStatement> StatementParser::parse_if() {
    SourceLocation if_loc = this->expect(vyb::TokenType::KEYWORD_IF, "Expected 'if'.").location;
    this->expect(vyb::TokenType::LPAREN, "Expected '(' after 'if'.");
    auto condition = this->expr_parser_.parse_expression();
    this->expect(vyb::TokenType::RPAREN, "Expected ')' after if condition.");
    auto then_branch = parse_block(); // 'if' body must be a block
    vyb::ast::StmtPtr else_branch = nullptr;
    SourceLocation end_loc = then_branch->loc; // Use loc member

    if (this->match(vyb::TokenType::KEYWORD_ELSE)) {
        if (this->peek().type == vyb::TokenType::KEYWORD_IF) { // 'else if'
            else_branch = parse_if(); // Recursively parse the 'else if'
        } else { // 'else'
            else_branch = parse_block(); // 'else' body must be a block
        }
        if (else_branch) {
            end_loc = else_branch->loc; // Use loc member
        }
    }

    return std::make_unique<vyb::ast::IfStatement>(if_loc, std::move(condition), std::move(then_branch), std::move(else_branch));
}

std::unique_ptr<vyb::ast::IfStatement> StatementParser::parse_ensure() {
    SourceLocation loc = expect(vyb::TokenType::KEYWORD_ENSURE, "Expected 'ensure'.").location;

    // Contract condition (e.g. `b != 0`). Expr parser stops at the `else` keyword.
    auto condition = expr_parser_.parse_expression();
    expect(vyb::TokenType::KEYWORD_ELSE, "Expected 'else' after ensure condition.");

    // Failure handling: a block or a single statement (e.g. `return -1`,
    // `fail<DivisionError>(...)`).
    vyb::ast::StmtPtr handling;
    if (check(vyb::TokenType::LBRACE)) {
        handling = parse_block();
    } else {
        handling = parse();
    }

    // Desugar `ensure cond else handling` into `if (cond) { } else { handling }`
    // so the handling runs exactly when the condition is false.
    std::vector<vyb::ast::StmtPtr> emptyBody;
    auto emptyThen = std::make_unique<vyb::ast::BlockStatement>(loc, std::move(emptyBody));
    return std::make_unique<vyb::ast::IfStatement>(
        loc, std::move(condition), std::move(emptyThen), std::move(handling));
}

std::unique_ptr<vyb::ast::WhileStatement> StatementParser::parse_while() {
    SourceLocation while_loc = this->expect(vyb::TokenType::KEYWORD_WHILE, "Expected 'while'.").location;
    this->expect(vyb::TokenType::LPAREN, "Expected '(' after 'while'.");
    auto condition = this->expr_parser_.parse_expression();
    this->expect(vyb::TokenType::RPAREN, "Expected ')' after while condition.");
    auto body = parse_block(); // 'while' body must be a block
    SourceLocation end_loc = body->loc; // Use loc member
    return std::make_unique<vyb::ast::WhileStatement>(while_loc, std::move(condition), std::move(body));
}

std::unique_ptr<vyb::ast::ForStatement> StatementParser::parse_for() {
    SourceLocation for_loc = this->expect(vyb::TokenType::KEYWORD_FOR, "Expected 'for'.").location;

    // Expect opening parenthesis
    this->expect(vyb::TokenType::LPAREN, "Expected '(' after 'for'");

    // Check for range-based for loop: `for (identifier in expression) { body }`
    if (this->peek().type == vyb::TokenType::IDENTIFIER) {
        size_t saved_pos = this->pos_;
        token::Token ident_token = this->consume();

        if (this->peek().type == vyb::TokenType::KEYWORD_IN) {
            // This is a range-based for loop
            this->consume(); // Consume 'in'

            // Parse the range expression
            vyb::ast::ExprPtr range_expr = this->expr_parser_.parse_expression();

            // Check for optional step/skip parameter: for i in 0..10, 2 or for x in vec, 2
            vyb::ast::ExprPtr skip_expr = nullptr;
            if (this->peek().type == vyb::TokenType::COMMA) {
                this->consume(); // Consume comma
                skip_expr = this->expr_parser_.parse_expression();
            }

            // Expect closing parenthesis
            this->expect(vyb::TokenType::RPAREN, "Expected ')' after for loop header");

            // Expect the body block
            auto body = parse_block();

            // Check what kind of expression we're iterating over
            if (range_expr->getType() == vyb::ast::NodeType::RANGE_EXPRESSION) {
                // Desugar range-based for loop to C-style for loop
                // for i in start..end { body }
                // becomes:
                // { var i = start; while i <= end { body; i = i + step; } }
                // Note: Ranges are now INCLUSIVE
                auto* range = static_cast<vyb::ast::RangeExpression*>(range_expr.get());

                // For range expressions, add the step to the range if provided
                if (skip_expr) {
                    range->step = std::move(skip_expr);
                }
                // Create the loop variable declaration: var i = start;
                auto loop_var_name = std::make_unique<vyb::ast::Identifier>(ident_token.location, ident_token.lexeme);
                auto type_name_id = std::make_unique<vyb::ast::Identifier>(ident_token.location, "Int");
                auto loop_var_type = std::make_unique<vyb::ast::TypeName>(ident_token.location, std::move(type_name_id));
                auto init_value = std::move(range->start);
                auto loop_var_decl = std::make_unique<vyb::ast::VariableDeclaration>(
                    ident_token.location,
                    std::move(loop_var_name),
                    false,  // isConst (false for var)
                    std::move(loop_var_type),
                    std::move(init_value)
                );

                // Create the condition: i <= end (INCLUSIVE)
                auto cond_left = std::make_unique<vyb::ast::Identifier>(ident_token.location, ident_token.lexeme);
                token::Token cond_op_token(vyb::TokenType::LTEQ, "<=", ident_token.location);
                auto condition = std::make_unique<vyb::ast::BinaryExpression>(
                    ident_token.location,
                    std::move(cond_left),
                    cond_op_token,
                    std::move(range->end)
                );

                // Create the increment: i = i + step (default step is 1)
                auto incr_left = std::make_unique<vyb::ast::Identifier>(ident_token.location, ident_token.lexeme);
                auto incr_right_left = std::make_unique<vyb::ast::Identifier>(ident_token.location, ident_token.lexeme);

                // Use provided step or default to 1
                vyb::ast::ExprPtr step_value;
                if (range->step) {
                    step_value = std::move(range->step);
                } else {
                    step_value = std::make_unique<vyb::ast::IntegerLiteral>(ident_token.location, 1);
                }

                token::Token plus_token(vyb::TokenType::PLUS, "+", ident_token.location);
                auto incr_right = std::make_unique<vyb::ast::BinaryExpression>(
                    ident_token.location,
                    std::move(incr_right_left),
                    plus_token,
                    std::move(step_value)
                );
                token::Token assign_token(vyb::TokenType::EQ, "=", ident_token.location);
                auto increment = std::make_unique<vyb::ast::AssignmentExpression>(
                    ident_token.location,
                    std::move(incr_left),
                    assign_token,
                    std::move(incr_right)
                );

                return std::make_unique<vyb::ast::ForStatement>(
                    for_loc,
                    std::move(loop_var_decl),
                    std::move(condition),
                    std::move(increment),
                    std::move(body)
                );
            } else {
                // Not a RangeExpression - assume it's a Vec<T> or other iterable
                // Desugar: for (item in vec) { body }
                // Optional skip parameter already parsed above: skip_expr

                // Check if range_expr is a simple identifier - if so, use it directly
                // Otherwise we'd need to store in temp (not implemented yet for complex expressions)
                auto* vec_ident = dynamic_cast<vyb::ast::Identifier*>(range_expr.get());
                if (!vec_ident) {
                    // Non-identifier iterable expression: desugar over the Iterator
                    // protocol. E.g. `for (x in v.iter()) { body }`.
                    return buildForLoopIteratorDesugar(
                        for_loc, ident_token, std::move(range_expr), std::move(body), std::move(skip_expr));
                }

                // A plain-identifier iterable is routed onto the Iterator protocol for
                // uniformity, exactly as `for (x in vec.iter())` would be. The old
                // index-based Vec desugar (`__idx`/`__len` over `vec.get(i)`) is gone,
                // so both Vec collections and stored iterator values iterate through
                // `core::iter::Iterator` via a single `.iter()` -> `.next()` path.
                auto vec_iter_member = std::make_unique<vyb::ast::MemberExpression>(
                    for_loc, std::move(range_expr), std::make_unique<vyb::ast::Identifier>(for_loc, "iter"), /*isArrow*/ false);
                auto vec_iter_call = std::make_unique<vyb::ast::CallExpression>(
                    for_loc, std::move(vec_iter_member), std::vector<vyb::ast::ExprPtr>());
                return buildForLoopIteratorDesugar(
                    for_loc, ident_token, std::move(vec_iter_call), std::move(body), std::move(skip_expr));
            }

        } else {
            // Not a range-based for loop, restore position
            this->pos_ = saved_pos;
        }
    }

    // C-style for loop: for (init; cond; update) { body }
    // Note: LPAREN was already consumed above

    vyb::ast::StmtPtr initializer = nullptr;

    // Check for variable declarations with all possible starting tokens
    if (this->peek().type == vyb::TokenType::KEYWORD_LET ||
        this->peek().type == vyb::TokenType::KEYWORD_MUT ||
        this->peek().type == vyb::TokenType::KEYWORD_CONST ||
        this->peek().type == vyb::TokenType::KEYWORD_VAR ||
        this->peek().type == vyb::TokenType::KEYWORD_AUTO) {
        // Standard syntax variable declaration
        initializer = parse_var_decl(); // Parses the full variable declaration including semicolon
    } else if (this->peek().type != vyb::TokenType::SEMICOLON) {
        // Check for relaxed syntax (Type name) variable declarations
        if (this->peek().type == vyb::TokenType::IDENTIFIER) {
            // Save position in case we need to backtrack
            size_t saved_pos = this->pos_;

            try {
                // Try to parse as a type
                auto type_node = this->type_parser_.parse();

                if (type_node && !this->IsAtEnd() && this->peek().type == vyb::TokenType::IDENTIFIER) {
                    // This looks like a relaxed syntax declaration: Type name
                    // Rewind position and parse as variable declaration
                    this->pos_ = saved_pos;
                    initializer = parse_var_decl();
                } else {
                    // Not a type or not followed by an identifier, restore position
                    this->pos_ = saved_pos;
                    initializer = parse_expression_statement(); // Parse as expression statement
                }
            } catch (...) {
                // Not a valid type, restore position and parse as expression
                this->pos_ = saved_pos;
                initializer = parse_expression_statement(); // Parse as expression statement
            }
        } else {
            // Not starting with an identifier, parse as expression
            initializer = parse_expression_statement(); // Parses expression then expects semicolon
        }
    } else {
        this->expect(vyb::TokenType::SEMICOLON, "Expected semicolon after empty for-loop initializer."); // Consume semicolon for empty initializer
    }

    vyb::ast::ExprPtr condition = nullptr;
    if (this->peek().type != vyb::TokenType::SEMICOLON) {
        condition = this->expr_parser_.parse_expression();
    }
    this->expect(vyb::TokenType::SEMICOLON, "Expected semicolon after for-loop condition.");

    vyb::ast::ExprPtr increment = nullptr;
    if (this->peek().type != vyb::TokenType::RPAREN) {
        increment = this->expr_parser_.parse_expression();
    }
    this->expect(vyb::TokenType::RPAREN, "Expected ')' after for-loop clauses.");

    auto body = parse_block(); // 'for' body must be a block
    SourceLocation end_loc = body->loc; // Use loc member

    return std::make_unique<vyb::ast::ForStatement>(for_loc, std::move(initializer), std::move(condition), std::move(increment), std::move(body));
}



std::unique_ptr<vyb::ast::ForStatement> StatementParser::buildForLoopIteratorDesugar(
    const SourceLocation& loc, const token::Token& ident, vyb::ast::ExprPtr range_expr,
    std::unique_ptr<vyb::ast::BlockStatement> body, vyb::ast::ExprPtr skip_expr) {

    std::string it_name = "__it_" + ident.lexeme;

    // var __it_<item> = <iterable>;
    auto it_var = std::make_unique<vyb::ast::Identifier>(loc, it_name);
    auto it_decl = std::make_unique<vyb::ast::VariableDeclaration>(
        loc, std::move(it_var), false, nullptr, std::move(range_expr));

    // Build `<it>.next()` as a call expression.
    auto make_next = [&loc, &it_name]() -> vyb::ast::ExprPtr {
        auto obj = std::make_unique<vyb::ast::Identifier>(loc, it_name);
        auto method = std::make_unique<vyb::ast::Identifier>(loc, "next");
        auto member = std::make_unique<vyb::ast::MemberExpression>(
            loc, std::move(obj), std::move(method), false);
        return std::make_unique<vyb::ast::CallExpression>(
            loc, std::move(member), std::vector<vyb::ast::ExprPtr>());
    };

    // None -> { break }
    auto none_pattern = std::make_unique<vyb::ast::Identifier>(loc, "None");
    std::vector<vyb::ast::StmtPtr> break_block_stmts;
    break_block_stmts.push_back(std::make_unique<vyb::ast::BreakStatement>(loc));
    auto break_block = std::make_unique<vyb::ast::BlockStatement>(loc, std::move(break_block_stmts));
    auto none_body = std::make_unique<vyb::ast::BlockExpression>(loc, std::move(break_block));

    // The Some-arm pattern binding: without a step it is the user's `item`;
    // with a step the Some-arm first advances `step`-1 more elements (so the
    // loop yields indices 0, step, 2*step, ... exactly like the Vec index path)
    // and the `item` it yields is the seed of each group.
    vyb::ast::ExprPtr some_binding = std::make_unique<vyb::ast::Identifier>(loc, ident.lexeme);
    std::vector<vyb::ast::StmtPtr> outer_pre; // statements placed before the main while

    if (skip_expr) {
        std::string step_name = "__step_" + ident.lexeme;
        std::string s_name = "__s_" + ident.lexeme;
        std::string v_name = "__v_" + ident.lexeme;

        // var __step_<item> = <skip>;
        auto step_var = std::make_unique<vyb::ast::Identifier>(loc, step_name);
        auto step_decl = std::make_unique<vyb::ast::VariableDeclaration>(
            loc, std::move(step_var), false, nullptr, std::move(skip_expr));
        outer_pre.push_back(std::move(step_decl));

        // var __s_<item> = 1;
        auto s_var = std::make_unique<vyb::ast::Identifier>(loc, s_name);
        auto s_one = std::make_unique<vyb::ast::IntegerLiteral>(loc, 1);
        auto s_decl = std::make_unique<vyb::ast::VariableDeclaration>(
            loc, std::move(s_var), false, nullptr, std::move(s_one));

        // while (__s_<item> < __step_<item>) { ... }
        auto lt_s = std::make_unique<vyb::ast::Identifier>(loc, s_name);
        auto lt_step = std::make_unique<vyb::ast::Identifier>(loc, step_name);
        token::Token lt_op(vyb::TokenType::LT, "<", loc);
        auto cond = std::make_unique<vyb::ast::BinaryExpression>(loc, std::move(lt_s), lt_op, std::move(lt_step));

        // inner match: Some(__v_<item>) -> { __s_ += 1 } | None -> { __s_ = __step_ }
        auto v_binding = std::make_unique<vyb::ast::Identifier>(loc, v_name);
        auto v_some_type_id = std::make_unique<vyb::ast::Identifier>(loc, "Some");
        auto v_some_name = std::make_unique<vyb::ast::TypeName>(loc, std::move(v_some_type_id));
        std::vector<vyb::ast::ExprPtr> v_some_args;
        v_some_args.push_back(std::move(v_binding));
        auto v_some_pattern = std::make_unique<vyb::ast::ConstructionExpression>(
            loc, std::move(v_some_name), std::move(v_some_args));
        auto s_inc_l = std::make_unique<vyb::ast::Identifier>(loc, s_name);
        auto s_inc_rl = std::make_unique<vyb::ast::Identifier>(loc, s_name);
        auto s_one2 = std::make_unique<vyb::ast::IntegerLiteral>(loc, 1);
        token::Token plus_op(vyb::TokenType::PLUS, "+", loc);
        auto s_rhs = std::make_unique<vyb::ast::BinaryExpression>(loc, std::move(s_inc_rl), plus_op, std::move(s_one2));
        token::Token eq1(vyb::TokenType::EQ, "=", loc);
        auto s_inc = std::make_unique<vyb::ast::AssignmentExpression>(loc, std::move(s_inc_l), eq1, std::move(s_rhs));
        std::vector<vyb::ast::StmtPtr> v_arm_stmts;
        v_arm_stmts.push_back(std::make_unique<vyb::ast::ExpressionStatement>(loc, std::move(s_inc)));
        auto v_arm_block = std::make_unique<vyb::ast::BlockStatement>(loc, std::move(v_arm_stmts));
        auto v_arm_body = std::make_unique<vyb::ast::BlockExpression>(loc, std::move(v_arm_block));

        auto s_reset_l = std::make_unique<vyb::ast::Identifier>(loc, s_name);
        auto s_reset_r = std::make_unique<vyb::ast::Identifier>(loc, step_name);
        token::Token eq2(vyb::TokenType::EQ, "=", loc);
        auto s_reset = std::make_unique<vyb::ast::AssignmentExpression>(loc, std::move(s_reset_l), eq2, std::move(s_reset_r));
        std::vector<vyb::ast::StmtPtr> inner_none_stmts;
        inner_none_stmts.push_back(std::make_unique<vyb::ast::ExpressionStatement>(loc, std::move(s_reset)));
        auto inner_none_block = std::make_unique<vyb::ast::BlockStatement>(loc, std::move(inner_none_stmts));
        auto inner_none_body = std::make_unique<vyb::ast::BlockExpression>(loc, std::move(inner_none_block));
        auto inner_none_pattern = std::make_unique<vyb::ast::Identifier>(loc, "None");
        std::vector<std::pair<vyb::ast::ExprPtr, vyb::ast::ExprPtr>> inner_cases;
        inner_cases.emplace_back(std::move(v_some_pattern), std::move(v_arm_body));
        inner_cases.emplace_back(std::move(inner_none_pattern), std::move(inner_none_body));
        auto inner_match = std::make_unique<vyb::ast::MatchStatement>(loc, make_next(), std::move(inner_cases));
        std::vector<vyb::ast::StmtPtr> inner_while_stmts;
        inner_while_stmts.push_back(std::move(inner_match));
        auto inner_while_block = std::make_unique<vyb::ast::BlockStatement>(loc, std::move(inner_while_stmts));
        auto inner_while = std::make_unique<vyb::ast::WhileStatement>(loc, std::move(cond), std::move(inner_while_block));

        // Prepend `{ var __s_ = 1; while (...) {...}; ` to the body so the user's
        // `item` (bound by the Some pattern) is the seed of each group.
        if (body) {
            body->body.insert(body->body.begin(), std::move(inner_while));
            body->body.insert(body->body.begin(), std::move(s_decl));
        }
    }

    // Some(<item>) -> { body }   (body may absorb the skip prologue above)
    auto some_type_id = std::make_unique<vyb::ast::Identifier>(loc, "Some");
    auto some_name = std::make_unique<vyb::ast::TypeName>(loc, std::move(some_type_id));
    std::vector<vyb::ast::ExprPtr> some_args;
    some_args.push_back(std::move(some_binding));
    auto outer_some_pattern = std::make_unique<vyb::ast::ConstructionExpression>(
        loc, std::move(some_name), std::move(some_args));
    auto some_body_expr = std::make_unique<vyb::ast::BlockExpression>(loc, std::move(body));

    std::vector<std::pair<vyb::ast::ExprPtr, vyb::ast::ExprPtr>> cases;
    cases.emplace_back(std::move(outer_some_pattern), std::move(some_body_expr));
    cases.emplace_back(std::move(none_pattern), std::move(none_body));
    auto iter_match = std::make_unique<vyb::ast::MatchStatement>(loc, make_next(), std::move(cases));

    // while (true) { iter_match }
    auto true_lit = std::make_unique<vyb::ast::BooleanLiteral>(loc, true);
    std::vector<vyb::ast::StmtPtr> while_stmts;
    while_stmts.push_back(std::move(iter_match));
    auto while_block = std::make_unique<vyb::ast::BlockStatement>(loc, std::move(while_stmts));
    auto while_stmt = std::make_unique<vyb::ast::WhileStatement>(loc, std::move(true_lit), std::move(while_block));

    // { [step_decl]; it_decl; while_stmt; }
    std::vector<vyb::ast::StmtPtr> outer_stmts;
    for (auto& st : outer_pre) outer_stmts.push_back(std::move(st));
    outer_stmts.push_back(std::move(it_decl));
    outer_stmts.push_back(std::move(while_stmt));
    auto final_block = std::make_unique<vyb::ast::BlockStatement>(loc, std::move(outer_stmts));

    // Wrap in a __run_once for-loop to satisfy the ForStatement return type.
    std::string run_name = "__run_once_" + ident.lexeme;
    auto run_var = std::make_unique<vyb::ast::Identifier>(loc, run_name);
    auto run_init = std::make_unique<vyb::ast::BooleanLiteral>(loc, true);
    auto run_decl = std::make_unique<vyb::ast::VariableDeclaration>(loc, std::move(run_var), false, nullptr, std::move(run_init));
    auto run_cond = std::make_unique<vyb::ast::Identifier>(loc, run_name);
    auto run_upd_l = std::make_unique<vyb::ast::Identifier>(loc, run_name);
    auto run_upd_r = std::make_unique<vyb::ast::BooleanLiteral>(loc, false);
    token::Token eq_token(vyb::TokenType::EQ, "=", loc);
    auto run_update = std::make_unique<vyb::ast::AssignmentExpression>(loc, std::move(run_upd_l), eq_token, std::move(run_upd_r));
    return std::make_unique<vyb::ast::ForStatement>(loc, std::move(run_decl), std::move(run_cond), std::move(run_update), std::move(final_block));
}

std::unique_ptr<vyb::ast::ReturnStatement> StatementParser::parse_return() {
    SourceLocation return_loc = this->expect(vyb::TokenType::KEYWORD_RETURN, "Expected 'return'.").location;
    vyb::ast::ExprPtr value = nullptr;
    SourceLocation end_loc = return_loc;

    if (this->peek().type != vyb::TokenType::SEMICOLON && this->peek().type != vyb::TokenType::NEWLINE && this->peek().type != vyb::TokenType::RBRACE && this->peek().type != vyb::TokenType::DEDENT) {
        // Parse the first expression
        value = this->expr_parser_.parse_expression();
        end_loc = value->loc; // Use loc member

        // Check if there are multiple comma-separated expressions
        if (this->peek().type == vyb::TokenType::COMMA) {
            std::vector<vyb::ast::ExprPtr> expressions;
            expressions.push_back(std::move(value)); // Add the first expression

            // Parse remaining expressions
            while (this->peek().type == vyb::TokenType::COMMA) {
                this->consume(); // Consume comma
                expressions.push_back(this->expr_parser_.parse_expression());
                end_loc = expressions.back()->loc;
            }

            // Create a SequenceExpression to hold all the expressions
            value = std::make_unique<vyb::ast::SequenceExpression>(return_loc, std::move(expressions));
        }
    }

    if (this->peek().type == vyb::TokenType::SEMICOLON) {
        end_loc = this->peek().location;
        this->consume(); // Consume semicolon
    } else if (this->peek().type == vyb::TokenType::NEWLINE || this->IsAtEnd() || this->peek().type == vyb::TokenType::RBRACE || this->peek().type == vyb::TokenType::DEDENT || this->is_statement_start(this->peek().type)) {
        // Optional semicolon at the end of a line, before a closing brace, or
        // directly before another new statement (the value expression may have
        // consumed the trailing newline).
    } else {
        throw std::runtime_error("Expected semicolon or newline after return statement at " + location_to_string(this->peek().location));
    }

    return std::make_unique<vyb::ast::ReturnStatement>(return_loc, std::move(value));
}

std::unique_ptr<vyb::ast::PassStatement> StatementParser::parse_pass() {
    SourceLocation pass_loc = this->expect(vyb::TokenType::KEYWORD_PASS, "Expected 'pass'.").location;
    vyb::ast::ExprPtr value = nullptr;
    SourceLocation end_loc = pass_loc;

    // Pass requires an argument - it must pass a value
    if (this->peek().type == vyb::TokenType::SEMICOLON || this->peek().type == vyb::TokenType::NEWLINE ||
        this->peek().type == vyb::TokenType::RBRACE || this->peek().type == vyb::TokenType::DEDENT) {
        throw std::runtime_error("Expected expression after 'pass' keyword at " + location_to_string(pass_loc));
    }

    // Parse the expression to pass
    value = this->expr_parser_.parse_expression();
    if (!value) {
        throw std::runtime_error("Expected expression after 'pass' at " + location_to_string(this->peek().location));
    }
    end_loc = value->loc;

    if (this->peek().type == vyb::TokenType::SEMICOLON) {
        end_loc = this->peek().location;
        this->consume(); // Consume semicolon
    } else if (this->peek().type == vyb::TokenType::NEWLINE || this->IsAtEnd() ||
               this->peek().type == vyb::TokenType::RBRACE || this->peek().type == vyb::TokenType::DEDENT) {
        // Optional semicolon at the end of a line or before closing brace
    } else {
        throw std::runtime_error("Expected semicolon or newline after pass statement at " + location_to_string(this->peek().location));
    }

    return std::make_unique<vyb::ast::PassStatement>(pass_loc, std::move(value));
}

vyb::ast::StmtPtr StatementParser::parse_var_decl() {
    // Declaration start location
    SourceLocation decl_loc = this->current_location();

    // Check what kind of declaration this is
    bool is_const_decl = false;
    bool auto_type_inference = false;

    // Check for 'auto' keyword first (type inference)
    if (this->match(vyb::TokenType::KEYWORD_AUTO)) {
        auto_type_inference = true;
        is_const_decl = false;

        // Parse variable name for auto
        vyb::token::Token name_token = this->expect(vyb::TokenType::IDENTIFIER, "Expected variable name after 'auto'.");
        auto identifier_node = std::make_unique<vyb::ast::Identifier>(name_token.location, name_token.lexeme);

        // Auto requires initializer
        this->expect(vyb::TokenType::EQ, "Auto variables require an initializer.");
        vyb::ast::ExprPtr initializer = this->expr_parser_.parse_expression();
        if (!initializer) {
            throw std::runtime_error("Expected initializer expression after '=' for auto variable at " +
                                   location_to_string(this->current_location()));
        }

        SourceLocation end_loc = initializer->loc;

        if (this->peek().type == vyb::TokenType::SEMICOLON) {
            end_loc = this->peek().location;
            this->consume();
        } else if (this->peek().type == vyb::TokenType::NEWLINE || this->IsAtEnd() ||
                   this->peek().type == vyb::TokenType::RBRACE || this->peek().type == vyb::TokenType::DEDENT ||
                   this->peek().type == vyb::TokenType::END_OF_FILE ||
                   is_statement_start(this->peek().type)) {
            // Optional semicolon
        } else {
            throw std::runtime_error("Expected statement separator after variable declaration at " +
                                   location_to_string(this->peek().location));
        }

        return std::make_unique<vyb::ast::VariableDeclaration>(
            decl_loc,
            std::move(identifier_node),
            is_const_decl,
            nullptr, // Type will be inferred
            std::shared_ptr<vyb::ast::Expression>(std::move(initializer))
        );
    }
    // Legacy support: Check for var keyword
    else if (this->match(vyb::TokenType::KEYWORD_VAR)) {
        is_const_decl = false;
        // Legacy var<Type> syntax - parse type in angle brackets
        this->expect(vyb::TokenType::LT, "Expected '<' after 'var'.");

        // Parse comma-separated types for inline tuple syntax
        std::vector<ast::TypeNodePtr> types;
        do {
            ast::TypeNodePtr type_expr = this->type_parser_.parse();
            if (!type_expr) {
                throw std::runtime_error("Expected type inside '<>' in variable declaration at " +
                                       location_to_string(this->peek().location));
            }
            types.push_back(std::move(type_expr));
        } while (this->match(vyb::TokenType::COMMA));

        this->expect(vyb::TokenType::GT, "Expected '>' after type in variable declaration.");

        // If multiple types, create TupleTypeNode; otherwise use single type
        ast::TypeNodePtr type_expr;
        if (types.size() == 1) {
            type_expr = std::move(types[0]);
        } else {
            type_expr = std::make_unique<ast::TupleTypeNode>(decl_loc, std::move(types));
        }

        // Parse variable name
        vyb::token::Token name_token = this->expect(vyb::TokenType::IDENTIFIER, "Expected variable name.");
        auto identifier_node = std::make_unique<vyb::ast::Identifier>(name_token.location, name_token.lexeme);

        vyb::ast::ExprPtr initializer = nullptr;
        SourceLocation end_loc = name_token.location;

        if (this->match(vyb::TokenType::EQ)) {
            initializer = this->expr_parser_.parse_expression();
            if (initializer) {
                end_loc = initializer->loc;
            }
        }

        if (this->peek().type == vyb::TokenType::SEMICOLON) {
            end_loc = this->peek().location;
            this->consume();
        } else if (this->peek().type == vyb::TokenType::NEWLINE || this->IsAtEnd() ||
                   this->peek().type == vyb::TokenType::RBRACE || this->peek().type == vyb::TokenType::DEDENT ||
                   this->peek().type == vyb::TokenType::END_OF_FILE ||
                   is_statement_start(this->peek().type)) {
            // Optional semicolon
        } else {
            throw std::runtime_error("Expected statement separator after variable declaration at " +
                                   location_to_string(this->peek().location));
        }

        return std::make_unique<vyb::ast::VariableDeclaration>(
            decl_loc,
            std::move(identifier_node),
            is_const_decl,
            std::move(type_expr),
            std::shared_ptr<vyb::ast::Expression>(std::move(initializer))
        );
    } else if (this->match(vyb::TokenType::KEYWORD_CONST)) {
        is_const_decl = true;

        // Support both const<Type> syntax and relaxed "const Type name" syntax
        if (this->peek().type == vyb::TokenType::LT) {
            // Vyb const<Type> syntax - parse type in angle brackets
            this->consume(); // consume '<'

            // Parse comma-separated types for inline tuple syntax
            std::vector<ast::TypeNodePtr> types;
            do {
                ast::TypeNodePtr type_expr = this->type_parser_.parse();
                if (!type_expr) {
                    throw std::runtime_error("Expected type inside '<>' in const variable declaration at " +
                                           location_to_string(this->peek().location));
                }
                types.push_back(std::move(type_expr));
            } while (this->match(vyb::TokenType::COMMA));

            this->expect(vyb::TokenType::GT, "Expected '>' after type in const variable declaration.");

            // If multiple types, create TupleTypeNode; otherwise use single type
            ast::TypeNodePtr type_expr;
            if (types.size() == 1) {
                type_expr = std::move(types[0]);
            } else {
                type_expr = std::make_unique<ast::TupleTypeNode>(decl_loc, std::move(types));
            }

            // Parse variable name
            vyb::token::Token name_token = this->expect(vyb::TokenType::IDENTIFIER, "Expected variable name.");
            auto identifier_node = std::make_unique<vyb::ast::Identifier>(name_token.location, name_token.lexeme);

            vyb::ast::ExprPtr initializer = nullptr;
            if (this->match(vyb::TokenType::EQ)) {
                initializer = this->expr_parser_.parse_expression();
            }
            if (this->peek().type == vyb::TokenType::SEMICOLON) this->consume();

            return std::make_unique<vyb::ast::VariableDeclaration>(
                decl_loc, std::move(identifier_node), is_const_decl,
                std::move(type_expr), std::shared_ptr<vyb::ast::Expression>(std::move(initializer)));
        } else if (this->peek().type == vyb::TokenType::IDENTIFIER) {
            // Relaxed syntax: const TypeName varName = value
            ast::TypeNodePtr type_expr = this->type_parser_.parse();
            vyb::token::Token name_token = this->expect(vyb::TokenType::IDENTIFIER, "Expected variable name.");
            auto identifier_node = std::make_unique<vyb::ast::Identifier>(name_token.location, name_token.lexeme);
            vyb::ast::ExprPtr initializer = nullptr;
            if (this->match(vyb::TokenType::EQ)) {
                initializer = this->expr_parser_.parse_expression();
            }
            if (this->peek().type == vyb::TokenType::SEMICOLON) this->consume();
            return std::make_unique<vyb::ast::VariableDeclaration>(
                decl_loc, std::move(identifier_node), is_const_decl,
                std::move(type_expr), std::shared_ptr<vyb::ast::Expression>(std::move(initializer)));
        } else {
            throw std::runtime_error("Expected '<' or type name after 'const', but found '" +
                token_type_to_string(this->peek().type) + "' at " + location_to_string(this->peek().location));
        }
    }

    // NEW UNIFIED SYNTAX: name<Type> pattern (supports multi-var: a<T>, b<U> = expr)
    // Collect all variable names and their types first
    struct VarDeclInfo {
        SourceLocation loc;
        std::unique_ptr<vyb::ast::Identifier> identifier;
        ast::TypeNodePtr type_expr;
    };

    std::vector<VarDeclInfo> var_decls;

    while (true) {
        // Parse the variable name
        vyb::token::Token name_token = this->expect(vyb::TokenType::IDENTIFIER, "Expected variable name.");
        auto identifier_node = std::make_unique<vyb::ast::Identifier>(name_token.location, name_token.lexeme);

        // Parse the type in angle brackets: name<Type>
        this->expect(vyb::TokenType::LT, "Expected '<' after variable name in unified syntax.");

        // Parse comma-separated types for inline tuple syntax
        std::vector<ast::TypeNodePtr> types;
        do {
            ast::TypeNodePtr type = this->type_parser_.parse();
            if (!type) {
                throw std::runtime_error("Expected type inside '<>' in variable declaration at " +
                                       location_to_string(this->peek().location));
            }
            types.push_back(std::move(type));
        } while (this->match(vyb::TokenType::COMMA));

        // Check for const modifier: name<Type const>
        if ((this->peek().type == vyb::TokenType::IDENTIFIER && this->peek().lexeme == "const") ||
            this->peek().type == vyb::TokenType::KEYWORD_CONST) {
            this->consume(); // consume "const"
            is_const_decl = true;
        }

        this->expect(vyb::TokenType::GT, "Expected '>' after type in variable declaration.");

        // If multiple types, create TupleTypeNode; otherwise use single type
        ast::TypeNodePtr type_expr;
        if (types.size() == 1) {
            type_expr = std::move(types[0]);
        } else {
            type_expr = std::make_unique<ast::TupleTypeNode>(decl_loc, std::move(types));
        }

        var_decls.push_back({decl_loc, std::move(identifier_node), std::move(type_expr)});

        // Check if followed by comma (more variables) or not
        if (!this->match(vyb::TokenType::COMMA)) {
            break; // No more variables
        }
        // Otherwise continue to parse next variable
    }

    // Parse shared initializer (if present)
    vyb::ast::ExprPtr initializer = nullptr;
    SourceLocation end_loc = decl_loc;

    if (this->match(vyb::TokenType::EQ)) {
        initializer = this->expr_parser_.parse_expression();
        if (initializer) {
            end_loc = initializer->loc;
        }
    } else if (is_const_decl && var_decls.size() == 1 && !initializer) {
        // Single const without initializer - could be forward declaration
    }

    // Consume statement separator
    if (this->peek().type == vyb::TokenType::SEMICOLON) {
        end_loc = this->peek().location;
        this->consume();
    } else if (this->peek().type == vyb::TokenType::NEWLINE || this->IsAtEnd() ||
               this->peek().type == vyb::TokenType::RBRACE || this->peek().type == vyb::TokenType::DEDENT ||
               this->peek().type == vyb::TokenType::END_OF_FILE ||
               is_statement_start(this->peek().type)) {
        // Optional semicolon - also accept statement starts and end of file
    } else {
        throw std::runtime_error("Expected statement separator after variable declaration at " +
                               location_to_string(this->peek().location));
    }

    // Create VariableDeclaration nodes for each variable (all share the same initializer via shared_ptr)
    if (var_decls.size() == 1) {
        auto& info = var_decls[0];
        return std::make_unique<vyb::ast::VariableDeclaration>(
            info.loc,
            std::move(info.identifier),
            is_const_decl,
            std::move(info.type_expr),
            std::shared_ptr<vyb::ast::Expression>(std::move(initializer)) // moves unique_ptr into constructor which converts to shared_ptr
        );
    }

    // Multi-var: wrap in BlockStatement
    // Convert initializer to shared_ptr so all vars can share it
    std::shared_ptr<vyb::ast::Expression> shared_init = initializer ? std::shared_ptr<vyb::ast::Expression>(std::move(initializer)) : nullptr;
    std::vector<vyb::ast::StmtPtr> body;
    for (auto& info : var_decls) {
        body.push_back(std::make_unique<vyb::ast::VariableDeclaration>(
            info.loc,
            std::move(info.identifier),
            is_const_decl,
            std::move(info.type_expr),
            shared_init // shared_ptr - all vars share the same expression
        ));
    }
    return std::make_unique<vyb::ast::BlockStatement>(decl_loc, std::move(body));
}



vyb::ast::StmtPtr StatementParser::try_parse_tuple_destructure() {
    // Detect pattern: IDENTIFIER [, IDENTIFIER]* = expression
    // This handles untyped tuple destructuring like: x, y = get_values()
    
    if (peek().type != vyb::TokenType::IDENTIFIER) {
        return nullptr;
    }
    
    // Save position in case we need to backtrack
    size_t saved_pos = this->pos_;
    
    // Collect identifiers
    std::vector<std::unique_ptr<vyb::ast::Identifier>> identifiers;
    
    // Parse first identifier
    token::Token id_token = consume();
    identifiers.push_back(std::make_unique<vyb::ast::Identifier>(id_token.location, id_token.lexeme));
    
    // Look for more identifiers separated by commas
    while (match(vyb::TokenType::COMMA)) {
        // Skip optional type annotation <Type> if present
        if (check(vyb::TokenType::LT)) {
            // Consume the type annotation - skip until >
            consume(); // <
            int depth = 1;
            while (depth > 0 && !IsAtEnd()) {
                auto t = consume();
                if (t.type == vyb::TokenType::LT) depth++;
                else if (t.type == vyb::TokenType::GT) depth--;
            }
        }
        
        // Expect identifier after comma (and optional type)
        if (check(vyb::TokenType::IDENTIFIER)) {
            id_token = consume();
            identifiers.push_back(std::make_unique<vyb::ast::Identifier>(id_token.location, id_token.lexeme));
        } else {
            // Not a valid tuple destructure pattern
            this->pos_ = saved_pos;
            return nullptr;
        }
    }
    
    // Expect assignment operator
    if (!match(vyb::TokenType::EQ)) {
        // Not an assignment, restore position
        this->pos_ = saved_pos;
        return nullptr;
    }
    
    // Parse RHS expression
    vyb::ast::ExprPtr rhs = this->expr_parser_.parse_expression();
    if (!rhs) {
        throw std::runtime_error("Expected expression after '=' in tuple destructure at " +
                                 location_to_string(this->current_location()));
    }
    
    // Consume optional semicolon
    if (match(vyb::TokenType::SEMICOLON)) {
        // consumed
    } else if (IsAtEnd() || peek().type == vyb::TokenType::RBRACE || 
               peek().type == vyb::TokenType::DEDENT ||
               is_statement_start(peek().type)) {
        // Optional semicolon - acceptable terminators
    } else {
        throw std::runtime_error("Expected semicolon, newline, '}', or DEDENT after tuple destructure at " +
                                 location_to_string(this->peek().location));
    }
    
    // Require at least 2 identifiers for tuple destructuring
    if (identifiers.size() < 2) {
        this->pos_ = saved_pos;
        return nullptr;
    }

    return std::make_unique<vyb::ast::TupleDestructureAssignment>(
        identifiers[0]->loc, std::move(identifiers), std::move(rhs)
    );
}

vyb::ast::ExprPtr StatementParser::parse_pattern() {
    // For now, a simple pattern is just an identifier.
    // This will be expanded for destructuring, etc.
    vyb::token::Token id_token = this->expect(vyb::TokenType::IDENTIFIER, "Expected identifier in pattern.");
    return std::make_unique<vyb::ast::Identifier>(id_token.location, id_token.lexeme); // Use lexeme
}


bool StatementParser::looks_like_generic_call() {
    // The statement currently starts with an identifier (pos_ points at it).
    // Peek ahead: if `name<Type...>` is immediately followed by '(' then this is a
    // generic function call with explicit type arguments (probe<Int>(0, 0)) rather
    // than a variable declaration, which is never followed directly by '('.
    size_t start = this->pos_;
    try {
        this->expect(vyb::TokenType::IDENTIFIER, "Expected identifier in generic call check.");
        this->expect(vyb::TokenType::LT, "Expected '<' in generic call check.");
        do {
            ast::TypeNodePtr type_node = this->type_parser_.parse();
            if (!type_node) {
                this->pos_ = start;
                return false;
            }
        } while (this->match(vyb::TokenType::COMMA));
        this->expect(vyb::TokenType::GT, "Expected '>' in generic call check.");
        bool is_call = (this->peek().type == vyb::TokenType::LPAREN);
        this->pos_ = start;
        return is_call;
    } catch (...) {
        this->pos_ = start;
        return false;
    }
}

bool StatementParser::is_statement_start(vyb::TokenType type) const {
    switch (type) {
        case vyb::TokenType::KEYWORD_LET:
        case vyb::TokenType::KEYWORD_MUT:
        case vyb::TokenType::KEYWORD_CONST:
        case vyb::TokenType::KEYWORD_VAR: // Added var
        case vyb::TokenType::KEYWORD_AUTO: // Added auto
        case vyb::TokenType::KEYWORD_ASYNC: // Accept async
        case vyb::TokenType::KEYWORD_EXTERN:
        case vyb::TokenType::KEYWORD_CLASS: // Added class
        case vyb::TokenType::KEYWORD_TEMPLATE: // Added template
        case vyb::TokenType::KEYWORD_IF:
        case vyb::TokenType::KEYWORD_ENSURE:
        case vyb::TokenType::KEYWORD_WHILE:
        case vyb::TokenType::KEYWORD_FOR:
        case vyb::TokenType::KEYWORD_MATCH:
        case vyb::TokenType::KEYWORD_RETURN:
        case vyb::TokenType::KEYWORD_PASS:
        case vyb::TokenType::LBRACE:
        case vyb::TokenType::KEYWORD_BREAK:
        case vyb::TokenType::KEYWORD_CONTINUE:
        case vyb::TokenType::KEYWORD_FREEDOM:
        case vyb::TokenType::KEYWORD_FAIL:
        case vyb::TokenType::KEYWORD_PANIC:
        case vyb::TokenType::KEYWORD_EXIT:
        case vyb::TokenType::KEYWORD_RETHROW:
        case vyb::TokenType::KEYWORD_DEFER:
        case vyb::TokenType::IDENTIFIER: // Added identifier for relaxed syntax
            return true;
        default:
            return this->expr_parser_.is_expression_start(type); // Changed: An expression can also be a statement
    }
}

vyb::ast::StmtPtr StatementParser::parse_try() {
    SourceLocation try_loc = this->current_location();
    this->expect(vyb::TokenType::KEYWORD_TRY);

    // Parse the try block
    std::unique_ptr<ast::BlockStatement> try_block = nullptr;
    SourceLocation try_block_start_loc = peek().location;
    if (this->peek().type == vyb::TokenType::LBRACE) {
        try_block = this->parse_block();
    } else if (this->peek().type == vyb::TokenType::INDENT) {
        try_block_start_loc = consume().location; // Consume INDENT, capture its location
        std::vector<ast::StmtPtr> stmts;
        while (!this->IsAtEnd() && this->peek().type != vyb::TokenType::DEDENT && this->peek().type != vyb::TokenType::END_OF_FILE) {
            while (!this->IsAtEnd() && this->peek().type == vyb::TokenType::NEWLINE) this->consume();
            if (this->IsAtEnd() || this->peek().type == vyb::TokenType::DEDENT) break;
            // Using a sub-parser as per existing pattern in the file for indented blocks
            StatementParser stmt_parser(this->tokens_, this->pos_, 0 /*TODO: review indent_level for sub-parsers*/, this->current_file_path_, this->type_parser_, this->expr_parser_, this->decl_parser_);
            stmts.push_back(stmt_parser.parse());
            this->pos_ = stmt_parser.get_current_pos();
        }
        this->expect(vyb::TokenType::DEDENT);
        try_block = std::make_unique<ast::BlockStatement>(try_block_start_loc, std::move(stmts));
    } else {
        throw error(peek(), "Expected block (starting with '{' or indent) after 'try'.");
    }

    // Parse a single catch clause (if present)
    std::optional<std::string> catch_variable_name; // Stores the variable name 'e'
    // std::unique_ptr<ast::Identifier> catch_identifier_node; // Future: for richer AST
    // std::unique_ptr<ast::TypeNode> catch_type_node;         // Future: for richer AST
    std::unique_ptr<ast::BlockStatement> catch_block = nullptr;

    if (match(vyb::TokenType::KEYWORD_CATCH)) { // Consumes 'catch'
        if (match(vyb::TokenType::LPAREN)) { // Parses 'catch (e: Type)' or 'catch (e)'
            if (peek().type == vyb::TokenType::IDENTIFIER) {
                token::Token ident_token = consume();
                catch_variable_name = ident_token.lexeme;
                // catch_identifier_node = std::make_unique<ast::Identifier>(ident_token.location, ident_token.lexeme);

                if (match(vyb::TokenType::COLON)) {
                    auto parsed_type = type_parser_.parse(); // Parse the type
                    if (!parsed_type) {
                        throw error(peek(), "Expected type annotation after ':' in catch clause.");
                    }
                    // catch_type_node = std::move(parsed_type); // Store if AST supports it
                }
            } else {
                // If language allows 'catch ()' for anonymous catch-all, handle here.
                // For now, assume identifier is required if parentheses are present.
                throw error(peek(), "Expected identifier within parentheses in catch clause.");
            }
            expect(vyb::TokenType::RPAREN);
        } else if (peek().type == vyb::TokenType::IDENTIFIER) { // Parses 'catch e'
            token::Token ident_token = consume();
            catch_variable_name = ident_token.lexeme;
            // catch_identifier_node = std::make_unique<ast::Identifier>(ident_token.location, ident_token.lexeme);
        }
        // If neither LPAREN nor IDENTIFIER follows 'catch', it's 'catch { ... }' or 'catch <indent> ...'
        // In this case, catch_variable_name remains std::nullopt.

        // Parse the catch block
        SourceLocation catch_block_start_loc = peek().location;
        if (this->peek().type == vyb::TokenType::LBRACE) {
            catch_block = this->parse_block();
        } else if (this->peek().type == vyb::TokenType::INDENT) {
            catch_block_start_loc = consume().location; // Consume INDENT, capture its location
            std::vector<ast::StmtPtr> stmts;
            while (!this->IsAtEnd() && this->peek().type != vyb::TokenType::DEDENT && this->peek().type != vyb::TokenType::END_OF_FILE) {
                while (!this->IsAtEnd() && this->peek().type == vyb::TokenType::NEWLINE) this->consume();
                if (this->IsAtEnd() || this->peek().type == vyb::TokenType::DEDENT) break;
                StatementParser stmt_parser(this->tokens_, this->pos_, 0 /*TODO: review indent_level*/, this->current_file_path_, this->type_parser_, this->expr_parser_, this->decl_parser_);
                stmts.push_back(stmt_parser.parse());
                this->pos_ = stmt_parser.get_current_pos();
            }
            this->expect(vyb::TokenType::DEDENT);
            catch_block = std::make_unique<ast::BlockStatement>(catch_block_start_loc, std::move(stmts));
        } else {
            throw error(peek(), "Expected block (starting with '{' or indent) after 'catch' clause.");
        }
    }

    // Skip any additional catch clauses
    while (match(vyb::TokenType::KEYWORD_CATCH)) {
        // Skip catch parameters or variable name
        if (match(vyb::TokenType::LPAREN)) {
            int depth = 1;
            while (depth > 0 && !this->IsAtEnd()) {
                if (match(vyb::TokenType::LPAREN)) depth++;
                else if (match(vyb::TokenType::RPAREN)) depth--;
                else this->consume();
            }
        } else if (peek().type == vyb::TokenType::IDENTIFIER) {
            this->consume();
        }
        // Skip catch block
        if (peek().type == vyb::TokenType::LBRACE) {
            this->parse_block();
        } else if (peek().type == vyb::TokenType::INDENT) {
            this->consume(); // consume INDENT
            while (!this->IsAtEnd() && peek().type != vyb::TokenType::DEDENT && peek().type != vyb::TokenType::END_OF_FILE) {
                this->consume();
            }
            this->expect(vyb::TokenType::DEDENT);
        } else {
            throw error(peek(), "Expected block after 'catch' in try statement.");
        }
    }

    // Optionally parse a finally block
    std::unique_ptr<ast::BlockStatement> finally_block = nullptr;
    if (match(vyb::TokenType::KEYWORD_FINALLY)) { // Consumes 'finally'
        SourceLocation finally_block_start_loc = peek().location;
        if (this->peek().type == vyb::TokenType::LBRACE) {
            finally_block = this->parse_block();
        } else if (this->peek().type == vyb::TokenType::INDENT) {
            finally_block_start_loc = consume().location; // Consume INDENT, capture its location
            std::vector<ast::StmtPtr> stmts;
            while (!this->IsAtEnd() && this->peek().type != vyb::TokenType::DEDENT && this->peek().type != vyb::TokenType::END_OF_FILE) {
                while (!this->IsAtEnd() && this->peek().type == vyb::TokenType::NEWLINE) this->consume();
                if (this->IsAtEnd() || this->peek().type == vyb::TokenType::DEDENT) break;
                StatementParser stmt_parser(this->tokens_, this->pos_, 0/*TODO: review indent_level*/, this->current_file_path_, this->type_parser_, this->expr_parser_, this->decl_parser_);
                stmts.push_back(stmt_parser.parse());
                this->pos_ = stmt_parser.get_current_pos();
            }
            this->expect(vyb::TokenType::DEDENT);
            finally_block = std::make_unique<ast::BlockStatement>(finally_block_start_loc, std::move(stmts));
        } else {
            throw error(peek(), "Expected block (starting with '{' or indent) after 'finally'.");
        }
    }

    // Assuming ast::TryStatement constructor takes: try_loc, try_block, catch_variable_name (optional<string>), catch_block, finally_block
    return std::make_unique<ast::TryStatement>(try_loc, std::move(try_block), catch_variable_name, std::move(catch_block), std::move(finally_block));
}

// Minimal stub implementations for defer and await
vyb::ast::StmtPtr StatementParser::parse_defer() {
    SourceLocation defer_loc = consume().location; // Consume 'defer'
    // Parse the deferred expression statement
    vyb::ast::ExprPtr expr = expr_parser_.parse_expression();
    if (!expr) {
        throw error(peek(), "Expected expression after 'defer'.");
    }
    // Consume optional semicolon
    if (peek().type == vyb::TokenType::SEMICOLON) consume();
    // Wrap the expression in an ExpressionStatement
    auto innerStmt = std::make_unique<vyb::ast::ExpressionStatement>(defer_loc, std::move(expr));
    return std::make_unique<vyb::ast::DeferStatement>(defer_loc, std::move(innerStmt));
}
vyb::ast::StmtPtr StatementParser::parse_await() {
    SourceLocation await_loc = consume().location; // Consume 'await' and get its location
    vyb::ast::ExprPtr expression = expr_parser_.parse_expression(); // Parse the expression being awaited

    if (!expression) {
        throw error(peek(), "Expected expression after 'await'.");
    }

    // Optional semicolon or newline handling (similar to ExpressionStatement)
    if (match(vyb::TokenType::SEMICOLON)) {
        // Semicolon consumed.
    } else if (peek().type == vyb::TokenType::NEWLINE ||
               IsAtEnd() ||
               peek().type == vyb::TokenType::RBRACE ||
               peek().type == vyb::TokenType::DEDENT) {
        // Optional semicolon: fine.
    } else {
        throw error(peek(), "Expected semicolon or newline after await statement.");
    }

    // Create an ExpressionStatement with a UnaryExpression for await
    // This assumes Await is handled as a UnaryExpression in the AST for now.
    // If a dedicated AwaitStatement or AwaitExpression AST node exists and is preferred,
    // adjust accordingly.
    token::Token await_op_token(TokenType::KEYWORD_AWAIT, "await", await_loc); // Create a token for await operator
    auto await_unary_expr = std::make_unique<ast::UnaryExpression>(await_loc, await_op_token, std::move(expression));
    return std::make_unique<ast::ExpressionStatement>(await_loc, std::move(await_unary_expr));
}

vyb::ast::StmtPtr StatementParser::parse_match() {
    SourceLocation match_loc = expect(vyb::TokenType::KEYWORD_MATCH, "Expected 'match'").location;

    // Expect opening parenthesis for the match expression
    expect(vyb::TokenType::LPAREN, "Expected '(' after 'match'.");

    // Parse the expression to match against (can be any expression)
    vyb::ast::ExprPtr match_expr = expr_parser_.parse_expression();
    if (!match_expr) {
        throw error(peek(), "Expected expression in match statement.");
    }

    // Expect closing parenthesis
    expect(vyb::TokenType::RPAREN, "Expected ')' after match expression.");

    // Expect opening brace
    expect(vyb::TokenType::LBRACE, "Expected '{' after match expression.");

    // Parse match arms: pattern => expression
    std::vector<std::pair<vyb::ast::ExprPtr, vyb::ast::ExprPtr>> cases;

    std::vector<vyb::ast::ExprPtr> guards;
    while (!check(vyb::TokenType::RBRACE) && !IsAtEnd()) {
        // Skip newlines between cases
        while (match(vyb::TokenType::NEWLINE)) {}

        if (check(vyb::TokenType::RBRACE)) break;

        // Parse pattern: '?', comparison pattern (e.g., >= 18), or literal
        vyb::ast::ExprPtr pattern;
        if (peek().type == vyb::TokenType::QUESTION_MARK) {
            // Wildcard pattern - represented as nullptr
            consume(); // consume '?'
            pattern = nullptr;
        } else if (peek().type == vyb::TokenType::LT || peek().type == vyb::TokenType::LTEQ ||
                   peek().type == vyb::TokenType::GT || peek().type == vyb::TokenType::GTEQ ||
                   peek().type == vyb::TokenType::EQEQ || peek().type == vyb::TokenType::NOTEQ) {
            // Comparison pattern (e.g., >= 18, < 0, == 5)
            auto op_token = consume(); // consume comparison operator
            auto value = expr_parser_.parse_primary();
            if (!value) {
                throw error(peek(), "Expected value after comparison operator in pattern.");
            }
            pattern = std::make_unique<vyb::ast::ComparisonPattern>(
                op_token.location, op_token, std::move(value)
            );
        } else {
            // Primary expression pattern (literal for exact match). A struct
            // destructuring pattern such as `Point { x, y }` parses as an object
            // literal whose fields are all value-less shorthand entries; detect
            // that form and reinterpret it as a StructPattern.
            pattern = expr_parser_.parse_primary();
            if (!pattern) {
                throw error(peek(), "Expected pattern in match arm.");
            }
            // Enum variant pattern with payload: `Circle(r)`, `Rect(w, h)`. The
            // primary parser returns the bare variant identifier and leaves the
            // `( binding, ... )` group; assemble it into a ConstructionExpression
            // that codegen reinterprets as a variant pattern.
            if (auto* pid = dynamic_cast<ast::Identifier*>(pattern.get())) {
                if (peek().type == vyb::TokenType::LPAREN) {
                    consume(); // '('
                    std::vector<ast::ExprPtr> vbindings;
                    while (peek().type != vyb::TokenType::RPAREN && !IsAtEnd()) {
                        ast::ExprPtr b = expr_parser_.parse_primary();
                        if (!b) {
                            throw error(peek(), "Expected binding name in enum variant pattern.");
                        }
                        vbindings.push_back(std::move(b));
                        if (!match(vyb::TokenType::COMMA)) break;
                    }
                    expect(vyb::TokenType::RPAREN, "Expected ')' after enum variant pattern bindings.");
                    auto vtName = std::make_unique<vyb::ast::TypeName>(
                        pid->loc, std::make_unique<vyb::ast::Identifier>(pid->loc, pid->name));
                    pattern = std::make_unique<vyb::ast::ConstructionExpression>(
                        pid->loc, std::move(vtName), std::move(vbindings));
                }
            }
            // Range pattern: `start..end` (inclusive). Parse the upper bound
            // here since parse_primary stops before binary operators.
            if (peek().type == vyb::TokenType::DOTDOT) {
                consume(); // consume '..'
                auto endExpr = expr_parser_.parse_primary();
                if (!endExpr) {
                    throw error(peek(), "Expected end value after '..' in range pattern.");
                }
                pattern = std::make_unique<vyb::ast::RangeExpression>(
                    pattern->loc, std::move(pattern), std::move(endExpr));
            } else if (auto* obj = dynamic_cast<vyb::ast::ObjectLiteral*>(pattern.get())) {
                if (obj->typePath && !obj->properties.empty()) {
                    bool allShorthand = true;
                    for (const auto& prop : obj->properties) {
                        if (prop.value) { allShorthand = false; break; }
                    }
                    if (allShorthand) {
                        std::vector<std::unique_ptr<vyb::ast::Identifier>> bindings;
                        for (const auto& prop : obj->properties) {
                            bindings.push_back(std::make_unique<vyb::ast::Identifier>(prop.key->loc, prop.key->name));
                        }
                        auto typeName = obj->typePath->clone();
                        pattern = std::make_unique<vyb::ast::StructPattern>(
                            pattern->loc, std::move(typeName), std::move(bindings));
                    }
                }
            }
        }

        // Optional guard clause: `pattern if condition`
        vyb::ast::ExprPtr guard = nullptr;
        if (match(vyb::TokenType::KEYWORD_IF)) {
            guard = expr_parser_.parse_expression();
            if (!guard) {
                throw error(peek(), "Expected condition after 'if' in match arm.");
            }
        }

        // Expect '->' (arrow)
        expect(vyb::TokenType::ARROW, "Expected '->' after match pattern.");

        // Parse result: either a block statement or an expression
        vyb::ast::ExprPtr result;
        if (check(vyb::TokenType::LBRACE)) {
            // Parse block statement and wrap it in a BlockExpression
            auto block_stmt = parse_block();
            result = std::make_unique<vyb::ast::BlockExpression>(
                block_stmt->loc, std::move(block_stmt)
            );
        } else {
            // Parse regular expression
            result = expr_parser_.parse_expression();
            if (!result) {
                throw error(peek(), "Expected expression or block after '->' in match arm.");
            }
        }

        cases.emplace_back(std::move(pattern), std::move(result));
        guards.push_back(std::move(guard));

        // Optional comma
        match(vyb::TokenType::COMMA);

        // Skip trailing newlines
        while (match(vyb::TokenType::NEWLINE)) {}
    }

    expect(vyb::TokenType::RBRACE, "Expected '}' after match cases.");

    return std::make_unique<vyb::ast::MatchStatement>(match_loc, std::move(match_expr), std::move(cases), std::move(guards));
}

std::unique_ptr<vyb::ast::BreakStatement> StatementParser::parse_break() {
    SourceLocation break_loc = expect(vyb::TokenType::KEYWORD_BREAK, "Expected 'break'").location;

    // Optional semicolon
    match(vyb::TokenType::SEMICOLON);

    return std::make_unique<vyb::ast::BreakStatement>(break_loc);
}

std::unique_ptr<vyb::ast::ContinueStatement> StatementParser::parse_continue() {
    SourceLocation continue_loc = expect(vyb::TokenType::KEYWORD_CONTINUE, "Expected 'continue'").location;

    // Optional semicolon
    match(vyb::TokenType::SEMICOLON);

    return std::make_unique<vyb::ast::ContinueStatement>(continue_loc);
}

// Parses an freedom block: 'freedom { ... }'
std::unique_ptr<vyb::ast::UnsafeStatement> StatementParser::parse_unsafe() {
    SourceLocation loc = expect(vyb::TokenType::KEYWORD_FREEDOM, "Expected 'freedom'").location;
    auto blockStmt = parse_block(); // parse_block consumes '{' and '}'
    return std::make_unique<vyb::ast::UnsafeStatement>(loc, std::move(blockStmt));
}

// --- Error Handling Statement Parsers ---

// Parses a fail statement: 'fail ErrorType { field = value }'
std::unique_ptr<vyb::ast::FailStatement> StatementParser::parse_fail() {
    SourceLocation loc = expect(vyb::TokenType::KEYWORD_FAIL, "Expected 'fail'").location;

    vyb::ast::TypeNodePtr explicitErrorType = nullptr;
    bool hasExplicitType = false;
    if (match(vyb::TokenType::LT)) {
        explicitErrorType = type_parser_.parse();
        expect(vyb::TokenType::GT, "Expected '>' after fail error type");
        hasExplicitType = true;
    }

    vyb::ast::ExprPtr errorExpr = nullptr;
    if (hasExplicitType) {
        expect(vyb::TokenType::LPAREN, "Expected '(' after typed fail");
        errorExpr = expr_parser_.parse_expression();
        expect(vyb::TokenType::RPAREN, "Expected ')' after typed fail expression");
    } else {
        // Parse the error expression (e.g., ErrorType { field = value })
        // This is typically a construction expression or identifier
        errorExpr = expr_parser_.parse_expression();
    }

    if (!errorExpr) {
        throw std::runtime_error("Expected error expression after 'fail' at " + location_to_string(loc));
    }

    // Optional semicolon
    match(vyb::TokenType::SEMICOLON);

    return std::make_unique<vyb::ast::FailStatement>(loc, std::move(errorExpr), std::move(explicitErrorType));
}

// Parses a panic statement: 'panic("message")'
std::unique_ptr<vyb::ast::PanicStatement> StatementParser::parse_panic() {
    SourceLocation loc = expect(vyb::TokenType::KEYWORD_PANIC, "Expected 'panic'").location;

    // Expect '('
    expect(vyb::TokenType::LPAREN, "Expected '(' after 'panic'");

    // Parse the panic message (typically a string literal)
    auto messageExpr = expr_parser_.parse_expression();

    if (!messageExpr) {
        throw std::runtime_error("Expected panic message expression at " + location_to_string(loc));
    }

    // Expect ')'
    expect(vyb::TokenType::RPAREN, "Expected ')' after panic message");

    // Optional semicolon
    match(vyb::TokenType::SEMICOLON);

    return std::make_unique<vyb::ast::PanicStatement>(loc, std::move(messageExpr));
}

// Parses an exit statement: 'exit(n)' — terminates process with exit code n
std::unique_ptr<vyb::ast::ExitStatement> StatementParser::parse_exit() {
    SourceLocation loc = expect(vyb::TokenType::KEYWORD_EXIT, "Expected 'exit'").location;

    // Expect '('
    expect(vyb::TokenType::LPAREN, "Expected '(' after 'exit'");

    // Parse the exit code expression (must evaluate to Int)
    auto codeExpr = expr_parser_.parse_expression();

    if (!codeExpr) {
        throw std::runtime_error("Expected exit code expression at " + location_to_string(loc));
    }

    // Expect ')'
    expect(vyb::TokenType::RPAREN, "Expected ')' after exit code");

    // Optional semicolon
    match(vyb::TokenType::SEMICOLON);

    return std::make_unique<vyb::ast::ExitStatement>(loc, std::move(codeExpr));
}

// Parses a rethrow statement: 'rethrow' or 'fail NewError { cause = e }'
std::unique_ptr<vyb::ast::RethrowStatement> StatementParser::parse_rethrow() {
    SourceLocation loc = expect(vyb::TokenType::KEYWORD_RETHROW, "Expected 'rethrow'").location;

    // Check if there's an error transformation (currently we just support simple rethrow)
    // In the future, we might support: rethrow NewError { cause = e }
    // For now, just simple rethrow

    // Optional semicolon
    match(vyb::TokenType::SEMICOLON);

    return std::make_unique<vyb::ast::RethrowStatement>(loc, nullptr);
}

} // namespace vyb
