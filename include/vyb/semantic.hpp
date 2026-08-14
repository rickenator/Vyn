#pragma once
/**
 * @file semantic.hpp
 * @brief This is the primary semantic analysis header for the Vyb compiler.
 *
 * IMPORTANT: This file was previously duplicated as include/vyb/vre/semantic.hpp
 * which was removed during cleanup. Do not create duplicate headers!
 *
 * The implementation for this header is in src/vre/semantic.cpp
 */

#include "vyb/parser/ast.hpp" // Ensure ast.hpp is included
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <unordered_set>
#include <map>

namespace vyb {

class Driver; // Forward declaration

namespace ast {
class Node;
class Module;
class ExpressionStatement;
class BlockStatement;
class IfStatement;
class ReturnStatement;
class WhileStatement;
class ForStatement;
class BreakStatement;
class ContinueStatement;
class ImportStatement; // Should be ImportDeclaration
class ExternStatement;
class TryCatchStatement; // Should be TryStatement
class ThrowStatement;
class MatchStatement;
class YieldStatement;
class YieldReturnStatement;
class AssertStatement;
class EmptyStatement;
class UnsafeStatement;

// Missing forward declarations for expressions
class Identifier;
class IntegerLiteral;
class UnaryExpression;
class BinaryExpression;
class CallExpression;
class ArrayElementExpression;
class MemberExpression;
class AssignmentExpression;
class LogicalExpression; // Add forward declaration
class ConditionalExpression; // Add forward declaration
class SequenceExpression; // Add forward declaration
class ObjectLiteral;
class ArrayLiteral;
class FunctionExpression; // Add forward declaration
class StringLiteral;
class FloatLiteral;
class BooleanLiteral;
class NilLiteral;
class ThisExpression; // Add forward declaration
class SuperExpression; // Add forward declaration
class AwaitExpression; // Add forward declaration
class ListComprehension;
class GenericInstantiationExpression;
class PointerDerefExpression;
class AddrOfExpression;
class FromIntToLocExpression;
class LocationExpression;
class ConstructionExpression; // Add forward declaration

// Missing forward declarations for declarations
class VariableDeclaration;
class FunctionDeclaration;
class StructDeclaration;
class EnumDeclaration;
class TypeAliasDeclaration;
class AspectDeclaration; // Add forward declaration
class BindDeclaration;
class NamespaceDeclaration; // Add forward declaration

// Missing forward declarations for types
class TypeName; // Add forward declaration
class PointerType; // Add forward declaration
class ArrayType; // Add forward declaration
class FunctionType; // Add forward declaration
class OptionalType; // Add forward declaration
class GenericParameter;

class Visitor; // Forward declaration if it\'s in the ast namespace and defined elsewhere
}

struct BorrowInfo {
    std::string ownerName;
    bool isMutable;
    ast::Node* borrowNode; // The AST node that created the borrow
    // Potentially add ast::TypeNode* of the borrowed type if needed for more complex checks
};

struct SymbolInfo {
    enum class Kind { Variable, Function, Type, TYPE_PARAMETER };
    Kind kind;
    std::string name;
    bool isConst = false;
    ast::OwnershipKind ownershipKind = ast::OwnershipKind::MY; // Default to unique ownership
    ast::TypeNode* type = nullptr;
    std::vector<std::string> bounds; // For TYPE_PARAMETER: aspect bounds (e.g., ["Display", "Clone"])
};

class Scope {
public:
    Scope(Scope* parent_scope = nullptr);
    SymbolInfo* find(const std::string& name);
    SymbolInfo* lookupDirect(const std::string& name); // Added declaration
    void insert(const std::string& name, SymbolInfo* symbol);
    Scope* getParent();

private:
    std::unordered_map<std::string, SymbolInfo*> symbols;
    Scope* parent;
};

class SymbolTable {
public:
    SymbolTable(SymbolTable* parent = nullptr) : parent(parent), isUnsafeBlock(false), isLoop(false) {}
    void add(const SymbolInfo& sym) { table[sym.name] = sym; }
    SymbolInfo* lookup(const std::string& name) {
        auto it = table.find(name);
        if (it != table.end()) return &it->second;
        if (parent) return parent->lookup(name);
        return nullptr;
    }
    SymbolInfo* lookupDirect(const std::string& name); // Added
    SymbolTable* getParent() { return parent; }
    bool isUnsafeBlock;
    bool isLoop;

private:
    std::unordered_map<std::string, SymbolInfo> table;
    SymbolTable* parent;
};

// Information about a generic aspect binding (e.g., bind<T> Display -> Box<T>)
// Declared before SemanticAnalyzer to allow use in getter return type
struct GenericImplInfo {
    std::string typePattern;           // e.g., "Box<T>"
    std::string traitName;             // e.g., "Display"
    std::vector<std::string> typeParams; // e.g., ["T"]
    bool isBounded = false;            // true when any type parameter declares aspect bounds
    ast::BindDeclaration* declaration; // Original AST node
    std::map<std::string, ast::FunctionDeclaration*> methods; // method name -> AST
    std::map<std::string, ast::TypeNode*> associatedTypeBindings; // associated type name -> assigned type AST

    GenericImplInfo(ast::BindDeclaration* decl) : declaration(decl) {
        if (decl->selfType) {
            typePattern = decl->selfType->toString();
        }
        if (decl->traitType) {
            traitName = decl->traitType->toString();
        }

        // Extract type parameters
        for (const auto& param : decl->genericParams) {
            if (param && param->name) {
                typeParams.push_back(param->name->name);
                if (!param->bounds.empty()) {
                    isBounded = true;
                }
            }
        }

        // Store methods
        for (const auto& method : decl->methods) {
            if (method && method->id) {
                methods[method->id->name] = method.get();
            }
        }

        for (const auto& assocBinding : decl->associatedTypeBindings) {
            if (assocBinding.name && assocBinding.valueType) {
                associatedTypeBindings[assocBinding.name->name] = assocBinding.valueType.get();
            }
        }
    }
};

// Information about aspect/trait methods and the trait itself
// Declared before SemanticAnalyzer for use in public API
struct TraitMethod {
    std::string name;
    std::vector<std::string> parameterNames;
    std::vector<ast::TypeNode*> parameterTypes;
    ast::TypeNode* returnType;
    ast::FunctionDeclaration* declaration; // Original AST node
    bool hasDefaultImpl;
};

struct TraitInfo {
    std::string name;
    std::vector<std::string> genericParams;
    std::vector<std::string> superTraits; // Aspect inheritance
    std::vector<std::string> associatedTypes;
    // Default types for associated types, index-aligned with associatedTypes
    // (nullptr = no default declared).
    std::vector<ast::TypeNode*> associatedTypeDefaults;
    // Aspect bounds for each associated type, index-aligned with associatedTypes
    // (empty vector = no bound declared).
    std::vector<std::vector<ast::TypeNode*>> associatedTypeConstraints;
    std::vector<TraitMethod> methods;
    ast::AspectDeclaration* declaration; // Original AST node

    // Returns the declared default type for an associated type, or nullptr.
    ast::TypeNode* getAssociatedTypeDefault(const std::string& assocName) const {
        for (size_t i = 0; i < associatedTypes.size() && i < associatedTypeDefaults.size(); ++i) {
            if (associatedTypes[i] == assocName) {
                return associatedTypeDefaults[i];
            }
        }
        return nullptr;
    }

    // Returns the declared aspect bounds for an associated type (its types),
    // or nullptr if the associated type has no bound.
    const std::vector<ast::TypeNode*>* getAssociatedTypeConstraints(const std::string& assocName) const {
        for (size_t i = 0; i < associatedTypes.size() && i < associatedTypeConstraints.size(); ++i) {
            if (associatedTypes[i] == assocName) {
                return &associatedTypeConstraints[i];
            }
        }
        return nullptr;
    }

    TraitInfo(ast::AspectDeclaration* decl) : declaration(decl) {
        if (decl->name) {
            name = decl->name->name;
        }

        // Extract generic parameters
        for (const auto& param : decl->genericParams) {
            if (param && param->name) {
                genericParams.push_back(param->name->name);
            }
        }

        // Extract super-aspects
        for (const auto& super : decl->superTypes) {
            if (super) {
                superTraits.push_back(super->name);
            }
        }

        for (size_t i = 0; i < decl->associatedTypes.size(); ++i) {
            const auto& associatedType = decl->associatedTypes[i];
            if (associatedType) {
                associatedTypes.push_back(associatedType->name);
                ast::TypeNode* def = nullptr;
                if (i < decl->associatedTypeDefaults.size()) {
                    def = decl->associatedTypeDefaults[i].get();
                }
                associatedTypeDefaults.push_back(def);

                std::vector<ast::TypeNode*> cons;
                if (i < decl->associatedTypeConstraints.size()) {
                    for (const auto& c : decl->associatedTypeConstraints[i]) {
                        cons.push_back(c.get());
                    }
                }
                associatedTypeConstraints.push_back(std::move(cons));
            }
        }

        // Extract methods
        for (const auto& method : decl->methods) {
            if (method) {
                TraitMethod tm;
                if (method->id) {
                    tm.name = method->id->name;
                }

                // Extract parameters
                for (const auto& param : method->params) {
                    tm.parameterNames.push_back(param.name->name);
                    tm.parameterTypes.push_back(param.typeNode.get());
                }

                tm.returnType = method->returnTypeNode.get();
                tm.declaration = method.get();
                tm.hasDefaultImpl = (method->body != nullptr);

                methods.push_back(std::move(tm));
            }
        }
    }
};

class SemanticAnalyzer : public ast::Visitor {
public:
    explicit SemanticAnalyzer(Driver& driver);
    void analyze(ast::Module* root);
    const std::vector<std::string>& getErrors() const { return errors; }

    // Access to generic trait implementations for monomorphization
    const std::unordered_map<std::string, std::unordered_map<std::string, std::unique_ptr<GenericImplInfo>>>&
    getGenericTraitImpls() const { return genericTraitImpls; }

    // Access to concrete trait implementations (bind Aspect -> Type)
    const std::unordered_map<std::string, std::unordered_map<std::string, std::vector<ast::FunctionDeclaration*>>>&
    getTraitImpls() const { return traitImpls; }

    // Access to aspect/trait registry
    const std::unordered_map<std::string, std::unique_ptr<TraitInfo>>&
    getTraitRegistry() const { return traitRegistry; }

    std::string resolveAssociatedTypeForType(const std::string& typeName,
                                             const std::string& traitName,
                                             const std::string& typeReference) const {
        return resolveAssociatedTypeReference(typeName, traitName, typeReference);
    }

    // Helper methods
    bool isInLoop();
    bool isInUnsafeBlock();
    bool isIntegerType(ast::TypeNode* type);
    bool isRawLocationType(ast::TypeNode* type); // Added to support location type checking
    bool isReservedWord(const std::string& name);
    bool isLValue(ast::Expression* expr);
    std::string borrowedRootName(ast::Expression* expr);
    bool areTypesCompatible(ast::TypeNode* typeA, ast::TypeNode* typeB); // Added
    std::shared_ptr<ast::TypeNode> cloneTypeNode(ast::TypeNode* type); // Helper to clone type nodes
    ast::TypeNode* substituteSelfType(ast::TypeNode* returnType, const std::string& concreteType); // Substitute Self with concrete type
    void handleVecMethodCall(ast::CallExpression* node, const std::string& objectName, const std::string& methodName);
    void handleVecMethodCallOnMember(ast::CallExpression* node, ast::VecType* vecType, const std::string& methodName);
    void handleQualifiedAspectCall(ast::CallExpression* node, const std::string& aspectName, const std::string& methodName);

    // Statements
    void visit(ast::BlockStatement* node) override;
    void visit(ast::ExpressionStatement* node) override;
    void visit(ast::IfStatement* node) override;
    void visit(ast::ForStatement* node) override;
    void visit(ast::WhileStatement* node) override;
    void visit(ast::ReturnStatement* node) override;
    void visit(ast::PassStatement* node) override;
    void visit(ast::BreakStatement* node) override;
    void visit(ast::ContinueStatement* node) override;
    void visit(ast::TryStatement* node) override;
    void visit(ast::UnsafeStatement* node) override;
    void visit(ast::EmptyStatement* node) override;
    void visit(ast::AssertStatement* node) override;
    void visit(ast::MatchStatement* node) override;
    void visit(ast::MatchExpression* node) override;
    void visit(ast::YieldStatement* node) override;
    void visit(ast::YieldReturnStatement* node) override;
    void visit(ast::ExternStatement* node) override; // Added
    void visit(ast::ThrowStatement* node) override; // Added

    // Error Handling
    void visit(ast::FailStatement* node) override;
    void visit(ast::TrapClause* node) override;
    void visit(ast::EnsureClause* node) override;
    void visit(ast::RethrowStatement* node) override;
    void visit(ast::PanicStatement* node) override;
    void visit(ast::ExitStatement* node) override;
    void visit(ast::DeferStatement* node) override;
    void visit(ast::TupleDestructureAssignment* node) override;

    // Expressions
    void visit(ast::Identifier* node) override;
    void visit(ast::IntegerLiteral* node) override;
    void visit(ast::UnaryExpression* node) override;
    void visit(ast::BinaryExpression* node) override;
    void visit(ast::CallExpression* node) override;
    void visit(ast::ArrayElementExpression* node) override;
    void visit(ast::MemberExpression* node) override;
    void visit(ast::AssignmentExpression* node) override;
    void visit(ast::LogicalExpression* node) override;
    void visit(ast::ConditionalExpression* node) override;
    void visit(ast::SequenceExpression* node) override;
    void visit(ast::ObjectLiteral* node) override;
    void visit(ast::ArrayLiteral* node) override;
    void visit(ast::FunctionExpression* node) override;
    void visit(ast::StringLiteral* node) override;
    void visit(ast::FloatLiteral* node) override;
    void visit(ast::BooleanLiteral* node) override;
    void visit(ast::NilLiteral* node) override;
    void visit(ast::ThisExpression* node) override;
    void visit(ast::SuperExpression* node) override;
    void visit(ast::AwaitExpression* node) override;
    void visit(ast::RangeExpression* node) override;
    void visit(ast::BlockExpression* node) override;
    void visit(ast::SelectExpression* node) override;
    void visit(ast::ComparisonPattern* node) override;
    void visit(ast::StructPattern* node) override;
    void visit(ast::ListComprehension* node) override;
    void visit(ast::GenericInstantiationExpression* node) override;
    void visit(ast::PointerDerefExpression* node) override;
    void visit(ast::AddrOfExpression* node) override;
    void visit(ast::FromIntToLocExpression* node) override;
    void visit(ast::LocationExpression* node) override;
    void visit(ast::ConstructionExpression* node) override;
    void visit(ast::BorrowExpression* node) override;
    void visit(ast::IfExpression* node) override;
    void visit(ast::ArrayInitializationExpression* node) override;
    void visit(ast::TypeofExpression* node) override;
    void visit(ast::TypenameExpression* node) override;

    // Declarations
    void visit(ast::Module* node) override; // Added declaration
    void visit(ast::VariableDeclaration* node) override;
    void visit(ast::FunctionDeclaration* node) override;
    void visit(ast::ClassDeclaration* node) override; // Add this line
    void visit(ast::StructDeclaration* node) override;
    void visit(ast::EnumDeclaration* node) override;
    void visit(ast::TypeAliasDeclaration* node) override;
    void visit(ast::ImportDeclaration* node) override;
    void visit(ast::AspectDeclaration* node) override; // Uncommented
    void visit(ast::BindDeclaration* node) override; // Ensure this is present
    void visit(ast::NamespaceDeclaration* node) override; // Uncommented
    void visit(ast::FieldDeclaration* node) override;
    void visit(ast::EnumVariant* node) override;
    void visit(ast::GenericParameter* node) override;
    void visit(ast::TemplateDeclaration* node) override;


    // Other
    void visit(ast::TypeNode* node) override;

    // Types
    void visit(ast::TypeName* node) override;
    void visit(ast::PointerType* node) override;
    void visit(ast::ArrayType* node) override;
    void visit(ast::VecType* node) override;
    void visit(ast::FutureType* node) override;
    void visit(ast::FunctionType* node) override;
    void visit(ast::OptionalType* node) override;
    void visit(ast::TupleTypeNode* node) override; // Added for tuple type support

    // Template storage system - public for access
    struct TemplateInfo {
        std::unique_ptr<ast::TemplateDeclaration> declaration;
        std::vector<std::string> parameterNames;
        std::vector<std::vector<std::string>> parameterConstraints; // bounds for each parameter
        std::string templateName;

        TemplateInfo(std::unique_ptr<ast::TemplateDeclaration> decl)
            : declaration(std::move(decl)), templateName(declaration->name->name) {
            for (const auto& param : declaration->genericParams) {
                if (param && param->name) {
                    parameterNames.push_back(param->name->name);

                    // Extract trait bounds for this parameter
                    std::vector<std::string> bounds;
                    for (const auto& bound : param->bounds) {
                        if (bound) {
                            bounds.push_back(bound->toString());
                        }
                    }
                    parameterConstraints.push_back(std::move(bounds));
                }
            }
        }
    };

private:
    Driver& driver_;
    SymbolTable* currentScope;
    std::vector<std::string> errors;
    std::unordered_map<ast::Node*, ast::TypeNode*> expressionTypes;
    std::vector<SymbolTable*> scopes;
    std::unordered_set<std::string> reservedWords; // Added for isReservedWord
    std::unordered_map<std::string, std::unique_ptr<TemplateInfo>> templateRegistry;
    std::unordered_map<std::string, std::unique_ptr<ast::Declaration>> instantiatedTemplates;

    // Trait registry for user-defined traits
    std::unordered_map<std::string, std::unique_ptr<TraitInfo>> traitRegistry;

    // Trait implementation registry: type -> trait -> methods
    // Maps "Point" -> "Comparable" -> { method implementations }
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<ast::FunctionDeclaration*>>> traitImpls;

    // Generic trait implementation registry: stores templates like impl<T> Display for Box<T>
    // Maps "Box<T>" -> "Display" -> GenericImplInfo
    std::unordered_map<std::string, std::unordered_map<std::string, std::unique_ptr<GenericImplInfo>>> genericTraitImpls;

    // Associated type assignments for concrete binds: type -> aspect -> associated type -> value type
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<std::string, ast::TypeNode*>>> traitAssociatedTypeImpls;

    // Struct field type storage for member access resolution
    std::unordered_map<std::string, std::map<std::string, ast::TypeNode*>> structFieldTypes;
    std::unordered_map<std::string, std::vector<std::string>> structGenericParamOrder;

    // Enum type names declared in this module (for identifier and member-expression resolution)
    std::unordered_set<std::string> enumTypeNames;

    // Payload types for data-carrying (tagged-union) enum variants:
    // enumName -> variantName -> payload TypeNodes (empty vector = unit variant).
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<ast::TypeNodePtr>>> enumVariantPayloadTypes;

    // Generic data enums (tagged unions with type parameters; e.g. `enum Box<T>`
    // { Value(T), Empty }`): keep the template payload types and the type-parameter
    // order so a concrete instantiation like `Box<Int>` can be materialized with
    // substituted payload types under its concrete type string.
    std::unordered_map<std::string, std::vector<std::string>> enumGenericParamOrder;
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<ast::TypeNodePtr>>> enumTemplatePayloadTypes;

    // Materialize payload types for a concrete generic enum instantiation (e.g.
    // `Box<Int>`) into `enumVariantPayloadTypes` under the concrete type string,
    // substituting the type arguments for the enum's type parameters.
    void registerGenericEnumConcrete(const std::string& enumName, const std::string& concreteTypeStr,
                                     const std::vector<ast::TypeNodePtr>& typeArgs);

    // Context for resolving 'Self' type in aspect implementations
    ast::TypeNode* currentImplType = nullptr;  // Set to Box<T> when processing bind Display -> Box<T>
    std::string currentImplTraitName;          // Set to Display when processing bind Display -> Type
    std::unordered_map<std::string, ast::TypeNode*> currentImplAssociatedTypeBindings;
    bool processingTraitOrBindMethod = false;  // True when visiting methods inside aspect or bind

    // Error handling context
    ast::FunctionDeclaration* currentFunction = nullptr;  // Track current function for error analysis
    int trapDepth = 0;  // Track nesting depth of trap clauses
    std::vector<ast::TypeNode*> activeTrapTypes;  // Stack of error types being trapped

    // Function registry for error propagation analysis
    std::unordered_map<std::string, ast::FunctionDeclaration*> functionRegistry;
    std::unordered_set<std::string> externalFunctionNames;

    struct BorrowState {
        int mutableBorrows = 0;
        int immutableBorrows = 0;
    };
    std::vector<std::unordered_map<std::string, BorrowState>> borrowScopes;
    // Move tracking: per-scope map of variable name -> whether it has been moved from
    struct MoveState {
        bool isMoved = false;
    };
    std::vector<std::unordered_map<std::string, MoveState>> moveScopes;

    // Helper methods for move tracking
    void recordMove(const std::string& varName);
    bool isMoved(const std::string& varName) const;
    bool hasOwnershipKindMY(SymbolInfo* sym) const;
    ast::OwnershipKind getOwnershipKind(SymbolInfo* sym) const;

    // Helper for transitive error propagation
    bool checkCallsFailableFunction(ast::Node* node);

    void enterScope();
    void exitScope();
    void addError(const std::string& message, const ast::Node* node);
    // bool isLValue(ast::Expression* expr); // Duplicate declaration removed
    bool isRawLocationType(ast::Expression* expr);
    BorrowState aggregateBorrowState(const std::string& rootName) const;
    void recordBorrow(const std::string& rootName, ast::BorrowKind kind, const ast::Node* node);
    bool hasActiveBorrow(const std::string& rootName) const;

    // Template management methods
    void registerTemplate(std::unique_ptr<ast::TemplateDeclaration> templateDecl);
    TemplateInfo* findTemplate(const std::string& templateName);
    bool isTemplateInstantiation(const std::string& name);
    std::unique_ptr<ast::Declaration> instantiateTemplate(const std::string& templateName,
                                                         const std::vector<std::string>& typeArgs);

    // Template instantiation helpers
    void handleTemplateInstantiation(ast::Identifier* identifier,
                                   const std::vector<ast::TypeNodePtr>& typeArgs,
                                   ast::GenericInstantiationExpression* node);
    void handleMemberTemplateInstantiation(ast::MemberExpression* memberExpr,
                                         const std::vector<ast::TypeNodePtr>& typeArgs,
                                         ast::GenericInstantiationExpression* node);

    // Builtin Vec constructor (`Vec()`, `Vec(n)`): validate arity, visit the
    // optional size argument, and (if no surrounding annotation propagated a
    // concrete element type) default to `Vec<Int>`.
    void handleVecConstructor(ast::CallExpression* node);
    std::unique_ptr<ast::Declaration> performMonomorphization(TemplateInfo* templateInfo,
                                                             const std::vector<std::string>& concreteTypes);
    std::unique_ptr<ast::Declaration> cloneAndSubstituteAST(ast::Declaration* templateBody,
                                                           const std::vector<std::string>& genericParams,
                                                           const std::vector<std::string>& concreteTypes);

    // Template constraint validation methods
    bool validateTemplateConstraints(TemplateInfo* templateInfo,
                                   const std::vector<std::string>& concreteTypes,
                                   ast::GenericInstantiationExpression* node);
    bool typeImplementsTrait(const std::string& typeName, const std::string& traitName);
    std::vector<std::string> getImplementedTraits(const std::string& typeName);
    bool isBuiltinTypeCompatible(const std::string& typeName, const std::string& traitName);

    // Aspect management methods
    void registerTrait(ast::AspectDeclaration* traitDecl);
    TraitInfo* findTrait(const std::string& traitName);
    void registerTraitImpl(ast::BindDeclaration* implDecl);
    bool validateTraitImpl(const std::string& typeName, const std::string& traitName,
                          const std::vector<std::unique_ptr<ast::FunctionDeclaration>>& methods,
                          const std::vector<ast::BindDeclaration::AssociatedTypeBinding>& associatedTypeBindings,
                          const ast::BindDeclaration* bindDecl);
    bool traitMethodSignatureMatches(const TraitMethod& traitMethod,
                                    ast::FunctionDeclaration* implMethod,
                                    const std::unordered_map<std::string, std::string>& associatedTypeBindings);
    std::string resolveAssociatedTypeReference(const std::string& typeName,
                                              const std::string& traitName,
                                              const std::string& typeReference,
                                              const std::unordered_map<std::string, ast::TypeNode*>* inlineAssociatedTypeBindings = nullptr) const;
    bool setResolvedTraitReturnType(ast::CallExpression* callNode,
                                    const std::string& concreteTypeName,
                                    const std::string& traitName,
                                    ast::TypeNode* traitReturnType);

    // Pattern matching helper for generic type matching
    // Returns true if concrete type (e.g., "Box<Int>") matches pattern (e.g., "Box<T>")
    bool matchesPattern(const std::string& concreteType, const std::string& pattern);

    // True if the concrete type has a bind (concrete or generic) for the given aspect.
    bool hasAspectBinding(const std::string& typeStr, const std::string& aspectName);

    // Validates super-aspects exist and that no aspect inheritance cycles exist.
    void validateAspectInheritance();
};

} // namespace vyb
