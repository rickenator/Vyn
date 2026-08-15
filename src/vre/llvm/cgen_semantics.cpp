// SPDX-License-Identifier: Apache-2.0

#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/semantic.hpp"
#include "vyb/parser/ast.hpp"

using namespace vyb;

// Helper method to extract original type name using semantic analysis data
std::string LLVMCodegen::extractOriginalTypeNameFromSemantics(vyb::ast::Expression* expr) {
    if (!expr) {
        return "unknown";
    }

    return extractOriginalTypeNameFromAST(expr);
}

// Helper method to extract original type name from AST directly (fallback method)
std::string LLVMCodegen::extractOriginalTypeNameFromAST(vyb::ast::Expression* expr) {
    if (!expr) {
        return "unknown";
    }

    // Handle different expression types
    if (auto intLiteral = dynamic_cast<ast::IntegerLiteral*>(expr)) {
        return "Int";
    } else if (auto floatLiteral = dynamic_cast<ast::FloatLiteral*>(expr)) {
        return "Float";
    } else if (auto stringLiteral = dynamic_cast<ast::StringLiteral*>(expr)) {
        return "String";
    } else if (auto boolLiteral = dynamic_cast<ast::BooleanLiteral*>(expr)) {
        return "Bool";
    } else if (auto nilLiteral = dynamic_cast<ast::NilLiteral*>(expr)) {
        return "Nil";
    } else if (auto identifier = dynamic_cast<ast::Identifier*>(expr)) {
        // For identifiers, we'd need to look up their declared type
        // For now, return a placeholder
        return "Identifier(" + identifier->name + ")";
    } else if (auto callExpr = dynamic_cast<ast::CallExpression*>(expr)) {
        // Handle intrinsic calls like notype()
        if (auto calleeId = dynamic_cast<ast::Identifier*>(callExpr->callee.get())) {
            if (calleeId->name == "notype") {
                // For notype(), return empty string to suppress type metadata
                return "";
            } else if (calleeId->name == "lit") {
                // For lit(), extract the type from the argument
                if (!callExpr->arguments.empty()) {
                    return extractOriginalTypeNameFromAST(callExpr->arguments[0].get());
                }
            }
        }
        // For other call expressions, try to determine from context
        return "CallResult";
    } else if (auto memberExpr = dynamic_cast<ast::MemberExpression*>(expr)) {
        return "MemberAccess";
    } else if (auto arrayExpr = dynamic_cast<ast::ArrayElementExpression*>(expr)) {
        return "ArrayElement";
    }

    // Default fallback
    return "Unknown";
}
