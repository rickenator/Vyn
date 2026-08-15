// SPDX-License-Identifier: Apache-2.0

#include "vyb/parser/ast.hpp"
#include "vyb/parser/token.hpp"
#include "vyb/parser/parser.hpp"
#include <vector>
#include <memory>
#include <stdexcept>

namespace vyb {

ModuleParser::ModuleParser(const std::vector<vyb::token::Token>& tokens, size_t& pos, const std::string& file_path, DeclarationParser& declaration_parser)
    : BaseParser(tokens, pos, file_path), declaration_parser_(declaration_parser) {}

std::unique_ptr<vyb::ast::Module> ModuleParser::parse() {
    vyb::SourceLocation module_loc = this->current_location();
    std::vector<vyb::ast::StmtPtr> module_body;

    this->skip_comments_and_newlines();

    while (this->peek().type != vyb::TokenType::END_OF_FILE) {
        vyb::token::Token current_token_before_parse = this->peek();
        size_t pos_before_parse = this->pos_;

        auto decl_node = this->declaration_parser_.parse();
        if (decl_node) {
            module_body.push_back(std::move(decl_node));
            #ifdef VERBOSE
            std::cerr << "[ModuleParser] Before consuming semicolons, next token: "
                      << vyb::token_type_to_string(this->peek().type) << " (" << this->peek().lexeme << ")\\\\n";
            #endif
            // Consume all consecutive semicolons after top-level declaration (e.g., class, struct, etc.)
            while (this->peek().type == vyb::TokenType::SEMICOLON) {
                #ifdef VERBOSE
                std::cerr << "[ModuleParser] Consuming semicolon at: " << this->peek().location.toString() << "\\\\n";
                #endif
                this->consume();
            }
            #ifdef VERBOSE
            std::cerr << "[ModuleParser] After consuming semicolons, next token: "
                      << vyb::token_type_to_string(this->peek().type) << " (" << this->peek().lexeme << ")\\\\n";
            #endif
        } else {
            // If parsing a declaration failed and no tokens were consumed,
            // it means we encountered something unexpected.
            // To prevent an infinite loop, consume the token and report an error, or break.
            if (this->pos_ == pos_before_parse && this->peek().type == current_token_before_parse.type && this->peek().lexeme == current_token_before_parse.lexeme) {
                std::cerr << "[ModuleParser] Error: No progress made. Unexpected token: "
                          << vyb::token_type_to_string(this->peek().type)
                          << " (\\\"" << this->peek().lexeme << "\\\") at "
                          << this->peek().location.toString() << ". Stopping parse.\\\\n";
                // Optionally, throw an exception here or add to an error list
                // For now, let's break to prevent infinite loop.
                // You might want to consume the token: this->consume();
                break;
            }
        }
    }

    return std::make_unique<vyb::ast::Module>(module_loc, std::move(module_body));
}

} // namespace vyb
