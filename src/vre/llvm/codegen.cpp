#include "vyn/vre/llvm/codegen.hpp"
#include "vyn/parser/ast.hpp" // Included via codegen.hpp, but good for clarity
#include "vyn/parser/token.hpp"
#include "vyn/parser/source_location.hpp" // For vyn::SourceLocation

#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/BasicBlock.h" // Added for llvm::BasicBlock
#include "llvm/IR/LLVMContext.h" // Added for llvm::LLVMContext
#include "llvm/IR/DerivedTypes.h" // Added for PointerType, StructType etc.

#include <cassert>
#include <iostream> // For logError
#include <stack>    // For blockStack, loopStack (if used, though direct members are simpler for now)

using namespace vyn;

// Error Logging Utility
void LLVMCodegen::logError(const SourceLocation& loc, const std::string& message) {
    // Basic error logging to stderr. Can be expanded (e.g., collect errors).\
    std::cerr << "Error at " << (loc.file ? loc.file : "unknown_file") << ":" << loc.line << ":" << loc.column << ": " << message << std::endl;
}

// --- RTTI Helper Implementations ---
llvm::StructType* LLVMCodegen::getOrCreateRTTIStructType() {
    if (rttiStructType) return rttiStructType;

    // Define a simple RTTI structure: { typeId: i32, typeName: i8* }
    std::vector<llvm::Type*> rttiFields = {
        int32Type,   // typeId / flags
        int8PtrType  // typeName (char*)
    };
    // Create the struct type but don't add it to the module's type table yet if it's a literal type.
    // If it's identified, it will be added when created.
    rttiStructType = llvm::StructType::create(*context, rttiFields, "vyn.RTTI");
    return rttiStructType;
}

llvm::Value* LLVMCodegen::generateRTTIObject(const std::string& typeName, int typeId) {
    llvm::StructType* rttiTy = getOrCreateRTTIStructType();
    llvm::Constant* typeNameGlobal = builder->CreateGlobalStringPtr(typeName);
    llvm::Constant* rttiVals[] = {
        llvm::ConstantInt::get(int32Type, typeId),
        typeNameGlobal
    };
    return llvm::ConstantStruct::get(rttiTy, rttiVals);
}

// Not using this more complex version for now.
// llvm::GlobalVariable* LLVMCodegen::getOrCreateTypeDescriptor(...)

LLVMCodegen::LLVMCodegen() // Changed signature
    : context(std::make_unique<llvm::LLVMContext>()),
      // Module will be created in generate() or initialized here with a default name
      module(std::make_unique<llvm::Module>("VynModule", *this->context)), 
      builder(std::make_unique<llvm::IRBuilder<>>(*this->context)),
      currentFunction(nullptr),
      currentClassType(nullptr),
      currentBlock(nullptr)
      // blockStack, loopStack, namedValues, userTypeMap, typeParameterMap are default-initialized
{
    // Initialize basic LLVM types
    voidType = llvm::Type::getVoidTy(*this->context);
    int1Type = llvm::Type::getInt1Ty(*this->context); // Added
    int8Type = llvm::Type::getInt8Ty(*this->context);
    int32Type = llvm::Type::getInt32Ty(*this->context);
    int64Type = llvm::Type::getInt64Ty(*this->context);
    floatType = llvm::Type::getFloatTy(*this->context);
    doubleType = llvm::Type::getDoubleTy(*this->context);
    int8PtrType = llvm::PointerType::getUnqual(int8Type);

    // Initialize RTTI struct type
    rttiStructType = getOrCreateRTTIStructType(); // Ensures it\'s created

    // Create a default entry block context if needed, or manage via pushBlock/popBlock
    // For now, currentBlock is nullptr, will be set by first pushBlock.
}

LLVMCodegen::~LLVMCodegen() = default;

// Changed signature and implementation
void LLVMCodegen::generate(const std::vector<std::unique_ptr<Node>>& ast, const std::string& outputFilename) {
    // Re-initialize or set module identifier if needed, e.g., based on outputFilename
    // For now, constructor creates "VynModule". We can change its ID:
    module->setModuleIdentifier(outputFilename);

    // Create a top-level block context for global code or initial setup if necessary.
    // Or, assume first function declaration will set up its own blocks.
    // For simplicity, let\'s assume top-level nodes are self-contained or functions.
    // If we need a global "main" or entry point simulation, that would go here.

    for (const auto& node : ast) {
        if (node) {
            node->accept(*this);
        }
    }

    // Verify the module
    if (llvm::verifyModule(*module, &llvm::errs())) {
        logError(SourceLocation(), "LLVM module verification failed.");
        // Optionally, dump IR even on failure for debugging
        // dumpIR();
        return; // Or throw an exception
    }

    // Write the LLVM IR to the output file
    std::error_code EC;
    llvm::raw_fd_ostream dest(outputFilename, EC, llvm::sys::fs::OF_None);

    if (EC) {
        logError(SourceLocation(), "Could not open file: " + EC.message());
        return;
    }

    module->print(dest, nullptr);
    dest.flush();
}

void LLVMCodegen::dumpIR() const {
    if (module) {
        module->print(llvm::errs(), nullptr);
    } else {
        std::cerr << "No LLVM module available to dump." << std::endl;
    }
}


// --- Helper Method Implementations ---

// New helper function: getTypeName
std::string LLVMCodegen::getTypeName(llvm::Type* type) {
    if (!type) return "null_type";
    std::string typeStr;
    llvm::raw_string_ostream rso(typeStr);
    type->print(rso);
    return rso.str();
}

// New helper function: tryCast
llvm::Value* LLVMCodegen::tryCast(llvm::Value* value, llvm::Type* targetType, const vyn::Location& loc) {
    if (!value || !targetType) {
        logError(loc, "tryCast called with null value or targetType.");
        return nullptr;
    }

    llvm::Type* sourceType = value->getType();

    if (sourceType == targetType) {
        return value; // No cast needed
    }

    // Integer to Integer (SExt, ZExt, Trunc)
    if (sourceType->isIntegerTy() && targetType->isIntegerTy()) {
        // Assuming signed extension/truncation is desired for now.
        // Language semantics should define this more clearly (e.g. explicit casts for sign changes).
        return builder->CreateIntCast(value, targetType, true, "intcast");
    }
    // Float to Float (FPExt, FPTrunc)
    if (sourceType->isFloatingPointTy() && targetType->isFloatingPointTy()) {
        if (sourceType->getPrimitiveSizeInBits() < targetType->getPrimitiveSizeInBits()) {
            return builder->CreateFPExt(value, targetType, "fpext");
        } else {
            return builder->CreateFPTrunc(value, targetType, "fptrunc");
        }
    }
    // Integer to Float (SIToFP, UIToFP)
    if (sourceType->isIntegerTy() && targetType->isFloatingPointTy()) {
        // Assuming signed conversion. Use CreateUIToFP for unsigned.
        return builder->CreateSIToFP(value, targetType, "sitofp");
    }
    // Float to Integer (FPToSI, FPToUI)
    if (sourceType->isFloatingPointTy() && targetType->isIntegerTy()) {
        // Assuming signed conversion. Use CreateFPToUI for unsigned.
        return builder->CreateFPToSI(value, targetType, "fptosi");
    }
    // Pointer to Pointer (BitCast)
    if (sourceType->isPointerTy() && targetType->isPointerTy()) {
        return builder->CreateBitCast(value, targetType, "ptrcast");
    }
    // Pointer to Integer (PtrToInt)
    if (sourceType->isPointerTy() && targetType->isIntegerTy()) {
        return builder->CreatePtrToInt(value, targetType, "ptrtoint");
    }
    // Integer to Pointer (IntToPtr)
    if (sourceType->isIntegerTy() && targetType->isPointerTy()) {
        return builder->CreateIntToPtr(value, targetType, "inttoptr");
    }

    logError(loc, "Unsupported cast from type " + getTypeName(sourceType) + " to " + getTypeName(targetType));
    return nullptr;
}


llvm::Value* LLVMCodegen::createEntryBlockAlloca(llvm::Function* func, const std::string& varName, llvm::Type* type) {
    if (!func) {
        logError(SourceLocation(), "Cannot create alloca: currentFunction is null.");
        return nullptr;
    }
    llvm::IRBuilder<> tmpB(&func->getEntryBlock(), func->getEntryBlock().begin());
    return tmpB.CreateAlloca(type, nullptr, varName);
}

void LLVMCodegen::pushBlock(llvm::BasicBlock* bb) {
    blockStack.emplace_back(bb);
    currentBlock = &blockStack.back();
}

void LLVMCodegen::popBlock() {
    if (!blockStack.empty()) {
        blockStack.pop_back();
        if (!blockStack.empty()) {
            currentBlock = &blockStack.back();
        } else {
            currentBlock = nullptr;
        }
    } else {
        currentBlock = nullptr; // Should not happen if push/pop are balanced
        logError(SourceLocation(), "Popped an empty block stack.");
    }
}

void LLVMCodegen::setCurrentBlockValue(llvm::Value* value) {
    if (currentBlock) {
        currentBlock->value = value;
    } else {
        logError(SourceLocation(), "Cannot set value: currentBlock is null.");
    }
}

llvm::Value* LLVMCodegen::getCurrentBlockValue() {
    if (currentBlock) {
        return currentBlock->value;
    }
    logError(SourceLocation(), "Cannot get value: currentBlock is null.");
    return nullptr;
}

void LLVMCodegen::pushLoop(llvm::BasicBlock* header, llvm::BasicBlock* body, llvm::BasicBlock* update, llvm::BasicBlock* exit) {
    loopStack.push_back({header, body, update, exit});
    currentLoopContext = loopStack.back();
}

void LLVMCodegen::popLoop() {
    if (!loopStack.empty()) {
        loopStack.pop_back();
        if (!loopStack.empty()) {
            currentLoopContext = loopStack.back();
        } else {
            // Reset to a default/invalid state if necessary
            currentLoopContext = {nullptr, nullptr, nullptr, nullptr};
        }
    } else {
        logError(SourceLocation(), "Popped an empty loop stack.");
        // Reset to a default/invalid state
        currentLoopContext = {nullptr, nullptr, nullptr, nullptr};
    }
}


// --- Visitor Methods for AST Traversal ---

void LLVMCodegen::visit(Module* node) {
    for (const auto& stmt : node->body) {
        stmt->accept(*this);
    }
}

// --- Type Mapping Helper ---
llvm::Type* LLVMCodegen::codegenType(TypeNode* type) {
    if (!type) {
        logError(SourceLocation(), "codegenType called with null TypeNode");
        return nullptr;
    }

    // Check if this type has already been codegen'd and cached
    auto it_cache = m_typeCache.find(type);
    if (it_cache != m_typeCache.end()) {
        return it_cache->second;
    }

    llvm::Type* resultType = nullptr;

    switch (type->category) {
        case TypeNode::TypeCategory::IDENTIFIER:
            if (!type->name) {
                logError(type->loc, "TypeNode with IDENTIFIER category has no name");
                return nullptr;
            }
            {
                // Check generic type parameters first
                auto itTypeParam = typeParameterMap.find(type->name->name);
                if (itTypeParam != typeParameterMap.end()) {
                    resultType = codegenType(itTypeParam->second); // Recursive call
                    m_typeCache[type] = resultType;
                    return resultType;
                }
            }
            // Built-in types
            if (type->name->name == "Int" || type->name->name == "int" || type->name->name == "i64") resultType = int64Type;
            else if (type->name->name == "i32" ) resultType = int32Type;
            else if (type->name->name == "i8" ) resultType = int8Type;
            else if (type->name->name == "Bool" || type->name->name == "bool") resultType = int1Type;
            else if (type->name->name == "Float" || type->name->name == "float" || type->name->name == "f64") resultType = doubleType;
            else if (type->name->name == "f32" ) resultType = floatType;
            else if (type->name->name == "String" || type->name->name == "string") resultType = int8PtrType; // Typically char*
            else if (type->name->name == "Void" || type->name->name == "void") resultType = voidType;
            else {
                // User-defined types (structs, classes, enums)
                auto itUserType = userTypeMap.find(type->name->name);
                if (itUserType != userTypeMap.end()) {
                    if (itUserType->second.llvmType) {
                        resultType = itUserType->second.llvmType;
                    } else {
                        // This implies an opaque type or a type not yet fully defined.
                        // For pointers to such types, this is okay.
                        // If a full definition is needed, it should have been processed.
                        // Let's create/get an opaque struct type if it's a forward declaration.
                        resultType = llvm::StructType::create(*context, type->name->name);
                        // We don't store this back in userTypeMap here, definition site does.
                        logError(type->loc, "User defined type '" + type->name->name + "' used as potentially incomplete type. Returning opaque struct.");
                    }
                } else {
                    logError(type->loc, "Unknown type identifier: " + type->name->name);
                    return nullptr;
                }
            }
            break;

        case TypeNode::TypeCategory::ARRAY:
            if (!type->arrayElementType) {
                 logError(type->loc, "Array TypeNode has no element type.");
                 return nullptr;
            }
            {
                llvm::Type* elemTy = codegenType(type->arrayElementType.get());
                if (!elemTy) return nullptr;

                if (type->arraySizeExpression) {
                    // For llvm::ArrayType, a constant size is needed.
                    // This requires constant folding or semantic analysis to provide the size.
                    // For now, we'll assume it's not a fixed-size array in LLVM terms
                    // unless it's a simple integer literal (which is hard to evaluate here).
                    // Vyn arrays are likely dynamic, so represent as pointer to element type
                    // or a struct { data_ptr, length, capacity }.
                    // For simplicity, let's use T* for now, implying dynamic sizing elsewhere.
                    logError(type->loc, "Fixed-size array codegen from expression not fully supported, treating as T*.");
                    resultType = llvm::PointerType::getUnqual(elemTy);
                } else {
                    // Unsized array (e.g., `[]Int`), typically represented as a pointer or a "slice" struct.
                    // For now, T*.
                    resultType = llvm::PointerType::getUnqual(elemTy);
                }
            }
            break;

        case TypeNode::TypeCategory::TUPLE:
            {
                std::vector<llvm::Type*> elems;
                for (const auto& elemNode : type->tupleElementTypes) {
                    llvm::Type* t = codegenType(elemNode.get());
                    if (!t) return nullptr;
                    elems.push_back(t);
                }
                resultType = llvm::StructType::get(*context, elems);
            }
            break;

        case TypeNode::TypeCategory::FUNCTION_SIGNATURE:
            {
                std::vector<llvm::Type*> params;
                for (const auto& paramNode : type->functionParameters) {
                    llvm::Type* t = codegenType(paramNode.get());
                    if (!t) return nullptr;
                    params.push_back(t);
                }
                llvm::Type* retTy = type->functionReturnType ? codegenType(type->functionReturnType.get()) : voidType;
                if (!retTy) return nullptr; // codegenType for return type failed
                resultType = llvm::FunctionType::get(retTy, params, false /*isVarArg*/);
            }
            break;

        case TypeNode::TypeCategory::OWNERSHIP_WRAPPED: // my, our, their, ptr
            if (!type->wrappedType) {
                logError(type->loc, "Ownership-wrapped TypeNode has no wrapped type.");
                return nullptr;
            }
            {
                llvm::Type* wrapped = codegenType(type->wrappedType.get());
                if (!wrapped) return nullptr;
                // All ownership types are represented as pointers to the wrapped type at LLVM level.
                // The distinction is for semantic analysis and memory management logic, not LLVM type.
                resultType = llvm::PointerType::getUnqual(wrapped);
            }
            break;
        case TypeNode::TypeCategory::OPTIONAL:
             if (!type->wrappedType) {
                logError(type->loc, "Optional TypeNode has no wrapped type.");
                return nullptr;
            }
            {
                llvm::Type* wrapped = codegenType(type->wrappedType.get());
                if (!wrapped) return nullptr;
                // Optionals can be represented in a few ways:
                // 1. Pointer type (nullable pointer). Works for reference types.
                // 2. Struct { T value, bool has_value }. Works for value types.
                // For simplicity, if wrapped is a pointer, use it (it's already nullable).
                // If wrapped is not a pointer, we might need the struct approach.
                // Let's assume for now that if it's not a pointer, it's a value type
                // and we'll represent `Optional<T>` as `struct { T, i1 }`.
                if (wrapped->isPointerTy()) {
                    resultType = wrapped; // The pointer itself can be null
                } else {
                    // For non-pointer types, use struct { value: T, has_value: i1 }
                    // Ensure RTTI is not accidentally part of this for primitive optionals
                    std::vector<llvm::Type*> optFields = {wrapped, int1Type};
                    // Construct a name for the optional struct type, e.g., "Optional<int>"
                    // Check if this specific optional struct type already exists
                    llvm::StructType* existingOptStruct = module->getTypeByName(optName);
                    if (existingOptStruct) {
                        resultType = existingOptStruct;
                    } else {
                        resultType = llvm::StructType::create(*context, optFields, optName);
                    }
                }
            }
            break;
        default:
            logError(type->loc, "Unknown or unsupported TypeNode category: " + std::to_string(static_cast<int>(type->category)));
            return nullptr;
    }

    m_typeCache[type] = resultType;
    return resultType;
}

// --- Literal Codegen ---
void LLVMCodegen::visit(Identifier* node) {
    auto it = namedValues.find(node->name);
    if (it == namedValues.end()) {
        logError(node->loc, "Identifier \'" + node->name + "\' not found.");
        setCurrentBlockValue(nullptr); // Use helper
        return;
    }
    // Load the value from the memory location (AllocaInst)
    setCurrentBlockValue(builder->CreateLoad(it->second->getAllocatedType(), it->second, node->name)); // Use helper
}

void LLVMCodegen::visit(IntegerLiteral* node) {
    // Vyn's default integer type is i64, as per user clarification.
    llvm::Type* intType = llvm::Type::getInt64Ty(*context);
    llvm::Value* val = llvm::ConstantInt::get(intType, node->value, true /*isSigned*/);
    setCurrentBlockValue(val);
}

void LLVMCodegen::visit(FloatLiteral* node) {
    // Assuming Vyn float is equivalent to double precision.
    llvm::Type* floatType = llvm::Type::getDoubleTy(*context);
    llvm::Value* val = llvm::ConstantFP::get(floatType, node->value);
    setCurrentBlockValue(val);
}

void LLVMCodegen::visit(BooleanLiteral* node) {
    llvm::Type* boolType = llvm::Type::getInt1Ty(*context);
    llvm::Value* val = llvm::ConstantInt::get(boolType, node->value ? 1 : 0, false /*isSigned (doesn't matter for i1)*/);
    setCurrentBlockValue(val);
}

void LLVMCodegen::visit(StringLiteral* node) {
    // Create a global string constant
    m_lastValue = m_builder->CreateGlobalStringPtr(node->value, ".str");
}

void LLVMCodegen::visit(NilLiteral* node) {
    // Represent nil as a null pointer, defaulting to i8* for now.
    // This might need to be more type-aware later.
    m_lastValue = llvm::ConstantPointerNull::get(llvm::PointerType::get(*m_context, 0));
}

void LLVMCodegen::visit(Identifier* node) {
    // Look up the identifier in the current function's named values (locals/params)
    if (m_currentFunctionNamedValues.count(node->name)) {
        llvm::AllocaInst* alloca = m_currentFunctionNamedValues[node->name];
        // Load the value from the alloca
        m_lastValue = m_builder->CreateLoad(alloca->getAllocatedType(), alloca, node->name.c_str());
        return;
    }

    // Look up in global variables/functions
    llvm::Value* globalVal = m_module->getNamedValue(node->name);
    if (globalVal) {
        if (llvm::isa<llvm::Function>(globalVal)) {
            m_lastValue = globalVal; // It's a function pointer
        } else if (llvm::GlobalVariable* gv = llvm::dyn_cast<llvm::GlobalVariable>(globalVal)) {
            // If it's a global variable, load its value
            m_lastValue = m_builder->CreateLoad(gv->getValueType(), gv, node->name.c_str());
        } else {
            // Potentially other types of global values, or an error
            // For now, assume it's a direct value if not function or global variable
            m_lastValue = globalVal;
        }
        return;
    }

    // TODO: More robust lookup, including enum variants, etc.
    // For now, if not found, it's an error or an unresolved symbol.
    // LogError("Unknown identifier: " + node->name);
    m_lastValue = nullptr; // Or some error marker
    std::cerr << "LLVM Codegen Error: Unknown identifier: " << node->name << std::endl;
}

// --- Statements ---

void LLVMCodegen::visit(BlockStatement* node) {
    // Potentially create a new scope for variables if Vyn has block scoping
    // For now, assume variables are function-scoped or global.
    for (const auto& stmt : node->body) {
        stmt->accept(*this);
        // If a statement was a terminator (like return), stop processing further statements in this block.
        // This check might be more robustly handled by checking if the current block has a terminator.
        if (m_builder->GetInsertBlock()->getTerminator()) {
            break;
        }
    }
    // m_lastValue for a block is typically the value of the last expression statement,
    // but Vyn might not have this C-style "value of a block" concept.
    // For now, a block statement itself doesn't produce a direct llvm::Value.
}

void LLVMCodegen::visit(ReturnStatement* node) {
    if (node->argument) {
        node->argument->accept(*this); // Evaluate the return expression
        llvm::Value* returnValue = m_lastValue;
        if (returnValue) {
            // TODO: Add type checking/casting if returnValue's type doesn't match function's return type
            m_builder->CreateRet(returnValue);
        } else {
            // Error: expression did not yield a value or was invalid
            std::cerr << "LLVM Codegen Error: Return expression is invalid." << std::endl;
            // Potentially return void or a default value for the function's return type
            if (m_currentFunction && m_currentFunction->getReturnType()->isVoidTy()) {
                m_builder->CreateRetVoid();
            } else {
                // Attempt to return a zeroinitializer for non-void functions if expression failed
                // This is a fallback and might not be correct for all types.
                if (m_currentFunction) {
                     m_builder->CreateRet(llvm::Constant::getNullValue(m_currentFunction->getReturnType()));
                } else {
                    // Should not happen if we are in a function context
                     std::cerr << "LLVM Codegen Error: Attempting to return from non-function context." << std::endl;
                }
            }
        }
    } else {
        m_builder->CreateRetVoid();
    }
    // Clearing named values here might be too early if there's dead code after return
    // that still tries to define/use variables. However, for correct code,
    // a return terminates the logical flow of the function for that block.
    // LLVM handles multiple return points in a function correctly.
    // m_currentFunctionNamedValues.clear(); // Let's defer this, might be better handled at function exit.
    m_lastValue = nullptr; // Return statement doesn't "produce" a value for subsequent instructions.
}

void LLVMCodegen::visit(ExpressionStatement* node) {
    if (node->expression) {
        node->expression->accept(*this);
        // The value of m_lastValue is typically ignored for an ExpressionStatement,
        // unless it's the last statement in a block that has a value (not standard in Vyn yet).
        // No specific LLVM instruction is usually generated for the statement itself,
        // only for the expression it contains.
    }
    m_lastValue = nullptr; // Expression statement itself doesn't produce a value for subsequent statements.
}

void LLVMCodegen::visit(IfStatement* node) {
    if (!node->test) {
        std::cerr << "LLVM Codegen Error: If statement has no test condition." << std::endl;
        // Potentially handle this error, e.g., by not generating any code for this if.
        return;
    }

    // Evaluate the test condition
    node->test->accept(*this);
    llvm::Value* conditionValue = m_lastValue;
    if (!conditionValue) {
        std::cerr << "LLVM Codegen Error: If condition expression is invalid." << std::endl;
        return;
    }

    // Convert condition to a boolean (i1)
    // TODO: This might need more sophisticated type checking/conversion,
    // e.g., ensuring the condition is actually a boolean or comparable to zero.
    // For now, assume it's an integer/pointer and compare with zero if not i1.
    if (conditionValue->getType() != llvm::Type::getInt1Ty(*m_context)) {
        conditionValue = m_builder->CreateICmpNE(
            conditionValue,
            llvm::Constant::getNullValue(conditionValue->getType()),
            "ifcond.tobool");
    }

    llvm::Function* parentFunction = m_builder->GetInsertBlock()->getParent();
    if (!parentFunction) {
         std::cerr << "LLVM Codegen Error: If statement not within a function." << std::endl;
         return; // Cannot create blocks without a parent function
    }

    // Create blocks for then, else (optional), and merge.
    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*m_context, "then", parentFunction);
    llvm::BasicBlock* elseBB = nullptr;
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*m_context, "ifcont"); // Continue block

    if (node->alternate) {
        elseBB = llvm::BasicBlock::Create(*m_context, "else");
        m_builder->CreateCondBr(conditionValue, thenBB, elseBB);
    } else {
        m_builder->CreateCondBr(conditionValue, thenBB, mergeBB);
    }

    // Emit then block.
    m_builder->SetInsertPoint(thenBB);
    if (node->consequent) {
        node->consequent->accept(*this);
    }
    if (!m_builder->GetInsertBlock()->getTerminator()) { // Add branch to merge if not already terminated
        m_builder->CreateBr(mergeBB);
    }
    // Codegen of 'Then' can change the current block, update thenBB for the PHI.
    thenBB = m_builder->GetInsertBlock();


    // Emit else block if it exists.
    if (node->alternate) {
        parentFunction->getBasicBlockList().push_back(elseBB); // Add else block to function
        m_builder->SetInsertPoint(elseBB);
        node->alternate->accept(*this);
        if (!m_builder->GetInsertBlock()->getTerminator()) { // Add branch to merge if not already terminated
            m_builder->CreateBr(mergeBB);
        }
        // Codegen of 'Else' can change the current block, update elseBB for the PHI.
        elseBB = m_builder->GetInsertBlock();
    }

    // Emit merge block.
    parentFunction->getBasicBlockList().push_back(mergeBB);
    m_builder->SetInsertPoint(mergeBB);

    // TODO: If expressions (if they return a value), would need a PHI node here.
    // For if statements, the merge block just continues execution.
    // If Vyn's 'if' can be an expression, m_lastValue would be set by a PHI node.
    // For now, 'if' statements don't produce a value.
    m_lastValue = nullptr;
}


// --- Expressions ---

void LLVMCodegen::visit(BinaryExpression* node) {
    // Evaluate left and right operands first
    node->left->accept(*this);
    llvm::Value* L = m_lastValue;
    node->right->accept(*this);
    llvm::Value* R = m_lastValue;

    if (!L || !R) {
        std::cerr << "LLVM Codegen Error: One or both operands of binary expression are invalid." << std::endl;
        m_lastValue = nullptr;
        return;
    }

    // TODO: Type checking and promotion/casting if L and R have different types.
    // For now, assume they are compatible or the same type.
    // We should also check if the operation is valid for the types.

    switch (node->op.type) {
        // Arithmetic operators
        case token::TokenType::PLUS:    // +
            if (L->getType()->isFPOrFPVectorTy()) {
                m_lastValue = m_builder->CreateFAdd(L, R, "addtmpf");
            } else { // Assume integer
                m_lastValue = m_builder->CreateAdd(L, R, "addtmp");
            }
            break;
        case token::TokenType::MINUS:   // -
            if (L->getType()->isFPOrFPVectorTy()) {
                m_lastValue = m_builder->CreateFSub(L, R, "subtmpf");
            } else { // Assume integer
                m_lastValue = m_builder->CreateSub(L, R, "subtmp");
            }
            break;
        case token::TokenType::STAR:    // *
            if (L->getType()->isFPOrFPVectorTy()) {
                m_lastValue = m_builder->CreateFMul(L, R, "multmpf");
            } else { // Assume integer
                m_lastValue = m_builder->CreateMul(L, R, "multmp");
            }
            break;
        case token::TokenType::SLASH:   // /
            // TODO: Integer division should distinguish between signed (SDiv) and unsigned (UDiv)
            // Vyn needs to specify this. Defaulting to SDiv for now.
            if (L->getType()->isFPOrFPVectorTy()) {
                m_lastValue = m_builder->CreateFDiv(L, R, "divtmpf");
            } else { // Assume integer
                m_lastValue = m_builder->CreateSDiv(L, R, "divtmp");
            }
            break;
        case token::TokenType::PERCENT: // %
            // TODO: Integer remainder should distinguish between SRem and URem.
            // Defaulting to SRem.
            if (L->getType()->isFPOrFPVectorTy()) {
                m_lastValue = m_builder->CreateFRem(L, R, "remtmpf");
            } else { // Assume integer
                m_lastValue = m_builder->CreateSRem(L, R, "remtmp");
            }
            break;

        // Comparison operators (result is always i1)
        case token::TokenType::EQUAL_EQUAL: // ==
            if (L->getType()->isFPOrFPVectorTy()) {
                m_lastValue = m_builder->CreateFCmpOEQ(L, R, "eqtmpf");
            } else { // Assume integer or pointer
                m_lastValue = m_builder->CreateICmpEQ(L, R, "eqtmp");
            }
            break;
        case token::TokenType::BANG_EQUAL:  // !=
            if (L->getType()->isFPOrFPVectorTy()) {
                m_lastValue = m_builder->CreateFCmpONE(L, R, "netmpf");
            } else { // Assume integer or pointer
                m_lastValue = m_builder->CreateICmpNE(L, R, "netmp");
            }
            break;
        case token::TokenType::LESS:        // <
            // TODO: Distinguish signed (SLT) vs unsigned (ULT) for integers.
            if (L->getType()->isFPOrFPVectorTy()) {
                m_lastValue = m_builder->CreateFCmpOLT(L, R, "lttmpf");
            } else { // Assume integer
                m_lastValue = m_builder->CreateICmpSLT(L, R, "lttmp");
            }
            break;
        case token::TokenType::LESS_EQUAL:  // <=
            // TODO: Distinguish signed (SLE) vs unsigned (ULE) for integers.
            if (L->getType()->isFPOrFPVectorTy()) {
                m_lastValue = m_builder->CreateFCmpOLE(L, R, "letmpf");
            } else { // Assume integer
                m_lastValue = m_builder->CreateICmpSLE(L, R, "letmp");
            }
            break;
        case token::TokenType::GREATER:     // >
            // TODO: Distinguish signed (SGT) vs unsigned (UGT) for integers.
            if (L->getType()->isFPOrFPVectorTy()) {
                m_lastValue = m_builder->CreateFCmpOGT(L, R, "gttmpf");
            } else { // Assume integer
                m_lastValue = m_builder->CreateICmpSGT(L, R, "gttmp");
            }
            break;
        case token::TokenType::GREATER_EQUAL: // >=
            // TODO: Distinguish signed (SGE) vs unsigned (UGE) for integers.
            if (L->getType()->isFPOrFPVectorTy()) {
                m_lastValue = m_builder->CreateFCmpOGE(L, R, "getmpf");
            } else { // Assume integer
                m_lastValue = m_builder->CreateICmpSGE(L, R, "getmp");
            }
            break;

        // Logical operators (short-circuiting for AND and OR needs careful handling with blocks)
        // For now, implementing non-short-circuiting (bitwise for integers, or direct for booleans)
        // Proper short-circuiting requires generating conditional branches similar to 'if'.
        case token::TokenType::AMPERSAND_AMPERSAND: // && (logical AND)
        case token::TokenType::BAR_BAR:             // || (logical OR)
            // These require short-circuiting logic, which is more complex than a single instruction.
            // This will be similar to how 'if' is handled, creating blocks.
            // For now, placeholder error or non-short-circuiting for i1 types.
            if (L->getType()->isIntegerTy(1) && R->getType()->isIntegerTy(1)) {
                 if (node->op.type == token::TokenType::AMPERSAND_AMPERSAND) {
                    m_lastValue = m_builder->CreateAnd(L, R, "andtmp");
                 } else {
                    m_lastValue = m_builder->CreateOr(L, R, "ortmp");
                 }
            } else {
                std::cerr << "LLVM Codegen Error: Logical AND/OR currently only supported for i1 (boolean) types without short-circuiting." << std::endl;
                m_lastValue = nullptr;
            }
            break;

        // Bitwise operators (should only apply to integers)
        case token::TokenType::AMPERSAND: // &
            if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
                m_lastValue = m_builder->CreateAnd(L, R, "bitwiseandtmp");
            } else {
                std::cerr << "LLVM Codegen Error: Bitwise AND requires integer operands." << std::endl;
                m_lastValue = nullptr;
            }
            break;
        case token::TokenType::BAR:       // |
            if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
                m_lastValue = m_builder->CreateOr(L, R, "bitwiseortmp");
            } else {
                std::cerr << "LLVM Codegen Error: Bitwise OR requires integer operands." << std::endl;
                m_lastValue = nullptr;
            }
            break;
        case token::TokenType::CARET:     // ^
            if (L->getType()->isIntegerTy() && R->getType()->isIntegerTy()) {
                m_lastValue = m_builder->CreateXor(L, R, "bitwisexortmp");
            } else {
                std::cerr << "LLVM Codegen Error: Bitwise XOR requires integer operands." << std::endl;
                m_lastValue = nullptr;
            }
            break;
        // TODO: Add bitwise shift operators (<<, >>)
        // case token::TokenType::LESS_LESS: // <<
        // case token::TokenType::GREATER_GREATER: // >>

        default:
            std::cerr << "LLVM Codegen Error: Unsupported binary operator: " << node->op.lexeme << std::endl;
            m_lastValue = nullptr;
            break;
    }
}

void LLVMCodegen::visit(CallExpression* node) {
    // 1. Codegen the callee expression.
    // This could be an Identifier (function name) or a more complex expression that results in a function pointer.
    node->callee->accept(*this);
    llvm::Value* calleeValue = m_lastValue;

    if (!calleeValue) {
        std::cerr << "LLVM Codegen Error: Callee expression is invalid or does not yield a function." << std::endl;
        m_lastValue = nullptr;
        return;
    }

    // Check if the callee is actually a function or a pointer to a function.
    llvm::Function* func = llvm::dyn_cast<llvm::Function>(calleeValue);
    llvm::FunctionType* funcType = nullptr;

    if (func) {
        funcType = func->getFunctionType();
    } else if (calleeValue->getType()->isPointerTy()) {
        llvm::Type* pointedToType = calleeValue->getType()->getContainedType(0);
        if (pointedToType->isFunctionTy()) {
            funcType = llvm::dyn_cast<llvm::FunctionType>(pointedToType);
        } 
    }

    if (!funcType) {
        std::cerr << "LLVM Codegen Error: Callee is not a function or a pointer to a function. Type: " << getTypeName(calleeValue->getType()) << std::endl;
        m_lastValue = nullptr;
        return;
    }
    
    // 2. Check argument count.
    if (funcType->getNumParams() != node->arguments.size() && !funcType->isVarArg()) {
        std::cerr << "LLVM Codegen Error: Incorrect number of arguments for function call. Expected "
                  << funcType->getNumParams() << ", got " << node->arguments.size() << std::endl;
        m_lastValue = nullptr;
        return;
    }

    // 3. Codegen arguments.
    std::vector<llvm::Value*> argValues;
    for (size_t i = 0; i < node->arguments.size(); ++i) {
        node->arguments[i]->accept(*this);
        llvm::Value* argValue = m_lastValue;
        if (!argValue) {
            std::cerr << "LLVM Codegen Error: Argument expression " << i << " is invalid." << std::endl;
            m_lastValue = nullptr;
            return;
        }

        // TODO: Type checking and casting for arguments.
        // llvm::Type* expectedArgType = funcType->getParamType(i);
        // if (argValue->getType() != expectedArgType) { ... cast or error ... }
        argValues.push_back(argValue);
    }

    // 4. Create the call instruction.
    // If `func` is not null, it means calleeValue was a direct llvm::Function.
    // Otherwise, calleeValue is a pointer to a function, which is also what CreateCall expects.
    m_lastValue = m_builder->CreateCall(funcType, calleeValue, argValues, "calltmp");
}

void LLVMCodegen::visit(MemberExpression* node) {
    // 1. Evaluate the object expression.
    node->object->accept(*this);
    llvm::Value* objectValue = m_lastValue;

    if (!objectValue) {
        std::cerr << "LLVM Codegen Error: Object in member expression is invalid." << std::endl;
        m_lastValue = nullptr;
        return;
    }

    llvm::Type* objectType = objectValue->getType();

    // We expect the object to be a pointer to a struct or class for field access.
    // If it's not a pointer, it might be a value type struct/class, which we might need to handle
    // by first allocating it on the stack and getting a pointer if Vyn allows direct member access on values.
    // For now, assume objectValue is a pointer to the aggregate type.
    if (!objectType->isPointerTy()) {
        std::cerr << "LLVM Codegen Error: Object in member expression is not a pointer. Actual type: " << getTypeName(objectType) << std::endl;
        // TODO: Handle direct access on value types if Vyn semantics allow.
        // Potentially, if objectValue is an AllocaInst (already a pointer to stack space), that's fine.
        // Or if it's a global variable (which is also a pointer).
        // If it's a loaded struct value, we need its address.
        m_lastValue = nullptr;
        return;
    }

    llvm::Type* pointedObjectType = objectType->getPointerElementType();
    if (!pointedObjectType->isStructTy()) {
        // TODO: Handle array access if node->computed is true and pointedObjectType is an array or pointer type.
        std::cerr << "LLVM Codegen Error: Object in member expression does not point to a struct type. Actual pointed type: " << getTypeName(pointedObjectType) << std::endl;
        m_lastValue = nullptr;
        return;
    }

    llvm::StructType* objectStructType = llvm::dyn_cast<llvm::StructType>(pointedObjectType);
    if (!objectStructType || objectStructType->isOpaque()) {
        std::cerr << "LLVM Codegen Error: Object in member expression points to an opaque or non-struct type." << std::endl;
        m_lastValue = nullptr;
        return;
    }

    // 2. Determine the property (field name or index).
    if (!node->computed) { // Dot access: object.property
        Identifier* propIdent = dynamic_cast<Identifier*>(node->property.get());
        if (!propIdent) {
            std::cerr << "LLVM Codegen Error: Property in dot-access member expression is not an identifier." << std::endl;
            m_lastValue = nullptr;
            return;
        }
        std::string fieldName = propIdent->name;

        // Find the struct/class metadata to get the field index.
        // The type name might be stored in userTypeMap or we might need to derive it.
        std::string structName = objectStructType->getName().str();
        // LLVM struct names can be mangled (e.g., "struct.MyStruct"). We might need to demangle or use the original Vyn type name.
        // For now, assume userTypeMap uses the Vyn type name.
        // This part is tricky if the structName from LLVM doesn't directly map to userTypeMap keys.
        // Let's try to find it by iterating userTypeMap if direct lookup fails.
        StructMetadata* metadata = nullptr;
        auto itMeta = userTypeMap.find(structName); // Try direct name first
        if (itMeta != userTypeMap.end() && itMeta->second.llvmType == objectStructType) {
            metadata = &itMeta->second;
        } else { // Fallback: search by comparing llvm::Type*
            for (auto& pair : userTypeMap) {
                if (pair.second.llvmType == objectStructType) {
                    metadata = &pair.second;
                    structName = pair.first; // Update structName to the Vyn name
                    break;
                }
            }
        }

        if (!metadata) {
            std::cerr << "LLVM Codegen Error: Could not find metadata for struct type '" << structName << "' for member access." << std::endl;
            m_lastValue = nullptr;
            return;
        }

        auto fieldIt = metadata->fieldIndices.find(fieldName);
        if (fieldIt == metadata->fieldIndices.end()) {
            std::cerr << "LLVM Codegen Error: Field '" << fieldName << "' not found in struct '" << structName << ".'" << std::endl;
            m_lastValue = nullptr;
            return;
        }
        unsigned fieldIndex = fieldIt->second;

        // 3. Create GEP to get a pointer to the field.
        llvm::Value* fieldPtr = m_builder->CreateStructGEP(objectStructType, objectValue, fieldIndex, fieldName + ".ptr");
        
        // 4. Load the value from the field pointer.
        // The result of a member access expression is the value of the member.
        m_lastValue = m_builder->CreateLoad(fieldPtr->getType()->getPointerElementType(), fieldPtr, fieldName);

    } else { // Computed access: object[index] - Typically for arrays or map-like objects.
        // TODO: Implement computed member access (e.g., array element access).
        // This will involve evaluating node->property as an index, then using CreateGEP.
        // For now, array access is not fully supported here.
        node->property->accept(*this);
        llvm::Value* indexValue = m_lastValue;

        if(!indexValue) {
            std::cerr << "LLVM Codegen Error: Index for computed member access is invalid." << std::endl;
            m_lastValue = nullptr;
            return;
        }

        // Assuming objectValue is a pointer to the first element of an array (e.g., T*)
        // and indexValue is an integer index.
        if (objectType->isPointerTy() && objectType->getPointerElementType()->isArrayTy()) {
             llvm::ArrayType* arrayType = llvm::dyn_cast<llvm::ArrayType>(objectType->getPointerElementType());
             llvm::Value* gepIndices[] = {
                llvm::ConstantInt::get(int64Type, 0), // Dereference the pointer to the array
                indexValue // Index into the array
            };
            llvm::Value* elementPtr = m_builder->CreateGEP(arrayType, objectValue, gepIndices, "elemptr");
            m_lastValue = m_builder->CreateLoad(arrayType->getElementType(), elementPtr, "elem");
        } else if (objectType->isPointerTy() && objectType->getPointerElementType()->isPointerTy()) {
            // This case could be for T** or similar, or for simple T* representing a dynamic array.
            // If it's T*, GEP directly on objectValue with the index.
            llvm::Value* elementPtr = m_builder->CreateGEP(objectType->getPointerElementType(), objectValue, indexValue, "elemptr");
            m_lastValue = m_builder->CreateLoad(objectType->getPointerElementType()->getPointerElementType(), elementPtr, "elem");
        } else {
            std::cerr << "LLVM Codegen Error: Computed member access (e.g. array indexing) on non-array/pointer type '" << getTypeName(objectType) << "' not yet fully supported." << std::endl;
            m_lastValue = nullptr;
        }
    }
}

void LLVMCodegen::visit(AssignmentExpression* node) {
    // 1. Evaluate the right-hand side (RValue) first.
    node->right->accept(*this);
    llvm::Value* valueToAssign = m_lastValue;

    if (!valueToAssign) {
        std::cerr << "LLVM Codegen Error: Right-hand side of assignment is invalid." << std::endl;
        m_lastValue = nullptr;
        return;
    }

    // 2. Determine the LValue (target of the assignment).
    // The LValue must be a memory location (e.g., an AllocaInst for a local var, or a GEP for a field/element).
    // We need to visit the left expression but *not* load its value; we need its address.
    // This requires a way to tell the Identifier or MemberExpression visitor to return a pointer, not a value.
    // For now, we'll re-evaluate and assume they can give us a pointer if asked, or we deduce it.

    llvm::Value* lvaluePtr = nullptr;

    if (Identifier* id = dynamic_cast<Identifier*>(node->left.get())) {
        // Assignment to a simple variable.
        if (m_currentFunctionNamedValues.count(id->name)) {
            lvaluePtr = m_currentFunctionNamedValues[id->name]; // This is an AllocaInst (pointer)
        } else {
            // Check globals
            llvm::GlobalVariable* gv = m_module->getNamedGlobal(id->name);
            if (gv) {
                lvaluePtr = gv;
            } else {
                std::cerr << "LLVM Codegen Error: Assigning to undeclared identifier '" << id->name << ".'" << std::endl;
                m_lastValue = nullptr;
                return;
            }
        }
    } else if (MemberExpression* memExpr = dynamic_cast<MemberExpression*>(node->left.get())) {
        // Assignment to a struct field or array element.
        // We need to get the pointer to the field/element, similar to visit(MemberExpression*), but without the final load.
        memExpr->object->accept(*this);
        llvm::Value* objectPtr = m_lastValue;
        if (!objectPtr || !objectPtr->getType()->isPointerTy()) {
            std::cerr << "LLVM Codegen Error: Object in assignment LValue is not a valid pointer." << std::endl; return;
        }
        llvm::Type* pointedObjectType = objectPtr->getType()->getPointerElementType();

        if (!memExpr->computed) { // field access: obj.field = ...
            if (!pointedObjectType->isStructTy()) { std::cerr << "Error: LHS of assignment not a struct pointer." << std::endl; return; }
            llvm::StructType* objStructType = llvm::dyn_cast<llvm::StructType>(pointedObjectType);
            Identifier* propIdent = dynamic_cast<Identifier*>(memExpr->property.get());
            if (!propIdent) { std::cerr << "Error: LHS field not an identifier." << std::endl; return; }
            
            std::string structName = objStructType->getName().str();
            StructMetadata* metadata = nullptr;
            auto itMeta = userTypeMap.find(structName);
            if (itMeta != userTypeMap.end() && itMeta->second.llvmType == objStructType) {
                metadata = &itMeta->second;
            } else {
                for (auto& pair : userTypeMap) {
                    if (pair.second.llvmType == objStructType) {
                        metadata = &pair.second; structName = pair.first; break;
                    }
                }
            }
            if (!metadata) { std::cerr << "Error: LHS struct metadata not found." << std::endl; return; }
            
            auto fieldIt = metadata->fieldIndices.find(propIdent->name);
            if (fieldIt == metadata->fieldIndices.end()) { std::cerr << "Error: LHS field not found." << std::endl; return; }
            lvaluePtr = m_builder->CreateStructGEP(objStructType, objectPtr, fieldIt->second, propIdent->name + ".ptr");
        } else { // array access: obj[index] = ...
            memExpr->property->accept(*this);
            llvm::Value* indexVal = m_lastValue;
            if (!indexVal) { std::cerr << "Error: LHS index invalid." << std::endl; return; }
            
            if (pointedObjectType->isArrayTy()) {
                llvm::ArrayType* arrayType = llvm::dyn_cast<llvm::ArrayType>(pointedObjectType);
                llvm::Value* gepIndices[] = { llvm::ConstantInt::get(int64Type, 0), indexVal };
                lvaluePtr = m_builder->CreateGEP(arrayType, objectPtr, gepIndices, "elem.ptr");
            } else if (pointedObjectType->isPointerTy()) { // T* for dynamic array
                 lvaluePtr = m_builder->CreateGEP(pointedObjectType->getPointerElementType(), objectPtr, indexVal, "elem.ptr");
            } else {
                std::cerr << "LLVM Codegen Error: LHS of computed assignment is not an array/pointer type." << std::endl; return;
            }
        }
    } else {
        std::cerr << "LLVM Codegen Error: Left-hand side of assignment is not a valid LValue (identifier or member expression)." << std::endl;
        m_lastValue = nullptr;
        return;
    }

    if (!lvaluePtr) {
        std::cerr << "LLVM Codegen Error: Could not determine memory location for assignment LValue." << std::endl;
        m_lastValue = nullptr;
        return;
    }

    // TODO: Type checking: ensure valueToAssign is compatible with lvaluePtr's element type.
    // llvm::Type* lvalueElementType = lvaluePtr->getType()->getPointerElementType();
    // if (valueToAssign->getType() != lvalueElementType) { ... cast or error ... }

    // 3. Create the store instruction.
    m_builder->CreateStore(valueToAssign, lvaluePtr);

    // 4. The result of an assignment expression is the assigned value.
    m_lastValue = valueToAssign;
    
    // TODO: Handle compound assignment operators (+=, -=, etc.)
    // This would involve loading the old value, performing the binary operation, then storing.
    // Example for += : 
    // if (node->op.type == token::TokenType::PLUS_EQUAL) {
    //    llvm::Value* oldValue = m_builder->CreateLoad(lvaluePtr->getType()->getPointerElementType(), lvaluePtr, "oldval");
    //    llvm::Value* newValue; 
    //    if (oldValue->getType()->isFPOrFPVectorTy()) { newValue = m_builder->CreateFAdd(oldValue, valueToAssign, "addassign"); }
    //    else { newValue = m_builder->CreateAdd(oldValue, valueToAssign, "addassign"); }
    //    m_builder->CreateStore(newValue, lvaluePtr);
    //    m_lastValue = newValue;
    // }
}
