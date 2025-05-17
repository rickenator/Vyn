\
#include "vyn/vre/llvm/codegen.hpp"
#include "vyn/parser/ast.hpp"
#include "vyn/parser/token.hpp" // For TokenType in BinaryExpression

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/DerivedTypes.h> // For PointerType, StructType
#include <llvm/ADT/APFloat.h>   // For APFloat in FloatLiteral
#include <llvm/ADT/APInt.h>     // For APInt in IntegerLiteral

using namespace vyn;
// using namespace llvm; // Uncomment if desired for brevity

// --- Literal Codegen ---
void LLVMCodegen::visit(vyn::ast::Identifier* node) {
    auto it = namedValues.find(node->name);
    if (it != namedValues.end()) {
        llvm::Value* val = it->second;
        // If it's an AllocaInst, it represents a memory location.
        // For RHS, we need to load it unless it's an array/function type (which decays to pointer).
        if (m_isLHSOfAssignment) {
            m_currentLLVMValue = val; // For LHS, we want the memory location (AllocaInst)
        } else {
            if (llvm::AllocaInst* alloca = llvm::dyn_cast<llvm::AllocaInst>(val)) {
                llvm::Type* allocaType = alloca->getAllocatedType();
                if (allocaType->isArrayTy() || allocaType->isFunctionTy()) {
                    // For arrays or functions, the identifier itself can represent the pointer to the first element/function.
                    // However, if you need to pass the array/function by value (which is not typical for arrays in C-like languages)
                    // or get a pointer to the alloca itself, this logic might change.
                    // For now, let's assume decaying to pointer is handled by GEPs or specific call semantics.
                    // If we need a pointer to the array (e.g. `&arr`), AddrOfExpression should handle it.
                    // Let's return the AllocaInst itself, and GEPs will take care of it.
                    // Or, more commonly, a GEP with index 0,0 to get a pointer to the first element.
                    // For simplicity, if it's an array, let's return the pointer to the first element.
                    // This is a common implicit conversion.
                    if (allocaType->isArrayTy()) {
                        llvm::Value* gepIndices[] = {
                            llvm::ConstantInt::get(int64Type, 0),
                            llvm::ConstantInt::get(int64Type, 0)
                        };
                        m_currentLLVMValue = builder->CreateInBoundsGEP(allocaType, alloca, gepIndices, node->name + ".ptr");
                    } else {
                        m_currentLLVMValue = builder->CreateLoad(allocaType, val, node->name + ".load");
                    }
                } else {
                    m_currentLLVMValue = builder->CreateLoad(allocaType, val, node->name + ".load");
                }
            } else {
                // If it's not an AllocaInst, it might be a global variable or a function argument (already a Value*)
                m_currentLLVMValue = val;
            }
        }
    } else {
        logError(node->loc, "Identifier '" + node->name + "' not found.");
        m_currentLLVMValue = nullptr;
    }
}

void LLVMCodegen::visit(vyn::ast::IntegerLiteral* node) {
    llvm::Type* intType = int64Type; // Vyn's default integer type
    m_currentLLVMValue = llvm::ConstantInt::get(intType, node->value, true); // true for signed
}

void LLVMCodegen::visit(vyn::ast::FloatLiteral* node) {
    llvm::Type* floatType = doubleType; // Vyn's default float type
    m_currentLLVMValue = llvm::ConstantFP::get(floatType, node->value);
}

void LLVMCodegen::visit(vyn::ast::BooleanLiteral* node) {
    m_currentLLVMValue = llvm::ConstantInt::get(int1Type, node->value ? 1 : 0, false);
}

void LLVMCodegen::visit(vyn::ast::StringLiteral* node) {
    // Create a global string constant for the string literal's value.
    // The result is a pointer to the first character (i8*).
    m_currentLLVMValue = builder->CreateGlobalStringPtr(node->value, ".str");
}

void LLVMCodegen::visit(vyn::ast::NilLiteral* node) {
    // Nil is a polymorphic null pointer. For now, default to i8*.
    // Type inference or context should ideally provide a more specific pointer type.
    if (auto* pType = llvm::dyn_cast<llvm::PointerType>(int8PtrType)) {
        m_currentLLVMValue = llvm::ConstantPointerNull::get(pType);
    } else {
        logError(node->loc, "int8PtrType is not a PointerType, cannot create ConstantPointerNull.");
        m_currentLLVMValue = nullptr;
    }
}

void LLVMCodegen::visit(vyn::ast::ObjectLiteral* node) {
    // This requires knowing the struct type to create an instance of.
    // Assuming the type is known and is a struct.
    // This is a simplified version. Real object literals might involve constructor calls.
    logError(node->loc, "ObjectLiteral codegen is not fully implemented yet.");
    m_currentLLVMValue = nullptr;
    // Example for a known struct type:
    // llvm::StructType* structTy = ... get struct type ...;
    // llvm::Constant* agg = llvm::ConstantStruct::get(structTy, fieldValues);
    // m_currentLLVMValue = agg; 
    // Or for mutable objects, allocate memory and store fields:
    // llvm::Value* alloca = builder->CreateAlloca(structTy, nullptr, "new_object");
    // For each field:
    //   llvm::Value* fieldPtr = builder->CreateStructGEP(structTy, alloca, fieldIndex, "field.ptr");
    //   builder->CreateStore(fieldValue, fieldPtr);
    // m_currentLLVMValue = alloca; // or the loaded value if appropriate
}

void LLVMCodegen::visit(vyn::ast::ArrayLiteralNode* node) {
    if (node->elements.empty()) {
        logError(node->loc, "Array literal has no elements, cannot determine type or create instance yet.");
        // Potentially create an empty array of a default type or based on context if available.
        m_currentLLVMValue = nullptr;
        return;
    }

    // Codegen all elements first to get their LLVM values and infer type
    std::vector<llvm::Constant*> elementConstants;
    llvm::Type* elementType = nullptr;

    for (const auto& elemNode : node->elements) {
        elemNode->accept(*this);
        if (!m_currentLLVMValue) {
            logError(elemNode->loc, "Element in array literal evaluated to null.");
            m_currentLLVMValue = nullptr;
            return;
        }
        if (!llvm::isa<llvm::Constant>(m_currentLLVMValue)) {
            logError(elemNode->loc, "Array literal elements must be constants for global/static array. Dynamic arrays need heap allocation.");
            // For now, we only support constant arrays. This could be extended.
            m_currentLLVMValue = nullptr;
            return;
        }
        llvm::Constant* constVal = llvm::cast<llvm::Constant>(m_currentLLVMValue);
        elementConstants.push_back(constVal);

        if (!elementType) {
            elementType = constVal->getType();
        } else if (elementType != constVal->getType()) {
            logError(elemNode->loc, "Array literal elements must have the same type. Found " + 
                                    getTypeName(constVal->getType()) + " and " + getTypeName(elementType));
            m_currentLLVMValue = nullptr;
            return;
        }
    }

    if (!elementType) {
        logError(node->loc, "Could not determine element type for array literal.");
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::ArrayType* arrayType = llvm::ArrayType::get(elementType, elementConstants.size());
    
    // Create a global variable for the array if it's constant, or alloca if local & constant elements
    // For simplicity, let's assume it can be a constant global array for now.
    // This might need adjustment based on where the array literal can appear.
    llvm::Constant* arrayConstant = llvm::ConstantArray::get(arrayType, elementConstants);

    // If in a function, create an alloca and initialize it.
    // If global, create a global variable.
    if (currentFunction) {
        llvm::AllocaInst* alloca = builder->CreateAlloca(arrayType, nullptr, "arraylit");
        builder->CreateStore(arrayConstant, alloca);
        m_currentLLVMValue = alloca; // The alloca is the l-value for the array
    } else {
        // Global array literal
        auto* gv = new llvm::GlobalVariable(*module, arrayType, true /*isConstant*/, 
                                          llvm::GlobalValue::PrivateLinkage, arrayConstant, ".arrlit");
        m_currentLLVMValue = gv;
    }
}

// --- Expressions ---
void LLVMCodegen::visit(vyn::ast::UnaryExpression* node) {
    node->operand->accept(*this);
    llvm::Value* operandValue = m_currentLLVMValue;

    if (!operandValue) {
        logError(node->loc, "Operand of unary expression is null.");
        m_currentLLVMValue = nullptr;
        return;
    }

    switch (node->op.type) {
        case TokenType::MINUS: // Negation
            if (operandValue->getType()->isFloatingPointTy()) {
                m_currentLLVMValue = builder->CreateFNeg(operandValue, "fnegtmp");
            } else if (operandValue->getType()->isIntegerTy()) {
                m_currentLLVMValue = builder->CreateNeg(operandValue, "negtmp");
            } else {
                logError(node->op.location, "Unary '-' operator can only be applied to numeric types.");
                m_currentLLVMValue = nullptr;
            }
            break;
        case TokenType::BANG: // Logical NOT
            // Ensure operand is boolean (i1) or can be converted
            if (operandValue->getType() != int1Type) {
                // Convert to boolean: value != 0 for integers, value != 0.0 for floats, ptr != null for pointers
                if (operandValue->getType()->isIntegerTy()) {
                    operandValue = builder->CreateICmpNE(operandValue, llvm::ConstantInt::get(operandValue->getType(), 0), "tobool");
                } else if (operandValue->getType()->isFloatingPointTy()) {
                    operandValue = builder->CreateFCmpONE(operandValue, llvm::ConstantFP::get(operandValue->getType(), 0.0), "tobool"); // ONE: ordered, not equal
                } else if (operandValue->getType()->isPointerTy()) {
                    operandValue = builder->CreateIsNotNull(operandValue, "tobool");
                } else {
                    logError(node->op.location, "Unary '!' operator applied to non-boolean convertible type: " + getTypeName(operandValue->getType()));
                    m_currentLLVMValue = nullptr;
                    return;
                }
            }
            m_currentLLVMValue = builder->CreateNot(operandValue, "nottmp"); // LLVM's 'not' is bitwise, for i1 it's logical NOT
            break;
        // Add cases for other unary operators like TILDE (bitwise NOT) if Vyn supports them
        default:
            logError(node->op.location, "Unsupported unary operator: " + node->op.lexeme);
            m_currentLLVMValue = nullptr;
    }
}

void LLVMCodegen::visit(vyn::ast::BinaryExpression* node) {
    // Special handling for short-circuiting logical operators
    if (node->op.type == TokenType::AND) {
        llvm::Function* parentFunc = builder->GetInsertBlock()->getParent();
        llvm::BasicBlock* startBB = builder->GetInsertBlock();
        llvm::BasicBlock* secondEvalBB = llvm::BasicBlock::Create(*context, "and.rhs", parentFunc);
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(*context, "and.end", parentFunc);

        node->left->accept(*this);
        llvm::Value* leftVal = m_currentLLVMValue;
        if (!leftVal) { logError(node->left->loc, "LHS of AND is null"); m_currentLLVMValue = nullptr; return; }
        if (leftVal->getType() != int1Type) { // Convert to bool if not already
            leftVal = builder->CreateICmpNE(leftVal, llvm::Constant::getNullValue(leftVal->getType()), "tobool.lhs");
        }
        
        // Conditional branch: if left is false, jump to endBB, else evaluate right
        builder->CreateCondBr(leftVal, secondEvalBB, endBB);

        builder->SetInsertPoint(secondEvalBB);
        node->right->accept(*this);
        llvm::Value* rightVal = m_currentLLVMValue;
        if (!rightVal) { logError(node->right->loc, "RHS of AND is null"); m_currentLLVMValue = nullptr; return; }
         if (rightVal->getType() != int1Type) { // Convert to bool if not already
            rightVal = builder->CreateICmpNE(rightVal, llvm::Constant::getNullValue(rightVal->getType()), "tobool.rhs");
        }
        llvm::BasicBlock* rhsEndBB = builder->GetInsertBlock(); // BB where RHS evaluation finished
        builder->CreateBr(endBB);

        builder->SetInsertPoint(endBB);
        llvm::PHINode* phi = builder->CreatePHI(int1Type, 2, "and.res");
        phi->addIncoming(llvm::ConstantInt::get(int1Type, 0), startBB); // If left was false
        phi->addIncoming(rightVal, rhsEndBB); // Value of right if left was true
        m_currentLLVMValue = phi;
        return;
    }

    if (node->op.type == TokenType::OR) {
        llvm::Function* parentFunc = builder->GetInsertBlock()->getParent();
        llvm::BasicBlock* startBB = builder->GetInsertBlock();
        llvm::BasicBlock* secondEvalBB = llvm::BasicBlock::Create(*context, "or.rhs", parentFunc);
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(*context, "or.end", parentFunc);

        node->left->accept(*this);
        llvm::Value* leftVal = m_currentLLVMValue;
        if (!leftVal) { logError(node->left->loc, "LHS of OR is null"); m_currentLLVMValue = nullptr; return; }
        if (leftVal->getType() != int1Type) { // Convert to bool if not already
            leftVal = builder->CreateICmpNE(leftVal, llvm::Constant::getNullValue(leftVal->getType()), "tobool.lhs");
        }

        // Conditional branch: if left is true, jump to endBB, else evaluate right
        builder->CreateCondBr(leftVal, endBB, secondEvalBB);

        builder->SetInsertPoint(secondEvalBB);
        node->right->accept(*this);
        llvm::Value* rightVal = m_currentLLVMValue;
        if (!rightVal) { logError(node->right->loc, "RHS of OR is null"); m_currentLLVMValue = nullptr; return; }
        if (rightVal->getType() != int1Type) { // Convert to bool if not already
            rightVal = builder->CreateICmpNE(rightVal, llvm::Constant::getNullValue(rightVal->getType()), "tobool.rhs");
        }
        llvm::BasicBlock* rhsEndBB = builder->GetInsertBlock(); // BB where RHS evaluation finished
        builder->CreateBr(endBB);

        builder->SetInsertPoint(endBB);
        llvm::PHINode* phi = builder->CreatePHI(int1Type, 2, "or.res");
        phi->addIncoming(llvm::ConstantInt::get(int1Type, 1), startBB); // If left was true
        phi->addIncoming(rightVal, rhsEndBB);    // Value of right if left was false
        m_currentLLVMValue = phi;
        return;
    }

    // Standard binary operation (non-short-circuiting)
    node->left->accept(*this);
    llvm::Value* L = m_currentLLVMValue;
    node->right->accept(*this);
    llvm::Value* R = m_currentLLVMValue;

    if (!L || !R) {
        logError(node->op.location, "One or both operands of binary expression are null.");
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::Type* lType = L->getType();
    llvm::Type* rType = R->getType();

    // Type checking and promotion might be needed here.
    // For simplicity, assume types are compatible or error out.
    // A more robust system would implement type promotion rules (e.g. int + float -> float)

    // Handle pointer arithmetic
    if (lType->isPointerTy() && rType->isIntegerTy()) {
        if (node->op.type == TokenType::PLUS || node->op.type == TokenType::MINUS) {
            llvm::PointerType* lPtrType = llvm::dyn_cast<llvm::PointerType>(lType);
            llvm::Type* elementType = lPtrType->getArrayElementType();
            if (node->op.type == TokenType::MINUS) {
                R = builder->CreateNeg(R, "negidx");
            }
            m_currentLLVMValue = builder->CreateGEP(elementType, L, R, "ptradd");
            return;
        }
    } else if (lType->isIntegerTy() && rType->isPointerTy()) {
        if (node->op.type == TokenType::PLUS) { // Only Int + Ptr, not Int - Ptr usually
            llvm::PointerType* rPtrType = llvm::dyn_cast<llvm::PointerType>(rType);
            llvm::Type* elementType = rPtrType->getArrayElementType();
            m_currentLLVMValue = builder->CreateGEP(elementType, R, L, "ptradd");
            return;
        }
    } else if (lType->isPointerTy() && rType->isPointerTy()) {
        if (node->op.type == TokenType::MINUS) { // Ptr - Ptr
            llvm::Type* intPtrTy = int64Type; // Or use DataLayout to get pointer size
            llvm::Value* lPtrInt = builder->CreatePtrToInt(L, intPtrTy, "lptrint");
            llvm::Value* rPtrInt = builder->CreatePtrToInt(R, intPtrTy, "rptrint");
            llvm::Value* diffBytes = builder->CreateSub(lPtrInt, rPtrInt, "ptrdiffbytes");
            
            // Result of pointer subtraction is number of elements, so divide by element size
            llvm::PointerType* lPtrType = llvm::dyn_cast<llvm::PointerType>(lType);
            llvm::Type* elementType = lPtrType->getArrayElementType();
            if (elementType->isSized()) {
                 llvm::DataLayout dl(module.get());
                 uint64_t elementSize = dl.getTypeAllocSize(elementType);
                 if (elementSize > 0) {
                    llvm::Value* sizeVal = llvm::ConstantInt::get(intPtrTy, elementSize);
                    m_currentLLVMValue = builder->CreateSDiv(diffBytes, sizeVal, "ptrdiffelem");
                 } else {
                     logError(node->op.location, "Pointer subtraction with zero-sized element type.");
                     m_currentLLVMValue = diffBytes; // Fallback to byte difference
                 }
            } else {
                 logError(node->op.location, "Pointer subtraction with unsized element type.");
                 m_currentLLVMValue = diffBytes; // Fallback to byte difference
            }
            return;
        }
        // For pointer comparisons, they are handled by ICmp below if types match, or after PtrToInt if not.
    }


    // Standard arithmetic and comparison operations
    switch (node->op.type) {
        case TokenType::PLUS:
            if (L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy()) {
                // Promote to float if one is float (simple rule, needs proper type system)
                // This assumes L and R are already compatible or one is float, one is int
                // A real system would have explicit cast nodes or implicit cast rules.
                if (L->getType()->isIntegerTy()) L = builder->CreateSIToFP(L, doubleType, "itofp");
                if (R->getType()->isIntegerTy()) R = builder->CreateSIToFP(R, doubleType, "itofp");
                m_currentLLVMValue = builder->CreateFAdd(L, R, "faddtmp");
            } else {
                m_currentLLVMValue = builder->CreateAdd(L, R, "addtmp");
            }
            break;
        case TokenType::MINUS:
            if (L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy()) {
                if (L->getType()->isIntegerTy()) L = builder->CreateSIToFP(L, doubleType, "itofp");
                if (R->getType()->isIntegerTy()) R = builder->CreateSIToFP(R, doubleType, "itofp");
                m_currentLLVMValue = builder->CreateFSub(L, R, "fsubtmp");
            } else {
                m_currentLLVMValue = builder->CreateSub(L, R, "subtmp");
            }
            break;
        case TokenType::MULTIPLY:
            if (L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy()) {
                if (L->getType()->isIntegerTy()) L = builder->CreateSIToFP(L, doubleType, "itofp");
                if (R->getType()->isIntegerTy()) R = builder->CreateSIToFP(R, doubleType, "itofp");
                m_currentLLVMValue = builder->CreateFMul(L, R, "fmultmp");
            } else {
                m_currentLLVMValue = builder->CreateMul(L, R, "multmp");
            }
            break;
        case TokenType::DIVIDE:
            if (L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy()) {
                if (L->getType()->isIntegerTy()) L = builder->CreateSIToFP(L, doubleType, "itofp");
                if (R->getType()->isIntegerTy()) R = builder->CreateSIToFP(R, doubleType, "itofp");
                m_currentLLVMValue = builder->CreateFDiv(L, R, "fdivtmp");
            } else { // Assuming signed division for integers
                m_currentLLVMValue = builder->CreateSDiv(L, R, "sdivtmp");
            }
            break;
        case TokenType::MODULO: // Typically integer-only
            if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
                m_currentLLVMValue = builder->CreateSRem(L, R, "sremtmp"); // Signed remainder
            } else {
                logError(node->op.location, "Modulo operator requires integer operands.");
                m_currentLLVMValue = nullptr;
            }
            break;
        // Comparisons
        case TokenType::EQEQ: // ==
            if (L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy()) {
                if (L->getType()->isIntegerTy()) L = builder->CreateSIToFP(L, doubleType, "itofp");
                if (R->getType()->isIntegerTy()) R = builder->CreateSIToFP(R, doubleType, "itofp");
                m_currentLLVMValue = builder->CreateFCmpOEQ(L, R, "fcmpeq"); // Ordered, Equal
            } else if (L->getType()->isPointerTy() && R->getType()->isPointerTy()) {
                 m_currentLLVMValue = builder->CreateICmpEQ(L, R, "ptrcmpeq");
            } else if (L->getType()->isPointerTy() && R->getType()->isIntegerTy() && llvm::cast<llvm::ConstantInt>(R)->isZero()) {
                 m_currentLLVMValue = builder->CreateICmpEQ(L, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(L->getType())), "ptrcmpnull");
            } else if (L->getType()->isIntegerTy() && R->getType()->isPointerTy() && llvm::cast<llvm::ConstantInt>(L)->isZero()) {
                 m_currentLLVMValue = builder->CreateICmpEQ(R, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(R->getType())), "ptrcmpnull");
            } else {
                m_currentLLVMValue = builder->CreateICmpEQ(L, R, "icmpeq");
            }
            break;
        case TokenType::NOTEQ: // !=
            if (L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy()) {
                if (L->getType()->isIntegerTy()) L = builder->CreateSIToFP(L, doubleType, "itofp");
                if (R->getType()->isIntegerTy()) R = builder->CreateSIToFP(R, doubleType, "itofp");
                m_currentLLVMValue = builder->CreateFCmpONE(L, R, "fcmpne"); // Ordered, Not Equal
            } else if (L->getType()->isPointerTy() && R->getType()->isPointerTy()) {
                 m_currentLLVMValue = builder->CreateICmpNE(L, R, "ptrcmpne");
            } else if (L->getType()->isPointerTy() && R->getType()->isIntegerTy() && llvm::cast<llvm::ConstantInt>(R)->isZero()) {
                 m_currentLLVMValue = builder->CreateICmpNE(L, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(L->getType())), "ptrcmpnotnull");
            } else if (L->getType()->isIntegerTy() && R->getType()->isPointerTy() && llvm::cast<llvm::ConstantInt>(L)->isZero()) {
                 m_currentLLVMValue = builder->CreateICmpNE(R, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(R->getType())), "ptrcmpnotnull");
            } else {
                m_currentLLVMValue = builder->CreateICmpNE(L, R, "icmpne");
            }
            break;
        case TokenType::LT: // <
            if (L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy()) {
                if (L->getType()->isIntegerTy()) L = builder->CreateSIToFP(L, doubleType, "itofp");
                if (R->getType()->isIntegerTy()) R = builder->CreateSIToFP(R, doubleType, "itofp");
                m_currentLLVMValue = builder->CreateFCmpOLT(L, R, "fcmplt"); // Ordered, Less Than
            } else { // Assuming signed comparison for integers
                m_currentLLVMValue = builder->CreateICmpSLT(L, R, "icmpslt");
            }
            break;
        case TokenType::LTEQ: // <=
            if (L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy()) {
                if (L->getType()->isIntegerTy()) L = builder->CreateSIToFP(L, doubleType, "itofp");
                if (R->getType()->isIntegerTy()) R = builder->CreateSIToFP(R, doubleType, "itofp");
                m_currentLLVMValue = builder->CreateFCmpOLE(L, R, "fcmple"); // Ordered, Less Than or Equal
            } else { // Assuming signed comparison
                m_currentLLVMValue = builder->CreateICmpSLE(L, R, "icmpsle");
            }
            break;
        case TokenType::GT: // >
            if (L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy()) {
                if (L->getType()->isIntegerTy()) L = builder->CreateSIToFP(L, doubleType, "itofp");
                if (R->getType()->isIntegerTy()) R = builder->CreateSIToFP(R, doubleType, "itofp");
                m_currentLLVMValue = builder->CreateFCmpOGT(L, R, "fcmpgt"); // Ordered, Greater Than
            } else { // Assuming signed comparison
                m_currentLLVMValue = builder->CreateICmpSGT(L, R, "icmpsgt");
            }
            break;
        case TokenType::GTEQ: // >=
            if (L->getType()->isFloatingPointTy() || R->getType()->isFloatingPointTy()) {
                if (L->getType()->isIntegerTy()) L = builder->CreateSIToFP(L, doubleType, "itofp");
                if (R->getType()->isIntegerTy()) R = builder->CreateSIToFP(R, doubleType, "itofp");
                m_currentLLVMValue = builder->CreateFCmpOGE(L, R, "fcmpge"); // Ordered, Greater Than or Equal
            } else { // Assuming signed comparison
                m_currentLLVMValue = builder->CreateICmpSGE(L, R, "icmpsge");
            }
            break;
        // Bitwise operators (assuming integer operands)
        case TokenType::AMPERSAND: // &
            if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
                m_currentLLVMValue = builder->CreateAnd(L, R, "andtmp");
            } else {
                logError(node->op.location, "Bitwise AND operator requires integer operands."); m_currentLLVMValue = nullptr;
            }
            break;
        case TokenType::PIPE: // |
            if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
                m_currentLLVMValue = builder->CreateOr(L, R, "ortmp");
            } else {
                logError(node->op.location, "Bitwise OR operator requires integer operands."); m_currentLLVMValue = nullptr;
            }
            break;
        case TokenType::CARET: // ^ (XOR)
            if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
                m_currentLLVMValue = builder->CreateXor(L, R, "xortmp");
            } else {
                logError(node->op.location, "Bitwise XOR operator requires integer operands."); m_currentLLVMValue = nullptr;
            }
            break;
        default:
            logError(node->op.location, "Unsupported binary operator: " + node->op.lexeme);
            m_currentLLVMValue = nullptr;
    }
}

void LLVMCodegen::visit(vyn::ast::CallExpression* node) {
    node->callee->accept(*this);
    llvm::Value* calleeFunc = m_currentLLVMValue;

    if (!calleeFunc) {
        logError(node->loc, "Callee of call expression is null.");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Check if calleeFunc is actually a function pointer or a function itself
    llvm::Function* func = llvm::dyn_cast<llvm::Function>(calleeFunc);
    llvm::FunctionType* funcType = nullptr;

    if (func) {
        funcType = func->getFunctionType();
    } else if (calleeFunc->getType()->isPointerTy()) {
        llvm::PointerType* ptrTy = llvm::dyn_cast<llvm::PointerType>(calleeFunc->getType());
        llvm::Type* pointeeType = ptrTy->getArrayElementType();
        if (pointeeType && pointeeType->isFunctionTy()) { 
            funcType = llvm::dyn_cast<llvm::FunctionType>(pointeeType); 
        } else {
            logError(node->callee->loc, "Callee is a pointer but not to a function type: " + getTypeName(calleeFunc->getType()));
            m_currentLLVMValue = nullptr;
            return;
        }
    } else {
        logError(node->callee->loc, "Callee is not a function or function pointer: " + getTypeName(calleeFunc->getType()));
        m_currentLLVMValue = nullptr;
        return;
    }

    if (!funcType) {
        logError(node->callee->loc, "Could not determine function type for callee.");
        m_currentLLVMValue = nullptr;
        return;
    }

    std::vector<llvm::Value*> argValues;
    for (const auto& argNode : node->arguments) {
        argNode->accept(*this);
        if (!m_currentLLVMValue) {
            logError(argNode->loc, "Argument in call expression evaluated to null.");
            m_currentLLVMValue = nullptr;
            return;
        }
        argValues.push_back(m_currentLLVMValue);
    }

    // Verify argument count
    if (funcType->getNumParams() != argValues.size() && !funcType->isVarArg()) {
        logError(node->loc, "Incorrect number of arguments passed to function. Expected " + 
                              std::to_string(funcType->getNumParams()) + ", got " + std::to_string(argValues.size()));
        m_currentLLVMValue = nullptr;
        return;
    }

    // Verify argument types and perform implicit casts if necessary/allowed by Vyn's type system
    for (unsigned i = 0; i < argValues.size(); ++i) {
        if (i < funcType->getNumParams()) { // Check only non-vararg params
            llvm::Type* expectedType = funcType->getParamType(i);
            llvm::Type* actualType = argValues[i]->getType();
            if (expectedType != actualType) {
                // Attempt a basic cast or log error. Vyn's type system rules apply here.
                // For now, let's try a simple cast if it's a common scenario like int to float for a float param.
                // This is a placeholder for more robust type checking and casting.
                llvm::Value* castedArg = tryCast(argValues[i], expectedType, node->arguments[i]->loc);
                if (castedArg) {
                    argValues[i] = castedArg;
                } else {
                    logError(node->arguments[i]->loc, "Type mismatch for argument " + std::to_string(i+1) + 
                                                    ". Expected " + getTypeName(expectedType) + 
                                                    ", got " + getTypeName(actualType));
                    m_currentLLVMValue = nullptr;
                    return;
                }
            }
        }
    }

    m_currentLLVMValue = builder->CreateCall(funcType, calleeFunc, argValues, "calltmp");
    // If the function returns void, the call instruction itself is void typed, but m_currentLLVMValue will hold it.
    // If it's used in a context expecting a value, LLVM will complain if it's a void return.
    // This is generally fine as the IR is type-checked.
}

void LLVMCodegen::visit(vyn::ast::MemberExpression* node) {
    // LHS (object) can be an identifier, another member expression, call expression, etc.
    bool oldIsLHS = m_isLHSOfAssignment;
    m_isLHSOfAssignment = false; // Temporarily false for object evaluation
    node->object->accept(*this);
    m_isLHSOfAssignment = oldIsLHS; // Restore

    llvm::Value* objectValue = m_currentLLVMValue;
    if (!objectValue) {
        logError(node->object->loc, "Object in member expression is null.");
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::Type* objectType = objectValue->getType();
    llvm::StructType* structType = nullptr;
    llvm::Value* objectPtr = objectValue; // Assume objectValue is already a pointer to the struct

    if (objectType->isPointerTy()) {
        llvm::PointerType* objPtrType = llvm::dyn_cast<llvm::PointerType>(objectType);
        llvm::Type* pointeeType = objPtrType->getArrayElementType();
        if (pointeeType->isStructTy()) {
            structType = llvm::dyn_cast<llvm::StructType>(pointeeType);
            // objectPtr is already correct (it\\\'s the pointer to the struct)
        } else {
            logError(node->object->loc, "Member access on a pointer to non-struct type: " + getTypeName(pointeeType));
            m_currentLLVMValue = nullptr;
            return;
        }
    } else if (objectType->isStructTy()) {
        // This case implies the objectValue is the struct itself (e.g. from a function returning a struct by value).
        // GEP requires a pointer. If we have a struct value, we might need to store it to memory first.
        // This is less common for direct member access in C-like languages; usually it's through a pointer.
        // For simplicity, let's assume member access is always on a pointer to a struct.
        // If Vyn allows direct member access on struct values, this needs careful handling (e.g. alloca + store).
        logError(node->object->loc, "Direct member access on struct value not yet supported. Expected pointer to struct. Type: " + getTypeName(objectType));
        m_currentLLVMValue = nullptr;
        // structType = llvm::dyn_cast<llvm::StructType>(objectType);
        // // Need to get a pointer to this struct value, e.g., by allocating and storing it.
        // llvm::AllocaInst* tempAlloca = builder->CreateAlloca(objectType, nullptr, "obj.val.alloca");
        // builder->CreateStore(objectValue, tempAlloca);
        // objectPtr = tempAlloca;
        return;
    } else {
        logError(node->object->loc, "Member access on non-struct, non-pointer-to-struct type: " + getTypeName(objectType));
        m_currentLLVMValue = nullptr;
        return;
    }

    if (!structType) {
        logError(node->object->loc, "Could not resolve struct type for member access.");
        m_currentLLVMValue = nullptr;
        return;
    }

    std::string fieldName;
    if (auto* identNode = dynamic_cast<vyn::ast::Identifier*>(node->property.get())) {
        fieldName = identNode->name;
    } else {
        logError(node->property->loc, "Member property is not an identifier.");
        m_currentLLVMValue = nullptr;
        return;
    }
    // int fieldIndex = getStructFieldIndex(structType, fieldName); // Original line
    int fieldIndex = getStructFieldIndex(structType, fieldName); // Corrected: removed third argument


    if (fieldIndex == -1) {
        logError(node->property->loc, "Field '" + fieldName + "' not found in struct " + 
                                    (structType->hasName() ? structType->getName().str() : "<anonymous>"));
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::Value* fieldPtr = builder->CreateStructGEP(structType, objectPtr, fieldIndex, "fieldptr."+fieldName);

    if (m_isLHSOfAssignment) {
        m_currentLLVMValue = fieldPtr; // For LHS of assignment, we want the address
    } else {
        // For RHS, load the value from the field's address
        llvm::Type* fieldType = structType->getElementType(fieldIndex);
        if (fieldType->isArrayTy() || fieldType->isFunctionTy()) {
            // If the field is an array or function, GEP itself is often the desired value (pointer)
            m_currentLLVMValue = fieldPtr;
        } else {
            m_currentLLVMValue = builder->CreateLoad(fieldType, fieldPtr, "fieldval."+fieldName);
        }
    }
}

void LLVMCodegen::visit(vyn::ast::AssignmentExpression* node) {
    // Evaluate RHS first
    bool oldIsLHS = m_isLHSOfAssignment;
    m_isLHSOfAssignment = false;
    node->right->accept(*this);
    llvm::Value* rhsValue = m_currentLLVMValue;
    m_isLHSOfAssignment = oldIsLHS;

    if (!rhsValue) {
        logError(node->right->loc, "RHS of assignment evaluated to null.");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate LHS to get the pointer to store to
    m_isLHSOfAssignment = true;
    node->left->accept(*this);
    llvm::Value* lhsPtr = m_currentLLVMValue;
    m_isLHSOfAssignment = false; // Reset after LHS evaluation

    if (!lhsPtr) {
        logError(node->left->loc, "LHS of assignment is not a valid l-value or evaluated to null.");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Ensure lhsPtr is actually a pointer
    if (!lhsPtr->getType()->isPointerTy()) {
        logError(node->left->loc, "LHS of assignment is not a pointer. Type: " + getTypeName(lhsPtr->getType()));
        m_currentLLVMValue = nullptr;
        return;
    }
    
    auto lhsPtrType = llvm::dyn_cast<llvm::PointerType>(lhsPtr->getType());
    if (!lhsPtrType) {
        logError(node->left->loc, "LHS of assignment is not a pointer type.");
        m_currentLLVMValue = nullptr;
        return;
    }
    llvm::Type* targetElementType = lhsPtrType->getArrayElementType();

    // Type check and cast RHS if necessary
    if (targetElementType != rhsValue->getType()) {
        llvm::Value* castedRHS = tryCast(rhsValue, targetElementType, node->right->loc);
        if (castedRHS) {
            rhsValue = castedRHS;
        } else {
            logError(node->loc, "Type mismatch in assignment. Cannot assign type " + 
                                getTypeName(rhsValue->getType()) + " to " + getTypeName(targetElementType));
            m_currentLLVMValue = nullptr;
            return;
        }
    }

    builder->CreateStore(rhsValue, lhsPtr);
    m_currentLLVMValue = rhsValue; // The value of an assignment expression is the assigned value
}

void LLVMCodegen::visit(vyn::ast::BorrowExprNode* node) {
    // '&expr' or 'borrow expr'
    // This should evaluate 'expr' to get an l-value (a memory location)
    // and then the result of BorrowExprNode is the address itself.
    bool oldIsLHS = m_isLHSOfAssignment;
    m_isLHSOfAssignment = true; // We need the address of the operand
    node->expression->accept(*this);
    m_isLHSOfAssignment = oldIsLHS; // Restore

    llvm::Value* operandAddr = m_currentLLVMValue;
    if (!operandAddr) {
        logError(node->loc, "Expression for borrow operator did not yield a valid address.");
        m_currentLLVMValue = nullptr;
        return;
    }

    if (!operandAddr->getType()->isPointerTy()) {
        logError(node->loc, "Borrow operator applied to non-addressable expression (not a pointer). Type: " + getTypeName(operandAddr->getType()));
        // This can happen if 'operandAddr' is a temporary value, not an AllocaInst or GEP result.
        m_currentLLVMValue = nullptr;
        return;
    }
    
    // The result of `&x` is the address of x, which is what `operandAddr` should already be.
    m_currentLLVMValue = operandAddr;
}

void LLVMCodegen::visit(vyn::ast::PointerDerefExpression* node) {
    // '*expr'
    node->pointer->accept(*this); // Changed from node->expression
    llvm::Value* ptrValue = m_currentLLVMValue;

    if (!ptrValue) {
        logError(node->loc, "Expression for pointer dereference is null.");
        m_currentLLVMValue = nullptr;
        return;
    }

    if (!ptrValue->getType()->isPointerTy()) {
        logError(node->loc, "Cannot dereference non-pointer type: " + getTypeName(ptrValue->getType()));
        m_currentLLVMValue = nullptr;
        return;
    }

    auto ptrTy = llvm::dyn_cast<llvm::PointerType>(ptrValue->getType());
    llvm::Type* pointeeType = ptrTy->getArrayElementType();

    if (m_isLHSOfAssignment) {
        // If on LHS of assignment (*p = val), the result is the pointer itself.
        m_currentLLVMValue = ptrValue;
    } else {
        // If on RHS (val = *p), load the value from the pointer.
        // Check for void* dereference, which is invalid.
        if (pointeeType->isVoidTy()) {
            logError(node->loc, "Cannot dereference void pointer.");
            m_currentLLVMValue = nullptr;
            return;
        }
        m_currentLLVMValue = builder->CreateLoad(pointeeType, ptrValue, "deref");
    }
}

void LLVMCodegen::visit(vyn::ast::AddrOfExpression* node) {
    // This is similar to BorrowExprNode, often used for taking address of functions or globals.
    // For variables, `visit(Identifier)` when `m_isLHSOfAssignment` is true already gives the AllocaInst (address).
    // This node might be more explicit or used in contexts where `m_isLHSOfAssignment` isn't the right signal.

    // Let's assume AddrOfExpression explicitly requests the address of its operand.
    // The operand should resolve to something that has an address.

    // If operand is an Identifier for a variable:
    if (auto* idNode = dynamic_cast<ast::Identifier*>(node->getLocation().get())) { // Changed from node->expression
        auto it = namedValues.find(idNode->name);
        if (it != namedValues.end()) {
            llvm::Value* val = it->second;
            // If it's an AllocaInst (local var) or GlobalVariable, it's already an address.
            if (llvm::isa<llvm::AllocaInst>(val) || llvm::isa<llvm::GlobalVariable>(val)) {
                m_currentLLVMValue = val;
                return;
            }
            // If it's a function, its value is already its address.
            if (llvm::isa<llvm::Function>(val)) {
                m_currentLLVMValue = val;
                return;
            }
            logError(node->loc, "Operand of AddrOf ('@') is not an addressable entity (variable, function): " + idNode->name);
            m_currentLLVMValue = nullptr; return;
        } else {
            logError(idNode->loc, "Identifier '" + idNode->name + "' not found for AddrOf operator.");
            m_currentLLVMValue = nullptr; return;
        }
    }

    // If operand is a MemberExpression (e.g. @foo.bar):
    if (auto* memberNode = dynamic_cast<ast::MemberExpression*>(node->getLocation().get())) { // Changed from node->expression
        bool oldIsLHS = m_isLHSOfAssignment;
        m_isLHSOfAssignment = true; // We want the address from the member expression
        memberNode->accept(*this);      // This should set m_currentLLVMValue to the GEP result (address)
        m_isLHSOfAssignment = oldIsLHS;
        // m_currentLLVMValue is already set by the MemberExpression visitor when m_isLHSOfAssignment is true.
        return;
    }
    
    // Fallback: try to evaluate the expression and see if it's an l-value pointer already.
    // This might be too generic or unsafe without more context on what AddrOf can apply to.
    bool oldIsLHS = m_isLHSOfAssignment;
    m_isLHSOfAssignment = true; 
    node->getLocation()->accept(*this); // Changed from node->expression
    m_isLHSOfAssignment = oldIsLHS;
    
    if (!m_currentLLVMValue || !m_currentLLVMValue->getType()->isPointerTy()){
        logError(node->loc, "AddrOf operator must be applied to an l-value (e.g. variable, field). Got: " + (m_currentLLVMValue ? getTypeName(m_currentLLVMValue->getType()) : "null"));
        m_currentLLVMValue = nullptr;
        return;
    }
    // The value is already the address we need.
}

void LLVMCodegen::visit(vyn::ast::FromIntToLocExpression* node) {
    // This is a Vyn-specific intrinsic or cast, e.g., `loc(some_integer)`
    // It converts an integer to a 'location' type, which might be an opaque pointer or a specific struct.
    // For now, let's assume 'location' is represented as i8* (generic pointer).
    node->getAddress()->accept(*this); // Changed from node->expression
    llvm::Value* intValue = m_currentLLVMValue;
    if (!intValue) {
        logError(node->loc, "Integer expression for loc() conversion is null.");
        m_currentLLVMValue = nullptr;
        return;
    }

    if (!intValue->getType()->isIntegerTy()) {
        logError(node->loc, "loc() conversion expects an integer argument, got " + getTypeName(intValue->getType()));
        m_currentLLVMValue = nullptr;
        return;
    }

    // Cast the integer to a generic pointer type (e.g., i8*).
    // Ensure the integer is wide enough if Vyn's 'location' implies a certain pointer size (e.g., 64-bit).
    // If intValue is smaller than pointer size, it might need zero/sign extension first.
    // For simplicity, directly use inttoptr.
    m_currentLLVMValue = builder->CreateIntToPtr(intValue, int8PtrType, "inttoloc");
}

void LLVMCodegen::visit(vyn::ast::ArrayElementExpression* node) {
    // object[index]
    // Evaluate the object (array/pointer)
    bool oldIsLHS = m_isLHSOfAssignment;
    m_isLHSOfAssignment = false; // Evaluate object as RHS first
    node->object->accept(*this);
    llvm::Value* arrayOrPtrValue = m_currentLLVMValue;
    m_isLHSOfAssignment = oldIsLHS;

    if (!arrayOrPtrValue) {
        logError(node->object->loc, "Object in array element expression is null.");
        m_currentLLVMValue = nullptr;
        return;
    }

    // Evaluate the index
    node->index->accept(*this);
    llvm::Value* indexValue = m_currentLLVMValue;
    if (!indexValue) {
        logError(node->index->loc, "Index in array element expression is null.");
        m_currentLLVMValue = nullptr;
        return;
    }
    if (!indexValue->getType()->isIntegerTy()) {
        logError(node->index->loc, "Array index must be an integer, got " + getTypeName(indexValue->getType()));
        m_currentLLVMValue = nullptr;
        return;
    }

    llvm::Type* arrayOrPtrType = arrayOrPtrValue->getType();
    llvm::Value* elementPtr = nullptr;

    if (arrayOrPtrType->isPointerTy()) {
        // It\\\'s a pointer, e.g. ptr<T> or an array decayed to pointer.
        llvm::PointerType* ptrTy = llvm::dyn_cast<llvm::PointerType>(arrayOrPtrType);
        llvm::Type* elementType = ptrTy->getArrayElementType();
        // GEP on pointer: ptr + index
        elementPtr = builder->CreateGEP(elementType, arrayOrPtrValue, indexValue, "ptrelemptr");
    } else if (arrayOrPtrType->isArrayTy()) {
        // It's an actual LLVM array type (likely from an AllocaInst for a local array, or a global array).
        // GEP on array: array, 0, index  (the 0 is for the first dimension of the array pointer itself)
        // This assumes arrayOrPtrValue is a pointer to an array (e.g. AllocaInst* or GlobalVariable*)
        // If arrayOrPtrValue was a loaded array value (not typical), this would be different.
        llvm::ArrayType* arrayTy = llvm::dyn_cast<llvm::ArrayType>(arrayOrPtrType);
        llvm::Type* elementType = arrayTy->getElementType();
        llvm::Value* gepIndices[] = {
            llvm::ConstantInt::get(int64Type, 0), // First index for the pointer to the array itself
            indexValue                            // Second index for the element within the array
        };
        // The GEP instruction needs a pointer to the array, not the array type itself for the first arg.
        // If arrayOrPtrValue is an AllocaInst, its type is PointerType(ArrayType).
        // If arrayOrPtrValue is a GlobalVariable, its type is PointerType(ArrayType).
        // This means arrayOrPtrValue is already the correct pointer for GEP.
        // The first argument to CreateInBoundsGEP should be the type of the elements *within* the aggregate structure being indexed.
        // For an array `[10 x i32]* %arr_ptr`, GEP %arr_ptr, i64 0, i64 %idx. The type is `[10 x i32]`.
        // No, the first type argument to GEP is the *pointee type* of the base pointer if the base pointer is being dereferenced one level.
        // So, if arrayOrPtrValue is PtrToArrType, its pointee is ArrType. This is what GEP needs.
        // llvm::PointerType* arrayPointerType = llvm::dyn_cast<llvm::PointerType>(arrayOrPtrValue->getType());
        if (!arrayOrPtrValue->getType()->isPointerTy()) { // Check if base is a pointer
             logError(node->object->loc, "Array value is not a pointer to array. Type: " + getTypeName(arrayOrPtrValue->getType()));
             m_currentLLVMValue = nullptr;
             return;
        }
        llvm::PointerType* arrayPtrType = llvm::dyn_cast<llvm::PointerType>(arrayOrPtrValue->getType());
        llvm::Type* arrayAggregateType = arrayPtrType->getArrayElementType(); 
        elementPtr = builder->CreateInBoundsGEP(arrayAggregateType, arrayOrPtrValue, gepIndices, "arrayelemptr");

    } else {
        logError(node->object->loc, "Array access on non-array, non-pointer type: " + getTypeName(arrayOrPtrType));
        m_currentLLVMValue = nullptr;
        return;
    }

    if (m_isLHSOfAssignment) {
        m_currentLLVMValue = elementPtr; // For LHS, we want the address of the element
    } else {
        // For RHS, load the value from the element\'s address
        llvm::Type* actualElementType = nullptr;
        if (elementPtr->getType()->isPointerTy()){ 
            llvm::PointerType* gepResultPtrTy = llvm::dyn_cast<llvm::PointerType>(elementPtr->getType());
            actualElementType = gepResultPtrTy->getArrayElementType();
        } else {
             logError(node->loc, "GEP result is not a pointer, cannot load. GEP Type: " + getTypeName(elementPtr->getType()));
             m_currentLLVMValue = nullptr;
             return;
        }

        if (actualElementType->isArrayTy() || actualElementType->isFunctionTy()) {
            // If the element itself is an array or function, the GEP result (pointer) is often the desired value.
            m_currentLLVMValue = elementPtr;
        } else {
            m_currentLLVMValue = builder->CreateLoad(actualElementType, elementPtr, "arrayelemval");
        }
    }
}

void LLVMCodegen::visit(vyn::ast::LocationExpression* node) {
    // Represents a memory location, potentially with an offset.
    // E.g., `loc(base_addr) + offset` or a symbolic location.
    // This is highly dependent on Vyn's memory model and 'location' type semantics.
    // For now, assume it evaluates to some form of pointer.
    logError(node->loc, "LocationExpression codegen is not fully implemented.");
    // Placeholder: treat as if it's an identifier that should resolve to a pointer.
    // if (node->identifier) { // Assuming LocationExpression has an identifier part
    //    node->identifier->accept(*this);
    // }
    m_currentLLVMValue = nullptr;
}

void LLVMCodegen::visit(vyn::ast::ListComprehension* node) {
    // [expr for item in iterable if condition]
    // This is a complex expression that involves loop generation and list building.
    // It's often desugared into a loop that appends to a new list.
    logError(node->loc, "ListComprehension codegen is not implemented yet.");
    m_currentLLVMValue = nullptr;
    // Basic sketch:
    // 1. Create a new empty list (e.g., std::vector in C++ or runtime list type).
    // 2. Codegen the 'iterable' expression.
    // 3. Create a loop that iterates over 'iterable'.
    //    - In each iteration, assign current item to 'item' variable (scoped).
    //    - If 'condition' exists, evaluate it.
    //    - If condition is true (or no condition), evaluate 'expr'.
    //    - Append result of 'expr' to the new list.
    // 4. The value of the comprehension is the new list.
    // This requires runtime support for lists and iteration protocols.
}

