#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include <stdexcept> // For std::runtime_error
#include <vector> // for std::vector
#include <memory> // for std::unique_ptr

namespace Vyn { // Added namespace Vyn

// Constructor updated
StatementParser::StatementParser(const std::vector<Vyn::Token>& tokens, size_t& pos, int indent_level, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser)
    : BaseParser(tokens, pos, file_path), indent_level_(indent_level), type_parser_(type_parser), expr_parser_(expr_parser) {}

std::unique_ptr<Vyn::AST::StmtNode> StatementParser::parse() {
    this->skip_comments_and_newlines();
    Vyn::Token current_token = this->peek();
    Vyn::AST::SourceLocation loc = this->current_location();

    // Dispatch based on the token type
    // Example:
    if (current_token.type == Vyn::TokenType::KEYWORD_IF) {
        return this->parse_if();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_WHILE) {
        return this->parse_while();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_FOR) {
        return this->parse_for();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_RETURN) {
        return this->parse_return();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_LET || current_token.type == Vyn::TokenType::KEYWORD_VAR) { // Assuming 'var' also for mut variables
        return this->parse_var_decl();
    } else if (current_token.type == Vyn::TokenType::LBRACE) { // Block statement
        return this->parse_block();
    } else if (current_token.type == Vyn::TokenType::KEYWORD_BREAK) {
        this->consume(); // consume 'break'
        // Assuming SEMICOLON is optional or handled by newline logic
        return std::make_unique<Vyn::AST::BreakStmtNode>(loc);
    } else if (current_token.type == Vyn::TokenType::KEYWORD_CONTINUE) {
        this->consume(); // consume 'continue'
        // Assuming SEMICOLON is optional or handled by newline logic
        return std::make_unique<Vyn::AST::ContinueStmtNode>(loc);
    }
    // ... other statement types ...
    
    // Default to expression statement if no other keyword matches
    // This needs to be careful not to consume tokens that start another valid statement type
    // that isn't explicitly checked above yet.
    auto expr_stmt = this->parse_expression_statement();
    if (expr_stmt && expr_stmt->expression) { // Ensure it's a valid expression statement
        return expr_stmt;
    }

    // If it's not a recognized statement and not a valid expression statement that could be parsed
    if (current_token.type != Vyn::TokenType::END_OF_FILE && current_token.type != Vyn::TokenType::DEDENT && current_token.type != Vyn::TokenType::RBRACE) {
        // Avoid erroring on RBRACE or DEDENT as those terminate blocks.
        throw std::runtime_error("Unexpected token in StatementParser: " + current_token.lexeme + " at " + loc.toString());
    }
    
    return nullptr; // No statement found (e.g. EOF, or end of block)
}

std::unique_ptr<Vyn::AST::ExpressionStmtNode> StatementParser::parse_expression_statement() {
    auto expr_loc_start = this->peek().location; // Capture location before parsing expression
    auto expr = this->expr_parser_.parse(); // Corrected: use public parse() method
    if (!expr) {
        // Error handling or return nullptr
        return nullptr;
    }
    auto loc = expr->location; // Use the expression's location

    // Check if the expression is followed by a semicolon or is at the end of a block/file.
    if (this->IsAtEnd() || this->peek().type == TokenType::SEMICOLON || this->peek().type == TokenType::END_OF_FILE || this->peek().type == TokenType::DEDENT) {
        if (this->peek().type == TokenType::SEMICOLON) {
            this->expect(TokenType::SEMICOLON); // Corrected: use expect instead of consume(type)
        }
        return std::make_unique<Vyn::AST::ExpressionStmtNode>(loc, std::move(expr));
    } else {
        // This case might indicate a syntax error, depending on language grammar
        // For now, assume it's a valid expression statement without a semicolon (e.g., last line in a block)
        // Or, it could be an error if semicolons are strictly required.
        // Add error reporting if necessary.
        // For example:
        // this->add_error("Expected semicolon after expression statement", this->peek().location);
        return std::make_unique<Vyn::AST::ExpressionStmtNode>(loc, std::move(expr));
    }
}

std::unique_ptr<Vyn::AST::BlockStmtNode> StatementParser::parse_block() {
    Vyn::AST::SourceLocation loc = this->current_location();
    this->expect(Vyn::TokenType::LBRACE);
    std::vector<std::unique_ptr<Vyn::AST::StmtNode>> statements;
    // TODO: Handle indentation if Vyn is indentation-sensitive within { } or only uses { }
    // Assuming { } delimited blocks for now.
    // The indent_level_ member might be used if Vyn supports Python-style blocks without braces.

    this->skip_comments_and_newlines();
    while (this->peek().type != TokenType::RBRACE && this->peek().type != TokenType::END_OF_FILE) {
        auto stmt = this->parse(); // Recursive call to parse statements within the block
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
        this->skip_comments_and_newlines(); // After each statement
    }
    this->expect(Vyn::TokenType::RBRACE);
    return std::make_unique<Vyn::AST::BlockStmtNode>(loc, std::move(statements));
}


// Placeholders for other statement parsing methods
std::unique_ptr<Vyn::AST::IfStmtNode> StatementParser::parse_if() {
    Vyn::AST::SourceLocation loc = this->current_location();
    this->expect(Vyn::TokenType::KEYWORD_IF);

    // Condition
    this->expect(Vyn::TokenType::LPAREN); // Assuming Vyn 'if' conditions are in parentheses like C/Java/Rust
    auto condition = this->expr_parser_.parse();
    if (!condition) {
        throw std::runtime_error("Expected condition in if statement at " + this->current_location().toString());
    }
    this->expect(Vyn::TokenType::RPAREN);

    // Then branch (must be a block)
    if (this->peek().type != Vyn::TokenType::LBRACE) {
        throw std::runtime_error("Expected block statement for 'then' branch of if statement at " + this->current_location().toString());
    }
    auto then_branch = this->parse_block();
    if (!then_branch) {
        // parse_block should throw on failure, but as a safeguard:
        throw std::runtime_error("Failed to parse 'then' block in if statement at " + loc.toString());
    }

    // Else branch
    std::unique_ptr<Vyn::AST::StmtNode> else_branch = nullptr;
    if (this->match(Vyn::TokenType::KEYWORD_ELSE)) {
        if (this->peek().type == Vyn::TokenType::KEYWORD_IF) { // Else if
            else_branch = this->parse_if(); // Recursive call for 'else if'
        } else if (this->peek().type == Vyn::TokenType::LBRACE) { // Else block
            else_branch = this->parse_block();
            if (!else_branch) {
                 throw std::runtime_error("Failed to parse 'else' block in if statement at " + this->current_location().toString());
            }
        } else {
            throw std::runtime_error("Expected 'if' or block after 'else' at " + this->current_location().toString());
        }
    }

    return std::make_unique<Vyn::AST::IfStmtNode>(loc, std::move(condition), std::move(then_branch), std::move(else_branch));
}

std::unique_ptr<Vyn::AST::WhileStmtNode> StatementParser::parse_while() {
    Vyn::AST::SourceLocation loc = this->current_location();
    this->expect(Vyn::TokenType::KEYWORD_WHILE);

    // Condition
    this->expect(Vyn::TokenType::LPAREN); // Assuming Vyn 'while' conditions are in parentheses
    auto condition = this->expr_parser_.parse();
    if (!condition) {
        throw std::runtime_error("Expected condition in while statement at " + this->current_location().toString());
    }
    this->expect(Vyn::TokenType::RPAREN);

    // Body (must be a block)
    if (this->peek().type != Vyn::TokenType::LBRACE) {
        throw std::runtime_error("Expected block statement for while loop body at " + this->current_location().toString());
    }
    auto body = this->parse_block();
    if (!body) {
        throw std::runtime_error("Failed to parse while loop body at " + loc.toString());
    }

    return std::make_unique<Vyn::AST::WhileStmtNode>(loc, std::move(condition), std::move(body));
}

std::unique_ptr<Vyn::AST::ForStmtNode> StatementParser::parse_for() {
    Vyn::AST::SourceLocation loc = this->current_location();
    this->expect(Vyn::TokenType::KEYWORD_FOR);

    this->expect(Vyn::TokenType::LPAREN); // Assuming Vyn 'for' loops use parentheses for setup

    // Pattern
    auto pattern = this->parse_pattern(); // Pass true for is_context_mutable if for loop vars are mutable by default
    if (!pattern) {
        throw std::runtime_error("Expected pattern in for loop at " + this->current_location().toString());
    }

    this->expect(Vyn::TokenType::KEYWORD_IN); // Or similar keyword like COLON in Python for item : iterable

    // Iterable
    auto iterable = this->expr_parser_.parse();
    if (!iterable) {
        throw std::runtime_error("Expected iterable expression in for loop at " + this->current_location().toString());
    }
    
    this->expect(Vyn::TokenType::RPAREN);


    // Body (must be a block)
    if (this->peek().type != Vyn::TokenType::LBRACE) {
        throw std::runtime_error("Expected block statement for for loop body at " + this->current_location().toString());
    }
    auto body = this->parse_block();
    if (!body) {
        throw std::runtime_error("Failed to parse for loop body at " + loc.toString());
    }
    // Corrected constructor call for ForStmtNode
    return std::make_unique<Vyn::AST::ForStmtNode>(loc, std::move(pattern), std::move(iterable), std::move(body));
}

std::unique_ptr<Vyn::AST::ReturnStmtNode> StatementParser::parse_return() {
    Vyn::AST::SourceLocation loc = this->current_location();
    this->expect(Vyn::TokenType::KEYWORD_RETURN);
    std::unique_ptr<Vyn::AST::ExprNode> value = nullptr;
    
    if (this->peek().type != TokenType::SEMICOLON && this->peek().type != TokenType::NEWLINE && this->peek().type != TokenType::END_OF_FILE && this->peek().type != TokenType::DEDENT) {
        value = this->expr_parser_.parse();
    }
    // Optional: this->expect(Vyn::TokenType::SEMICOLON);
    return std::make_unique<Vyn::AST::ReturnStmtNode>(loc, std::move(value));
}


std::unique_ptr<Vyn::AST::VarDeclStmtNode> StatementParser::parse_var_decl() {
    Vyn::AST::SourceLocation loc = this->current_location();
    bool is_mutable_decl = false; 
    if (this->peek().type == Vyn::TokenType::KEYWORD_LET) {
        this->consume(); 
        is_mutable_decl = false;
    } else if (this->peek().type == Vyn::TokenType::KEYWORD_VAR) {
        this->consume(); 
        is_mutable_decl = true;
    } else {
        throw std::runtime_error("Expected 'let' or 'var' for variable declaration at " + loc.toString());
    }

    auto pattern = this->parse_pattern(is_mutable_decl);
    if (!pattern) {
        throw std::runtime_error("Expected pattern after 'let'/'var' at " + this->current_location().toString());
    }

    std::unique_ptr<Vyn::AST::TypeNode> type_annotation = nullptr;
    if (this->match(Vyn::TokenType::COLON)) {
        type_annotation = this->type_parser_.parse();
        if (!type_annotation) {
            throw std::runtime_error("Expected type annotation after ':' in variable declaration at " + this->current_location().toString());
        }
    }

    std::unique_ptr<Vyn::AST::ExprNode> initializer = nullptr;
    if (this->match(Vyn::TokenType::EQ)) {
        initializer = this->expr_parser_.parse();
        if (!initializer) {
            throw std::runtime_error("Expected initializer expression after '=' in variable declaration at " + this->current_location().toString());
        }
    } else {
        if (!type_annotation) { 
             throw std::runtime_error("Variable declaration requires an initializer if type is not specified, at " + loc.toString());
        }
    }
    
    this->expect(Vyn::TokenType::SEMICOLON); 

    return std::make_unique<Vyn::AST::VarDeclStmtNode>(loc, std::move(pattern), std::move(type_annotation), std::move(initializer), is_mutable_decl);
}

// Changed parse_pattern to not take is_context_mutable as a parameter,
// as IdentifierPatternNode now has its own 'isMutable' field set by 'mut' keyword.
// The VarDeclStmtNode's 'isMut' (from let/var) applies to the binding itself, not the pattern's internal structure.
std::unique_ptr<Vyn::AST::PatternNode> StatementParser::parse_pattern(bool is_binding_mutable /* = false */) {
    Vyn::AST::SourceLocation loc = this->current_location();
    this->skip_comments_and_newlines();
    Vyn::Token token = this->peek();

    // Wildcard: _
    if (this->match(Vyn::TokenType::UNDERSCORE)) {
        return std::make_unique<Vyn::AST::WildcardPatternNode>(loc);
    }

    // Tuple Pattern: (p1, p2, ...)
    if (token.type == Vyn::TokenType::LPAREN) {
        this->consume(); // LPAREN
        std::vector<std::unique_ptr<Vyn::AST::PatternNode>> elements;
        if (this->peek().type != Vyn::TokenType::RPAREN) {
            do {
                auto elem_pattern = this->parse_pattern(is_binding_mutable); // Pass context
                if (!elem_pattern) {
                    throw std::runtime_error("Expected pattern inside tuple pattern at " + this->current_location().toString());
                }
                elements.push_back(std::move(elem_pattern));
            } while (this->match(Vyn::TokenType::COMMA));
        }
        this->expect(Vyn::TokenType::RPAREN);
        return std::make_unique<Vyn::AST::TuplePatternNode>(loc, std::move(elements));
    }

    // Check for 'mut' keyword before an identifier pattern
    bool is_explicitly_mut_pattern = false;
    Vyn::AST::SourceLocation mut_keyword_loc = this->current_location(); // For potential 'mut' keyword
    if (this->match(Vyn::TokenType::KEYWORD_MUT)) {
        is_explicitly_mut_pattern = true;
        // After 'mut', we expect an identifier or a pattern that can be made mutable.
        // For now, assuming 'mut' only applies to simple identifier patterns directly.
        // If 'mut' can prefix other patterns like 'mut (a,b)' or 'mut Point{x,y}', this logic needs expansion.
        token = this->peek(); // Update token after consuming 'mut'
        loc = this->current_location(); // Update loc to the start of the actual pattern after 'mut'
    }

    // Identifier, EnumVariant, or Struct Pattern
    if (token.type == Vyn::TokenType::IDENTIFIER) {
        Vyn::AST::SourceLocation path_start_loc = loc; // Use updated loc if 'mut' was present
        std::vector<std::unique_ptr<Vyn::AST::IdentifierNode>> segments;
        
        segments.push_back(std::make_unique<Vyn::AST::IdentifierNode>(this->current_location(), this->consume().lexeme));

        while (this->peek().type == Vyn::TokenType::COLONCOLON) { // Corrected from DOUBLE_COLON
            this->consume(); // Consume '::'
            if (this->peek().type != Vyn::TokenType::IDENTIFIER) {
                throw std::runtime_error("Expected identifier after '::' in qualified name pattern at " + this->current_location().toString());
            }
            segments.push_back(std::make_unique<Vyn::AST::IdentifierNode>(this->current_location(), this->consume().lexeme));
        }
        
        auto qualified_path = std::make_unique<Vyn::AST::PathNode>(path_start_loc, std::move(segments));

        if (this->peek().type == Vyn::TokenType::LPAREN) {
            if (is_explicitly_mut_pattern) {
                 throw std::runtime_error("'mut' keyword cannot precede an enum variant pattern: " + qualified_path->getPathString() + " at " + mut_keyword_loc.toString());
            }
            this->consume(); // LPAREN
            std::vector<std::unique_ptr<Vyn::AST::PatternNode>> arguments;
            if (this->peek().type != Vyn::TokenType::RPAREN) {
                do {
                    auto arg_pattern = this->parse_pattern(is_binding_mutable); // Propagate context mutability
                    if (!arg_pattern) {
                        throw std::runtime_error("Expected pattern argument for enum variant '" + qualified_path->getPathString() + "' at " + this->current_location().toString());
                    }
                    arguments.push_back(std::move(arg_pattern));
                } while (this->match(Vyn::TokenType::COMMA));
            }
            this->expect(Vyn::TokenType::RPAREN);
            return std::make_unique<Vyn::AST::EnumVariantPatternNode>(qualified_path->location, std::move(qualified_path), std::move(arguments));
        }
        else if (this->peek().type == Vyn::TokenType::LBRACE) {
            if (is_explicitly_mut_pattern) {
                 throw std::runtime_error("'mut' keyword cannot precede a struct pattern: " + qualified_path->getPathString() + " at " + mut_keyword_loc.toString());
            }
            this->consume(); // LBRACE
            std::vector<Vyn::AST::StructPatternField> fields;
            if (this->peek().type != Vyn::TokenType::RBRACE) {
                do {
                    Vyn::AST::SourceLocation field_loc = this->current_location();
                    if (this->peek().type != Vyn::TokenType::IDENTIFIER) {
                        throw std::runtime_error("Expected field name in struct pattern for '" + qualified_path->getPathString() + "' at " + this->current_location().toString());
                    }
                    // Changed to directly use the string for field_name in StructPatternField constructor
                    std::string field_name_str = this->consume().lexeme;
                    
                    this->expect(Vyn::TokenType::COLON);
                    
                    auto field_pattern = this->parse_pattern(is_binding_mutable); // Propagate context mutability
                    if (!field_pattern) {
                        throw std::runtime_error("Expected pattern for field '" + field_name_str + "' in struct pattern for '" + qualified_path->getPathString() + "' at " + this->current_location().toString());
                    }
                    // Pass field_name_str directly
                    fields.emplace_back(field_loc, std::move(field_name_str), std::move(field_pattern));
                } while (this->match(Vyn::TokenType::COMMA));
            }
            this->expect(Vyn::TokenType::RBRACE);
            return std::make_unique<Vyn::AST::StructPatternNode>(qualified_path->location, std::move(qualified_path), std::move(fields));
        }
        
        if (qualified_path->segments.size() > 1) {
            if (is_explicitly_mut_pattern) {
                 throw std::runtime_error("'mut' keyword cannot precede a qualified path: " + qualified_path->getPathString() + " at " + mut_keyword_loc.toString());
            }
            // This error might be too strict if qualified paths are allowed for simple bindings,
            // but for now, assume simple identifier patterns are not qualified unless they are enum/struct.
            // However, a simple `foo::bar` could be a valid pattern if `foo` is a module/enum and `bar` is a constant.
            // For now, let's assume if it's not an enum/struct pattern, it must be a simple identifier.
            // This means qualified_path->segments.size() == 1 for IdentifierPatternNode.
            // The error below is for cases like `let foo::bar = ...` where `foo::bar` is not an enum/struct pattern.
            // This logic might need adjustment based on language semantics for path patterns.
             throw std::runtime_error("Unexpected '::' in simple identifier pattern '" + qualified_path->getPathString() + "' at " + qualified_path->location.toString() + ". If this is an enum or struct, it's missing () or {}.");
        }
        // For IdentifierPatternNode, the 'isMutable' field refers to the mutability of the binding introduced by the pattern itself (e.g. `mut x`).
        // The `is_binding_mutable` from `let`/`var` is handled by `VarDeclStmtNode::isMut`.
        // So, IdentifierPatternNode's mutability is solely determined by the `mut` keyword before the identifier in the pattern.
        return std::make_unique<Vyn::AST::IdentifierPatternNode>(qualified_path->segments[0]->location, std::move(qualified_path->segments[0]), is_explicitly_mut_pattern);
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
        
        std::unique_ptr<Vyn::AST::ExprNode> literal_expr;
        this->consume(); // Consume the literal token identified by peek()

        if (token.type == Vyn::TokenType::INT_LITERAL) {
            literal_expr = std::make_unique<Vyn::AST::IntLiteralNode>(loc, std::stoll(token.lexeme)); // Corrected to IntLiteralNode and token.lexeme
        } else if (token.type == Vyn::TokenType::FLOAT_LITERAL) {
            literal_expr = std::make_unique<Vyn::AST::FloatLiteralNode>(loc, std::stod(token.lexeme)); // Corrected to token.lexeme
        } else if (token.type == Vyn::TokenType::STRING_LITERAL) {
            literal_expr = std::make_unique<Vyn::AST::StringLiteralNode>(loc, token.lexeme); // Corrected to token.lexeme
        } else if (token.type == Vyn::TokenType::CHAR_LITERAL) {
            literal_expr = std::make_unique<Vyn::AST::CharLiteralNode>(loc, token.lexeme.empty() ? '\\0' : token.lexeme[0]); // Corrected to token.lexeme
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

    throw std::runtime_error("Unexpected token in pattern at " + loc.toString() + ". Token: " + token.lexeme + " (" + Vyn::token_type_to_string(token.type) + ")");
}

} // namespace Vyn
