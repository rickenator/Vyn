#include "vyb/parser/parser.hpp" // For BaseParser and other parser components
#include "vyb/parser/ast.hpp"      // For AST node types like IntegerLiteral, etc.
#include "vyb/parser/token.hpp"    // For TokenType and Token
#include <stdexcept>               // For std::runtime_error
#include <algorithm> // Required for std::any_of, if used by match or other helpers
#include <functional> // Required for std::function
#include <vector> // Required for std::vector

namespace vyb {
// Forward-declare g_debug_codegen so the parser can share the same flag.
extern bool g_debug_codegen;
} // namespace vyb
#ifndef VYB_CDBG
#define VYB_CDBG if (vyb::g_debug_codegen) std::cerr
#endif

namespace vyb {

    // Parse an integer literal token's value, honoring `0x`/`0X` (base 16) and
    // `0b`/`0B` (base 2) prefixes; plain and leading-zero literals are decimal.
    static int64_t parseIntegerLiteralValue(const std::string& lit) {
        std::string s = lit;
        long base = 10;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s = s.substr(2);
        } else if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
            base = 2;
            s = s.substr(2);
        }
        return std::stoll(s, nullptr, base);
    }

    // Constructor
    ExpressionParser::ExpressionParser(const std::vector<token::Token>& tokens, size_t& pos, const std::string& file_path)
        : BaseParser(tokens, pos, file_path) {}

    // Public method to start parsing an expression
    vyb::ast::ExprPtr ExpressionParser::parse_expression() {
        DEBUG_PRINT("Entering parse_expression");
        DEBUG_TOKEN(peek());
        // This should call the highest precedence expression parser in your setup,
        // often assignment or logical OR. Assuming parse_assignment_expr() is the entry.
        auto expr = parse_assignment_expr();
        DEBUG_PRINT("Exiting parse_expression");
        if (expr) {
            DEBUG_PRINT("Successfully parsed expression.");
        } else {
            DEBUG_PRINT("Failed to parse expression or expression was null.");
        }
        DEBUG_TOKEN(peek()); // Token after parsing the whole expression
        return expr;
    }

    // Parses assignment expressions (e.g., x = 10, y += 5)
    vyb::ast::ExprPtr ExpressionParser::parse_assignment_expr() {
        vyb::ast::ExprPtr left = parse_logical_or_expr(); // Precedence: logical OR is higher than assignment

        // Check for assignment operators
        std::optional<token::Token> op;
        if ((op = match(TokenType::EQ)) || (op = match(TokenType::PLUSEQ)) || (op = match(TokenType::MINUSEQ)) ||
            (op = match(TokenType::MULTIPLYEQ)) || (op = match(TokenType::DIVEQ)) || (op = match(TokenType::MODEQ)) ||
            (op = match(TokenType::LSHIFTEQ)) || (op = match(TokenType::RSHIFTEQ)) ||
            (op = match(TokenType::BITWISEANDEQ)) || (op = match(TokenType::BITWISEOREQ)) || (op = match(TokenType::BITWISEXOREQ)) ||
            (op = match(TokenType::COLONEQ))) {
            // Build assignment node using the operator token
            token::Token op_token = op.value();
            SourceLocation op_loc = op_token.location;
            // Parse RHS
            vyb::ast::ExprPtr right = parse_expression();
            return std::make_unique<ast::AssignmentExpression>(op_loc, std::move(left), op_token, std::move(right));
        }
        return left; // Not an assignment
    }

    vyb::ast::ExprPtr ExpressionParser::parse_call_expression(vyb::ast::ExprPtr callee_expr) {
        std::vector<vyb::ast::ExprPtr> arguments;
        SourceLocation call_loc = previous_token().location;

        if (!match(TokenType::RPAREN)) {
            do {
                arguments.push_back(parse_expression());
            } while (match(TokenType::COMMA));
            expect(TokenType::RPAREN);
        }
        return std::make_unique<ast::CallExpression>(call_loc, std::move(callee_expr), std::move(arguments));
    }

    vyb::ast::ExprPtr ExpressionParser::parse_member_access(vyb::ast::ExprPtr object) {
        SourceLocation member_loc = peek().location;
        if (peek().type == TokenType::IDENTIFIER) {
            token::Token property_token = consume();
            auto property_identifier = std::make_unique<ast::Identifier>(property_token.location, property_token.lexeme);
            return std::make_unique<ast::MemberExpression>(member_loc, std::move(object), std::move(property_identifier), false /* not computed */);
        }
        // this->errors.push_back("Expected identifier for member access at " + location_to_string(member_loc));
        throw std::runtime_error("Expected identifier for member access at " + location_to_string(member_loc));
        return nullptr;
    }

    vyb::ast::ExprPtr ExpressionParser::parse_primary() {
        DEBUG_PRINT("Entering parse_primary");
        DEBUG_TOKEN(peek());
        SourceLocation loc = peek().location; // General location, might be overridden

        // Handle select expressions: select(expr) -> { pattern -> result, ... }
        if (match(TokenType::KEYWORD_SELECT)) {
            SourceLocation select_loc = peek().location;

            // Expect opening parenthesis for the select expression
            expect(TokenType::LPAREN, "Expected '(' after 'select'");

            // Parse the expression to match against
            vyb::ast::ExprPtr select_expr = parse_expression();
            if (!select_expr) {
                throw error(peek(), "Expected expression in select statement");
            }

            // Expect closing parenthesis
            expect(TokenType::RPAREN, "Expected ')' after select expression");

            // Expect arrow
            expect(TokenType::ARROW, "Expected '->' after select expression");

            // Expect opening brace
            expect(TokenType::LBRACE, "Expected '{' after '->' in select");

            // Parse select arms: pattern -> expression
            std::vector<std::pair<vyb::ast::ExprPtr, vyb::ast::ExprPtr>> cases;

            while (!check(TokenType::RBRACE) && !IsAtEnd()) {
                // Skip newlines between cases
                while (match(TokenType::NEWLINE)) {}

                if (check(TokenType::RBRACE)) break;

                // Parse pattern: '?', comparison pattern (e.g., >= 18), or literal
                vyb::ast::ExprPtr pattern;
                if (peek().type == TokenType::QUESTION_MARK) {
                    consume(); // consume '?'
                    pattern = nullptr; // Wildcard pattern
                } else if (peek().type == TokenType::LT || peek().type == TokenType::LTEQ ||
                           peek().type == TokenType::GT || peek().type == TokenType::GTEQ ||
                           peek().type == TokenType::EQEQ || peek().type == TokenType::NOTEQ) {
                    // Comparison pattern (e.g., >= 18, < 0, == 5)
                    auto op_token = consume(); // consume comparison operator
                    auto value = parse_primary();
                    if (!value) {
                        throw error(peek(), "Expected value after comparison operator in pattern");
                    }
                    pattern = std::make_unique<vyb::ast::ComparisonPattern>(
                        op_token.location, op_token, std::move(value)
                    );
                } else {
                    pattern = parse_primary();
                    if (!pattern) {
                        throw error(peek(), "Expected pattern in select arm");
                    }
                    // Enum variant pattern with payload: `Circle(r)`, `Rect(w, h)`.
                    // The primary parser returns the bare variant identifier and
                    // leaves the `( binding, ... )` group; assemble it into a
                    // ConstructionExpression that codegen reinterprets as a
                    // variant pattern, matching how the match parser handles it.
                    if (auto* pid = dynamic_cast<ast::Identifier*>(pattern.get())) {
                        if (peek().type == vyb::TokenType::LPAREN) {
                            consume(); // '('
                            std::vector<ast::ExprPtr> vbindings;
                            while (peek().type != vyb::TokenType::RPAREN && !IsAtEnd()) {
                                ast::ExprPtr b = parse_primary();
                                if (!b) {
                                    throw error(peek(), "Expected binding name in enum variant pattern");
                                }
                                vbindings.push_back(std::move(b));
                                if (!match(vyb::TokenType::COMMA)) break;
                            }
                            expect(vyb::TokenType::RPAREN, "Expected ')' after enum variant pattern bindings");
                            auto vtName = std::make_unique<ast::TypeName>(
                                pid->loc, std::make_unique<ast::Identifier>(pid->loc, pid->name));
                            pattern = std::make_unique<ast::ConstructionExpression>(
                                pid->loc, std::move(vtName), std::move(vbindings));
                        }
                    }
                }

                // Expect '->' (arrow)
                expect(TokenType::ARROW, "Expected '->' after select pattern");

                // Parse result: either a block or a naked expression
                vyb::ast::ExprPtr result;
                if (check(TokenType::LBRACE) && stmt_parser_) {
                    // Parse block statement and wrap it in a BlockExpression
                    auto block_stmt = stmt_parser_->parse_block();

                    // Check for trap clauses
                    std::vector<std::unique_ptr<vyb::ast::TrapClause>> trapClauses;
                    while (match(TokenType::KEYWORD_TRAP)) {
                        trapClauses.push_back(parse_trap_clause());
                    }

                    // Check for ensure clause
                    std::unique_ptr<vyb::ast::EnsureClause> ensureClause;
                    if (match(TokenType::KEYWORD_ENSURE)) {
                        ensureClause = parse_ensure_clause();
                    }

                    result = std::make_unique<vyb::ast::BlockExpression>(
                        block_stmt->loc, std::move(block_stmt),
                        std::move(trapClauses), std::move(ensureClause)
                    );
                } else {
                    // Parse naked expression
                    result = parse_expression();
                    if (!result) {
                        throw error(peek(), "Expected expression after '->' in select arm");
                    }
                }

                cases.emplace_back(std::move(pattern), std::move(result));

                // Optional comma
                match(TokenType::COMMA);

                // Skip trailing newlines
                while (match(TokenType::NEWLINE)) {}
            }

            expect(TokenType::RBRACE, "Expected '}' after select cases");

            return std::make_unique<vyb::ast::SelectExpression>(select_loc, std::move(select_expr), std::move(cases));
        }

        // Handle `match` used as a value-returning expression: the matched arm's
        // value becomes the expression result. Reuses the match statement parser.
        if (peek().type == TokenType::KEYWORD_MATCH) {
            if (!stmt_parser_) {
                throw error(peek(), "match expression requires a statement parser");
            }
            vyb::ast::StmtPtr matchStmt = stmt_parser_->parse_match();
            auto* ms = dynamic_cast<vyb::ast::MatchStatement*>(matchStmt.get());
            if (!ms) {
                throw error(peek(), "internal error: expected match statement");
            }
            std::unique_ptr<vyb::ast::MatchStatement> owned(
                static_cast<vyb::ast::MatchStatement*>(matchStmt.release()));
            return std::make_unique<vyb::ast::MatchExpression>(ms->loc, std::move(owned));
        }

        // Handle if statements as expressions (e.g. `let x = if cond { 1 } else { 0 }`)
        if (match(TokenType::KEYWORD_IF)) {
            expect(TokenType::LPAREN); // Expect \\\'(\\\' after \\\'if\\\'
            vyb::ast::ExprPtr condition = parse_expression(); // Condition
            expect(TokenType::RPAREN); // Expect \\\')\\\' after condition

            expect(TokenType::LBRACE); // Then block
            vyb::ast::ExprPtr then_branch = parse_expression(); // Expression inside then block
            expect(TokenType::RBRACE);

            vyb::ast::ExprPtr else_branch = nullptr;
            if (match(TokenType::KEYWORD_ELSE)) {
                expect(TokenType::LBRACE); // Else block
                else_branch = parse_expression(); // Expression inside else block
                expect(TokenType::RBRACE);
            } else {
                throw error(peek(), "Expected \'else\' branch for if-expression.");
            }
            return std::make_unique<ast::IfExpression>(loc, std::move(condition), std::move(then_branch), std::move(else_branch));
        }

        // Handle lambda expressions: |param1, param2| -> expression
        // Syntax: |x, y| -> x + y  or  |x| -> { return x * 2 }
        // A zero-arg lambda may be written `|| -> body`. The lexer collapses the
        // two `|` into a single OR token, so accept an OR token as an empty
        // parameter list only when followed by `->` (otherwise OR is the binary
        // logical-or operator handled further up the precedence chain).
        bool zeroArgOrLambda = check(TokenType::OR) && peekNext().type == TokenType::ARROW;
        if (check(TokenType::PIPE) || zeroArgOrLambda) {
            SourceLocation lambda_loc = peek().location;
            bool isAsync = false;  // TODO: Support async keyword before pipe

            bool isZeroArgOr = check(TokenType::OR);
            if (isZeroArgOr) {
                // The single `||` token is both opening and closing pipe with an
                // empty parameter list; nothing more to consume for params.
                consume();
            } else {
                consume(); // consume opening |
            }

            // Parse parameters
            std::vector<ast::FunctionParameter> params;

            if (!isZeroArgOr && !check(TokenType::PIPE)) {
                // Parse parameter list: x, y, z
                do {
                    if (check(TokenType::PIPE)) {
                        break; // End of parameters
                    }

                    if (!check(TokenType::IDENTIFIER)) {
                        throw error(peek(), "Expected parameter name in lambda expression");
                    }

                    token::Token param_token = consume();
                    auto param_name = std::make_unique<ast::Identifier>(
                        param_token.location, param_token.lexeme
                    );

                    // Optional type annotation: |x<Int>, y<String>|
                    ast::TypeNodePtr param_type = nullptr;
                    if (match(TokenType::LT)) {
                        // Create TypeParser to parse the type
                        TypeParser type_parser(tokens_, pos_, current_file_path_, *this);
                        param_type = type_parser.parse();

                        expect(TokenType::GT, "Expected '>' after parameter type in lambda");
                    }

                    params.emplace_back(std::move(param_name), std::move(param_type));

                } while (match(TokenType::COMMA));
            }

            if (!isZeroArgOr) {
                expect(TokenType::PIPE, "Expected closing '|' after lambda parameters");
            }

            // Expect arrow: ->
            expect(TokenType::ARROW, "Expected '->' after lambda parameters");

            // Parse body: either { block } or expression
            ast::ExprPtr body;
            if (check(TokenType::LBRACE) && stmt_parser_) {
                // Block body: |x| -> { result<Int> = x * 2; return result }
                auto block_stmt = stmt_parser_->parse_block();
                body = std::make_unique<ast::BlockExpression>(
                    block_stmt->loc,
                    std::move(block_stmt),
                    std::vector<std::unique_ptr<ast::TrapClause>>{},
                    nullptr // no ensure clause
                );
            } else {
                // Expression body: |x| -> x * 2
                body = parse_expression();
                if (!body) {
                    throw error(peek(), "Expected expression or block after '->' in lambda");
                }
            }

            // Return FunctionExpression (represents lambda)
            return std::make_unique<ast::FunctionExpression>(
                lambda_loc, std::move(params), std::move(body), isAsync
            );
        }

        // Try to parse TypeName(arguments) - Constructor Call
        // This requires being able to parse a TypeNode first.
        // We need a TypeParser instance here.
        // For now, we\\\'ll assume TypeParser is part of `this` parser or can be created.
        // This is a lookahead and backtrack mechanism.
        size_t initial_pos = pos_;

        // Before trying type parsing, check if this is likely a function call pattern
        bool skip_type_parsing = false;
        if (peek().type == TokenType::IDENTIFIER || peek().type == TokenType::KEYWORD_MY ||
            peek().type == TokenType::KEYWORD_THEIR || peek().type == TokenType::KEYWORD_OUR ||
            peek().type == TokenType::KEYWORD_MILD ||
            peek().type == TokenType::KEYWORD_BORROW || peek().type == TokenType::KEYWORD_VIEW) {
            std::string identifier_name = peek().lexeme;

            // Helper lambda to find next non-comment/non-newline token position
            auto find_next_token = [&](size_t start_pos) -> size_t {
                for (size_t i = start_pos; i < tokens_.size(); ++i) {
                    if (tokens_[i].type != TokenType::COMMENT &&
                        tokens_[i].type != TokenType::NEWLINE &&
                        tokens_[i].type != TokenType::INDENT &&
                        tokens_[i].type != TokenType::DEDENT) {
                        return i;
                    }
                }
                return tokens_.size(); // Not found
            };

            // Find the actual position of the identifier that peek() sees
            size_t identifier_pos = pos_;
            while (identifier_pos < tokens_.size() &&
                   (tokens_[identifier_pos].type == TokenType::COMMENT ||
                    tokens_[identifier_pos].type == TokenType::NEWLINE ||
                    tokens_[identifier_pos].type == TokenType::INDENT ||
                    tokens_[identifier_pos].type == TokenType::DEDENT)) {
                identifier_pos++;
            }

            // Check for simple function call: identifier()
            // Use the identifier_pos to find the next token after the identifier
            size_t after_identifier_pos = find_next_token(identifier_pos + 1);
            if (after_identifier_pos < tokens_.size() && tokens_[after_identifier_pos].type == TokenType::LPAREN) {
                skip_type_parsing = true;
            }
            // Find positions of next significant tokens for chained member access patterns
            size_t token_pos = find_next_token(identifier_pos + 1);  // Start after identifier

            // Check for chained member access patterns: obj.field.field.method()
            bool found_method_call_pattern = false;
            while (token_pos < tokens_.size()) {
                if (tokens_[token_pos].type == TokenType::DOT ||
                    tokens_[token_pos].type == TokenType::COLONCOLON) {
                    // Found DOT or ::, look for identifier after it
                    size_t next_identifier_pos = find_next_token(token_pos + 1);
                    if (next_identifier_pos >= tokens_.size() ||
                        tokens_[next_identifier_pos].type != TokenType::IDENTIFIER) {
                        break; // No identifier after DOT/::
                    }

                    // Check what comes after this identifier
                    size_t after_identifier_pos = find_next_token(next_identifier_pos + 1);
                    if (after_identifier_pos < tokens_.size() &&
                        tokens_[after_identifier_pos].type == TokenType::LPAREN) {
                        // Found identifier followed by LPAREN - this is a method call
                        found_method_call_pattern = true;
                        break;
                    } else if (after_identifier_pos < tokens_.size() &&
                               (tokens_[after_identifier_pos].type == TokenType::DOT ||
                                tokens_[after_identifier_pos].type == TokenType::COLONCOLON)) {
                        // Continue the chain
                        token_pos = after_identifier_pos;
                    } else {
                        // End of chain without method call
                        break;
                    }
                } else {
                    break;
                }
            }

            if (found_method_call_pattern) {
                skip_type_parsing = true;
            }
            // Also skip for known intrinsic functions even without parentheses
            else if (identifier_name == "println" || identifier_name == "print" || identifier_name == "debug" ||
                identifier_name == "error" || identifier_name == "warn" || identifier_name == "info" ||
                identifier_name == "lit" || identifier_name == "notype" || identifier_name == "bare" ||
                identifier_name == "deserial" || identifier_name == "my" || identifier_name == "their" ||
                identifier_name == "our" || identifier_name == "borrow" || identifier_name == "view") {
                #ifdef VERBOSE
                VYB_CDBG << "DEBUG: Skipping type parsing for intrinsic function: " << identifier_name << std::endl;
                #endif
                skip_type_parsing = true;
            }
        }

        if (!skip_type_parsing) {
            #ifdef VERBOSE
            VYB_CDBG << "DEBUG: Attempting type parsing for identifier: " << peek().lexeme << std::endl;
            #endif
            try {
                TypeParser type_parser(tokens_, pos_, current_file_path_, *this); // Pass *this for ExpressionParser reference
                ast::TypeNodePtr type_node = type_parser.parse(); // Call parse() instead of parse_type_annotation()

                if (type_node && match(TokenType::LPAREN)) { // Successfully parsed a type and found '('
                    std::vector<vyb::ast::ExprPtr> arguments;
                    SourceLocation call_loc = previous_token().location;
                    if (!match(TokenType::RPAREN)) {
                        do {
                            arguments.push_back(parse_expression());
                        } while (match(TokenType::COMMA));
                        expect(TokenType::RPAREN);
                    }
                    // Handle memory intrinsics parsed as type-like calls
                    if (auto tname = dynamic_cast<ast::TypeName*>(type_node.get())) {
                        std::string name = tname->identifier ? tname->identifier->name : std::string();
                        if (name == "loc") {
                            if (arguments.size() != 1) throw error(peek(), "loc() expects 1 argument");
                            return std::make_unique<ast::LocationExpression>(call_loc, std::move(arguments[0]));
                        } else if (name == "addr") {
                            if (arguments.size() != 1) throw error(peek(), "addr() expects 1 argument");
                            return std::make_unique<ast::AddrOfExpression>(call_loc, std::move(arguments[0]));
                        } else if (name == "at") {
                            if (arguments.size() != 1) throw error(peek(), "at() expects 1 argument");
                            return std::make_unique<ast::PointerDerefExpression>(call_loc, std::move(arguments[0]));
                        } else if (name == "from") {
                            if (tname->genericArgs.size() != 1) throw error(peek(), "from<T>() expects a single generic type argument");
                            if (arguments.size() != 1) throw error(peek(), "from<T>() expects 1 argument");
                            auto targetType = std::move(tname->genericArgs[0]);
                            return std::make_unique<ast::FromIntToLocExpression>(call_loc, std::move(arguments[0]), std::move(targetType));
                        }
                    }
                    // Fallback to regular construction
                    return std::make_unique<ast::ConstructionExpression>(type_node->loc, std::move(type_node), std::move(arguments));
                } else {
                    // Not a TypeName(...), backtrack
                    pos_ = initial_pos;
                }
            } catch (const std::runtime_error& e) {
                // Parsing type failed, or not followed by \\\'(\\\', backtrack
                pos_ = initial_pos;
                // Log or handle error if needed, or just proceed to other parsing rules
            }
        }
        // If it wasn\\\'t a TypeName(...), reset pos_ and try other primary expression forms.
        // Ensure pos_ is correctly managed by TypeParser or reset it manually.
        // The TypeParser must not consume tokens if it fails to parse a complete type for this to work well.


        // Try to parse [Type; Size]() - Array Initialization
        if (peek().type == TokenType::LBRACKET) { // Check before consuming for ArrayInit
            size_t before_array_init_pos = pos_;
            token::Token lbracket_peek = peek(); // Peek for location
            consume(); // Consume LBRACKET for ArrayInit attempt
            DEBUG_PRINT("Attempting to parse ArrayInitialization: [Type; Size]()");
            DEBUG_TOKEN(lbracket_peek); // Log the LBRACKET that was consumed

            try {
                TypeParser type_parser_for_array(tokens_, pos_, current_file_path_, *this); // Pass *this for ExpressionParser reference
                ast::TypeNodePtr element_type = nullptr;

                // Catch any exceptions from the type parser and backtrack
                try {
                    element_type = type_parser_for_array.parse(); // Call parse()
                } catch (const std::runtime_error& e) {
                    // Failed to parse type - this might be a list comprehension or array literal
                    pos_ = before_array_init_pos;
                    goto regular_array_literal; // Continue with normal array literal parsing
                }

                if (element_type && match(TokenType::SEMICOLON)) {
                    ast::ExprPtr size_expr = nullptr;

                    // Try to parse size expression
                    try {
                        size_expr = parse_expression();
                    } catch (const std::runtime_error& e) {
                        // Failed to parse size expression
                        pos_ = before_array_init_pos;
                        goto regular_array_literal;
                    }

                    if (!size_expr) {
                        pos_ = before_array_init_pos;
                        goto regular_array_literal;
                    }

                    try {
                        expect(TokenType::RBRACKET);
                    } catch (const std::runtime_error& e) {
                        // Failed to find RBRACKET
                        pos_ = before_array_init_pos;
                        goto regular_array_literal;
                    }

                    if (match(TokenType::LPAREN)) {
                        try {
                            expect(TokenType::RPAREN);
                            // It's an ArrayInitializationExpression
                            return std::make_unique<ast::ArrayInitializationExpression>(loc, std::move(element_type), std::move(size_expr));
                        } catch (const std::runtime_error& e) {
                            // Failed to find RPAREN
                            pos_ = before_array_init_pos;
                            goto regular_array_literal;
                        }
                    }
                    // If not LPAREN RPAREN, it's something else, backtrack
                    pos_ = before_array_init_pos; // Backtrack fully
                } else {
                    // Not [Type; Size], backtrack to before LBRACKET was consumed
                    pos_ = before_array_init_pos;
                }
            } catch (const std::runtime_error& e) {
                // Failed to parse type or other parts, backtrack
                pos_ = before_array_init_pos;
            }
            // If we backtracked, it might be a regular array literal, fall through to that logic.
            // Ensure pos_ is reset correctly if it was an array init attempt that failed.
            // The LBRACKET for array/list literal parsing is matched again below if this path fails and backtracks.
        }

regular_array_literal:
        // Handle 'from<Type>(expr)' syntax, Typed Struct Literals, and Plain Identifiers
        if (peek().type == TokenType::IDENTIFIER || peek().type == TokenType::KEYWORD_FROM ||
            peek().type == TokenType::KEYWORD_MY ||
            peek().type == TokenType::KEYWORD_THEIR || peek().type == TokenType::KEYWORD_OUR ||
            peek().type == TokenType::KEYWORD_MILD ||
            peek().type == TokenType::KEYWORD_BORROW || peek().type == TokenType::KEYWORD_VIEW) {
            token::Token current_id_token = peek(); // Peek, don't consume yet

            if (current_id_token.lexeme == "from") {
                // Potential from<Type>(expr)
                // Lookahead: from < Type > ( expr )
                // Check for '<' after 'from'
                if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].type == TokenType::LT) { // Changed LESS_THAN to LT
                    consume(); // Consume 'from'
                    SourceLocation from_loc = current_id_token.location;

                    expect(TokenType::LT); // Changed LESS_THAN to LT, Consume '<'

                    TypeParser type_parser(tokens_, pos_, current_file_path_, *this);
                    ast::TypeNodePtr target_type = type_parser.parse();
                    if (!target_type) {
                        throw error(peek(), "Expected type specification after 'from<'.");
                    }

                    expect(TokenType::GT); // Changed GREATER_THAN to GT, Consume '>'
                    expect(TokenType::LPAREN);      // Consume '('

                    vyb::ast::ExprPtr address_expr = parse_expression();

                    expect(TokenType::RPAREN);      // Consume ')'

                    return std::make_unique<ast::FromIntToLocExpression>(from_loc, std::move(address_expr), std::move(target_type));
                }
                // If "from" is not followed by "<", it will be treated like any other identifier below
                // (either as a typed struct name like "from { ... }" or a plain variable "from").
            }

            // Typed Struct Literal: Identifier { ... } or Identifier<T, ...> { ... } or Plain Identifier
            bool is_typed_struct = false;
            bool has_generic_args = false;

            // Locate the identifier's real token index. peek() may sit on a
            // leading NEWLINE/INDENT (e.g., a match-arm pattern on its own line),
            // so inspect the first significant token AFTER the identifier.
            auto next_significant = [&](size_t cstart) -> size_t {
                for (size_t i = cstart; i < tokens_.size(); ++i) {
                    if (tokens_[i].type != TokenType::COMMENT &&
                        tokens_[i].type != TokenType::NEWLINE &&
                        tokens_[i].type != TokenType::INDENT &&
                        tokens_[i].type != TokenType::DEDENT) {
                        return i;
                    }
                }
                return tokens_.size();
            };
            size_t ident_idx = pos_;
            while (ident_idx < tokens_.size() &&
                   (tokens_[ident_idx].type == TokenType::COMMENT ||
                    tokens_[ident_idx].type == TokenType::NEWLINE ||
                    tokens_[ident_idx].type == TokenType::INDENT ||
                    tokens_[ident_idx].type == TokenType::DEDENT)) {
                ident_idx++;
            }

            // Generic enum construction: `Ident<Args>::Variant(...)` — e.g.
            // `Box<Int>::Value(42)`. If the identifier is followed by a generic
            // argument list whose matching `>` is followed by `::`, parse the
            // args into a GenericInstantiationExpression and let the postfix
            // `::` member-access handler attach the variant name and call.
            {
                bool is_generic_scoped = false;
                size_t scan_pos = pos_;
                while (scan_pos < tokens_.size() &&
                       (tokens_[scan_pos].type == TokenType::COMMENT ||
                        tokens_[scan_pos].type == TokenType::NEWLINE ||
                        tokens_[scan_pos].type == TokenType::INDENT ||
                        tokens_[scan_pos].type == TokenType::DEDENT)) {
                    scan_pos++;
                }
                if (scan_pos + 1 < tokens_.size() && tokens_[scan_pos + 1].type == TokenType::LT) {
                    int adepth = 0;
                    size_t g = scan_pos + 1;
                    bool closed = false;
                    while (g < tokens_.size()) {
                        if (tokens_[g].type == TokenType::LT) adepth++;
                        else if (tokens_[g].type == TokenType::GT) {
                            adepth--;
                            if (adepth == 0) { closed = true; break; }
                        }
                        g++;
                    }
                    if (closed) {
                        size_t after_gt = next_significant(g + 1);
                        if (after_gt < tokens_.size() &&
                            tokens_[after_gt].type == TokenType::COLONCOLON) {
                            is_generic_scoped = true;
                        }
                    }
                }
                if (is_generic_scoped) {
                    SourceLocation id_loc = peek().location;
                    auto base = std::make_unique<ast::Identifier>(id_loc, peek().lexeme);
                    consume(); // Consume the identifier
                    SourceLocation lt_loc = peek().location;
                    expect(TokenType::LT);
                    std::vector<ast::TypeNodePtr> args;
                    while (!check(TokenType::GT) && !IsAtEnd()) {
                        TypeParser tp(tokens_, pos_, current_file_path_, *this);
                        ast::TypeNodePtr arg = tp.parse();
                        if (!arg) {
                            throw error(peek(), "Expected type argument in generic enum construction");
                        }
                        args.push_back(std::move(arg));
                        if (!match(TokenType::COMMA)) break;
                    }
                    SourceLocation gt_loc = peek().location;
                    expect(TokenType::GT);
                    return std::make_unique<ast::GenericInstantiationExpression>(
                        id_loc, std::move(base), std::move(args), lt_loc, gt_loc);
                }
            }

            // Look ahead to check for Type { or Type<Args> { patterns
            if (ident_idx + 1 < tokens_.size()) {
                size_t after_ident = next_significant(ident_idx + 1);
                if (after_ident < tokens_.size()) {
                    if (tokens_[after_ident].type == TokenType::LBRACE) {
                        // Simple case: Type {
                        is_typed_struct = true;
                    } else if (tokens_[after_ident].type == TokenType::LT) {
                        // Potential generic type: Type<...> {
                        // Scan ahead to find matching > followed by {
                        int angle_depth = 0;
                        size_t scan_pos = after_ident;
                        while (scan_pos < tokens_.size()) {
                            if (tokens_[scan_pos].type == TokenType::LT) {
                                angle_depth++;
                            } else if (tokens_[scan_pos].type == TokenType::GT) {
                                angle_depth--;
                                if (angle_depth == 0) {
                                    // Found matching >, check next token for {
                                    size_t after_gt = next_significant(scan_pos + 1);
                                    if (after_gt < tokens_.size() &&
                                        tokens_[after_gt].type == TokenType::LBRACE) {
                                        is_typed_struct = true;
                                        has_generic_args = true;
                                    }
                                    break;
                                }
                            } else if (angle_depth == 0) {
                                // Hit something else at same level, not a generic type literal
                                break;
                            }
                            scan_pos++;
                        }
                    }
                }
            }

            if (is_typed_struct) {
                token::Token type_name_token = consume(); // Consume IDENTIFIER
                SourceLocation struct_loc = type_name_token.location;

                // Build the type path
                auto type_identifier_node = std::make_unique<ast::Identifier>(type_name_token.location, type_name_token.lexeme);
                std::vector<ast::TypeNodePtr> generic_args;

                if (has_generic_args) {
                    // Parse generic arguments: <Type1, Type2, ...>
                    expect(TokenType::LT);

                    while (true) {
                        // Parse each type argument recursively as a simple type
                        // For now, support simple identifiers and nested generics
                        if (!check(TokenType::IDENTIFIER)) {
                            throw error(peek(), "Expected type identifier in generic arguments");
                        }

                        token::Token arg_token = consume();
                        auto arg_id = std::make_unique<ast::Identifier>(arg_token.location, arg_token.lexeme);

                        // Check for nested generics (e.g., Box<Vec<Int>>)
                        std::vector<ast::TypeNodePtr> nested_args;
                        if (match(TokenType::LT)) {
                            // Recursively parse nested generic args
                            int nested_depth = 1;
                            size_t start_pos = pos_ - 1;

                            while (nested_depth > 0 && pos_ < tokens_.size()) {
                                if (peek().type == TokenType::LT) {
                                    nested_depth++;
                                    consume();
                                } else if (peek().type == TokenType::GT) {
                                    nested_depth--;
                                    consume();
                                } else {
                                    consume();
                                }
                            }
                            // For now, we'll skip nested parsing and just create a simple type
                            // This can be enhanced later for full nested generic support
                        }

                        generic_args.push_back(std::make_unique<ast::TypeName>(arg_token.location, std::move(arg_id), std::move(nested_args)));

                        if (match(TokenType::COMMA)) {
                            continue;
                        } else {
                            break;
                        }
                    }

                    expect(TokenType::GT);
                }

                auto type_path_node = std::make_unique<ast::TypeName>(struct_loc, std::move(type_identifier_node), std::move(generic_args));

                expect(TokenType::LBRACE); // Consumes LBRACE

                std::vector<ast::ObjectProperty> properties;
                if (!check(TokenType::RBRACE)) {
                    while (true) {
                        if (peek().type != TokenType::IDENTIFIER) {
                            throw error(peek(), "Expected identifier for struct field name.");
                        }
                        token::Token key_token = consume();
                        auto key_identifier = std::make_unique<ast::Identifier>(key_token.location, key_token.lexeme);

                        vyb::ast::ExprPtr value = nullptr;
                        if (match(TokenType::COLON) || match(TokenType::EQ)) {
                            if (check(TokenType::COMMA) || check(TokenType::RBRACE)) {
                                throw error(peek(), "Expected expression for struct field value after \':\' or \'=\'.");
                            }
                            value = parse_expression();
                        } else {
                            // Shorthand: { fieldName }
                        }
                        properties.emplace_back(key_token.location, std::move(key_identifier), std::move(value));

                        if (match(TokenType::COMMA)) {
                            if (check(TokenType::RBRACE)) {
                                break;
                            }
                            if (peek().type != TokenType::IDENTIFIER) {
                                throw error(peek(), "Expected identifier for struct field name after comma.");
                            }
                        } else {
                            break;
                        }
                    }
                }
                expect(TokenType::RBRACE);
                return std::make_unique<ast::ObjectLiteral>(struct_loc, std::move(type_path_node), std::move(properties));
            } else {
                // Plain Identifier (including ownership keywords treated as identifiers)
                if ((current_id_token.type == TokenType::KEYWORD_VIEW || current_id_token.type == TokenType::KEYWORD_BORROW)) {
                    size_t next_pos = pos_ + 1;
                    while (next_pos < tokens_.size() &&
                           (tokens_[next_pos].type == TokenType::COMMENT ||
                            tokens_[next_pos].type == TokenType::NEWLINE ||
                            tokens_[next_pos].type == TokenType::INDENT ||
                            tokens_[next_pos].type == TokenType::DEDENT)) {
                        next_pos++;
                    }
                    if (next_pos >= tokens_.size() || tokens_[next_pos].type != TokenType::LPAREN) {
                        throw error(current_id_token,
                            "Prefix '" + current_id_token.lexeme + " expr' syntax is no longer supported. Use '" +
                            current_id_token.lexeme + "(expr)' instead.");
                    }
                }
                token::Token id_token = consume(); // Consume IDENTIFIER or ownership keyword
                return std::make_unique<ast::Identifier>(id_token.location, id_token.lexeme);
            }
        }

        if (is_literal(peek().type)) {
            DEBUG_PRINT("Parsing literal in parse_primary");
            auto lit_expr = parse_literal();
            DEBUG_PRINT("Exiting literal parsing in parse_primary");
            DEBUG_TOKEN(peek());
            return lit_expr;
        }

        if (match(TokenType::LPAREN)) {
            DEBUG_PRINT("Parsing grouped expression or tuple literal (LPAREN)");
            DEBUG_TOKEN(previous_token());
            SourceLocation lparen_loc = previous_token().location;

            // Check for empty tuple ()
            if (check(TokenType::RPAREN)) {
                consume(); // consume RPAREN
                // Empty tuple - create sequence with no elements
                return std::make_unique<vyb::ast::SequenceExpression>(lparen_loc, std::vector<vyb::ast::ExprPtr>{});
            }

            // Parse first expression
            vyb::ast::ExprPtr first_expr = parse_expression();

            // Check if this is a tuple (has comma) or just a grouped expression
            if (check(TokenType::COMMA)) {
                // This is a tuple literal - collect all expressions
                std::vector<vyb::ast::ExprPtr> elements;
                elements.push_back(std::move(first_expr));

                while (match(TokenType::COMMA)) {
                    // Allow trailing comma
                    if (check(TokenType::RPAREN)) {
                        break;
                    }
                    elements.push_back(parse_expression());
                }

                expect(TokenType::RPAREN);
                DEBUG_PRINT("Parsed tuple literal with " + std::to_string(elements.size()) + " elements");
                return std::make_unique<vyb::ast::SequenceExpression>(lparen_loc, std::move(elements));
            } else {
                // Just a grouped expression
                expect(TokenType::RPAREN);
                DEBUG_PRINT("Exiting grouped expression (RPAREN)");
                DEBUG_TOKEN(previous_token());
                return first_expr;
            }
        }

        // Array literals and list comprehensions: [element1, element2] or [expr for var in iterable ...]
        if (match(TokenType::LBRACKET)) { // This LBRACKET is for array/list literals
            DEBUG_PRINT("parse_primary: Matched LBRACKET for array/list literal.");
            DEBUG_TOKEN(previous_token()); // The LBRACKET token

            SourceLocation array_loc = previous_token().location;

            // Check for empty array
            if (check(TokenType::RBRACKET)) {
                DEBUG_PRINT("parse_primary: Parsing empty array literal []");
                consume(); // consume RBRACKET
                DEBUG_PRINT("parse_primary: Consumed RBRACKET for empty array.");
                DEBUG_TOKEN(previous_token());
                return std::make_unique<ast::ArrayLiteral>(array_loc, std::vector<vyb::ast::ExprPtr>{});
            }

            // --- Lookahead to detect list comprehension by scanning tokens ---
            size_t snapshot_pos = pos_;
            bool will_comprehension = false;
            {
                size_t scan_pos = snapshot_pos;
                int bracket_nest = 1; // starting after consuming '['
                while (scan_pos < tokens_.size()) {
                    const auto& tk = tokens_[scan_pos];
                    if (tk.type == TokenType::LBRACKET) {
                        bracket_nest++;
                    } else if (tk.type == TokenType::RBRACKET) {
                        bracket_nest--;
                        if (bracket_nest == 0) break;
                    } else if (bracket_nest == 1 && tk.type == TokenType::KEYWORD_FOR) {
                        will_comprehension = true;
                        break;
                    }
                    scan_pos++;
                }
            }
            pos_ = snapshot_pos;

            // --- Actual parsing of first expression ---
            DEBUG_PRINT("parse_primary: Before parsing first_expr in array/list. Current token:");
            DEBUG_TOKEN(peek());
            vyb::ast::ExprPtr first_expr = parse_expression();
            DEBUG_PRINT("parse_primary: After parsing first_expr in array/list. Current token:");
            DEBUG_TOKEN(peek());

            // List comprehension if lookahead or actual FOR match
            if (will_comprehension || check(TokenType::KEYWORD_FOR)) {
                // Consume the 'for' keyword
                token::Token for_token = consume();
                DEBUG_PRINT("parse_primary: Matched KEYWORD_FOR, parsing list comprehension.");
                DEBUG_TOKEN(for_token); // The FOR token

                if (!check(TokenType::IDENTIFIER)) {
                    throw error(peek(), "Expected identifier after 'for' in list comprehension.");
                }
                token::Token var_token = consume();
                DEBUG_PRINT("parse_primary: Consumed loop variable.");
                DEBUG_TOKEN(var_token);
                auto loop_var = std::make_unique<ast::Identifier>(var_token.location, var_token.lexeme);

                if (!match(TokenType::KEYWORD_IN)) {
                    throw error(peek(), "Expected 'in' after loop variable in list comprehension.");
                }
                DEBUG_PRINT("parse_primary: Matched KEYWORD_IN.");
                DEBUG_TOKEN(previous_token()); // The IN token

                DEBUG_PRINT("parse_primary: Before parsing iterable_expr in list comprehension. Current token:");
                DEBUG_TOKEN(peek());
                vyb::ast::ExprPtr iterable_expr = parse_expression();
                DEBUG_PRINT("parse_primary: After parsing iterable_expr in list comprehension. Current token:");
                DEBUG_TOKEN(peek());

                vyb::ast::ExprPtr cond_expr = nullptr;
                if (match(TokenType::KEYWORD_IF)) {
                    DEBUG_PRINT("parse_primary: Matched KEYWORD_IF for condition.");
                    DEBUG_TOKEN(previous_token()); // The IF token
                    DEBUG_PRINT("parse_primary: Before parsing cond_expr in list comprehension. Current token:");
                    DEBUG_TOKEN(peek());
                    cond_expr = parse_expression();
                    DEBUG_PRINT("parse_primary: After parsing cond_expr in list comprehension. Current token:");
                    DEBUG_TOKEN(peek());
                }

                DEBUG_PRINT("parse_primary: Before expect(RBRACKET) for list comprehension. Current token:");
                DEBUG_TOKEN(peek());
                expect(TokenType::RBRACKET);
                DEBUG_PRINT("parse_primary: Consumed RBRACKET for list comprehension.");
                DEBUG_TOKEN(previous_token());
                return std::make_unique<ast::ListComprehension>(array_loc, std::move(first_expr), std::move(loop_var), std::move(iterable_expr), std::move(cond_expr));
            } else {
                DEBUG_PRINT("parse_primary: Parsing regular array literal (after first element).");
                std::vector<vyb::ast::ExprPtr> elements;
                elements.push_back(std::move(first_expr)); // Add the already parsed first element

                while (match(TokenType::COMMA)) {
                    DEBUG_PRINT("parse_primary: Matched COMMA in array literal.");
                    DEBUG_TOKEN(previous_token()); // The COMMA token
                    if (check(TokenType::RBRACKET)) {
                        DEBUG_PRINT("parse_primary: Trailing comma detected in array literal.");
                        break;
                    }
                    DEBUG_PRINT("parse_primary: Before parsing next element in array literal. Current token:");
                    DEBUG_TOKEN(peek());
                    elements.push_back(parse_expression()); // Parse subsequent elements
                    DEBUG_PRINT("parse_primary: After parsing next element in array literal. Current token:");
                    DEBUG_TOKEN(peek());
                }
                DEBUG_PRINT("parse_primary: Before expect(RBRACKET) for array literal. Current token:");
                DEBUG_TOKEN(peek());
                expect(TokenType::RBRACKET);
                DEBUG_PRINT("parse_primary: Consumed RBRACKET for array literal.");
                DEBUG_TOKEN(previous_token());
                return std::make_unique<ast::ArrayLiteral>(array_loc, std::move(elements));
            }
        }

        // Block expressions with trap/ensure: { statements... } trap (...) -> {...} ensure -> {...}
        // Disambiguate from object literals by looking ahead one token
        if (check(TokenType::LBRACE) && stmt_parser_) {
            // Lookahead to distinguish block vs object literal
            size_t saved_pos = pos_;
            consume(); // consume LBRACE to look at next token

            bool is_block = false;
            TokenType next_type = peek().type;

            // Check if first token suggests a block (statement keyword) vs object literal (identifier for field)
            if (next_type == TokenType::RBRACE) {
                // Empty braces - treat as object literal {}
                is_block = false;
            } else if (next_type == TokenType::KEYWORD_FAIL ||
                       next_type == TokenType::KEYWORD_PANIC ||
                       next_type == TokenType::KEYWORD_RETHROW ||
                       next_type == TokenType::KEYWORD_RETURN ||
                       next_type == TokenType::KEYWORD_IF ||
                       next_type == TokenType::KEYWORD_WHILE ||
                       next_type == TokenType::KEYWORD_FOR ||
                       next_type == TokenType::KEYWORD_BREAK ||
                       next_type == TokenType::KEYWORD_CONTINUE ||
                       next_type == TokenType::KEYWORD_PASS ||
                       next_type == TokenType::KEYWORD_DEFER ||
                       next_type == TokenType::KEYWORD_AWAIT ||
                       next_type == TokenType::KEYWORD_MATCH ||
                       next_type == TokenType::KEYWORD_TRY) {
                // Starts with statement keyword - definitely a block
                is_block = true;
            } else if (next_type == TokenType::IDENTIFIER) {
                // Need more lookahead - check what follows the identifier
                consume(); // consume IDENTIFIER
                TokenType after_ident = peek().type;
                if (after_ident == TokenType::COLON || after_ident == TokenType::EQ) {
                    // identifier: or identifier = means object literal
                    is_block = false;
                } else if (after_ident == TokenType::COMMA || after_ident == TokenType::RBRACE) {
                    // identifier, or identifier} means object literal (shorthand)
                    is_block = false;
                } else if (after_ident == TokenType::LT) {
                    // identifier< could be variable declaration with type
                    is_block = true;
                } else if (after_ident == TokenType::LPAREN) {
                    // identifier( is a function call - statement
                    is_block = true;
                } else {
                    // Default to block for other cases
                    is_block = true;
                }
            } else {
                // Non-identifier token after brace (number, bool, string, operator, etc.)
                // Cannot be a struct field name, so treat as block
                is_block = true;
            }

            // Restore position
            pos_ = saved_pos;

            if (is_block) {
                // Parse as block expression
                auto block_stmt = stmt_parser_->parse_block();

                // Check for trap clauses
                std::vector<std::unique_ptr<vyb::ast::TrapClause>> trapClauses;
                while (match(TokenType::KEYWORD_TRAP)) {
                    trapClauses.push_back(parse_trap_clause());
                }

                // Check for ensure clause
                std::unique_ptr<vyb::ast::EnsureClause> ensureClause;
                if (match(TokenType::KEYWORD_ENSURE)) {
                    ensureClause = parse_ensure_clause();
                }

                return std::make_unique<vyb::ast::BlockExpression>(
                    block_stmt->loc, std::move(block_stmt),
                    std::move(trapClauses), std::move(ensureClause)
                );
            }
            // Otherwise fall through to object literal parsing
        }

        // Anonymous Struct literals: { field1: value1, field2 }
        if (match(TokenType::LBRACE)) {
            SourceLocation struct_loc = previous_token().location;
            std::vector<ast::ObjectProperty> properties;

            if (!check(TokenType::RBRACE)) { // If not empty struct {}
                while (true) {
                    if (peek().type != TokenType::IDENTIFIER) {
                         throw error(peek(), "Expected identifier for struct field name.");
                    }
                    token::Token key_token = consume();
                    auto key_identifier = std::make_unique<ast::Identifier>(key_token.location, key_token.lexeme);

                    vyb::ast::ExprPtr value = nullptr;

                    if (match(TokenType::COLON) || match(TokenType::EQ)) {
                        // Check for missing value after ':' or '='
                        if (check(TokenType::COMMA) || check(TokenType::RBRACE)) {
                            throw error(peek(), "Expected expression for struct field value after ':' or '='.");
                        }
                        value = parse_expression();
                    } else {
                        // Shorthand: { fieldName }
                        // AST.md: value is optional (null).
                    }
                    properties.emplace_back(key_token.location, std::move(key_identifier), std::move(value));

                    if (match(TokenType::COMMA)) {
                        if (check(TokenType::RBRACE)) { // Trailing comma: { a:1, }
                            break;
                        }
                        // Comma consumed, expect another property. If next is not IDENTIFIER, it's an error.
                        if (peek().type != TokenType::IDENTIFIER) {
                             throw error(peek(), "Expected identifier for struct field name after comma.");
                        }
                    } else {
                        break; // No comma, so this must be the last property
                    }
                }
            }
            expect(TokenType::RBRACE);
            return std::make_unique<ast::ObjectLiteral>(struct_loc, nullptr, std::move(properties));
        }

        throw error(peek(), "Expected primary expression.");
    }

    // Parses literal expressions (integers, floats, strings, booleans, null)
    vyb::ast::ExprPtr ExpressionParser::parse_literal() {
        DEBUG_PRINT("Entering parse_literal");
        DEBUG_TOKEN(peek());
        token::Token current_token = peek(); // Keep for location and lexeme, consume advances current_token internally
        switch (current_token.type) {
            case TokenType::INT_LITERAL: {
                consume();
                return std::make_unique<ast::IntegerLiteral>(
                    current_token.location, parseIntegerLiteralValue(current_token.lexeme));
            }
            case TokenType::FLOAT_LITERAL: {
                consume();
                return std::make_unique<ast::FloatLiteral>(current_token.location, std::stod(current_token.lexeme));
            }
            case TokenType::STRING_LITERAL: {
                consume();
                return std::make_unique<ast::StringLiteral>(current_token.location, current_token.lexeme);
            }
            case TokenType::KEYWORD_TRUE: {
                consume();
                return std::make_unique<ast::BooleanLiteral>(current_token.location, true);
            }
            case TokenType::KEYWORD_FALSE: {
                consume();
                return std::make_unique<ast::BooleanLiteral>(current_token.location, false);
            }
            case TokenType::KEYWORD_NULL:
            case TokenType::KEYWORD_NIL: {
                consume();
                return std::make_unique<ast::NilLiteral>(current_token.location);
            }
            default:
                // this->errors.push_back("Unexpected token in parse_literal: " + token_type_to_string(current_token.type) + " at " + location_to_string(current_token.location));
                throw std::runtime_error("Unexpected token in parse_literal: " + token_type_to_string(current_token.type) + " at " + location_to_string(current_token.location));
                return nullptr;
        }
        DEBUG_PRINT("Exiting parse_literal (should have returned or thrown)");
        return nullptr; // Should be unreachable if all cases return/throw
    }

    vyb::ast::ExprPtr ExpressionParser::parse_atom() {
        return parse_primary();
    }

    vyb::ast::ExprPtr ExpressionParser::parse_binary_expression(std::function<vyb::ast::ExprPtr()> parse_higher_precedence, const std::vector<TokenType>& operators) {
        DEBUG_PRINT("Entering parse_binary_expression");
        DEBUG_TOKEN(peek());
        vyb::ast::ExprPtr left = parse_higher_precedence();
        DEBUG_PRINT("parse_binary_expression: After parsing left operand. Current token:");
        DEBUG_TOKEN(peek());

        while (true) {
            bool operator_found = false;
            TokenType matched_op_type = TokenType::ILLEGAL; // Placeholder

            for (TokenType op_type : operators) {
                if (check(op_type)) { // Just check, don't consume
                    operator_found = true;
                    matched_op_type = op_type;
                    break;
                }
            }

            if (operator_found) {
                token::Token op_token = consume(); // Consume the operator (it must be matched_op_type)
                DEBUG_PRINT("parse_binary_expression: Matched operator.");
                DEBUG_TOKEN(op_token);

                DEBUG_PRINT("parse_binary_expression: Before parsing right operand. Current token:");
                DEBUG_TOKEN(peek());
                vyb::ast::ExprPtr right = parse_higher_precedence();
                DEBUG_PRINT("parse_binary_expression: After parsing right operand. Current token:");
                DEBUG_TOKEN(peek());

                // Special handling for range operators
                if (matched_op_type == TokenType::DOTDOT) {
                    left = std::make_unique<ast::RangeExpression>(op_token.location, std::move(left), std::move(right));
                } else {
                    left = std::make_unique<ast::BinaryExpression>(op_token.location, std::move(left), op_token, std::move(right));
                }
            } else {
                DEBUG_PRINT("parse_binary_expression: No more matching operators found.");
                break; // No matching operator found
            }
        }
        DEBUG_PRINT("Exiting parse_binary_expression. Current token:");
        DEBUG_TOKEN(peek());
        return left;
    }

    vyb::ast::ExprPtr ExpressionParser::parse_logical_or_expr() {
        return parse_binary_expression([this]() { return parse_logical_and_expr(); }, {TokenType::OR});
    }

    vyb::ast::ExprPtr ExpressionParser::parse_logical_and_expr() {
        return parse_binary_expression([this]() { return parse_bitwise_or_expr(); }, {TokenType::AND});
    }

    vyb::ast::ExprPtr ExpressionParser::parse_bitwise_or_expr() {
        return parse_binary_expression([this]() { return parse_bitwise_xor_expr(); }, {TokenType::PIPE});
    }

    vyb::ast::ExprPtr ExpressionParser::parse_bitwise_xor_expr() {
        return parse_binary_expression([this]() { return parse_bitwise_and_expr(); }, {TokenType::CARET});
    }

    vyb::ast::ExprPtr ExpressionParser::parse_bitwise_and_expr() {
        return parse_binary_expression([this]() { return parse_equality_expr(); }, {TokenType::AMPERSAND});
    }

    vyb::ast::ExprPtr ExpressionParser::parse_equality_expr() {
        DEBUG_PRINT("Entering parse_equality_expr");
        DEBUG_TOKEN(peek());
        auto expr = parse_binary_expression([this]() {
            DEBUG_PRINT("parse_equality_expr: calling nested parse_relational_expr");
            DEBUG_TOKEN(peek());
            auto inner_expr = parse_relational_expr();
            DEBUG_PRINT("parse_equality_expr: returned from nested parse_relational_expr");
            DEBUG_TOKEN(peek());
            return inner_expr;
        }, {TokenType::EQEQ, TokenType::NOTEQ});
        DEBUG_PRINT("Exiting parse_equality_expr");
        DEBUG_TOKEN(peek());
        return expr;
    }

    vyb::ast::ExprPtr ExpressionParser::parse_relational_expr() {
        DEBUG_PRINT("Entering parse_relational_expr");
        DEBUG_TOKEN(peek());
        auto expr = parse_binary_expression([this]() {
            DEBUG_PRINT("parse_relational_expr: calling nested parse_shift_expr");
            DEBUG_TOKEN(peek());
            auto inner_expr = parse_shift_expr();
            DEBUG_PRINT("parse_relational_expr: returned from nested parse_shift_expr");
            DEBUG_TOKEN(peek());
            return inner_expr;
        }, {TokenType::LT, TokenType::LTEQ, TokenType::GT, TokenType::GTEQ, TokenType::DOTDOT});
        DEBUG_PRINT("Exiting parse_relational_expr");
        DEBUG_TOKEN(peek());
        return expr;
    }

    vyb::ast::ExprPtr ExpressionParser::parse_shift_expr() {
        // `<<`/`>>` are lexed as two adjacent `<`/`>` tokens so that nested generic
        // closes (which use the same `>>` spelling) stay unambiguous: the type
        // parser sees two `>` closes, while here, when an operator is expected,
        // an adjacent pair is interpreted as a shift.
        vyb::ast::ExprPtr left = parse_additive_expr();
        while (true) {
            vyb::token::Token op_token = previous_token();
            bool isShift = false;
            if (check(TokenType::LSHIFT)) {
                op_token = consume();
                isShift = true;
            } else if (check(TokenType::RSHIFT)) {
                op_token = consume();
                isShift = true;
            } else if (check(TokenType::LT) && peekNext().type == TokenType::LT) {
                const vyb::token::Token& first = peek();
                consume();
                consume();
                op_token = vyb::token::Token(TokenType::LSHIFT, "<<", first.location);
                isShift = true;
            } else if (check(TokenType::GT) && peekNext().type == TokenType::GT) {
                const vyb::token::Token& first = peek();
                consume();
                consume();
                op_token = vyb::token::Token(TokenType::RSHIFT, ">>", first.location);
                isShift = true;
            }
            if (!isShift) break;

            vyb::ast::ExprPtr right = parse_additive_expr();
            left = std::make_unique<ast::BinaryExpression>(op_token.location, std::move(left), op_token, std::move(right));
        }
        return left;
    }

    vyb::ast::ExprPtr ExpressionParser::parse_additive_expr() {
        DEBUG_PRINT("Entering parse_additive_expr");
        DEBUG_TOKEN(peek());
        auto expr = parse_binary_expression([this]() {
            DEBUG_PRINT("parse_additive_expr: calling nested parse_multiplicative_expr");
            DEBUG_TOKEN(peek());
            auto inner_expr = parse_multiplicative_expr();
            DEBUG_PRINT("parse_additive_expr: returned from nested parse_multiplicative_expr");
            DEBUG_TOKEN(peek());
            return inner_expr;
        }, {TokenType::PLUS, TokenType::MINUS});
        DEBUG_PRINT("Exiting parse_additive_expr");
        DEBUG_TOKEN(peek());
        return expr;
    }

    vyb::ast::ExprPtr ExpressionParser::parse_multiplicative_expr() {
        DEBUG_PRINT("Entering parse_multiplicative_expr");
        DEBUG_TOKEN(peek());
        auto expr = parse_binary_expression([this]() {
            DEBUG_PRINT("parse_multiplicative_expr: calling nested parse_cast_expr");
            DEBUG_TOKEN(peek());
            auto inner_expr = parse_cast_expr();
            DEBUG_PRINT("parse_multiplicative_expr: returned from nested parse_cast_expr");
            DEBUG_TOKEN(peek());
            return inner_expr;
        }, {TokenType::MULTIPLY, TokenType::DIVIDE, TokenType::MODULO});
        DEBUG_PRINT("Exiting parse_multiplicative_expr");
        DEBUG_TOKEN(peek());
        return expr;
    }

    // `value as TargetType`: safe downcasting. Parses a unary/primary operand then,
    // if followed by the `as` keyword, a target type name (loops for chained casts).
    vyb::ast::ExprPtr ExpressionParser::parse_cast_expr() {
        auto left = parse_unary_expr();
        while (match(TokenType::KEYWORD_AS)) {
            token::Token as_token = previous_token();
            TypeParser type_parser(tokens_, pos_, current_file_path_, *this);
            ast::TypeNodePtr target = type_parser.parse();
            if (!target) {
                throw error(as_token, "Expected a type after 'as'.");
            }
            left = std::make_unique<ast::AsExpression>(
                as_token.location, std::move(left), std::move(target));
        }
        return left;
    }

    vyb::ast::ExprPtr ExpressionParser::parse_unary_expr() {
        if (match(TokenType::KEYWORD_AWAIT)) {
            token::Token await_token = previous_token();
            vyb::ast::ExprPtr operand = parse_unary_expr();
            return std::make_unique<ast::AwaitExpression>(await_token.location, std::move(operand));
        }

        // Handle introspection operators
        if (match(TokenType::KEYWORD_TYPEOF)) {
            token::Token typeof_token = previous_token();
            if (match(TokenType::LT)) {
                // typeof<T>() - compile-time type hash of T
                TypeParser type_parser(tokens_, pos_, current_file_path_, *this);
                ast::TypeNodePtr targ = type_parser.parse();
                if (!targ) {
                    throw error(previous_token(), "Expected a type argument in 'typeof<T>()'.");
                }
                expect(TokenType::GT, "Expected '>' after typeof type argument");
                expect(TokenType::LPAREN, "Expected '(' after 'typeof<T>'");
                expect(TokenType::RPAREN, "Expected ')' after 'typeof<T>'");
                return std::make_unique<ast::TypeofExpression>(typeof_token.location, std::move(targ));
            }
            expect(TokenType::LPAREN, "Expected '(' after 'typeof'");
            vyb::ast::ExprPtr operand = parse_expression();
            expect(TokenType::RPAREN, "Expected ')' after typeof operand");
            return std::make_unique<ast::TypeofExpression>(typeof_token.location, std::move(operand));
        }

        if (match(TokenType::KEYWORD_TYPENAME)) {
            token::Token typename_token = previous_token();
            expect(TokenType::LPAREN, "Expected '(' after 'typename'");
            vyb::ast::ExprPtr operand = parse_expression();
            expect(TokenType::RPAREN, "Expected ')' after typename operand");
            return std::make_unique<ast::TypenameExpression>(typename_token.location, std::move(operand));
        }

        if (match(TokenType::BANG) || match(TokenType::MINUS) || match(TokenType::TILDE)) {
            token::Token op_token = previous_token();
            vyb::ast::ExprPtr operand = parse_unary_expr();
            return std::make_unique<ast::UnaryExpression>(op_token.location, op_token, std::move(operand));
        }
        // Attempt to parse TypeNode for potential constructor call or array initialization `[Type; Size]()`
        // This is a lookahead. We try to parse a type. If it fails, or if it's not followed by
        // '(' or by '; expr ) ( )', then we backtrack and parse as a normal primary expression.
        // This is complex because a TypeName can be a simple Identifier, which parse_primary also handles.

        // Create a TypeParser instance. Assuming it's part of the same parser structure
        // and can be instantiated or accessed here. For now, let's assume we can create it.
        // This requires TypeParser to be available and to have a parse_type_annotation() method.
        // If TypeParser is not designed to be used this way (e.g., it's part of a DeclarationParser),
        // this approach needs rethinking. For now, we'll assume it's usable.

        // Store current position to backtrack if type parsing isn't part of a constructor/array init
        size_t before_type_parse_pos = pos_;
        // It's tricky to integrate TypeParser here directly without knowing its interface
        // and how it handles errors or non-matches. A simple Identifier check might be
        // a first step, then extending to full TypeParser if an LPAREN follows.

        return parse_postfix_expr();
    }

    // Helper to parse postfix operations like calls, member access, subscripting
    vyb::ast::ExprPtr ExpressionParser::parse_postfix_expr() {
        DEBUG_PRINT("Entering parse_postfix_expr");
        DEBUG_TOKEN(peek());
        vyb::ast::ExprPtr expr = parse_primary();
        DEBUG_PRINT("After parse_primary in parse_postfix_expr");
        if(expr) { DEBUG_PRINT("Primary expr parsed successfully."); } else { DEBUG_PRINT("Primary expr is null."); }
        DEBUG_TOKEN(peek());

        while (true) {
            SourceLocation op_loc = peek().location; // Location of the operator (. [ ()
            if (match(TokenType::LPAREN)) {
                DEBUG_PRINT("parse_postfix_expr: Matched LPAREN for call.");
                // Check if \'expr\' is an identifier for an intrinsic function
                // This check needs to be robust. For example, ensure \'expr\' is indeed an Identifier.
                if (expr->getType() == ast::NodeType::IDENTIFIER) {
                    auto id = static_cast<ast::Identifier*>(expr.get());
                    std::string name = id->name;

                    // Handle safe borrow operations: view(expr), borrow(expr)
                    if (name == "view" || name == "borrow") {
                        std::vector<vyb::ast::ExprPtr> arguments;
                        if (!check(TokenType::RPAREN)) {
                            do {
                                arguments.push_back(parse_expression());
                            } while (match(TokenType::COMMA));
                        }
                        expect(TokenType::RPAREN);

                        if (arguments.size() != 1) {
                            vyb::token::Token intrinsic_token(vyb::TokenType::IDENTIFIER, name, id->loc);
                            throw error(intrinsic_token,
                                std::string("Intrinsic '") + name + "' expects 1 argument, got " + std::to_string(arguments.size()));
                        }

                        auto kind = (name == "borrow")
                            ? ast::BorrowKind::MUTABLE_BORROW
                            : ast::BorrowKind::IMMUTABLE_VIEW;
                        expr = std::make_unique<ast::BorrowExpression>(op_loc, std::move(arguments[0]), kind);
                        continue;
                    }

                    // Handle intrinsic-like calls: loc(var), addr(loc_var), at(loc_var), lit(expr), notype(expr), bare(expr), deserial(expr)
                    if (name == "loc" || name == "addr" || name == "at" || name == "lit" || name == "notype" || name == "bare" || name == "deserial") {
                        std::vector<vyb::ast::ExprPtr> arguments;
                        if (!check(TokenType::RPAREN)) {
                            do {
                                arguments.push_back(parse_expression());
                            } while (match(TokenType::COMMA));
                        }
                        expect(TokenType::RPAREN);

                        // Memory intrinsics require exactly 1 argument
                        if (name == "loc" || name == "addr" || name == "at") {
                            if (arguments.size() != 1) {
                                vyb::token::Token intrinsic_token(vyb::TokenType::IDENTIFIER, name, id->loc);
                                throw error(intrinsic_token,
                                    std::string("Intrinsic '") + name + "' expects 1 argument, got " + std::to_string(arguments.size()));
                            }
                        }
                        // Serialization intrinsics can have 1 or more arguments
                        else if (arguments.empty()) {
                            vyb::token::Token intrinsic_token(vyb::TokenType::IDENTIFIER, name, id->loc);
                            throw error(intrinsic_token,
                                std::string("Intrinsic '") + name + "' expects at least 1 argument, got 0");
                        }
                        // Create specialized AST node for intrinsic
                        if (name == "loc" || name == "addr" || name == "at") {
                            // Memory intrinsics use special AST nodes and require exactly 1 argument
                            auto& arg0 = arguments[0];
                            if (name == "loc") {
                                expr = std::make_unique<ast::LocationExpression>(op_loc, std::move(arg0));
                            } else if (name == "addr") {
                                expr = std::make_unique<ast::AddrOfExpression>(op_loc, std::move(arg0));
                            } else { // at
                                expr = std::make_unique<ast::PointerDerefExpression>(op_loc, std::move(arg0));
                            }
                        } else {
                            // For serialization intrinsics (lit, notype, bare, deserial), use CallExpression with all arguments
                            expr = std::make_unique<ast::CallExpression>(op_loc, std::move(expr), std::move(arguments));
                        }
                        continue;
                    }
                }
                // If not an intrinsic or not an identifier, parse as a regular call expression
                expr = parse_call_expression(std::move(expr)); // parse_call_expression expects callee and handles args
            } else if (match(TokenType::DOT)) {
                DEBUG_PRINT("parse_postfix_expr: Matched DOT for member access.");
                DEBUG_TOKEN(previous_token());
                expr = parse_member_access(std::move(expr));
                DEBUG_PRINT("parse_postfix_expr: After parse_member_access. Current token:");
                DEBUG_TOKEN(peek());
            } else if (match(TokenType::COLONCOLON)) {
                DEBUG_PRINT("parse_postfix_expr: Matched COLONCOLON for static member access.");
                DEBUG_TOKEN(previous_token());
                expr = parse_member_access(std::move(expr));
                DEBUG_PRINT("parse_postfix_expr: After parse_member_access. Current token:");
                DEBUG_TOKEN(peek());
            } else if (match(TokenType::LBRACKET)) { // This LBRACKET is for array element access
            token::Token bracket_token = previous_token(); // Location of \'[\'
            auto index_expr = parse_expression();
            expect(TokenType::RBRACKET);
            expr = std::make_unique<ast::ArrayElementExpression>(bracket_token.location, std::move(expr), std::move(index_expr));
        } else {
            break;
        }
    }
    return expr;
}

// Implementation for is_literal
bool ExpressionParser::is_literal(TokenType type) const {
    switch (type) {
        case TokenType::INT_LITERAL:
        case TokenType::FLOAT_LITERAL:
        case TokenType::STRING_LITERAL:
        case TokenType::KEYWORD_TRUE:
        case TokenType::KEYWORD_FALSE:
        case TokenType::KEYWORD_NULL:
        case TokenType::KEYWORD_NIL:
            return true;
        default:
            return false;
    }
}

// Implementation for is_expression_start
// This function helps in determining if a token can start an expression.
// It's used in contexts like parsing the body of a for loop or an if statement.
bool ExpressionParser::is_expression_start(vyb::TokenType type) const {
    // Primary expression starters
    if (type == TokenType::IDENTIFIER ||
        type == TokenType::KEYWORD_FROM ||
        type == TokenType::KEYWORD_MY ||
        type == TokenType::KEYWORD_THEIR ||
        type == TokenType::KEYWORD_OUR ||
        type == TokenType::KEYWORD_MILD ||
        type == TokenType::KEYWORD_BORROW ||
        type == TokenType::KEYWORD_VIEW ||
        type == TokenType::INT_LITERAL ||
        type == TokenType::FLOAT_LITERAL ||
        type == TokenType::STRING_LITERAL ||
        type == TokenType::KEYWORD_TRUE ||
        type == TokenType::KEYWORD_FALSE ||
        type == TokenType::KEYWORD_NULL ||
        type == TokenType::KEYWORD_NIL ||
        type == TokenType::LPAREN ||    // Grouped expression or tuple
        type == TokenType::LBRACKET ||  // Array literal or list comprehension
        type == TokenType::LBRACE ||    // Object literal
        type == TokenType::KEYWORD_IF ||  // If expression
        type == TokenType::KEYWORD_TYPEOF ||  // typeof(expr) / typeof<T>()
        type == TokenType::KEYWORD_TYPENAME)  // typename(expr)
    {
        return true;
    }

    // Check for await keyword (special case)
    if (type == TokenType::KEYWORD_AWAIT) {
        return true;
    }

    // Unary operator starters
    if (type == TokenType::BANG ||
        type == TokenType::MINUS ||
        type == TokenType::TILDE)
    {
        return true;
    }

    // Keywords that can start expressions (like 'from<T>(e)' or constructor calls if types are keywords)
    if (type == TokenType::KEYWORD_FROM) {
        return true;
    }

    return false;
}

// Parse trap clause: trap (errorName<ErrorType>) -> { handler } or trap (e<?>) -> { handler }
std::unique_ptr<vyb::ast::TrapClause> ExpressionParser::parse_trap_clause() {
    auto loc = current_location();

    // Expect 'trap' keyword (already matched by caller)

    // Expect '('
    expect(TokenType::LPAREN, "Expected '(' after 'trap'");

    // Parse error binding: errorName<ErrorType> or errorName<?>
    if (!check(TokenType::IDENTIFIER)) {
        throw error(peek(), "Expected error variable name in trap clause");
    }
    auto errorToken = consume();
    auto errorNameStr = errorToken.lexeme;
    auto errorNameLoc = errorToken.location;

    // Create Identifier for error name
    auto errorNameIdent = std::make_unique<vyb::ast::Identifier>(errorNameLoc, errorNameStr);

    // Expect '<'
    expect(TokenType::LT, "Expected '<' after error variable name");

    // Check for wildcard '?'
    bool isWildcard = false;
    bool isMultiType = false;
    vyb::ast::TypeNodePtr errorType = nullptr;
    std::vector<vyb::ast::TypeNodePtr> errorTypes;

    if (check(TokenType::QUESTION_MARK)) {
        // Wildcard trap: trap (e<?>) -> { ... }
        consume(); // Consume '?'
        isWildcard = true;
    } else {
        // Specific type trap: trap (e<ErrorType>) or multi-type: trap (e<Type1 | Type2>)
        if (!check(TokenType::IDENTIFIER)) {
            throw error(peek(), "Expected error type name or '?' in trap clause");
        }

        // Parse first type
        auto typeToken = consume();
        std::vector<std::string> typePath;
        typePath.push_back(typeToken.lexeme);

        // Handle module paths like module::ErrorType
        while (match(TokenType::COLONCOLON)) {
            if (!check(TokenType::IDENTIFIER)) {
                throw error(peek(), "Expected identifier after '::'");
            }
            auto nextToken = consume();
            typePath.push_back(nextToken.lexeme);
        }

        // Create identifier from type path
        std::string fullTypeName = typePath[0];
        for (size_t i = 1; i < typePath.size(); ++i) {
            fullTypeName += "::" + typePath[i];
        }
        auto typeIdentifier = std::make_unique<vyb::ast::Identifier>(typeToken.location, fullTypeName);

        // Create TypeName with the identifier
        auto firstType = std::make_unique<vyb::ast::TypeName>(typeToken.location, std::move(typeIdentifier));

        // Check for union operator '|' for multi-type traps
        if (check(TokenType::PIPE)) {
            // Multi-type trap: trap (e<Type1 | Type2 | Type3>) -> { ... }
            isMultiType = true;
            errorTypes.push_back(std::move(firstType));

            // Parse additional types separated by '|'
            while (match(TokenType::PIPE)) {
                if (!check(TokenType::IDENTIFIER)) {
                    throw error(peek(), "Expected type name after '|' in trap clause");
                }

                auto nextTypeToken = consume();
                std::vector<std::string> nextTypePath;
                nextTypePath.push_back(nextTypeToken.lexeme);

                // Handle module paths
                while (match(TokenType::COLONCOLON)) {
                    if (!check(TokenType::IDENTIFIER)) {
                        throw error(peek(), "Expected identifier after '::'");
                    }
                    auto pathToken = consume();
                    nextTypePath.push_back(pathToken.lexeme);
                }

                // Create identifier from type path
                std::string nextFullTypeName = nextTypePath[0];
                for (size_t i = 1; i < nextTypePath.size(); ++i) {
                    nextFullTypeName += "::" + nextTypePath[i];
                }
                auto nextTypeIdentifier = std::make_unique<vyb::ast::Identifier>(nextTypeToken.location, nextFullTypeName);
                auto nextType = std::make_unique<vyb::ast::TypeName>(nextTypeToken.location, std::move(nextTypeIdentifier));

                errorTypes.push_back(std::move(nextType));
            }
        } else {
            // Single type trap
            errorType = std::move(firstType);
        }
    }

    // Expect '>'
    expect(TokenType::GT, "Expected '>' after error type");

    // Expect ')'
    expect(TokenType::RPAREN, "Expected ')' after error pattern");

    // Expect '->'
    expect(TokenType::ARROW, "Expected '->' after trap pattern");

    // Parse handler block - don't consume LBRACE, let parse_block() do it
    if (!stmt_parser_) {
        throw error(peek(), "Statement parser not available for trap handler");
    }

    if (!check(TokenType::LBRACE)) {
        throw error(peek(), "Expected '{' for trap handler block");
    }
    auto handlerBlock = stmt_parser_->parse_block();

    // Create trap clause
    auto trapClause = std::make_unique<vyb::ast::TrapClause>(
        loc, std::move(errorNameIdent), std::move(errorType), std::move(handlerBlock), isWildcard, isMultiType
    );

    // Move errorTypes vector into the trap clause
    if (isMultiType) {
        trapClause->errorTypes = std::move(errorTypes);
    }

    return trapClause;
}

// Parse ensure clause: ensure -> { cleanup }
std::unique_ptr<vyb::ast::EnsureClause> ExpressionParser::parse_ensure_clause() {
    auto loc = current_location();

    // Expect 'ensure' keyword (already matched by caller)

    // Expect '->'
    expect(TokenType::ARROW, "Expected '->' after 'ensure'");

    // Parse cleanup block - don't consume LBRACE, let parse_block() do it
    if (!stmt_parser_) {
        throw error(peek(), "Statement parser not available for ensure handler");
    }

    if (!check(TokenType::LBRACE)) {
        throw error(peek(), "Expected '{' for ensure cleanup block");
    }
    auto cleanupBlock = stmt_parser_->parse_block();

    return std::make_unique<vyb::ast::EnsureClause>(loc, std::move(cleanupBlock));
}

} // namespace vyb
