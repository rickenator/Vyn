// SPDX-License-Identifier: Apache-2.0

\
#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/parser/ast.hpp"
#include "vyb/parser/source_location.hpp" // For vyb::SourceLocation

// LLVM Headers
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Path.h"

// Standard C++ Headers
#include <iostream> // For logError, std::cerr
#include <map>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <system_error> // For std::error_code
#include <vector>

using namespace vyb;

// Error Logging Utility
void LLVMCodegen::logError(const SourceLocation& loc, const std::string& message) {
    std::cerr << "Error at " << (!loc.filePath.empty() ? loc.filePath.c_str() : "unknown_file") << ":" << loc.line << ":" << loc.column << ": " << message << std::endl;
}

// Warning Logging Utility
void LLVMCodegen::logWarning(const SourceLocation& loc, const std::string& message) {
    std::cerr << "Warning at " << (!loc.filePath.empty() ? loc.filePath.c_str() : "unknown_file") << ":" << loc.line << ":" << loc.column << ": " << message << std::endl;
}

// --- RTTI Helper Implementations ---
llvm::StructType* LLVMCodegen::getOrCreateRTTIStructType() {
    if (rttiStructType) return rttiStructType;

    std::vector<llvm::Type*> rttiFields = {
        int32Type,
        int8PtrType
    };
    rttiStructType = llvm::StructType::create(*context, rttiFields, "vyb.RTTI");
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

LLVMCodegen::LLVMCodegen(Driver& driver)
    : driver_(driver),
      context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("VybModule", *this->context)),
      builder(std::make_unique<llvm::IRBuilder<>>(*this->context)),
      debugBuilder(std::make_unique<llvm::DIBuilder>(*module)),
      debugCompileUnit(nullptr),
      debugFile(nullptr),
      m_isLHSOfAssignment(false),
      verbose(false),
      currentFunction(nullptr),
      currentClassType(nullptr),
      m_currentLLVMValue(nullptr),
      m_currentVybModule(nullptr)
{
    voidType = llvm::Type::getVoidTy(*this->context);
    int1Type = llvm::Type::getInt1Ty(*this->context);
    int8Type = llvm::Type::getInt8Ty(*this->context);
    int32Type = llvm::Type::getInt32Ty(*this->context);
    int64Type = llvm::Type::getInt64Ty(*this->context);
    floatType = llvm::Type::getFloatTy(*this->context);
    doubleType = llvm::Type::getDoubleTy(*this->context);
    int8PtrType = llvm::PointerType::getUnqual(int8Type);

    rttiStructType = getOrCreateRTTIStructType();

    currentLoopContext = {nullptr, nullptr, nullptr, nullptr};
}

LLVMCodegen::~LLVMCodegen() = default;

void LLVMCodegen::generate(vyb::ast::Module* astModule, const std::string& outputFilename) {
    if (!astModule) {
        logError(SourceLocation(), "Cannot generate code: AST module is null.");
        return;
    }

    // Initialize debug information
    initializeDebugInfo(outputFilename);

    astModule->accept(*this);

    if (vyb::g_kernel_mode) {
        // issue #198: lower the module as NVPTX device code. Set the target triple
        // so the module carries the right target identity end-to-end (the NVPTX
        // data layout is applied at lowering time). The __vyb_* intrinsic externs
        // are NOT declared here — a kernel is pure value/data-parallel code with no
        // host runtime, and any __vyb_* reference left in a kernel body is a bug that
        // must surface as an unresolved-symbol failure at PTX emission.
        module->setTargetTriple("nvptx64-nvidia-cuda");
    } else {
        // Ensure all core intrinsic functions are declared in the module
        // This prevents JIT runtime errors when functions are not found
        ensureCoreIntrinsicFunctions();
    }

    // Finalize debug information before verification
    finalizeDebugInfo();

    // DEBUG: Print divide function RIGHT before verification
    if (llvm::Function* divideFunc = module->getFunction("divide")) {
        VYB_CDBG << "DEBUG: divide function RIGHT BEFORE VERIFICATION:" << std::endl;
        divideFunc->print(llvm::outs());
        std::cout << std::endl;
    }

    bool verificationFailed = llvm::verifyModule(*module, &llvm::errs());

    // DEBUG: Print divide function RIGHT after verification
    if (llvm::Function* divideFunc = module->getFunction("divide")) {
        VYB_CDBG << "DEBUG: divide function RIGHT AFTER VERIFICATION:" << std::endl;
        divideFunc->print(llvm::outs());
        std::cout << std::endl;
    }

    if (verificationFailed) {
        logError(SourceLocation(), "LLVM module verification failed.");
        // Still print the IR for debugging purposes
        std::cerr << "\n\n=== INVALID LLVM IR (for debugging) ===\n" << std::endl;
        module->print(llvm::errs(), nullptr);
        std::cerr << "\n=== END INVALID IR ===\n" << std::endl;
        return;
    }

    std::error_code EC;
    llvm::raw_fd_ostream dest(outputFilename, EC, llvm::sys::fs::OpenFlags{});

    if (EC) {
        logError(SourceLocation(), "Could not open file: " + EC.message());
        return;
    }

    if (vyb::g_debug_codegen) {
        std::cerr << "DEBUG: Functions in module before printing IR:" << std::endl;
        for (llvm::Function& func : module->functions()) {
            std::cerr << "  - " << func.getName().str() << " (blocks: " << func.size() << ")" << std::endl;
        }
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

void LLVMCodegen::visit(vyb::ast::Module* node) {
    if (!node) return;

    VYB_CDBG << "DEBUG: Module visitor called with " << node->body.size() << " statements" << std::endl;

    vyb::ast::Module* previousModule = m_currentVybModule;
    m_currentVybModule = node;

    // TYPE PASS 0 (enums): Register enum declarations BEFORE structs. Struct
    // field types may reference an enum (e.g. `struct Rec { kindP<KindP> }`),
    // and struct-field LLVM types are resolved during the struct pass below, so
    // the enum must already be in userTypeMap/taggedEnumInfo by then. Previously
    // enums were processed after structs, so any struct field whose type was an
    // enum failed to resolve ("Unknown type identifier") and, worse, cascaded
    // into an unsized struct that crashed the compiler on the error path (#181).
    // Enum payloads only carry scalars (Int/Float/String) in current usage, so
    // this reorder cannot regress enum-payload -> struct references.
    VYB_CDBG << "DEBUG: Type pass 0 - processing enum declarations" << std::endl;
    for (size_t i = 0; i < node->body.size(); ++i) {
        const auto& stmt = node->body[i];
        if (stmt && stmt->getType() == vyb::ast::NodeType::ENUM_DECLARATION) {
            VYB_CDBG << "DEBUG: Type pass 0 - processing enum declaration statement " << i << std::endl;
            stmt->accept(*this);
        }
    }

    // FIRST PASS: Process all struct declarations to establish type information
    VYB_CDBG << "DEBUG: First pass - processing struct declarations" << std::endl;
    for (size_t i = 0; i < node->body.size(); ++i) {
        const auto& stmt = node->body[i];
        if (stmt && stmt->getType() == vyb::ast::NodeType::STRUCT_DECLARATION) {
            VYB_CDBG << "DEBUG: Processing struct declaration statement " << i << std::endl;
            stmt->accept(*this);
        }
    }


    // FIRST-ALIAS PASS: Process type alias declarations before function forward declarations
    // so that type aliases are available when resolving function return types.
    VYB_CDBG << "DEBUG: First-alias pass - processing type alias declarations" << std::endl;
    for (size_t i = 0; i < node->body.size(); ++i) {
        const auto& stmt = node->body[i];
        if (stmt && stmt->getType() == vyb::ast::NodeType::TYPE_ALIAS_DECLARATION) {
            VYB_CDBG << "DEBUG: Processing type alias declaration statement " << i << std::endl;
            stmt->accept(*this);
        }
    }

    // SECOND PASS: Create forward declarations for all functions
    VYB_CDBG << "DEBUG: Second pass - creating function forward declarations" << std::endl;
    for (size_t i = 0; i < node->body.size(); ++i) {
        const auto& stmt = node->body[i];
        if (stmt && stmt->getType() == vyb::ast::NodeType::FUNCTION_DECLARATION) {
            VYB_CDBG << "DEBUG: Creating forward declaration for function statement " << i << std::endl;
            vyb::ast::FunctionDeclaration* funcDecl = static_cast<vyb::ast::FunctionDeclaration*>(stmt.get());
            createFunctionForwardDeclaration(funcDecl);
        }
    }

    // THIRD PASS: Process all remaining statements (skip structs already processed in first pass)
    VYB_CDBG << "DEBUG: Third pass - processing all statements" << std::endl;
    for (size_t i = 0; i < node->body.size(); ++i) {
        const auto& stmt = node->body[i];
        if (stmt) {
            // Skip struct declarations since they were already processed in first pass
            if (stmt->getType() == vyb::ast::NodeType::STRUCT_DECLARATION) {
                VYB_CDBG << "DEBUG: Skipping struct declaration statement " << i << " (already processed in first pass)" << std::endl;
                continue;
            }
            VYB_CDBG << "DEBUG: Processing module statement " << i << " (type: " << static_cast<int>(stmt->getType()) << ")" << std::endl;
            stmt->accept(*this);
        }
    }

    // FOURTH PASS: Register all type metadata for runtime JSON serialization
    // In kernel mode (#198) this runtime registry doesn't exist — device modules
    // are pure data-parallel kernels with no host runtime. Skipping it also keeps
    // __vyb_* reference-count/registry symbols out of the NVPTX module.
    VYB_CDBG << "DEBUG: Fourth pass - registering type metadata" << std::endl;
    if (!vyb::g_kernel_mode) {
        registerTypeMetadata();
        registerTypeNames();
    }

    m_currentVybModule = previousModule;
    m_currentLLVMValue = nullptr;
}

// --- Debug Information Implementation ---

void LLVMCodegen::initializeDebugInfo(const std::string& filename) {
    if (!debugBuilder) {
        debugBuilder = std::make_unique<llvm::DIBuilder>(*module);
    }

    // Create debug file
    llvm::SmallString<256> currentDir;
    llvm::sys::fs::current_path(currentDir);
    llvm::SmallString<256> absolutePath(filename);
    llvm::sys::fs::make_absolute(currentDir, absolutePath);

    std::string directory = llvm::sys::path::parent_path(absolutePath).str();
    std::string fileName = llvm::sys::path::filename(absolutePath).str();

    debugFile = debugBuilder->createFile(fileName, directory);

    // Create compile unit
    debugCompileUnit = debugBuilder->createCompileUnit(
        llvm::dwarf::DW_LANG_C_plus_plus,  // Use C++ as closest language
        debugFile,
        "Vyb Compiler",      // Producer
        false,               // isOptimized
        "",                  // Flags
        0                    // Runtime version
    );

    // Set debug info version
    module->addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);
    module->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 4);

    // Push compile unit as initial scope
    debugScopeStack.push(debugCompileUnit);

    VYB_CDBG << "DEBUG: Initialized debug info for file: " << fileName << " in directory: " << directory << std::endl;
}

void LLVMCodegen::finalizeDebugInfo() {
    if (debugBuilder) {
        debugBuilder->finalize();
        VYB_CDBG << "DEBUG: Finalized debug information" << std::endl;
    }
}

llvm::DISubprogram* LLVMCodegen::createDebugFunctionInfo(llvm::Function* function, const std::string& name,
                                                        const SourceLocation& loc, bool isAsync) {
    if (!debugBuilder || !debugFile) {
        return nullptr;
    }

    // Create function type for debug info
    llvm::SmallVector<llvm::Metadata*, 8> paramTypes;

    // Add return type (first element)
    llvm::DIType* returnType = getDebugType(function->getReturnType(), "return");
    paramTypes.push_back(returnType);

    // Add parameter types
    for (auto& arg : function->args()) {
        llvm::DIType* paramType = getDebugType(arg.getType(), arg.getName().str());
        paramTypes.push_back(paramType);
    }

    llvm::DISubroutineType* functionType = debugBuilder->createSubroutineType(
        debugBuilder->getOrCreateTypeArray(paramTypes)
    );

    // Create subprogram
    std::string displayName = isAsync ? ("async " + name) : name;
    llvm::DISubprogram* subprogram = debugBuilder->createFunction(
        debugFile,                    // Scope
        displayName,                  // Name
        name,                         // Linkage name
        debugFile,                    // File
        loc.line,                     // Line number
        functionType,                 // Type
        loc.line,                     // Scope line
        llvm::DINode::FlagPrototyped, // Flags
        llvm::DISubprogram::SPFlagDefinition
    );

    // Set subprogram for the function
    function->setSubprogram(subprogram);

    // Push function scope
    debugScopeStack.push(subprogram);

    VYB_CDBG << "DEBUG: Created debug info for " << (isAsync ? "async " : "") << "function: " << name
              << " at line " << loc.line << std::endl;

    return subprogram;
}

void LLVMCodegen::setDebugLocation(const SourceLocation& loc) {
    if (!debugBuilder || debugScopeStack.empty()) {
        return;
    }

    llvm::DIScope* scope = debugScopeStack.top();
    llvm::DILocation* debugLoc = llvm::DILocation::get(*context, loc.line, loc.column, scope);
    builder->SetCurrentDebugLocation(debugLoc);
}

void LLVMCodegen::pushDebugScope(llvm::DIScope* scope) {
    if (scope) {
        debugScopeStack.push(scope);
    }
}

void LLVMCodegen::popDebugScope() {
    if (!debugScopeStack.empty()) {
        debugScopeStack.pop();
    }
}

llvm::DIType* LLVMCodegen::getDebugType(llvm::Type* llvmType, const std::string& typeName) {
    if (!debugBuilder) {
        return nullptr;
    }

    if (llvmType->isVoidTy()) {
        return nullptr; // Void type
    } else if (llvmType->isIntegerTy(1)) {
        return debugBuilder->createBasicType("bool", 1, llvm::dwarf::DW_ATE_boolean);
    } else if (llvmType->isIntegerTy(8)) {
        return debugBuilder->createBasicType("i8", 8, llvm::dwarf::DW_ATE_signed);
    } else if (llvmType->isIntegerTy(32)) {
        return debugBuilder->createBasicType("i32", 32, llvm::dwarf::DW_ATE_signed);
    } else if (llvmType->isIntegerTy(64)) {
        return debugBuilder->createBasicType("i64", 64, llvm::dwarf::DW_ATE_signed);
    } else if (llvmType->isFloatTy()) {
        return debugBuilder->createBasicType("f32", 32, llvm::dwarf::DW_ATE_float);
    } else if (llvmType->isDoubleTy()) {
        return debugBuilder->createBasicType("f64", 64, llvm::dwarf::DW_ATE_float);
    } else if (llvmType->isPointerTy()) {
        // In newer LLVM versions, pointers are opaque
        // Create a generic pointer type
        return debugBuilder->createPointerType(nullptr, 64); // 64-bit opaque pointer
    } else if (llvmType->isStructTy()) {
        llvm::StructType* structType = llvm::cast<llvm::StructType>(llvmType);
        std::string structName = structType->hasName() ? structType->getName().str() : ("struct_" + typeName);

        // Create a basic struct type for now
        return debugBuilder->createStructType(
            debugFile,                          // Scope
            structName,                         // Name
            debugFile,                          // File
            0,                                  // Line number
            64 * structType->getNumElements(),  // Size in bits (rough estimate)
            8,                                  // Alignment
            llvm::DINode::FlagZero,             // Flags
            nullptr,                            // Derived from
            llvm::DINodeArray()                 // Elements (empty for now)
        );
    }

    // Default case: create an unspecified type
    return debugBuilder->createUnspecifiedType(typeName.empty() ? "unknown" : typeName);
}

llvm::DILocalVariable* LLVMCodegen::createDebugVariableInfo(const std::string& varName, llvm::DIType* debugType,
                                                           const SourceLocation& loc, llvm::DIScope* scope) {
    if (!debugBuilder || !debugType) {
        return nullptr;
    }

    // Use provided scope or current scope from stack
    llvm::DIScope* currentScope = scope;
    if (!currentScope && !debugScopeStack.empty()) {
        currentScope = debugScopeStack.top();
    }

    if (!currentScope) {
        return nullptr;
    }

    // Create local variable debug info
    llvm::DILocalVariable* debugVar = debugBuilder->createAutoVariable(
        currentScope,           // Scope
        varName,               // Name
        debugFile,             // File
        loc.line,              // Line number
        debugType,             // Type
        true,                  // Always preserve
        llvm::DINode::FlagZero // Flags
    );

    VYB_CDBG << "DEBUG: Created debug info for variable '" << varName << "' at line " << loc.line << std::endl;
    return debugVar;
}

void LLVMCodegen::insertDebugVariableDeclaration(llvm::DILocalVariable* debugVar, llvm::Value* alloca,
                                                 const SourceLocation& loc) {
    if (!debugBuilder || !debugVar || !alloca) {
        return;
    }

    // Insert a debug declaration at the current insertion point
    debugBuilder->insertDeclare(
        alloca,                                    // Storage
        debugVar,                                  // Variable info
        debugBuilder->createExpression(),          // Complex expression (empty)
        llvm::DILocation::get(                     // Debug location
            *context,
            loc.line,
            loc.column,
            debugScopeStack.empty() ? debugCompileUnit : debugScopeStack.top()
        ),
        builder->GetInsertBlock()                  // Basic block
    );

    VYB_CDBG << "DEBUG: Inserted debug declaration for variable '" << debugVar->getName().str()
              << "' at line " << loc.line << " column " << loc.column << std::endl;
}

// --- Async State Machine Debug Information Implementation ---

void LLVMCodegen::initializeAsyncStateDebugInfo(const std::string& functionName, const SourceLocation& loc) {
    if (!debugBuilder || !currentAsyncState.isAsync) {
        return;
    }

    // Create debug variables for async state tracking
    if (currentAsyncState.stateStructType && currentAsyncState.stateStructInstance) {
        // Create debug info for the state variable
        llvm::DIType* stateType = getDebugType(int32Type, "AsyncState");
        currentAsyncState.stateDebugVar = createDebugVariableInfo(
            functionName + "_state", stateType, loc);

        if (currentAsyncState.stateDebugVar) {
            insertDebugVariableDeclaration(currentAsyncState.stateDebugVar,
                                           currentAsyncState.stateStructInstance, loc);
        }
    }

    // Create debug info for the future result variable
    if (currentAsyncState.futureValue) {
        llvm::DIType* futureType = getDebugType(currentAsyncState.futureValue->getType(), "Future");
        currentAsyncState.futureDebugVar = createDebugVariableInfo(
            functionName + "_future", futureType, loc);

        if (currentAsyncState.futureDebugVar && currentAsyncState.futureValue) {
            insertDebugVariableDeclaration(currentAsyncState.futureDebugVar,
                                           currentAsyncState.futureValue, loc);
        }
    }

    VYB_CDBG << "DEBUG: Initialized async state debug info for function '" << functionName
              << "' at line " << loc.line << std::endl;
}

void LLVMCodegen::createSuspensionPointDebugInfo(int stateNumber, const SourceLocation& loc, const std::string& description) {
    if (!debugBuilder || !currentAsyncState.isAsync) {
        return;
    }

    // Create debug location for this suspension point
    llvm::DILocation* suspensionLocation = llvm::DILocation::get(
        *context,
        loc.line,
        loc.column,
        debugScopeStack.empty() ? debugCompileUnit : debugScopeStack.top()
    );

    // Store suspension point information
    currentAsyncState.suspensionPointLocations[stateNumber] = suspensionLocation;
    currentAsyncState.stateDescriptions[stateNumber] = description;

    VYB_CDBG << "DEBUG: Created suspension point " << stateNumber << " (" << description
              << ") at line " << loc.line << " column " << loc.column << std::endl;
}

void LLVMCodegen::insertAsyncStateTransitionDebugInfo(int fromState, int toState, const SourceLocation& loc) {
    if (!debugBuilder || !currentAsyncState.isAsync) {
        return;
    }

    // Set debug location for the state transition
    setDebugLocation(loc);

    // Log the state transition for debugging
    VYB_CDBG << "DEBUG: Async state transition from " << fromState << " to " << toState
              << " at line " << loc.line << " column " << loc.column << std::endl;

    // Store the transition information (could be used for more sophisticated debug info later)
    auto fromDesc = currentAsyncState.stateDescriptions.find(fromState);
    auto toDesc = currentAsyncState.stateDescriptions.find(toState);

    if (fromDesc != currentAsyncState.stateDescriptions.end() &&
        toDesc != currentAsyncState.stateDescriptions.end()) {
        VYB_CDBG << "DEBUG: State transition: '" << fromDesc->second << "' -> '" << toDesc->second << "'" << std::endl;
    }
}

void LLVMCodegen::insertContinuationDebugMarker(int stateNumber, const SourceLocation& loc) {
    if (!debugBuilder || !currentAsyncState.isAsync) {
        return;
    }

    // Set debug location for the continuation point
    setDebugLocation(loc);

    // Find the suspension point location if available
    auto suspensionPoint = currentAsyncState.suspensionPointLocations.find(stateNumber);
    auto stateDesc = currentAsyncState.stateDescriptions.find(stateNumber);

    VYB_CDBG << "DEBUG: Continuation point for state " << stateNumber;
    if (stateDesc != currentAsyncState.stateDescriptions.end()) {
        std::cout << " (" << stateDesc->second << ")";
    }
    std::cout << " at line " << loc.line << " column " << loc.column << std::endl;

    if (suspensionPoint != currentAsyncState.suspensionPointLocations.end()) {
        VYB_CDBG << "DEBUG: Resuming from suspension point at line "
                  << suspensionPoint->second->getLine() << " column "
                  << suspensionPoint->second->getColumn() << std::endl;
    }
}
