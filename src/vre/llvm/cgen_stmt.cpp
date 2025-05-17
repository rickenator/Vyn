#include <vyn/parser/ast.hpp>
#include "vyn/vre/llvm/codegen.hpp"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h> // For llvm::verifyFunction

namespace vyn {

// --- Statements ---

void LLVMCodegen::visit(vyn::ast::BlockStatement* node) {
    // Scoping for namedValues would be handled here in a more advanced setup.
    // For now, new allocas are function-scoped.
    for (const auto& stmt : node->body) {
        stmt->accept(*this);
        if (builder->GetInsertBlock() && builder->GetInsertBlock()->getTerminator()) {
            // If the block has been terminated (e.g., by a return), stop processing.
            // This can happen if a code path definitely returns.
            // However, subsequent statements might be part of a different path if there was a branch.
            // For a simple sequential block, this is fine.
            // More robust handling might involve checking if the current insert point is reachable.
            break; 
        }
    }
}

void LLVMCodegen::visit(vyn::ast::ReturnStatement* node) {
    if (node->argument) {
        node->argument->accept(*this);
        llvm::Value* returnValue = m_currentLLVMValue;
        if (returnValue) {
            if (currentFunction) {
                llvm::Type* expectedReturnType = currentFunction->getReturnType();
                if (returnValue->getType() != expectedReturnType) {
                    // Attempt to cast if types are different but compatible (e.g. int to float)
                    // This is a placeholder for more sophisticated type checking and casting.
                    // For now, we'll log an error if types don't match exactly.
                    // A more robust solution would involve checking assignability or implicit conversions.
                    llvm::Value* castedValue = tryCast(returnValue, expectedReturnType, node->loc);
                    if (!castedValue) {
                        logError(node->loc, "Return value type (" + getTypeName(returnValue->getType()) +
                                           ") does not match function return type (" + getTypeName(expectedReturnType) +
                                           ") and no cast was performed.");
                        // Return undef if types mismatch and no cast, to avoid IR errors, but signal problem
                        builder->CreateRet(llvm::UndefValue::get(expectedReturnType));
                        m_currentLLVMValue = nullptr;
                        return;
                    }
                    returnValue = castedValue;
                }
                builder->CreateRet(returnValue);
            } else {
                logError(node->loc, "Return statement outside of a function.");
            }
        } else {
            logError(node->loc, "Return argument evaluated to null.");
            if (currentFunction && !currentFunction->getReturnType()->isVoidTy()) {
                 builder->CreateRet(llvm::UndefValue::get(currentFunction->getReturnType()));
            } else if (currentFunction) { // Void return type, and argument was null (error already logged)
                 builder->CreateRetVoid();
            }
        }
    } else {
        // Return void
        if (currentFunction && !currentFunction->getReturnType()->isVoidTy()) {
            logError(node->loc, "Void return in function with non-void return type " + getTypeName(currentFunction->getReturnType()));
             builder->CreateRet(llvm::UndefValue::get(currentFunction->getReturnType())); // Return undef to satisfy LLVM if type is non-void
        } else if (currentFunction) { // Correctly returning void from a void function
            builder->CreateRetVoid();
        } else {
            logError(node->loc, "Return statement (void) outside of a function.");
        }
    }
    m_currentLLVMValue = nullptr; // Return statement doesn't produce a value for further expressions
}

void LLVMCodegen::visit(vyn::ast::ExpressionStatement* node) {
    if (node->expression) {
        node->expression->accept(*this);
        // The value of the expression is m_currentLLVMValue, but it's not used by the statement itself.
    }
    m_currentLLVMValue = nullptr; // Expression statement doesn't produce a value for further expressions
}

void LLVMCodegen::visit(vyn::ast::IfStatement* node) {
    node->test->accept(*this);
    llvm::Value* conditionValue = m_currentLLVMValue;
    if (!conditionValue) {
        logError(node->loc, "Condition of if statement is null.");
        return;
    }

    // Convert condition to i1 if necessary
    if (conditionValue->getType()->isPointerTy()) {
        // Treat pointer as true if not null, false if null
        conditionValue = builder->CreateIsNotNull(conditionValue, "ptrcond");
    } else if (conditionValue->getType()->isIntegerTy() && conditionValue->getType() != int1Type) {
        // Treat non-zero integer as true, zero as false
        conditionValue = builder->CreateICmpNE(
            conditionValue, 
            llvm::ConstantInt::get(conditionValue->getType(), 0), 
            "intcond_tobool"
        );
    } else if (conditionValue->getType() != int1Type) {
         // If not boolean, pointer, or integer, it's an error.
         logError(node->test->loc, "If condition is not boolean or convertible to boolean (type: " + getTypeName(conditionValue->getType()) + "). Treating as false.");
         conditionValue = llvm::ConstantInt::get(int1Type, 0); // Treat as false to prevent IR errors
    }


    llvm::Function* parentFunction = builder->GetInsertBlock()->getParent();
    if (!parentFunction) { // Should not happen if we are generating code inside a function
        logError(node->loc, "Cannot create blocks for if statement: not in a function.");
        return;
    }

    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "then", parentFunction);
    llvm::BasicBlock* elseBB = nullptr;
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "ifcont"); // Don't add to function yet, might not be needed if all paths return

    if (node->alternate) {
        elseBB = llvm::BasicBlock::Create(*context, "else", parentFunction);
        builder->CreateCondBr(conditionValue, thenBB, elseBB);
    } else {
        builder->CreateCondBr(conditionValue, thenBB, mergeBB);
    }

    // Emit then block
    builder->SetInsertPoint(thenBB);
    node->consequent->accept(*this);
    if (!builder->GetInsertBlock()->getTerminator()) { // If 'then' doesn't end with a return/break
        builder->CreateBr(mergeBB);
    }
    // thenBB = builder->GetInsertBlock(); // Update thenBB to the actual end block of the 'then' part

    // Emit else block
    if (node->alternate) {
        // parentFunction->getBasicBlockList().push_back(elseBB); // Create already adds it
        builder->SetInsertPoint(elseBB);
        node->alternate->accept(*this);
        if (!builder->GetInsertBlock()->getTerminator()) { // If 'else' doesn't end with a return/break
            builder->CreateBr(mergeBB);
        }
        // elseBB = builder->GetInsertBlock(); // Update elseBB
    }
    
    // If mergeBB is not used by any predecessors (e.g. both then and else return), it can be removed.
    // However, LLVM's dead code elimination should handle this.
    // We must add mergeBB to the function if it has predecessors.
    if (!mergeBB->use_empty() || (!node->alternate && thenBB->getTerminator() && !llvm::isa<llvm::ReturnInst>(thenBB->getTerminator()))) { // if no alternate, and thenBB doesn't return, it must branch to mergeBB
        // parentFunction->getBasicBlockList().push_back(mergeBB); // OLD - private member
        if (!mergeBB->getParent()) { // Only insert if not already part of a function
             mergeBB->insertInto(parentFunction);
        }
        builder->SetInsertPoint(mergeBB);
    } else {
        mergeBB->eraseFromParent(); // Or just let DCE handle it. For clarity, explicit removal if unused.
    }
    
    // If IfStatement were an expression, a PHI node would be needed here.
    // For now, IfStatement does not produce a value.
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyn::ast::WhileStatement* node) {
    llvm::Function* parentFunc = builder->GetInsertBlock()->getParent();
    if (!parentFunc) {
        logError(node->loc, "Cannot create while loop: not in a function.");
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::BasicBlock* loopHeaderBB = llvm::BasicBlock::Create(*context, "loop.header", parentFunc);
    llvm::BasicBlock* loopBodyBB = llvm::BasicBlock::Create(*context, "loop.body", parentFunc);
    llvm::BasicBlock* loopExitBB = llvm::BasicBlock::Create(*context, "loop.exit", parentFunc);

    // Jump to header
    builder->CreateBr(loopHeaderBB);

    // Populate header: condition check
    builder->SetInsertPoint(loopHeaderBB);
    node->test->accept(*this); // Evaluate condition
    llvm::Value* condVal = m_currentLLVMValue;
    if (!condVal) {
        logError(node->test->loc, "While loop condition is null.");
        // Treat as false and branch to exit to avoid crashing
        builder->CreateBr(loopExitBB);
    } else {
        if (condVal->getType() != int1Type) { // Ensure condition is i1
            // Convert to boolean if necessary (e.g., integer to bool: 0 is false, non-0 is true)
            if (condVal->getType()->isPointerTy()) {
                condVal = builder->CreateIsNotNull(condVal, "ptrcond_while");
            } else if (condVal->getType()->isIntegerTy()) {
                condVal = builder->CreateICmpNE(condVal, llvm::Constant::getNullValue(condVal->getType()), "whilecond_tobool");
            } else {
                logError(node->test->loc, "While loop condition is not boolean or convertible to boolean. Treating as false.");
                condVal = llvm::ConstantInt::get(int1Type, 0);
            }
        }
        builder->CreateCondBr(condVal, loopBodyBB, loopExitBB);
    }
    
    // Populate body
    builder->SetInsertPoint(loopBodyBB);
    // The LoopContext struct in codegen.hpp is {llvm::BasicBlock *loopHeader, *loopBody, *loopUpdate, *loopExit;}
    // For a 'while' loop, the 'update' block is effectively the header where the condition is re-evaluated.
    pushLoop(loopHeaderBB, loopBodyBB, loopHeaderBB /*update is header for while*/, loopExitBB); 
    node->body->accept(*this); // Generate loop body
    popLoop(); // Pop loop context

    if (!builder->GetInsertBlock()->getTerminator()) { // If body doesn't end with break/return
        builder->CreateBr(loopHeaderBB); // Jump back to header
    }

    // Continue codegen in the exit block
    builder->SetInsertPoint(loopExitBB);
    m_currentLLVMValue = nullptr; // While statement itself doesn't produce a value
}

void LLVMCodegen::visit(vyn::ast::BreakStatement* node) {
    if (loopStack.empty()) {
        logError(node->loc, "Break statement outside of a loop.");
        m_currentLLVMValue = nullptr;
        return;
    }
    LoopContext& currentLoop = loopStack.back();
    // Member name is loopExit based on struct LoopContext definition in codegen.hpp
    if (!currentLoop.loopExit) { 
         logError(node->loc, "Invalid loop context: exit block is null for break.");
         m_currentLLVMValue = nullptr;
         return;
    }
    builder->CreateBr(currentLoop.loopExit); 
    m_currentLLVMValue = nullptr; 
    // Note: After a break, the current block is terminated. 
    // We might need to create a new block if code generation is supposed to continue after the break
    // in the same scope, but typically break is the last thing in its path.
    // For simplicity, we assume subsequent code is unreachable or handled by block structure.
}

void LLVMCodegen::visit(vyn::ast::ContinueStatement* node) {
    if (loopStack.empty()) {
        logError(node->loc, "Continue statement outside of a loop.");
        m_currentLLVMValue = nullptr;
        return;
    }
    LoopContext& currentLoop = loopStack.back();
    // Member name is loopUpdate based on struct LoopContext definition in codegen.hpp
     if (!currentLoop.loopUpdate) { 
         logError(node->loc, "Invalid loop context: update/header block is null for continue.");
         m_currentLLVMValue = nullptr;
         return;
    }
    builder->CreateBr(currentLoop.loopUpdate); 
    m_currentLLVMValue = nullptr;
    // Similar to break, continue terminates the current path in the block.
}

void LLVMCodegen::visit(vyn::ast::ForStatement* node) {
    llvm::Function* parentFunc = builder->GetInsertBlock()->getParent();
    if (!parentFunc) {
        logError(node->loc, "Cannot create for loop: not in a function.");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Create blocks for the loop parts
    llvm::BasicBlock* initBB = llvm::BasicBlock::Create(*context, "for.init", parentFunc);
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "for.cond", parentFunc);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*context, "for.body", parentFunc);
    llvm::BasicBlock* updateBB = llvm::BasicBlock::Create(*context, "for.update", parentFunc);
    llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(*context, "for.exit", parentFunc);

    // Initializer block
    builder->CreateBr(initBB);
    builder->SetInsertPoint(initBB);
    if (node->init) {
        node->init->accept(*this); // Generate initializer code
    }
    builder->CreateBr(condBB); // Fall through to condition check

    // Condition block
    builder->SetInsertPoint(condBB);
    if (node->test) {
        node->test->accept(*this); // Evaluate condition
        llvm::Value* condVal = m_currentLLVMValue;
        if (!condVal) {
            logError(node->test->loc, "For loop condition is null. Treating as false.");
            condVal = llvm::ConstantInt::get(int1Type, 0);
        }
        if (condVal->getType() != int1Type) { // Ensure condition is i1
             if (condVal->getType()->isPointerTy()) {
                condVal = builder->CreateIsNotNull(condVal, "ptrcond_for");
            } else if (condVal->getType()->isIntegerTy()) {
                condVal = builder->CreateICmpNE(condVal, llvm::Constant::getNullValue(condVal->getType()), "forcond_tobool");
            } else {
                logError(node->test->loc, "For loop condition is not boolean or convertible. Treating as false.");
                condVal = llvm::ConstantInt::get(int1Type, 0);
            }
        }
        builder->CreateCondBr(condVal, bodyBB, exitBB);
    } else {
        // No condition means infinite loop (or until break)
        builder->CreateBr(bodyBB);
    }

    // Body block
    builder->SetInsertPoint(bodyBB);
    pushLoop(condBB, bodyBB, updateBB, exitBB);
    node->body->accept(*this); // Generate loop body
    popLoop();
    if (!builder->GetInsertBlock()->getTerminator()) { // If body doesn't end with break/return
        builder->CreateBr(updateBB); // Jump to update block
    }

    // Update block
    builder->SetInsertPoint(updateBB);
    if (node->update) {
        node->update->accept(*this); // Generate update code
    }
    builder->CreateBr(condBB); // Jump back to condition check

    // Exit block
    builder->SetInsertPoint(exitBB);
    m_currentLLVMValue = nullptr; // For statement itself doesn't produce a value
}

void LLVMCodegen::visit(vyn::ast::TryStatement* node) {
    // NOTE: This is a simplified implementation without actual exception handling (e.g., landing pads).
    // It will execute the try block, and if a finally block exists, it will execute it.
    // Catch block is ignored for now as proper C++ style exception handling is complex in LLVM IR
    // without specific runtime support or a personality function.

    logError(node->loc, "TryStatement codegen is currently a stub and does not handle exceptions.");

    llvm::Function* parentFunc = builder->GetInsertBlock()->getParent();
    if (!parentFunc) {
        logError(node->loc, "Cannot create try-finally: not in a function.");
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::BasicBlock* tryBB = llvm::BasicBlock::Create(*context, "try.block", parentFunc);
    llvm::BasicBlock* finallyBB = nullptr;
    llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "try.cont", parentFunc);

    builder->CreateBr(tryBB);
    builder->SetInsertPoint(tryBB);
    if (node->tryBlock) {
        node->tryBlock->accept(*this);
    }
    // If try block did not terminate, branch to finally or continue
    if (!builder->GetInsertBlock()->getTerminator()) {
        if (node->finallyBlock) {
            finallyBB = llvm::BasicBlock::Create(*context, "finally.block", parentFunc);
            builder->CreateBr(finallyBB);
        } else {
            builder->CreateBr(contBB);
        }
    }

    if (node->catchBlock) {
        logError(node->loc, "Catch blocks in TryStatement are not yet supported in codegen.");
        // To make it somewhat valid, if there was a catch block, we might need another path.
        // For now, it's ignored.
    }

    if (node->finallyBlock) {
        if (!finallyBB) { // Should have been created if node->finallyBlock exists
             finallyBB = llvm::BasicBlock::Create(*context, "finally.block", parentFunc);
        }
        builder->SetInsertPoint(finallyBB);
        node->finallyBlock->accept(*this);
        // If finally block did not terminate, branch to continue block
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(contBB);
        }
    }
    
    builder->SetInsertPoint(contBB);
    m_currentLLVMValue = nullptr; // Try statement itself doesn't produce a value
}

} // namespace vyn

