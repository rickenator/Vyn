#pragma once

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <memory>
#include <set>
#include <stack>
#include <string>
#include <vector>

#include "vyb/parser/ast.hpp"
#include "vyb/semantic.hpp" // For SourceLocation, UserTypeInfo
#include "vyb/driver.hpp"   // Added to resolve Driver type

// Forward declarations
namespace llvm {
    class Value;
    class Type;
    class Function;
    class BasicBlock;
    class StructType;
    class AllocaInst;
}

namespace vyb {
    class Driver; // Forward declaration might also work if full include causes issues
}


namespace vyb {

// Global flag: when false (the default), all "DEBUG: ..." codegen prints are suppressed.
// Enable with --debug-codegen CLI flag.
extern bool g_debug_codegen;

// Convenience macro: use VYB_CDBG in place of std::cerr for DEBUG-level codegen output.
// The entire chained << expression is skipped when g_debug_codegen is false.
#define VYB_CDBG if (vyb::g_debug_codegen) std::cerr

// Helper struct for storing information about user-defined types
struct UserTypeInfo {
    llvm::StructType* llvmType;
    std::map<std::string, unsigned> fieldIndices; // Map field name to index
    bool isStruct; // True if struct, false if class (or could be enum later)
    bool isReprC = false;
    // Potentially: vtable, parent type info, etc.
};

// Helper struct to manage loop context
struct LoopContext {
    llvm::BasicBlock *loopHeader; // Block for the loop condition check
    llvm::BasicBlock *loopBody;   // Block for the loop body
    llvm::BasicBlock *loopUpdate; // Block for the loop increment/update
    llvm::BasicBlock *loopExit;   // Block after the loop
};

// Helper struct to manage value-yielding contexts (select expressions and
// match-as-value expressions). Both `select` arms and `match` block arms yield
// a value via the `pass` statement, which stores into resultAlloca and branches
// to endBlock.
struct YieldContext {
    llvm::BasicBlock *endBlock;   // Block after the yielding expression
    llvm::AllocaInst *resultAlloca; // Alloca for storing the result
};

class LLVMCodegen : public ast::Visitor {
public:
    // explicit LLVMCodegen(); // Old constructor
    explicit LLVMCodegen(Driver& driver); // Constructor expects a Driver reference
    virtual ~LLVMCodegen(); // Add virtual destructor declaration

    void generate(vyb::ast::Module* astModule, const std::string& outputFilename); // Add declaration
    void dumpIR() const; // Add declaration
    std::unique_ptr<llvm::Module> releaseModule(); // Add declaration
    std::unique_ptr<llvm::LLVMContext> releaseContext(); // Add declaration for context release
    llvm::Module* getModule() const { return module.get(); } // Add method to get module pointer without releasing

private:
    Driver& driver_; // Add a Driver reference
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;

    // Debug information support
    std::unique_ptr<llvm::DIBuilder> debugBuilder;
    llvm::DICompileUnit* debugCompileUnit;
    llvm::DIFile* debugFile;
    std::stack<llvm::DIScope*> debugScopeStack;

    // Basic LLVM types
    llvm::Type* voidType;
    llvm::Type* int1Type; // For booleans
    llvm::Type* int8Type;
    llvm::Type* int32Type;
    llvm::Type* int64Type;
    llvm::Type* floatType;
    llvm::Type* doubleType;
    llvm::Type* int8PtrType; // Generic pointer type (char*)
    llvm::StructType* rttiStructType; // For RTTI objects
    llvm::Type* stringType; // Placeholder for Vyb's string type representation

    // Current state
    llvm::Type* m_currentLLVMType = nullptr; // Initialize

    llvm::Value* m_currentLLVMValue = nullptr; // Unified value propagation

    // Scope and symbol management
    llvm::Function* currentFunction = nullptr; // Initialize
    vyb::ast::FunctionDeclaration* currentFunctionAST = nullptr; // Track AST node for error propagation
    llvm::StructType* currentClassType = nullptr; // Initialize
    LoopContext currentLoopContext;
    std::vector<LoopContext> loopStack;
    std::vector<YieldContext> yieldContextStack_;  // Track nested select/match yield expressions
    bool infer_types_only = false;  // Flag for type inference without codegen
    std::map<std::string, llvm::AllocaInst*> m_currentFunctionNamedValues;

    // Defer support: stack of deferred statement lists, one per function scope
    std::vector<std::vector<vyb::ast::Statement*>> m_deferStack;


    // Global and type information
    std::map<std::string, llvm::Value*> namedValues;
    std::map<std::string, UserTypeInfo> userTypeMap;
    std::map<std::string, llvm::Type*> typeParameterMap;
    std::map<std::string, llvm::Type*> typeAliasMap; // Maps type alias names to their underlying LLVM types
    std::map<vyb::ast::TypeNode*, llvm::Type*> m_typeCache;
    std::map<llvm::Value*, std::shared_ptr<vyb::ast::TypeNode>> valueTypeMap; // Maps LLVM values to AST types
    std::map<std::string, llvm::FunctionType*> localLambdaTypes; // Maps lambda variable name to its function type
    vyb::ast::TypeNode* m_currentImplTypeNode = nullptr; // Initialize
    std::string m_currentImplTraitName;
    vyb::ast::Module* m_currentVybModule = nullptr;
    bool m_isLHSOfAssignment = false;
    bool verbose = false;  // Controls detailed warning output
    bool m_isMemberAccessBase = false; // Controls Identifier behavior for member access
    // Auto-serialization: when main() has a non-Int, non-Void, non-String return type,
    // its LLVM return type is changed to void and the value is serialized and printed.
    // This member holds the original return type so cgen_stmt knows how to serialize.
    llvm::Type* m_mainAutoSerializeOrigRetType = nullptr;
    llvm::Type* m_asyncResultType = nullptr;  // Result type T for Future<T> in async context
    llvm::Type* m_currentCallResultType = nullptr;  // Result type from most recent function call

    // Ownership and scope tracking
    struct ScopeVariable {
        std::string name;
        llvm::Value* allocaInst;  // The alloca instruction for the variable
        llvm::Value* value;       // Current value (may be loaded from alloca)
        ast::OwnershipKind ownership;
        bool needsCleanup;
        llvm::Type* type;
        bool isVecWithMallocData; // Tracks if this is a Vec that owns malloc'd data
    };
    std::vector<std::vector<ScopeVariable>> scopeStack;
    std::map<std::string, uint32_t> refCounts; // For our<T> reference counting
    std::map<std::string, llvm::Value*> refCountStorage; // Storage for refcount variables

    // Error handling state
    struct TrapContext {
        llvm::BasicBlock* landingPad;        // Landing pad for error handling
        llvm::BasicBlock* resumeBlock;       // Block to resume to after handling
        llvm::Value* errorSlot;              // Heap-allocated slot for error pointer
        ast::TypeNode* errorType;            // Expected error type
        std::string errorVarName;            // Name of error variable
        llvm::BasicBlock* ensureBlock;       // Ensure block to run before resuming (if any)
        llvm::AllocaInst* resultAlloca;      // Result alloca for storing handler return values
    };
    std::vector<TrapContext> trapStack;      // Stack of active trap contexts
    bool inTrapHandler = false;           // True when executing trap handler body
    int currentTrapHandlerIndex = -1;     // Index of current trap handler being executed
    std::vector<llvm::BasicBlock*> ensureBlocks; // Ensure cleanup blocks to execute
    std::vector<llvm::Value*> trapHandlerReturnValues; // Return values from trap handlers (for PHI node)
    llvm::AllocaInst* currentErrorSlot = nullptr; // Current error being handled

    // Stack trace capture for error handling (Phase 6.4)
    struct CallStackFrame {
        std::string functionName;       // Vyb function name
        SourceLocation location;        // Source location of function definition
        llvm::Function* llvmFunction;   // LLVM function pointer
    };
    std::vector<CallStackFrame> callStack; // Runtime call stack for error reporting

    // Monomorphization: Generic type instantiation
    std::map<std::string, vyb::ast::StructDeclaration*> genericStructTemplates; // Store generic struct AST nodes (e.g., Box<T>)
    std::map<std::string, llvm::StructType*> monomorphizedStructs; // Cache instantiated types (e.g., "Box<Int>" -> Box_Int LLVM type)
    std::map<std::string, llvm::GlobalVariable*> typeMetadataGlobals; // Type metadata for JSON serialization

    // Generic function templates
    std::map<std::string, vyb::ast::FunctionDeclaration*> genericFunctionTemplates; // Store generic function AST nodes (e.g., printItem<T>)
    std::map<std::string, llvm::Function*> monomorphizedFunctions; // Cache instantiated functions (e.g., "printItem_Point" -> Function*)

    // Enum variant integer constants: "EnumName::VariantName" -> constant i64
    std::map<std::string, llvm::Constant*> enumVariantValues;
    // Set of declared enum type names (for quick lookup)
    std::set<std::string> enumTypeNames;

    // Helper methods
    llvm::Type* codegenType(vyb::ast::TypeNode* typeNode); // Converts vyb::TypeNode to llvm::Type
    std::string mangleGenericTypeName(const std::string& baseName, const std::vector<vyb::ast::TypeNodePtr>& typeArgs); // Generate mangled name like Box_Int
    llvm::StructType* monomorphizeStruct(const std::string& baseName, const std::vector<vyb::ast::TypeNodePtr>& typeArgs); // Generate specialized struct
    void generateTypeMetadata(const std::string& typeName, vyb::ast::StructDeclaration* structDecl); // Generate type metadata for JSON/reflection
    void registerTypeMetadata(); // Register all type metadata at program startup
    llvm::Function* getCurrentFunction();
    llvm::BasicBlock* getCurrentBasicBlock();
    void createFunctionForwardDeclaration(vyb::ast::FunctionDeclaration* funcDecl); // Forward declaration helper

    // Error and warning reporting
    void logError(const SourceLocation& loc, const std::string& message);
    void logWarning(const SourceLocation& loc, const std::string& message); // Added this line
    llvm::Value* createEntryBlockAlloca(llvm::Function* func, const std::string& varName, llvm::Type* type);
    llvm::AllocaInst* createEntryBlockAlloca(llvm::Type* type, const std::string& name);


    // Type system helpers
    std::string getTypeName(llvm::Type* type);
    llvm::Type* getPointeeTypeInfo(llvm::Value* ptr);
    llvm::Function* getLitConversionFunction();
    bool isLitIntrinsicCall(vyb::ast::Expression* expr);
    bool functionBodyReturnsLitIntrinsic(vyb::ast::BlockStatement* body);
    std::string extractOriginalTypeNameFromSemantics(vyb::ast::Expression* expr);
    std::string extractOriginalTypeNameFromAST(vyb::ast::Expression* expr);

    llvm::Value* tryCast(llvm::Value* value, llvm::Type* targetType, const vyb::SourceLocation& loc);

    // Generic trait method monomorphization
    struct TypePattern {
        std::string base;                    // e.g., "Box"
        std::vector<std::string> args;       // e.g., ["Int"] or ["T"]

        static TypePattern parse(const std::string& typeStr);
        bool matchesPattern(const TypePattern& concrete, std::map<std::string, std::string>& substitutions) const;
        std::string toMangled() const;       // e.g., "Box<Int>" -> "Box_Int"
    };

    llvm::Function* monomorphizeTraitMethod(const std::string& concreteType,
                                           const std::string& traitName,
                                           const std::string& methodName);
    std::string extractBasePattern(const std::string& concreteType);
    std::string getFullTypeName(vyb::ast::Expression* expr);
    vyb::ast::TypeNodePtr typePatternToTypeNode(const TypePattern& pattern,
                                                const vyb::SourceLocation& loc);

    // Generic function monomorphization
    llvm::Function* monomorphizeGenericFunction(const std::string& functionName,
                                               const std::vector<std::string>& concreteTypeArgs);
    std::string mangleGenericFunctionName(const std::string& baseName,
                                          const std::vector<std::string>& typeArgs);

    // Helper methods for monomorphization with type substitution
    llvm::Type* resolveTypeForMonomorphization(const TypePattern& pattern,
                                               const std::map<std::string, std::string>& substitutions);
    llvm::Type* resolveParameterTypeWithSubstitution(vyb::ast::TypeNode* typeNode,
                                                     const std::map<std::string, std::string>& substitutions);
    llvm::Type* resolveReturnTypeWithSubstitution(vyb::ast::TypeNode* typeNode,
                                                  const std::map<std::string, std::string>& substitutions);

    // Current type substitutions active during monomorphization
    std::map<std::string, std::string> currentTypeSubstitutions;

    // Cache for monomorphized trait methods: "Box<Int>::show" -> Function*
    std::map<std::string, llvm::Function*> monomorphizedMethods;

    // String operations
    llvm::Value* generateStringConcatenation(llvm::Value* leftStr, llvm::Value* rightStr, SourceLocation loc);
    llvm::Value* generateStringComparison(llvm::Value* leftStr, llvm::Value* rightStr, vyb::TokenType op);

    // Array serialization
    llvm::Value* generateArraySerialization(llvm::Value* arrayPtr, vyb::ast::ArrayType* arrayType);
    llvm::Value* generateGenericSerialization(llvm::Value* objPtr, vyb::ast::TypeNode* typeNode);
    llvm::Value* generateIntToString(llvm::Value* intValue);
    llvm::Value* generateFloatToString(llvm::Value* floatValue);
    llvm::Value* generateBoolToString(llvm::Value* boolValue);
    llvm::Function* getSprintfFunction();

    // ToString conversion helpers for mixed-type string concatenation
    llvm::Value* generateToStringCall(llvm::Value* value, llvm::Type* valueType, vyb::ast::TypeNode* astType, SourceLocation loc);
    llvm::Value* generateMixedStringConcatenation(llvm::Value* leftValue, llvm::Value* rightValue,
                                                vyb::ast::TypeNode* leftTypeNode, vyb::ast::TypeNode* rightTypeNode, SourceLocation loc);
    std::string resolveTypeAliasToBaseName(vyb::ast::TypeNode* typeNode);

    // IO operations
    llvm::Function* getPrintlnFunction();
    llvm::Function* getVybPrintlnFunction();
    llvm::Function* getVybPrintFunction();   // print() - no newline
    llvm::Function* getVybPrintlnIntFunction();  // println_int()
    llvm::Function* getVybPrintIntFunction();    // print_int()
    llvm::Function* getVybPrintlnBoolFunction(); // println_bool()
    llvm::Function* getVybPrintBoolFunction();   // print_bool()
    llvm::Function* getSerializeToJsonFunction();

    // Error handling runtime functions
    llvm::Function* getVybPanicFunction();
    llvm::Function* getVybUntrappedErrorFunction();

    // Error handling helpers
    void setupTrapContext(ast::BlockExpression* blockExpr, llvm::BasicBlock* continueBB);
    void cleanupTrapContext();
    llvm::Value* createErrorValue(ast::Expression* errorExpr, ast::TypeNode* errorType);
    void preCreateTrapAllocas(ast::Statement* stmt, llvm::Function* func, llvm::Instruction** lastAllocaInsertPt = nullptr);
    void emitDeferredStatementsForCurrentFunction();
    void emitPropagatingErrorReturn(llvm::Value* errorPtr);

    // Stack trace helpers (Phase 6.4)
    void pushCallStackFrame(const std::string& functionName, const SourceLocation& loc, llvm::Function* llvmFunc);
    void popCallStackFrame();
    llvm::GlobalVariable* createCallStackGlobal();
    void generatePushFrameCall(const std::string& functionName, const SourceLocation& loc);
    void generatePopFrameCall();

    // Vec operations
    void handleVecMethod(vyb::ast::CallExpression* node, const std::string& objectName, const std::string& methodName);
    void handleVecMethodOnValue(vyb::ast::CallExpression* node, llvm::Value* vecValue, const std::string& methodName, vyb::ast::Expression* objectExpr);
    void handleVecPush(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecPop(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecLen(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecGet(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecPushArray(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecToArray(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecClear(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecIsEmpty(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecCapacity(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecConcat(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecContains(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecRemoveAt(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecResize(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecGetArray(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecGetVec(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);

    // String type methods
    void handleStringMethod(vyb::ast::CallExpression* node, const std::string& objectName, const std::string& methodName);
    void handleStringLen(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType);
    void handleStringConcat(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType);
    void handleStringSubstring(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType);
    void handleStringCharAt(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType);
    void handleStringToBytes(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType);
    void handleStringFromBytes(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType);
    void handleStringStartsWith(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType);
    void handleStringEndsWith(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType);
    void handleStringContains(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType);
    void handleStringToUpper(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType);
    void handleStringToLower(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType);
    void handleStringTrim(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType);
    void handleStringReplace(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType);

    // Scope and ownership management
    void enterScope();
    void exitScope();
    void registerVariable(const std::string& name, llvm::Value* allocaInst, llvm::Value* value, ast::OwnershipKind ownership, llvm::Type* type, bool needsCleanup = false);
    void cleanupVariable(const ScopeVariable& var);
    void incrementRefCount(const std::string& name);
    void decrementRefCount(const std::string& name);
    llvm::Function* getOrCreateFreeFunction();
    llvm::Function* getOrCreateMallocFunction();
    llvm::Function* getOrCreateMemsetFunction();
    llvm::Function* getOrCreateMemcpyFunction();
    llvm::StructType* getControlBlockType(llvm::Type* objectPtrType);
    bool isVecStructType(llvm::Type* type); // Check if LLVM type matches Vec{T, i64, i64} layout
    // Deep-copy a Vec struct value (clones malloc'd data so caller and callee are independent).
    // Returns an updated Vec struct value with a freshly malloc'd data buffer.
    llvm::Value* generateVecDeepCopy(llvm::Value* vecStructValue, llvm::Type* elemType, llvm::Type* vecStructType);

    // Async/await support
    struct AsyncState {
        llvm::Function* asyncFunction;
        llvm::Function* stateMachineFunction;
        llvm::StructType* stateStructType;
        llvm::Value* stateStructInstance;
        llvm::Value* currentStateValue;
        llvm::BasicBlock* resumeBlock;
        llvm::Value* futureValue;
        llvm::Type* futureResultType;  // Result type T for Future<T> (opaque pointer tracking)
        int stateCounter;
        bool isAsync;

        // Debug support for async state machines
        llvm::DILocalVariable* stateDebugVar;
        llvm::DILocalVariable* futureDebugVar;
        std::map<int, llvm::DILocation*> suspensionPointLocations;
        std::map<int, std::string> stateDescriptions;

        AsyncState() : asyncFunction(nullptr), stateMachineFunction(nullptr),
                       stateStructType(nullptr), stateStructInstance(nullptr),
                       currentStateValue(nullptr), resumeBlock(nullptr),
                       futureValue(nullptr), futureResultType(nullptr), stateCounter(0), isAsync(false),
                       stateDebugVar(nullptr), futureDebugVar(nullptr) {}
    };

    AsyncState currentAsyncState;
    llvm::Function* getOrCreateScheduleTaskFunction();
    llvm::Function* getOrCreateAwaitTaskFunction();
    llvm::Function* getOrCreateCreateFutureFunction();
    llvm::StructType* createFutureStructType(llvm::Type* resultType);

    // Ensure all core intrinsic functions are declared
    void ensureCoreIntrinsicFunctions();

    // Debug information support
    void initializeDebugInfo(const std::string& filename);
    void finalizeDebugInfo();
    llvm::DISubprogram* createDebugFunctionInfo(llvm::Function* function, const std::string& name,
                                                const SourceLocation& loc, bool isAsync = false);
    void setDebugLocation(const SourceLocation& loc);
    void pushDebugScope(llvm::DIScope* scope);
    void popDebugScope();
    llvm::DIType* getDebugType(llvm::Type* llvmType, const std::string& typeName = "");
    llvm::DILocalVariable* createDebugVariableInfo(const std::string& varName, llvm::DIType* debugType,
                                                   const SourceLocation& loc, llvm::DIScope* scope = nullptr);
    void insertDebugVariableDeclaration(llvm::DILocalVariable* debugVar, llvm::Value* alloca,
                                        const SourceLocation& loc);

    // Async state machine debug support
    void initializeAsyncStateDebugInfo(const std::string& functionName, const SourceLocation& loc);
    void createSuspensionPointDebugInfo(int stateNumber, const SourceLocation& loc, const std::string& description);
    void insertAsyncStateTransitionDebugInfo(int fromState, int toState, const SourceLocation& loc);
    void insertContinuationDebugMarker(int stateNumber, const SourceLocation& loc);

    // RTTI (Run-Time Type Information)
    llvm::StructType* getOrCreateRTTIStructType();
    llvm::Value* generateRTTIObject(const std::string& typeName, int typeId); // typeId for distinguishing types

    // Loop handling
    void pushLoop(llvm::BasicBlock* header, llvm::BasicBlock* body, llvm::BasicBlock* update, llvm::BasicBlock* exit);
    void popLoop();

    // Struct field access
    int getStructFieldIndex(llvm::StructType* structType, const std::string& fieldName);
    void bindStructPatternFields(const vyb::ast::StructPattern* node, llvm::Value* matchValue);

public:
    // Visitor methods overridden from vyb::Visitor, corrected to match ast.hpp
    // Literals
    void visit(vyb::ast::Identifier* node) override;
    void visit(vyb::ast::IntegerLiteral* node) override;
    void visit(vyb::ast::FloatLiteral* node) override;
    void visit(vyb::ast::StringLiteral* node) override;
    void visit(vyb::ast::BooleanLiteral* node) override;
    void visit(vyb::ast::ObjectLiteral* node) override;
    void visit(vyb::ast::NilLiteral* node) override;

    // Expressions
    void visit(vyb::ast::UnaryExpression* node) override;
    void visit(vyb::ast::BinaryExpression* node) override;
    void visit(vyb::ast::CallExpression* node) override; // Ensure this is declared
    void visit(vyb::ast::MemberExpression* node) override; // Ensure this is declared
    void visit(vyb::ast::AssignmentExpression* node) override; // Ensure this is declared
    void visit(vyb::ast::ArrayLiteral* node) override;
    void visit(vyb::ast::BorrowExpression* node) override;
    void visit(vyb::ast::PointerDerefExpression* node) override;
    void visit(vyb::ast::AddrOfExpression* node) override;
    void visit(vyb::ast::FromIntToLocExpression* node) override;
    void visit(vyb::ast::ArrayElementExpression* node) override;
    void visit(vyb::ast::LocationExpression* node) override;
    void visit(vyb::ast::ListComprehension* node) override;
    void visit(vyb::ast::IfExpression* node) override; // Added this line
    void visit(vyb::ast::ConstructionExpression* node) override; // Existing "Added"
    void visit(vyb::ast::ArrayInitializationExpression* node) override; // Existing "Added
    void visit(vyb::ast::TypeofExpression* node) override;
    void visit(vyb::ast::TypenameExpression* node) override;
    void visit(vyb::ast::LogicalExpression* node) override;
    void visit(vyb::ast::ConditionalExpression* node) override;
    void visit(vyb::ast::SequenceExpression* node) override;
    void visit(vyb::ast::FunctionExpression* node) override;
    void visit(vyb::ast::ThisExpression* node) override;
    void visit(vyb::ast::SuperExpression* node) override;
    void visit(vyb::ast::AwaitExpression* node) override;
    void visit(vyb::ast::RangeExpression* node) override;
    void visit(vyb::ast::BlockExpression* node) override;
    void visit(vyb::ast::SelectExpression* node) override;
    void visit(vyb::ast::ComparisonPattern* node) override;
    void visit(vyb::ast::StructPattern* node) override;

    // Add missing visit methods for expressions from ast.hpp if they are defined there
    // and are causing linker errors.
    // Based on linker errors, AssignmentExpression is already declared.
    // CallExpression and MemberExpression need to be checked if they are part of ast::Visitor
    // and implemented in LLVMCodegen.
    // It seems CallExpression and MemberExpression were expected by the vtable.

    // Statements
    void visit(vyb::ast::BlockStatement* node) override;
    void visit(vyb::ast::ExpressionStatement* node) override;
    void visit(vyb::ast::IfStatement* node) override;
    void visit(vyb::ast::WhileStatement* node) override;
    void visit(vyb::ast::ForStatement* node) override;
    void visit(vyb::ast::ReturnStatement* node) override;
    void visit(vyb::ast::PassStatement* node) override;
    void visit(vyb::ast::BreakStatement* node) override;
    void visit(vyb::ast::ContinueStatement* node) override;
    void visit(vyb::ast::UnsafeStatement* node) override;
    void visit(vyb::ast::EmptyStatement* node) override;
    void visit(vyb::ast::ExternStatement* node) override;
    void visit(vyb::ast::YieldStatement* node) override;
    void visit(vyb::ast::YieldReturnStatement* node) override;
    void visit(vyb::ast::MatchStatement* node) override; // Added this line
    void visit(vyb::ast::MatchExpression* node) override;
    void codegenMatch(vyb::ast::MatchStatement* node, llvm::AllocaInst* resultAlloca);
    void visit(vyb::ast::TryStatement* node) override; // Added this line

    // Declarations
    void visit(vyb::ast::VariableDeclaration* node) override;
    void visit(vyb::ast::FunctionDeclaration* node) override;
    void visit(vyb::ast::TypeAliasDeclaration* node) override;
    void visit(vyb::ast::ImportDeclaration* node) override;
    void visit(vyb::ast::StructDeclaration* node) override;
    void visit(vyb::ast::ClassDeclaration* node) override;
    void visit(vyb::ast::FieldDeclaration* node) override;
    void visit(vyb::ast::BindDeclaration* node) override;
    void visit(vyb::ast::EnumDeclaration* node) override;
    void visit(vyb::ast::EnumVariant* node) override;
    void visit(vyb::ast::GenericParameter* node) override;
    void visit(vyb::ast::TemplateDeclaration* node) override;
    void visit(vyb::ast::AspectDeclaration* node) override;
    void visit(vyb::ast::NamespaceDeclaration* node) override;
    void visit(vyb::ast::Module* node) override;
    void visit(vyb::ast::GenericInstantiationExpression* node) override;
    void visit(vyb::ast::ThrowStatement* node) override;

    // Error Handling
    void visit(vyb::ast::FailStatement* node) override;
    void visit(vyb::ast::TrapClause* node) override;
    void visit(vyb::ast::EnsureClause* node) override;
    void visit(vyb::ast::RethrowStatement* node) override;
    void visit(vyb::ast::PanicStatement* node) override;
    void visit(vyb::ast::ExitStatement* node) override;
    void visit(vyb::ast::DeferStatement* node) override;
    void visit(vyb::ast::TupleDestructureAssignment* node) override;

    void visit(vyb::ast::TypeNode* node) override;
    void visit(vyb::ast::AssertStatement* node) override;
    void visit(vyb::ast::TypeName* node) override;
    void visit(vyb::ast::PointerType* node) override;
    void visit(vyb::ast::ArrayType* node) override;
    void visit(vyb::ast::VecType* node) override;
    void visit(vyb::ast::FutureType* node) override;
    void visit(vyb::ast::FunctionType* node) override;
    void visit(vyb::ast::OptionalType* node) override;
    void visit(vyb::ast::TupleTypeNode* node) override;

};

} // namespace vyb
