// SPDX-License-Identifier: Apache-2.0

#include "vyb/vre/llvm/codegen.hpp"
#include <iostream>

using namespace vyb;

// Helper to recursively scan statements for trap blocks and pre-create their allocas
void LLVMCodegen::preCreateTrapAllocas(ast::Statement* stmt, llvm::Function* func, llvm::Instruction** lastAllocaInsertPt) {
    if (!stmt || !func) return;

    // Check if this is a block statement
    if (auto* blockStmt = dynamic_cast<ast::BlockStatement*>(stmt)) {
        // Scan all statements in the block
        for (const auto& s : blockStmt->body) {
            preCreateTrapAllocas(s.get(), func, lastAllocaInsertPt);
        }
        return;
    }

    // Check if this is an expression statement containing a block expression
    if (auto* exprStmt = dynamic_cast<ast::ExpressionStatement*>(stmt)) {
        if (auto* blockExpr = dynamic_cast<ast::BlockExpression*>(exprStmt->expression.get())) {
            // This block expression has trap clauses - pre-create the alloca
            if (!blockExpr->trapClauses.empty()) {
                VYB_CDBG << "DEBUG: Pre-creating trap_error alloca for expression statement block" << std::endl;
                llvm::Type* errorPtrType = llvm::PointerType::get(*context, 0);
                auto* alloca = createEntryBlockAlloca(errorPtrType, "trap_error");
                // Don't initialize here - will be initialized when the block expression is processed
            }
        }
        return;
    }

    // Check if this is a variable declaration with an initializer
    if (auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt)) {
        if (varDecl->init) {
            if (auto* blockExpr = dynamic_cast<ast::BlockExpression*>(varDecl->init.get())) {
                if (!blockExpr->trapClauses.empty()) {
                    VYB_CDBG << "DEBUG: Pre-creating trap_error alloca for variable initializer with "
                              << blockExpr->trapClauses.size() << " trap clauses" << std::endl;
                    llvm::Type* errorPtrType = llvm::PointerType::get(*context, 0);
                    auto* alloca = createEntryBlockAlloca(errorPtrType, "trap_error");
                    // Don't initialize here - will be initialized when the block expression is processed
                }
            }
        }
        return;
    }

    // Check if/else statements
    if (auto* ifStmt = dynamic_cast<ast::IfStatement*>(stmt)) {
        preCreateTrapAllocas(ifStmt->consequent.get(), func, lastAllocaInsertPt);
        if (ifStmt->alternate) {
            preCreateTrapAllocas(ifStmt->alternate.get(), func, lastAllocaInsertPt);
        }
        return;
    }

    // Check while statements
    if (auto* whileStmt = dynamic_cast<ast::WhileStatement*>(stmt)) {
        preCreateTrapAllocas(whileStmt->body.get(), func, lastAllocaInsertPt);
        return;
    }

    // Check for statements
    if (auto* forStmt = dynamic_cast<ast::ForStatement*>(stmt)) {
        preCreateTrapAllocas(forStmt->body.get(), func, lastAllocaInsertPt);
        return;
    }

    // Check match statements
    if (auto* matchStmt = dynamic_cast<ast::MatchStatement*>(stmt)) {
        for (const auto& matchCase : matchStmt->cases) {
            // matchCase is a pair<ExprPtr, ExprPtr> - pattern and body
            if (matchCase.second) {
                // Body might be a BlockExpression
                if (auto* blockExpr = dynamic_cast<ast::BlockExpression*>(matchCase.second.get())) {
                    if (!blockExpr->trapClauses.empty()) {
                        VYB_CDBG << "DEBUG: Pre-creating trap_error alloca for match case with "
                                  << blockExpr->trapClauses.size() << " trap clauses" << std::endl;
                        llvm::Type* errorPtrType = llvm::PointerType::get(*context, 0);
                        auto* alloca = createEntryBlockAlloca(errorPtrType, "trap_error");
                        // Don't initialize here - will be initialized when the block expression is processed
                    }
                }
            }
        }
        return;
    }
}
