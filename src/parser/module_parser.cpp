#include "vyn/parser/ast.hpp"
#include "vyn/parser/token.hpp"
#include "vyn/parser/parser.hpp" // Added missing include for parser definitions
#include <vector>
#include <memory>
#include <stdexcept> // Required for std::runtime_error

namespace vyn { // Changed Vyn to vyn

// Constructor for ModuleParser
ModuleParser::ModuleParser(const std::vector<vyn::token::Token>& tokens, size_t& pos, const std::string& file_path, DeclarationParser& declaration_parser) // Changed Vyn::Token to vyn::token::Token
    : BaseParser(tokens, pos, file_path), declaration_parser_(declaration_parser) {}

std::unique_ptr<vyn::Module> ModuleParser::parse() {
    vyn::SourceLocation module_loc = this->current_location();
    std::vector<vyn::StmtPtr> module_body;

    this->skip_comments_and_newlines();

    while (this->peek().type != vyn::TokenType::END_OF_FILE) {
        // Try to parse a declaration first
        auto decl_node = this->declaration_parser_.parse();
        if (decl_node) {
            module_body.push_back(std::move(decl_node));
        } else {
            // If not a declaration, try to parse a statement
            // We need to construct a StatementParser for this
            StatementParser stmt_parser(this->tokens_, this->pos_, /*indent_level=*/0, this->current_file_path_,
                                       this->declaration_parser_.get_type_parser(),
                                       this->declaration_parser_.get_expr_parser());
            auto stmt_node = stmt_parser.parse();
            this->pos_ = stmt_parser.get_current_pos();
            if (stmt_node) {
                module_body.push_back(std::move(stmt_node));
            } else {
                // If both fail, break (likely EOF or unrecoverable error)
                break;
            }
        }
        this->skip_comments_and_newlines();
    }

    return std::make_unique<vyn::Module>(module_loc, std::move(module_body));
}

} // End namespace vyn