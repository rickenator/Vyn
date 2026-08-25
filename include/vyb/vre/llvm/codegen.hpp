// SPDX-License-Identifier: Apache-2.0

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
    std::string label;            // loop label ("" = none) for labeled break/continue
    size_t scopeBaseline = 0;     // Scope-stack depth at loop entry; a break/continue
                                  // releases only scopes stacked strictly above this.
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
    // True when main()'s return argument is a lit(...) call (raw JSON passthrough).
    bool isMainReturnLitRaw() const { return m_mainReturnIsLitRaw; }

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
    bool m_currentFunctionFailable = false; // True inside a failable lambda body
    size_t m_functionScopeBaseline = 0;   // Scope-stack depth at function entry
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
    // Module-level global variables, kept visible to generated function bodies
    // even though each function starts codegen from an isolated namedValues
    // (functions swap the module scope out and back in on entry/exit).
    std::map<std::string, llvm::GlobalVariable*> globalValues_;
    // Module-level globals whose initializers are not pure compile-time
    // constants (they reference other globals, or compute a value at runtime).
    // Their value is stored in __vyb_module_init before main body runs.
    std::vector<std::pair<llvm::GlobalVariable*, vyb::ast::Expression*>> pendingGlobalInits_;
    // For mutable captures, maps the captured variable name to the address of
    // the *outer* variable's alloca, so writes inside a lambda can propagate
    // back to the enclosing scope. Populated only while generating a lambda.
    std::map<std::string, llvm::Value*> mutableCaptureOuterPointers;
    std::map<std::string, UserTypeInfo> userTypeMap;
    std::map<std::string, llvm::Type*> typeParameterMap;
    std::map<std::string, llvm::Type*> typeAliasMap; // Maps type alias names to their underlying LLVM types
    // Memo of AST TypeNode -> LLVM type. Keyed by the raw TypeNode pointer but
    // each entry also records the node's `toString()` at store time and the hit
    // is validated against the current node's string. Transient substitution
    // clones (created in `monomorphizeStruct` etc.) are freed and their heap
    // addresses reused, so a raw-pointer key alone produced stale false hits
    // (e.g. `Bool` false-resolving to `Vec`); the string check makes a stale
    // entry for a *different* type at a reused address a miss. Distinct nodes
    // with the same string are still keyed separately, preserving context that
    // can change how a type resolves (e.g. `Self` inside trait binds).
    std::map<vyb::ast::TypeNode*, std::pair<llvm::Type*, std::string>> m_typeCache;
    std::map<llvm::Value*, std::shared_ptr<vyb::ast::TypeNode>> valueTypeMap; // Maps LLVM values to AST types
    std::map<std::string, llvm::FunctionType*> localLambdaTypes; // Maps lambda variable name to its function type
    // Uniform closure representation: every lambda (capturing or not) is a
    // `struct { ptr env; ptr fn }`. The env is null for non-capturing lambdas.
    llvm::StructType* getClosureStructType();
    // User-facing function signature of the most recently generated lambda
    // (without the hidden environment parameter), for localLambdaTypes.
    llvm::FunctionType* lastLambdaFuncType = nullptr;
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
    // True when `main()`'s return argument is a `lit(...)` intrinsic call. `lit()`
    // produces an already-serialized raw JSON fragment that must pass through to
    // stdout verbatim (no JSON escaping, no surrounding quotes), unlike a genuine
    // user String return which is escaped+quoted. The JIT runner and the standalone
    // wrapper consult this to decide raw-passthrough vs escaped output.
    bool m_mainReturnIsLitRaw = false;
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
        bool isOwnedStruct;       // Tracks if this is a struct binding owning Vec/String fields
    };
    std::vector<std::vector<ScopeVariable>> scopeStack;
    // Counter for synthetic owned-struct receiver-temp alloca names (#192).
    int m_recvStructTempCounter = 0;
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
        bool disabled = false;               // Set while this trap's own handler body
                                              // is being generated, so a `fail` raised
                                              // there propagates outward instead of
                                              // re-entering the same handler.
        bool errorHandedOff = false;         // Set when this handler's `refail` re-raised
                                              // the caught error: ownership of the error
                                              // object transfers outward, so the handler
                                              // must NOT free it on exit.
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
    std::map<std::string, vyb::ast::EnumDeclaration*> genericEnumTemplates;   // Generic data-enum AST nodes (enum Box<T> { ... })
    std::map<std::string, llvm::GlobalVariable*> typeMetadataGlobals; // Type metadata for JSON serialization
    std::map<std::string, llvm::GlobalVariable*> enumMetadataGlobals; // Enum metadata for JSON serialization

    // Generic function templates
    std::map<std::string, vyb::ast::FunctionDeclaration*> genericFunctionTemplates; // Store generic function AST nodes (e.g., printItem<T>)
    std::map<std::string, llvm::Function*> monomorphizedFunctions; // Cache instantiated functions (e.g., "printItem_Point" -> Function*)
    // Declared struct constructors: struct name -> list of (arity, ctor fn name).
    // `HashMap<K,V>(n)` dispatches to the matching constructor generic function.
    std::map<std::string, std::vector<std::pair<unsigned, std::string>>> structConstructors;

    // Enum variant integer constants: "EnumName::VariantName" -> constant i64
    std::map<std::string, llvm::Constant*> enumVariantValues;
    // Set of declared enum type names (for quick lookup)
    std::set<std::string> enumTypeNames;

    // Tagged-union layout for enums that carry data variants (e.g. enum Shape { Circle(Float) }).
    // Represented as a value-semantics struct { i64 tag, [N x i8] data } where N is the
    // largest payload (in bytes) among the variants. C-like enums with no data variants
    // are represented by a single scalar i64 tag (isScalar=true) so that `Enum` values
    // interoperate with C integer-backed enums across the FFI boundary.
    struct TaggedEnumInfo {
        llvm::StructType* llvmType = nullptr;          // { i64 tag, [N x i8] data }
        unsigned payloadBytes = 0;                     // N
        bool isScalar = false;                         // C-like enum: a single i64 tag, no struct
        std::map<std::string, unsigned> variantTags;   // VariantName -> tag value
        std::map<std::string, llvm::StructType*> variantPayloadTypes; // VariantName -> payload struct (absent = unit variant)
    };
    std::map<std::string, TaggedEnumInfo> taggedEnumInfo;

    // Helper methods
    llvm::Type* codegenType(vyb::ast::TypeNode* typeNode); // Converts vyb::TypeNode to llvm::Type
    const TaggedEnumInfo* findTaggedEnum(llvm::Type* structTy) const;
    const TaggedEnumInfo* findTaggedEnum(vyb::ast::TypeNode* typeNode); // Resolve by concrete AST type name
    llvm::Value* buildTaggedEnumValue(const std::string& enumName, const std::string& variantName,
                                      std::vector<llvm::Value*> payloadVals);
    llvm::Value* extractEnumVariantField(llvm::Value* enumVal, llvm::StructType* payloadTy, unsigned fieldIdx);
    // Equality for tagged-union enum values (`==` / `!=`): compare the i64 tag
    // and, when the tags match, the payload fields of the matched variant.
    // Previously the EQEQ/NOTEQ path fed a `{ i64 tag, [N x i8] data }` struct
    // straight into ICmp, which LLVM rejects with an assert crash (#181).
    llvm::Value* generateTaggedEnumEquality(llvm::Value* L, llvm::Value* R, vyb::TokenType op,
                                            const TaggedEnumInfo& info);
    // Emit a Vec value for the builtin Vec constructor (`Vec()`, `Vec(n)`), sharing
    // the codegen between the bare `Vec(...)` and legacy `Vec::new(...)` forms.
    void emitVecConstructor(vyb::ast::CallExpression* node);
    std::string mangleGenericTypeName(const std::string& baseName, const std::vector<vyb::ast::TypeNodePtr>& typeArgs); // Generate mangled name like Box_Int
    llvm::StructType* monomorphizeStruct(const std::string& baseName, const std::vector<vyb::ast::TypeNodePtr>& typeArgs); // Generate specialized struct
    llvm::StructType* monomorphizeEnum(const std::string& baseName, const std::vector<vyb::ast::TypeNodePtr>& typeArgs);   // Generate specialized tagged-union enum
    std::vector<vyb::ast::TypeNodePtr> applyTypeSubstitutions(const std::vector<vyb::ast::TypeNodePtr>& typeArgs);
    void generateTypeMetadata(const std::string& typeName, vyb::ast::StructDeclaration* structDecl); // Generate type metadata for JSON/reflection
    void generateEnumTypeMetadata(const std::string& typeName, vyb::ast::EnumDeclaration* enumDecl);  // Generate enum metadata for JSON round-trip
    void registerTypeMetadata(); // Register all type metadata at program startup
    void registerTypeNames(); // Register all compile-time-known type names in the runtime type registry
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

    // Store a produced value into a result slot, wrapping a raw char* (e.g. the
    // result of a primitive .to_string()) into a String { ptr, i64 } struct so the
    // length field is set. Without the wrap, a char* stored into a String slot
    // leaves the length at zero and the String compares unequal / reports length 0.
    void storeIntoResultSlot(llvm::Value* value, llvm::AllocaInst* slot,
                             const vyb::SourceLocation& loc);

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
    // Register a struct's declared constructors as synthetic generic functions
    // (`__ctor_<Struct>_<N>`) and record their arities for construction dispatch.
    void registerStructConstructors(vyb::ast::StructDeclaration* node);

    // Helper methods for monomorphization with type substitution
    llvm::Type* resolveTypeForMonomorphization(const TypePattern& pattern,
                                               const std::map<std::string, std::string>& substitutions);
    llvm::Type* resolveParameterTypeWithSubstitution(vyb::ast::TypeNode* typeNode,
                                                     const std::map<std::string, std::string>& substitutions);
    llvm::Type* resolveReturnTypeWithSubstitution(vyb::ast::TypeNode* typeNode,
                                                  const std::map<std::string, std::string>& substitutions);
    std::string replaceTypeTokens(const std::string& s, const std::string& token, const std::string& repl);

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
    llvm::Value* generateTaggedEnumToString(llvm::Value* value, const TaggedEnumInfo& info, const std::string& typeName, SourceLocation loc);
    llvm::Value* generateMixedStringConcatenation(llvm::Value* leftValue, llvm::Value* rightValue,
                                                vyb::ast::TypeNode* leftTypeNode, vyb::ast::TypeNode* rightTypeNode,
                                                SourceLocation loc, bool freeLeftOwnedTemp = false, bool freeRightOwnedTemp = false);
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
    llvm::Value* buildRuntimeErrorFromValue(const std::string& typeName, llvm::Value* errorValue, const SourceLocation& loc);
    void forwardError(llvm::Value* errorPtr, const SourceLocation& loc);
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
    void handleVecSet(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    void handleVecPushArray(vyb::ast::CallExpression* node, llvm::Value* vecPtr, llvm::Type* vecStructType);
    llvm::Value* normalizeVecStringElement(llvm::Value* value);
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
    void handleStringMethodOnValue(vyb::ast::CallExpression* node, llvm::Value* strPtr, const std::string& methodName);
    void emitChannelMethod(vyb::ast::CallExpression* node, llvm::Value* handle, const std::string& methodName, bool isString, const vyb::ast::TypeNode* elem);
    llvm::Value* encodeChannelScalar(llvm::Value* payload, const vyb::ast::TypeNode* elem);
    llvm::Value* decodeChannelScalar(llvm::Value* raw, const vyb::ast::TypeNode* elem);

    void dispatchStringMethod(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType, const std::string& methodName);
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
    void handleStringFormat(vyb::ast::CallExpression* node, llvm::Value* strPtr, llvm::Type* strStructType);

    // Scope and ownership management
    void enterScope();
    void exitScope();
    void exitToFunctionBaseline();
    void cleanupScopesToBaseline(size_t baseline);
    void registerVariable(const std::string& name, llvm::Value* allocaInst, llvm::Value* value, ast::OwnershipKind ownership, llvm::Type* type, bool needsCleanup = false);
    void cleanupVariable(const ScopeVariable& var);
    void incrementRefCount(const std::string& name);
    void decrementRefCount(const std::string& name);
    llvm::Function* getOrCreateFreeFunction();
    llvm::Function* getOrCreateVybStringFreeFunction();
    llvm::Function* getOrCreateVybStringRetainFunction();
    llvm::Function* getOrCreateVybStringRegisterFunction();
    llvm::Function* getOrCreateMallocFunction();
    llvm::Function* getOrCreateClosureRetainFunction();
    llvm::Function* getOrCreateClosureReleaseFunction();
    bool isClosureStructType(llvm::Type* type);       // `{ ptr env, ptr fn }`
    bool isFnTypeNode(const vyb::ast::TypeNode* tn) const; // true for `fn` types
    void retainClosureValue(llvm::Value* closureVal);  // +1 on a copied closure value
    void releaseClosureValue(llvm::Value* closureVal); // -1 on a closure value
    void releaseClosureAlloca(llvm::Value* allocaInst); // load closure from an alloca, then -1
    // Build the per-layout destructor for a closure capture environment that
    // owns transferred `my<Struct>` payloads; reclaims each captured pointee's
    // owned fields and frees the heap block. Returns the function (or null).
    llvm::Function* generateClosureEnvDtor(
        llvm::StructType* envTy, const std::string& tag,
        const std::vector<std::pair<size_t, const vyb::ast::TypeNode*>>& ownedFields);
    // Describes one owned parameter field inside an async-task environment.
    struct AsyncEnvField {
        size_t fieldIx = 0;                    // env struct field index to reclaim
        bool isString = false;                 // a Vyb String value
        bool isVec = false;                    // a Vec<T> value
        bool vecIsString = false;              // Vec<String>: release String elements before freeing data
        bool isOur = false;                    // an `our<T>` control-block pointer (release on cleanup)
        bool isStruct = false;                 // an inline struct value (deep-copied into the env)
        bool isClosure = false;                // a closure `{ ptr env, ptr fn }` value (release env on cleanup)
        const vyb::ast::TypeNode* structType = nullptr; // AST type of the struct field (for reclaim)
    };
    // Build the per-layout destructor for an async-task environment that holds
    // inline owned param fields (String / Vec<T>): release each String buffer
    // reference or reclaim each Vec's storage, then free the heap block. Returns
    // the function (or null).
    llvm::Function* generateAsyncEnvDtor(
        llvm::StructType* envTy, const std::string& tag,
        const std::vector<AsyncEnvField>& fields);
    void retainStringValue(llvm::Value* strVal);          // +1 on a copied String value
    void releaseStringValue(llvm::Value* strVal);         // -1 on a String value
    void releaseStringAlloca(llvm::Value* allocaInst);    // load a String from an alloca, then -1
    void releaseStringElements(llvm::Value* dataPtr, llvm::Value* count); // -1 per String element in a Vec buffer
    void retainStringElements(llvm::Value* dataPtr, llvm::Value* count);  // +1 per String element in a Vec buffer
    llvm::Function* getOrCreateVybStringReleaseEachFunction();
    llvm::Function* getOrCreateVybStringRetainEachFunction();
    llvm::Function* getOrCreateMemsetFunction();
    llvm::Function* getOrCreateMemcpyFunction();
    llvm::StructType* getControlBlockType(llvm::Type* objectPtrType);
    bool isVecStructType(llvm::Type* type); // Check if LLVM type matches Vec{T, i64, i64} layout
    bool isVybStringStructType(llvm::Type* type); // `{ ptr, i64 }` Vyb String layout
    bool isOptionalStructType(llvm::Type* type); // literal `{ T, i1 }` native `T?`
    llvm::Value* generateOptionalEquality(llvm::Value* L, llvm::Value* R, vyb::TokenType op); // presence+payload == / !=
    bool exprProducesOwnedStringTemp(vyb::ast::Expression* expr); // String expr yielding a fresh owned heap buffer
    bool exprIsStringTransfer(vyb::ast::Expression* expr); // String value whose single ref transfers on stow
    bool exprIsOurTransfer(vyb::ast::Expression* expr);   // `our`/`grab`/fn-call value whose fresh strong ref transfers on stow
    bool exprIsMildTransfer(vyb::ast::Expression* expr);  // `soft(...)` value whose fresh weak ref transfers on stow
    // Deep-copy a Vec struct value (clones malloc'd data so caller and callee are independent).
    // Returns an updated Vec struct value with a freshly malloc'd data buffer.
    // When `astElemType` is supplied and names a struct that owns heap data, each
    // element is deep-copied individually (retaining String buffers, cloning inner
    // Vecs) instead of a shallow memcpy, so the clone's owned fields never alias the
    // source (which would double-free when both are reclaimed).
    llvm::Value* generateVecDeepCopy(llvm::Value* vecStructValue, llvm::Type* elemType, llvm::Type* vecStructType,
                                     const vyb::ast::TypeNode* astElemType = nullptr);
    // Deep-copy a struct value into an independent owned copy: retain String
    // buffers and our/mild control blocks, clone Vec buffers and `my<Struct>`
    // heap blocks, and recurse into nested structs (scalars copied by value).
    // Mirrors reclaimStructOwnedFieldsAt so reclaiming the result balances every
    // action taken here. Returns an updated struct value.
    llvm::Value* generateStructDeepCopy(llvm::Value* structValue,
                                        const vyb::ast::TypeNode* astType,
                                        llvm::StructType* llvmTy);
    // Deep-copy a standalone `my<Struct>` heap payload so the new owner holds data
    // independent of the caller's. Mirrors generateStructDeepCopy's field handling.
    llvm::Value* deepCopyMyStruct(llvm::Value* myPtr,
                                  const vyb::ast::TypeNode* pointeeAst,
                                  llvm::StructType* pointeeTy);

    // Owned-field introspection + reclaim for struct-typed storage. Resolves a
    // struct's concrete field type nodes (substituting generic args) in layout
    // order; reclaims each owned field's heap buffer/String reference on scope exit.
    bool collectStructConcreteFieldTypes(const vyb::ast::TypeNode* astType,
                                         std::vector<vyb::ast::TypeNodePtr>& out) const;
    bool isVecOfStringTypeNode(const vyb::ast::TypeNode* tn) const;
    bool isKnownStructTypeNode(const vyb::ast::TypeNode* tn) const;
    bool isMyOwnedStructTypeNode(const vyb::ast::TypeNode* tn) const;
    const vyb::ast::TypeNode* myPointeeOf(const vyb::ast::TypeNode* tn) const;
    bool structTypeHasOwnedFields(const vyb::ast::TypeNode* astType) const;
    bool scopeVarIsOwnedStruct(const ScopeVariable& var) const;
    void reclaimOwnedStructAt(llvm::Value* structPtr, const vyb::ast::TypeNode* astType,
                              llvm::StructType* llvmTy);
    void reclaimStructOwnedFieldsAt(llvm::Value* structPtr, const vyb::ast::TypeNode* astType,
                                    llvm::StructType* llvmTy, std::set<std::string>& visited);

    bool isOurRefType(const vyb::ast::TypeNode* tn) const;   // `our<...>` wrapper type node
    bool isMildRefType(const vyb::ast::TypeNode* tn) const;  // `mild<...>` wrapper type node

    // Pointee type node `T` of an `our<T>` / `mild<T>` wrapper (or null when the
    // node is not that ref wrapper). Used to reclaim a struct payload's owned
    // fields when its strong count drops to zero.
    const vyb::ast::TypeNode* refPointeeOf(const vyb::ast::TypeNode* tn, const std::string& kind) const;
    const vyb::ast::TypeNode* ourPointeeOf(const vyb::ast::TypeNode* tn) const;
    const vyb::ast::TypeNode* mildPointeeOf(const vyb::ast::TypeNode* tn) const;

    // Retain an `our`/`mild` refcount control block: bump the strong (our) or
    // weak (mild) count on the shared block so a new storage location that will
    // release on scope exit holds its own reference. `controlBlockPtr` may be null.
    void retainOurControlBlock(llvm::Value* controlBlockPtr, const std::string& tag);
    // Release an `our`/`mild` refcount control block (shared by top-level
    // bindings and struct fields). `controlBlockPtr` may be null. When the strong
    // count drops to zero, the payload struct is freed; if the pointee type is a
    // struct with owned fields, those fields are reclaimed first so nested
    // resources (inner our/mild refs, Vec storage, String buffers, my blocks) are
    // not leaked. `pointeeAst`/`pointeeLlvm` may be null (treated as scalar/no-op).
    void releaseOurControlBlock(llvm::Value* controlBlockPtr, const std::string& tag,
                                const vyb::ast::TypeNode* pointeeAst = nullptr,
                                llvm::Type* pointeeLlvm = nullptr);
    void releaseMildControlBlock(llvm::Value* controlBlockPtr, const std::string& tag);
    // Retain a `mild` weak-count control block: bump the weak count so a new
    // storage location that will release on scope exit holds its own weak ref.
    // `controlBlockPtr` may be null.
    void retainMildControlBlock(llvm::Value* controlBlockPtr, const std::string& tag);

    // Data-carrying built-in enums (Option<T>, Result<T, E>) whose payload is an
    // `our<T>` reference own a strong count on a shared control block. Copies
    // (Some(owner)) must retain, and scope exit must release, so the control
    // block is reclaimed once the last owner drops.
    bool enumPayloadHoldsOurRef(const vyb::ast::TypeNode* astType) const;
    void reclaimEnumOurPayload(llvm::Value* enumPtr, const vyb::ast::TypeNode* astType,
                               bool retain);
    bool enumInitIsOurTransfer(vyb::ast::Expression* init);

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
    /// Cache of Future<T> struct types keyed by their result type so that all
    /// references to a given Future<T> (async fn return, explicit variable type,
    /// launcher) share one canonical LLVM struct type instead of distinct ones.
    std::map<llvm::Type*, llvm::StructType*> futureStructCache;
    llvm::StructType* createFutureStructType(llvm::Type* resultType);
    /// Phase-1/2 real async: an `async fn(...)<Future<Int>>` runs its body as a
    /// task on the cooperative event loop. Splits the declaration into a public
    /// launcher (`fn(params...)` returns a Future, spawning the task with a
    /// closure env snapshotting the args) and a hidden worker
    /// (`<fn>$__async_body` is a plain `fn(params...) -> Int`) that runs the
    /// body, dispatched from an `i64(void*)` entry trampoline that unpacks env.
    void codegenAsyncTask(vyb::ast::FunctionDeclaration* node);
    /// Async lambda: `async |x| -> await process(x)`. The lambda body is compiled
    /// as a normal closure (reused worker); the outer closure value launches a
    /// cooperative task that runs that closure and returns a Future<T> to the
    /// caller. Mirrors the worker + env snapshot + entry + launcher split that
    /// real async functions use, composed with the closure {env,fn} value.
    void codegenAsyncLambda(vyb::ast::FunctionExpression* node);

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
    void pushLoop(llvm::BasicBlock* header, llvm::BasicBlock* body, llvm::BasicBlock* update, llvm::BasicBlock* exit, const std::string& label = "");
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
    void visit(vyb::ast::AsExpression* node) override;
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
    void visit(vyb::ast::RefailStatement* node) override;
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
