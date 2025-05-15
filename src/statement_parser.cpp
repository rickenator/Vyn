#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include <stdexcept> // For std::runtime_error
#include <vector> // for std::vector
#include <memory> // for std::unique_ptr
#include <string> // for std::to_string

namespace vyn {

// Helper to convert SourceLocation to string for error messages
static std::string location_to_string(const vyn::SourceLocation& loc) {
    return loc.filePath + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column);
}

// Constructor updated
StatementParser::StatementParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, int indent_level, const std::string& file_path, TypeParser& type_parser, ExpressionParser& expr_parser)
    : BaseParser(tokens, pos, file_path), indent_level_(indent_level), type_parser_(type_parser), expr_parser_(expr_parser) {}

vyn::StmtPtr StatementParser::parse() {
    this->skip_comments_and_newlines();
    vyn::token::Token current_token = this->peek();
    vyn::SourceLocation loc = this->current_location();

    // If at end of file or block, return nullptr (prevents double-parse at EOF)
    if (current_token.type == vyn::TokenType::END_OF_FILE ||
        current_token.type == vyn::TokenType::DEDENT ||
        current_token.type == vyn::TokenType::RBRACE) {
        return nullptr;
    }

    if (current_token.type == vyn::TokenType::KEYWORD_IF) {
        return this->parse_if();
    } else if (current_token.type == vyn::TokenType::KEYWORD_WHILE) {
        return this->parse_while();
    } else if (current_token.type == vyn::TokenType::KEYWORD_FOR) {
        return this->parse_for();
    } else if (current_token.type == vyn::TokenType::KEYWORD_RETURN) {
        return this->parse_return();
    } else if (current_token.type == vyn::TokenType::KEYWORD_LET || current_token.type == vyn::TokenType::KEYWORD_VAR || current_token.type == vyn::TokenType::KEYWORD_CONST) {
        return this->parse_var_decl();
    } else if (current_token.type == vyn::TokenType::LBRACE) {
        return this->parse_block();
    } else if (current_token.type == vyn::TokenType::KEYWORD_BREAK) {
        this->consume();
        return std::make_unique<vyn::BreakStatement>(loc);
    } else if (current_token.type == vyn::TokenType::KEYWORD_CONTINUE) {
        this->consume();
        return std::make_unique<vyn::ContinueStatement>(loc);
    } else if (current_token.type == vyn::TokenType::KEYWORD_TRY) {
        this->consume();
        // Parse the try block (must be a block)
        std::unique_ptr<vyn::BlockStatement> try_block;
        if (this->peek().type == vyn::TokenType::LBRACE || this->peek().type == vyn::TokenType::INDENT) {
            try_block = this->parse_block();
        } else {
            throw std::runtime_error("Expected block after 'try' at " + location_to_string(this->current_location()));
        }

        std::optional<std::string> catch_ident;
        std::unique_ptr<vyn::BlockStatement> catch_block = nullptr;
        std::unique_ptr<vyn::BlockStatement> finally_block = nullptr;

        // Parse all catch blocks - currently we only support one in the AST, so we'll combine them
        std::vector<std::pair<std::string, std::unique_ptr<vyn::BlockStatement>>> catch_blocks;
        
        while (this->peek().type == vyn::TokenType::KEYWORD_CATCH) {
            this->consume(); // consume 'catch'
            this->skip_comments_and_newlines(); // Skip any whitespace after 'catch'
            
            std::string current_catch_ident;
            std::string error_type;
            
            // Optional identifier in parentheses: catch (e) or catch (e: ErrorType)
            if (this->peek().type == vyn::TokenType::LPAREN) {
                this->consume(); // consume '('
                this->skip_comments_and_newlines(); // Skip any whitespace after the opening parenthesis
                
                if (this->peek().type != vyn::TokenType::IDENTIFIER) {
                    throw std::runtime_error("Expected identifier in catch clause at " + location_to_string(this->current_location()));
                }
                current_catch_ident = this->consume().lexeme; // consume the identifier
                
                // Handle error type specification (e.g., "e: NetworkError")
                if (this->peek().type == vyn::TokenType::COLON) {
                    this->consume(); // consume ':'
                    this->skip_comments_and_newlines();
                    
                    // Parse the error type
                    if (this->peek().type == vyn::TokenType::IDENTIFIER) {
                        error_type = this->consume().lexeme; // Store the error type
                    } else {
                        throw std::runtime_error("Expected error type after ':' in catch clause at " + location_to_string(this->current_location()));
                    }
                }
                
                this->skip_comments_and_newlines(); // Skip any whitespace before the closing parenthesis
                this->expect(vyn::TokenType::RPAREN); // consume ')'
            } 
            // If there's an identifier directly after 'catch' without parentheses
            else if (this->peek().type == vyn::TokenType::IDENTIFIER) {
                current_catch_ident = this->consume().lexeme; // consume the identifier
            }
            
            this->skip_comments_and_newlines(); // Skip any whitespace after catch parameter
            
            std::unique_ptr<vyn::BlockStatement> current_catch_block;
            if (this->peek().type == vyn::TokenType::LBRACE || this->peek().type == vyn::TokenType::INDENT) {
                current_catch_block = this->parse_block();
            } else {
                // Allow a single statement after 'catch' (wrap in BlockStatement)
                vyn::SourceLocation stmt_loc = this->current_location();
                auto stmt = this->parse();
                this->skip_comments_and_newlines();
                std::vector<vyn::StmtPtr> stmts;
                if (stmt) stmts.push_back(std::move(stmt));
                current_catch_block = std::make_unique<vyn::BlockStatement>(stmt_loc, std::move(stmts));
            }
            
            // Store this catch block for later processing
            catch_blocks.push_back({current_catch_ident, std::move(current_catch_block)});
        }
        
        // Process the catch blocks
        if (!catch_blocks.empty()) {
            // Use the identifier from the first catch block
            catch_ident = catch_blocks[0].first;
            
            // For now, we'll just use the first catch block's body
            catch_block = std::move(catch_blocks[0].second);
            
            // In a more complete implementation, we would combine multiple catch blocks
            // into a single block with conditional logic based on the error type
        }

        // Optionally parse 'finally' block
        if (this->peek().type == vyn::TokenType::KEYWORD_FINALLY) {
            this->consume(); // consume 'finally'
            this->skip_comments_and_newlines(); // Skip any whitespace after 'finally'
            
            if (this->peek().type == vyn::TokenType::LBRACE || this->peek().type == vyn::TokenType::INDENT) {
                finally_block = this->parse_block();
            } else {
                // Allow a single statement after 'finally' (wrap in BlockStatement)
                vyn::SourceLocation stmt_loc = this->current_location();
                auto stmt = this->parse();
                this->skip_comments_and_newlines();
                std::vector<vyn::StmtPtr> stmts;
                if (stmt) stmts.push_back(std::move(stmt));
                finally_block = std::make_unique<vyn::BlockStatement>(stmt_loc, std::move(stmts));
            }
        }

        // At least one of catch or finally must be present
        if (!catch_block && !finally_block) {
            throw std::runtime_error("'try' must be followed by at least a 'catch' or 'finally' block at " + location_to_string(loc));
        }

        return std::make_unique<vyn::TryStatement>(loc, std::move(try_block), std::move(catch_ident), std::move(catch_block), std::move(finally_block));
    } else if (current_token.type == vyn::TokenType::KEYWORD_DEFER) {
        this->consume();
        // Defer: treat as an expression statement
        auto expr = this->expr_parser_.parse();
        return std::make_unique<vyn::ExpressionStatement>(loc, std::move(expr));
    } else if (current_token.type == vyn::TokenType::KEYWORD_ASYNC) {
        // Peek ahead: if next token is 'fn', this is an async function declaration, not an expression
        if (this->peekNext().type == vyn::TokenType::KEYWORD_FN) {
            // Do not consume 'async' here; let the declaration parser handle it
            // We need to delegate to the declaration parser
            // To do this, construct a DeclarationParser and call parse()
            DeclarationParser decl_parser(this->tokens_, this->pos_, this->current_file_path_, this->type_parser_, this->expr_parser_, *this);
            auto decl = decl_parser.parse();
            this->pos_ = decl_parser.get_current_pos();
            return decl;
        } else {
            this->consume();
            // Async at statement level: treat as an expression statement (e.g., await inside)
            auto expr = this->expr_parser_.parse();
            return std::make_unique<vyn::ExpressionStatement>(loc, std::move(expr));
        }
    } else if (current_token.type == vyn::TokenType::KEYWORD_AWAIT) {
        #ifdef VERBOSE
        std::cerr << "[STATEMENT_PARSER] Found await keyword at " 
                  << location_to_string(this->current_location()) 
                  << std::endl;
        #endif
                  
        // We let the expression parser handle the await token and expression parsing
        auto expr = this->expr_parser_.parse();
        
        #ifdef VERBOSE
        std::cerr << "[STATEMENT_PARSER] Expression parser returned " 
                  << (expr ? "valid expression" : "nullptr") 
                  << std::endl;
        #endif
        
        if (!expr) {
            std::string error_msg = "Expected expression after await in statement at " + 
                location_to_string(this->current_location());
                
            #ifdef VERBOSE
            std::cerr << "[STATEMENT_PARSER] ERROR: " << error_msg << std::endl;
            #endif
            
            throw std::runtime_error(error_msg);
        }
        
        #ifdef VERBOSE
        std::cerr << "[STATEMENT_PARSER] Returning await expression statement" << std::endl;
        #endif
        
        return std::make_unique<vyn::ExpressionStatement>(loc, std::move(expr));
    }
    
    auto expr_stmt = this->parse_expression_statement();
    // Check if expr_stmt is not null AND its expression member is also not null
    if (expr_stmt && expr_stmt->expression) {
        return expr_stmt;
    }

    // After parsing, check the *current* token, not the stale one from the start
    vyn::token::Token after_token = this->peek();
    if (after_token.type != vyn::TokenType::END_OF_FILE && after_token.type != vyn::TokenType::DEDENT && after_token.type != vyn::TokenType::RBRACE) {
        throw std::runtime_error("Unexpected token in StatementParser: " + after_token.lexeme + " at " + location_to_string(after_token.location));
    }

    return nullptr;
}

std::unique_ptr<vyn::ExpressionStatement> StatementParser::parse_expression_statement() {
    auto expr_loc_start = this->peek().location; 
    auto expr = this->expr_parser_.parse(); 
    if (!expr) {
        // If we hit end-of-file or end-of-block, just return nullptr (not an error)
        return nullptr;
    }
    // Use the expression's location for the statement node
    auto loc = expr->loc; 

    if (this->IsAtEnd() || this->peek().type == vyn::TokenType::SEMICOLON || this->peek().type == vyn::TokenType::END_OF_FILE || this->peek().type == vyn::TokenType::DEDENT || this->peek().type == vyn::TokenType::RBRACE) {
        if (this->peek().type == vyn::TokenType::SEMICOLON) {
            this->expect(vyn::TokenType::SEMICOLON); 
        }
        return std::make_unique<vyn::ExpressionStatement>(loc, std::move(expr));
    } else {
        // If no semicolon and not at a typical end-of-statement token, it might be an error depending on language rules.
        // For now, assume it's valid (e.g. last expr in a block).
        return std::make_unique<vyn::ExpressionStatement>(loc, std::move(expr));
    }
}

std::unique_ptr<vyn::BlockStatement> StatementParser::parse_block() {
    vyn::SourceLocation loc = this->current_location();
    std::vector<vyn::StmtPtr> statements;

    if (this->peek().type == vyn::TokenType::LBRACE) {
        this->expect(vyn::TokenType::LBRACE);
        this->skip_comments_and_newlines();
        while (this->peek().type != vyn::TokenType::RBRACE && this->peek().type != vyn::TokenType::END_OF_FILE) {
            auto stmt = this->parse(); 
            if (stmt) {
                statements.push_back(std::move(stmt));
            } else {
                break; 
            }
            this->skip_comments_and_newlines(); 
        }
        this->expect(vyn::TokenType::RBRACE);
    } else if (this->peek().type == vyn::TokenType::INDENT) {
        this->expect(vyn::TokenType::INDENT);
        this->skip_comments_and_newlines();
        while (this->peek().type != vyn::TokenType::DEDENT && this->peek().type != vyn::TokenType::END_OF_FILE) {
            auto stmt = this->parse();
            if (stmt) {
                statements.push_back(std::move(stmt));
            } else {
                break;
            }
            this->skip_comments_and_newlines();
        }
        this->expect(vyn::TokenType::DEDENT);
    } else {
        throw std::runtime_error("Expected '{' or INDENT to start a block at " + location_to_string(this->current_location()));
    }
    return std::make_unique<vyn::BlockStatement>(loc, std::move(statements));
}

std::unique_ptr<vyn::IfStatement> StatementParser::parse_if() {
    vyn::SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_IF);

    // Condition - parentheses are optional
    bool has_parens = false;
    if (this->match(vyn::TokenType::LPAREN)) {
        has_parens = true;
    }
    
    auto condition = this->expr_parser_.parse();
    if (!condition) {
        throw std::runtime_error("Expected condition in if statement at " + location_to_string(this->current_location()));
    }
    
    if (has_parens) {
        this->expect(vyn::TokenType::RPAREN);
    }

    // Then branch (can be a block or a single statement)
    vyn::StmtPtr then_branch = nullptr;
    
    // If it's a block (starts with { or INDENT), parse it as a block
    if (this->peek().type == vyn::TokenType::LBRACE || this->peek().type == vyn::TokenType::INDENT) {
        auto then_branch_block = this->parse_block(); // parse_block returns unique_ptr<BlockStatement>
        if (!then_branch_block) {
            throw std::runtime_error("Failed to parse 'then' block in if statement at " + location_to_string(loc));
        }
        then_branch = std::move(then_branch_block);
    } else {
        // Otherwise, parse a single statement and wrap it in a block
        vyn::SourceLocation stmt_loc = this->current_location();
        auto stmt = this->parse();
        if (!stmt) {
            throw std::runtime_error("Expected statement for 'then' branch of if statement at " + location_to_string(stmt_loc));
        }
        
        std::vector<vyn::StmtPtr> stmts;
        stmts.push_back(std::move(stmt));
        then_branch = std::make_unique<vyn::BlockStatement>(stmt_loc, std::move(stmts));
    }


    // Else branch
    vyn::StmtPtr else_branch = nullptr;
    if (this->match(vyn::TokenType::KEYWORD_ELSE)) {
        if (this->peek().type == vyn::TokenType::KEYWORD_IF) { 
            else_branch = this->parse_if(); 
        } else if (this->peek().type == vyn::TokenType::LBRACE || this->peek().type == vyn::TokenType::INDENT) { 
            else_branch = this->parse_block();
            if (!else_branch) {
                throw std::runtime_error("Failed to parse 'else' block in if statement at " + location_to_string(this->current_location()));
            }
        } else {
            // Parse a single statement for the else branch and wrap it in a block
            vyn::SourceLocation stmt_loc = this->current_location();
            auto stmt = this->parse();
            if (!stmt) {
                throw std::runtime_error("Expected statement after 'else' at " + location_to_string(stmt_loc));
            }
            
            std::vector<vyn::StmtPtr> stmts;
            stmts.push_back(std::move(stmt));
            else_branch = std::make_unique<vyn::BlockStatement>(stmt_loc, std::move(stmts));
        }
    }

    return std::make_unique<vyn::IfStatement>(loc, std::move(condition), std::move(then_branch), std::move(else_branch));
}

std::unique_ptr<vyn::WhileStatement> StatementParser::parse_while() {
    vyn::SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_WHILE);

    // Condition
    this->expect(vyn::TokenType::LPAREN); 
    auto condition = this->expr_parser_.parse();
    if (!condition) {
        throw std::runtime_error("Expected condition in while statement at " + location_to_string(this->current_location()));
    }
    this->expect(vyn::TokenType::RPAREN);

    // Body (must be a block)
    if (this->peek().type != vyn::TokenType::LBRACE) {
        throw std::runtime_error("Expected block statement for while loop body at " + location_to_string(this->current_location()));
    }
    auto body_stmt = this->parse_block();
    if (!body_stmt) {
        throw std::runtime_error("Failed to parse while loop body at " + location_to_string(loc));
    }
    vyn::StmtPtr body = std::move(body_stmt);

    return std::make_unique<vyn::WhileStatement>(loc, std::move(condition), std::move(body));
}

std::unique_ptr<vyn::ForStatement> StatementParser::parse_for() {
    vyn::SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_FOR);

    this->expect(vyn::TokenType::LPAREN); 

    // ForStatement in ast.hpp: NodePtr init, ExprPtr test, ExprPtr update, StmtPtr body
    // 'pattern' in 'for pattern in iterable' often becomes a variable declaration.
    // Let's assume 'parse_pattern()' returns an ExprPtr, which is a NodePtr.
    // This might need to be a VarDecl later, or the 'init' field of ForStatement handles it.
    // For now, parse_pattern returns ExprPtr.
    vyn::ExprPtr pattern_expr = this->parse_pattern(); 
    if (!pattern_expr) {
        throw std::runtime_error("Expected pattern in for loop at " + location_to_string(this->current_location()));
    }
    // ForStatement expects NodePtr for init. ExprPtr is compatible.
    vyn::NodePtr init_node = std::move(pattern_expr);


    this->expect(vyn::TokenType::KEYWORD_IN); 

    auto iterable = this->expr_parser_.parse();
    if (!iterable) {
        throw std::runtime_error("Expected iterable expression in for loop at " + location_to_string(this->current_location()));
    }
    
    this->expect(vyn::TokenType::RPAREN);

    // Body (must be a block)
    if (this->peek().type != vyn::TokenType::LBRACE) {
        throw std::runtime_error("Expected block statement for for loop body at " + location_to_string(this->current_location()));
    }
    auto body_stmt = this->parse_block();
    if (!body_stmt) {
        throw std::runtime_error("Failed to parse for loop body at " + location_to_string(loc));
    }
    vyn::StmtPtr body = std::move(body_stmt);
    // For now, test and update are nullptr. This is for a for-in loop.
    // The ForStatement AST node might need to be adapted for for-in loops vs C-style for.
    // Current ForStatement(loc, init, test, update, body)
    // For a for-in loop like 'for x in y', init=x (pattern), iterable=y, body=body.
    // We are passing 'iterable' as 'test' and 'update' as nullptr. This is not quite right.
    // A dedicated ForInStatement or modification of ForStatement in ast.hpp would be better.
    // For now, shoehorning: init = pattern, test = iterable, update = nullptr.
    return std::make_unique<vyn::ForStatement>(loc, std::move(init_node), std::move(iterable), nullptr, std::move(body));
}

std::unique_ptr<vyn::ReturnStatement> StatementParser::parse_return() {
    vyn::SourceLocation loc = this->current_location();
    this->expect(vyn::TokenType::KEYWORD_RETURN);
    vyn::ExprPtr value = nullptr;
    
    // Check if there's something to parse as a value before semicolon/newline etc.
    if (this->peek().type != vyn::TokenType::SEMICOLON && 
        this->peek().type != vyn::TokenType::NEWLINE && 
        this->peek().type != vyn::TokenType::END_OF_FILE && 
        this->peek().type != vyn::TokenType::DEDENT &&
        this->peek().type != vyn::TokenType::RBRACE) { // Also check for RBRACE
        value = this->expr_parser_.parse();
    }
    // Semicolon is optional for return statements in some languages if it's the last thing in a block.
    // For now, let's not expect it strictly.
    return std::make_unique<vyn::ReturnStatement>(loc, std::move(value));
}

std::unique_ptr<vyn::VariableDeclaration> StatementParser::parse_var_decl() {
    vyn::SourceLocation loc = this->current_location();
    bool is_const_decl = true;
    if (this->peek().type == vyn::TokenType::KEYWORD_LET) {
        this->consume();
        is_const_decl = true;
    } else if (this->peek().type == vyn::TokenType::KEYWORD_VAR) {
        this->consume();
        is_const_decl = false;
    } else if (this->peek().type == vyn::TokenType::KEYWORD_CONST) {
        this->consume();
        is_const_decl = true;
    } else {
        throw std::runtime_error("Expected 'let', 'var', or 'const' for variable declaration at " + location_to_string(loc));
    }

    // VariableDeclaration in ast.hpp takes std::unique_ptr<Identifier> id.
    // parse_pattern returns ExprPtr. We need to ensure it's an Identifier.
    vyn::ExprPtr pattern_expr = this->parse_pattern(); // is_binding_mutable (now is_const_decl) is for VarDecl node, not pattern internals
    if (!pattern_expr) {
        throw std::runtime_error("Expected pattern after \\'let\\'/'var\\' at " + location_to_string(this->current_location()));
    }

    std::unique_ptr<vyn::Identifier> identifier_node;
    vyn::Identifier* raw_id = dynamic_cast<vyn::Identifier*>(pattern_expr.get());
    if (raw_id) {
        // Release ownership from pattern_expr and transfer to identifier_node
        pattern_expr.release(); // pattern_expr is now nullptr
        identifier_node.reset(raw_id);
    } else {
        throw std::runtime_error("Pattern for variable declaration must be a simple identifier at " + location_to_string(pattern_expr->loc) + ". Destructuring not supported here.");
    }


    vyn::TypeNodePtr type_node = nullptr; // Changed from TypeAnnotationPtr to TypeNodePtr
    if (this->match(vyn::TokenType::COLON)) {
        type_node = this->type_parser_.parse(); // Changed from type_annotation to type_node
        if (!type_node) { // Changed from type_annotation to type_node
            throw std::runtime_error("Expected type annotation after ':' in variable declaration at " + location_to_string(this->current_location()));
        }
    }

    vyn::ExprPtr initializer = nullptr;
    if (this->match(vyn::TokenType::EQ)) {
        initializer = this->expr_parser_.parse();
        if (!initializer) {
            throw std::runtime_error("Expected initializer expression after '=' in variable declaration at " + location_to_string(this->current_location()));
        }
    } else {
        if (!type_node && is_const_decl) { // Changed from type_annotation to type_node. Constants often require initializers if not typed (or always)
             throw std::runtime_error("Constant variable declaration 'let' requires an initializer if type is not specified, at " + location_to_string(loc));
        }
         // Non-const 'var' without initializer and without type is also often an error or defaults to 'any'
        if (!type_node && !initializer) { // Changed from type_annotation to type_node
             throw std::runtime_error("Variable declaration requires an initializer or a type annotation, at " + location_to_string(loc));
        }
    }
    
    // Semicolon might be optional depending on language style (e.g. newline terminated)
    // For now, let's assume it's expected or handled by skip_comments_and_newlines if it implies statement termination.
    // If semicolons are mandatory:
    // this->expect(vyn::TokenType::SEMICOLON); 

    return std::make_unique<vyn::VariableDeclaration>(loc, std::move(identifier_node), is_const_decl, std::move(type_node), std::move(initializer)); // Changed from type_annotation to type_node
}

// parse_pattern now returns ExprPtr.
// The is_binding_mutable parameter is removed as it's handled by VariableDeclaration::isConst.
// 'mut' keyword inside patterns like 'let (x, mut y)' is not supported by current ast.hpp.
vyn::ExprPtr StatementParser::parse_pattern() {
    vyn::SourceLocation loc = this->current_location();
    this->skip_comments_and_newlines();
    vyn::token::Token token = this->peek();

    // Wildcard: _
    if (this->match(vyn::TokenType::UNDERSCORE)) {
        return std::make_unique<vyn::Identifier>(loc, "_");
    }

    // Tuple Pattern: (p1, p2, ...) -> ArrayLiteral
    if (token.type == vyn::TokenType::LPAREN) {
        this->consume(); // LPAREN
        vyn::SourceLocation arr_loc = loc;
        std::vector<vyn::ExprPtr> elements_expr; // Changed from PatternPtr to ExprPtr
        if (!this->check(vyn::TokenType::RBRACKET)) {
            do {
                // Assuming parse_pattern() can return something convertible to ExprPtr
                // or parse_expression() should be used if array elements are expressions.
                // For now, let's assume parse_pattern() is intended and returns ExprPtr.
                elements_expr.push_back(this->parse_pattern());
            } while (this->match(vyn::TokenType::COMMA));
        }
        this->expect(vyn::TokenType::RBRACKET);
        // Use ArrayLiteralNode as established in expression_parser.cpp
        return std::make_unique<vyn::ArrayLiteralNode>(arr_loc, std::move(elements_expr));
    }

    // 'mut' keyword before an identifier pattern (e.g. `for mut x in ...`, or `let (mut x, y)`)
    // This is not directly supported by vyn::Identifier or VariableDeclaration structure for sub-patterns.
    // If 'mut' is encountered, we can note it, but can't store it in Identifier.
    // For now, if 'mut' is part of the pattern syntax itself (not 'let mut'), it's an error or ignored.
    // Let's assume 'mut' is not allowed directly before a pattern element here.
    if (this->peek().type == vyn::TokenType::KEYWORD_MUT) {
         throw std::runtime_error("\\'mut\\' keyword is not supported directly within patterns here. Use 'var' for mutable bindings. At " + location_to_string(this->current_location()));
    }


    // Identifier, Path (MemberExpression), Enum-like (CallExpression), Struct-like (CallExpression with ObjectLiteral arg)
    if (token.type == vyn::TokenType::IDENTIFIER) {
        vyn::SourceLocation path_start_loc = loc; 
        std::vector<std::unique_ptr<vyn::Identifier>> segments;
        
        segments.push_back(std::make_unique<vyn::Identifier>(this->current_location(), this->consume().lexeme));

        while (this->peek().type == vyn::TokenType::COLONCOLON) { 
            this->consume(); // Consume '::'
            if (this->peek().type != vyn::TokenType::IDENTIFIER) {
                throw std::runtime_error("Expected identifier after \\'::\\' in qualified name pattern at " + location_to_string(this->current_location()));
            }
            segments.push_back(std::make_unique<vyn::Identifier>(this->current_location(), this->consume().lexeme));
        }
        
        // Construct path_expr (Identifier or MemberExpression)
        vyn::ExprPtr path_expr;
        if (segments.size() == 1) {
            path_expr = std::move(segments[0]);
        } else {
            // Build MemberExpression from right to left
            // Example: a::b::c -> MemberExpr(MemberExpr(a,b), c)
            vyn::ExprPtr current_expr = std::move(segments.back());
            segments.pop_back();
            while(!segments.empty()){
                auto obj = std::move(segments.back());
                segments.pop_back();
                // MemberExpression location should span obj.prop
                // For simplicity, using property's location for now or combined.
                // This location might need refinement.
                vyn::SourceLocation member_loc = obj->loc; // Approximate
                current_expr = std::make_unique<vyn::MemberExpression>(member_loc, std::move(obj), std::move(current_expr), false);
            }
            path_expr = std::move(current_expr);
        }


        if (this->peek().type == vyn::TokenType::LPAREN) { // EnumVariant-like pattern: Path(arg1, ...) -> CallExpression
            this->consume(); // LPAREN
            std::vector<vyn::ExprPtr> arguments_expr;
            if (this->peek().type != vyn::TokenType::RPAREN) {
                do {
                    auto arg_expr = this->parse_pattern(); 
                    if (!arg_expr) {
                        throw std::runtime_error("Expected pattern argument for enum-like pattern \\'" + path_expr->toString() + "\\' at " + location_to_string(this->current_location()));
                    }
                    arguments_expr.push_back(std::move(arg_expr));
                } while (this->match(vyn::TokenType::COMMA));
            }
            this->expect(vyn::TokenType::RPAREN);
            return std::make_unique<vyn::CallExpression>(path_start_loc, std::move(path_expr), std::move(arguments_expr));
        }
        else if (this->peek().type == vyn::TokenType::LBRACE) { // Struct-like pattern: Path { field: val, ... } -> CallExpression(Path, ObjectLiteral)
            this->consume(); // LBRACE
            vyn::SourceLocation obj_lit_loc = this->current_location();
            std::vector<vyn::ObjectProperty> fields;
            if (this->peek().type != vyn::TokenType::RBRACE) {
                do {
                    vyn::SourceLocation field_loc = this->current_location();
                    if (this->peek().type != vyn::TokenType::IDENTIFIER) {
                        throw std::runtime_error("Expected field name in struct-like pattern for \\'" + path_expr->toString() + "\\' at " + location_to_string(this->current_location()));
                    }
                    auto field_name_ident = std::make_unique<vyn::Identifier>(this->current_location(), this->consume().lexeme);
                    
                    this->expect(vyn::TokenType::COLON);
                    
                    auto field_pattern_expr = this->parse_pattern(); 
                    if (!field_pattern_expr) {
                        throw std::runtime_error("Expected pattern for field \\'" + field_name_ident->name + "\\' in struct-like pattern for \\'" + path_expr->toString() + "\\' at " + location_to_string(this->current_location()));
                    }
                    fields.emplace_back(field_loc, std::move(field_name_ident), std::move(field_pattern_expr));
                } while (this->match(vyn::TokenType::COMMA));
            }
            this->expect(vyn::TokenType::RBRACE);
            auto object_literal = std::make_unique<vyn::ObjectLiteral>(obj_lit_loc, std::move(fields));
            
            std::vector<vyn::ExprPtr> call_args;
            call_args.push_back(std::move(object_literal));
            return std::make_unique<vyn::CallExpression>(path_start_loc, std::move(path_expr), std::move(call_args));
        }
        
        // If not enum-like or struct-like, it's just the path_expr (Identifier or MemberExpression)
        return path_expr;
    }


    // Literal Pattern
    if (token.type == vyn::TokenType::INT_LITERAL ||
        token.type == vyn::TokenType::FLOAT_LITERAL ||
        token.type == vyn::TokenType::STRING_LITERAL ||
        token.type == vyn::TokenType::CHAR_LITERAL || // Will be mapped to IntegerLiteral
        token.type == vyn::TokenType::KEYWORD_TRUE ||
        token.type == vyn::TokenType::KEYWORD_FALSE) {
        
        vyn::ExprPtr literal_expr_node;
        vyn::SourceLocation literal_loc = this->current_location();
        this->consume(); 

        if (token.type == vyn::TokenType::INT_LITERAL) {
            literal_expr_node = std::make_unique<vyn::IntegerLiteral>(literal_loc, std::stoll(token.lexeme)); 
        } else if (token.type == vyn::TokenType::FLOAT_LITERAL) {
            literal_expr_node = std::make_unique<vyn::FloatLiteral>(literal_loc, std::stod(token.lexeme)); 
        } else if (token.type == vyn::TokenType::STRING_LITERAL) {
            literal_expr_node = std::make_unique<vyn::StringLiteral>(literal_loc, token.lexeme); 
        } else if (token.type == vyn::TokenType::CHAR_LITERAL) {
            // Map char literal to IntegerLiteral (char code)
            if (token.lexeme.empty()) {
                 throw std::runtime_error("Empty char literal at " + location_to_string(literal_loc));
            }
            // Assuming char literal lexeme is the char itself, not escaped value yet.
            // For 'a', lexeme is "a". For '\\n', lexeme might be "\\n" or "n" depending on lexer.
            // For simplicity, take first char. Proper escape handling should be in lexer or here.
            literal_expr_node = std::make_unique<vyn::IntegerLiteral>(literal_loc, static_cast<int64_t>(token.lexeme[0]));
        } else if (token.type == vyn::TokenType::KEYWORD_TRUE) {
            literal_expr_node = std::make_unique<vyn::BooleanLiteral>(literal_loc, true);
        } else if (token.type == vyn::TokenType::KEYWORD_FALSE) {
            literal_expr_node = std::make_unique<vyn::BooleanLiteral>(literal_loc, false);
        }
        // KEYWORD_NULL is not handled as ast.hpp has no NullLiteral. It will fall through to error.
        
        if (literal_expr_node) {
            return literal_expr_node;
        }
    }

    throw std::runtime_error("Unexpected token in pattern at " + location_to_string(loc) + ". Token: " + token.lexeme + " (" + vyn::token_type_to_string(token.type) + ")");
}

} // namespace vyn
