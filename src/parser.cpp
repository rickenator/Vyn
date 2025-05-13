// Stub file for parser other implementations
#include "vyn/parser.hpp"
#include "vyn/ast.hpp"
#include "vyn/token.hpp"

#include <stdexcept> // For std::runtime_error

// Main Parser class implementation
Parser::Parser(const std::vector<Vyn::Token>& tokens, std::string file_path)
    : tokens_(tokens),
      pos_(0), // Initialize pos_ here
      file_path_(std::move(file_path)),
      expression_parser_(tokens_, pos_, file_path_),
      type_parser_(tokens_, pos_, file_path_, expression_parser_),
      // StatementParser needs indent_level, type_parser, expr_parser.
      // Initial indent level for top-level statements in a module context might be 0.
      // This instance of StatementParser is primarily for DeclarationParser to use when it needs to parse blocks (e.g. function bodies).
      statement_parser_(tokens_, pos_, 0, file_path_, type_parser_, expression_parser_),
      // DeclarationParser now also needs StatementParser
      declaration_parser_(tokens_, pos_, file_path_, type_parser_, expression_parser_, statement_parser_),
      module_parser_(tokens_, pos_, file_path_, declaration_parser_) {}

std::unique_ptr<Vyn::AST::ModuleNode> Parser::parse_module() {
    // The main entry point to parsing is expected to be a module.
    // Delegate to ModuleParser.
    auto module_node = module_parser_.parse();
    
    // After parsing the module, we should be at the EOF token.
    // We need a way to peek without skipping comments/newlines if we want to check the absolute last token.
    // Or, rely on the module parser to consume everything up to EOF.
    // For now, let's assume module_parser.parse() consumes tokens until EOF or throws.
    
    // Check if there are any remaining tokens that are not EOF, COMMENT, or NEWLINE
    // This check is a bit tricky with the current BaseParser::peek() which skips comments/newlines.
    // A more robust check might involve a raw peek or checking pos_ against tokens_.size()
    // after module_parser_.parse() completes.

    // If module_node is null and no exception was thrown, it's an unexpected state.
    if (!module_node) {
        // If pos_ is not at the end and the next token is not EOF, throw an error.
        // This check needs to be careful not to advance pos_.
        size_t temp_pos = pos_;
        while (temp_pos < tokens_.size() &&
               (tokens_[temp_pos].type == Vyn::TokenType::COMMENT ||
                tokens_[temp_pos].type == Vyn::TokenType::NEWLINE)) {
            temp_pos++;
        }
        if (temp_pos < tokens_.size() && tokens_[temp_pos].type != Vyn::TokenType::EOF_TOKEN) {
             throw std::runtime_error("Parser::parse_module: Failed to parse the entire module. Unexpected token at end: " + Vyn::token_type_to_string(tokens_[temp_pos].type) + " in file " + file_path_);
        } else if (temp_pos >= tokens_.size() && !tokens_.empty() && tokens_.back().type != Vyn::TokenType::EOF_TOKEN) {
            // This case means we ran out of tokens but the last one wasn't EOF. Lexer issue?
             throw std::runtime_error("Parser::parse_module: Token stream did not end with EOF_TOKEN in file " + file_path_);
        }
        // If module_node is null but we are at EOF, it means an empty file or only comments.
        // This should be handled by ModuleParser::parse() returning a valid (possibly empty) ModuleNode.
        // So, if it's null here, it's an issue.
        if (!module_node && temp_pos < tokens_.size() && tokens_[temp_pos].type == Vyn::TokenType::EOF_TOKEN) {
            // This implies ModuleParser returned nullptr for a valid EOF scenario, which it shouldn't.
            // It should return an empty ModuleNode.
            // For now, let's assume ModuleParser handles empty files by returning a ModuleNode with no declarations.
            // If it *can* return nullptr on success (e.g. truly empty file and that's valid), this needs adjustment.
            // Based on AST design, ModuleNode should always be created.
             throw std::runtime_error("Parser::parse_module: ModuleParser returned null for a seemingly valid EOF. file: " + file_path_);
        }
    }


    return module_node;
}

// Constructor implementations for other parsers will go into their respective files.
// e.g., ExpressionParser constructor in expression_parser.cpp