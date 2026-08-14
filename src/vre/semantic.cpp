#include "vyb/semantic.hpp"
#include "vyb/parser/token.hpp"
#include "vyb/parser/ast.hpp"
#include "vyb/driver.hpp"
#include <stdexcept>
#include <memory>
#include <unordered_set>
#include <string>
#include <map>
#include <set>
#include <functional>

namespace vyb {
// Forward-declare g_debug_codegen so semantic.cpp can use VYB_CDBG without
// depending on the LLVM codegen headers.
extern bool g_debug_codegen;
} // namespace vyb
#ifndef VYB_CDBG
#define VYB_CDBG if (vyb::g_debug_codegen) std::cerr
#endif

namespace vyb {

static bool isBuiltinVecMethodName(const std::string& methodName) {
    return methodName == "push" || methodName == "pop" || methodName == "len" ||
           methodName == "get" || methodName == "push_array" ||
           methodName == "to_array" || methodName == "get_array" ||
           methodName == "clear" || methodName == "is_empty" ||
           methodName == "capacity" || methodName == "concat" ||
           methodName == "contains" || methodName == "remove_at" ||
           methodName == "get_vec";
}

static bool isBuiltinVecType(ast::TypeNode* typeNode) {
    if (dynamic_cast<ast::VecType*>(typeNode)) {
        return true;
    }
    auto* typeName = dynamic_cast<ast::TypeName*>(typeNode);
    return typeName && typeName->identifier &&
           typeName->identifier->name == "Vec" &&
           !typeName->genericArgs.empty();
}

// Ownership-wrapped types whose runtime representation is a plain scalar value.
// At codegen time, ownership wrappers over primitives keep the underlying type
// (e.g. my<Int> is just an i64), so semantic analysis treats them as transparent.
static const std::set<std::string> ownershipWrapperTypes = {
    "my", "our", "their", "mild", "view", "borrow"
};

static const std::set<std::string> primitiveValueTypes = {
    "Int", "Int8", "Int16", "Int32", "Int64",
    "UInt8", "UInt16", "UInt32", "UInt64",
    "Float", "Float32", "Float64", "Bool", "Char", "Rune",
    "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64",
    "CChar", "CUChar", "CShort", "CUShort", "CInt", "CUInt", "CLong", "CULong",
    "CSize", "CSSize", "CFloat", "CDouble", "CVoid", "CString"
};

// If `type` is an ownership wrapper over a primitive value type, return the
// underlying primitive type; otherwise return `type` unchanged.
static ast::TypeNode* unwrapPrimitiveOwnershipType(ast::TypeNode* type) {
    if (!type) return nullptr;
    auto* typeName = dynamic_cast<ast::TypeName*>(type);
    if (!typeName || !typeName->identifier || typeName->genericArgs.size() != 1 ||
        !ownershipWrapperTypes.count(typeName->identifier->name)) {
        return type;
    }
    ast::TypeNode* inner = typeName->genericArgs[0].get();
    auto* innerName = dynamic_cast<ast::TypeName*>(inner);
    if (innerName && innerName->identifier &&
        primitiveValueTypes.count(innerName->identifier->name)) {
        return inner;
    }
    return type;
}

static std::string reprCUnsupportedReason(ast::TypeNode* typeNode) {
    if (!typeNode) {
        return "has no declared type";
    }

    if (auto* typeName = dynamic_cast<ast::TypeName*>(typeNode)) {
        if (!typeName->identifier) {
            return "has an unnamed type";
        }

        const std::string name = typeName->identifier->name;
        if (name == "String" || name == "string" || name == "Bytes" || name == "bytes") {
            return "cannot use Vyb " + name + "; use CString or an explicit C ABI representation";
        }
        if (name == "my" || name == "our" || name == "their" ||
            name == "mild" || name == "view" || name == "borrow") {
            return "cannot use ownership-qualified type " + typeNode->toString() + " in a C ABI layout";
        }
        if (name == "Vec" || name == "Future" || name == "Tuple") {
            return "cannot use Vyb runtime type " + typeNode->toString() + " in a C ABI layout";
        }

        for (const auto& arg : typeName->genericArgs) {
            std::string reason = reprCUnsupportedReason(arg.get());
            if (!reason.empty()) {
                return reason;
            }
        }
        return "";
    }

    if (auto* pointerType = dynamic_cast<ast::PointerType*>(typeNode)) {
        return reprCUnsupportedReason(pointerType->pointeeType.get());
    }
    if (auto* arrayType = dynamic_cast<ast::ArrayType*>(typeNode)) {
        return reprCUnsupportedReason(arrayType->elementType.get());
    }
    if (auto* vecType = dynamic_cast<ast::VecType*>(typeNode)) {
        return "cannot use Vyb runtime type " + typeNode->toString() + " in a C ABI layout";
    }
    if (auto* futureType = dynamic_cast<ast::FutureType*>(typeNode)) {
        return "cannot use Vyb runtime type " + typeNode->toString() + " in a C ABI layout";
    }
    if (auto* optionalType = dynamic_cast<ast::OptionalType*>(typeNode)) {
        return "cannot use optional type " + typeNode->toString() + " in a C ABI layout";
    }
    if (auto* tupleType = dynamic_cast<ast::TupleTypeNode*>(typeNode)) {
        return "cannot use tuple type " + typeNode->toString() + " in a C ABI layout";
    }

    return "";
}

static ast::TypeNodePtr substituteGenericArgsForValidation(
    ast::TypeNode* typeNode,
    const std::map<std::string, ast::TypeNode*>& substitutions) {
    if (!typeNode) {
        return nullptr;
    }

    if (auto* typeName = dynamic_cast<ast::TypeName*>(typeNode)) {
        if (!typeName->identifier) {
            return typeNode->clone();
        }

        const std::string name = typeName->identifier->name;
        if (typeName->genericArgs.empty()) {
            auto substIt = substitutions.find(name);
            if (substIt != substitutions.end() && substIt->second) {
                return substIt->second->clone();
            }
        }

        std::vector<ast::TypeNodePtr> substitutedArgs;
        substitutedArgs.reserve(typeName->genericArgs.size());
        for (const auto& arg : typeName->genericArgs) {
            if (auto substituted = substituteGenericArgsForValidation(arg.get(), substitutions)) {
                substitutedArgs.push_back(std::move(substituted));
            }
        }

        return std::make_unique<ast::TypeName>(
            typeName->loc,
            std::make_unique<ast::Identifier>(typeName->loc, name),
            std::move(substitutedArgs));
    }

    if (auto* vecType = dynamic_cast<ast::VecType*>(typeNode)) {
        auto substitutedElement = substituteGenericArgsForValidation(vecType->elementType.get(), substitutions);
        return std::make_unique<ast::VecType>(
            vecType->loc,
            substitutedElement ? std::move(substitutedElement) : vecType->elementType->clone());
    }

    if (auto* futureType = dynamic_cast<ast::FutureType*>(typeNode)) {
        auto substitutedResult = substituteGenericArgsForValidation(futureType->resultType.get(), substitutions);
        return std::make_unique<ast::FutureType>(
            futureType->loc,
            substitutedResult ? std::move(substitutedResult) : futureType->resultType->clone());
    }

    if (auto* optionalType = dynamic_cast<ast::OptionalType*>(typeNode)) {
        auto substitutedContained = substituteGenericArgsForValidation(optionalType->containedType.get(), substitutions);
        return std::make_unique<ast::OptionalType>(
            optionalType->loc,
            substitutedContained ? std::move(substitutedContained) : optionalType->containedType->clone());
    }

    if (auto* pointerType = dynamic_cast<ast::PointerType*>(typeNode)) {
        auto substitutedPointee = substituteGenericArgsForValidation(pointerType->pointeeType.get(), substitutions);
        return std::make_unique<ast::PointerType>(
            pointerType->loc,
            substitutedPointee ? std::move(substitutedPointee) : pointerType->pointeeType->clone());
    }

    if (auto* tupleType = dynamic_cast<ast::TupleTypeNode*>(typeNode)) {
        std::vector<ast::TypeNodePtr> substitutedMembers;
        substitutedMembers.reserve(tupleType->memberTypes.size());
        for (const auto& member : tupleType->memberTypes) {
            if (auto substituted = substituteGenericArgsForValidation(member.get(), substitutions)) {
                substitutedMembers.push_back(std::move(substituted));
            }
        }
        return std::make_unique<ast::TupleTypeNode>(tupleType->loc, std::move(substitutedMembers));
    }

    return typeNode->clone();
}

// Scope class implementation
Scope::Scope(Scope* parent_scope) : parent(parent_scope) {}

SymbolInfo* Scope::find(const std::string& name) {
    auto it = symbols.find(name);
    if (it != symbols.end()) {
        return it->second;
    }
    if (parent) {
        return parent->find(name);
    }
    return nullptr;
}

// Add SymbolTable::lookupDirect (assuming Scope is effectively SymbolTable here based on usage)
SymbolInfo* SymbolTable::lookupDirect(const std::string& name) {
    auto it = table.find(name);
    if (it != table.end()) {
        return &it->second;
    }
    return nullptr;
}

void Scope::insert(const std::string& name, SymbolInfo* symbol) {
    symbols[name] = symbol;
}

Scope* Scope::getParent() {
    return parent;
}

// --- SemanticAnalyzer Implementation ---

// SemanticAnalyzer constructor
SemanticAnalyzer::SemanticAnalyzer(Driver& driver) : driver_(driver), currentScope(nullptr) {
    enterScope(); // Create global scope
    // Initialize reservedWords set
    reservedWords = {
        "let", "var", "const", "fn", "struct", "enum", "trait", "impl", "type", "class",
        "if", "else", "while", "for", "return", "break", "continue", "loop", "match",
        "try", "catch", "finally", "defer", "throw", "async", "await", "my", "our", "their",
        "ptr", "borrow", "view", "freedom", "pub", "template", "operator", "as", "in", "ref",
        "extern", "import", "smuggle", "module", "use", "mut", "scoped", "bundle",
        "true", "false", "null", "nil",
        "addr", "at", "loc", "from",
        "lit", "notype", "bare", "deserial"
    };

    // Built-in generic data enums: `enum Option<T> { Some(T), None }` and
    // `enum Result<T, E> { Ok(T), Err(E) }`. Registered as templates so the types
    // are first-class and their variants participate in match/select dispatch and
    // exhaustiveness, just like source-declared generic enums (e.g. `Box<T>`).
    auto typeParam = [](const char* name) {
        return std::make_unique<ast::TypeName>(
            SourceLocation(), std::make_unique<ast::Identifier>(SourceLocation(), name));
    };
    enumGenericParamOrder["Option"] = std::vector<std::string>{"T"};
    enumTemplatePayloadTypes["Option"]["Some"].push_back(typeParam("T")->clone());
    enumTemplatePayloadTypes["Option"]["None"] = std::vector<ast::TypeNodePtr>();
    enumGenericParamOrder["Result"] = std::vector<std::string>{"T", "E"};
    enumTemplatePayloadTypes["Result"]["Ok"].push_back(typeParam("T")->clone());
    enumTemplatePayloadTypes["Result"]["Err"].push_back(typeParam("E")->clone());
}

// --- Canonical block of helpers and basic visit methods ---

// Helper methods (Single definitions)
void SemanticAnalyzer::addError(const std::string& message, const ast::Node* node) {
    errors.push_back(message);
}

// If `expr` is a bare constructor of a built-in generic data enum (`Some(x)` /
// `None` for `Option<T>`, or `Ok(x)` / `Err(x)` for `Result<T, E>`) and
// `targetType` is the matching enum type, inject the concrete enum type into
// `expr` so the constructor infers its payload without explicit type arguments.
// This lets `r<Option<Int>> = None` and `return Ok(v)` infer their payloads.
static bool injectBareEnumConstructorType(ast::Expression* expr, ast::TypeNode* targetType) {
    if (!expr || !targetType) return false;
    auto* tn = dynamic_cast<ast::TypeName*>(targetType);
    if (!tn || !tn->identifier) return false;
    const std::string& enumName = tn->identifier->name;

    bool isSome = false, isNone = false, isOk = false, isErr = false;
    if (auto* call = dynamic_cast<ast::CallExpression*>(expr)) {
        if (auto* id = dynamic_cast<ast::Identifier*>(call->callee.get())) {
            const std::string& cn = id->name;
            isSome = cn == "Some";
            isOk = cn == "Ok";
            isErr = cn == "Err";
        }
    } else if (auto* id = dynamic_cast<ast::Identifier*>(expr)) {
        isNone = id->name == "None";
    }

    bool matches = false;
    if (enumName == "Option") {
        matches = isSome || isNone;
    } else if (enumName == "Result") {
        matches = isOk || isErr;
    }
    if (!matches) return false;

    expr->type = std::shared_ptr<ast::TypeNode>(targetType->clone());
    return true;
}

void SemanticAnalyzer::injectBareEnumCtorArgTypes(ast::CallExpression* node) {
    // Inject a parameter's `Option<T>`/`Result<T,E>` type into a matching bare
    // constructor argument (`Some`/`None`/`Ok`/`Err`) so it infers its payload.
    auto tryInject = [&](ast::Expression* arg, ast::TypeNode* paramType) {
        if (!arg || !paramType) return;
        injectBareEnumConstructorType(arg, paramType);
    };

    // 1. Named function call: callee is a plain identifier.
    if (auto* id = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        auto it = functionRegistry.find(id->name);
        if (it == functionRegistry.end()) return;
        ast::FunctionDeclaration* fd = it->second;
        if (!fd) return;
        std::set<std::string> gparams;
        for (const auto& gp : fd->genericParams) {
            if (gp && gp->name) gparams.insert(gp->name->name);
        }
        size_t nArg = node->arguments.size();
        size_t nPar = fd->params.size();
        for (size_t i = 0; i < nArg && i < nPar; ++i) {
            ast::TypeNode* pt = fd->params[i].typeNode.get();
            if (!pt) continue;
            // Skip a param that references a generic type parameter: without
            // forward inference there is no concrete Option/Result to inject.
            if (!gparams.empty()) {
                std::string pstr = pt->toString();
                bool refsGeneric = false;
                for (const auto& g : gparams) {
                    if (pstr.find(g) != std::string::npos) { refsGeneric = true; break; }
                }
                if (refsGeneric) continue;
            }
            tryInject(node->arguments[i].get(), pt);
        }
        return;
    }

    // 2. Vec method call: `v.push(x)` propagates the vector's element type into
    //    its single argument (e.g. `v<Vec<Option<Int>>>` with `v.push(Some(x))`).
    if (auto* mem = dynamic_cast<ast::MemberExpression*>(node->callee.get())) {
        auto* objId = dynamic_cast<ast::Identifier*>(mem->object.get());
        auto* propId = dynamic_cast<ast::Identifier*>(mem->property.get());
        if (objId && propId && propId->name == "push") {
            SymbolInfo* sym = currentScope->lookup(objId->name);
            if (sym && sym->type) {
                if (auto* vt = dynamic_cast<ast::VecType*>(sym->type)) {
                    if (vt->elementType && node->arguments.size() == 1) {
                        tryInject(node->arguments[0].get(), vt->elementType.get());
                    }
                }
            }
        }
    }
}

bool SemanticAnalyzer::isWildcardErrorExpr(ast::Expression* expr) const {
    auto* id = dynamic_cast<ast::Identifier*>(expr);
    if (!id) return false;
    SymbolInfo* sym = currentScope->lookup(id->name);
    return sym && sym->type == nullptr;
}

void SemanticAnalyzer::enterScope() {
    currentScope = new SymbolTable(currentScope);
    borrowScopes.emplace_back();
    moveScopes.emplace_back();
}

void SemanticAnalyzer::exitScope() {
    if (currentScope) {
        SymbolTable* parent = currentScope->getParent();
        delete currentScope;
        currentScope = parent;
    }
    if (!borrowScopes.empty()) {
        borrowScopes.pop_back();
        moveScopes.pop_back();
    }
}

void SemanticAnalyzer::analyze(ast::Module* root) {
    if (root) {
        root->accept(*this);
    }
}

bool SemanticAnalyzer::checkCallsFailableFunction(ast::Node* node) {
    if (!node) return false;

    // Check if this node is a CallExpression to a failable function
    if (auto callExpr = dynamic_cast<ast::CallExpression*>(node)) {
        if (auto idExpr = dynamic_cast<ast::Identifier*>(callExpr->callee.get())) {
            // Look up the function in the registry
            auto it = functionRegistry.find(idExpr->name);
            if (it != functionRegistry.end()) {
                ast::FunctionDeclaration* funcDecl = it->second;
                if (funcDecl->canFail) {
                    return true;  // This calls a failable function!
                }
            }
        }
    }

    // Recursively check statement types
    if (auto block = dynamic_cast<ast::BlockStatement*>(node)) {
        for (auto& stmt : block->body) {
            if (stmt && checkCallsFailableFunction(stmt.get())) return true;
        }
    }
    else if (auto ifStmt = dynamic_cast<ast::IfStatement*>(node)) {
        if (checkCallsFailableFunction(ifStmt->consequent.get())) return true;
        if (checkCallsFailableFunction(ifStmt->alternate.get())) return true;
    }
    else if (auto whileStmt = dynamic_cast<ast::WhileStatement*>(node)) {
        if (checkCallsFailableFunction(whileStmt->body.get())) return true;
    }
    else if (auto forStmt = dynamic_cast<ast::ForStatement*>(node)) {
        if (checkCallsFailableFunction(forStmt->body.get())) return true;
    }
    else if (auto blockExpr = dynamic_cast<ast::BlockExpression*>(node)) {
        if (checkCallsFailableFunction(blockExpr->block.get())) return true;
    }
    else if (auto exprStmt = dynamic_cast<ast::ExpressionStatement*>(node)) {
        if (checkCallsFailableFunction(exprStmt->expression.get())) return true;
    }
    else if (auto varDecl = dynamic_cast<ast::VariableDeclaration*>(node)) {
        if (checkCallsFailableFunction(varDecl->init.get())) return true;
    }
    else if (auto retStmt = dynamic_cast<ast::ReturnStatement*>(node)) {
        if (checkCallsFailableFunction(retStmt->argument.get())) return true;
    }
    else if (auto binExpr = dynamic_cast<ast::BinaryExpression*>(node)) {
        if (checkCallsFailableFunction(binExpr->left.get())) return true;
        if (checkCallsFailableFunction(binExpr->right.get())) return true;
    }

    return false;
}

bool SemanticAnalyzer::isInLoop() {
    SymbolTable* scope = currentScope;
    while (scope) {
        if (scope->isLoop) {
            return true;
        }
        scope = scope->getParent();
    }
    return false;
}

bool SemanticAnalyzer::isInUnsafeBlock() {
    SymbolTable* scope = currentScope;
    while (scope) {
        if (scope->isUnsafeBlock) {
            return true;
        }
        scope = scope->getParent();
    }
    return false;
}

bool SemanticAnalyzer::isIntegerType(ast::TypeNode* type) {
    if (!type) return false;

    // Handle TypeName nodes (most common case)
    if (auto tn = dynamic_cast<ast::TypeName*>(type)) {
        if (!tn->identifier) return false;
        const std::string& name = tn->identifier->name;
        return name == "Int" || name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
               name == "u8" || name == "u16" || name == "u32" || name == "u64" || name == "size_t" ||
               name == "isize" || name == "usize" ||
               name == "Int8" || name == "Int16" || name == "Int32" || name == "Int64" ||
               name == "UInt8" || name == "UInt16" || name == "UInt32" || name == "UInt64" ||
               name == "Byte" || name == "Char" || name == "Rune" ||
               name == "CChar" || name == "CUChar" || name == "CShort" || name == "CUShort" ||
               name == "CInt" || name == "CUInt" || name == "CLong" || name == "CULong" ||
               name == "CSize" || name == "CSSize";
    }

    // Handle array size expressions which might be integer literals
    if (auto arrayType = dynamic_cast<ast::ArrayType*>(type)) {
        // If it has a size expression that's a literal, it might be integer type
        if (arrayType->sizeExpression) {
            auto it = expressionTypes.find(arrayType->sizeExpression.get());
            if (it != expressionTypes.end() && it->second) {
                return isIntegerType(it->second);
            }
        }
    }

    // Add any other type checks that could represent integer types
    // For example, if there are typedef'ed types or alias types

    return false;
}

bool SemanticAnalyzer::isReservedWord(const std::string& name) {
    return reservedWords.count(name);
}

bool SemanticAnalyzer::isLValue(ast::Expression* expr) {
    return dynamic_cast<ast::Identifier*>(expr) != nullptr ||
           dynamic_cast<ast::MemberExpression*>(expr) != nullptr ||
           dynamic_cast<ast::ArrayElementExpression*>(expr) != nullptr ||
           dynamic_cast<ast::PointerDerefExpression*>(expr) != nullptr;
}

std::string SemanticAnalyzer::borrowedRootName(ast::Expression* expr) {
    if (auto* ident = dynamic_cast<ast::Identifier*>(expr)) {
        return ident->name;
    }
    if (auto* member = dynamic_cast<ast::MemberExpression*>(expr)) {
        return borrowedRootName(member->object.get());
    }
    if (auto* element = dynamic_cast<ast::ArrayElementExpression*>(expr)) {
        return borrowedRootName(element->array.get());
    }
    return "";
}

SemanticAnalyzer::BorrowState SemanticAnalyzer::aggregateBorrowState(const std::string& rootName) const {
    BorrowState total;
    for (const auto& scope : borrowScopes) {
        auto it = scope.find(rootName);
        if (it != scope.end()) {
            total.mutableBorrows += it->second.mutableBorrows;
            total.immutableBorrows += it->second.immutableBorrows;
        }
    }
    return total;
}

bool SemanticAnalyzer::hasActiveBorrow(const std::string& rootName) const {
    BorrowState state = aggregateBorrowState(rootName);
    return state.mutableBorrows > 0 || state.immutableBorrows > 0;
}

void SemanticAnalyzer::recordBorrow(const std::string& rootName, ast::BorrowKind kind, const ast::Node* node) {
    if (rootName.empty()) {
        return;
    }
    if (borrowScopes.empty()) {
        borrowScopes.emplace_back();
    moveScopes.emplace_back();
    }

    BorrowState active = aggregateBorrowState(rootName);
    VYB_CDBG << "DEBUG: recordBorrow " << rootName
             << " kind=" << (kind == ast::BorrowKind::MUTABLE_BORROW ? "borrow" : "view")
             << " mutable=" << active.mutableBorrows
             << " immutable=" << active.immutableBorrows << std::endl;
    if (kind == ast::BorrowKind::MUTABLE_BORROW) {
        if (active.mutableBorrows > 0 || active.immutableBorrows > 0) {
            addError("Cannot take mutable borrow of '" + rootName + "' while it is already borrowed.", node);
            return;
        }
        borrowScopes.back()[rootName].mutableBorrows++;
        return;
    }

    if (active.mutableBorrows > 0) {
        addError("Cannot take view of '" + rootName + "' while it has an active mutable borrow.", node);
        return;
    }
    borrowScopes.back()[rootName].immutableBorrows++;
}

// Move tracking helpers
bool SemanticAnalyzer::hasOwnershipKindMY(SymbolInfo* sym) const {
    if (!sym || !sym->type) return false;
    // Check if the type name starts with "my<" - this indicates MY ownership
    std::string typeStr = sym->type->toString();
    return typeStr.rfind("my<", 0) == 0;
}

ast::OwnershipKind SemanticAnalyzer::getOwnershipKind(SymbolInfo* sym) const {
    if (!sym) return ast::OwnershipKind::MY;
    return sym->ownershipKind;
}

void SemanticAnalyzer::recordMove(const std::string& varName) {
    if (!moveScopes.empty()) {
        moveScopes.back()[varName].isMoved = true;
    }
}

bool SemanticAnalyzer::isMoved(const std::string& varName) const {
    // Walk scopes from innermost to outermost
    for (auto it = moveScopes.rbegin(); it != moveScopes.rend(); ++it) {
        auto vit = it->find(varName);
        if (vit != it->end()) {
            return vit->second.isMoved;
        }
    }
    return false;
}



bool SemanticAnalyzer::isRawLocationType(ast::Expression* expr) {
    auto it = expressionTypes.find(expr);
    if (it == expressionTypes.end() || !it->second) return false;
    return true;
}

std::shared_ptr<ast::TypeNode> SemanticAnalyzer::cloneTypeNode(ast::TypeNode* type) {
    if (!type) return nullptr;

    // Use the existing clone() method on TypeNode
    return std::shared_ptr<ast::TypeNode>(type->clone());
}

// Helper to substitute Self with concrete type in return types
ast::TypeNode* SemanticAnalyzer::substituteSelfType(ast::TypeNode* returnType, const std::string& concreteType) {
    if (!returnType) return nullptr;

    // Check if return type is Self
    if (auto typeName = dynamic_cast<ast::TypeName*>(returnType)) {
        if (typeName->identifier && typeName->identifier->name == "Self") {
            // Replace Self with the concrete type
            // Need to parse concreteType to extract base name and generic arguments
            // E.g., "Box<Point>" -> base="Box", genericArgs=["Point"]

            size_t anglePos = concreteType.find('<');
            if (anglePos != std::string::npos) {
                // Has generic arguments
                std::string baseName = concreteType.substr(0, anglePos);
                std::string argsStr = concreteType.substr(anglePos + 1);
                // Remove trailing '>'
                if (!argsStr.empty() && argsStr.back() == '>') {
                    argsStr.pop_back();
                }

                // Parse generic arguments (simple comma-separated list for now)
                std::vector<ast::TypeNodePtr> genericArgs;
                size_t start = 0;
                while (start < argsStr.length()) {
                    size_t commaPos = argsStr.find(',', start);
                    std::string argName;
                    if (commaPos != std::string::npos) {
                        argName = argsStr.substr(start, commaPos - start);
                        start = commaPos + 1;
                    } else {
                        argName = argsStr.substr(start);
                        start = argsStr.length();
                    }

                    // Trim whitespace
                    argName.erase(0, argName.find_first_not_of(" \t"));
                    argName.erase(argName.find_last_not_of(" \t") + 1);

                    if (!argName.empty()) {
                        // Create TypeName for this argument
                        auto argId = std::make_unique<ast::Identifier>(typeName->loc, argName);
                        genericArgs.push_back(std::make_unique<ast::TypeName>(typeName->loc, std::move(argId)));
                    }
                }

                // Create TypeName with base and generic args
                auto baseId = std::make_unique<ast::Identifier>(typeName->loc, baseName);
                return new ast::TypeName(typeName->loc, std::move(baseId), std::move(genericArgs));
            } else {
                // No generic arguments
                return new ast::TypeName(typeName->loc, std::make_unique<ast::Identifier>(typeName->loc, concreteType));
            }
        }
    }

    // If not Self, return the original type
    return returnType;
}


// Basic visit methods for expressions (Single definitions)
void SemanticAnalyzer::visit(ast::Identifier* node) {
    // Bare Option unit constructor: `None`. A surrounding annotation injects the
    // target `Option<T>` type into this node so `None` is typed as that Option.
    if (node->name == "None" && node->type) {
        auto* optTn = dynamic_cast<ast::TypeName*>(node->type.get());
        if (optTn && optTn->identifier && optTn->identifier->name == "Option") {
            expressionTypes[node] = node->type.get();
            return;
        }
    }
    // Handle built-in type identifiers that don't need to be in symbol table
    // These are used in contexts like Vec::new(), Int::from_string() where the type is a namespace
    if (node->name == "Vec" || node->name == "Int" || node->name == "Float" ||
        node->name == "Bool" || node->name == "String" || node->name == "Type" ||
        enumGenericParamOrder.count(node->name)) {
        // Built-in types - don't error on them
        // They will be properly typed in the context where they're used (e.g., Int::from_string())
        return;
    }

    // Enum type names used as namespaces (e.g., Direction in Direction::North)
    if (enumTypeNames.count(node->name)) {
        return;
    }

    SymbolInfo* symbol = currentScope->lookup(node->name);
    if (!symbol) {
        addError("Undefined identifier: " + node->name, node);
        expressionTypes[node] = nullptr;
        return;
    }
    // Reading an ownership-wrapped primitive (my<Int>, our<Int>, ...) yields the
    // underlying primitive type, matching the inline value representation in codegen.
    ast::TypeNode* readType = symbol->type ? unwrapPrimitiveOwnershipType(symbol->type) : nullptr;
    expressionTypes[node] = readType;

    // Move tracking: reject use of MY-owned variables that have been moved from
    if (hasOwnershipKindMY(symbol) && isMoved(node->name)) {
        addError("Use after move: '" + node->name + "' has been moved and is no longer valid.", node);
        expressionTypes[node] = nullptr;
        return;
    }
    if (readType) {
        node->type = std::shared_ptr<ast::TypeNode>(readType->clone());

    } else {
        VYB_CDBG << "DEBUG: No type found for identifier '" << node->name << "'" << std::endl;
    }
}

void SemanticAnalyzer::visit(ast::IntegerLiteral* node) {
    auto* type = new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, "Int"));
    expressionTypes[node] = type;
    node->type = std::shared_ptr<ast::TypeNode>(type->clone());
}

void SemanticAnalyzer::visit(ast::FloatLiteral* node) {
    auto* type = new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, "Float"));
    expressionTypes[node] = type;
    node->type = std::shared_ptr<ast::TypeNode>(type->clone());
}

void SemanticAnalyzer::visit(ast::StringLiteral* node) {
    auto* type = new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, "String"));
    expressionTypes[node] = type;
    node->type = std::shared_ptr<ast::TypeNode>(type->clone());
}

void SemanticAnalyzer::visit(ast::BooleanLiteral* node) {
    auto* type = new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, "Bool"));
    expressionTypes[node] = type;
    node->type = std::shared_ptr<ast::TypeNode>(type->clone());
}

void SemanticAnalyzer::visit(ast::NilLiteral* node) {
    auto* type = new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, "nil"));
    expressionTypes[node] = type;
    node->type = std::shared_ptr<ast::TypeNode>(type->clone());
}

// Basic visit methods for statements (Single definitions)
void SemanticAnalyzer::visit(ast::BlockStatement* node) {
    enterScope();
    for (auto& stmt : node->body) {
        stmt->accept(*this);
    }
    exitScope();
}

void SemanticAnalyzer::visit(ast::ExpressionStatement* node) {
    if (node->expression) {
        node->expression->accept(*this);
    }
}

void SemanticAnalyzer::visit(ast::EmptyStatement* node) {
    // Empty statements don't need any analysis
}

// END OF THE CANONICAL BLOCK OF HELPERS AND BASIC VISITORS
// ALL SUBSEQUENT DUPLICATE DEFINITIONS OF THE METHODS ABOVE THIS COMMENT WILL BE REMOVED.

// --- More complex visit methods and specific logic follow ---

void SemanticAnalyzer::visit(ast::Module* node) {
    // Two-pass analysis for error propagation:
    // Pass 1: Build function registry and detect explicit fail statements
    // Pass 2: Propagate failability transitively through function calls

    // First pass: Build registry and visit all declarations
    functionRegistry.clear();
    externalFunctionNames.clear();
    for (auto& item : node->body) {
        if (auto funcDecl = dynamic_cast<ast::FunctionDeclaration*>(item.get())) {
            if (funcDecl->id) {
                functionRegistry[funcDecl->id->name] = funcDecl;
            }
        } else if (auto namespaceDecl = dynamic_cast<ast::NamespaceDeclaration*>(item.get())) {
            if (namespaceDecl->name && namespaceDecl->name->name == "__extern_C") {
                for (auto& member : namespaceDecl->members) {
                    if (auto funcDecl = dynamic_cast<ast::FunctionDeclaration*>(member.get())) {
                        if (funcDecl->id) {
                            externalFunctionNames.insert(funcDecl->id->name);
                        }
                    }
                }
            }
        }
    }

    // Visit all declarations (this detects explicit fail statements)
    for (auto& item : node->body) {
        if (item) item->accept(*this);
    }

    // Second pass: Propagate failability transitively
    bool changed = true;
    int iterations = 0;
    const int MAX_ITERATIONS = 100; // Prevent infinite loops

    while (changed && iterations < MAX_ITERATIONS) {
        changed = false;
        iterations++;

        for (auto& item : node->body) {
            if (auto funcDecl = dynamic_cast<ast::FunctionDeclaration*>(item.get())) {
                // Skip main function - it's the entry point and should handle errors explicitly
                if (funcDecl->id && funcDecl->id->name == "main") {
                    continue;
                }

                if (!funcDecl->canFail) {
                    // Check if this function calls any failable functions
                    bool callsFailableFunction = checkCallsFailableFunction(funcDecl->body.get());
                    if (callsFailableFunction) {
                        funcDecl->canFail = true;
                        funcDecl->needsErrorReturn = true;
                        if (funcDecl->errorTypes.empty()) {
                            funcDecl->errorTypes.push_back("Error");
                        }
                        changed = true;
                        VYB_CDBG << "DEBUG: Marked function '" << funcDecl->id->name
                                  << "' as failable (calls failable function)" << std::endl;
                    }
                }
            }
        }
    }

    if (iterations >= MAX_ITERATIONS) {
        std::cerr << "Warning: Error propagation analysis hit maximum iterations" << std::endl;
    }

    // Third pass: reject untrapped calls to failable functions from callers that
    // are still non-failable after propagation analysis.
    std::function<void(ast::Node*, bool, ast::FunctionDeclaration*)> validateCalls;
    validateCalls = [&](ast::Node* n, bool trapProtected, ast::FunctionDeclaration* owner) {
        if (!n) return;

        if (auto* call = dynamic_cast<ast::CallExpression*>(n)) {
            if (!trapProtected && owner && !owner->canFail) {
                if (auto* calleeIdent = dynamic_cast<ast::Identifier*>(call->callee.get())) {
                    auto it = functionRegistry.find(calleeIdent->name);
                    if (it != functionRegistry.end() && it->second && it->second->canFail) {
                        addError(
                            "Call to failable function '" + calleeIdent->name + "' from non-failable function '" +
                            owner->id->name + "' requires a trap block or marking the caller as failable.",
                            call
                        );
                    }
                }
            }
            for (auto& arg : call->arguments) {
                validateCalls(arg.get(), trapProtected, owner);
            }
            validateCalls(call->callee.get(), trapProtected, owner);
            return;
        }

        if (auto* block = dynamic_cast<ast::BlockStatement*>(n)) {
            for (auto& stmt : block->body) validateCalls(stmt.get(), trapProtected, owner);
            return;
        }

        if (auto* exprStmt = dynamic_cast<ast::ExpressionStatement*>(n)) {
            validateCalls(exprStmt->expression.get(), trapProtected, owner);
            return;
        }

        if (auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(n)) {
            validateCalls(varDecl->init.get(), trapProtected, owner);
            return;
        }

        if (auto* retStmt = dynamic_cast<ast::ReturnStatement*>(n)) {
            validateCalls(retStmt->argument.get(), trapProtected, owner);
            return;
        }

        if (auto* ifStmt = dynamic_cast<ast::IfStatement*>(n)) {
            validateCalls(ifStmt->test.get(), trapProtected, owner);
            validateCalls(ifStmt->consequent.get(), trapProtected, owner);
            validateCalls(ifStmt->alternate.get(), trapProtected, owner);
            return;
        }

        if (auto* whileStmt = dynamic_cast<ast::WhileStatement*>(n)) {
            validateCalls(whileStmt->test.get(), trapProtected, owner);
            validateCalls(whileStmt->body.get(), trapProtected, owner);
            return;
        }

        if (auto* forStmt = dynamic_cast<ast::ForStatement*>(n)) {
            validateCalls(forStmt->init.get(), trapProtected, owner);
            validateCalls(forStmt->test.get(), trapProtected, owner);
            validateCalls(forStmt->update.get(), trapProtected, owner);
            validateCalls(forStmt->body.get(), trapProtected, owner);
            return;
        }

        if (auto* blockExpr = dynamic_cast<ast::BlockExpression*>(n)) {
            bool protectedBody = trapProtected || !blockExpr->trapClauses.empty();
            validateCalls(blockExpr->block.get(), protectedBody, owner);
            for (auto& trapClause : blockExpr->trapClauses) {
                if (trapClause && trapClause->handler) {
                    validateCalls(trapClause->handler.get(), trapProtected, owner);
                }
            }
            if (blockExpr->ensureClause && blockExpr->ensureClause->cleanupBlock) {
                validateCalls(blockExpr->ensureClause->cleanupBlock.get(), trapProtected, owner);
            }
            return;
        }
    };

    for (auto& item : node->body) {
        if (auto* funcDecl = dynamic_cast<ast::FunctionDeclaration*>(item.get())) {
            validateCalls(funcDecl->body.get(), false, funcDecl);
        }
    }

    // Validate aspect inheritance (super-aspect existence, no cycles) after all
    // declaratons have been visited and registered.
    validateAspectInheritance();
}

void SemanticAnalyzer::visit(ast::FunctionDeclaration* node) {
    if (isReservedWord(node->id->name)) {
        addError("Identifier \\\"" + node->id->name + "\\\" is a reserved word and cannot be used as a function name.", node->id.get());
    }

    // Variadic functions are only meaningful for C interop: they must be
    // extern/forward declarations (no body), e.g. `printf(fmt: *i8, ...)`.
    if (node->variadic && node->body) {
        addError("Variadic function \\\"" + node->id->name + "\\\" cannot have a body; variadic functions must be declared as extern C without a body.", node);
    }

    // Skip adding to scope if this is a method in an aspect or bind
    // These are stored in the trait registry, not the global symbol table
    if (!processingTraitOrBindMethod) {
        if (currentScope->lookupDirect(node->id->name)) {
            addError("Redefinition of function \\\"" + node->id->name + "\\\" in the same scope.", node->id.get());
        }

        auto funcSymbol = new SymbolInfo{SymbolInfo::Kind::Function, node->id->name, false, ast::OwnershipKind::MY, nullptr};
        currentScope->add(SymbolInfo{funcSymbol->kind, funcSymbol->name, funcSymbol->isConst, funcSymbol->ownershipKind, funcSymbol->type});
        delete funcSymbol;
    }

    // Track current function for error handling validation
    auto previousFunction = currentFunction;
    currentFunction = node;

    enterScope();

    // Handle generic parameters if present (e.g., fn printItem<T<Display>>)
    bool hasGenericParams = !node->genericParams.empty();
    if (hasGenericParams) {
        for (const auto& param : node->genericParams) {
            if (param && param->name) {
                std::string paramName = param->name->name;

                // ============================================================================
                // ASPECT BOUNDS VALIDATION
                // ============================================================================
                // Bounds constrain what types can be used for generic type parameters.
                //
                // CRITICAL CONCEPT:
                // - Bounds affect what you can do INSIDE the generic implementation
                // - Bounded generics let you call aspect methods on type parameters
                // - Unbounded generics treat type parameters as opaque
                //
                // Example:
                //   bind<T> Display -> Box<T>          // Unbounded: T is opaque
                //     - Works for ANY T (Int, String, Point, etc.)
                //     - Inside impl: CANNOT call self.value.show() - T might not have Display
                //     - External: box.show() works for all Box<T>
                //
                //   bind<T<Display>> Display -> Box<T>  // Bounded: T must have Display
                //     - Works ONLY when T has Display (Point if Point has Display)
                //     - Inside impl: CAN call self.value.show() - bound guarantees it exists
                //     - External: box.show() works ONLY for Box<DisplayTypes>
                //
                // Validation checks:
                // 1. Bound names refer to actual aspects (not structs, not undefined types)
                // 2. Bounds are stored in symbol table for later use
                // 3. Method calls on bounded parameters are allowed (checked in MemberExpression)
                // ============================================================================

                // Validate aspect bounds (if any)
                for (const auto& bound : param->bounds) {
                    if (bound) {
                        std::string boundName = bound->toString();
                        // Check that the bound is actually an aspect
                        if (!findTrait(boundName)) {
                            addError("Bound '" + boundName + "' on type parameter '" + paramName + "' is not a defined aspect.", param.get());
                        }
                    }
                }

                // Register the type parameter as a TYPE_PARAMETER symbol
                SymbolInfo typeParamSymbol;
                typeParamSymbol.name = paramName;
                typeParamSymbol.kind = SymbolInfo::Kind::TYPE_PARAMETER;
                typeParamSymbol.type = nullptr;

                // Store bounds for this type parameter
                for (const auto& bound : param->bounds) {
                    if (bound) {
                        typeParamSymbol.bounds.push_back(bound->toString());
                    }
                }

                currentScope->add(typeParamSymbol);
            }
        }
    }

    auto isSelfReceiverType = [](const ast::TypeNode* typeNode) {
        auto typeName = dynamic_cast<const ast::TypeName*>(typeNode);
        return typeName && typeName->identifier &&
               typeName->identifier->name == "Self" &&
               typeName->genericArgs.empty();
    };

    std::vector<std::unique_ptr<ast::TypeNode>> paramTypesVec;
    for (auto& param : node->params) {
        if (param.name) {
            if (!processingTraitOrBindMethod &&
                param.name->name == "self" &&
                isSelfReceiverType(param.typeNode.get())) {
                addError("Receiver parameter \"self\" is only valid as the first parameter of an aspect or bind method.", param.name.get());
                paramTypesVec.push_back(nullptr);
                currentScope->add(SymbolInfo{SymbolInfo::Kind::Variable, param.name->name, false, ast::OwnershipKind::MY, nullptr});
                continue;
            }

             if (isReservedWord(param.name->name)) {
                addError("Identifier \\\"" + param.name->name + "\\\" is a reserved word and cannot be used as a parameter name.", param.name.get());
            }
            if (currentScope->lookupDirect(param.name->name)) {
                 addError("Redefinition of parameter \\\"" + param.name->name + "\\\".", param.name.get());
            }
            if (param.typeNode) {
                // Resolve Self type if we're in a bind/trait impl context
                ast::TypeNode* resolvedType = param.typeNode.get();
                if (currentImplType) {
                    if (auto typeName = dynamic_cast<ast::TypeName*>(param.typeNode.get())) {
                        if (typeName->identifier && typeName->identifier->name == "Self") {
                            // Replace Self with the current impl type
                            resolvedType = currentImplType;
                            VYB_CDBG << "DEBUG: Resolved parameter type Self to " << currentImplType->toString() << std::endl;
                        }
                    }
                }

                resolvedType->accept(*this);
                ast::TypeNode* effectiveType = resolvedType->type ? resolvedType->type.get() : resolvedType;
                paramTypesVec.push_back(effectiveType->clone());
                currentScope->add(SymbolInfo{SymbolInfo::Kind::Variable, param.name->name, false, ast::OwnershipKind::MY, effectiveType->clone().release()});
            } else {
                addError("Parameter \\\"" + param.name->name + "\\\" missing type.", param.name.get());
                paramTypesVec.push_back(nullptr);
                currentScope->add(SymbolInfo{SymbolInfo::Kind::Variable, param.name->name, false, ast::OwnershipKind::MY, nullptr});
            }
        }
    }

    ast::TypeNode* returnTypeAstNode = nullptr;
    if (node->returnTypeNode) {
        node->returnTypeNode->accept(*this);
        ast::TypeNode* effectiveReturnType = node->returnTypeNode->type ? node->returnTypeNode->type.get() : node->returnTypeNode.get();
        returnTypeAstNode = effectiveReturnType->clone().release();
    } else {
        auto void_type_id = std::make_unique<ast::Identifier>(node->loc, "void");
        returnTypeAstNode = new ast::TypeName(node->loc, std::move(void_type_id));
    }

    // Only look up and set function type in symbol table for regular functions
    // For trait/bind methods, they're stored in the trait registry instead
    if (!processingTraitOrBindMethod) {
        SymbolInfo* funcSymFromTable = currentScope->getParent()->lookup(node->id->name);
        if (funcSymFromTable) {
            funcSymFromTable->type = new ast::FunctionType(node->loc, std::move(paramTypesVec), std::unique_ptr<ast::TypeNode>(returnTypeAstNode));
            if (funcSymFromTable->type) {
                 node->type = std::shared_ptr<ast::TypeNode>(funcSymFromTable->type->clone().release());
            }
        }
    } else {
        // For trait/bind methods, still set the node's type directly
        node->type = std::shared_ptr<ast::TypeNode>(new ast::FunctionType(node->loc, std::move(paramTypesVec), std::unique_ptr<ast::TypeNode>(returnTypeAstNode)));
    }

    if (node->body) {
        node->body->accept(*this);

        // Phase 1: Detect if this function contains fail statements
        // This enables automatic error propagation across function boundaries
        // Simple recursive helper to scan AST for FailStatement nodes
        std::function<bool(ast::Node*)> containsFailStatement = [&](ast::Node* n) -> bool {
            if (!n) return false;

            // Check if this node is a FailStatement
            if (dynamic_cast<ast::FailStatement*>(n)) {
                return true;
            }

            // Recursively check statement types
            if (auto block = dynamic_cast<ast::BlockStatement*>(n)) {
                for (auto& stmt : block->body) {
                    if (stmt && containsFailStatement(stmt.get())) return true;
                }
            }
            else if (auto ifStmt = dynamic_cast<ast::IfStatement*>(n)) {
                if (containsFailStatement(ifStmt->consequent.get())) return true;
                if (containsFailStatement(ifStmt->alternate.get())) return true;
            }
            else if (auto whileStmt = dynamic_cast<ast::WhileStatement*>(n)) {
                if (containsFailStatement(whileStmt->body.get())) return true;
            }
            else if (auto forStmt = dynamic_cast<ast::ForStatement*>(n)) {
                if (containsFailStatement(forStmt->body.get())) return true;
            }
            else if (auto tryStmt = dynamic_cast<ast::TryStatement*>(n)) {
                if (containsFailStatement(tryStmt->tryBlock.get())) return true;
                if (containsFailStatement(tryStmt->catchBlock.get())) return true;
                if (containsFailStatement(tryStmt->finallyBlock.get())) return true;
            }
            else if (auto matchStmt = dynamic_cast<ast::MatchStatement*>(n)) {
                for (auto& caseItem : matchStmt->cases) {
                    if (containsFailStatement(caseItem.second.get())) return true;
                }
            }
            else if (auto blockExpr = dynamic_cast<ast::BlockExpression*>(n)) {
                if (containsFailStatement(blockExpr->block.get())) return true;
                for (auto& clause : blockExpr->trapClauses) {
                    if (clause && containsFailStatement(clause->handler.get())) return true;
                }
            }
            else if (auto exprStmt = dynamic_cast<ast::ExpressionStatement*>(n)) {
                if (containsFailStatement(exprStmt->expression.get())) return true;
            }
            else if (auto retStmt = dynamic_cast<ast::ReturnStatement*>(n)) {
                if (containsFailStatement(retStmt->argument.get())) return true;
            }

            return false;
        };

        // Scan the function body for fail statements
        bool hasFailStatement = containsFailStatement(node->body.get());

        // Update function metadata for error propagation
        node->canFail = hasFailStatement;
        node->needsErrorReturn = hasFailStatement;
        if (hasFailStatement) {
            // For now, use generic "Error" type - later we'll extract actual error types
            node->errorTypes.push_back("Error");
        }
    }

    exitScope();

    // Restore previous function context
    currentFunction = previousFunction;
}

void SemanticAnalyzer::visit(ast::VariableDeclaration* node) {
    if (isReservedWord(node->id->name)) {
        addError("Identifier \\\"" + node->id->name + "\\\" is a reserved word and cannot be used as a variable name.", node->id.get());
    }

    if (currentScope->lookupDirect(node->id->name)) {
        addError("Redefinition of variable \"" + node->id->name + "\" in the same scope.", node->id.get());
    }

    // Enforce mandatory type annotation for Vyb variables (name<Type> = value syntax)
    // Allow compiler-generated internal variables (starting with __) to skip this check
    // Allow variables with initializers to use type inference (error if type can't be inferred)
    const std::string& varName = node->id ? node->id->name : "";
    bool isInternalVar = varName.size() >= 2 && varName[0] == '_' && varName[1] == '_';
    bool needsTypeCheck = !node->typeNode && node->id && !isInternalVar;
    // We defer the type-missing error until after visiting the initializer (to allow type inference)

    if (node->typeNode) {
        node->typeNode->accept(*this);
    }

    if (node->init) {
        // Special case: if the initializer is a Vec constructor (`Vec()`, `Vec(n)`,
        // or the legacy `Vec::new()`), propagate the variable's concrete Vec<T>
        // element type into the call BEFORE visiting it.
        if (auto callExpr = dynamic_cast<ast::CallExpression*>(node->init.get())) {
            bool isVecCtor = false;
            if (auto memberExpr = dynamic_cast<ast::MemberExpression*>(callExpr->callee.get())) {
                if (auto vecIdent = dynamic_cast<ast::Identifier*>(memberExpr->object.get())) {
                    if (auto newIdent = dynamic_cast<ast::Identifier*>(memberExpr->property.get())) {
                        isVecCtor = vecIdent->name == "Vec" && newIdent->name == "new";
                    }
                }
            } else if (auto idCallee = dynamic_cast<ast::Identifier*>(callExpr->callee.get())) {
                isVecCtor = idCallee->name == "Vec";
            }

            if (isVecCtor && node->typeNode) {
                ast::TypeNode* varType = node->typeNode->type ? node->typeNode->type.get() : node->typeNode.get();
                if (auto vecVarType = dynamic_cast<ast::VecType*>(varType)) {
                    if (vecVarType->elementType) {
                        auto vecType = std::make_unique<ast::VecType>(callExpr->loc, vecVarType->elementType->clone());
                        expressionTypes[callExpr] = vecType.get();
                        callExpr->type = std::shared_ptr<ast::TypeNode>(std::move(vecType));
                    }
                }
            }
        }

        // Bare Option constructor injection: `result<Option<Int>> = None` or
        // `result<Option<Int>> = Some(value)` infers the payload type from the
        // variable's declared Option<T> annotation without explicit type args.
        if (node->typeNode && node->init) {
            ast::TypeNode* varType = node->typeNode->type ? node->typeNode->type.get() : node->typeNode.get();
            if (varType) injectBareEnumConstructorType(node->init.get(), varType);
        }

        node->init->accept(*this);

        // Type inference: if no annotation given, infer from initializer
        if (needsTypeCheck && expressionTypes.count(node->init.get())) {
            ast::TypeNode* initType = expressionTypes[node->init.get()];
            if (initType) {
                // Successfully inferred - no error needed
                node->typeNode = std::unique_ptr<ast::TypeNode>(initType->clone());
                needsTypeCheck = false; // resolved via inference
            }
        }
        // If still no type, report error with correct Vyb syntax
        if (needsTypeCheck) {
            if (node->isConst) {
                addError("Missing type annotation on constant '" + node->id->name + "'; use const<Type> name = value syntax.", node);
            } else {
                addError("Missing type annotation on '" + node->id->name + "'; use name<Type> = value syntax.", node);
            }
        }

        // Now check types match (for both explicit types and inferred types)
        if (node->typeNode && expressionTypes.count(node->init.get())) {
            // Use resolved type if available (e.g., TypeName with ->type set to VecType or TupleTypeNode)
            ast::TypeNode* varType = node->typeNode->type ? node->typeNode->type.get() : node->typeNode.get();
            ast::TypeNode* initType = expressionTypes[node->init.get()];
            if (initType) {
                if (!areTypesCompatible(varType, initType)) {
                    addError("Initializer type does not match variable type for '" + node->id->name + "'. Expected " + varType->toString() + " but got " + initType->toString(), node);
                }
            }
        }

        // Move tracking: a variable declaration initialized from a MY-owned
        // value transfers ownership out of the source (matching assignment).
        if (auto* initIdent = dynamic_cast<ast::Identifier*>(node->init.get())) {
            SymbolInfo* initSymbol = currentScope->lookup(initIdent->name);
            if (initSymbol && hasOwnershipKindMY(initSymbol)) {
                recordMove(initIdent->name);
            }
        }

        if (auto* borrowExpr = dynamic_cast<ast::BorrowExpression*>(node->init.get())) {
            recordBorrow(borrowedRootName(borrowExpr->expression.get()), borrowExpr->kind, borrowExpr);
        } else if (auto* callExpr = dynamic_cast<ast::CallExpression*>(node->init.get())) {
            if (auto* callee = dynamic_cast<ast::Identifier*>(callExpr->callee.get())) {
                if ((callee->name == "borrow" || callee->name == "view") && callExpr->arguments.size() == 1) {
                    auto kind = callee->name == "borrow"
                        ? ast::BorrowKind::MUTABLE_BORROW
                        : ast::BorrowKind::IMMUTABLE_VIEW;
                    recordBorrow(borrowedRootName(callExpr->arguments[0].get()), kind, callExpr);
                }
            }
        }
    } else if (needsTypeCheck) {
        // No initializer and no type annotation
        if (node->isConst) {
            addError("Missing type annotation on constant '" + node->id->name + "'; use const<Type> name = value syntax.", node);
        } else {
            addError("Missing type annotation on '" + node->id->name + "'; use name<Type> = value syntax.", node);
        }
    }

    // Get the resolved type - prefer the resolved type from typeNode->type if available
    ast::TypeNode* symbolType = nullptr;
    if (node->typeNode) {
        // If the typeNode has been resolved (e.g., TypeName->type contains the resolved VecType),
        // use that instead of the TypeName itself
        if (node->typeNode->type) {
            symbolType = node->typeNode->type.get();
        } else {
            symbolType = node->typeNode.get();
        }
    } else if (node->init && expressionTypes.count(node->init.get())) {
        // For type inference without explicit type annotation
        symbolType = expressionTypes[node->init.get()];
    }

    SymbolInfo::Kind kind = SymbolInfo::Kind::Variable;
    currentScope->add(SymbolInfo{kind, node->id->name, node->isConst, ast::OwnershipKind::MY, symbolType ? symbolType->clone().release() : nullptr}); // Explicit SymbolInfo
}

void SemanticAnalyzer::visit(ast::ClassDeclaration* node) {
    if (isReservedWord(node->name->name)) {
        addError("Identifier \\\"" + node->name->name + "\\\" is a reserved word and cannot be used as a class name.", node->name.get());
    }
    if (currentScope->lookupDirect(node->name->name)) {
        addError("Redefinition of class \\\"" + node->name->name + "\\\" in the same scope.", node->name.get());
    }
    currentScope->add(SymbolInfo{SymbolInfo::Kind::Type, node->name->name, false, ast::OwnershipKind::MY, new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->name->loc, node->name->name))});


    enterScope();

    for (auto& member : node->members) {
        if (member) {
            // Assuming members are Declarations, e.g. FunctionDeclaration (for methods) or VariableDeclaration (for fields)
            // The accept method will call the appropriate visit method.
            member->accept(*this);
        }
    }
    exitScope();
}

// Removed visit(ast::MethodDeclaration* node)
// Removed visit(ast::ConstructorDeclaration* node)
// Removed visit(ast::DestructorDeclaration* node)

// Trait and Impl visitor implementations moved below after TemplateDeclaration


void SemanticAnalyzer::visit(ast::NamespaceDeclaration* node) {
    if (!node) {
        return;
    }
    if (node->name && isReservedWord(node->name->name)) {
        addError("Identifier \\\"" + node->name->name + "\\\" is a reserved word and cannot be used as a namespace name.", node->name.get());
    }

    bool isExternCBlock = node->name && node->name->name == "__extern_C";
    for (auto& member : node->members) {
        if (member) {
            if (isExternCBlock) {
                if (auto* functionDecl = dynamic_cast<ast::FunctionDeclaration*>(member.get())) {
                    if (functionDecl->id) {
                        externalFunctionNames.insert(functionDecl->id->name);
                    }
                }
            }
            member->accept(*this);
        }
    }
}

void SemanticAnalyzer::visit(ast::TypeAliasDeclaration* node) {
    if (node->name && isReservedWord(node->name->name)) {
        addError("Identifier \\\"" + node->name->name + "\\\" is a reserved word and cannot be used as a type alias name.", node->name.get());
    }
    if (node->name && currentScope->lookupDirect(node->name->name)) {
        addError("Redefinition of type alias \\\"" + node->name->name + "\\\" in the same scope.", node->name.get());
    }

    if (node->typeNode) {
        node->typeNode->accept(*this);
    }

    if (node->name && node->typeNode && node->typeNode->type) {
        currentScope->add(SymbolInfo{SymbolInfo::Kind::Type, node->name->name, false, ast::OwnershipKind::MY, node->typeNode->type->clone().release()}); // Explicit SymbolInfo
    } else if (node->name) {
        addError("Type alias \\\"" + node->name->name + "\\\" has an unresolved target type.", node);
        currentScope->add(SymbolInfo{SymbolInfo::Kind::Type, node->name->name, false, ast::OwnershipKind::MY, nullptr}); // Explicit SymbolInfo
    }
}

// --- Type and expression analysis ---

void SemanticAnalyzer::visit(ast::UnaryExpression* node) {
    if (!node || !node->operand) {
        return;
    }
    // Visit the operand to get its type
    node->operand->accept(*this);
    
    // The result type of a unary expression is the same as the operand type
    if (expressionTypes.count(node->operand.get())) {
        expressionTypes[node] = expressionTypes[node->operand.get()];
    } else if (node->operand->type) {
        expressionTypes[node] = node->operand->type.get();
        node->type = std::shared_ptr<ast::TypeNode>(node->operand->type->clone());
    }
}
void SemanticAnalyzer::visit(ast::BinaryExpression* node) {
    if (!node || !node->left || !node->right) {
        addError("Malformed binary expression.", node);
        return;
    }

    // Visit left and right operands
    node->left->accept(*this);
    node->right->accept(*this);

    // Get the types of the operands
    auto leftTypeIt = expressionTypes.find(node->left.get());
    auto rightTypeIt = expressionTypes.find(node->right.get());

    ast::TypeNode* leftType = (leftTypeIt != expressionTypes.end()) ? leftTypeIt->second : nullptr;
    ast::TypeNode* rightType = (rightTypeIt != expressionTypes.end()) ? rightTypeIt->second : nullptr;

    if (!leftType || !rightType) {
        // Cannot determine type without both operands
        VYB_CDBG << "DEBUG: Binary expression - could not determine operand types" << std::endl;
        return;
    }

    // `Type` is an opaque type identity: only equality / inequality is meaningful.
    bool leftIsType = leftType->toString() == "Type";
    bool rightIsType = rightType->toString() == "Type";
    if ((leftIsType || rightIsType) &&
        node->op.type != TokenType::EQEQ && node->op.type != TokenType::NOTEQ) {
        addError("Operator '" + node->op.lexeme + "' is not allowed on Type values; only '==' and '!=' are supported.", node);
        return;
    }

    // For arithmetic operators (+, -, *, /, %), the result type is typically the same as the operands
    // For comparison operators (<, >, <=, >=, ==, !=), the result is Bool
    // For logical operators (&&, ||), the result is Bool

    ast::TypeNode* resultType = nullptr;

    switch (node->op.type) {
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::MULTIPLY:
        case TokenType::DIVIDE:
        case TokenType::MODULO:
            // Arithmetic operations: result type is the same as operands (assuming compatible types)
            resultType = leftType;
            break;

        case TokenType::LT:
        case TokenType::LTEQ:
        case TokenType::GT:
        case TokenType::GTEQ:
        case TokenType::EQEQ:
        case TokenType::NOTEQ:
            // Comparison operations: result is Bool
            resultType = new ast::TypeName(node->loc,
                std::make_unique<ast::Identifier>(node->loc, "Bool"));
            break;

        case TokenType::AND:
        case TokenType::OR:
            // Logical operations: result is Bool
            resultType = new ast::TypeName(node->loc,
                std::make_unique<ast::Identifier>(node->loc, "Bool"));
            break;

        default:
            // Unknown operator
            addError("Unknown binary operator in expression.", node);
            return;
    }

    if (resultType) {
        expressionTypes[node] = resultType;
        node->type = std::shared_ptr<ast::TypeNode>(resultType->clone());

        VYB_CDBG << "DEBUG: Binary expression result type: " << resultType->toString() << std::endl;
    }
}

void SemanticAnalyzer::handleVecConstructor(ast::CallExpression* node) {
    if (node->arguments.size() > 1) {
        addError("Vec() accepts at most 1 argument (optional size)", node);
        return;
    }
    if (node->arguments.size() == 1 && node->arguments[0]) {
        node->arguments[0]->accept(*this);
    }
    // If a surrounding annotation (e.g. `v<Vec<String>> = Vec()`) already
    // propagated the concrete Vec<T> element type, use it.
    if (expressionTypes.count(node) && expressionTypes[node]) {
        return;
    }
    // Fall back to Vec<Int> when no element type is inferable.
    auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
    auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
    auto vecType = std::make_unique<ast::VecType>(node->loc, std::move(intType));
    expressionTypes[node] = vecType.get();
    node->type = std::shared_ptr<ast::TypeNode>(std::move(vecType));
}

void SemanticAnalyzer::visit(ast::CallExpression* node) {
    // Bare builtin enum constructor: `Some(x)` (Option) and `Ok(x)` / `Err(e)`
    // (Result). A surrounding annotation (variable declaration or return
    // statement) injects the target enum type into this node, so the constructor
    // infers its payload without explicit type arguments.
    if (auto calleeId = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        const std::string& cn = calleeId->name;
        if (node->type && (cn == "Some" || cn == "Ok" || cn == "Err")) {
            auto* etn = dynamic_cast<ast::TypeName*>(node->type.get());
            if (etn && etn->identifier) {
                const std::string& en = etn->identifier->name;
                bool isOptionSome = (cn == "Some" && en == "Option");
                bool isResultCtor = (en == "Result" && (cn == "Ok" || cn == "Err"));
                if (isOptionSome || isResultCtor) {
                    if (node->arguments.size() != 1) {
                        addError(cn + " expects exactly one payload argument, got " +
                                 std::to_string(node->arguments.size()), node);
                        return;
                    }
                    node->arguments[0]->accept(*this);
                    expressionTypes[node] = node->type.get();
                    return;
                }
            }
        }
    }
    // Bare builtin Vec constructor: `Vec()`, `Vec(n)`. The callee is the bare
    // identifier `Vec` rather than the legacy `Vec::new(...)` static form.
    if (auto idCallee = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        if (idCallee->name == "Vec") {
            handleVecConstructor(node);
            return;
        }
    }
    // Check if this is an intrinsic function call before visiting the callee
    bool isIntrinsic = false;
    if (auto ident = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        const std::string& name = ident->name;
        if (name == "lit" || name == "notype" || name == "bare" || name == "deserial" ||
            name == "borrow" || name == "view" || name == "my" || name == "their" || name == "our" ||
            name == "soft" || name == "println" || name == "print" || name == "println_int" ||
            name == "print_int" || name == "println_bool" || name == "print_bool" ||
            name == "abs" || name == "sqrt" || name == "pow" || name == "sin" || name == "cos" ||
            name == "tan" || name == "exp" || name == "log" || name == "log2" || name == "log10" ||
            name == "floor" || name == "ceil" || name == "round" || name == "min" || name == "max") {
            isIntrinsic = true;
        }
    }

    // Qualified aspect method call: Aspect::method(receiver, ...)
    // Detected before the callee is visited so the aspect identifier is not
    // treated as the type of a plain member-expression object.
    if (auto memberExpr = dynamic_cast<ast::MemberExpression*>(node->callee.get())) {
        if (auto objIdent = dynamic_cast<ast::Identifier*>(memberExpr->object.get())) {
            if (auto methodIdent = dynamic_cast<ast::Identifier*>(memberExpr->property.get())) {
                if (findTrait(objIdent->name)) {
                    handleQualifiedAspectCall(node, objIdent->name, methodIdent->name);
                    return;
                }
            }
        }
    }

    // Tagged-union enum variant constructor: Shape::Circle(x), Shape::Rect(a, b).
    // Validates arity and types the call as the enum (so `s = Shape::Circle(1.5)`
    // infers the enum type) before the callee is consumed as a generic expression.
    if (auto memCtor = dynamic_cast<ast::MemberExpression*>(node->callee.get())) {
        // Generic data enum: `Box<Int>::Value(x)`.
        if (auto gi = dynamic_cast<ast::GenericInstantiationExpression*>(memCtor->object.get())) {
            if (auto baseId = dynamic_cast<ast::Identifier*>(gi->baseExpression.get())) {
                if (auto varId = dynamic_cast<ast::Identifier*>(memCtor->property.get())) {
                    if (enumGenericParamOrder.count(baseId->name)) {
                        const std::string concreteStr = gi->toString();
                        registerGenericEnumConcrete(baseId->name, concreteStr, gi->genericArguments);
                        auto enumIt = enumVariantPayloadTypes.find(concreteStr);
                        if (enumIt != enumVariantPayloadTypes.end()) {
                            auto varIt = enumIt->second.find(varId->name);
                            if (varIt == enumIt->second.end()) {
                                addError("Unknown variant '" + varId->name + "' of enum " + concreteStr, node);
                                return;
                            }
                            const auto& payload = varIt->second;
                            if (payload.size() != node->arguments.size()) {
                                addError("Enum variant '" + varId->name + "' of " + concreteStr +
                                         " expects " + std::to_string(payload.size()) + " argument(s), got " +
                                         std::to_string(node->arguments.size()), node);
                                return;
                            }
                            auto enumType = std::make_unique<ast::TypeName>(node->loc,
                                std::make_unique<ast::Identifier>(node->loc, baseId->name));
                            for (auto& a : gi->genericArguments) enumType->genericArgs.push_back(a->clone());
                            node->type = std::shared_ptr<ast::TypeNode>(enumType.release());
                            expressionTypes[node] = node->type.get();
                            for (auto& arg : node->arguments) {
                                if (arg) arg->accept(*this);
                            }
                            return;
                        }
                    }
                }
            }
        }
        if (auto objId = dynamic_cast<ast::Identifier*>(memCtor->object.get())) {
            if (auto varId = dynamic_cast<ast::Identifier*>(memCtor->property.get())) {
                auto enumIt = enumVariantPayloadTypes.find(objId->name);
                if (enumIt != enumVariantPayloadTypes.end()) {
                    auto varIt = enumIt->second.find(varId->name);
                    if (varIt == enumIt->second.end()) {
                        addError("Unknown variant '" + varId->name + "' of enum " + objId->name, node);
                        return;
                    }
                    const auto& payload = varIt->second;
                    if (payload.size() != node->arguments.size()) {
                        addError("Enum variant '" + varId->name + "' of " + objId->name +
                                 " expects " + std::to_string(payload.size()) + " argument(s), got " +
                                 std::to_string(node->arguments.size()), node);
                        return;
                    }
                    auto enumType = std::make_unique<ast::TypeName>(
                        node->loc, std::make_unique<ast::Identifier>(node->loc, objId->name));
                    node->type = std::shared_ptr<ast::TypeNode>(enumType.release());
                    expressionTypes[node] = node->type.get();
                    // Still visit args so side expressions and nested types are checked.
                    for (auto& arg : node->arguments) {
                        if (arg) arg->accept(*this);
                    }
                    return;
                }
            }
        }
    }

    // Only visit callee if it's not an intrinsic (intrinsics don't need symbol resolution)
    if (!isIntrinsic && node->callee) {
        node->callee->accept(*this);
    }

    // Expected-type propagation: inject param/element types into bare enum
    // constructor arguments (`Some`/`None`/`Ok`/`Err`) before they are visited,
    // so they can appear as subexpressions (e.g. `unwrap(Some(7))`).
    injectBareEnumCtorArgTypes(node);

    // Visit arguments
    for (auto& arg : node->arguments) {
        if (arg) arg->accept(*this);
    }



    // Handle intrinsics
    if (auto ident = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        const std::string& name = ident->name;

        if (externalFunctionNames.count(name) && !isInUnsafeBlock()) {
            addError("External function '" + name + "' can only be called inside a freedom block.", node);
        }

        // Handle serialization intrinsics (lit, notype, bare, deserial)
        if (name == "lit" || name == "notype" || name == "bare" || name == "deserial") {
            if (node->arguments.empty()) {
                addError(name + "() requires at least one argument", node);
                return;
            }

            // For notype(): validate that argument is a struct type, not a primitive
            if (name == "notype") {
                ast::Expression* argExpr = node->arguments[0].get();
                ast::TypeNode* argType = nullptr;
                if (argExpr) {
                    argType = argExpr->type.get();
                    if (!argType) {
                        auto it = expressionTypes.find(argExpr);
                        if (it != expressionTypes.end()) argType = it->second;
                    }
                }
                if (argType) {
                    std::string argTypeName = argType->toString();
                    // Check if it's a primitive type
                    static const std::set<std::string> primitiveTypes = {
                        "Int", "Int8", "Int16", "Int32", "Int64",
                        "UInt8", "UInt16", "UInt32", "UInt64",
                        "Float", "Float32", "Float64",
                        "Bool", "Char", "Rune",
                        "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64"
                    };
                    if (primitiveTypes.count(argTypeName)) {
                        addError("notype() requires struct input, but got primitive " + argTypeName, node);
                        return;
                    }
                }
            }

            // Get the argument type
            ast::Expression* argExpr = node->arguments[0].get();
            if (argExpr) {
                // For lit(), the result type is the same as the input type
                // This ensures the serialization system knows how to handle it
                if (argExpr->type) {
                    node->type = std::shared_ptr<ast::TypeNode>(argExpr->type->clone().release());
                    expressionTypes[node] = argExpr->type.get();
                } else if (auto argType = expressionTypes[argExpr]) {
                    node->type = std::shared_ptr<ast::TypeNode>(argType->clone().release());
                    expressionTypes[node] = argType;
                } else {
                    // Fallback to String type if we can't determine the argument type
                    auto stringId = std::make_unique<ast::Identifier>(node->loc, "String");
                    ast::TypeNode* stringType = new ast::TypeName(node->loc, std::move(stringId), {});
                    node->type = std::shared_ptr<ast::TypeNode>(stringType);
                    expressionTypes[node] = stringType;
                }
            }
            return;
        }

        // Handle borrow()/view() intrinsics
        if (name == "borrow" || name == "view") {
            if (node->arguments.size() != 1) {
                addError(name + "() expects exactly one argument", node);
                return;
            }
            ast::Expression* argExpr = node->arguments[0].get();
            if (!isLValue(argExpr)) {
                addError(name + "() requires an lvalue argument.", node);
                return;
            }
            auto argTypeIt = expressionTypes.find(argExpr);
            ast::TypeNode* argType = (argTypeIt != expressionTypes.end()) ? argTypeIt->second : nullptr;
            if (!argType) {
                addError("Cannot determine type of argument to " + name + "()", node);
                return;
            }

            ast::TypeNodePtr innerType;
            auto baseTypeName = dynamic_cast<ast::TypeName*>(argType);
            if (baseTypeName && baseTypeName->identifier &&
                (baseTypeName->identifier->name == "my" || baseTypeName->identifier->name == "our") &&
                baseTypeName->genericArgs.size() == 1) {
                innerType = baseTypeName->genericArgs[0]->clone();
            } else {
                innerType = argType->clone();
            }

            using ast::BorrowKind;
            BorrowKind kind = (name == "borrow" ? BorrowKind::MUTABLE_BORROW : BorrowKind::IMMUTABLE_VIEW);
            // Create their<T> TypeName
            auto theirId = std::make_unique<ast::Identifier>(node->loc, "their");
            std::vector<ast::TypeNodePtr> theirArgs;
            theirArgs.push_back(std::move(innerType));
            ast::TypeNode* resultType = new ast::TypeName(node->loc, std::move(theirId), std::move(theirArgs));
            expressionTypes[node] = resultType;
            node->type = std::shared_ptr<ast::TypeNode>(resultType->clone().release());
            return;
        }

        // Handle ownership constructors: my(), their(), our()
        if (name == "my" || name == "their" || name == "our") {
            if (node->arguments.size() != 1) {
                addError(name + "() expects exactly one argument", node);
                return;
            }

            ast::Expression* argExpr = node->arguments[0].get();
            if (!argExpr) {
                addError(name + "() argument cannot be null", node);
                return;
            }

            // Get the argument type
            ast::TypeNode* argType = nullptr;
            if (argExpr->type) {
                argType = argExpr->type.get();
            } else {
                auto typeIt = expressionTypes.find(argExpr);
                if (typeIt != expressionTypes.end()) {
                    argType = typeIt->second;
                }
            }

            if (!argType) {
                addError("Cannot determine type of argument to " + name + "()", node);
                return;
            }

            // Create ownership type: my<T>, their<T>, or our<T>
            auto ownershipId = std::make_unique<ast::Identifier>(node->loc, name);
            std::vector<ast::TypeNodePtr> ownershipArgs;
            ownershipArgs.push_back(argType->clone());

            ast::TypeNode* resultType = new ast::TypeName(node->loc, std::move(ownershipId), std::move(ownershipArgs));
            expressionTypes[node] = resultType;
            node->type = std::shared_ptr<ast::TypeNode>(resultType->clone().release());
            return;
        }

        // Handle soft() constructor: creates mild<T> from our<T>
        if (name == "soft") {
            if (node->arguments.size() != 1) {
                addError("soft() expects exactly one argument", node);
                return;
            }

            ast::Expression* argExpr = node->arguments[0].get();
            if (!argExpr) {
                addError("soft() argument cannot be null", node);
                return;
            }

            // Get the argument type
            ast::TypeNode* argType = nullptr;
            if (argExpr->type) {
                argType = argExpr->type.get();
            } else {
                auto typeIt = expressionTypes.find(argExpr);
                if (typeIt != expressionTypes.end()) {
                    argType = typeIt->second;
                }
            }

            if (!argType) {
                addError("Cannot determine type of argument to soft()", node);
                return;
            }

            // Validate that argument is our<T>
            ast::TypeName* argTypeName = dynamic_cast<ast::TypeName*>(argType);
            if (!argTypeName || !argTypeName->identifier || argTypeName->identifier->name != "our") {
                addError("soft() can only be applied to our<T> (shared ownership) types", node);
                return;
            }

            // Extract inner type T from our<T>
            if (argTypeName->genericArgs.empty()) {
                addError("soft() argument our<T> is missing type parameter", node);
                return;
            }

            ast::TypeNode* innerType = argTypeName->genericArgs[0].get();

            // Create mild<T> type
            auto mildId = std::make_unique<ast::Identifier>(node->loc, "mild");
            std::vector<ast::TypeNodePtr> mildArgs;
            mildArgs.push_back(innerType->clone());

            ast::TypeNode* resultType = new ast::TypeName(node->loc, std::move(mildId), std::move(mildArgs));
            expressionTypes[node] = resultType;
            node->type = std::shared_ptr<ast::TypeNode>(resultType->clone().release());
            return;
        }

        // Handle println intrinsic
        if (name == "println") {
            // println accepts one or more arguments of any type and returns Void
            if (node->arguments.empty()) {
                addError("println() requires at least one argument", node);
                return;
            }

            // Create Void return type
            auto voidId = std::make_unique<ast::Identifier>(node->loc, "Void");
            ast::TypeNode* voidType = new ast::TypeName(node->loc, std::move(voidId), std::vector<ast::TypeNodePtr>{});
            expressionTypes[node] = voidType;
            node->type = std::shared_ptr<ast::TypeNode>(voidType->clone().release());
            return;
        }

        // Handle print intrinsic (no newline)
        if (name == "print") {
            if (node->arguments.empty()) {
                addError("print() requires at least one argument", node);
                return;
            }
            auto voidId = std::make_unique<ast::Identifier>(node->loc, "Void");
            ast::TypeNode* voidType = new ast::TypeName(node->loc, std::move(voidId), std::vector<ast::TypeNodePtr>{});
            expressionTypes[node] = voidType;
            node->type = std::shared_ptr<ast::TypeNode>(voidType->clone().release());
            return;
        }

        // Handle println_int / print_int intrinsics
        if (name == "println_int" || name == "print_int" || name == "println_bool" || name == "print_bool") {
            if (node->arguments.size() != 1) {
                addError(name + "() expects exactly one argument", node);
                return;
            }
            auto voidId = std::make_unique<ast::Identifier>(node->loc, "Void");
            ast::TypeNode* voidType = new ast::TypeName(node->loc, std::move(voidId), std::vector<ast::TypeNodePtr>{});
            expressionTypes[node] = voidType;
            node->type = std::shared_ptr<ast::TypeNode>(voidType->clone().release());
            return;
        }

        // Handle math library intrinsics
        {
            static const std::set<std::string> floatMathFuncs = {
                "sqrt", "sin", "cos", "tan", "exp", "log", "log2", "log10",
                "floor", "ceil", "round", "pow"
            };
            static const std::set<std::string> intOrFloatFuncs = {
                "abs", "min", "max"
            };
            if (floatMathFuncs.count(name)) {
                size_t expected = (name == "pow") ? 2 : 1;
                if (node->arguments.size() != expected) {
                    addError(name + "() expects " + std::to_string(expected) + " argument(s)", node);
                    return;
                }
                for (auto& arg : node->arguments) if (arg) arg->accept(*this);
                auto floatType = new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, "Float"));
                expressionTypes[node] = floatType;
                node->type = std::shared_ptr<ast::TypeNode>(floatType->clone());
                return;
            }
            if (intOrFloatFuncs.count(name)) {
                if (node->arguments.size() < 1 || node->arguments.size() > 2) {
                    addError(name + "() expects 1 or 2 arguments", node);
                    return;
                }
                for (auto& arg : node->arguments) if (arg) arg->accept(*this);
                // Return type matches first argument - default to Int
                auto firstArgTypeIt = (node->arguments.size() > 0) ? expressionTypes.find(node->arguments[0].get()) : expressionTypes.end();
                if (firstArgTypeIt != expressionTypes.end() && firstArgTypeIt->second) {
                    expressionTypes[node] = firstArgTypeIt->second;
                    node->type = std::shared_ptr<ast::TypeNode>(firstArgTypeIt->second->clone());
                } else {
                    auto intType = new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, "Int"));
                    expressionTypes[node] = intType;
                    node->type = std::shared_ptr<ast::TypeNode>(intType->clone());
                }
                return;
            }
        }

        // Normal function calls return the function's declared return type.
        auto registryIt = functionRegistry.find(name);

        // Generic function calls: infer type arguments from the actual arguments
        // (a type parameter that appears bare or inside a parameter type is unified
        // with the argument's concrete type), validate declared aspect bounds against
        // the inferred concrete types, then substitute the inferred types into the
        // return type so callers see a concrete type instead of the placeholder T.
        std::function<void(ast::TypeNode*, ast::TypeNode*,
                           const std::set<std::string>&,
                           std::map<std::string, ast::TypeNode*>&)> unifyGenericType;
        unifyGenericType = [&](ast::TypeNode* paramType, ast::TypeNode* argType,
                               const std::set<std::string>& genericParamNames,
                               std::map<std::string, ast::TypeNode*>& substitutions) -> void {
            if (!paramType || !argType) return;
            if (auto tn = dynamic_cast<ast::TypeName*>(paramType)) {
                if (tn->identifier && tn->genericArgs.empty() &&
                    genericParamNames.count(tn->identifier->name)) {
                    substitutions[tn->identifier->name] = argType;
                    return;
                }
                if (auto argTn = dynamic_cast<ast::TypeName*>(argType)) {
                    if (tn->identifier && argTn->identifier &&
                        tn->identifier->name == argTn->identifier->name &&
                        tn->genericArgs.size() == argTn->genericArgs.size()) {
                        for (size_t i = 0; i < tn->genericArgs.size(); ++i) {
                            unifyGenericType(tn->genericArgs[i].get(), argTn->genericArgs[i].get(),
                                             genericParamNames, substitutions);
                        }
                    }
                }
                return;
            }
            if (auto vt = dynamic_cast<ast::VecType*>(paramType)) {
                if (auto avt = dynamic_cast<ast::VecType*>(argType)) {
                    unifyGenericType(vt->elementType.get(), avt->elementType.get(),
                                     genericParamNames, substitutions);
                }
                return;
            }
            if (auto ft = dynamic_cast<ast::FutureType*>(paramType)) {
                if (auto aft = dynamic_cast<ast::FutureType*>(argType)) {
                    unifyGenericType(ft->resultType.get(), aft->resultType.get(),
                                     genericParamNames, substitutions);
                }
                return;
            }
            if (auto ot = dynamic_cast<ast::OptionalType*>(paramType)) {
                if (auto aot = dynamic_cast<ast::OptionalType*>(argType)) {
                    unifyGenericType(ot->containedType.get(), aot->containedType.get(),
                                     genericParamNames, substitutions);
                }
                return;
            }
            if (auto pt = dynamic_cast<ast::PointerType*>(paramType)) {
                if (auto apt = dynamic_cast<ast::PointerType*>(argType)) {
                    unifyGenericType(pt->pointeeType.get(), apt->pointeeType.get(),
                                     genericParamNames, substitutions);
                }
                return;
            }
            if (auto tt = dynamic_cast<ast::TupleTypeNode*>(paramType)) {
                auto argTuple = dynamic_cast<ast::TupleTypeNode*>(argType);
                if (argTuple && tt->memberTypes.size() == argTuple->memberTypes.size()) {
                    for (size_t i = 0; i < tt->memberTypes.size(); ++i) {
                        unifyGenericType(tt->memberTypes[i].get(),
                                         argTuple->memberTypes[i].get(),
                                         genericParamNames, substitutions);
                    }
                }
                return;
            }
        };

        // Does a concrete type have a bind (concrete or generic matching) for an aspect?
        auto hasAspectBind = [&](const std::string& concreteTypeStr,
                                 const std::string& aspectName) -> bool {
            auto it = traitImpls.find(concreteTypeStr);
            if (it != traitImpls.end() && it->second.count(aspectName)) {
                return true;
            }
            for (const auto& typeEntry : genericTraitImpls) {
                if (matchesPattern(concreteTypeStr, typeEntry.first) &&
                    typeEntry.second.count(aspectName)) {
                    return true;
                }
            }
            return false;
        };

        // Does a type parameter name in the current scope carry the bound itself?
        // (e.g. inside `bind<K<Hashable>, V> ... -> Map<K,V>`, `K` satisfies a
        // Hashable bound because its own declaration is bounded; `hashkey<K<Hashable>>(ck)`
        // with `ck:K` then validates.)
        auto typeParamHasBound = [&](const std::string& typeParamName,
                                     const std::string& aspectName) -> bool {
            SymbolInfo* sym = currentScope->lookup(typeParamName);
            if (!sym || sym->kind != SymbolInfo::Kind::TYPE_PARAMETER) {
                return false;
            }
            for (const auto& b : sym->bounds) {
                if (b == aspectName) return true;
            }
            return false;
        };

        // Build inferred substitutions + validate bounds + substitute the return type.
        auto resolveGenericCallReturnType =
            [&](ast::FunctionDeclaration* funcDecl) -> ast::TypeNodePtr {
            std::set<std::string> genericParamNames;
            for (const auto& gp : funcDecl->genericParams) {
                if (gp && gp->name) {
                    genericParamNames.insert(gp->name->name);
                }
            }

            std::map<std::string, ast::TypeNode*> substitutions;
            if (!node->explicitTypeArgs.empty()) {
                // Explicit type arguments (e.g. `make_box<Int>(7)`) determine the
                // substitutions directly, matched positionally to the type params.
                for (size_t i = 0; i < node->explicitTypeArgs.size() && i < funcDecl->genericParams.size(); ++i) {
                    if (funcDecl->genericParams[i] && funcDecl->genericParams[i]->name &&
                        node->explicitTypeArgs[i]) {
                        substitutions[funcDecl->genericParams[i]->name->name] = node->explicitTypeArgs[i].get();
                    }
                }
            } else {
                for (size_t i = 0; i < node->arguments.size() && i < funcDecl->params.size(); ++i) {
                    auto argTypeIt = expressionTypes.find(node->arguments[i].get());
                    ast::TypeNode* argType = (argTypeIt != expressionTypes.end()) ? argTypeIt->second : nullptr;
                    if (argType) {
                        unifyGenericType(funcDecl->params[i].typeNode.get(), argType,
                                         genericParamNames, substitutions);
                    }
                }
            }

            // Bounds validation: each bounded type parameter's inferred concrete type
            // must itself bind the declared aspect(s).
            for (const auto& gp : funcDecl->genericParams) {
                if (!gp || !gp->name || gp->bounds.empty()) continue;
                auto sit = substitutions.find(gp->name->name);
                if (sit != substitutions.end() && sit->second) {
                    std::string concreteTypeStr = sit->second->toString();
                    for (const auto& bound : gp->bounds) {
                        if (!bound) continue;
                        std::string boundName = bound->toString();
                        if (!hasAspectBind(concreteTypeStr, boundName) &&
                            !typeParamHasBound(concreteTypeStr, boundName)) {
                            addError("Type '" + concreteTypeStr + "' does not satisfy aspect bound '" +
                                     boundName + "' on type parameter '" + gp->name->name +
                                     "' for function '" + (funcDecl->id ? funcDecl->id->name : "<anonymous>") +
                                     "'.", node);
                        }
                    }
                }
            }

            ast::TypeNode* returnTypeNode = funcDecl->returnTypeNode
                ? funcDecl->returnTypeNode.get() : nullptr;
            if (!returnTypeNode) {
                return nullptr;
            }
            if (substitutions.empty()) {
                return ast::TypeNodePtr(returnTypeNode->clone());
            }
            return substituteGenericArgsForValidation(returnTypeNode, substitutions);
        };

        SymbolInfo* functionSymbol = currentScope->lookup(name);
        if (functionSymbol && functionSymbol->kind == SymbolInfo::Kind::Function && functionSymbol->type) {
            if (auto functionType = dynamic_cast<ast::FunctionType*>(functionSymbol->type)) {
                if (functionType->returnType) {
                    bool isGenericCall = registryIt != functionRegistry.end() &&
                        registryIt->second && !registryIt->second->genericParams.empty();
                    if (isGenericCall) {
                        ast::TypeNodePtr substituted = resolveGenericCallReturnType(registryIt->second);
                        if (substituted) {
                            node->type = std::shared_ptr<ast::TypeNode>(substituted.release());
                            expressionTypes[node] = node->type.get();
                        }
                    } else {
                        ast::TypeNode* returnType = functionType->returnType->type
                            ? functionType->returnType->type.get()
                            : functionType->returnType.get();
                        expressionTypes[node] = returnType;
                        node->type = std::shared_ptr<ast::TypeNode>(returnType->clone());
                    }
                }

            // Move tracking: mark MY-owned argument identifiers as moved when passed to function
            for (auto& arg : node->arguments) {
                if (auto* argIdent = dynamic_cast<ast::Identifier*>(arg.get())) {
                    SymbolInfo* argSymbol = currentScope->lookup(argIdent->name);
                    if (argSymbol && hasOwnershipKindMY(argSymbol)) {
                        recordMove(argIdent->name);
                    }
                }
            }
                return;
            }
        }

        if (registryIt != functionRegistry.end() && registryIt->second && registryIt->second->returnTypeNode) {
            ast::TypeNode* declaredReturn = registryIt->second->returnTypeNode.get();
            if (!declaredReturn->type) {
                declaredReturn->accept(*this);
            }
            ast::TypeNode* returnType = declaredReturn->type ? declaredReturn->type.get() : declaredReturn;
            expressionTypes[node] = returnType;
            node->type = std::shared_ptr<ast::TypeNode>(returnType->clone());

            // Move tracking: mark MY-owned argument identifiers as moved when passed to function
            for (auto& arg : node->arguments) {
                if (auto* argIdent = dynamic_cast<ast::Identifier*>(arg.get())) {
                    SymbolInfo* argSymbol = currentScope->lookup(argIdent->name);
                    if (argSymbol && hasOwnershipKindMY(argSymbol)) {
                        recordMove(argIdent->name);
                    }
                }
            }
            return;
        }
    }

    // Handle Vec::new() constructor calls
    if (auto memberExpr = dynamic_cast<ast::MemberExpression*>(node->callee.get())) {
        // Check if this is Vec::new()
        if (auto vecIdent = dynamic_cast<ast::Identifier*>(memberExpr->object.get())) {
            if (auto newIdent = dynamic_cast<ast::Identifier*>(memberExpr->property.get())) {
                if (vecIdent->name == "Vec" && newIdent->name == "new") {
                    // This is Vec::new() or Vec::new(size) - create a vector
                    if (node->arguments.size() > 1) {
                        addError("Vec::new() accepts at most 1 argument (optional size)", node);
                        return;
                    }

                    // If size argument is provided, validate it's an integer type
                    if (node->arguments.size() == 1) {
                        auto sizeArg = node->arguments[0].get();
                        if (sizeArg) {
                            sizeArg->accept(*this);
                            // TODO: In full implementation, validate that size argument is Int type
                        }
                    }

                    // Check if type was already set by VariableDeclaration (type propagation)
                    if (expressionTypes.count(node) && expressionTypes[node]) {
                        // Type already set - use it
                        VYB_CDBG << "DEBUG: Vec::new() type already propagated: " << expressionTypes[node]->toString() << std::endl;
                        return;
                    }

                    // Need to infer element type from context or default to a generic type
                    // For now, we'll default to Vec<Int> if no context is available
                    // TODO: In a full implementation, this should be inferred from the variable declaration
                    auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
                    auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
                    auto vecType = std::make_unique<ast::VecType>(node->loc, std::move(intType));

                    expressionTypes[node] = vecType.get();
                    node->type = std::shared_ptr<ast::TypeNode>(std::move(vecType));
                    return;
                }

                // Handle T::from_string() static method calls for type conversion
                const std::string& typeName = vecIdent->name;
                const std::string& methodName = newIdent->name;

                if (methodName == "from_string") {
                    // Validate arguments
                    if (node->arguments.size() != 1) {
                        addError(typeName + "::from_string() expects exactly 1 argument (string to parse)", node);
                        return;
                    }

                    // Visit the string argument
                    node->arguments[0]->accept(*this);

                    // Validate argument is String type
                    auto argTypeIt = expressionTypes.find(node->arguments[0].get());
                    if (argTypeIt != expressionTypes.end() && argTypeIt->second) {
                        if (auto argTypeName = dynamic_cast<ast::TypeName*>(argTypeIt->second)) {
                            if (!argTypeName->identifier || argTypeName->identifier->name != "String") {
                                addError(typeName + "::from_string() argument must be of type String", node);
                                return;
                            }
                        }
                    }

                    // Return the target type (Int, Float, Bool, String, or custom struct)
                    // For primitives, return the type directly
                    if (typeName == "Int" || typeName == "Float" || typeName == "Bool" || typeName == "String") {
                        auto resultType = new ast::TypeName(node->loc,
                            std::make_unique<ast::Identifier>(node->loc, typeName));
                        expressionTypes[node] = resultType;
                        node->type = std::shared_ptr<ast::TypeNode>(resultType->clone());
                        VYB_CDBG << "DEBUG: " << typeName << "::from_string() returns " << typeName << std::endl;
                        return;
                    }

                    // For complex types (structs), check if type exists and return it
                    auto structIt = structFieldTypes.find(typeName);
                    if (structIt != structFieldTypes.end()) {
                        auto resultType = new ast::TypeName(node->loc,
                            std::make_unique<ast::Identifier>(node->loc, typeName));
                        expressionTypes[node] = resultType;
                        node->type = std::shared_ptr<ast::TypeNode>(resultType->clone());
                        VYB_CDBG << "DEBUG: " << typeName << "::from_string() returns " << typeName << " (custom struct)" << std::endl;
                        return;
                    }

                    addError("Unknown type '" + typeName + "' in from_string() call", node);
                    return;
                }

                // Handle String::from_bytes() static factory method
                if (typeName == "String" && methodName == "from_bytes") {
                    if (node->arguments.size() != 2) {
                        addError("String::from_bytes() expects exactly 2 arguments (byte_ptr, length)", node);
                        return;
                    }
                    for (auto& arg : node->arguments) arg->accept(*this);
                    auto strType = new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, "String"));
                    expressionTypes[node] = strType;
                    node->type = std::shared_ptr<ast::TypeNode>(strType->clone());
                    return;
                }
            }
        }

        // Handle Vec instance method calls: obj.method()
        // First check if it's a simple identifier (e.g., vec.push())
        if (auto objIdent = dynamic_cast<ast::Identifier*>(memberExpr->object.get())) {
            if (auto methodIdent = dynamic_cast<ast::Identifier*>(memberExpr->property.get())) {
                std::string methodName = methodIdent->name;

                // Look up the object's type
                SymbolInfo* objSymbol = currentScope->lookup(objIdent->name);
                if (objSymbol && objSymbol->type) {
                    // Check if it's a Vec type (directly or as TypeName "Vec<T>" from function params)
                    if (dynamic_cast<ast::VecType*>(objSymbol->type) && isBuiltinVecMethodName(methodName)) {
                        handleVecMethodCall(node, objIdent->name, methodName);
                        return;
                    }
                    if (auto tn = dynamic_cast<ast::TypeName*>(objSymbol->type)) {
                        if (tn->identifier && tn->identifier->name == "Vec" && isBuiltinVecMethodName(methodName)) {
                            handleVecMethodCall(node, objIdent->name, methodName);
                            return;
                        }
                    }

                    // Check if it's a Tuple type
                    if (auto tupleType = dynamic_cast<ast::TupleTypeNode*>(objSymbol->type)) {
                        if (methodName == "len") {
                            // len() -> Int
                            if (node->arguments.size() != 0) {
                                addError("Tuple::len expects no arguments", node);
                                return;
                            }
                            auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
                            auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
                            expressionTypes[node] = intType.get();
                            node->type = std::shared_ptr<ast::TypeNode>(std::move(intType));
                            return;
                        }
                    }

                    struct AspectMethodCandidate {
                        std::string traitName;
                        ast::TypeNode* returnType;
                        bool synthesized = false; // owns a temporary node (needs a stable copy)
                    };

                    auto resolveAspectMethodForTypeString = [&](const std::string& typeNameStr) -> bool {
                        std::vector<AspectMethodCandidate> candidates;
                        std::set<std::string> seenCandidates;
                        auto addCandidate = [&](const std::string& traitName, ast::TypeNode* returnType, bool synthesized = false) {
                            std::string key = traitName + "::" + methodName;
                            if (seenCandidates.insert(key).second) {
                                candidates.push_back({traitName, returnType, synthesized});
                            }
                        };

                        // Replace whole-identifier occurrences of `tok` with `repl`.
                        auto replaceTokens = [](std::string s, const std::string& tok, const std::string& repl) {
                            if (tok.empty()) return s;
                            auto isIdChar = [](char c) {
                                return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
                            };
                            std::string out;
                            out.reserve(s.size());
                            for (size_t i = 0; i < s.size();) {
                                if (i + tok.size() <= s.size() && s.compare(i, tok.size(), tok) == 0 &&
                                    (i == 0 || !isIdChar(s[i - 1])) &&
                                    (i + tok.size() == s.size() || !isIdChar(s[i + tok.size()]))) {
                                    out += repl;
                                    i += tok.size();
                                } else {
                                    out += s[i];
                                    ++i;
                                }
                            }
                            return out;
                        };

                        // Top-level generic type arguments from "Boxer<Int>" -> ["Int"].
                        auto parseTypeArgs = [](const std::string& s) -> std::vector<std::string> {
                            size_t lt = s.find('<');
                            if (lt == std::string::npos) return {};
                            size_t gt = s.rfind('>');
                            if (gt == std::string::npos || gt < lt) return {};
                            std::string inner = s.substr(lt + 1, gt - lt - 1);
                            std::vector<std::string> args;
                            std::string cur;
                            int depth = 0;
                            for (char c : inner) {
                                if (c == '<') ++depth;
                                else if (c == '>') --depth;
                                if (c == ',' && depth == 0) {
                                    args.push_back(cur);
                                    cur.clear();
                                } else {
                                    cur.push_back(c);
                                }
                            }
                            if (!cur.empty()) args.push_back(cur);
                            return args;
                        };

                        // Resolve a generic-impl method's return type against the concrete
                        // type: substitutes type parameters (and Self::Item through the
                        // generic bind's associated-type assignments) to the concrete type.
                        auto resolveGenericReturnType = [&](const std::string& concreteType,
                                                            const std::string& traitName,
                                                            const GenericImplInfo* implInfo,
                                                            ast::TypeNode* returnTypeNode) -> std::string {
                            if (!returnTypeNode) return "";
                            std::string rt = returnTypeNode->toString();
                            const std::string selfPrefix = "Self::";
                            const std::string traitPrefix = traitName + "::";
                            if (rt.rfind(selfPrefix, 0) == 0 || rt.rfind(traitPrefix, 0) == 0) {
                                size_t sep = rt.find("::");
                                std::string assocName = (sep != std::string::npos) ? rt.substr(sep + 2) : rt;
                                auto it = implInfo->associatedTypeBindings.find(assocName);
                                if (it != implInfo->associatedTypeBindings.end() && it->second) {
                                    rt = it->second->toString();
                                }
                            }
                            std::vector<std::string> args = parseTypeArgs(concreteType);
                            if (!args.empty()) {
                                const auto& params = implInfo->typeParams;
                                for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
                                    rt = replaceTokens(rt, params[i], args[i]);
                                }
                            }
                            return rt;
                        };

                        // Owns synthesized return-type nodes created for generic impls.
                        std::vector<std::unique_ptr<ast::TypeNode>> resolvedReturnTypes;

                        // Resolve a Self::Item / <trait>::Item return reference against the
                        // associated-type bindings of a concrete trait impl.
                        auto resolveConcreteAssocReturnType = [&](const std::string& rtStr,
                                                                  const std::string& traitName) -> std::string {
                            if (rtStr.rfind("Self::", 0) == 0 || rtStr.rfind(traitName + "::", 0) == 0) {
                                std::string bound = resolveAssociatedTypeReference(typeNameStr, traitName, rtStr);
                                if (!bound.empty()) {
                                    return replaceTokens(bound, "Self", typeNameStr);
                                }
                            }
                            return "";
                        };

                        auto typeImplsIt = traitImpls.find(typeNameStr);
                        if (typeImplsIt != traitImpls.end()) {
                            for (const auto& traitEntry : typeImplsIt->second) {
                                const std::string& traitName = traitEntry.first;
                                const std::vector<ast::FunctionDeclaration*>& methods = traitEntry.second;
                                bool foundInImpl = false;

                                for (ast::FunctionDeclaration* method : methods) {
                                    if (method && method->id && method->id->name == methodName) {
                                        std::string rtStr = method->returnTypeNode ? method->returnTypeNode->toString() : "";
                                        std::string resolved = resolveConcreteAssocReturnType(rtStr, traitName);
                                        if (resolved.empty()) {
                                            addCandidate(traitName, method->returnTypeNode.get());
                                        } else {
                                            auto resolvedNode = std::make_unique<ast::TypeName>(
                                                node->loc, std::make_unique<ast::Identifier>(node->loc, resolved));
                                            ast::TypeNode* ptr = resolvedNode.get();
                                            resolvedReturnTypes.push_back(std::move(resolvedNode));
                                            addCandidate(traitName, ptr, true);
                                        }
                                        foundInImpl = true;
                                    }
                                }

                                if (!foundInImpl) {
                                    auto traitIt = traitRegistry.find(traitName);
                                    if (traitIt != traitRegistry.end()) {
                                        for (const auto& traitMethod : traitIt->second->methods) {
                                            if (traitMethod.name == methodName && traitMethod.hasDefaultImpl) {
                                                std::string rtStr = traitMethod.returnType ? traitMethod.returnType->toString() : "";
                                                std::string resolved = resolveConcreteAssocReturnType(rtStr, traitName);
                                                if (!resolved.empty()) {
                                                    auto resolvedNode = std::make_unique<ast::TypeName>(
                                                        node->loc, std::make_unique<ast::Identifier>(node->loc, resolved));
                                                    ast::TypeNode* ptr = resolvedNode.get();
                                                    resolvedReturnTypes.push_back(std::move(resolvedNode));
                                                    addCandidate(traitName, ptr, true);
                                                    continue;
                                                }
                                                addCandidate(traitName, traitMethod.returnType);
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        for (const auto& typeEntry : genericTraitImpls) {
                            const std::string& pattern = typeEntry.first;
                            if (matchesPattern(typeNameStr, pattern)) {
                                for (const auto& traitEntry : typeEntry.second) {
                                    const std::string& traitName = traitEntry.first;
                                    const GenericImplInfo* implInfo = traitEntry.second.get();
                                    if (implInfo && implInfo->declaration) {
                                        bool foundInImpl = false;
                                        for (const auto& method : implInfo->declaration->methods) {
                                            if (method && method->id && method->id->name == methodName) {
                                                std::string resolved = resolveGenericReturnType(
                                                    typeNameStr, traitName, implInfo, method->returnTypeNode.get());
                                                if (resolved.empty()) {
                                                    addCandidate(traitName, method->returnTypeNode.get());
                                                } else {
                                                    auto resolvedNode = std::make_unique<ast::TypeName>(
                                                        node->loc, std::make_unique<ast::Identifier>(node->loc, resolved));
                                                    ast::TypeNode* ptr = resolvedNode.get();
                                                    resolvedReturnTypes.push_back(std::move(resolvedNode));
                                                    addCandidate(traitName, ptr, true);
                                                }
                                                foundInImpl = true;
                                            }
                                        }

                                        if (!foundInImpl) {
                                            auto traitIt = traitRegistry.find(traitName);
                                            if (traitIt != traitRegistry.end()) {
                                                for (const auto& traitMethod : traitIt->second->methods) {
                                                    if (traitMethod.name == methodName && traitMethod.hasDefaultImpl) {
                                                        std::string resolved = resolveGenericReturnType(
                                                            typeNameStr, traitName, implInfo, traitMethod.returnType);
                                                        if (resolved.empty()) {
                                                            addCandidate(traitName, traitMethod.returnType);
                                                        } else {
                                                            auto resolvedNode = std::make_unique<ast::TypeName>(
                                                                node->loc, std::make_unique<ast::Identifier>(node->loc, resolved));
                                                            ast::TypeNode* ptr = resolvedNode.get();
                                                            resolvedReturnTypes.push_back(std::move(resolvedNode));
                                                            addCandidate(traitName, ptr, true);
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (candidates.empty()) {
                            return false;
                        }

                        if (candidates.size() > 1) {
                            std::string candidateList;
                            for (size_t i = 0; i < candidates.size(); ++i) {
                                if (i > 0) candidateList += ", ";
                                candidateList += candidates[i].traitName + "::" + methodName;
                            }
                            addError("Ambiguous aspect method '" + methodName + "' for type '" +
                                     typeNameStr + "'. Candidates: " + candidateList, node);
                            return true;
                        }

                        if (candidates[0].returnType) {
                            ast::TypeNode* actualReturnType = substituteSelfType(candidates[0].returnType, typeNameStr);
                            // Synthesized nodes are owned by the temp buffer inside this
                            // lambda; keep a stable heap copy so later type checks don't
                            // dangle after the buffer goes out of scope.
                            if (candidates[0].synthesized) {
                                ast::TypeNode* stableCopy = actualReturnType->clone().release();
                                expressionTypes[node] = stableCopy;
                                node->type = std::shared_ptr<ast::TypeNode>(stableCopy->clone());
                            } else {
                                expressionTypes[node] = actualReturnType;
                                node->type = std::shared_ptr<ast::TypeNode>(actualReturnType->clone());
                            }
                        }
                        return true;
                    };

                    if (auto vecType = dynamic_cast<ast::VecType*>(objSymbol->type)) {
                        if (resolveAspectMethodForTypeString(vecType->toString())) {
                            return;
                        }
                    }

                    // Otherwise check for trait methods
                    if (auto typeName = dynamic_cast<ast::TypeName*>(objSymbol->type)) {
                        if (typeName->identifier) {
                            std::string typeNameStr = typeName->toString(); // Use full type with generic args

                            // Handle mild<T>.grab() and mild<T>.released() intrinsic methods
                            if (typeNameStr.find("mild<") == 0) {
                                if (methodName == "grab") {
                                    // mild<T>.grab() returns our<T>
                                    VYB_CDBG << "DEBUG: Setting return type for mild<T>.grab()" << std::endl;
                                    // Extract T from mild<T>
                                    if (!typeName->genericArgs.empty()) {
                                        auto innerType = typeName->genericArgs[0].get();
                                        // Create our<T> type
                                        auto ourId = std::make_unique<ast::Identifier>(node->loc, "our");
                                        std::vector<ast::TypeNodePtr> ourArgs;
                                        ourArgs.push_back(innerType->clone());
                                        auto resultType = new ast::TypeName(node->loc, std::move(ourId), std::move(ourArgs));
                                        expressionTypes[node] = resultType;
                                        node->type = std::shared_ptr<ast::TypeNode>(resultType->clone());
                                        VYB_CDBG << "DEBUG: mild<T>.grab() returns " << resultType->toString() << std::endl;
                                        return;
                                    }
                                } else if (methodName == "released") {
                                    // mild<T>.released() returns Bool
                                    VYB_CDBG << "DEBUG: Setting return type for mild<T>.released() to Bool" << std::endl;
                                    auto boolType = new ast::TypeName(node->loc,
                                        std::make_unique<ast::Identifier>(node->loc, "Bool"));
                                    expressionTypes[node] = boolType;
                                    node->type = std::shared_ptr<ast::TypeNode>(boolType->clone());
                                    return;
                                }
                            }

                            if (resolveAspectMethodForTypeString(typeNameStr)) {
                                return;
                            }

                            // Look for concrete trait implementations
                            auto typeImplsIt = traitImpls.find(typeNameStr);
                            if (typeImplsIt != traitImpls.end()) {
                                for (const auto& traitEntry : typeImplsIt->second) {
                                    const std::string& traitName = traitEntry.first;
                                    const std::vector<ast::FunctionDeclaration*>& methods = traitEntry.second;

                                    for (ast::FunctionDeclaration* method : methods) {
                                        if (method && method->id && method->id->name == methodName) {
                                            if (method->returnTypeNode) {
                                                // Substitute Self with concrete type
                                                ast::TypeNode* actualReturnType = substituteSelfType(method->returnTypeNode.get(), typeNameStr);
                                                expressionTypes[node] = actualReturnType;
                                                node->type = std::shared_ptr<ast::TypeNode>(actualReturnType->clone());
                                            }

                                            VYB_CDBG << "DEBUG: Resolved trait method call: " << typeNameStr
                                                      << "." << methodName << " from trait " << traitName << std::endl;
                                            return;
                                        }
                                    }
                                }
                            }

                            // Also check generic trait impls
                            for (const auto& typeEntry : genericTraitImpls) {
                                const std::string& pattern = typeEntry.first;

                                if (matchesPattern(typeNameStr, pattern)) {
                                    for (const auto& traitEntry : typeEntry.second) {
                                        const std::string& traitName = traitEntry.first;
                                        const GenericImplInfo* implInfo = traitEntry.second.get();
                                        if (implInfo && implInfo->declaration) {
                                            for (const auto& method : implInfo->declaration->methods) {
                                                if (method && method->id && method->id->name == methodName) {
                                                    if (method->returnTypeNode) {
                                                        // Substitute Self with concrete type
                                                        ast::TypeNode* actualReturnType = substituteSelfType(method->returnTypeNode.get(), typeNameStr);
                                                        expressionTypes[node] = actualReturnType;
                                                        node->type = std::shared_ptr<ast::TypeNode>(actualReturnType->clone());
                                                    }
                                                    return;
                                                }
                                            }

                                            // Not in impl - check if aspect has default implementation
                                            auto traitIt = traitRegistry.find(traitName);
                                            if (traitIt != traitRegistry.end()) {
                                                for (const auto& traitMethod : traitIt->second->methods) {
                                                    if (traitMethod.name == methodName && traitMethod.hasDefaultImpl) {
                                                        // Found default implementation
                                                        if (traitMethod.returnType) {
                                                            setResolvedTraitReturnType(node, typeNameStr, traitName, traitMethod.returnType);
                                                        }
                                                        return;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // Check concrete impls for default methods
                            auto concreteImplsIt = traitImpls.find(typeNameStr);
                            if (concreteImplsIt != traitImpls.end()) {
                                for (const auto& traitEntry : concreteImplsIt->second) {
                                    const std::string& traitName = traitEntry.first;
                                    const std::vector<ast::FunctionDeclaration*>& methods = traitEntry.second;

                                    // Check if method is in impl
                                    bool foundInImpl = false;
                                    for (ast::FunctionDeclaration* method : methods) {
                                        if (method && method->id && method->id->name == methodName) {
                                            foundInImpl = true;
                                            break;
                                        }
                                    }

                                    // If not in impl, check aspect for default implementation
                                    if (!foundInImpl) {
                                        auto traitIt = traitRegistry.find(traitName);
                                        if (traitIt != traitRegistry.end()) {
                                            for (const auto& traitMethod : traitIt->second->methods) {
                                                if (traitMethod.name == methodName && traitMethod.hasDefaultImpl) {
                                                    // Found default implementation
                                                    if (traitMethod.returnType) {
                                                        setResolvedTraitReturnType(node, typeNameStr, traitName, traitMethod.returnType);
                                                    }
                                                    return;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Check if this is a method call on a type parameter
                if (objSymbol && objSymbol->type) {
                    if (auto typeName = dynamic_cast<ast::TypeName*>(objSymbol->type)) {
                        if (typeName->identifier && typeName->genericArgs.empty()) {
                            std::string typeStr = typeName->identifier->name;
                            SymbolInfo* typeParamSym = currentScope->lookup(typeStr);
                            if (typeParamSym && typeParamSym->kind == SymbolInfo::Kind::TYPE_PARAMETER) {
                                // This is a type parameter - check its bounds for the method
                                for (const std::string& boundName : typeParamSym->bounds) {
                                    const TraitInfo* declaringAspect = nullptr;
                                    const TraitMethod* method = nullptr;
                                    if (findAspectMethod(boundName, methodName, declaringAspect, method)) {
                                        // Found! Set return type from the aspect
                                        if (method->returnType) {
                                            // Return type might be Self - substitute with type parameter
                                            ast::TypeNode* actualReturnType = substituteSelfType(method->returnType, typeStr);
                                            expressionTypes[node] = actualReturnType;
                                            node->type = std::shared_ptr<ast::TypeNode>(actualReturnType->clone());
                                        }
                                        return;
                                    }
                                }
                            }
                        }
                    }
                }

                // If we reach here and it's a Vec type, try Vec-specific methods
                // Otherwise, it's an unknown method error
                if (objSymbol && objSymbol->type) {
                    if (dynamic_cast<ast::VecType*>(objSymbol->type) && isBuiltinVecMethodName(methodName)) {
                        handleVecMethodCall(node, objIdent->name, methodName);
                        return;
                    }

                    // Check for primitive type methods (Int.to_string(), etc.)
                    if (auto objTypeName = dynamic_cast<ast::TypeName*>(objSymbol->type)) {
                        if (objTypeName->identifier && objTypeName->identifier->name == "Vec" && isBuiltinVecMethodName(methodName)) {
                            handleVecMethodCall(node, objIdent->name, methodName);
                            return;
                        }
                        if (objTypeName->identifier) {
                            std::string typeStr = objTypeName->identifier->name;
                            if (methodName == "to_string" &&
                                (typeStr == "Int" || typeStr == "Float" || typeStr == "Bool")) {
                                // Return String type for .to_string() on primitives
                                auto stringType = new ast::TypeName(node->loc,
                                    std::make_unique<ast::Identifier>(node->loc, "String"));
                                expressionTypes[node] = stringType;
                                node->type = std::shared_ptr<ast::TypeNode>(stringType->clone());
                                VYB_CDBG << "DEBUG: Primitive method " << typeStr << ".to_string() returns String (early path)" << std::endl;
                                return;
                            }

                            // Check for complex/struct type methods
                            if (methodName == "to_string") {
                                auto structIt = structFieldTypes.find(typeStr);
                                if (structIt != structFieldTypes.end()) {
                                    // Complex type .to_string() → JSON serialization
                                    auto stringType = new ast::TypeName(node->loc,
                                        std::make_unique<ast::Identifier>(node->loc, "String"));
                                    expressionTypes[node] = stringType;
                                    node->type = std::shared_ptr<ast::TypeNode>(stringType->clone());
                                    VYB_CDBG << "DEBUG: Complex type " << typeStr << ".to_string() returns JSON String (early path)" << std::endl;
                                    return;
                                }
                            }
                        }
                    }
                }

                // Check for String type methods
                if (objSymbol && objSymbol->type) {
                    if (auto objTypeName = dynamic_cast<ast::TypeName*>(objSymbol->type)) {
                        if (objTypeName->identifier && objTypeName->identifier->name == "String") {
                            // String methods: len/length -> Int, contains/starts_with/ends_with -> Bool,
                            // substring/to_upper/to_lower/concat -> String, char_at -> Int
                            if (methodName == "len" || methodName == "length") {
                                auto intType = new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, "Int"));
                                expressionTypes[node] = intType;
                                node->type = std::shared_ptr<ast::TypeNode>(intType->clone());
                                return;
                            } else if (methodName == "contains" || methodName == "starts_with" || methodName == "ends_with") {
                                auto boolType = new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, "Bool"));
                                expressionTypes[node] = boolType;
                                node->type = std::shared_ptr<ast::TypeNode>(boolType->clone());
                                return;
                            } else if (methodName == "substring" || methodName == "substr" ||
                                       methodName == "to_upper" || methodName == "to_lower" ||
                                       methodName == "concat" || methodName == "trim" ||
                                       methodName == "strip" || methodName == "replace") {
                                auto strType = new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, "String"));
                                expressionTypes[node] = strType;
                                node->type = std::shared_ptr<ast::TypeNode>(strType->clone());
                                return;
                            } else if (methodName == "char_at") {
                                auto intType = new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, "Int"));
                                expressionTypes[node] = intType;
                                node->type = std::shared_ptr<ast::TypeNode>(intType->clone());
                                return;
                            } else if (methodName == "to_bytes") {
                                auto intPtrType = new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, "Int"));
                                expressionTypes[node] = intPtrType;
                                node->type = std::shared_ptr<ast::TypeNode>(intPtrType->clone());
                                return;
                            }
                        }
                    }
                }

                // Not a Vec and not a trait method - unknown method
                std::string typeName = objSymbol && objSymbol->type ? objSymbol->type->toString() : "unknown";
                addError("Unknown method '" + methodName + "' on type '" + typeName + "'", node);
                return;
            }
        }

        // Now handle the case where object is a member expression (e.g., tree.nodes.push())
        // In this case, we need to analyze the object to determine its type
        if (auto methodIdent = dynamic_cast<ast::Identifier*>(memberExpr->property.get())) {
            // Visit the object expression to determine its type
            memberExpr->object->accept(*this);

            // Get the object's type
            auto objTypeIt = expressionTypes.find(memberExpr->object.get());
            if (objTypeIt != expressionTypes.end() && objTypeIt->second) {

                // Handle mild<T>.grab() and mild<T>.released() method calls
                std::string objTypeStr = objTypeIt->second->toString();
                std::string methodName = methodIdent->name;
                if (objTypeStr.find("mild<") == 0) {
                    if (methodName == "grab") {
                        // mild<T>.grab() returns our<T>? (optional)
                        // For now, just return the inner type wrapped in our<> (will be nil)
                        // TODO: Implement proper optional/result type
                        VYB_CDBG << "DEBUG: Setting return type for mild<T>.grab()" << std::endl;
                        // Extract T from mild<T>
                        if (auto typeName = dynamic_cast<ast::TypeName*>(objTypeIt->second)) {
                            if (!typeName->genericArgs.empty()) {
                                auto innerType = typeName->genericArgs[0].get();
                                // Create our<T> type
                                auto ourId = std::make_unique<ast::Identifier>(node->loc, "our");
                                std::vector<ast::TypeNodePtr> ourArgs;
                                ourArgs.push_back(innerType->clone());
                                auto resultType = new ast::TypeName(node->loc, std::move(ourId), std::move(ourArgs));
                                expressionTypes[node] = resultType;
                                node->type = std::shared_ptr<ast::TypeNode>(resultType->clone());
                                VYB_CDBG << "DEBUG: mild<T>.grab() returns " << resultType->toString() << std::endl;
                                return;
                            }
                        }
                    } else if (methodName == "released") {
                        // mild<T>.released() returns Bool
                        VYB_CDBG << "DEBUG: Setting return type for mild<T>.released() to Bool" << std::endl;
                        auto boolType = new ast::TypeName(node->loc,
                            std::make_unique<ast::Identifier>(node->loc, "Bool"));
                        expressionTypes[node] = boolType;
                        node->type = std::shared_ptr<ast::TypeNode>(boolType->clone());
                        return;
                    }
                }

                // Check if the object's type is a Vec type
                if (auto vecType = dynamic_cast<ast::VecType*>(objTypeIt->second)) {
                    // This is a Vec method call, handle it
                    // We pass a dummy name since we're working with member expressions
                    handleVecMethodCallOnMember(node, vecType, methodIdent->name);
                    return;
                }
                // Also handle TypeName "Vec<T>" (e.g., struct fields of Vec type)
                if (auto tn = dynamic_cast<ast::TypeName*>(objTypeIt->second)) {
                    if (tn->identifier && tn->identifier->name == "Vec") {
                        // Create a temporary VecType to pass to handleVecMethodCallOnMember
                        ast::TypeNodePtr elemType = tn->genericArgs.empty()
                            ? std::make_unique<ast::TypeName>(node->loc, std::make_unique<ast::Identifier>(node->loc, "Int"))
                            : tn->genericArgs[0]->clone();
                        auto tempVecType = std::make_unique<ast::VecType>(node->loc, std::move(elemType));
                        handleVecMethodCallOnMember(node, tempVecType.get(), methodIdent->name);
                        return;
                    }
                }

                // Check if this is a trait method call
                // Get the type name to look up trait implementations
                if (auto typeName = dynamic_cast<ast::TypeName*>(objTypeIt->second)) {
                    if (typeName->identifier) {
                        std::string typeNameStr = typeName->toString(); // Use full type string with generic args
                        std::string methodName = methodIdent->name;

                        // Look for concrete trait implementations for this type
                        auto typeImplsIt = traitImpls.find(typeNameStr);
                        if (typeImplsIt != traitImpls.end()) {
                            // Check each trait this type implements
                            for (const auto& traitEntry : typeImplsIt->second) {
                                const std::string& traitName = traitEntry.first;
                                const std::vector<ast::FunctionDeclaration*>& methods = traitEntry.second;

                                // Look for the method in this trait's implementation
                                for (ast::FunctionDeclaration* method : methods) {
                                    if (method && method->id && method->id->name == methodName) {
                                        // Found the method! Set the return type
                                        if (method->returnTypeNode) {
                                            expressionTypes[node] = method->returnTypeNode.get();
                                            node->type = std::shared_ptr<ast::TypeNode>(method->returnTypeNode->clone());
                                        }

                                        VYB_CDBG << "DEBUG: Resolved trait method call: " << typeNameStr
                                                  << "." << methodName << " from trait " << traitName << std::endl;
                                        return;
                                    }
                                }
                            }
                        }

                        // Also check generic trait impls - check if typeNameStr matches any pattern
                        for (const auto& typeEntry : genericTraitImpls) {
                            const std::string& pattern = typeEntry.first; // e.g., "Box<T>"

                            // Check if typeNameStr matches pattern (e.g., Box<Int> matches Box<T>)
                            if (matchesPattern(typeNameStr, pattern)) {
                                for (const auto& traitEntry : typeEntry.second) {
                                    const std::string& traitName = traitEntry.first;
                                    const GenericImplInfo* implInfo = traitEntry.second.get();
                                    if (implInfo && implInfo->declaration) {
                                        for (const auto& method : implInfo->declaration->methods) {
                                            if (method && method->id && method->id->name == methodName) {
                                                // Found the generic trait method! Substitute Self with concrete type
                                                if (method->returnTypeNode) {
                                                    VYB_CDBG << "DEBUG: Generic trait method " << methodName
                                                              << " return type before substitution: " << method->returnTypeNode->toString() << std::endl;
                                                    ast::TypeNode* actualReturnType = substituteSelfType(method->returnTypeNode.get(), typeNameStr);
                                                    VYB_CDBG << "DEBUG: After Self substitution: " << actualReturnType->toString() << std::endl;
                                                    expressionTypes[node] = actualReturnType;
                                                    node->type = std::shared_ptr<ast::TypeNode>(actualReturnType->clone());
                                                }
                                                return;
                                            }
                                        }

                                        // Not in impl - check if aspect has default implementation
                                        auto traitIt = traitRegistry.find(traitName);
                                        if (traitIt != traitRegistry.end()) {
                                            for (const auto& traitMethod : traitIt->second->methods) {
                                                if (traitMethod.name == methodName && traitMethod.hasDefaultImpl) {
                                                    // Found default implementation - set return type from aspect
                                                    if (traitMethod.returnType) {
                                                        setResolvedTraitReturnType(node, typeNameStr, traitName, traitMethod.returnType);
                                                    }
                                                    return;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Check concrete impls for default methods
                        auto concreteImplsIt = traitImpls.find(typeNameStr);
                        if (concreteImplsIt != traitImpls.end()) {
                            for (const auto& traitEntry : concreteImplsIt->second) {
                                const std::string& traitName = traitEntry.first;
                                const std::vector<ast::FunctionDeclaration*>& methods = traitEntry.second;

                                // Check if method is in impl
                                bool foundInImpl = false;
                                for (ast::FunctionDeclaration* method : methods) {
                                    if (method && method->id && method->id->name == methodName) {
                                        foundInImpl = true;
                                        break;
                                    }
                                }

                                // If not in impl, check aspect for default implementation
                                if (!foundInImpl) {
                                    auto traitIt = traitRegistry.find(traitName);
                                    if (traitIt != traitRegistry.end()) {
                                        for (const auto& traitMethod : traitIt->second->methods) {
                                            if (traitMethod.name == methodName && traitMethod.hasDefaultImpl) {
                                                // Found default implementation - set return type from aspect
                                                if (traitMethod.returnType) {
                                                    setResolvedTraitReturnType(node, typeNameStr, traitName, traitMethod.returnType);
                                                }
                                                return;
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Check if this is a type parameter with bounds
                        SymbolInfo* typeParamSym = currentScope->lookup(typeNameStr);
                        if (typeParamSym && typeParamSym->kind == SymbolInfo::Kind::TYPE_PARAMETER) {
                            // Type parameter - check if any bound provides this method
                            for (const std::string& boundName : typeParamSym->bounds) {
                                const TraitInfo* declaringAspect = nullptr;
                                const TraitMethod* method = nullptr;
                                if (findAspectMethod(boundName, methodName, declaringAspect, method)) {
                                    VYB_CDBG << "DEBUG: CallExpression: Type parameter " << typeNameStr
                                              << " with bound " << boundName
                                              << " allows method " << methodName << std::endl;
                                    // Found the method in bounds - substitute Self with type parameter
                                    if (method->returnType) {
                                        // Check if return type is Self - if so, replace with type parameter
                                        ast::TypeNode* actualReturnType = method->returnType;
                                        if (auto returnTypeName = dynamic_cast<ast::TypeName*>(method->returnType)) {
                                            if (returnTypeName->identifier && returnTypeName->identifier->name == "Self") {
                                                // Return Self -> substitute with type parameter
                                                actualReturnType = typeName; // The type parameter itself
                                                VYB_CDBG << "DEBUG: Substituting Self return type with type parameter " << typeNameStr << std::endl;
                                            }
                                        }
                                        expressionTypes[node] = actualReturnType;
                                        node->type = std::shared_ptr<ast::TypeNode>(actualReturnType->clone());
                                    }
                                    return;
                                }
                            }
                        }

                        // Check for primitive type methods (Int.to_string(), Float.to_string(), Bool.to_string())
                        if (methodName == "to_string") {
                            if (typeNameStr == "Int" || typeNameStr == "Float" || typeNameStr == "Bool") {
                                // Return String type for .to_string() on primitives
                                auto stringType = new ast::TypeName(node->loc,
                                    std::make_unique<ast::Identifier>(node->loc, "String"));
                                expressionTypes[node] = stringType;
                                node->type = std::shared_ptr<ast::TypeNode>(stringType->clone());
                                VYB_CDBG << "DEBUG: Primitive method " << typeNameStr << ".to_string() returns String" << std::endl;
                                return;
                            }

                            // Check if this is a complex/struct type
                            auto structIt = structFieldTypes.find(typeNameStr);
                            if (structIt != structFieldTypes.end()) {
                                // Complex type .to_string() → JSON serialization
                                auto stringType = new ast::TypeName(node->loc,
                                    std::make_unique<ast::Identifier>(node->loc, "String"));
                                expressionTypes[node] = stringType;
                                node->type = std::shared_ptr<ast::TypeNode>(stringType->clone());
                                VYB_CDBG << "DEBUG: Complex type " << typeNameStr << ".to_string() returns JSON String" << std::endl;
                                return;
                            }
                        }

                        // If we didn't find a trait method, it might be a struct method (future feature)
                        // or it's an error
                        addError("Method '" + methodName + "' not found for type '" + typeNameStr + "'", node);
                        return;
                    }
                }
            }
        }
    }

    // Fallback: look up return type from function registry for plain function calls
    if (auto ident = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        auto it = functionRegistry.find(ident->name);
        if (it != functionRegistry.end() && it->second->returnTypeNode) {
            expressionTypes[node] = it->second->returnTypeNode.get();
            node->type = std::shared_ptr<ast::TypeNode>(it->second->returnTypeNode->clone());
        }
    }
}

void SemanticAnalyzer::visit(ast::ArrayElementExpression* node) {
    if (!node || !node->array || !node->index) {
        addError("Malformed array element expression.", node);
        return;
    }

    // Process the array expression first
    node->array->accept(*this);

    // Process the index expression
    node->index->accept(*this);

    // Get the array's type to determine the element type
    auto arrayTypeIt = expressionTypes.find(node->array.get());
    if (arrayTypeIt != expressionTypes.end() && arrayTypeIt->second) {
        if (auto arrayType = dynamic_cast<ast::ArrayType*>(arrayTypeIt->second)) {
            // The element type is the array's element type
            if (arrayType->elementType) {
                expressionTypes[node] = arrayType->elementType->clone().release();
                node->type = std::shared_ptr<ast::TypeNode>(arrayType->elementType->clone());
            }
        }
    }

    // Validate index type (should be integer)
    auto indexTypeIt = expressionTypes.find(node->index.get());
    if (indexTypeIt != expressionTypes.end() && indexTypeIt->second) {
        if (auto indexTypeName = dynamic_cast<ast::TypeName*>(indexTypeIt->second)) {
            if (indexTypeName->identifier->name != "Int") {
                addError("Array index must be an integer type, got: " + indexTypeName->identifier->name, node);
            }
        }
    }
}
void SemanticAnalyzer::visit(ast::MemberExpression* node) {
    if (!node || !node->object || !node->property) {
        addError("Malformed member expression.", node);
        return;
    }

    // First, process the object to determine its type
    node->object->accept(*this);

    // Get the property identifier (must be an identifier for dot notation)
    ast::Identifier* propertyId = dynamic_cast<ast::Identifier*>(node->property.get());
    if (!propertyId) {
        addError("Member property must be an identifier.", node);
        return;
    }

    // Generic data enum variant access: `Box<Int>::Value` / `Box<Int>::Empty`.
    if (auto gi = dynamic_cast<ast::GenericInstantiationExpression*>(node->object.get())) {
        auto* baseId = dynamic_cast<ast::Identifier*>(gi->baseExpression.get());
        if (baseId && enumGenericParamOrder.count(baseId->name)) {
            auto typeIt = expressionTypes.find(gi);
            const std::string concreteStr = (typeIt != expressionTypes.end() && typeIt->second)
                ? typeIt->second->toString() : gi->toString();
            auto enumPayIt = enumVariantPayloadTypes.find(concreteStr);
            if (enumPayIt != enumVariantPayloadTypes.end()) {
                auto varIt = enumPayIt->second.find(propertyId->name);
                if (varIt == enumPayIt->second.end()) {
                    addError("Unknown variant '" + propertyId->name + "' of enum " + concreteStr, node);
                    return;
                }
                if (!varIt->second.empty()) {
                    addError("Enum variant '" + propertyId->name + "' of " + concreteStr +
                             " requires constructor arguments", node);
                    return;
                }
                auto enumType = std::make_unique<ast::TypeName>(node->loc,
                    std::make_unique<ast::Identifier>(node->loc, baseId->name));
                for (auto& a : gi->genericArguments) enumType->genericArgs.push_back(a->clone());
                node->type = std::shared_ptr<ast::TypeNode>(enumType->clone());
                expressionTypes[node] = enumType.release();
                return;
            }
        }
    }

    // Check for static method calls (e.g., Vec::new(), Int::from_string())
    // In these cases, the object is a type identifier, not a value
    if (auto objectIdent = dynamic_cast<ast::Identifier*>(node->object.get())) {
        const std::string& typeName = objectIdent->name;
        const std::string& methodName = propertyId->name;

        // Enum variant access: EnumName::VariantName
        if (enumTypeNames.count(typeName)) {
            auto enumPayIt = enumVariantPayloadTypes.find(typeName);
            if (enumPayIt != enumVariantPayloadTypes.end()) {
                // Tagged-union enum: a unit variant used as a value has the enum type.
                auto varIt = enumPayIt->second.find(methodName);
                if (varIt == enumPayIt->second.end()) {
                    addError("Unknown variant '" + methodName + "' of enum " + typeName, node);
                    return;
                }
                if (!varIt->second.empty()) {
                    addError("Enum variant '" + methodName + "' of " + typeName +
                             " requires constructor arguments", node);
                    return;
                }
                auto* enumVariantTy = new ast::TypeName(node->loc,
                    std::make_unique<ast::Identifier>(node->loc, typeName));
                node->type = std::shared_ptr<ast::TypeNode>(enumVariantTy->clone());
                expressionTypes[node] = enumVariantTy;
                return;
            }
            // C-like enums not otherwise registered: the variant carries the enum type.
            auto* enumVariantTy = new ast::TypeName(node->loc,
                std::make_unique<ast::Identifier>(node->loc, typeName));
            node->type = std::shared_ptr<ast::TypeNode>(enumVariantTy->clone());
            expressionTypes[node] = enumVariantTy;
            return;
        }

        // Vec::new() - create vector
        if (typeName == "Vec" && methodName == "new") {
            // Don't try to resolve the type of Vec as a value
            // The actual typing will be handled in CallExpression visitor
            return;
        }

        // Primitive type static methods: Int::from_string(), Float::from_string(), etc.
        if (methodName == "from_string" &&
            (typeName == "Int" || typeName == "Float" || typeName == "Bool" || typeName == "String")) {
            // Static method call for type conversion
            // The actual typing will be handled in CallExpression visitor
            return;
        }

        // String::from_bytes() - static factory for String from raw bytes
        if (typeName == "String" && methodName == "from_bytes") {
            return;
        }

        // Complex type static methods: Struct::from_string() for JSON deserialization
        // Check if this is a user-defined struct
        auto structIt = structFieldTypes.find(typeName);
        if (structIt != structFieldTypes.end() && methodName == "from_string") {
            // User-defined struct with from_string() static method
            return;
        }
    }

    // Now get the object's type from expressionTypes map
    auto it = expressionTypes.find(node->object.get());
    if (it == expressionTypes.end() || !it->second) {
        addError("Cannot determine type of object in member expression.", node);
        return;
    }

    // Get the full type string (e.g., "Box<Int>") not just the base name
    std::string structTypeName = it->second->toString();
    const std::string& fieldName = propertyId->name;

    VYB_CDBG << "DEBUG: MemberExpression type check: structTypeName=" << structTypeName
              << ", fieldName=" << fieldName
              << ", typeNodeKind=" << typeid(*it->second).name() << std::endl;

    // Handle mild<T>.grab() and mild<T>.released() intrinsic methods
    if (structTypeName.find("mild<") == 0) {
        if (fieldName == "grab" || fieldName == "released") {
            VYB_CDBG << "DEBUG: Recognized mild<T>." << fieldName << "() method" << std::endl;
            // These are intrinsic methods on mild<T>, allow them
            // Type will be set in CallExpression visitor
            return;
        }
    }

    // Check if the object's TYPE is a type parameter with bounds
    // This handles both direct variable access (clonedValue.show()) and chained access (self.value.show())
    if (auto objTypeName = dynamic_cast<ast::TypeName*>(it->second)) {
        if (objTypeName->identifier) {
            std::string objTypeStr = objTypeName->identifier->name;
            // Check if this type is a type parameter
            SymbolInfo* typeParamSym = currentScope->lookup(objTypeStr);
            if (typeParamSym && typeParamSym->kind == SymbolInfo::Kind::TYPE_PARAMETER) {
                // This object's type is a type parameter - check its bounds for the method
                for (const std::string& boundName : typeParamSym->bounds) {
                    const TraitInfo* declaringAspect = nullptr;
                    const TraitMethod* method = nullptr;
                    if (findAspectMethod(boundName, fieldName, declaringAspect, method)) {
                        VYB_CDBG << "DEBUG: Object of type parameter " << objTypeStr
                                  << " with bound " << boundName
                                  << " allows method " << fieldName << std::endl;
                        // Found the method in one of the bounds - allow it
                        return;
                    }
                }
                // If we get here, no bound provides this method
                addError("Type parameter '" + objTypeStr + "' does not have bound that provides method '" + fieldName + "'", node);
                return;
            }
        }
    }

    // Also check if the object is a variable whose type is a type parameter (for additional safety)
    if (auto objIdent = dynamic_cast<ast::Identifier*>(node->object.get())) {
        SymbolInfo* varSym = currentScope->lookup(objIdent->name);
        if (varSym && varSym->type) {
            if (auto varTypeName = dynamic_cast<ast::TypeName*>(varSym->type)) {
                if (varTypeName->identifier) {
                    std::string varTypeStr = varTypeName->identifier->name;
                    // Check if this type is a type parameter
                    SymbolInfo* typeParamSym = currentScope->lookup(varTypeStr);
                    if (typeParamSym && typeParamSym->kind == SymbolInfo::Kind::TYPE_PARAMETER) {
                        // This variable's type is a type parameter - check its bounds for the method
                        for (const std::string& boundName : typeParamSym->bounds) {
                            const TraitInfo* declaringAspect = nullptr;
                            const TraitMethod* method = nullptr;
                            if (findAspectMethod(boundName, fieldName, declaringAspect, method)) {
                                VYB_CDBG << "DEBUG: Variable " << objIdent->name
                                          << " has type parameter " << varTypeStr
                                          << " with bound " << boundName
                                          << " allowing method " << fieldName << std::endl;
                                // Found the method in one of the bounds - allow it
                                return;
                            }
                        }
                        // If we get here, no bound provides this method
                        addError("Type parameter '" + varTypeStr + "' does not have bound that provides method '" + fieldName + "'", node);
                        return;
                    }
                }
            }
        }
    }

    // Also check if the type name itself is a type parameter (for direct type parameter method calls)
    SymbolInfo* typeParamSym = currentScope->lookup(structTypeName);
    if (typeParamSym && typeParamSym->kind == SymbolInfo::Kind::TYPE_PARAMETER) {
        // This is a type parameter - check its bounds for the method
        for (const std::string& boundName : typeParamSym->bounds) {
            const TraitInfo* declaringAspect = nullptr;
            const TraitMethod* method = nullptr;
            if (findAspectMethod(boundName, fieldName, declaringAspect, method)) {
                VYB_CDBG << "DEBUG: Type parameter " << structTypeName
                          << " with bound " << boundName
                          << " allows method " << fieldName << std::endl;
                // Found the method in one of the bounds - allow it
                return;
            }
        }
        // If we get here, no bound provides this method
        addError("Type parameter '" + structTypeName + "' does not have bound that provides method '" + fieldName + "'", node);
        return;
    }

    // Before checking struct fields, check if this might be a trait method call
    // (MemberExpression can be part of CallExpression, where callee is the MemberExpression)

    // First check concrete trait impls
    auto typeImplsIt = traitImpls.find(structTypeName);
    if (typeImplsIt != traitImpls.end()) {
        for (const auto& traitEntry : typeImplsIt->second) {
            const std::vector<ast::FunctionDeclaration*>& methods = traitEntry.second;
            for (ast::FunctionDeclaration* method : methods) {
                if (method && method->id && method->id->name == fieldName) {
                    // This is a trait method, not a field
                    // Don't set type here - let CallExpression handle it
                    VYB_CDBG << "DEBUG: MemberExpression identified trait method: "
                              << structTypeName << "." << fieldName << std::endl;
                    return;
                }
            }
        }
    }

    // Also check generic trait impls - check if structTypeName matches any pattern
    for (const auto& typeEntry : genericTraitImpls) {
        const std::string& pattern = typeEntry.first; // e.g., "Box<T>"

        // Simple pattern matching: check if structTypeName matches pattern
        // Box<Int> should match Box<T>
        if (matchesPattern(structTypeName, pattern)) {
            for (const auto& traitEntry : typeEntry.second) {
                const GenericImplInfo* implInfo = traitEntry.second.get();
                if (implInfo && implInfo->declaration) {
                    for (const auto& method : implInfo->declaration->methods) {
                        if (method && method->id && method->id->name == fieldName) {
                            // This is a generic trait method
                            return;
                        }
                    }

                    // Not in impl - check if aspect has default implementation
                    const std::string& traitName = traitEntry.first;
                    auto traitIt = traitRegistry.find(traitName);
                    if (traitIt != traitRegistry.end()) {
                        for (const auto& traitMethod : traitIt->second->methods) {
                            if (traitMethod.name == fieldName && traitMethod.hasDefaultImpl) {
                                // Found default implementation in aspect
                                return;
                            }
                        }
                    }
                }
            }
        }
    }

    // Check concrete trait impls for default methods
    auto concreteImplsIt = traitImpls.find(structTypeName);
    if (concreteImplsIt != traitImpls.end()) {
        for (const auto& traitEntry : concreteImplsIt->second) {
            const std::string& traitName = traitEntry.first;
            const std::vector<ast::FunctionDeclaration*>& methods = traitEntry.second;

            // Check if method is in impl
            bool foundInImpl = false;
            for (ast::FunctionDeclaration* method : methods) {
                if (method && method->id && method->id->name == fieldName) {
                    foundInImpl = true;
                    break;
                }
            }

            // If not in impl, check aspect for default implementation
            if (!foundInImpl) {
                auto traitIt = traitRegistry.find(traitName);
                if (traitIt != traitRegistry.end()) {
                    for (const auto& traitMethod : traitIt->second->methods) {
                        if (traitMethod.name == fieldName && traitMethod.hasDefaultImpl) {
                            // Found default implementation in aspect
                            return;
                        }
                    }
                }
            }
        }
    }

    // Check if this is a tuple type - tuples only support len() method
    if (auto tupleType = dynamic_cast<ast::TupleTypeNode*>(it->second)) {
        if (fieldName == "len") {
            // This is handled in CallExpression visitor, just allow it here
            return;
        } else {
            addError("Tuple type only supports len() method, not '" + fieldName + "'", node);
            return;
        }
    }

    // Extract base struct name for field lookup (Box<Int> -> Box)
    // Special handling for ownership wrappers: their<Counter> -> Counter
    std::string baseStructName = structTypeName;
    size_t anglePos = structTypeName.find('<');
    if (anglePos != std::string::npos) {
        baseStructName = structTypeName.substr(0, anglePos);

        // Check if this is an ownership keyword (their, my, view, our, etc.)
        if (baseStructName == "their" || baseStructName == "my" || baseStructName == "view" ||
            baseStructName == "our" || baseStructName == "borrow") {
            // Extract the inner type: their<Counter> -> Counter
            size_t closeAnglePos = structTypeName.find('>');
            if (closeAnglePos != std::string::npos && closeAnglePos > anglePos + 1) {
                std::string innerType = structTypeName.substr(anglePos + 1, closeAnglePos - anglePos - 1);
                // Recursively extract base name from inner type (handles their<Box<Int>> -> Box)
                size_t innerAnglePos = innerType.find('<');
                if (innerAnglePos != std::string::npos) {
                    baseStructName = innerType.substr(0, innerAnglePos);
                } else {
                    baseStructName = innerType;
                }
                // Update structTypeName to be the inner type for subsequent operations
                structTypeName = innerType;
            }
        }
    }

    // `.tag` accessor: an enum value exposes its raw positional i64 tag as an Int.
    // This works for both C-like scalar enums and data-carrying enums.
    if (fieldName == "tag" && enumTypeNames.count(baseStructName)) {
        auto* tagTy = new ast::TypeName(node->loc,
            std::make_unique<ast::Identifier>(node->loc, "Int"));
        expressionTypes[node] = tagTy;
        node->type = std::shared_ptr<ast::TypeNode>(tagTy->clone());
        return;
    }

    // Special handling for built-in types with methods (Vec, Future, etc.)
    // These are not user-defined structs, so they won't be in structFieldTypes
    // Their methods are handled in CallExpression visitor
    if (baseStructName == "Vec" || baseStructName == "Future") {
        // This is a built-in type - don't check struct fields
        // The actual method resolution will happen in CallExpression visitor
        return;
    }

    // Special handling for primitive types with methods (Int.to_string(), etc.)
    if (baseStructName == "Int" || baseStructName == "Float" || baseStructName == "Bool" || baseStructName == "String") {
        // Primitive types can have methods like to_string()
        // The actual method resolution will happen in CallExpression visitor
        return;
    }

    // Check if this is a user-defined struct type accessing a method (not a field)
    // We need to allow method calls on structs (like .to_string()) to pass through to CallExpression
    // So if the member name is a known method, don't treat it as a field access
    auto structIt = structFieldTypes.find(baseStructName);
    if (structIt != structFieldTypes.end()) {
        // This is a known struct type
        // Check if propertyId is a method name (to_string, from_string, etc.)
        if (propertyId->name == "to_string") {
            // This is a method call, not a field access
            // Let CallExpression visitor handle it
            return;
        }
    }

    // Check if we have field information for this struct
    if (structIt == structFieldTypes.end()) {
        addError("Unknown struct type: " + baseStructName, node);
        return;
    }

    // Look up the specific field
    auto& fieldMap = structIt->second;
    auto fieldIt = fieldMap.find(fieldName);
    if (fieldIt == fieldMap.end()) {
        addError("Field '" + fieldName + "' not found in struct '" + structTypeName + "'", node);
        return;
    }

    // Set the type of the member expression to the field's type
    ast::TypeNode* fieldType = fieldIt->second;
    expressionTypes[node] = fieldType;
    node->type = std::shared_ptr<ast::TypeNode>(fieldType->clone());

    VYB_CDBG << "DEBUG: Resolved member access " << structTypeName << "." << fieldName
              << " to type: " << fieldType->toString() << std::endl;
}
void SemanticAnalyzer::visit(ast::AssignmentExpression* node) {
    if (!node || !node->left || !node->right) {
        // Should not happen with a valid AST
        addError("Malformed assignment expression.", node);
        return;
    }

    // Special handling: If LHS is a pointer dereference, temporarily set its type to the pointer type for assignment
    ast::PointerDerefExpression* derefLHS = dynamic_cast<ast::PointerDerefExpression*>(node->left.get());
    ast::TypeNode* savedDerefType = nullptr;
    if (derefLHS) {
        // Analyze the pointer expression to get its type
        derefLHS->pointer->accept(*this);
        auto ptrTypeIt = expressionTypes.find(derefLHS->pointer.get());
        if (ptrTypeIt != expressionTypes.end() && ptrTypeIt->second) {
            // Save the original type (pointee type)
            auto origTypeIt = expressionTypes.find(derefLHS);
            if (origTypeIt != expressionTypes.end()) {
                savedDerefType = origTypeIt->second;
            }
            // Set the deref node's type to the pointer type for assignment compatibility
            expressionTypes[derefLHS] = ptrTypeIt->second;
            derefLHS->type = std::shared_ptr<ast::TypeNode>(ptrTypeIt->second->clone());
        }
    }

    node->left->accept(*this);
    node->right->accept(*this);

    // Restore the original type for derefLHS after assignment analysis
    if (derefLHS) {
        if (savedDerefType) {
            expressionTypes[derefLHS] = savedDerefType;
            derefLHS->type = std::shared_ptr<ast::TypeNode>(savedDerefType->clone());
        }
    }

    if (!isLValue(node->left.get())) {
        addError("LHS of assignment is not a valid L-value.", node->left.get());
        expressionTypes[node] = nullptr; // Mark as error
        return;
    }

    std::string assignedRoot = borrowedRootName(node->left.get());
    if (!assignedRoot.empty() && hasActiveBorrow(assignedRoot)) {
        addError("Cannot assign to '" + assignedRoot + "' while it has an active borrow.", node->left.get());
        expressionTypes[node] = nullptr;
        return;
    }

    auto leftTypeIt = expressionTypes.find(node->left.get());
    auto rightTypeIt = expressionTypes.find(node->right.get());

    ast::TypeNode* leftType = (leftTypeIt != expressionTypes.end()) ? leftTypeIt->second : nullptr;
    ast::TypeNode* rightType = (rightTypeIt != expressionTypes.end()) ? rightTypeIt->second : nullptr;

    VYB_CDBG << "DEBUG: Assignment type check - LHS type: "
              << (leftType ? leftType->toString() : "null")
              << ", RHS type: "
              << (rightType ? rightType->toString() : "null") << std::endl;

    if (!leftType || !rightType) {
        addError("Type error in assignment: could not determine type of LHS or RHS.", node);
        expressionTypes[node] = nullptr; // Mark as error
        return;
    }

    // Check if the types are compatible for assignment
    if (!areTypesCompatible(leftType, rightType)) {
        addError("Type error in assignment: incompatible types - cannot assign '" + rightType->toString() +
                "' to '" + leftType->toString() + "'.", node);
        // Continue processing to avoid cascading errors, but mark this node as erroneous
        expressionTypes[node] = leftType; // Still use left type for further analysis
    } else {
        // The type of the assignment expression is typically the type of the LHS (or RHS after conversion).
        expressionTypes[node] = leftType; // Or rightType, depending on language rules for assignment expr type
    }

    if (leftType) {
      node->type = std::shared_ptr<ast::TypeNode>(leftType->clone());
    }

    // Move tracking: if RHS is a MY-owned identifier being assigned to LHS, mark RHS as moved
    if (auto* rhsIdent = dynamic_cast<ast::Identifier*>(node->right.get())) {
        if (auto* lhsIdent = dynamic_cast<ast::Identifier*>(node->left.get())) {
            SymbolInfo* rhsSymbol = currentScope->lookup(rhsIdent->name);
            if (rhsSymbol && hasOwnershipKindMY(rhsSymbol)) {
                recordMove(rhsIdent->name);
            }
        }
    }
}

void SemanticAnalyzer::visit(ast::LogicalExpression* node) {
    // Check left and right operands
    if (node->left) {
        node->left->accept(*this);
    } else {
        addError("Logical expression missing left operand.", node);
        expressionTypes[node] = nullptr;
        return;
    }

    if (node->right) {
        node->right->accept(*this);
    } else {
        addError("Logical expression missing right operand.", node);
        expressionTypes[node] = nullptr;
        return;
    }

    // Get types of operands
    auto leftTypeIt = expressionTypes.find(node->left.get());
    auto rightTypeIt = expressionTypes.find(node->right.get());

    if (leftTypeIt == expressionTypes.end() || rightTypeIt == expressionTypes.end() ||
        !leftTypeIt->second || !rightTypeIt->second) {
        addError("Cannot determine types for logical expression operands.", node);
        expressionTypes[node] = nullptr;
        return;
    }

    // Check both operands are boolean or convertible to boolean
    // For now we'll do a simple check for boolean type
    bool isLeftBoolean = false;
    bool isRightBoolean = false;

    if (auto* typeName = dynamic_cast<ast::TypeName*>(leftTypeIt->second)) {
        isLeftBoolean = typeName->identifier && typeName->identifier->name == "bool";
    }

    if (auto* typeName = dynamic_cast<ast::TypeName*>(rightTypeIt->second)) {
        isRightBoolean = typeName->identifier && typeName->identifier->name == "bool";
    }

    if (!isLeftBoolean) {
        addError("Left operand of logical expression must be boolean.", node->left.get());
    }

    if (!isRightBoolean) {
        addError("Right operand of logical expression must be boolean.", node->right.get());
    }

    // Result type is always boolean
    // Create a boolean TypeName
    auto identifier = std::make_unique<ast::Identifier>(node->loc, "bool");
    auto boolType = std::make_unique<ast::TypeName>(node->loc, std::move(identifier));
    expressionTypes[node] = boolType.get();
    node->type = std::move(boolType);
}

void SemanticAnalyzer::visit(ast::ConditionalExpression* node) {
    // Check condition
    if (node->condition) {
        node->condition->accept(*this);
    } else {
        addError("Conditional expression missing condition.", node);
        expressionTypes[node] = nullptr;
        return;
    }

    // Check condition is boolean
    auto conditionTypeIt = expressionTypes.find(node->condition.get());
    if (conditionTypeIt == expressionTypes.end() || !conditionTypeIt->second) {
        addError("Cannot determine type for conditional expression condition.", node);
        expressionTypes[node] = nullptr;
        return;
    }

    bool isConditionBoolean = false;
    if (auto* typeName = dynamic_cast<ast::TypeName*>(conditionTypeIt->second)) {
        isConditionBoolean = typeName->identifier && typeName->identifier->name == "bool";
    }

    if (!isConditionBoolean) {
        addError("Condition of conditional expression must be boolean.", node->condition.get());
    }

    // Check then branch
    if (node->thenExpr) {
        node->thenExpr->accept(*this);
    } else {
        addError("Conditional expression missing 'then' branch.", node);
        expressionTypes[node] = nullptr;
        return;
    }

    // Check else branch
    if (node->elseExpr) {
        node->elseExpr->accept(*this);
    } else {
        addError("Conditional expression missing 'else' branch.", node);
        expressionTypes[node] = nullptr;
        return;
    }

    // Get types of branches
    auto thenTypeIt = expressionTypes.find(node->thenExpr.get());
    auto elseTypeIt = expressionTypes.find(node->elseExpr.get());

    if (thenTypeIt == expressionTypes.end() || elseTypeIt == expressionTypes.end() ||
        !thenTypeIt->second || !elseTypeIt->second) {
        addError("Cannot determine types for conditional expression branches.", node);
        expressionTypes[node] = nullptr;
        return;
    }

    // Check if branch types are compatible
    if (!areTypesCompatible(thenTypeIt->second, elseTypeIt->second) &&
        !areTypesCompatible(elseTypeIt->second, thenTypeIt->second)) {
        addError("Incompatible types in conditional expression: '" +
                 thenTypeIt->second->toString() + "' and '" +
                 elseTypeIt->second->toString() + "'.", node);
        // Use then branch type to continue analysis
        expressionTypes[node] = thenTypeIt->second;
        if (thenTypeIt->second) {
            node->type = std::shared_ptr<ast::TypeNode>(thenTypeIt->second->clone());
        }
        return;
    }

    // The result type is the common type of both branches
    // For now, use the 'then' branch type as the result type
    expressionTypes[node] = thenTypeIt->second;
    if (thenTypeIt->second) {
        node->type = std::shared_ptr<ast::TypeNode>(thenTypeIt->second->clone());
    }
}

void SemanticAnalyzer::visit(ast::SequenceExpression* node) {
    // Process each expression in the sequence
    if (node->expressions.empty()) {
        // Empty tuple - create empty TupleTypeNode
        auto emptyTupleType = std::make_unique<ast::TupleTypeNode>(node->loc, std::vector<ast::TypeNodePtr>{});
        expressionTypes[node] = emptyTupleType.get();
        node->type = std::shared_ptr<ast::TypeNode>(emptyTupleType.release());
        return;
    }

    // Visit all expressions and collect their types
    std::vector<ast::TypeNodePtr> elementTypes;
    for (auto& expr : node->expressions) {
        if (expr) {
            expr->accept(*this);
            auto exprTypeIt = expressionTypes.find(expr.get());
            if (exprTypeIt != expressionTypes.end() && exprTypeIt->second) {
                elementTypes.push_back(exprTypeIt->second->clone());
            } else {
                addError("Cannot determine type of expression in tuple literal.", node);
                expressionTypes[node] = nullptr;
                return;
            }
        } else {
            addError("Null expression in sequence.", node);
            expressionTypes[node] = nullptr;
            return;
        }
    }

    // Create a TupleTypeNode with all element types
    auto tupleType = std::make_unique<ast::TupleTypeNode>(node->loc, std::move(elementTypes));
    expressionTypes[node] = tupleType.get();
    node->type = std::shared_ptr<ast::TypeNode>(tupleType.release());
}
void SemanticAnalyzer::visit(ast::ObjectLiteral* node) {
    // Try to determine the type from the typePath field (e.g., Point in Point { ... })
    if (!node || !node->typePath) {
        addError("Object literal missing type path.", node);
        expressionTypes[node] = nullptr;
        return;
    }

    // Get the struct name (base name without generic args)
    auto typeName = dynamic_cast<ast::TypeName*>(node->typePath.get());
    if (!typeName || !typeName->identifier) {
        addError("Object literal has invalid type path.", node);
        expressionTypes[node] = node->typePath->clone().release();
        return;
    }

    std::string structName = typeName->identifier->name;

    // Visit all field values to determine their types
    for (auto& prop : node->properties) {
        if (prop.value) {
            prop.value->accept(*this);
        }
    }

    // Check if this struct has generic parameters that need to be inferred
    auto structFieldsIt = structFieldTypes.find(structName);
    if (structFieldsIt == structFieldTypes.end()) {
        // No field information. If this is an error type literal (used in fail statement),
        // register it with the fields from the literal so member access works in trap clauses.
        std::map<std::string, ast::TypeNode*> implicitFields;
        for (auto& prop : node->properties) {
            if (prop.key && prop.value) {
                auto valueTypeIt = expressionTypes.find(prop.value.get());
                if (valueTypeIt != expressionTypes.end() && valueTypeIt->second) {
                    implicitFields[prop.key->name] = valueTypeIt->second;
                }
            }
        }
        if (!implicitFields.empty()) {
            structFieldTypes[structName] = implicitFields;
        }
        expressionTypes[node] = node->typePath->clone().release();
        return;
    }

    const auto& fieldTypes = structFieldsIt->second;

    auto genericOrderIt = structGenericParamOrder.find(structName);
    const std::vector<std::string>* genericParamOrder =
        genericOrderIt == structGenericParamOrder.end() ? nullptr : &genericOrderIt->second;

    if (!typeName->genericArgs.empty() && genericParamOrder &&
        typeName->genericArgs.size() == genericParamOrder->size()) {
        std::map<std::string, ast::TypeNode*> explicitTypeArgs;
        for (size_t i = 0; i < genericParamOrder->size(); ++i) {
            explicitTypeArgs[(*genericParamOrder)[i]] = typeName->genericArgs[i].get();
        }

        for (const auto& prop : node->properties) {
            if (!prop.key || !prop.value) continue;
            auto fieldTypeIt = fieldTypes.find(prop.key->name);
            if (fieldTypeIt == fieldTypes.end()) {
                addError("Field '" + prop.key->name + "' does not exist in struct '" + structName + "'", node);
                continue;
            }
            auto valueTypeIt = expressionTypes.find(prop.value.get());
            if (valueTypeIt == expressionTypes.end() || !valueTypeIt->second) continue;

            auto expectedFieldType = substituteGenericArgsForValidation(fieldTypeIt->second, explicitTypeArgs);
            if (expectedFieldType && !areTypesCompatible(expectedFieldType.get(), valueTypeIt->second)) {
                addError("Field '" + prop.key->name + "' initializer type does not match explicit generic argument. Expected " +
                         expectedFieldType->toString() + " but got " + valueTypeIt->second->toString(), node);
            }
        }

        expressionTypes[node] = node->typePath->clone().release();
        node->type = std::shared_ptr<ast::TypeNode>(node->typePath->clone());
        return;
    }

    // Map type parameters to their inferred types
    // E.g., for Box<T> with field "value: T", if value=Point, then T → Point
    std::map<std::string, ast::TypeNode*> typeParamMap;

    for (const auto& prop : node->properties) {
        if (!prop.key || !prop.value) continue;

        std::string fieldName = prop.key->name;

        // Look up the field's declared type in the struct
        auto fieldTypeIt = fieldTypes.find(fieldName);
        if (fieldTypeIt == fieldTypes.end()) {
            addError("Field '" + fieldName + "' does not exist in struct '" + structName + "'", node);
            continue;
        }

        ast::TypeNode* declaredFieldType = fieldTypeIt->second;

        // Get the actual type of the value
        auto valueTypeIt = expressionTypes.find(prop.value.get());
        if (valueTypeIt == expressionTypes.end() || !valueTypeIt->second) {
            continue; // Can't infer if value type unknown
        }

        ast::TypeNode* actualValueType = valueTypeIt->second;

        // Check if the declared field type is a type parameter
        if (auto declaredTypeName = dynamic_cast<ast::TypeName*>(declaredFieldType)) {
            if (declaredTypeName->identifier && declaredTypeName->genericArgs.empty()) {
                std::string declaredTypeStr = declaredTypeName->identifier->name;

                // Check if this is a type parameter by looking for it as a primitive type
                // Type parameters are NOT primitive types (Int, Float, String, Bool, etc.)
                bool isPrimitive = (declaredTypeStr == "Int" || declaredTypeStr == "Float" ||
                                   declaredTypeStr == "String" || declaredTypeStr == "Bool" ||
                                   declaredTypeStr == "Void" ||
                                   declaredTypeStr == "CChar" || declaredTypeStr == "CUChar" ||
                                   declaredTypeStr == "CShort" || declaredTypeStr == "CUShort" ||
                                   declaredTypeStr == "CInt" || declaredTypeStr == "CUInt" ||
                                   declaredTypeStr == "CLong" || declaredTypeStr == "CULong" ||
                                   declaredTypeStr == "CSize" || declaredTypeStr == "CSSize" ||
                                   declaredTypeStr == "CFloat" || declaredTypeStr == "CDouble" ||
                                   declaredTypeStr == "CVoid" || declaredTypeStr == "CString");

                if (!isPrimitive) {
                    // Check if it's a known struct/type by looking it up
                    SymbolInfo* sym = currentScope->lookup(declaredTypeStr);
                    bool isKnownType = (sym && sym->kind == SymbolInfo::Kind::Type);

                    if (!isKnownType) {
                        // Not a primitive and not a known type - likely a type parameter
                        typeParamMap[declaredTypeStr] = actualValueType;
                    }
                }
            }
        }
    }

    // Build the final type with inferred generic arguments
    if (!typeParamMap.empty()) {
        // Create TypeName with generic arguments
        std::vector<ast::TypeNodePtr> genericArgs;

        if (genericParamOrder && !genericParamOrder->empty()) {
            for (const auto& paramName : *genericParamOrder) {
                auto inferredIt = typeParamMap.find(paramName);
                if (inferredIt != typeParamMap.end() && inferredIt->second) {
                    genericArgs.push_back(std::unique_ptr<ast::TypeNode>(inferredIt->second->clone()));
                }
            }
        } else {
            for (const auto& entry : typeParamMap) {
                genericArgs.push_back(std::unique_ptr<ast::TypeNode>(entry.second->clone()));
            }
        }

        auto resultType = new ast::TypeName(
            typeName->loc,
            std::make_unique<ast::Identifier>(typeName->loc, structName),
            std::move(genericArgs)
        );

        expressionTypes[node] = resultType;
        node->type = std::shared_ptr<ast::TypeNode>(resultType->clone());
    } else {
        // No generic parameters or couldn't infer - use type as-is
        expressionTypes[node] = node->typePath->clone().release();
        node->type = std::shared_ptr<ast::TypeNode>(node->typePath->clone());
    }
}
void SemanticAnalyzer::visit(ast::ArrayLiteral* node) {
    if (!node) {
        return;
    }

    // Process all elements to infer types
    ast::TypeNode* elementType = nullptr;
    for (auto& element : node->elements) {
        if (element) {
            element->accept(*this);

            // Get the element type
            auto elemTypeIt = expressionTypes.find(element.get());
            if (elemTypeIt != expressionTypes.end() && elemTypeIt->second) {
                if (!elementType) {
                    // First element sets the type
                    elementType = elemTypeIt->second;
                } else {
                    // TODO: Type compatibility check for all elements
                    // For now, assume all elements have the same type
                }
            }
        }
    }

    if (elementType) {
        // Create an array type [ElementType; Size]
        auto sizeExpr = std::make_unique<ast::IntegerLiteral>(
            node->loc,
            static_cast<int64_t>(node->elements.size())
        );

        auto arrayType = std::make_unique<ast::ArrayType>(
            node->loc,
            elementType->clone(),
            std::move(sizeExpr)
        );

        expressionTypes[node] = arrayType.release();
        node->type = std::shared_ptr<ast::TypeNode>(expressionTypes[node]->clone());
    }
}
void SemanticAnalyzer::visit(ast::FunctionExpression* node) {
    // Collect parameter types for the FunctionType
    std::vector<ast::TypeNodePtr> paramTypes;
    for (const auto& param : node->params) {
        if (param.typeNode) {
            paramTypes.push_back(param.typeNode->clone());
        } else {
            // Unknown parameter type — use a generic placeholder
            paramTypes.push_back(std::make_unique<ast::TypeName>(
                node->loc, std::make_unique<ast::Identifier>(node->loc, "?")));
        }
    }

    // Enter a new scope for the lambda body
    enterScope();

    // Register each parameter in the lambda's scope
    for (const auto& param : node->params) {
        if (param.name) {
            ast::TypeNode* paramTypeRaw = param.typeNode ? param.typeNode.get() : nullptr;
            currentScope->add(SymbolInfo{
                SymbolInfo::Kind::Variable,
                param.name->name,
                /*isConst=*/false,
                ast::OwnershipKind::MY,
                paramTypeRaw ? paramTypeRaw->clone().release() : nullptr
            });
        }
    }

    // Visit body to detect captures and validate inner expressions
    if (node->body) {
        node->body->accept(*this);
    }

    exitScope();

    // Build a FunctionType for this lambda so variable declarations can infer its type.
    // Use the same raw-pointer ownership convention as the rest of expressionTypes.
    auto* funcType = new ast::FunctionType(node->loc, std::move(paramTypes), /*returnType=*/nullptr);
    expressionTypes[node] = funcType;
    node->type = std::shared_ptr<ast::TypeNode>(funcType->clone());
}
void SemanticAnalyzer::visit(ast::ThisExpression* node) {}
void SemanticAnalyzer::visit(ast::SuperExpression* node) {}
void SemanticAnalyzer::visit(ast::AwaitExpression* node) {}

void SemanticAnalyzer::visit(ast::RangeExpression* node) {
    if (node->start) {
        node->start->accept(*this);
    }
    if (node->end) {
        node->end->accept(*this);
    }
    if (node->step) {
        node->step->accept(*this);
    }
}
void SemanticAnalyzer::visit(ast::BlockExpression* node) {
    if (node->block) {
        node->block->accept(*this);
    }

    // Process trap clauses attached to this block
    for (auto& trapClause : node->trapClauses) {
        if (trapClause) {
            trapClause->accept(*this);
        }
    }

    // Process ensure clause if present
    if (node->ensureClause) {
        node->ensureClause->accept(*this);
    }

    // A block used as a value (e.g. `s<String> = { risky() } trap (e<?>) -> { "hi" }`)
    // gets its result type from the last trap handler's yielded expression, mirroring
    // MatchExpression/SelectExpression resultType inference. Without this the block
    // resolves to the enclosing body's last expression type (e.g. i64), which makes
    // aggregates like String get flagged as "expected String but got i64".
    ast::TypeNode* yielded = nullptr;
    if (!node->trapClauses.empty()) {
        auto& lastClause = node->trapClauses.back();
        if (lastClause && lastClause->handler) {
            if (auto* handlerBlock = dynamic_cast<ast::BlockStatement*>(lastClause->handler.get())) {
                for (auto it = handlerBlock->body.rbegin(); it != handlerBlock->body.rend(); ++it) {
                    if (auto* exprStmt = dynamic_cast<ast::ExpressionStatement*>(it->get())) {
                        if (exprStmt->expression) {
                            auto eIt = expressionTypes.find(exprStmt->expression.get());
                            if (eIt != expressionTypes.end() && eIt->second) yielded = eIt->second;
                        }
                        break;
                    }
                }
            }
        }
    }
    if (!yielded && node->block && !node->block->body.empty()) {
        for (auto it = node->block->body.rbegin(); it != node->block->body.rend(); ++it) {
            if (auto* exprStmt = dynamic_cast<ast::ExpressionStatement*>(it->get())) {
                if (exprStmt->expression) {
                    auto eIt = expressionTypes.find(exprStmt->expression.get());
                    if (eIt != expressionTypes.end() && eIt->second) yielded = eIt->second;
                }
                break;
            }
        }
    }
    // A void handler/body (e.g. an error-catch print) means the block is not used
    // as a value, so don't stamp a void result type on it.
    if (yielded && yielded->toString() != "Void") {
        node->type = std::shared_ptr<ast::TypeNode>(yielded->clone());
        expressionTypes[node] = node->type.get();
    }
}
void SemanticAnalyzer::visit(ast::SelectExpression* node) {
    // Visit the expression being matched
    if (node->expr) {
        node->expr->accept(*this);
    }

    // Best-effort static type of the select target, used to bind enum variant
    // payload fields and to check exhaustiveness on tagged-union enums.
    std::string selectTypeStr;
    if (node->expr) {
        if (auto it = expressionTypes.find(node->expr.get()); it != expressionTypes.end() && it->second) {
            selectTypeStr = it->second->toString();
        } else if (node->expr->type) {
            selectTypeStr = node->expr->type->toString();
        }
    }

    // Track comparison patterns for unreachable detection
    struct ComparisonInfo {
        vyb::TokenType op;
        int64_t value;
        size_t caseIndex;
    };
    std::vector<ComparisonInfo> comparisons;
    size_t wildcardIndex = SIZE_MAX;

    // Visit all patterns and check for unreachable patterns
    for (size_t i = 0; i < node->cases.size(); i++) {
        const auto& [pattern, result] = node->cases[i];

        // Each arm gets its own scope so enum-variant payload bindings from a
        // `Circle(r)` pattern are scoped to the matching arm only.
        enterScope();

        if (!pattern) {
            // Wildcard pattern (nullptr)
            if (wildcardIndex == SIZE_MAX) {
                wildcardIndex = i;
            } else {
                // Can't get loc from nullptr, use result as node
                addError("Wildcard pattern already used in case " +
                        std::to_string(wildcardIndex + 1) + ", this pattern is unreachable", result.get());
            }
        } else if (auto* comp = dynamic_cast<ast::ComparisonPattern*>(pattern.get())) {
            // Check if this pattern comes after a wildcard
            if (wildcardIndex != SIZE_MAX) {
                addError("Pattern after wildcard in case " +
                        std::to_string(wildcardIndex + 1) + " is unreachable", pattern.get());
            }

            // Try to extract constant value for unreachable detection
            if (auto* intLit = dynamic_cast<ast::IntegerLiteral*>(comp->value.get())) {
                int64_t value = intLit->value;
                vyb::TokenType op = comp->op.type;

                // Check against all previous comparison patterns
                for (const auto& prev : comparisons) {
                    bool isUnreachable = false;
                    std::string reason;

                    // Check for duplicate patterns
                    if (prev.op == op && prev.value == value) {
                        isUnreachable = true;
                        reason = "duplicate pattern";
                    }
                    // Check for subsumption: >= 80 subsumes >= 90
                    else if (prev.op == vyb::TokenType::GTEQ && op == vyb::TokenType::GTEQ && prev.value <= value) {
                        isUnreachable = true;
                        reason = "subsumed by earlier '>= " + std::to_string(prev.value) + "'";
                    }
                    // Check for subsumption: <= 80 subsumes <= 70
                    else if (prev.op == vyb::TokenType::LTEQ && op == vyb::TokenType::LTEQ && prev.value >= value) {
                        isUnreachable = true;
                        reason = "subsumed by earlier '<= " + std::to_string(prev.value) + "'";
                    }
                    // Check for subsumption: >= 80 subsumes > 80
                    else if (prev.op == vyb::TokenType::GTEQ && op == vyb::TokenType::GT && prev.value <= value) {
                        isUnreachable = true;
                        reason = "subsumed by earlier '>= " + std::to_string(prev.value) + "'";
                    }
                    // Check for subsumption: <= 80 subsumes < 80
                    else if (prev.op == vyb::TokenType::LTEQ && op == vyb::TokenType::LT && prev.value >= value) {
                        isUnreachable = true;
                        reason = "subsumed by earlier '<= " + std::to_string(prev.value) + "'";
                    }
                    // Check for == within >= range: >= 70 subsumes == 75
                    else if (prev.op == vyb::TokenType::GTEQ && op == vyb::TokenType::EQEQ && value >= prev.value) {
                        isUnreachable = true;
                        reason = "covered by earlier '>= " + std::to_string(prev.value) + "'";
                    }
                    // Check for == within <= range: <= 80 subsumes == 75
                    else if (prev.op == vyb::TokenType::LTEQ && op == vyb::TokenType::EQEQ && value <= prev.value) {
                        isUnreachable = true;
                        reason = "covered by earlier '<= " + std::to_string(prev.value) + "'";
                    }
                    // Check for == within > range: > 70 subsumes == 75
                    else if (prev.op == vyb::TokenType::GT && op == vyb::TokenType::EQEQ && value > prev.value) {
                        isUnreachable = true;
                        reason = "covered by earlier '> " + std::to_string(prev.value) + "'";
                    }
                    // Check for == within < range: < 80 subsumes == 75
                    else if (prev.op == vyb::TokenType::LT && op == vyb::TokenType::EQEQ && value < prev.value) {
                        isUnreachable = true;
                        reason = "covered by earlier '< " + std::to_string(prev.value) + "'";
                    }

                    if (isUnreachable) {
                        std::string opStr;
                        switch (op) {
                            case vyb::TokenType::EQEQ:  opStr = "=="; break;
                            case vyb::TokenType::NOTEQ: opStr = "!="; break;
                            case vyb::TokenType::LT:    opStr = "<"; break;
                            case vyb::TokenType::LTEQ:  opStr = "<="; break;
                            case vyb::TokenType::GT:    opStr = ">"; break;
                            case vyb::TokenType::GTEQ:  opStr = ">="; break;
                            default: opStr = "?"; break;
                        }
                        addError("Pattern '" + opStr + " " + std::to_string(value) +
                                "' in case " + std::to_string(i + 1) + " is " + reason +
                                " (case " + std::to_string(prev.caseIndex + 1) + ")", pattern.get());
                        break;
                    }
                }

                comparisons.push_back({op, value, i});
            }

            comp->accept(*this);
        } else if (auto* ctor = dynamic_cast<ast::ConstructionExpression*>(pattern.get())) {
            // Enum variant pattern with payload: `Circle(r)`, `Rect(w, h)`.
            bool handled = false;
            if (!selectTypeStr.empty()) {
                auto variantsIt = enumVariantPayloadTypes.find(selectTypeStr);
                auto* tv = ctor->constructedType
                    ? dynamic_cast<ast::TypeName*>(ctor->constructedType.get()) : nullptr;
                if (variantsIt != enumVariantPayloadTypes.end() && tv && tv->identifier) {
                    auto varIt = variantsIt->second.find(tv->identifier->name);
                    if (varIt != variantsIt->second.end()) {
                        handled = true;
                        const auto& payload = varIt->second;
                        if (payload.size() != ctor->arguments.size()) {
                            addError("Enum variant '" + tv->identifier->name + "' of " + selectTypeStr +
                                     " expects " + std::to_string(payload.size()) + " binding(s), got " +
                                     std::to_string(ctor->arguments.size()), ctor);
                        }
                        for (size_t bi = 0; bi < ctor->arguments.size(); ++bi) {
                            auto* bid = dynamic_cast<ast::Identifier*>(ctor->arguments[bi].get());
                            if (!bid) {
                                addError("Enum variant pattern bindings must be identifiers.", ctor);
                                continue;
                            }
                            const std::string& bname = bid->name;
                            if (currentScope->lookupDirect(bname)) {
                                addError("Redefinition of variable \"" + bname +
                                         "\" in variant pattern.", bid);
                                continue;
                            }
                            ast::TypeNode* pt = (bi < payload.size()) ? payload[bi].get() : nullptr;
                            currentScope->add(SymbolInfo{SymbolInfo::Kind::Variable, bname, false,
                                ast::OwnershipKind::MY, pt ? pt->clone().release() : nullptr});
                        }
                    }
                }
            }
            if (!handled) {
                ctor->accept(*this);
            }
        } else if (auto* vid = dynamic_cast<ast::Identifier*>(pattern.get())) {
            // Enum unit-variant pattern: `Unit`.
            bool handled = false;
            if (!selectTypeStr.empty()) {
                auto variantsIt = enumVariantPayloadTypes.find(selectTypeStr);
                if (variantsIt != enumVariantPayloadTypes.end() && variantsIt->second.count(vid->name)) {
                    handled = true;
                }
            }
            if (!handled) {
                vid->accept(*this);
            }
        } else {
            // Other pattern types
            pattern->accept(*this);
        }

        if (result) {
            result->accept(*this);
        }

        exitScope();
    }

    // Exhaustiveness checking for tagged-union enums: a select on a data-carrying
    // enum must cover every variant or have a wildcard, or some value could fall
    // through with no matching arm. Select arms carry no guards, so a variant
    // pattern always covers its variant.
    if (wildcardIndex == SIZE_MAX && !selectTypeStr.empty()) {
        auto variantsIt = enumVariantPayloadTypes.find(selectTypeStr);
        if (variantsIt != enumVariantPayloadTypes.end() && !variantsIt->second.empty()) {
            const auto& variants = variantsIt->second;
            std::set<std::string> covered;
            for (auto& c : node->cases) {
                if (!c.first) { covered.insert("__wildcard__"); break; }
                std::string vname;
                if (auto* ctor = dynamic_cast<ast::ConstructionExpression*>(c.first.get())) {
                    auto* tv = ctor->constructedType
                        ? dynamic_cast<ast::TypeName*>(ctor->constructedType.get()) : nullptr;
                    if (tv && tv->identifier) vname = tv->identifier->name;
                } else if (auto* idp = dynamic_cast<ast::Identifier*>(c.first.get())) {
                    vname = idp->name;
                }
                if (variants.count(vname)) covered.insert(vname);
            }
            if (!covered.count("__wildcard__") && covered.size() < variants.size()) {
                std::string uncovered;
                for (auto& kv : variants) {
                    if (!covered.count(kv.first)) {
                        if (!uncovered.empty()) uncovered += ", ";
                        uncovered += kv.first;
                    }
                }
                addError("Select on " + selectTypeStr + " is not exhaustive: variant(s) " +
                         uncovered + " are not covered (add the missing variant(s) or a wildcard '?')", node);
            }
        }
    }
}

void SemanticAnalyzer::visit(ast::ComparisonPattern* node) {
    // Visit the value expression in the comparison pattern
    if (node->value) {
        node->value->accept(*this);
    }

    // TODO: Type checking:
    // - Verify the comparison operator is valid for the value type
    // - Check that the matched expression type supports the comparison
}


void SemanticAnalyzer::visit(ast::StructPattern* node) {
    // Struct destructuring is handled inline in MatchStatement::visit so the
    // bound fields live in the correct case scope alongside the case body.
    (void)node;
}

void SemanticAnalyzer::visit(ast::MatchExpression* node) {
    if (node->match) {
        node->match->accept(*this);
    }
    // Infer the result type from the first arm's yielded value. A naked
    // expression yields its expression type; a block arm yields via a `pass`
    // statement. Codegen uses this type for the shared result slot.
    if (node->match && !node->match->cases.empty()) {
        ast::TypeNode* yielded = nullptr;
        ast::ExprPtr& body = node->match->cases[0].second;
        if (body) {
            if (auto* be = dynamic_cast<ast::BlockExpression*>(body.get())) {
                if (be->block) {
                    for (auto& stmt : be->block->body) {
                        if (auto* pass = dynamic_cast<ast::PassStatement*>(stmt.get())) {
                            if (pass->argument) {
                                auto it = expressionTypes.find(pass->argument.get());
                                if (it != expressionTypes.end()) yielded = it->second;
                            }
                            break;
                        }
                    }
                }
            } else {
                auto it = expressionTypes.find(body.get());
                if (it != expressionTypes.end()) yielded = it->second;
            }
        }
        if (yielded) {
            node->resultType = yielded->clone();
        }
    }
}

void SemanticAnalyzer::visit(ast::ListComprehension* node) {}
void SemanticAnalyzer::visit(ast::GenericInstantiationExpression* node) {
    if (!node || !node->baseExpression) {
        addError("Invalid generic instantiation expression.", node);
        return;
    }

    // Visit the base expression to understand what we're instantiating
    node->baseExpression->accept(*this);

    // Visit all generic arguments (type parameters)
    for (auto& typeArg : node->genericArguments) {
        if (typeArg) {
            typeArg->accept(*this);
        }
    }

    // Try to identify if this is a template instantiation
    if (auto identifier = dynamic_cast<ast::Identifier*>(node->baseExpression.get())) {
        if (enumGenericParamOrder.count(identifier->name)) {
            // Generic data enum concrete type, e.g. `Box<Int>`. Materialize its
            // payload types and type the expression as the concrete enum type.
            registerGenericEnumConcrete(identifier->name, node->toString(), node->genericArguments);
            auto* tn = new ast::TypeName(node->loc,
                std::make_unique<ast::Identifier>(node->loc, identifier->name));
            for (auto& a : node->genericArguments) tn->genericArgs.push_back(a->clone());
            expressionTypes[node] = tn;
            node->type = std::shared_ptr<ast::TypeNode>(tn->clone());
            return;
        }
        handleTemplateInstantiation(identifier, node->genericArguments, node);
    } else if (auto memberExpr = dynamic_cast<ast::MemberExpression*>(node->baseExpression.get())) {
        // Handle something like Container<Int>::create
        handleMemberTemplateInstantiation(memberExpr, node->genericArguments, node);
    } else {
        addError("Invalid base expression for generic instantiation.", node->baseExpression.get());
    }
}
void SemanticAnalyzer::visit(ast::PointerDerefExpression* node) {
    // at() intrinsic only allowed inside an freedom block
    if (!isInUnsafeBlock()) {
        addError("at() (pointer dereference) is only allowed inside an freedom block.", node);
        expressionTypes[node] = nullptr;
        return;
    }
    if (!node || !node->pointer) {
        addError("Malformed pointer dereference expression.", node);
        expressionTypes[node] = nullptr;
        return;
    }
    node->pointer->accept(*this);
    auto pointerTypeIt = expressionTypes.find(node->pointer.get());

    if (pointerTypeIt == expressionTypes.end() || !pointerTypeIt->second) {
        addError("Cannot dereference pointer with unknown type.", node->pointer.get());
        expressionTypes[node] = nullptr;
        return;
    }

    ast::TypeNode* resolvedPointerType = pointerTypeIt->second;
    ast::TypeNode* pointeeType = nullptr;

    if (auto ptrType = dynamic_cast<ast::PointerType*>(resolvedPointerType)) {
        if (ptrType->pointeeType) {
            pointeeType = ptrType->pointeeType->clone().release();
        } else {
            addError("Pointer type has no pointee type.", node);
        }
    } else if (auto locTypeName = dynamic_cast<ast::TypeName*>(resolvedPointerType)) {
        if (locTypeName->identifier->name == "loc") {
            if (!locTypeName->genericArgs.empty() && locTypeName->genericArgs[0]) {
                pointeeType = locTypeName->genericArgs[0]->clone().release();
            } else {
                addError("loc type used in dereference is missing its type parameter (e.g., loc<T>).", node);
            }
        } else {
            addError("Type " + locTypeName->identifier->name + " is not a 'loc<T>' or pointer type, cannot dereference.", node);
        }
    } else {
        addError("Cannot dereference non-pointer/non-loc type: " + resolvedPointerType->toString(), node);
    }

    if (!pointeeType) {
        addError("Failed to determine pointee type for dereference.", node);
        expressionTypes[node] = nullptr;
        if (node->type) node->type.reset();
        return;
    }

    expressionTypes[node] = pointeeType; // raw pointer, ownership handled by clone().release()
    node->type = std::shared_ptr<ast::TypeNode>(pointeeType->clone()); // node->type takes ownership of a new clone
}
void SemanticAnalyzer::visit(ast::AddrOfExpression* node) {
    // addr() intrinsic only allowed inside an freedom block
    if (!isInUnsafeBlock()) {
        addError("addr() is only allowed inside an freedom block.", node);
        expressionTypes[node] = nullptr;
        return;
    }
    if (!node || !node->getLocation()) {
         addError("Malformed addr_of expression.", node);
         expressionTypes[node] = nullptr;
        return;
    }
    node->getLocation()->accept(*this); // Operand of addr()

    if (!isLValue(node->getLocation().get())) {
        addError("Cannot take address of non-lvalue with addr().", node->getLocation().get());
        expressionTypes[node] = nullptr;
        return;
    }

    auto operandTypeIt = expressionTypes.find(node->getLocation().get());
    if (operandTypeIt == expressionTypes.end() || !operandTypeIt->second) {
        addError("Cannot take address of expression with unknown type using addr().", node->getLocation().get());
        expressionTypes[node] = nullptr;
        return;
    }

    // The type of addr(operand) is an integer type representing an address.
    // Using "i64" as a placeholder for the address type.
    // Vyb should have a dedicated address type (e.g., uintptr).
    auto addr_type_ident = std::make_unique<ast::Identifier>(node->loc, "i64");
    ast::TypeNode* addrAstType = new ast::TypeName(node->loc, std::move(addr_type_ident));
    expressionTypes[node] = addrAstType;
    node->type = std::shared_ptr<ast::TypeNode>(addrAstType->clone());
}
void SemanticAnalyzer::visit(ast::FromIntToLocExpression* node) {
    // from<T>() intrinsic only allowed inside an freedom block
    if (!isInUnsafeBlock()) {
        addError("from<Type>(expr) is only allowed inside an freedom block.", node);
        expressionTypes[node] = nullptr;
        return;
    }
    if (!node || !node->getAddressExpression() || !node->getTargetType()) {
        addError("Malformed from_int_to_loc expression.", node);
        expressionTypes[node] = nullptr;
        return;
    }

    node->getAddressExpression()->accept(*this);
    auto addrExprTypeIt = expressionTypes.find(node->getAddressExpression().get());
    ast::TypeNode* addrExprType = (addrExprTypeIt != expressionTypes.end()) ? addrExprTypeIt->second : nullptr;

    // Assuming TypeNode has isIntegerTy() or similar. If not, this needs adjustment.
    // For now, we rely on the isIntegerType helper if addrExprType is a TypeName.
    bool isAddrExprInteger = false;
    if (addrExprType) {
        if (auto tn = dynamic_cast<ast::TypeName*>(addrExprType)) {
             isAddrExprInteger = isIntegerType(tn); // Use our helper
        } else {
            // TODO: Handle other TypeNode subtypes if they can be integers
        }
    }

    if (!isAddrExprInteger) {
        addError("Address expression in from<T>() must be an integer type. Got: " + (addrExprType ? addrExprType->toString() : "unknown"), node);
        expressionTypes[node] = nullptr;
        return;
    }

    node->getTargetType()->accept(*this); // Resolve/validate the target type (e.g. loc<i64>)
    ast::TypeNode* targetType = node->getTargetType().get();

    bool isTargetLocOrPointer = false;
    if (auto tn = dynamic_cast<ast::TypeName*>(targetType)) {
        if (tn->identifier && tn->identifier->name == "loc" && !tn->genericArgs.empty() && tn->genericArgs[0]) {
            isTargetLocOrPointer = true;
        }
    } else if (dynamic_cast<ast::PointerType*>(targetType)) {
        isTargetLocOrPointer = true;
    }

    if (!isTargetLocOrPointer) {
        addError("Target type in from<T>() must be a location/pointer type (e.g., loc<ActualType>). Got: " + targetType->toString(), node);
        expressionTypes[node] = nullptr;
        return;
    }

    expressionTypes[node] = targetType->clone().release();
    node->type = std::shared_ptr<ast::TypeNode>(targetType->clone());
}
void SemanticAnalyzer::visit(ast::LocationExpression* node) {
    // Check if we're in an freedom block
    if (!isInUnsafeBlock()) {
        addError("loc() (location-of) is only allowed inside an freedom block.", node);
        expressionTypes[node] = nullptr;
        return;
    }

    // Visit the expression to get its type
    if (!node->expression) {
        addError("Missing expression in loc().", node);
        expressionTypes[node] = nullptr;
        return;
    }

    node->expression->accept(*this);

    // Create a pointer type for the expression
    auto it = expressionTypes.find(node->expression.get());
    if (it == expressionTypes.end() || !it->second) {
        addError("Cannot get location of expression with unknown type", node);
        expressionTypes[node] = nullptr;
        return;
    }

    // Set the type of the location expression
    ast::TypeNode* pointeeType = it->second;

    // Create loc<T> type where T is the pointed-to type
    auto locIdent = std::make_unique<ast::Identifier>(node->loc, "loc");
    auto typeName = new ast::TypeName(node->loc, std::move(locIdent));
    typeName->genericArgs.push_back(std::unique_ptr<ast::TypeNode>(pointeeType->clone()));

    expressionTypes[node] = typeName;
    node->type = std::shared_ptr<ast::TypeNode>(typeName->clone());
}
void SemanticAnalyzer::visit(ast::IfStatement* node) {
    // Visit the test condition
    node->test->accept(*this);

    // Visit the consequent block
    node->consequent->accept(*this);

    // Visit the alternate block if it exists
    if (node->alternate) {
        node->alternate->accept(*this);
    }
}
void SemanticAnalyzer::visit(ast::ForStatement* node) {
    // Enter a new scope for the loop
    enterScope();
    currentScope->isLoop = true;

    // Visit the initialization
    if (node->init) {
        node->init->accept(*this);
    }

    // Visit the test condition
    if (node->test) {
        node->test->accept(*this);
    }

    // Visit the update expression
    if (node->update) {
        node->update->accept(*this);
    }

    // Visit the body
    node->body->accept(*this);

    // Exit the loop scope
    exitScope();
}
void SemanticAnalyzer::visit(ast::WhileStatement* node) {
    // Enter a new scope for the loop
    enterScope();
    currentScope->isLoop = true;

    // Visit the test condition
    node->test->accept(*this);

    // Visit the body
    node->body->accept(*this);

    // Exit the loop scope
    exitScope();
}
void SemanticAnalyzer::visit(ast::ReturnStatement* node) {
    // Visit the return value if it exists
    if (node->argument) {
        // Bare Option constructor injection for `return Some(x)` / `return None`
        // inside a function whose return type is `Option<T>`.
        if (currentFunction && currentFunction->returnTypeNode) {
            ast::TypeNode* retTy = currentFunction->returnTypeNode->type
                ? currentFunction->returnTypeNode->type.get()
                : currentFunction->returnTypeNode.get();
            if (retTy) injectBareEnumConstructorType(node->argument.get(), retTy);
        }
        node->argument->accept(*this);
    }
}

void SemanticAnalyzer::visit(ast::PassStatement* node) {
    // Visit the pass value (required)
    if (node->argument) {
        node->argument->accept(*this);
    } else {
        errors.push_back("Pass statement requires an expression");
    }
    // TODO: Check that we're inside a select expression block
}

void SemanticAnalyzer::visit(ast::BreakStatement* node) {
    // Check if we're inside a loop
    if (!isInLoop()) {
        errors.push_back("Break statement must be inside a loop");
    }
}
void SemanticAnalyzer::visit(ast::ContinueStatement* node) {
    // Check if we're inside a loop
    if (!isInLoop()) {
        errors.push_back("Continue statement must be inside a loop");
    }
}
void SemanticAnalyzer::visit(ast::TryStatement* node) {
    // Visit the try block
    node->tryBlock->accept(*this);

    // Visit the catch block if it exists
    if (node->catchBlock) {
        node->catchBlock->accept(*this);
    }

    // Visit the finally block if it exists
    if (node->finallyBlock) {
        node->finallyBlock->accept(*this);
    }
}
void SemanticAnalyzer::visit(ast::UnsafeStatement* node) {
    // Enter a new scope for the freedom block
    enterScope();
    currentScope->isUnsafeBlock = true;
    node->block->accept(*this);
    exitScope();
}
void SemanticAnalyzer::visit(ast::AssertStatement* node) {}
void SemanticAnalyzer::visit(ast::MatchStatement* node) {
    // Visit the expression being matched
    if (node->expr) {
        node->expr->accept(*this);
    }

    // Best-effort static type of the match expression, used to reject struct
    // destructuring patterns whose target type can never match.
    std::string matchTypeStr;
    if (node->expr) {
        if (auto it = expressionTypes.find(node->expr.get()); it != expressionTypes.end() && it->second) {
            matchTypeStr = it->second->toString();
        } else if (node->expr->type) {
            matchTypeStr = node->expr->type->toString();
        }
    }

    // Track comparison patterns for unreachable detection
    struct ComparisonInfo {
        vyb::TokenType op;
        int64_t value;
        size_t caseIndex;
    };
    std::vector<ComparisonInfo> comparisons;
    size_t wildcardIndex = SIZE_MAX;

    // Visit all patterns and check for unreachable patterns
    for (size_t i = 0; i < node->cases.size(); i++) {
        const auto& [pattern, block] = node->cases[i];

        // Each case (pattern + body) gets its own scope so destructured bindings
        // from a struct pattern are scoped to the matching arm only.
        enterScope();

        if (!pattern) {
            // Wildcard pattern (nullptr). A wildcard WITH a guard is not
            // exhaustive, so it does not make later patterns unreachable.
            bool wildcardHasGuard = (i < node->guards.size() && node->guards[i]);
            if (wildcardHasGuard) {
                // A guarded wildcard covers only the guarded condition; leave
                // downstream patterns reachable.
            } else if (wildcardIndex == SIZE_MAX) {
                wildcardIndex = i;
            } else {
                // Can't get loc from nullptr, use block as node
                addError("Wildcard pattern already used in case " +
                        std::to_string(wildcardIndex + 1) + ", this pattern is unreachable", block.get());
            }
        } else if (auto* comp = dynamic_cast<ast::ComparisonPattern*>(pattern.get())) {
            // Check if this pattern comes after a wildcard
            if (wildcardIndex != SIZE_MAX) {
                addError("Pattern after wildcard in case " +
                        std::to_string(wildcardIndex + 1) + " is unreachable", pattern.get());
            }

            // Try to extract constant value for unreachable detection
            if (auto* intLit = dynamic_cast<ast::IntegerLiteral*>(comp->value.get())) {
                int64_t value = intLit->value;
                vyb::TokenType op = comp->op.type;

                // Check against all previous comparison patterns
                for (const auto& prev : comparisons) {
                    bool isUnreachable = false;
                    std::string reason;

                    // Check for duplicate patterns
                    if (prev.op == op && prev.value == value) {
                        isUnreachable = true;
                        reason = "duplicate pattern";
                    }
                    // Check for subsumption: >= 80 subsumes >= 90
                    else if (prev.op == vyb::TokenType::GTEQ && op == vyb::TokenType::GTEQ && prev.value <= value) {
                        isUnreachable = true;
                        reason = "subsumed by earlier '>= " + std::to_string(prev.value) + "'";
                    }
                    // Check for subsumption: <= 80 subsumes <= 70
                    else if (prev.op == vyb::TokenType::LTEQ && op == vyb::TokenType::LTEQ && prev.value >= value) {
                        isUnreachable = true;
                        reason = "subsumed by earlier '<= " + std::to_string(prev.value) + "'";
                    }
                    // Check for subsumption: >= 80 subsumes > 80
                    else if (prev.op == vyb::TokenType::GTEQ && op == vyb::TokenType::GT && prev.value <= value) {
                        isUnreachable = true;
                        reason = "subsumed by earlier '>= " + std::to_string(prev.value) + "'";
                    }
                    // Check for subsumption: <= 80 subsumes < 80
                    else if (prev.op == vyb::TokenType::LTEQ && op == vyb::TokenType::LT && prev.value >= value) {
                        isUnreachable = true;
                        reason = "subsumed by earlier '<= " + std::to_string(prev.value) + "'";
                    }
                    // Check for == within >= range: >= 70 subsumes == 75
                    else if (prev.op == vyb::TokenType::GTEQ && op == vyb::TokenType::EQEQ && value >= prev.value) {
                        isUnreachable = true;
                        reason = "covered by earlier '>= " + std::to_string(prev.value) + "'";
                    }
                    // Check for == within <= range: <= 80 subsumes == 75
                    else if (prev.op == vyb::TokenType::LTEQ && op == vyb::TokenType::EQEQ && value <= prev.value) {
                        isUnreachable = true;
                        reason = "covered by earlier '<= " + std::to_string(prev.value) + "'";
                    }
                    // Check for == within > range: > 70 subsumes == 75
                    else if (prev.op == vyb::TokenType::GT && op == vyb::TokenType::EQEQ && value > prev.value) {
                        isUnreachable = true;
                        reason = "covered by earlier '> " + std::to_string(prev.value) + "'";
                    }
                    // Check for == within < range: < 80 subsumes == 75
                    else if (prev.op == vyb::TokenType::LT && op == vyb::TokenType::EQEQ && value < prev.value) {
                        isUnreachable = true;
                        reason = "covered by earlier '< " + std::to_string(prev.value) + "'";
                    }

                    if (isUnreachable) {
                        std::string opStr;
                        switch (op) {
                            case vyb::TokenType::EQEQ:  opStr = "=="; break;
                            case vyb::TokenType::NOTEQ: opStr = "!="; break;
                            case vyb::TokenType::LT:    opStr = "<"; break;
                            case vyb::TokenType::LTEQ:  opStr = "<="; break;
                            case vyb::TokenType::GT:    opStr = ">"; break;
                            case vyb::TokenType::GTEQ:  opStr = ">="; break;
                            default: opStr = "?"; break;
                        }
                        addError("Pattern '" + opStr + " " + std::to_string(value) +
                                "' in case " + std::to_string(i + 1) + " is " + reason +
                                " (case " + std::to_string(prev.caseIndex + 1) + ")", pattern.get());
                        break;
                    }
                }

                comparisons.push_back({op, value, i});
            }

            comp->accept(*this);
        } else if (auto* sp = dynamic_cast<ast::StructPattern*>(pattern.get())) {
            // Struct destructuring: bind each field name as a variable in the
            // current (case) scope so the case body can read the fields.
            std::string structName = sp->typeName ? sp->typeName->toString() : "";
            // Extract the core type identifier of a type string (strip ownership
            // wrappers like my<Point> and trailing generic arguments) so we can
            // reject patterns that can never match the match expression's type.
            auto coreTypeName = [](std::string t) -> std::string {
                bool stripped = true;
                while (stripped) {
                    stripped = false;
                    size_t lt = t.find('<');
                    if (lt != std::string::npos) {
                        std::string head = t.substr(0, lt);
                        if (head == "my" || head == "our" || head == "their" || head == "mild") {
                            size_t gt = t.rfind('>');
                            if (gt != std::string::npos && gt > lt) {
                                t = t.substr(lt + 1, gt - lt - 1);
                                stripped = true;
                            }
                        }
                    }
                }
                size_t lt = t.find('<');
                if (lt != std::string::npos) t = t.substr(0, lt);
                return t;
            };
            if (!matchTypeStr.empty() && coreTypeName(matchTypeStr) != structName) {
                addError("Struct pattern '" + structName + "' cannot match a value of type '" +
                         matchTypeStr + "'.", sp);
                auto fieldsIt = structFieldTypes.find(structName);
                if (fieldsIt != structFieldTypes.end()) {
                    const auto& spFields = fieldsIt->second;
                    for (auto& binding : sp->bindings) {
                        binding->type = nullptr;
                    }
                }
            } else {
                auto fieldsIt = structFieldTypes.find(structName);
                if (fieldsIt == structFieldTypes.end()) {
                    addError("Unknown struct type in destructuring pattern: " + structName, sp);
                } else {
                    const auto& spFields = fieldsIt->second;
                    for (auto& binding : sp->bindings) {
                        const std::string& fname = binding->name;
                        if (currentScope->lookupDirect(fname)) {
                            addError("Redefinition of variable \"" + fname +
                                     "\" in destructuring pattern.", binding.get());
                            continue;
                        }
                        auto ftIt = spFields.find(fname);
                        if (ftIt == spFields.end()) {
                            addError("Struct '" + structName + "' has no field '" + fname +
                                     "' in destructuring pattern.", binding.get());
                            continue;
                        }
                        ast::TypeNode* fieldType = ftIt->second;
                        currentScope->add(SymbolInfo{
                            SymbolInfo::Kind::Variable, fname, false, ast::OwnershipKind::MY,
                            fieldType ? fieldType->clone().release() : nullptr});
                        // Record the field type on the binding itself so codegen can
                        // populate valueTypeMap for the destructured variable.
                        binding->type = fieldType ? std::shared_ptr<ast::TypeNode>(fieldType->clone()) : nullptr;
                    }
                }
            }
        } else if (auto* rng = dynamic_cast<ast::RangeExpression*>(pattern.get())) {
            // Range pattern `start..end` (inclusive). Validate the bounds and
            // flag a range that can never match (start > end).
            if (rng->start) rng->start->accept(*this);
            if (rng->end) rng->end->accept(*this);
            auto* startLit = rng->start ? dynamic_cast<ast::IntegerLiteral*>(rng->start.get()) : nullptr;
            auto* endLit = rng->end ? dynamic_cast<ast::IntegerLiteral*>(rng->end.get()) : nullptr;
            if (startLit && endLit && startLit->value > endLit->value) {
                addError("Range pattern '..' has start greater than end and can never match.", rng);
            }
        } else if (auto* ctor = dynamic_cast<ast::ConstructionExpression*>(pattern.get())) {
            // Enum variant pattern with payload: `Circle(r)`, `Rect(w, h)`.
            bool handled = false;
            if (!matchTypeStr.empty()) {
                auto variantsIt = enumVariantPayloadTypes.find(matchTypeStr);
                auto* tv = ctor->constructedType
                    ? dynamic_cast<ast::TypeName*>(ctor->constructedType.get()) : nullptr;
                if (variantsIt != enumVariantPayloadTypes.end() && tv && tv->identifier) {
                    auto varIt = variantsIt->second.find(tv->identifier->name);
                    if (varIt != variantsIt->second.end()) {
                        handled = true;
                        const auto& payload = varIt->second;
                        if (payload.size() != ctor->arguments.size()) {
                            addError("Enum variant '" + tv->identifier->name + "' of " + matchTypeStr +
                                     " expects " + std::to_string(payload.size()) + " binding(s), got " +
                                     std::to_string(ctor->arguments.size()), ctor);
                        }
                        for (size_t bi = 0; bi < ctor->arguments.size(); ++bi) {
                            auto* bid = dynamic_cast<ast::Identifier*>(ctor->arguments[bi].get());
                            if (!bid) {
                                addError("Enum variant pattern bindings must be identifiers.", ctor);
                                continue;
                            }
                            const std::string& bname = bid->name;
                            if (currentScope->lookupDirect(bname)) {
                                addError("Redefinition of variable \"" + bname +
                                         "\" in variant pattern.", bid);
                                continue;
                            }
                            ast::TypeNode* pt = (bi < payload.size()) ? payload[bi].get() : nullptr;
                            currentScope->add(SymbolInfo{SymbolInfo::Kind::Variable, bname, false,
                                ast::OwnershipKind::MY, pt ? pt->clone().release() : nullptr});
                        }
                    }
                }
            }
            if (!handled) {
                // Not a known variant of the matched enum: fall back to a generic
                // construction expression.
                ctor->accept(*this);
            }
        } else if (auto* vid = dynamic_cast<ast::Identifier*>(pattern.get())) {
            // Enum unit-variant pattern: `Unit`, `None`. When matching a tagged
            // enum and the identifier names one of its variants, it binds no
            // payload; otherwise it is a plain literal/identifier pattern.
            bool handled = false;
            if (!matchTypeStr.empty()) {
                auto variantsIt = enumVariantPayloadTypes.find(matchTypeStr);
                if (variantsIt != enumVariantPayloadTypes.end() && variantsIt->second.count(vid->name)) {
                    handled = true;
                }
            }
            if (!handled) {
                vid->accept(*this);
            }
        } else {
            // Other pattern types
            pattern->accept(*this);
        }

        // Optional guard clause is analyzed in the same (case) scope so it can
        // reference destructured bindings from a struct pattern.
        if (i < node->guards.size() && node->guards[i]) {
            node->guards[i]->accept(*this);
        }

        if (block) {
            block->accept(*this);
        }

        exitScope();
    }

    // Exhaustiveness checking for tagged-union enums: a match on a data-carrying
    // enum is exhaustive only when it has an unguarded wildcard or arms that
    // cover every variant. A non-exhaustive match would silently fall through to
    // an impossible default at runtime (the codegen default block is
    // `unreachable` for the exhaustive case), so reject it here.
    if (wildcardIndex == SIZE_MAX && !matchTypeStr.empty()) {
        auto variantsIt = enumVariantPayloadTypes.find(matchTypeStr);
        if (variantsIt != enumVariantPayloadTypes.end() && !variantsIt->second.empty()) {
            const auto& variants = variantsIt->second;
            std::set<std::string> covered;
            for (size_t i = 0; i < node->cases.size(); ++i) {
                auto& pat = node->cases[i].first;
                if (!pat) { covered.insert("__wildcard__"); break; }
                // A guarded arm covers only the subset where its guard is true,
                // so it does not establish unconditional coverage of its variant.
                bool guarded = (i < node->guards.size() && node->guards[i]);
                if (guarded) continue;
                std::string vname;
                if (auto* ctor = dynamic_cast<ast::ConstructionExpression*>(pat.get())) {
                    auto* tv = ctor->constructedType
                        ? dynamic_cast<ast::TypeName*>(ctor->constructedType.get()) : nullptr;
                    if (tv && tv->identifier) vname = tv->identifier->name;
                } else if (auto* idp = dynamic_cast<ast::Identifier*>(pat.get())) {
                    vname = idp->name;
                }
                if (variants.count(vname)) covered.insert(vname);
            }
            if (!covered.count("__wildcard__") && covered.size() < variants.size()) {
                std::string uncovered;
                for (auto& kv : variants) {
                    if (!covered.count(kv.first)) {
                        if (!uncovered.empty()) uncovered += ", ";
                        uncovered += kv.first;
                    }
                }
                addError("Match on " + matchTypeStr + " is not exhaustive: variant(s) " +
                         uncovered + " are not covered by an unguarded arm " +
                         "(add an unguarded arm or a wildcard '?')", node);
            }
        }
    }
}
void SemanticAnalyzer::visit(ast::YieldStatement* node) {
    // Check if we're inside a generator function
    // For now, we'll just check for any yield expression

    // Analyze the expression being yielded, if any
    if (node->expression) {
        node->expression->accept(*this);
    }

    // In a full implementation, we would:
    // 1. Verify we're inside a generator function (function with yield)
    // 2. Track the yielded type for generator return type inference
    // 3. Ensure the yielded type is consistent across the function
}

void SemanticAnalyzer::visit(ast::YieldReturnStatement* node) {
    // Similar to YieldStatement but specifically for final return from generator

    // Analyze the expression being returned, if any
    if (node->expression) {
        node->expression->accept(*this);
    }

    // In a full implementation:
    // 1. Verify we're inside a generator function
    // 2. Check that the returned type is compatible with the generator's return type
    // 3. Mark the generator as having an explicit final return value
}

// REMOVE DUPLICATE/EMPTY visit methods from here down
// void SemanticAnalyzer::visit(ast::TypeAliasDeclaration* node) {} // Handled above
void SemanticAnalyzer::visit(ast::ExternStatement* node) {
    // In an extern statement, we typically:
    // 1. Register the extern declaration in the current scope
    // 2. Don't analyze the body since it's external

    // For now, we'll just mark the statement as visited
    // In a full implementation, we would register any external declarations
    // in the symbol table for later use

    // No recursive visits needed as extern statements don't have a body to analyze
}

void SemanticAnalyzer::visit(ast::ImportDeclaration* node) {}
void SemanticAnalyzer::visit(ast::StructDeclaration* node) {
    if (!node || !node->name) {
        addError("Malformed struct declaration.", node);
        return;
    }

    const std::string& structName = node->name->name;

    if (isReservedWord(structName)) {
        addError("Identifier \"" + structName + "\" is a reserved word and cannot be used as a struct name.", node->name.get());
    }

    if (currentScope->lookupDirect(structName)) {
        addError("Redefinition of struct \"" + structName + "\" in the same scope.", node->name.get());
        return;
    }

    std::vector<std::string> genericParamNames;
    for (const auto& param : node->genericParams) {
        if (param && param->name) {
            genericParamNames.push_back(param->name->name);
        }
    }
    structGenericParamOrder[structName] = genericParamNames;

    // Handle generic parameters if present (e.g., struct Box<T>)
    bool hasGenericParams = !node->genericParams.empty();
    if (node->reprC && hasGenericParams) {
        addError("repr(C) struct \"" + structName + "\" cannot be generic; C ABI layout must be fully concrete.", node);
    }

    if (hasGenericParams) {
        enterScope();  // Create scope for type parameters

        for (const auto& param : node->genericParams) {
            if (param && param->name) {
                std::string paramName = param->name->name;

                // Validate aspect bounds (if any)
                for (const auto& bound : param->bounds) {
                    if (bound) {
                        std::string boundName = bound->toString();
                        // Check that the bound is actually an aspect
                        if (!findTrait(boundName)) {
                            addError("Bound '" + boundName + "' on type parameter '" + paramName + "' is not a defined aspect.", param.get());
                        }
                    }
                }

                // Register the type parameter as a TYPE_PARAMETER symbol
                SymbolInfo typeParamSymbol;
                typeParamSymbol.name = paramName;
                typeParamSymbol.kind = SymbolInfo::Kind::TYPE_PARAMETER;
                typeParamSymbol.type = nullptr;

                // Store bounds for this type parameter
                for (const auto& bound : param->bounds) {
                    if (bound) {
                        typeParamSymbol.bounds.push_back(bound->toString());
                    }
                }

                currentScope->add(typeParamSymbol);


            }
        }
    }

    // Create and register the struct type in the symbol table
    auto structType = new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->name->loc, structName));

    // Register in parent scope (not the type parameter scope)
    if (hasGenericParams) {
        currentScope->getParent()->add(SymbolInfo{SymbolInfo::Kind::Type, structName, false, ast::OwnershipKind::MY, structType});
    } else {
        currentScope->add(SymbolInfo{SymbolInfo::Kind::Type, structName, false, ast::OwnershipKind::MY, structType});
    }

    // Store struct field information for member access resolution
    // We'll use a map to store field types by struct name
    std::map<std::string, ast::TypeNode*> fieldTypes;

    for (auto& field : node->fields) {
        if (field && field->name && field->typeNode) {
            if (node->reprC) {
                std::string reason = reprCUnsupportedReason(field->typeNode.get());
                if (!reason.empty()) {
                    addError("repr(C) struct field \"" + field->name->name + "\" " + reason + ".", field.get());
                }
            }

            // Visit the field type to ensure it's valid (type params are now in scope)
            field->typeNode->accept(*this);

            // Store the resolved field type for later member access resolution.
            ast::TypeNode* effectiveFieldType = field->typeNode->type ? field->typeNode->type.get() : field->typeNode.get();
            fieldTypes[field->name->name] = effectiveFieldType;

            if (auto* fieldTypeName = dynamic_cast<ast::TypeName*>(effectiveFieldType)) {
                if (fieldTypeName->identifier && fieldTypeName->identifier->name == structName) {
                    addError("Struct \"" + structName + "\" cannot contain direct value field \"" +
                             field->name->name + "\" of its own type; use loc<" + structName +
                             "> or another indirection type.", field.get());
                }
            }

            VYB_CDBG << "DEBUG: Registered struct field " << structName << "." << field->name->name
                      << " with type: " << effectiveFieldType->toString() << std::endl;
        }
    }

    // Store field information in the semantic analyzer (we'll need to add this storage)
    structFieldTypes[structName] = fieldTypes;

    // Exit type parameter scope if we entered one
    if (hasGenericParams) {
        exitScope();

    }
}
// void SemanticAnalyzer::visit(ast::ClassDeclaration* node) {} // Handled above
void SemanticAnalyzer::visit(ast::EnumDeclaration* node) {
    if (!node || !node->name) return;
    const std::string& enumName = node->name->name;
    enumTypeNames.insert(enumName);
    if (node->variants.empty()) return;

    bool hasData = false;
    for (const auto& v : node->variants) {
        if (v && !v->associatedTypes.empty()) { hasData = true; break; }
    }
    if (!hasData) {
        // C-like enums are now first-class typed values: every variant is a unit
        // (zero-payload) variant of the named enum type rather than a raw i64. This
        // lets `r<Color> = Color::Red` take the enum type, variant access resolve to
        // `Color`, and `match`/`select` dispatch on a runtime tag with the same
        // exhaustiveness handling as data-carrying enums.
        if (node->genericParams.empty()) {
            auto& payloadMap = enumVariantPayloadTypes[enumName];
            for (const auto& v : node->variants) {
                if (v && v->name) payloadMap[v->name->name]; // default-construct (unit variant)
            }
            currentScope->add(SymbolInfo{SymbolInfo::Kind::Type, enumName, false,
                ast::OwnershipKind::MY,
                new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, enumName))});
        }
        return;
    }

    // Generic data enums keep an unsubstituted template plus the parameter order;
    // concrete instantiations (`Box<Int>`) are materialized on demand via
    // registerGenericEnumConcrete.
    if (!node->genericParams.empty()) {
        std::vector<std::string> paramNames;
        for (const auto& p : node->genericParams) {
            if (p && p->name) paramNames.push_back(p->name->name);
        }
        enumGenericParamOrder[enumName] = paramNames;
        auto& tmplMap = enumTemplatePayloadTypes[enumName];
        for (const auto& v : node->variants) {
            if (!v || !v->name) continue;
            std::vector<ast::TypeNodePtr> payload;
            for (const auto& t : v->associatedTypes) {
                if (t) payload.push_back(t->clone());
            }
            tmplMap[v->name->name] = std::move(payload);
        }
        currentScope->add(SymbolInfo{SymbolInfo::Kind::Type, enumName, false,
            ast::OwnershipKind::MY,
            new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, enumName))});
        return;
    }

    auto& payloadMap = enumVariantPayloadTypes[enumName];
    for (const auto& v : node->variants) {
        if (!v || !v->name) continue;
        std::vector<ast::TypeNodePtr> payload;
        for (const auto& t : v->associatedTypes) {
            if (t) payload.push_back(t->clone());
        }
        payloadMap[v->name->name] = std::move(payload);
    }

    // Register the enum as a known type so annotations like `s<Shape>` resolve
    // and so `s = Shape::Circle(...)` infers the enum type.
    currentScope->add(SymbolInfo{SymbolInfo::Kind::Type, enumName, false,
        ast::OwnershipKind::MY,
        new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, enumName))});
}

void SemanticAnalyzer::registerGenericEnumConcrete(
        const std::string& enumName, const std::string& concreteTypeStr,
        const std::vector<ast::TypeNodePtr>& typeArgs) {
    auto paramIt = enumGenericParamOrder.find(enumName);
    auto tmplIt = enumTemplatePayloadTypes.find(enumName);
    if (paramIt == enumGenericParamOrder.end() || tmplIt == enumTemplatePayloadTypes.end()) return;
    const auto& params = paramIt->second;
    if (params.size() != typeArgs.size()) return; // arity mismatch reported at the use site
    std::map<std::string, ast::TypeNode*> subs;
    bool ok = true;
    for (size_t i = 0; i < params.size(); ++i) {
        if (!typeArgs[i]) { ok = false; break; }
        subs[params[i]] = typeArgs[i].get();
    }
    if (!ok) return;
    auto& concrete = enumVariantPayloadTypes[concreteTypeStr];
    concrete.clear();
    for (const auto& kv : tmplIt->second) {
        std::vector<ast::TypeNodePtr> concretePayload;
        for (const auto& t : kv.second) {
            concretePayload.push_back(t ? substituteGenericArgsForValidation(t.get(), subs) : nullptr);
        }
        concrete[kv.first] = std::move(concretePayload);
    }
}
// void SemanticAnalyzer::visit(ast::TraitDeclaration* node) {} // Handled above (commented out)
// void SemanticAnalyzer::visit(ast::ImplDeclaration* node) {} // Handled above
// void SemanticAnalyzer::visit(ast::NamespaceDeclaration* node) {} // Handled above (commented out)
void SemanticAnalyzer::visit(ast::FieldDeclaration* node) {}
void SemanticAnalyzer::visit(ast::EnumVariant* node) {}
// void SemanticAnalyzer::visit(ast::GenericParameter* node) {} // Handled above
void SemanticAnalyzer::visit(ast::TemplateDeclaration* node) {
    if (!node || !node->name) {
        addError("Malformed template declaration.", node);
        return;
    }

    const std::string& templateName = node->name->name;

    // Check if template name is already registered
    if (templateRegistry.find(templateName) != templateRegistry.end()) {
        addError("Template '" + templateName + "' is already defined.", node);
        return;
    }

    // Validate generic parameters
    for (const auto& param : node->genericParams) {
        if (!param || !param->name) {
            addError("Invalid generic parameter in template '" + templateName + "'.", node);
            return;
        }
    }

    // Clone the template declaration for storage
    auto clonedDecl = std::make_unique<ast::TemplateDeclaration>(
        node->loc,
        std::make_unique<ast::Identifier>(node->name->loc, node->name->name),
        std::vector<std::unique_ptr<ast::GenericParameter>>(),
        nullptr // Will be cloned separately if needed
    );

    // Clone generic parameters
    for (const auto& param : node->genericParams) {
        if (param && param->name) {
            auto clonedParam = std::make_unique<ast::GenericParameter>(
                param->loc,
                std::make_unique<ast::Identifier>(param->name->loc, param->name->name)
            );

            // Clone bounds if present
            for (const auto& bound : param->bounds) {
                if (bound) {
                    clonedParam->bounds.push_back(bound->clone());
                }
            }

            clonedDecl->genericParams.push_back(std::move(clonedParam));
        }
    }

    // Register the template BEFORE visiting the body
    // This allows recursive template definitions
    auto templateInfo = std::make_unique<TemplateInfo>(std::move(clonedDecl));

    // Enter a new scope for the template body
    enterScope();

    // Register all generic type parameters as valid types in this scope
    // This allows code inside the template to use T, K, V, etc.
    for (const auto& param : node->genericParams) {
        if (param && param->name) {
            const std::string& paramName = param->name->name;

            // Create a placeholder TypeName for the generic parameter
            auto genericType = std::make_unique<ast::TypeName>(
                param->loc,
                std::make_unique<ast::Identifier>(param->loc, paramName)
            );

            // Register it in the symbol table as a type parameter
            SymbolInfo sym;
            sym.name = paramName;
            sym.type = genericType.get(); // Store raw pointer, managed separately
            sym.kind = SymbolInfo::Kind::TYPE_PARAMETER; // Mark as generic type parameter

            currentScope->add(sym);

            VYB_CDBG << "DEBUG: Registered template type parameter: " << paramName << std::endl;
        }
    }

    // Store template in registry
    templateRegistry[templateName] = std::move(templateInfo);

    // Visit the template body with type parameters in scope
    if (node->body) {
        VYB_CDBG << "DEBUG: Visiting template body for: " << templateName << std::endl;
        node->body->accept(*this);
    }

    // Exit the template scope
    exitScope();

    VYB_CDBG << "DEBUG: Template '" << templateName << "' registered successfully" << std::endl;
}

void SemanticAnalyzer::visit(ast::AspectDeclaration* node) {
    if (!node || !node->name) {
        addError("Malformed aspect declaration.", node);
        return;
    }

    const std::string& traitName = node->name->name;

    // Check if aspect is already registered
    if (traitRegistry.find(traitName) != traitRegistry.end()) {
        addError("Aspect '" + traitName + "' is already defined.", node);
        return;
    }

    // Validate generic parameters
    for (const auto& param : node->genericParams) {
        if (!param || !param->name) {
            addError("Invalid generic parameter in aspect '" + traitName + "'.", node);
            return;
        }
    }

    std::unordered_set<std::string> associatedTypeNames;
    for (const auto& associatedType : node->associatedTypes) {
        if (!associatedType) {
            addError("Invalid associated type in aspect '" + traitName + "'.", node);
            return;
        }

        const std::string& associatedTypeName = associatedType->name;
        if (!associatedTypeNames.insert(associatedTypeName).second) {
            addError("Duplicate associated type '" + associatedTypeName + "' in aspect '" + traitName + "'.", associatedType.get());
            return;
        }
    }

    // Validate aspect methods
    for (const auto& method : node->methods) {
        if (!method || !method->id) {
            addError("Invalid method in aspect '" + traitName + "'.", node);
            return;
        }

        // Check for self parameter
        bool hasSelfParam = false;
        if (!method->params.empty() && method->params[0].name) {
            if (method->params[0].name->name == "self") {
                hasSelfParam = true;
            }
        }

        if (!hasSelfParam) {
            addError("Aspect method '" + method->id->name + "' must have 'self' as first parameter.", method.get());
        }

        // Validate return type
        if (!method->returnTypeNode) {
            addError("Aspect method '" + method->id->name + "' must declare a return type.", method.get());
        }

        VYB_CDBG << "DEBUG:   Method: " << method->id->name
                  << " (default impl: " << (method->body ? "yes" : "no") << ")" << std::endl;
    }

    // Register the aspect
    registerTrait(node);

    // Register aspect as a type in the symbol table
    SymbolInfo traitSym;
    traitSym.name = traitName;
    traitSym.kind = SymbolInfo::Kind::Type;
    traitSym.type = nullptr; // Aspects are interface types, not concrete
    currentScope->add(traitSym);
}

void SemanticAnalyzer::visit(ast::BindDeclaration* node) {
    if (!node || !node->selfType) {
        addError("Malformed bind declaration.", node);
        return;
    }

    // Handle generic parameters if present (e.g., impl<T> ...)
    bool hasGenericParams = !node->genericParams.empty();
    std::vector<std::string> typeParamNames;

    if (hasGenericParams) {


        // Enter a new scope for type parameters
        enterScope();

        // Register each type parameter as a valid type in this scope
        for (const auto& param : node->genericParams) {
            if (param && param->name) {
                std::string paramName = param->name->name;
                typeParamNames.push_back(paramName);

                // Validate aspect bounds (if any)
                for (const auto& bound : param->bounds) {
                    if (bound) {
                        std::string boundName = bound->toString();
                        // Check that the bound is actually an aspect
                        if (!findTrait(boundName)) {
                            addError("Bound '" + boundName + "' on type parameter '" + paramName + "' is not a defined aspect.", param.get());
                        }
                    }
                }

                // Register the type parameter as a TYPE_PARAMETER symbol
                SymbolInfo typeParamSymbol;
                typeParamSymbol.name = paramName;
                typeParamSymbol.kind = SymbolInfo::Kind::TYPE_PARAMETER;
                typeParamSymbol.type = nullptr; // Generic type parameter has no concrete type yet

                // Store bounds for this type parameter
                for (const auto& bound : param->bounds) {
                    if (bound) {
                        typeParamSymbol.bounds.push_back(bound->toString());
                    }
                }

                currentScope->add(typeParamSymbol);

                VYB_CDBG << "DEBUG: Registered type parameter: " << paramName << std::endl;
            }
        }
    }

    std::string typeName = node->selfType->toString();
    std::string traitName;

    // Set current impl type for Self resolution (will be restored later)
    ast::TypeNode* previousImplType = currentImplType;
    currentImplType = node->selfType.get();
    VYB_CDBG << "DEBUG: Set currentImplType to " << typeName << " for Self resolution" << std::endl;

    if (node->traitType) {
        // This is an aspect implementation: bind Aspect -> Type
        traitName = node->traitType->toString();

        VYB_CDBG << "DEBUG: Processing impl " << traitName << " for " << typeName << std::endl;

        // Check if trait exists
        TraitInfo* traitInfo = findTrait(traitName);
        if (!traitInfo) {
            addError("Trait '" + traitName + "' is not defined.", node);
            if (hasGenericParams) exitScope();
            return;
        }

        // Check if type exists (for user-defined types)
        // Primitives like Int, Float, String are always valid
        bool isBuiltinType = (typeName == "Int" || typeName == "Float" ||
                             typeName == "Bool" || typeName == "String" ||
                             typeName == "Char" || typeName == "Rune");

        // For generic impls, the type might contain type parameters (e.g., Vec<T>)
        // which should be allowed if T is a type parameter
        bool isGenericType = false;
        if (hasGenericParams) {
            // Check if typeName contains any type parameters
            for (const auto& paramName : typeParamNames) {
                if (typeName.find(paramName) != std::string::npos) {
                    isGenericType = true;
                    VYB_CDBG << "DEBUG: Type " << typeName << " uses type parameter " << paramName << std::endl;
                    break;
                }
            }
        }

        bool isBuiltinGenericType = isBuiltinVecType(node->selfType.get());

        if (!isBuiltinType && !isBuiltinGenericType && !isGenericType) {
            SymbolInfo* typeSym = currentScope->lookup(typeName);
            if (!typeSym || typeSym->kind != SymbolInfo::Kind::Type) {
                // Allow binding to a concrete instantiation of a generic struct
                // (e.g. bind Display -> Box<Int>): resolve the base template and
                // validate the type-argument count against its generic parameters.
                bool resolvedInstantiation = false;
                auto* selfT = dynamic_cast<ast::TypeName*>(node->selfType.get());
                if (selfT && selfT->identifier) {
                    const std::string& baseId = selfT->identifier->name;
                    const std::vector<std::string>* paramOrder = nullptr;
                    auto structIt = structGenericParamOrder.find(baseId);
                    auto enumIt = enumGenericParamOrder.find(baseId);
                    if (structIt != structGenericParamOrder.end()) {
                        SymbolInfo* baseSym = currentScope->lookup(baseId);
                        if (baseSym && baseSym->kind == SymbolInfo::Kind::Type) {
                            resolvedInstantiation = true;
                            paramOrder = &structIt->second;
                        }
                    } else if (enumIt != enumGenericParamOrder.end()) {
                        // Generic enum bind target: a user-defined generic enum
                        // (e.g. bind ToStr -> Box<Int>) or a built-in generic enum
                        // (Option<T> / Result<T,E>).
                        if (enumIt->first == "Option" || enumIt->first == "Result") {
                            resolvedInstantiation = true;  // built-in: no scope symbol
                            paramOrder = &enumIt->second;
                        } else {
                            SymbolInfo* baseSym = currentScope->lookup(baseId);
                            if (baseSym && baseSym->kind == SymbolInfo::Kind::Type) {
                                resolvedInstantiation = true;
                                paramOrder = &enumIt->second;
                            }
                        }
                    }
                    if (resolvedInstantiation && paramOrder) {
                        size_t expectedParams = paramOrder->size();
                        if (!selfT->genericArgs.empty() && expectedParams != 0 &&
                            selfT->genericArgs.size() != expectedParams) {
                            addError("Type '" + typeName + "' has " + std::to_string(selfT->genericArgs.size()) +
                                     " type argument(s) but '" + baseId + "' expects " +
                                     std::to_string(expectedParams) + ".", node);
                            if (hasGenericParams) exitScope();
                            return;
                        }
                    }
                }
                if (!resolvedInstantiation) {
                    addError("Type '" + typeName + "' is not defined.", node);
                    if (hasGenericParams) exitScope();
                    return;
                }
            }
        }

        // Validate that all required trait methods are implemented
        if (!validateTraitImpl(typeName, traitName, node->methods, node->associatedTypeBindings, node)) {
            addError("Incomplete implementation of trait '" + traitName + "' for type '" + typeName + "'.", node);
            if (hasGenericParams) exitScope();
            return;
        }

        // Register the trait implementation
        registerTraitImpl(node);

        // Visit all methods to validate their bodies (while type params are still in scope)
        std::string previousImplTraitName = currentImplTraitName;
        auto previousAssociatedTypeBindings = currentImplAssociatedTypeBindings;
        currentImplTraitName = traitName;
        currentImplAssociatedTypeBindings.clear();
        for (const auto& assocBinding : node->associatedTypeBindings) {
            if (assocBinding.name && assocBinding.valueType) {
                currentImplAssociatedTypeBindings[assocBinding.name->name] = assocBinding.valueType.get();
            }
        }

        processingTraitOrBindMethod = true;  // Don't add bind methods to global scope
        for (const auto& method : node->methods) {
            if (method) {
                method->accept(*this);
            }
        }
        processingTraitOrBindMethod = false;
        currentImplTraitName = previousImplTraitName;
        currentImplAssociatedTypeBindings = previousAssociatedTypeBindings;

        VYB_CDBG << "DEBUG: Successfully registered impl " << traitName << " for " << typeName
                  << " with " << node->methods.size() << " methods" << std::endl;
    } else {
        // This is an inherent bind: bind Type { ... }
        // Just adds methods directly to the type without an aspect
        VYB_CDBG << "DEBUG: Processing inherent bind for " << typeName << std::endl;

        // Visit all methods to validate them
        processingTraitOrBindMethod = true;  // Don't add inherent bind methods to global scope
        for (const auto& method : node->methods) {
            if (method) {
                method->accept(*this);
            }
        }
        processingTraitOrBindMethod = false;
    }

    // Exit the type parameter scope if we entered one
    if (hasGenericParams) {
        exitScope();

    }

    // Restore previous impl type
    currentImplType = previousImplType;
}

void SemanticAnalyzer::visit(ast::ThrowStatement* node) {}

// --- Error Handling Visitor Implementations ---

void SemanticAnalyzer::visit(ast::FailStatement* node) {
    if (currentFunction) {
        currentFunction->canFail = true;
        currentFunction->needsErrorReturn = true;
    }

    // Verify error expression is present
    if (!node->error) {
        addError("fail statement requires an error expression", node);
        return;
    }

    // Type check the error expression
    node->error->accept(*this);

    ast::TypeNode* explicitErrorType = nullptr;
    if (node->errorType) {
        node->errorType->accept(*this);
        explicitErrorType = node->errorType->type ? node->errorType->type.get() : node->errorType.get();
    }

    // Get the type of the error expression
    auto it = expressionTypes.find(node->error.get());
    if (it != expressionTypes.end() && it->second) {
        ast::TypeNode* errorType = it->second;
        if (explicitErrorType && !areTypesCompatible(explicitErrorType, errorType)) {
            addError("Typed fail expects " + explicitErrorType->toString() +
                     " but error expression has type " + errorType->toString(), node);
            return;
        }

        // TODO: Verify error type implements Errable aspect when aspect system is complete
        // For now, accept any struct/object type as potential error

        VYB_CDBG << "DEBUG: fail statement with error type: " << errorType->toString() << std::endl;
    } else {
        addError("Could not determine type of error expression in fail statement", node->error.get());
    }

    // Note: Stack trace capture will be handled in codegen phase
}

void SemanticAnalyzer::visit(ast::TrapClause* node) {
    // Verify error name and type are present
    if (!node->errorName) {
        addError("trap clause requires an error variable name", node);
        return;
    }

    // Phase 6.5: Allow wildcard traps (e<?>) - errorType is nullptr but isWildcard is true
    // Phase 6.6: Allow multi-type traps (e<Type1 | Type2>) - errorTypes vector populated
    if (!node->isWildcard && !node->isMultiType && !node->errorType) {
        addError("trap clause requires an error type", node);
        return;
    }

    // Validate multi-type trap
    if (node->isMultiType) {
        if (node->errorTypes.empty()) {
            addError("multi-type trap clause requires at least one error type", node);
            return;
        }

        // Check for duplicate types in union
        std::set<std::string> seenTypes;
        for (auto& errorType : node->errorTypes) {
            if (!errorType) {
                addError("invalid null type in multi-type trap clause", node);
                return;
            }

            std::string typeName = errorType->toString();
            if (seenTypes.count(typeName) > 0) {
                addError("duplicate type '" + typeName + "' in multi-type trap clause", node);
                return;
            }
            seenTypes.insert(typeName);

            // Type check each error type
            errorType->accept(*this);
        }
    } else if (node->errorType) {
        // Type check single error type (unless wildcard)
        node->errorType->accept(*this);
    }

    // Enter new scope for trap handler
    enterScope();
    trapDepth++;

    // Add error types to active trap stack
    if (node->isWildcard) {
        activeTrapTypes.push_back(nullptr);
    } else if (node->isMultiType) {
        // For multi-type, add each type to the active trap stack
        for (auto& errorType : node->errorTypes) {
            activeTrapTypes.push_back(errorType.get());
        }
    } else {
        activeTrapTypes.push_back(node->errorType.get());
    }

    // Add error variable to scope (immutable binding). Wildcard and multi-type
    // traps both bind an opaque error pointer: the static type is unknown (could
    // be any listed type at runtime), so the handler resolves the concrete type
    // via introspection against the runtime type ID (`e as T`, `typeof(e)`,
    // `typename(e)`) rather than a single concrete type.
    SymbolInfo errorSymbol;
    errorSymbol.name = node->errorName->name;
    if (node->isWildcard || node->isMultiType) {
        errorSymbol.type = nullptr;
    } else {
        errorSymbol.type = node->errorType.get();
    }
    errorSymbol.isConst = true;  // Error binding is immutable (const)
    errorSymbol.ownershipKind = ast::OwnershipKind::MY;  // Error value is owned
    currentScope->add(errorSymbol);

    if (node->isWildcard) {
        VYB_CDBG << "DEBUG: trap clause for wildcard error type" << std::endl;
    } else if (node->isMultiType) {
        VYB_CDBG << "DEBUG: trap clause for multi-type union: ";
        for (size_t i = 0; i < node->errorTypes.size(); ++i) {
            if (i > 0) VYB_CDBG << " | ";
            VYB_CDBG << node->errorTypes[i]->toString();
        }
        VYB_CDBG << std::endl;
    } else if (node->errorType) {
        VYB_CDBG << "DEBUG: trap clause for error type: " << node->errorType->toString() << std::endl;
    }

    // Type check handler block
    if (node->handler) {
        node->handler->accept(*this);
    }

    // Pop error types from trap stack
    if (node->isMultiType) {
        for (size_t i = 0; i < node->errorTypes.size(); ++i) {
            activeTrapTypes.pop_back();
        }
    } else {
        activeTrapTypes.pop_back();
    }
    trapDepth--;

    // Exit trap scope
    exitScope();

    // TODO: Validate that handler return type matches the block expression's expected type
}

void SemanticAnalyzer::visit(ast::EnsureClause* node) {
    // Verify cleanup block is present
    if (!node->cleanupBlock) {
        addError("ensure clause requires a cleanup block", node);
        return;
    }

    VYB_CDBG << "DEBUG: processing ensure clause" << std::endl;

    // Enter new scope for ensure block
    enterScope();

    // Type check cleanup block
    node->cleanupBlock->accept(*this);

    // Exit ensure scope
    exitScope();

    // Note: Cleanup blocks can return values but those are typically ignored
    // They execute for side effects (cleanup resources)
}

void SemanticAnalyzer::visit(ast::RethrowStatement* node) {
    // Verify rethrow is inside a trap clause
    if (trapDepth == 0) {
        addError("rethrow statement can only be used inside a trap clause", node);
        return;
    }

    VYB_CDBG << "DEBUG: rethrow statement at trap depth " << trapDepth << std::endl;

    // If transforming the error, type check the new error expression
    if (node->transformedError) {
        node->transformedError->accept(*this);

        // Get the type of the transformed error
        auto it = expressionTypes.find(node->transformedError.get());
        if (it != expressionTypes.end() && it->second) {
            ast::TypeNode* newErrorType = it->second;
            VYB_CDBG << "DEBUG: rethrow with transformed error type: " << newErrorType->toString() << std::endl;

            // TODO: Verify new error type is compatible with outer trap handlers
        }
    } else {
        // Simple rethrow - propagates current error
        if (!activeTrapTypes.empty()) {
            ast::TypeNode* currentErrorType = activeTrapTypes.back();
            VYB_CDBG << "DEBUG: rethrow current error type: " << currentErrorType->toString() << std::endl;
        }
    }
}

void SemanticAnalyzer::visit(ast::PanicStatement* node) {
    // Verify message expression is present
    if (!node->message) {
        addError("panic statement requires a message expression", node);
        return;
    }

    // Type check the message expression
    node->message->accept(*this);

    // Get the type of the message
    auto it = expressionTypes.find(node->message.get());
    if (it != expressionTypes.end() && it->second) {
        ast::TypeNode* msgType = it->second;

        // Verify message is a string type
        if (auto typeName = dynamic_cast<ast::TypeName*>(msgType)) {
            if (typeName->identifier &&
                (typeName->identifier->name == "String" ||
                 typeName->identifier->name == "str" ||
                 typeName->identifier->name == "string")) {
                VYB_CDBG << "DEBUG: panic statement with string message" << std::endl;
            } else {
                addError("panic message must be a String type, got: " + typeName->toString(), node->message.get());
            }
        } else {
            addError("panic message must be a String type", node->message.get());
        }
    } else {
        addError("Could not determine type of panic message", node->message.get());
    }

    // Note: panic is a noreturn operation - control flow never continues after panic
    // This will be tracked in control flow analysis
}

void SemanticAnalyzer::visit(ast::ExitStatement* node) {
    // Verify exit code expression is present
    if (!node->code) {
        addError("exit statement requires an integer exit code expression", node);
        return;
    }
    // Type check the exit code expression
    node->code->accept(*this);

    // Verify the exit code expression resolves to an integer type
    auto it = expressionTypes.find(node->code.get());
    if (it != expressionTypes.end() && it->second) {
        ast::TypeNode* codeType = it->second;
        if (auto typeName = dynamic_cast<ast::TypeName*>(codeType)) {
            const std::string& name = typeName->identifier ? typeName->identifier->name : "";
            // Accept Int and all sized integer types
            bool isIntType = (name == "Int" || name == "Int8" || name == "Int16" ||
                              name == "Int32" || name == "Int64" || name == "UInt" ||
                              name == "UInt8" || name == "UInt16" || name == "UInt32" ||
                              name == "UInt64");
            if (!isIntType) {
                addError("exit() requires an integer exit code, got: " + typeName->toString(), node->code.get());
            }
        }
        // If we can't determine the type name (e.g. complex expression), allow it through
    }
    // Note: exit is a noreturn operation - control flow never continues after exit
}

void SemanticAnalyzer::visit(ast::DeferStatement* node) {
    if (!node->statement) {
        addError("defer statement requires a body", node);
        return;
    }
    // Validate the deferred statement semantically
    node->statement->accept(*this);
}

void SemanticAnalyzer::visit(ast::TypeNode* node) {
    // This is a base class, specific derived type visitors should be called.
    // If this is ever called directly, it might indicate an issue or a need
    // for a more generic type handling here.
    // For now, it can be a no-op, or log a warning.
    // std::cout << "Warning: SemanticAnalyzer::visit(ast::TypeNode*) called directly for: " << node->toString() << std::endl;
}

void SemanticAnalyzer::visit(ast::TypeName* node) {
    if (!node || !node->identifier) {
        addError("Malformed TypeName node.", node);
        return;
    }
    const std::string& typeNameStr = node->identifier->name;

    // Handle 'Self' type - resolve to current impl type or allow as placeholder
    if (typeNameStr == "Self") {
        if (currentImplType) {
            // We're inside an impl block - resolve Self to the implementing type
            VYB_CDBG << "DEBUG: Resolving Self to " << currentImplType->toString() << std::endl;
            expressionTypes[node] = currentImplType;
            return;
        } else {
            // We're in an aspect declaration - treat Self as a valid placeholder type
            VYB_CDBG << "DEBUG: Self used as placeholder in aspect declaration" << std::endl;
            node->type = std::shared_ptr<ast::TypeNode>(node->clone());
            return;
        }
    }

    if (currentImplType && !currentImplTraitName.empty()) {
        std::string resolvedAssocType = resolveAssociatedTypeReference(
            currentImplType->toString(),
            currentImplTraitName,
            typeNameStr,
            &currentImplAssociatedTypeBindings
        );
        if (!resolvedAssocType.empty()) {
            node->type = std::shared_ptr<ast::TypeNode>(
                new ast::TypeName(node->loc, std::make_unique<ast::Identifier>(node->loc, resolvedAssocType))
            );
            return;
        }
    }

    if (typeNameStr == "Vec") {
        // Convert Vec<T> TypeName to VecType
        if (node->genericArgs.empty() || !node->genericArgs[0]) {
            addError("Vec type requires a type parameter (e.g., Vec<Int>).", node);
            return;
        }
        if (node->genericArgs.size() > 1) {
            addError("Vec type accepts only one type parameter.", node);
            return;
        }

        // Visit the element type
        node->genericArgs[0]->accept(*this);

        // Create a VecType instance
        auto vecType = std::make_unique<ast::VecType>(node->loc, node->genericArgs[0]->clone());
        node->type = std::shared_ptr<ast::TypeNode>(vecType.release());
    } else if (typeNameStr == "Tuple") {
        // Convert Tuple<T, U, ...> TypeName to TupleTypeNode
        if (node->genericArgs.empty()) {
            addError("Tuple type requires at least one type parameter (e.g., Tuple<Int, String>).", node);
            return;
        }

        // Visit all element types
        std::vector<ast::TypeNodePtr> memberTypes;
        for (const auto& argTypeNode : node->genericArgs) {
            if (argTypeNode) {
                argTypeNode->accept(*this);
                memberTypes.push_back(argTypeNode->clone());
            } else {
                addError("Null generic argument in Tuple type.", node);
                return;
            }
        }

        // Create a TupleTypeNode instance
        auto tupleType = std::make_unique<ast::TupleTypeNode>(node->loc, std::move(memberTypes));
        node->type = std::shared_ptr<ast::TypeNode>(tupleType.release());
    } else if (typeNameStr == "Future") {
        // Convert Future<T> TypeName to FutureType
        if (node->genericArgs.empty() || !node->genericArgs[0]) {
            addError("Future type requires a type parameter (e.g., Future<Int>).", node);
            return;
        }
        if (node->genericArgs.size() > 1) {
            addError("Future type accepts only one type parameter.", node);
            return;
        }

        // Visit the result type
        node->genericArgs[0]->accept(*this);

        // Create a FutureType instance
        auto futureType = std::make_unique<ast::FutureType>(node->loc, node->genericArgs[0]->clone());
        node->type = std::shared_ptr<ast::TypeNode>(futureType.release());
    } else if (typeNameStr == "Option") {
        // Built-in generic data enum `Option<T> { Some(T), None }`.
        if (node->genericArgs.size() != 1 || !node->genericArgs[0]) {
            addError("Option type requires exactly one type parameter (e.g., Option<Int>).", node);
            return;
        }
        node->genericArgs[0]->accept(*this);
        registerGenericEnumConcrete("Option", node->toString(), node->genericArgs);
        node->type = std::shared_ptr<ast::TypeNode>(node->clone());
    } else if (typeNameStr == "Result") {
        // Built-in generic data enum `Result<T, E> { Ok(T), Err(E) }`.
        if (node->genericArgs.size() != 2 || !node->genericArgs[0] || !node->genericArgs[1]) {
            addError("Result type requires exactly two type parameters (e.g., Result<Int, String>).", node);
            return;
        }
        node->genericArgs[0]->accept(*this);
        node->genericArgs[1]->accept(*this);
        registerGenericEnumConcrete("Result", node->toString(), node->genericArgs);
        node->type = std::shared_ptr<ast::TypeNode>(node->clone());
    } else if (typeNameStr == "loc" || typeNameStr == "CPtr") {
        if (node->genericArgs.empty() || !node->genericArgs[0]) {
            addError(typeNameStr + " type constructor requires a type parameter (e.g., " + typeNameStr + "<T>).", node);
            return;
        }
        for ( auto& argTypeNode : node->genericArgs ) {
            if (argTypeNode) {
                argTypeNode->accept(*this);
            } else {
                 addError("Null generic argument in loc<T> type.", node);
                 return;
            }
        }
        node->type = std::shared_ptr<ast::TypeNode>(node->clone());
    } else if (enumGenericParamOrder.count(typeNameStr)) {
        // Generic data enum: materialize the concrete payload types (with the
        // given type arguments substituted) for the concrete type string.
        for (auto& arg : node->genericArgs) {
            if (arg) arg->accept(*this);
        }
        registerGenericEnumConcrete(typeNameStr, node->toString(), node->genericArgs);
        node->type = std::shared_ptr<ast::TypeNode>(node->clone());
    } else if (typeNameStr == "i8" || typeNameStr == "i16" || typeNameStr == "i32" || typeNameStr == "i64" ||
               typeNameStr == "u8" || typeNameStr == "u16" || typeNameStr == "u32" || typeNameStr == "u64" ||
               typeNameStr == "f32" || typeNameStr == "f64" ||
               typeNameStr == "bool" || typeNameStr == "Bool" || typeNameStr == "string" || typeNameStr == "void" ||
               typeNameStr == "Int" || typeNameStr == "Float" ||
               typeNameStr == "Int8" || typeNameStr == "Int16" || typeNameStr == "Int32" || typeNameStr == "Int64" ||
               typeNameStr == "UInt8" || typeNameStr == "UInt16" || typeNameStr == "UInt32" || typeNameStr == "UInt64" ||
               typeNameStr == "Float32" || typeNameStr == "Float64" ||
               typeNameStr == "Char" || typeNameStr == "Rune" ||
               typeNameStr == "String" || typeNameStr == "Type" ||
               typeNameStr == "Future" || typeNameStr == "Void" ||
               typeNameStr == "CChar" || typeNameStr == "CUChar" || typeNameStr == "CShort" ||
               typeNameStr == "CUShort" || typeNameStr == "CInt" || typeNameStr == "CUInt" ||
               typeNameStr == "CLong" || typeNameStr == "CULong" || typeNameStr == "CSize" ||
               typeNameStr == "CSSize" || typeNameStr == "CFloat" || typeNameStr == "CDouble" ||
               typeNameStr == "CVoid" || typeNameStr == "CString" ||
               typeNameStr == "my" || typeNameStr == "our" || typeNameStr == "their" ||
               typeNameStr == "mild" || typeNameStr == "view" || typeNameStr == "borrow") {
        node->type = std::shared_ptr<ast::TypeNode>(node->clone());
        // For ownership types, visit generic arguments if present
        for (auto& argTypeNode : node->genericArgs) {
            if (argTypeNode) argTypeNode->accept(*this);
        }
    } else {
        // Check if this is a generic type parameter first
        SymbolInfo* symbol = currentScope->lookup(typeNameStr);
        if (symbol && symbol->kind == SymbolInfo::Kind::TYPE_PARAMETER) {
            // This is a valid generic type parameter (like T, K, V)
            VYB_CDBG << "DEBUG: Recognized generic type parameter: " << typeNameStr << std::endl;
            node->type = std::shared_ptr<ast::TypeNode>(node->clone());
            // Visit generic arguments if present (e.g., Vec<T>)
            for (auto& argTypeNode : node->genericArgs) {
                if (argTypeNode) argTypeNode->accept(*this);
            }
            return;
        }

        // Check if it's a regular type
        if (!symbol || !symbol->type) {
            addError("Unknown type identifier: " + typeNameStr, node);
            return;
        }

        // Clone the base type, but preserve generic arguments from this node
        if (!node->genericArgs.empty()) {
            // This is a parameterized user-defined type (e.g., Box<Int>)
            // Keep the original node with its generic arguments
            node->type = std::shared_ptr<ast::TypeNode>(node->clone());
            // Visit the generic arguments
            for (auto& argTypeNode : node->genericArgs) {
                if (argTypeNode) argTypeNode->accept(*this);
            }
        } else {
            // Non-parameterized type, use the symbol's type
            node->type = std::shared_ptr<ast::TypeNode>(symbol->type->clone());
        }
    }
}
void SemanticAnalyzer::visit(ast::ConstructionExpression* node) {
    if (!node || !node->constructedType) {
        addError("Construction expression is missing the type to construct.", node);
        expressionTypes[node] = nullptr;
        return;
    }
    auto typeNameNode = dynamic_cast<ast::TypeName*>(node->constructedType.get());
    if (!typeNameNode || !typeNameNode->identifier) {
         node->constructedType->accept(*this);
         expressionTypes[node] = node->constructedType->clone().release();
         if (expressionTypes[node]) {
            node->type = std::shared_ptr<ast::TypeNode>(expressionTypes[node]->clone());
         }
         for (auto& arg : node->arguments) {
             if (arg) arg->accept(*this);
         }
         return;
    }
    const std::string& constructedName = typeNameNode->identifier->name;
    // A ConstructionExpression whose "constructed type" is actually a generic
    // function template invoked with explicit type args (e.g. `make_box<Int>(7)`)
    // is a generic function call, not a struct construction. Delegate to the
    // call path so the explicit type args are honored and the return type
    // resolves to the substituted concrete type. The children are moved into a
    // temporary CallExpression for analysis and then moved back, so the shared
    // AST remains intact for the LLVM codegen pass (which re-dispatches this).
    {
        auto fnIt = functionRegistry.find(constructedName);
        if (fnIt != functionRegistry.end() && fnIt->second &&
            !fnIt->second->genericParams.empty() && !typeNameNode->genericArgs.empty()) {
            for (auto& arg : node->arguments) if (arg) arg->accept(*this);
            auto calleeId = std::make_unique<ast::Identifier>(node->loc, constructedName);
            auto callExpr = std::make_unique<ast::CallExpression>(node->loc, std::move(calleeId), std::move(node->arguments));
            callExpr->explicitTypeArgs = std::move(typeNameNode->genericArgs);
            this->visit(static_cast<ast::CallExpression*>(callExpr.get()));
            if (callExpr->type) {
                node->type = callExpr->type;
                expressionTypes[node] = node->type.get();
            }
            // Restore the moved children so the AST is unchanged for codegen.
            node->arguments = std::move(callExpr->arguments);
            typeNameNode->genericArgs = std::move(callExpr->explicitTypeArgs);
            return;
        }
    }
    if (constructedName == "addr") {
        // ... (addr handling logic as before, ensure it's correct) ...
        if (node->arguments.size() != 1 || !node->arguments[0]) {
            addError("addr() intrinsic expects 1 argument.", node); expressionTypes[node] = nullptr; return;
        }
        node->arguments[0]->accept(*this);
        ast::Expression* argExpr = node->arguments[0].get();
        if (!isLValue(argExpr)) {
            addError("Argument to addr() must be an L-value.", node); expressionTypes[node] = nullptr; return;
        }
        auto argTypeIt = expressionTypes.find(argExpr);
        if (argTypeIt == expressionTypes.end() || !argTypeIt->second) {
            addError("Argument to addr() has an unknown type.", node); expressionTypes[node] = nullptr; return;
        }
        auto res_type_ident = std::make_unique<ast::Identifier>(node->loc, "i64");
        ast::TypeNode* resType = new ast::TypeName(node->loc, std::move(res_type_ident));
        expressionTypes[node] = resType;
        node->type = std::shared_ptr<ast::TypeNode>(resType->clone());

    } else if (constructedName == "from") {
        // ... (from handling logic as before, ensure it's correct) ...
        if (typeNameNode->genericArgs.empty() || !typeNameNode->genericArgs[0]) {
            addError("from<TargetType>() intrinsic requires TargetType as a generic argument.", node); expressionTypes[node] = nullptr; return;
        }
        if (node->arguments.size() != 1 || !node->arguments[0]) {
            addError("from<T>() intrinsic expects 1 argument value.", node); expressionTypes[node] = nullptr; return;
        }
        ast::TypeNode* targetTypeAst = typeNameNode->genericArgs[0].get();
        targetTypeAst->accept(*this);
        bool isTargetLocOrPointer = false;
        if (auto tn = dynamic_cast<ast::TypeName*>(targetTypeAst)) {
            if (tn->identifier->name == "loc" && !tn->genericArgs.empty()) isTargetLocOrPointer = true;
        } else if (dynamic_cast<ast::PointerType*>(targetTypeAst)) {
            isTargetLocOrPointer = true;
        }
        if (!isTargetLocOrPointer) {
             addError("Target type in from<T>() must be a location/pointer type (e.g., loc<ActualType>).", node); expressionTypes[node] = nullptr; return;
        }
        node->arguments[0]->accept(*this);
        ast::Expression* addrValExpr = node->arguments[0].get();
        auto addrValTypeIt = expressionTypes.find(addrValExpr);
        ast::TypeNode* addrValType = (addrValTypeIt != expressionTypes.end()) ? addrValTypeIt->second : nullptr;

        bool isAddrValInteger = false;
        if(addrValType) {
            if(auto tn_val = dynamic_cast<ast::TypeName*>(addrValType)) {
                isAddrValInteger = isIntegerType(tn_val);
            }
        }
        if (!isAddrValInteger) {
            addError("Address argument to from<T>() must be an integer type. Got: " + (addrValType ? addrValType->toString() : "unknown"), node);
            expressionTypes[node] = nullptr; return;
        }
        expressionTypes[node] = targetTypeAst->clone().release();
        node->type = std::shared_ptr<ast::TypeNode>(targetTypeAst->clone());

    } else if (constructedName == "at") {
        // ... (at handling logic as before, ensure it's correct) ...
        if (node->arguments.size() != 1 || !node->arguments[0]) {
            addError("at() intrinsic expects 1 argument (the pointer).", node); expressionTypes[node] = nullptr; return;
        }
        node->arguments[0]->accept(*this);
        ast::Expression* ptrExpr = node->arguments[0].get();
        auto ptrTypeIt = expressionTypes.find(ptrExpr);
        if (ptrTypeIt == expressionTypes.end() || !ptrTypeIt->second) {
            addError("Cannot dereference pointer with unknown type using at().", node); expressionTypes[node] = nullptr; return;
        }
        ast::TypeNode* resolvedPointerType = ptrTypeIt->second;
        ast::TypeNode* pointeeType = nullptr;
        if (auto pt = dynamic_cast<ast::PointerType*>(resolvedPointerType)) {
            if (pt->pointeeType) pointeeType = pt->pointeeType->clone().release();
        } else if (auto locTn = dynamic_cast<ast::TypeName*>(resolvedPointerType)) {
            if (locTn->identifier->name == "loc" && !locTn->genericArgs.empty() && locTn->genericArgs[0]) {
                pointeeType = locTn->genericArgs[0]->clone().release();
            } else {
                 addError("Type for at() is \'loc\' but missing type parameter (e.g. loc<T>).", node); expressionTypes[node] = nullptr; return;
            }
        } else {
            addError("Argument to at() must be a pointer or loc<T> type. Got: " + resolvedPointerType->toString(), node);
            expressionTypes[node] = nullptr; return;
        }
        if (!pointeeType) {
             addError("Failed to determine pointee type for at() operation.", node); expressionTypes[node] = nullptr; return;
        }
        expressionTypes[node] = pointeeType;
        node->type = std::shared_ptr<ast::TypeNode>(pointeeType->clone());
    } else {
        node->constructedType->accept(*this);
        expressionTypes[node] = node->constructedType->clone().release();
        if (expressionTypes[node]) {
           node->type = std::shared_ptr<ast::TypeNode>(expressionTypes[node]->clone());
        } else {
            addError("Constructed type " + constructedName + " could not be resolved.", node);
            return;
        }
        for (auto& arg : node->arguments) {
            if (arg) arg->accept(*this);
        }
    }
}
void SemanticAnalyzer::visit(ast::PointerType* node) {
    if (node && node->pointeeType) {
        node->pointeeType->accept(*this);
    }
}
void SemanticAnalyzer::visit(ast::ArrayType* node) {
    if (node && node->elementType) {
        node->elementType->accept(*this);
    }
}

void SemanticAnalyzer::visit(ast::VecType* node) {
    if (node && node->elementType) {
        node->elementType->accept(*this);
    }
}

void SemanticAnalyzer::visit(ast::FutureType* node) {
    if (node && node->resultType) {
        node->resultType->accept(*this);
    }
}

void SemanticAnalyzer::visit(ast::FunctionType* node) {
    if (node) {
        for (auto& paramType : node->parameterTypes) {
            if (paramType) paramType->accept(*this);
        }
        if (node->returnType) {
            node->returnType->accept(*this);
        }
    }
}
void SemanticAnalyzer::visit(ast::OptionalType* node) {
    if (node && node->containedType) {
        node->containedType->accept(*this);
    }
}
void SemanticAnalyzer::visit(ast::GenericParameter* node) {
    // Generic parameters are usually resolved in the context of a generic declaration
    // or instantiation. For a standalone visit, we might try to look it up if it's
    // already defined in the current scope (e.g. within a generic function body).
    // For now, this can be a no-op or a simple lookup.
    // SymbolInfo* symbol = currentScope->lookup(node->name->name); // Assuming GenericParameter has a name field
    // if (!symbol) { addError("Generic parameter " + node->name->name + " not found.", node); }
}

// Normalize LLVM-internal type names to their canonical Vyb surface-type names.
// The LLVM backend accepts e.g. "i32" as an alias for "Int32"; the semantic
// analyzer must honour the same aliases so validation is consistent with codegen.
static std::string normalizeTypeName(const std::string& name) {
    if (name == "i64" || name == "int" || name == "long")  return "Int";
    if (name == "i32" || name == "int32")                  return "Int32";
    if (name == "i16" || name == "int16" || name == "short") return "Int16";
    if (name == "i8"  || name == "int8"  || name == "char") return "Int8";
    if (name == "CChar")                                   return "Int8";
    if (name == "CUChar")                                  return "UInt8";
    if (name == "CShort")                                  return "Int16";
    if (name == "CUShort")                                 return "UInt16";
    if (name == "CInt")                                    return "Int32";
    if (name == "CUInt")                                   return "UInt32";
    if (name == "CLong" || name == "CSSize")               return "Int";
    if (name == "CULong" || name == "CSize")               return "UInt64";
    if (name == "CFloat")                                  return "Float32";
    if (name == "CDouble")                                 return "Float";
    if (name == "CVoid")                                   return "Void";
    if (name == "u64" || name == "uint64")                 return "UInt64";
    if (name == "u32" || name == "uint32")                 return "UInt32";
    if (name == "u16" || name == "uint16")                 return "UInt16";
    if (name == "u8"  || name == "uint8" || name == "byte") return "UInt8";
    if (name == "f64" || name == "double")                 return "Float";
    if (name == "f32" || name == "float")                  return "Float32";
    if (name == "bool")                                    return "Bool";
    if (name == "void")                                    return "Void";
    return name; // already canonical
}

// Implementation for areTypesCompatible
// Checks if valueType can be assigned to targetType
bool SemanticAnalyzer::areTypesCompatible(ast::TypeNode* targetType, ast::TypeNode* valueType) {
    if (!targetType || !valueType) {
        // Both null → same unresolved type; otherwise incompatible
        return (!targetType && !valueType);
    }

    // Resolve type aliases: if either type is a TypeName that's defined as a type alias
    // in the current scope, use the aliased type instead.
    auto resolveAlias = [this](ast::TypeNode* t) -> ast::TypeNode* {
        if (t && t->getCategory() == ast::TypeNode::Category::IDENTIFIER) {
            auto* tn = static_cast<ast::TypeName*>(t);
            if (tn->identifier && tn->genericArgs.empty()) {
                auto* sym = currentScope->lookup(tn->identifier->name);
                if (sym && sym->kind == SymbolInfo::Kind::Type && sym->type) {
                    return sym->type;
                }
            }
        }
        return t;
    };
    ast::TypeNode* resolvedTarget = resolveAlias(targetType);
    ast::TypeNode* resolvedValue = resolveAlias(valueType);
    // If aliases resolved to different nodes, recurse with resolved types
    if (resolvedTarget != targetType || resolvedValue != valueType) {
        return areTypesCompatible(resolvedTarget, resolvedValue);
    }

    // If they are the exact same type object.
    if (targetType == valueType) {
        return true;
    }

    // Ownership wrappers over primitives are transparent: my<Int>, Int, and
    // our<Int> all lower to the same scalar at codegen time.
    ast::TypeNode* unwrappedTarget = unwrapPrimitiveOwnershipType(targetType);
    ast::TypeNode* unwrappedValue = unwrapPrimitiveOwnershipType(valueType);
    if (unwrappedTarget != targetType || unwrappedValue != valueType) {
        return areTypesCompatible(unwrappedTarget, unwrappedValue);
    }

    ast::TypeNode::Category categoryTarget = targetType->getCategory();
    ast::TypeNode::Category categoryValue = valueType->getCategory();

    // Special Case 1: Assigning 'nil' to an Optional type
    if (categoryValue == ast::TypeNode::Category::IDENTIFIER) {
        if (auto* tnValue = dynamic_cast<ast::TypeName*>(valueType)) {
            if (tnValue->identifier && tnValue->identifier->name == "nil") {
                if (categoryTarget == ast::TypeNode::Category::OPTIONAL) {
                    return true;
                }
                // Vyb might also allow assigning 'nil' to raw pointer types.
                // if (categoryTarget == ast::TypeNode::Category::POINTER) return true;
            }
        }
    }

    // Special Case 2: Integer literal (typed as "Int" or "int") can be assigned to any integer type.
    if (categoryTarget == ast::TypeNode::Category::IDENTIFIER &&
        categoryValue == ast::TypeNode::Category::IDENTIFIER) {
        auto* tnTarget = static_cast<ast::TypeName*>(targetType);
        auto* tnValue = static_cast<ast::TypeName*>(valueType);
        if (tnTarget->identifier && tnValue->identifier) {
            bool isSpecificIntTarget = isIntegerType(tnTarget);
            const std::string& valName = tnValue->identifier->name;
            // Integer literals are typed as "Int" (64-bit) in the semantic analyser
            bool isGenericIntValue = (valName == "Int" || valName == "int");
            if (isSpecificIntTarget && isGenericIntValue) {
                return true; // e.g., x<i32> = 10  or  x<Int8> = 255
            }

            // Also allow any integer-alias → any other integer-alias (e.g. i32 → Int32)
            if (isIntegerType(tnTarget) && isIntegerType(tnValue)) {
                return normalizeTypeName(tnTarget->identifier->name) ==
                       normalizeTypeName(tnValue->identifier->name);
            }

            // Float literal (typed as "Float") can be assigned to any float type
            bool isFloatTarget = (tnTarget->identifier->name == "Float" ||
                                  tnTarget->identifier->name == "Float32" ||
                                  tnTarget->identifier->name == "Float64" ||
                                  tnTarget->identifier->name == "f32" ||
                                  tnTarget->identifier->name == "f64");
            bool isGenericFloatValue = (valName == "Float" || valName == "Float64" || valName == "f64");
            if (isFloatTarget && isGenericFloatValue) {
                return true; // e.g., x<Float32> = 3.14
            }

            // Special Case: Ownership wrappers. my<T>, their<T>, our<T>, mild<T> are compatible
            // with the inner type T for initialization (e.g., x<my<Int>> = 42).
            static const std::set<std::string> ownershipWrappers = {"my", "their", "our", "mild", "view"};
            if (tnTarget->identifier && ownershipWrappers.count(tnTarget->identifier->name) > 0
                && tnTarget->genericArgs.size() == 1) {
                // Check if value type is compatible with the inner type
                if (areTypesCompatible(tnTarget->genericArgs[0].get(), valueType)) {
                    return true;
                }
            }
        }
    }

    // If categories are different and not covered by the above special cases,
    // they are generally not compatible without an explicit cast.
    // Exception: if both types produce the same string representation, treat them as
    // compatible. This handles cases like Vec<Int> TypeName vs VecType, Future<T>
    // TypeName vs FutureType, etc., which arise from different paths through the
    // semantic analyzer and parser that produce structurally identical types in
    // different internal representations.
    if (categoryTarget != categoryValue) {
        if (targetType->toString() == valueType->toString()) {
            return true;
        }
        // Cross-category: Tuple<T,U> TypeName vs TupleTypeNode
        auto isTupleTypeName = [](ast::TypeNode* t) -> ast::TypeName* {
            if (t->getCategory() != ast::TypeNode::Category::IDENTIFIER) return nullptr;
            auto* tn = static_cast<ast::TypeName*>(t);
            if (tn->identifier && tn->identifier->name == "Tuple") return tn;
            return nullptr;
        };
        ast::TypeName* tupleTarget = isTupleTypeName(targetType);
        ast::TypeName* tupleValue = isTupleTypeName(valueType);
        if (tupleTarget && categoryValue == ast::TypeNode::Category::TUPLE) {
            auto* tt = static_cast<ast::TupleTypeNode*>(valueType);
            if (tupleTarget->genericArgs.size() == tt->memberTypes.size()) {
                bool ok = true;
                for (size_t i = 0; i < tt->memberTypes.size(); ++i) {
                    if (!areTypesCompatible(tupleTarget->genericArgs[i].get(), tt->memberTypes[i].get())) { ok = false; break; }
                }
                if (ok) return true;
            }
        }
        if (tupleValue && categoryTarget == ast::TypeNode::Category::TUPLE) {
            auto* tt = static_cast<ast::TupleTypeNode*>(targetType);
            if (tupleValue->genericArgs.size() == tt->memberTypes.size()) {
                bool ok = true;
                for (size_t i = 0; i < tt->memberTypes.size(); ++i) {
                    if (!areTypesCompatible(tt->memberTypes[i].get(), tupleValue->genericArgs[i].get())) { ok = false; break; }
                }
                if (ok) return true;
            }
        }
        return false;
    }

    // Categories are the same, proceed with category-specific checks.
    switch (categoryTarget) {
        case ast::TypeNode::Category::IDENTIFIER: {
            auto* tnTarget = static_cast<ast::TypeName*>(targetType);
            auto* tnValue = static_cast<ast::TypeName*>(valueType);
            if (!tnTarget->identifier || !tnValue->identifier) return false;

            // Normalize type names: treat LLVM aliases (i32, i64, …) as their
            // canonical Vyb equivalents (Int32, Int, …) for compatibility checks.
            std::string nameTarget = normalizeTypeName(tnTarget->identifier->name);
            std::string nameValue  = normalizeTypeName(tnValue->identifier->name);

            // For integer types, require the same normalised name.
            // (isIntegerType checks the original name; recheck after normalisation)
            if (nameTarget == nameValue) {
                if (tnTarget->genericArgs.size() != tnValue->genericArgs.size()) return false;
                for (size_t i = 0; i < tnTarget->genericArgs.size(); ++i) {
                    if (!areTypesCompatible(tnTarget->genericArgs[i].get(), tnValue->genericArgs[i].get())) {
                        return false;
                    }
                }
                return true;
            }
            return false;
        }
        case ast::TypeNode::Category::POINTER: {
            auto* ptTarget = static_cast<ast::PointerType*>(targetType);
            auto* ptValue = static_cast<ast::PointerType*>(valueType);
            // T* is compatible with U* if T is compatible with U (invariant for now).
            // Vyb might have rules for void* or covariance/contravariance.
            return areTypesCompatible(ptTarget->pointeeType.get(), ptValue->pointeeType.get());
        }
        case ast::TypeNode::Category::ARRAY: {
            auto* atTarget = static_cast<ast::ArrayType*>(targetType);
            auto* atValue = static_cast<ast::ArrayType*>(valueType);
            // T[N] is compatible with U[M] if T is compatible with U.
            // For simplicity, ignoring size compatibility for now (atTarget->sizeExpression vs atValue->sizeExpression).
            // A full check would compare constant sizes if available.
            return areTypesCompatible(atTarget->elementType.get(), atValue->elementType.get());
        }
        case ast::TypeNode::Category::VEC: {
            auto* vtTarget = static_cast<ast::VecType*>(targetType);
            auto* vtValue = static_cast<ast::VecType*>(valueType);
            // Vec<T> is compatible with Vec<U> if T is compatible with U.
            return areTypesCompatible(vtTarget->elementType.get(), vtValue->elementType.get());
        }
        case ast::TypeNode::Category::FUNCTION: {
            auto* ftTarget = static_cast<ast::FunctionType*>(targetType);
            auto* ftValue = static_cast<ast::FunctionType*>(valueType);

            // Return types: both null (unspecified) counts as compatible
            if (ftTarget->returnType || ftValue->returnType) {
                if (!areTypesCompatible(ftTarget->returnType.get(), ftValue->returnType.get())) return false;
            }

            // Parameter types: invariant comparison; unknown types ("?") are always compatible
            if (ftTarget->parameterTypes.size() != ftValue->parameterTypes.size()) return false;
            for (size_t i = 0; i < ftTarget->parameterTypes.size(); ++i) {
                auto* pt = ftTarget->parameterTypes[i].get();
                auto* pv = ftValue->parameterTypes[i].get();
                // If either side is the "?" placeholder, skip the comparison
                auto* ptName = dynamic_cast<ast::TypeName*>(pt);
                auto* pvName = dynamic_cast<ast::TypeName*>(pv);
                bool targetIsUnknown = ptName && ptName->identifier && ptName->identifier->name == "?";
                bool valueIsUnknown  = pvName && pvName->identifier && pvName->identifier->name == "?";
                if (targetIsUnknown || valueIsUnknown) continue;
                if (!areTypesCompatible(pt, pv)) return false;
            }
            return true;
        }
        case ast::TypeNode::Category::OPTIONAL: {
            auto* otTarget = static_cast<ast::OptionalType*>(targetType);
            auto* otValue = static_cast<ast::OptionalType*>(valueType);
            // Optional<T> is compatible with Optional<U> if T is compatible with U.
            return areTypesCompatible(otTarget->containedType.get(), otValue->containedType.get());
        }
        case ast::TypeNode::Category::TUPLE: {
            auto* ttTarget = static_cast<ast::TupleTypeNode*>(targetType);
            auto* ttValue = static_cast<ast::TupleTypeNode*>(valueType);
            if (ttTarget->memberTypes.size() != ttValue->memberTypes.size()) return false;
            for (size_t i = 0; i < ttTarget->memberTypes.size(); ++i) {
                if (!areTypesCompatible(ttTarget->memberTypes[i].get(), ttValue->memberTypes[i].get())) {
                    return false;
                }
            }
            return true;
        }
        // case ast::TypeNode::Category::STRUCT:
            // Struct compatibility would typically be nominal (same definition) or structural.
            // Nominal is partly handled by IDENTIFIER if struct names are unique and resolved.
            // Structural would require comparing field types and names.
        // case ast::TypeNode::Category::REFERENCE: // Not fully defined in provided AST
        // case ast::TypeNode::Category::SLICE:     // Not fully defined in provided AST
        default:
            // Fallback for unhandled categories or complex types.
            // This is a weak check and ideally should be replaced with more specific rules
            // or by ensuring all types are resolved to canonical forms before comparison.
            if (targetType->toString() == valueType->toString()) { // Basic structural check via string representation
                return true;
            }
            // If they are TypeName, it might have been missed by earlier checks
            if (categoryTarget == ast::TypeNode::Category::IDENTIFIER) {
                 auto* tnTarget = static_cast<ast::TypeName*>(targetType);
                 auto* tnValue = static_cast<ast::TypeName*>(valueType); // Already know category is IDENTIFIER
                 if (tnTarget->identifier && tnValue->identifier && tnTarget->identifier->name == tnValue->identifier->name) {
                     if (tnTarget->genericArgs.size() == tnValue->genericArgs.size()) {
                         bool allArgsCompatible = true;
                         for (size_t i = 0; i < tnTarget->genericArgs.size(); ++i) {
                            if (!areTypesCompatible(tnTarget->genericArgs[i].get(), tnValue->genericArgs[i].get())) {
                                allArgsCompatible = false;
                                break;
                            }
                         }
                         if (allArgsCompatible) return true;
                     }
                 }
            }
            return false;
    }
    return false; // Should be unreachable if all cases are handled
}

// ... ensure other visit methods like visit(ast::FromIntToLocExpression* node) are correctly implemented or placeholder ...
// The provided snippets for AddrOfExpression, PointerDerefExpression are now primary.
// The ConstructionExpression handles cases where these might be parsed as such.
// If the parser correctly creates AddrOfExpression, FromIntToLocExpression, PointerDerefExpression,
// then the specific visitors for those AST nodes will be used. The logic for 'addr', 'from', 'at'
// in ConstructionExpression::visit is a fallback or handles cases where the parser uses
// ConstructionExpression for these intrinsics.

// --- Consolidated implementations from semantic_*.cpp files ---

// From semantic_tuple_type.cpp
void SemanticAnalyzer::visit(ast::TupleTypeNode* node) {
    // Basic implementation - verify each element of the tuple type
    for (auto& element : node->memberTypes) {
        if (element) {
            element->accept(*this);
        } else {
            // Report error for null element type
            errors.push_back("Tuple type contains a null element type at " +
                              node->loc.toString());
        }
    }
}

bool SemanticAnalyzer::isRawLocationType(ast::TypeNode* type) {
    // Determine if the type is a raw location type (loc<T>)
    if (auto* typeName = dynamic_cast<ast::TypeName*>(type)) {
        return typeName->identifier && typeName->identifier->name == "loc" &&
               !typeName->genericArgs.empty();
       }
    return false;
}

// From semantic_if_expr.cpp
void SemanticAnalyzer::visit(ast::IfExpression* node) {
    // Check the condition - must be a boolean expression
    if (node->condition) {
        node->condition->accept(*this);
        // In a full implementation, we would check if the condition's type is boolean
        // For now, we won't do type checking
    } else {
        addError("If expression condition missing", node);
    }

    // Check the then branch
    if (node->thenBranch) {
        node->thenBranch->accept(*this);
    } else {
        addError("If expression then branch missing", node);
    }

    // Check the else branch (required in if expressions)
    if (node->elseBranch) {
        node->elseBranch->accept(*this);
    } else {
        addError("If expression else branch is required", node);
    }

    // For a full implementation:
    // 1. Check that condition has boolean type
    // 2. Check that then and else branches have compatible types
    // 3. Set the if-expression's type to the common type of then and else

    // For now we skip the type checking
}

// From semantic_borrow_expr.cpp
void SemanticAnalyzer::visit(ast::BorrowExpression* node) {
    // First analyze the expression that's being borrowed
    if (node->expression) {
        node->expression->accept(*this);
    }

    // Ensure that the borrowed expression is valid for borrowing
    if (!isLValue(node->expression.get())) {
        addError("Cannot borrow non-lvalue expression", node);
        expressionTypes[node] = nullptr;
        return;
    }

    auto exprTypeIt = expressionTypes.find(node->expression.get());
    ast::TypeNode* exprType = (exprTypeIt != expressionTypes.end()) ? exprTypeIt->second : nullptr;
    if (!exprType) {
        addError("Cannot determine type of borrowed expression", node);
        expressionTypes[node] = nullptr;
        return;
    }

    ast::TypeNodePtr innerType;
    if (auto* typeName = dynamic_cast<ast::TypeName*>(exprType)) {
        if (typeName->identifier &&
            (typeName->identifier->name == "my" || typeName->identifier->name == "our") &&
            typeName->genericArgs.size() == 1) {
            innerType = typeName->genericArgs[0]->clone();
        }
    }
    if (!innerType) {
        innerType = exprType->clone();
    }

    auto theirId = std::make_unique<ast::Identifier>(node->loc, "their");
    std::vector<ast::TypeNodePtr> args;
    args.push_back(std::move(innerType));
    ast::TypeNode* resultType = new ast::TypeName(node->loc, std::move(theirId), std::move(args));
    expressionTypes[node] = resultType;
    node->type = std::shared_ptr<ast::TypeNode>(resultType->clone().release());
}

// From semantic_array_init_expr.cpp
void SemanticAnalyzer::visit(ast::ArrayInitializationExpression* node) {
    // Check that the element type is valid
    if (node->elementType) {
        node->elementType->accept(*this);
    } else {
        addError("Array initialization missing element type", node);
    }

    // Check that the size expression is valid
    if (node->sizeExpression) {
        node->sizeExpression->accept(*this);
        // In a full implementation, we would check if the size expression evaluates to an integer type
        // For now, we won't do type checking
    } else {
        addError("Array initialization missing size expression", node);
    }

    // For a full implementation:
    // 1. Check that the size expression is of integer type
    // 2. Set the array initialization expression's type to an array type with the element type

    // For now we skip the advanced type checking
}

// Template management method implementations
void SemanticAnalyzer::registerTemplate(std::unique_ptr<ast::TemplateDeclaration> templateDecl) {
    if (!templateDecl || !templateDecl->name) {
        return;
    }

    const std::string& templateName = templateDecl->name->name;
    auto templateInfo = std::make_unique<TemplateInfo>(std::move(templateDecl));
    templateRegistry[templateName] = std::move(templateInfo);
}

SemanticAnalyzer::TemplateInfo* SemanticAnalyzer::findTemplate(const std::string& templateName) {
    auto it = templateRegistry.find(templateName);
    if (it != templateRegistry.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool SemanticAnalyzer::isTemplateInstantiation(const std::string& name) {
    // Check if this looks like a template instantiation (contains '<' and '>')
    return name.find('<') != std::string::npos && name.find('>') != std::string::npos;
}

std::unique_ptr<ast::Declaration> SemanticAnalyzer::instantiateTemplate(
    const std::string& templateName,
    const std::vector<std::string>& typeArgs) {

    TemplateInfo* templateInfo = findTemplate(templateName);
    if (!templateInfo) {
        return nullptr;
    }

    // Validate type argument count
    if (typeArgs.size() != templateInfo->parameterNames.size()) {
        return nullptr;
    }

    // TODO: Implement actual monomorphization here
    // For now, return nullptr to indicate "not yet implemented"
    // This is where we would:
    // 1. Clone the template body
    // 2. Substitute type parameters with concrete types
    // 3. Generate specialized AST nodes
    // 4. Return the instantiated declaration

    return nullptr;
}

void SemanticAnalyzer::handleTemplateInstantiation(ast::Identifier* identifier,
                                                  const std::vector<ast::TypeNodePtr>& typeArgs,
                                                  ast::GenericInstantiationExpression* node) {
    if (!identifier) return;

    std::string templateName = identifier->name;
    TemplateInfo* templateInfo = findTemplate(templateName);

    if (!templateInfo) {
        addError("Template '" + templateName + "' not found.", node);
        return;
    }

    // Validate type argument count
    if (typeArgs.size() != templateInfo->parameterNames.size()) {
        addError("Template '" + templateName + "' expects " +
                std::to_string(templateInfo->parameterNames.size()) +
                " type arguments, got " + std::to_string(typeArgs.size()) + ".", node);
        return;
    }

    // Convert type arguments to string representations for substitution
    std::vector<std::string> concreteTypes;
    for (const auto& typeArg : typeArgs) {
        if (typeArg) {
            concreteTypes.push_back(typeArg->toString());
        } else {
            addError("Invalid type argument in template instantiation.", node);
            return;
        }
    }

    // Validate template constraints before monomorphization
    if (!validateTemplateConstraints(templateInfo, concreteTypes, node)) {
        return; // Error already reported
    }

    // Perform monomorphization
    auto instantiated = performMonomorphization(templateInfo, concreteTypes);
    if (instantiated) {
        // Store the instantiated template for later use in codegen
        // For now, just mark that we successfully processed it
        expressionTypes[node] = nullptr; // Will be properly typed later
    }
}

void SemanticAnalyzer::handleMemberTemplateInstantiation(ast::MemberExpression* memberExpr,
                                                        const std::vector<ast::TypeNodePtr>& typeArgs,
                                                        ast::GenericInstantiationExpression* node) {
    // Handle cases like Container<Int>::create
    if (!memberExpr || !memberExpr->object) {
        addError("Invalid member template instantiation.", node);
        return;
    }

    // For now, delegate to regular template instantiation
    // In a full implementation, this would handle method templates and nested templates
    if (auto objectId = dynamic_cast<ast::Identifier*>(memberExpr->object.get())) {
        handleTemplateInstantiation(objectId, typeArgs, node);
    } else {
        addError("Complex member template instantiation not yet supported.", node);
    }
}

std::unique_ptr<ast::Declaration> SemanticAnalyzer::performMonomorphization(TemplateInfo* templateInfo,
                                                                           const std::vector<std::string>& concreteTypes) {
    if (!templateInfo || !templateInfo->declaration) {
        return nullptr;
    }

    // Generate a unique key for this instantiation
    std::string instanceKey = templateInfo->templateName + "<";
    for (size_t i = 0; i < concreteTypes.size(); ++i) {
        if (i > 0) instanceKey += ",";
        instanceKey += concreteTypes[i];
    }
    instanceKey += ">";

    // For now, just return nullptr to indicate "monomorphization not yet implemented"
    // In a full implementation, this would:
    // 1. Clone the template body AST
    // 2. Substitute type parameters with concrete types
    // 3. Cache the result for reuse
    // 4. Return the instantiated declaration

    return nullptr;
}

std::unique_ptr<ast::Declaration> SemanticAnalyzer::cloneAndSubstituteAST(ast::Declaration* templateBody,
                                                                         const std::vector<std::string>& genericParams,
                                                                         const std::vector<std::string>& concreteTypes) {
    if (!templateBody) {
        return nullptr;
    }

    // Placeholder implementation - proper AST cloning would require:
    // 1. Deep clone the AST structure
    // 2. Walk through all TypeName nodes in the cloned AST
    // 3. Replace generic parameter names with concrete types
    // 4. Update function signatures, struct fields, etc.
    // 5. Generate specialized mangled names

    return nullptr;
}

// Template constraint validation methods
bool SemanticAnalyzer::validateTemplateConstraints(TemplateInfo* templateInfo,
                                                  const std::vector<std::string>& concreteTypes,
                                                  ast::GenericInstantiationExpression* node) {
    if (!templateInfo || concreteTypes.size() != templateInfo->parameterNames.size()) {
        return false;
    }

    // Validate each type argument against its constraints
    for (size_t i = 0; i < concreteTypes.size(); ++i) {
        const std::string& concreteType = concreteTypes[i];
        const std::vector<std::string>& constraints = templateInfo->parameterConstraints[i];

        // Check each trait constraint for this type parameter
        for (const std::string& traitName : constraints) {
            if (!typeImplementsTrait(concreteType, traitName)) {
                addError("Type '" + concreteType + "' does not implement required trait '" +
                        traitName + "' for template parameter '" +
                        templateInfo->parameterNames[i] + "'.", node);
                return false;
            }
        }
    }

    return true;
}

bool SemanticAnalyzer::typeImplementsTrait(const std::string& typeName, const std::string& traitName) {
    // Check if the type implements the specified trait

    // First, check for built-in trait implementations
    if (isBuiltinTypeCompatible(typeName, traitName)) {
        return true;
    }

    // Look up user-defined trait implementations
    auto typeIt = traitImpls.find(typeName);
    if (typeIt != traitImpls.end()) {
        auto traitIt = typeIt->second.find(traitName);
        if (traitIt != typeIt->second.end()) {
            // Found an impl block for this type and trait
            return true;
        }
    }

    // TODO: Handle generic aspect implementations (bind<T> Aspect -> Type<T>)

    return false;
}

std::vector<std::string> SemanticAnalyzer::getImplementedTraits(const std::string& typeName) {
    std::vector<std::string> traits;

    // Add built-in trait implementations
    if (typeName == "Int" || typeName == "Int32" || typeName == "Float" || typeName == "Float32" || typeName == "Char") {
        traits.push_back("Comparable");
        traits.push_back("Equatable");
        if (typeName == "Int" || typeName == "Int32" || typeName == "Float" || typeName == "Float32") {
            traits.push_back("Numeric");
        }
    }

    if (typeName == "String" || typeName == "Char") {
        traits.push_back("Equatable");
        if (typeName == "String") {
            traits.push_back("Comparable");
        }
    }

    if (typeName == "Bool") {
        traits.push_back("Equatable");
    }

    // Look up user-defined trait implementations
    auto typeIt = traitImpls.find(typeName);
    if (typeIt != traitImpls.end()) {
        for (const auto& traitEntry : typeIt->second) {
            traits.push_back(traitEntry.first);
        }
    }

    return traits;
}

bool SemanticAnalyzer::isBuiltinTypeCompatible(const std::string& typeName, const std::string& traitName) {
    // Check built-in type and trait compatibility
    if (traitName == "Comparable") {
        return typeName == "Int" || typeName == "Int32" || typeName == "Float" || typeName == "Float32" || typeName == "Char" || typeName == "String";
    }

    if (traitName == "Equatable") {
        return typeName == "Int" || typeName == "Int32" || typeName == "Float" || typeName == "Float32" || typeName == "Char" ||
               typeName == "String" || typeName == "Bool";
    }

    if (traitName == "Numeric") {
        return typeName == "Int" || typeName == "Int32" || typeName == "Float" || typeName == "Float32";
    }

    if (traitName == "Hashable") {
        return typeName == "Int" || typeName == "String" || typeName == "Char" || typeName == "Bool";
    }

    return false;
}

void SemanticAnalyzer::handleQualifiedAspectCall(ast::CallExpression* node,
                                                  const std::string& aspectName,
                                                  const std::string& methodName) {
    if (node->arguments.empty()) {
        addError("Qualified aspect call " + aspectName + "::" + methodName +
                 "() requires a receiver as the first argument.", node);
        return;
    }

    // Visit the receiver to resolve its type.
    ast::Expression* receiver = node->arguments[0].get();
    if (receiver) receiver->accept(*this);

    ast::TypeNode* receiverType = nullptr;
    if (receiver) {
        auto it = expressionTypes.find(receiver);
        if (it != expressionTypes.end() && it->second) receiverType = it->second;
        if (!receiverType && receiver->type) receiverType = receiver->type.get();
    }
    if (!receiverType) {
        addError("Cannot determine the type of the receiver argument to " +
                 aspectName + "::" + methodName + "().", node);
        return;
    }

    // Visit the remaining arguments for typing.
    for (size_t i = 1; i < node->arguments.size(); ++i) {
        if (node->arguments[i]) node->arguments[i]->accept(*this);
    }

    const std::string typeStr = receiverType->toString();

    TraitInfo* traitInfo = findTrait(aspectName);
    if (!traitInfo) {
        addError("Aspect '" + aspectName + "' is not defined.", node);
        return;
    }

    // A receiver may be a bounded type parameter (e.g. thing<T> where T has a
    // Display bound). Such a receiver has no concrete bind entry; the return
    // type is resolved from the bound aspect's declared method signature.
    bool isBoundTypeParameter = false;
    if (SymbolInfo* sym = currentScope->lookup(typeStr)) {
        if (sym->kind == SymbolInfo::Kind::TYPE_PARAMETER) {
            for (const std::string& bound : sym->bounds) {
                if (boundAspectProvides(bound, aspectName)) {
                    isBoundTypeParameter = true;
                    break;
                }
            }
        }
    }

    // Locate an explicit bind implementation for this concrete type.
    ast::FunctionDeclaration* methodDecl = nullptr;
    bool typeMatchesImpl = false;
    if (!isBoundTypeParameter) {
        auto typeImplsIt = traitImpls.find(typeStr);
        if (typeImplsIt != traitImpls.end()) {
            auto traitIt = typeImplsIt->second.find(aspectName);
            if (traitIt != typeImplsIt->second.end()) {
                typeMatchesImpl = true;
                for (ast::FunctionDeclaration* m : traitIt->second) {
                    if (m && m->id && m->id->name == methodName) {
                        methodDecl = m;
                        break;
                    }
                }
            }
        }

        // Fall back to a generic impl pattern (e.g., bind<T> Container -> Vec<T>).
        if (!typeMatchesImpl) {
            for (const auto& typeEntry : genericTraitImpls) {
                if (!matchesPattern(typeStr, typeEntry.first)) continue;
                const auto& traitMap = typeEntry.second;
                auto traitIt = traitMap.find(aspectName);
                if (traitIt == traitMap.end()) continue;
                const GenericImplInfo* implInfo = traitIt->second.get();
                if (!implInfo || !implInfo->declaration) continue;
                typeMatchesImpl = true;
                for (const auto& m : implInfo->declaration->methods) {
                    if (m && m->id && m->id->name == methodName) {
                        methodDecl = m.get();
                        break;
                    }
                }
                break;
            }
        }
    }

    // Determine whether the method is declared by the aspect itself or any of
    // its transitive super-aspects (inherited methods are dispatchable too).
    const TraitInfo* declaringAspect = nullptr;
    const TraitMethod* declaredMethod = nullptr;
    findAspectMethod(aspectName, methodName, declaringAspect, declaredMethod);
    if (!declaredMethod && !methodDecl) {
        addError("Aspect '" + aspectName + "' does not define a method named '" +
                 methodName + "'.", node);
        return;
    }
    if (!isBoundTypeParameter && !typeMatchesImpl) {
        addError("Type '" + typeStr + "' does not implement aspect '" + aspectName +
                 "' (no bind found).", node);
        return;
    }

    ast::TypeNode* returnTypeNode = methodDecl ? methodDecl->returnTypeNode.get()
                                               : declaredMethod->returnType;
    if (!returnTypeNode) {
        addError("Method '" + methodName + "' in aspect '" + aspectName +
                 "' has no return type.", node);
        return;
    }

    ast::TypeNode* actualReturnType = substituteSelfType(returnTypeNode, typeStr);
    expressionTypes[node] = actualReturnType;
    node->type = std::shared_ptr<ast::TypeNode>(actualReturnType->clone());
    VYB_CDBG << "DEBUG: Qualified aspect call " << aspectName << "::" << methodName
              << " on " << typeStr << " returns " << actualReturnType->toString()
              << std::endl;
}

void SemanticAnalyzer::handleVecMethodCall(ast::CallExpression* node, const std::string& objectName, const std::string& methodName) {
    // Validate that the object is a Vec type and check constness
    // Look up the object in the symbol table to check mutability
    SymbolInfo* objSymbol = currentScope->lookup(objectName);
    bool isConstVec = false;
    bool isTheirVec = false;

    if (objSymbol) {
        // Check if the variable is const or has ownership constraints
        if (objSymbol->isConst) {
            isConstVec = true;
        }
        // Check for 'their' ownership (borrowed reference)
        if (objSymbol->ownershipKind == ast::OwnershipKind::THEIR) {
            isTheirVec = true;
        }
    }

    if (methodName == "push") {
        // push(element) -> Vec<T> (for chaining)
        if (node->arguments.size() != 1) {
            addError("Vec::push expects exactly 1 argument", node);
            return;
        }
        // Check constness - push is a mutating operation
        if (isConstVec) {
            addError("Cannot call mutating method 'push' on const Vec: " + objectName, node);
            return;
        }
        // Return type is the Vec itself for chaining
        // In full implementation, get element type from Vec<T>
        auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
        auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
        auto vecType = std::make_unique<ast::VecType>(node->loc, std::move(intType));
        expressionTypes[node] = vecType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(vecType));

    } else if (methodName == "pop") {
        // pop() -> T (element type)
        if (node->arguments.size() != 0) {
            addError("Vec::pop expects no arguments", node);
            return;
        }
        // Check constness - pop is a mutating operation
        if (isConstVec) {
            addError("Cannot call mutating method 'pop' on const Vec: " + objectName, node);
            return;
        }
        // Return element type
        auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
        auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
        expressionTypes[node] = intType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(intType));

    } else if (methodName == "len") {
        // len() -> Int
        if (node->arguments.size() != 0) {
            addError("Vec::len expects no arguments", node);
            return;
        }
        auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
        auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
        expressionTypes[node] = intType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(intType));

    } else if (methodName == "get") {
        // get(index) -> T (element type)
        if (node->arguments.size() != 1) {
            addError("Vec::get expects exactly 1 argument (index)", node);
            return;
        }
        // Validate index is integer type
        if (node->arguments[0]) {
            node->arguments[0]->accept(*this);
            // TODO: Check that argument is Int type
        }

        // Extract element type from Vec<T>
        // Look up the object in the symbol table to get its Vec type
        SymbolInfo* objSymbol = currentScope->lookup(objectName);
        if (objSymbol && objSymbol->type) {
            // Check if the type is a VecType
            if (auto* vecType = dynamic_cast<ast::VecType*>(objSymbol->type)) {
                // Clone the element type for the return type
                if (vecType->elementType) {
                    // Deep clone the element type node
                    std::shared_ptr<ast::TypeNode> clonedElementType = cloneTypeNode(vecType->elementType.get());
                    expressionTypes[node] = clonedElementType.get();
                    node->type = clonedElementType;
                    return;
                }
            }
            // Also handle TypeName "Vec<T>" (e.g., function parameters)
            if (auto* typeName = dynamic_cast<ast::TypeName*>(objSymbol->type)) {
                if (typeName->identifier && typeName->identifier->name == "Vec" && !typeName->genericArgs.empty()) {
                    if (typeName->genericArgs[0]) {
                        std::shared_ptr<ast::TypeNode> clonedElementType = typeName->genericArgs[0]->clone();
                        expressionTypes[node] = clonedElementType.get();
                        node->type = clonedElementType;
                        return;
                    }
                }
            }
        }

        // Fallback to Int if we couldn't determine the element type
        auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
        auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
        expressionTypes[node] = intType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(intType));

    } else if (methodName == "push_array") {
        // push_array(array) -> Vec<T> (for chaining)
        if (node->arguments.size() != 1) {
            addError("Vec::push_array expects exactly 1 argument (array)", node);
            return;
        }
        // Check constness - push_array is a mutating operation
        if (isConstVec) {
            addError("Cannot call mutating method 'push_array' on const Vec: " + objectName, node);
            return;
        }
        // TODO: Validate argument is array type [T; N] compatible with Vec<T>
        // Return Vec type for chaining
        auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
        auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
        auto vecType = std::make_unique<ast::VecType>(node->loc, std::move(intType));
        expressionTypes[node] = vecType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(vecType));

    } else if (methodName == "to_array") {
        // to_array(size) -> [T; N]
        if (node->arguments.size() != 1) {
            addError("Vec::to_array expects exactly 1 argument (array size)", node);
            return;
        }
        // TODO: Validate size argument is integer
        // Return array type [T; N]
        auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
        auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
        auto arrayType = std::make_unique<ast::ArrayType>(node->loc, std::move(intType), nullptr);
        expressionTypes[node] = arrayType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(arrayType));

    } else if (methodName == "clear") {
        // clear() -> void
        if (node->arguments.size() != 0) {
            addError("Vec::clear expects no arguments", node);
            return;
        }
        // Check constness - clear is a mutating operation
        if (isConstVec) {
            addError("Cannot call mutating method 'clear' on const Vec: " + objectName, node);
            return;
        }
        // Return void (no type)
        node->type = nullptr;

    } else if (methodName == "is_empty") {
        // is_empty() -> Bool
        if (node->arguments.size() != 0) {
            addError("Vec::is_empty expects no arguments", node);
            return;
        }
        auto boolId = std::make_unique<ast::Identifier>(node->loc, "Bool");
        auto boolType = std::make_unique<ast::TypeName>(node->loc, std::move(boolId));
        expressionTypes[node] = boolType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(boolType));

    } else if (methodName == "capacity") {
        // capacity() -> Int
        if (node->arguments.size() != 0) {
            addError("Vec::capacity expects no arguments", node);
            return;
        }
        auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
        auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
        expressionTypes[node] = intType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(intType));

    } else if (methodName == "concat") {
        // concat(other_vec) -> Vec<T> (for chaining)
        if (node->arguments.size() != 1) {
            addError("Vec::concat expects exactly 1 argument (other Vec)", node);
            return;
        }
        // TODO: Validate argument is compatible Vec<T> type
        auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
        auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
        auto vecType = std::make_unique<ast::VecType>(node->loc, std::move(intType));
        expressionTypes[node] = vecType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(vecType));

    } else if (methodName == "contains") {
        // contains(value) -> Bool
        if (node->arguments.size() != 1) {
            addError("Vec::contains expects exactly 1 argument (value to search)", node);
            return;
        }
        // TODO: Validate argument is compatible with element type T
        auto boolId = std::make_unique<ast::Identifier>(node->loc, "Bool");
        auto boolType = std::make_unique<ast::TypeName>(node->loc, std::move(boolId));
        expressionTypes[node] = boolType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(boolType));

    } else if (methodName == "remove_at") {
        // remove_at(index) -> T (removed element)
        if (node->arguments.size() != 1) {
            addError("Vec::remove_at expects exactly 1 argument (index)", node);
            return;
        }
        // Check constness - remove_at is a mutating operation
        if (isConstVec) {
            addError("Cannot call mutating method 'remove_at' on const Vec: " + objectName, node);
            return;
        }
        // TODO: Validate index is Int type
        // Return element type
        auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
        auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
        expressionTypes[node] = intType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(intType));

    } else if (methodName == "get_array") {
        // get_array(pre_allocated_array) -> Int (number of elements copied)
        if (node->arguments.size() != 1) {
            addError("Vec::get_array expects exactly 1 argument (pre-allocated array)", node);
            return;
        }
        // get_array is read-only, so it's allowed on const/their Vecs
        // TODO: Validate argument is array type [T; N] compatible with Vec<T>
        // Return Int (number of elements copied for efficiency feedback)
        auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
        auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
        expressionTypes[node] = intType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(intType));

    } else if (methodName == "get_vec") {
        // get_vec(target_vec) -> Int (number of elements copied)
        // Extracts contents from any Vec into target Vec, respecting constness
        if (node->arguments.size() != 1) {
            addError("Vec::get_vec expects exactly 1 argument (target Vec)", node);
            return;
        }
        // get_vec is read-only on source Vec (works with all ownership types: MY, OUR, THEIR, PTR)
        // constness is automatically respected since this is a read-only operation
        // TODO: Validate argument is Vec<T> type compatible with source Vec<T>
        // Return Int (number of elements copied)
        auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
        auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
        expressionTypes[node] = intType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(intType));

    } else {
        addError("Unknown Vec method: " + methodName, node);
    }
}

void SemanticAnalyzer::handleVecMethodCallOnMember(ast::CallExpression* node, ast::VecType* vecType, const std::string& methodName) {
    // Handle Vec method calls on member expressions (e.g., tree.nodes.push())
    // This variant doesn't need to look up the object in the symbol table since we already have the type

    // Get the element type from the Vec<T>
    ast::TypeNode* elementType = vecType->elementType.get();

    // IMPORTANT: vecType may point to a temporary unique_ptr in the caller (e.g. tempVecType).
    // To avoid use-after-free when that temporary is destroyed, always clone types into node->type
    // FIRST, then store node->type.get() in expressionTypes so the pointer is stable.

    if (methodName == "push") {
        // push(element) -> Vec<T> (for chaining)
        if (node->arguments.size() != 1) {
            addError("Vec::push expects exactly 1 argument", node);
            return;
        }
        // Accept the argument to ensure it gets analyzed
        node->arguments[0]->accept(*this);

        // Clone type into node->type first, then store stable pointer
        node->type = std::shared_ptr<ast::TypeNode>(vecType->clone());
        expressionTypes[node] = node->type.get();

    } else if (methodName == "pop") {
        // pop() -> T (element type)
        if (node->arguments.size() != 0) {
            addError("Vec::pop expects no arguments", node);
            return;
        }
        // Return element type
        if (elementType) {
            node->type = std::shared_ptr<ast::TypeNode>(elementType->clone());
            expressionTypes[node] = node->type.get();
        }

    } else if (methodName == "len") {
        // len() -> Int
        if (node->arguments.size() != 0) {
            addError("Vec::len expects no arguments", node);
            return;
        }
        auto intId = std::make_unique<ast::Identifier>(node->loc, "Int");
        auto intType = std::make_unique<ast::TypeName>(node->loc, std::move(intId));
        expressionTypes[node] = intType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(intType));

    } else if (methodName == "get") {
        // get(index) -> T (element type)
        if (node->arguments.size() != 1) {
            addError("Vec::get expects exactly 1 argument (index)", node);
            return;
        }
        // Validate index argument
        if (node->arguments[0]) {
            node->arguments[0]->accept(*this);
        }
        // Return element type
        if (elementType) {
            node->type = std::shared_ptr<ast::TypeNode>(elementType->clone());
            expressionTypes[node] = node->type.get();
        }

    } else if (methodName == "clear") {
        // clear() -> void
        if (node->arguments.size() != 0) {
            addError("Vec::clear expects no arguments", node);
            return;
        }
        node->type = nullptr;

    } else if (methodName == "is_empty") {
        // is_empty() -> Bool
        if (node->arguments.size() != 0) {
            addError("Vec::is_empty expects no arguments", node);
            return;
        }
        auto boolId = std::make_unique<ast::Identifier>(node->loc, "Bool");
        auto boolType = std::make_unique<ast::TypeName>(node->loc, std::move(boolId));
        expressionTypes[node] = boolType.get();
        node->type = std::shared_ptr<ast::TypeNode>(std::move(boolType));

    } else {
        addError("Unknown Vec method: " + methodName, node);
    }
}

// ===== Aspect Management Methods =====

void SemanticAnalyzer::registerTrait(ast::AspectDeclaration* traitDecl) {
    if (!traitDecl || !traitDecl->name) {
        return;
    }

    auto traitInfo = std::make_unique<TraitInfo>(traitDecl);
    const std::string& traitName = traitInfo->name;

    traitRegistry[traitName] = std::move(traitInfo);
}

TraitInfo* SemanticAnalyzer::findTrait(const std::string& traitName) {
    auto it = traitRegistry.find(traitName);
    if (it != traitRegistry.end()) {
        return it->second.get();
    }
    return nullptr;
}

void SemanticAnalyzer::registerTraitImpl(ast::BindDeclaration* implDecl) {
    if (!implDecl || !implDecl->selfType || !implDecl->traitType) {
        return;
    }

    std::string typeName = implDecl->selfType->toString();
    std::string traitName = implDecl->traitType->toString();

    // Check if this is a generic implementation (has type parameters)
    bool isGeneric = !implDecl->genericParams.empty();

    if (isGeneric) {
        // Store generic implementation separately
        auto genericInfo = std::make_unique<GenericImplInfo>(implDecl);
        bool newIsBounded = genericInfo->isBounded;

        // Bind selection precedence: when both a bounded and an unbounded generic
        // bind exist for the same trait and type shape, the bounded (more
        // specialized) bind wins regardless of declaration order.
        auto& traitMap = genericTraitImpls[typeName];
        auto existing = traitMap.find(traitName);
        if (existing != traitMap.end()) {
            if (!newIsBounded && existing->second->isBounded) {
                VYB_CDBG << "DEBUG: Keeping bounded generic trait impl for " << traitName
                         << " " << typeName << " over unbounded duplicate" << std::endl;
                return;
            }
        }

        VYB_CDBG << "DEBUG: Storing generic trait impl: " << traitName << " for " << typeName << std::endl;
        traitMap[traitName] = std::move(genericInfo);
    } else {
        // Store concrete implementation
        VYB_CDBG << "DEBUG: Storing concrete trait impl: " << traitName << " for " << typeName << std::endl;

        std::vector<ast::FunctionDeclaration*> implMethods;
        for (const auto& method : implDecl->methods) {
            if (method) {
                implMethods.push_back(method.get());
            }
        }

        traitImpls[typeName][traitName] = implMethods;

        auto& associatedMap = traitAssociatedTypeImpls[typeName][traitName];
        associatedMap.clear();
        for (const auto& assocBinding : implDecl->associatedTypeBindings) {
            if (assocBinding.name && assocBinding.valueType) {
                associatedMap[assocBinding.name->name] = assocBinding.valueType.get();
            }
        }

        // Fill in aspect-declared default types for any associated types the
        // bind did not explicitly assign.
        auto traitIt = traitRegistry.find(traitName);
        if (traitIt != traitRegistry.end() && traitIt->second) {
            for (const auto& declaredAssoc : traitIt->second->associatedTypes) {
                if (associatedMap.find(declaredAssoc) != associatedMap.end()) {
                    continue;
                }
                ast::TypeNode* assocDefault = traitIt->second->getAssociatedTypeDefault(declaredAssoc);
                if (assocDefault) {
                    associatedMap[declaredAssoc] = assocDefault;
                }
            }
        }
    }
}

bool SemanticAnalyzer::boundAspectProvides(const std::string& boundAspect,
                                            const std::string& requestedAspect) {
    if (boundAspect == requestedAspect) {
        return true;
    }
    std::unordered_set<std::string> visited;
    std::vector<std::string> stack{boundAspect};
    while (!stack.empty()) {
        std::string cur = stack.back();
        stack.pop_back();
        if (!visited.insert(cur).second) {
            continue;
        }
        if (cur == requestedAspect) {
            return true;
        }
        TraitInfo* info = findTrait(cur);
        if (!info) {
            continue;
        }
        for (const auto& super : info->superTraits) {
            if (!visited.count(super)) {
                stack.push_back(super);
            }
        }
    }
    return false;
}

bool SemanticAnalyzer::findAspectMethod(const std::string& aspectName,
                                        const std::string& methodName,
                                        const TraitInfo*& declaringAspect,
                                        const TraitMethod*& outMethod) {
    std::function<bool(const TraitInfo*, std::unordered_set<const TraitInfo*>&)> search;
    search = [&](const TraitInfo* info, std::unordered_set<const TraitInfo*>& visited) -> bool {
        if (!info || !visited.insert(info).second) {
            return false;
        }
        for (const auto& tm : info->methods) {
            if (tm.name == methodName) {
                declaringAspect = info;
                outMethod = &tm;
                return true;
            }
        }
        for (const auto& superName : info->superTraits) {
            TraitInfo* super = findTrait(superName);
            if (super && search(super, visited)) {
                return true;
            }
        }
        return false;
    };
    std::unordered_set<const TraitInfo*> visited;
    return search(findTrait(aspectName), visited);
}

bool SemanticAnalyzer::hasAspectBinding(const std::string& typeStr, const std::string& aspectName) {
    auto it = traitImpls.find(typeStr);
    if (it != traitImpls.end() && it->second.count(aspectName)) {
        return true;
    }
    for (const auto& typeEntry : genericTraitImpls) {
        if (matchesPattern(typeStr, typeEntry.first) && typeEntry.second.count(aspectName)) {
            return true;
        }
    }
    return false;
}

void SemanticAnalyzer::validateAspectInheritance() {
    // Run after every aspect and bind has been registered. Validates that every
    // super-aspect names a defined aspect and that no inheritance cycle exists.
    for (const auto& entry : traitRegistry) {
        TraitInfo* info = entry.second.get();
        if (!info || info->superTraits.empty()) {
            continue;
        }

        std::vector<std::string> stack;
        std::function<bool(const std::string&, const std::string&, int)> findCycle;
        findCycle = [&](const std::string& start, const std::string& current, int depth) -> bool {
            if (depth > 0 && current == start) {
                return true;
            }
            auto curIt = traitRegistry.find(current);
            if (curIt == traitRegistry.end()) {
                // Unknown super-aspect; reported separately below.
                return false;
            }
            for (const auto& next : curIt->second->superTraits) {
                if (findCycle(start, next, depth + 1)) {
                    return true;
                }
            }
            return false;
        };

        for (const auto& superName : info->superTraits) {
            if (traitRegistry.find(superName) == traitRegistry.end()) {
                addError("Super-aspect '" + superName + "' of aspect '" + info->name +
                         "' is not a defined aspect.", info->declaration);
                continue;
            }
            if (findCycle(info->name, superName, 1)) {
                addError("Aspect '" + info->name + "' has a cyclic super-aspect dependency.", info->declaration);
            }
        }

        // Bind requirement: every type (concrete or generic) that binds this
        // sub-aspect must also bind each of its super-aspects. Runs after all
        // binds are registered so declaration order does not matter.
        for (const auto& superName : info->superTraits) {
            for (const auto& typeEntry : traitImpls) {
                if (typeEntry.second.count(info->name) &&
                    !hasAspectBinding(typeEntry.first, superName)) {
                    addError("Type '" + typeEntry.first + "' binds aspect '" + info->name +
                             "' which requires also binding super-aspect '" + superName +
                             "'.", info->declaration);
                }
            }
            for (const auto& typeEntry : genericTraitImpls) {
                if (typeEntry.second.count(info->name) &&
                    !hasAspectBinding(typeEntry.first, superName)) {
                    addError("Generic type '" + typeEntry.first + "' binds aspect '" + info->name +
                             "' which requires also binding super-aspect '" + superName +
                             "'.", info->declaration);
                }
            }
        }
    }
}

bool SemanticAnalyzer::validateTraitImpl(const std::string& typeName,
                                        const std::string& traitName,
                                        const std::vector<std::unique_ptr<ast::FunctionDeclaration>>& methods,
                                        const std::vector<ast::BindDeclaration::AssociatedTypeBinding>& associatedTypeBindings,
                                        const ast::BindDeclaration* bindDecl) {
    TraitInfo* traitInfo = findTrait(traitName);
    if (!traitInfo) {
        return false;
    }

    std::unordered_map<std::string, std::string> associatedTypeBindingNames;
    std::unordered_set<std::string> seenAssociatedTypeNames;
    for (const auto& assocBinding : associatedTypeBindings) {
        if (!assocBinding.name || !assocBinding.valueType) {
            continue;
        }

        const std::string& assocName = assocBinding.name->name;
        if (!seenAssociatedTypeNames.insert(assocName).second) {
            addError("Duplicate associated type assignment '" + assocName + "' in bind '" + traitName + " -> " + typeName + "'.", bindDecl);
            return false;
        }

        bool isKnown = false;
        for (const auto& declaredAssoc : traitInfo->associatedTypes) {
            if (declaredAssoc == assocName) {
                isKnown = true;
                break;
            }
        }

        if (!isKnown) {
            addError("Unknown associated type '" + assocName + "' in bind '" + traitName + " -> " + typeName + "'. Aspect '" + traitName + "' does not declare it.", bindDecl);
            return false;
        }

        associatedTypeBindingNames[assocName] = assocBinding.valueType->toString();
    }

    for (const auto& declaredAssoc : traitInfo->associatedTypes) {
        if (associatedTypeBindingNames.find(declaredAssoc) == associatedTypeBindingNames.end()) {
            // An undeclared associated type may be satisfied by the aspect's
            // default type (e.g. `type Item = Int` in the aspect declaration).
            ast::TypeNode* assocDefault = traitInfo->getAssociatedTypeDefault(declaredAssoc);
            if (!assocDefault) {
                addError("Missing associated type assignment '" + declaredAssoc + "' in bind '" + traitName + " -> " + typeName + "'.", bindDecl);
                return false;
            }
            associatedTypeBindingNames[declaredAssoc] = assocDefault->toString();
        }
    }

    // Validate that assigned associated types satisfy every declared aspect
    // bound (e.g. `type Item<Display>` requires the bound type to implement Display).
    for (const auto& declaredAssoc : traitInfo->associatedTypes) {
        const std::vector<ast::TypeNode*>* constraints =
            traitInfo->getAssociatedTypeConstraints(declaredAssoc);
        if (!constraints || constraints->empty()) {
            continue;
        }
        auto boundIt = associatedTypeBindingNames.find(declaredAssoc);
        if (boundIt == associatedTypeBindingNames.end()) {
            continue; // Reported as missing above.
        }
        const std::string& boundTypeStr = boundIt->second;
        for (const ast::TypeNode* constraint : *constraints) {
            if (!constraint) continue;
            auto constraintId = dynamic_cast<const ast::TypeName*>(constraint);
            if (!constraintId || !constraintId->identifier) continue;
            const std::string& constraintAspect = constraintId->identifier->name;
            TraitInfo* boundTrait = findTrait(constraintAspect);
            if (!boundTrait) {
                addError("Associated type bound '" + constraintAspect + "' in aspect '" +
                         traitName + "' names an aspect that is not defined.", bindDecl);
                return false;
            }
            if (!typeImplementsTrait(boundTypeStr, constraintAspect)) {
                addError("Associated type '" + declaredAssoc + "' of type '" + boundTypeStr +
                         "' in bind '" + traitName + " -> " + typeName +
                         "' does not implement aspect '" + constraintAspect + "'.", bindDecl);
                return false;
            }
        }
    }

    // Check that all required trait methods are implemented
    for (const auto& traitMethod : traitInfo->methods) {
        // Skip methods with default implementations
        if (traitMethod.hasDefaultImpl) {
            continue;
        }

        bool found = false;
        for (const auto& implMethod : methods) {
            if (implMethod && implMethod->id &&
                implMethod->id->name == traitMethod.name) {

                // Validate signature matches
                if (!traitMethodSignatureMatches(traitMethod, implMethod.get(), associatedTypeBindingNames)) {
                    VYB_CDBG << "DEBUG: Method signature mismatch for: " << traitMethod.name << std::endl;
                    return false;
                }

                found = true;
                break;
            }
        }

        if (!found) {
            VYB_CDBG << "DEBUG: Missing required trait method: " << traitMethod.name << std::endl;
            return false;
        }
    }

    return true;
}

bool SemanticAnalyzer::traitMethodSignatureMatches(const TraitMethod& traitMethod,
                                                   ast::FunctionDeclaration* implMethod,
                                                   const std::unordered_map<std::string, std::string>& associatedTypeBindings) {
    if (!implMethod || !implMethod->id) {
        return false;
    }

    // Check method name
    if (implMethod->id->name != traitMethod.name) {
        return false;
    }

    // Check parameter count (including self)
    if (implMethod->params.size() != traitMethod.parameterNames.size()) {
        VYB_CDBG << "DEBUG: Parameter count mismatch: "
                  << implMethod->params.size() << " vs "
                  << traitMethod.parameterNames.size() << std::endl;
        return false;
    }

    if (!traitMethod.parameterNames.empty() && traitMethod.parameterNames[0] == "self") {
        if (implMethod->params.empty() || !implMethod->params[0].name ||
            implMethod->params[0].name->name != "self") {
            addError("Method '" + implMethod->id->name + "' must use receiver parameter 'self' as its first parameter to implement aspect method '" + traitMethod.name + "'.", implMethod);
            return false;
        }
    }

    // Check return type matches
    if (implMethod->returnTypeNode && traitMethod.returnType) {
        std::string implReturnType = implMethod->returnTypeNode->toString();
        std::string traitReturnType = traitMethod.returnType->toString();

        auto resolveAssocRef = [&associatedTypeBindings](std::string type) -> std::string {
            if (type.rfind("Self::", 0) == 0) {
                std::string assocName = type.substr(6);
                auto assocIt = associatedTypeBindings.find(assocName);
                if (assocIt != associatedTypeBindings.end()) {
                    return assocIt->second;
                }
            }
            return type;
        };

        implReturnType = resolveAssocRef(implReturnType);
        traitReturnType = resolveAssocRef(traitReturnType);

        if (implReturnType != traitReturnType) {
            VYB_CDBG << "DEBUG: Return type mismatch: "
                      << implReturnType << " vs " << traitReturnType << std::endl;
            return false;
        }
    } else if (implMethod->returnTypeNode || traitMethod.returnType) {
        // One has return type, other doesn't
        return false;
    }

    // TODO: More rigorous type checking for parameters
    // For now, just basic validation

    return true;
}

std::string SemanticAnalyzer::resolveAssociatedTypeReference(
    const std::string& typeName,
    const std::string& traitName,
    const std::string& typeReference,
    const std::unordered_map<std::string, ast::TypeNode*>* inlineAssociatedTypeBindings) const {
    static constexpr size_t kSelfPrefixLength = 6; // strlen("Self::")
    std::string assocName;
    if (typeReference.rfind("Self::", 0) == 0) {
        assocName = typeReference.substr(kSelfPrefixLength);
    } else if (typeReference.rfind(traitName + "::", 0) == 0) {
        assocName = typeReference.substr(traitName.length() + 2);
    } else {
        return "";
    }

    if (assocName.empty()) {
        return "";
    }

    if (inlineAssociatedTypeBindings) {
        auto inlineIt = inlineAssociatedTypeBindings->find(assocName);
        if (inlineIt != inlineAssociatedTypeBindings->end() && inlineIt->second) {
            return inlineIt->second->toString();
        }
    }

    auto typeIt = traitAssociatedTypeImpls.find(typeName);
    if (typeIt == traitAssociatedTypeImpls.end()) {
        return "";
    }

    auto traitIt = typeIt->second.find(traitName);
    if (traitIt == typeIt->second.end()) {
        return "";
    }

    auto assocIt = traitIt->second.find(assocName);
    if (assocIt == traitIt->second.end() || !assocIt->second) {
        return "";
    }

    return assocIt->second->toString();
}

bool SemanticAnalyzer::setResolvedTraitReturnType(ast::CallExpression* callNode,
                                                  const std::string& concreteTypeName,
                                                  const std::string& traitName,
                                                  ast::TypeNode* traitReturnType) {
    if (!callNode || !traitReturnType) {
        return false;
    }

    std::string resolvedAssocType = resolveAssociatedTypeReference(concreteTypeName, traitName, traitReturnType->toString());
    if (!resolvedAssocType.empty()) {
        auto resolvedType = new ast::TypeName(callNode->loc, std::make_unique<ast::Identifier>(callNode->loc, resolvedAssocType));
        expressionTypes[callNode] = resolvedType;
        callNode->type = std::shared_ptr<ast::TypeNode>(resolvedType->clone());
        return true;
    }

    expressionTypes[callNode] = traitReturnType;
    callNode->type = std::shared_ptr<ast::TypeNode>(traitReturnType->clone());
    return true;
}

bool SemanticAnalyzer::matchesPattern(const std::string& concreteType, const std::string& pattern) {
    // Simple pattern matching: Box<Int> matches Box<T>
    // Extract base type from both

    size_t concreteAngle = concreteType.find('<');
    size_t patternAngle = pattern.find('<');

    // If pattern has no angle brackets, must match exactly
    if (patternAngle == std::string::npos) {
        return concreteType == pattern;
    }

    // If concrete type has no angle brackets but pattern does, no match
    if (concreteAngle == std::string::npos) {
        return false;
    }

    // Check base types match (e.g., "Box" == "Box")
    std::string concreteBase = concreteType.substr(0, concreteAngle);
    std::string patternBase = pattern.substr(0, patternAngle);

    if (concreteBase != patternBase) {
        return false;
    }

    // For now, if base types match and both have angle brackets, consider it a match
    // A more sophisticated implementation would validate type argument counts
    return true;
}

// Introspection: typeof(expr) returns Type (8-byte type ID)
// Introspection: typeof(expr) / typeof<T>() returns an opaque Type type ID.
void SemanticAnalyzer::visit(ast::TypeofExpression* node) {
    if (!node) {
        addError("typeof() requires an operand expression.", node);
        expressionTypes[node] = nullptr;
        return;
    }

    // Result type is always "Type" (primitive introspection type)
    auto typeIdent = std::make_unique<ast::Identifier>(node->loc, "Type");
    ast::TypeNode* resultType = new ast::TypeName(node->loc, std::move(typeIdent));
    expressionTypes[node] = resultType;
    node->type = std::shared_ptr<ast::TypeNode>(resultType->clone());

    // Compile-time form: typeof<T>() just resolves the target type.
    if (node->typeArg) {
        node->typeArg->accept(*this);
        return;
    }

    if (!node->operand) {
        addError("typeof() requires an operand expression.", node);
        expressionTypes[node] = nullptr;
        return;
    }
    node->operand->accept(*this);

    // A wildcard trap error (e<?>) has no static type; extract its runtime type.
    if (isWildcardErrorExpr(node->operand.get())) {
        node->operandFromWildcardError = true;
    }
}

// Introspection: typename(expr) returns String
// Introspection: typename(expr) returns String.
void SemanticAnalyzer::visit(ast::TypenameExpression* node) {
    if (!node || !node->operand) {
        addError("typename() requires an operand expression.", node);
        expressionTypes[node] = nullptr;
        return;
    }

    // Analyze operand to determine its type
    node->operand->accept(*this);

    // Result type is always "String" — Vyb's string type uses PascalCase
    auto stringIdent = std::make_unique<ast::Identifier>(node->loc, "String");
    ast::TypeNode* resultType = new ast::TypeName(node->loc, std::move(stringIdent));
    expressionTypes[node] = resultType;
    node->type = std::shared_ptr<ast::TypeNode>(resultType->clone());

    // A wildcard trap error (e<?>): load its runtime type name at codegen.
    if (isWildcardErrorExpr(node->operand.get())) {
        node->operandFromWildcardError = true;
    }

    // A `Type` value operand: its static type is the opaque `Type`, so the actual
    // type name must be resolved at runtime from the type ID via the registry.
    if (node->operand->type && node->operand->type->toString() == "Type") {
        node->operandFromTypeValue = true;
    }
}



// `value as TargetType`: safe downcasting. Types the expression as TargetType
// and marks a wildcard trap error operand (`e<?>`) so codegen extracts its payload.
void SemanticAnalyzer::visit(ast::AsExpression* node) {
    if (!node || !node->operand || !node->targetType) {
        addError("Malformed 'as' cast expression.", node);
        return;
    }
    node->operand->accept(*this);
    node->targetType->accept(*this);

    ast::TypeNode* targetType = node->targetType->type
        ? node->targetType->type.get() : node->targetType.get();
    if (!targetType) {
        addError("Cannot resolve target type of 'as' cast.", node);
        expressionTypes[node] = nullptr;
        return;
    }

    // Result type is the target type.
    node->type = std::shared_ptr<ast::TypeNode>(targetType->clone());
    expressionTypes[node] = node->type.get();

    // Determine the operand's static type, if any.
    ast::TypeNode* operandType = node->operand->type
        ? node->operand->type.get() : nullptr;
    if (!operandType) {
        auto it = expressionTypes.find(node->operand.get());
        if (it != expressionTypes.end()) operandType = it->second;
    }

    // A wildcard trap error (`e<?>`) has no static type; `e as TargetType` is the
    // canonical safe downcast that extracts the concrete payload from the error
    // struct at codegen.
    bool isWildcardError = false;
    if (auto* id = dynamic_cast<ast::Identifier*>(node->operand.get())) {
        SymbolInfo* sym = currentScope->lookup(id->name);
        if (sym && sym->type == nullptr) isWildcardError = true;
    }
    if (isWildcardError || !operandType) {
        node->operandIsWildcardError = true;
        return;
    }

    // Identity / same-type cast: allowed.
    if (targetType->toString() == operandType->toString() ||
        areTypesCompatible(targetType, operandType)) {
        return;
    }

    addError("Unsupported 'as' cast from '" + operandType->toString() + "' to '" +
             targetType->toString() + "'.", node);
}

// TupleDestructureAssignment: x, y = expr
void SemanticAnalyzer::visit(ast::TupleDestructureAssignment* node) {
    if (!node || !node->expression) {
        addError("Tuple destructure requires an expression.", node);
        return;
    }
    
    // Analyze RHS expression to get its type
    node->expression->accept(*this);
    
    // Try expressionTypes first, then fall back to expression's own type field
    ast::TypeNode* rhsType = nullptr;
    auto it = expressionTypes.find(node->expression.get());
    if (it != expressionTypes.end() && it->second) {
        rhsType = it->second;
    } else if (node->expression->type) {
        rhsType = node->expression->type.get();
    }
    
    if (!rhsType) {
        addError("Tuple destructure RHS has no type.", node);
        return;
    }
    
    // The RHS can be either:
    // 1. A TupleTypeNode (from multi-value function returns like get_values())
    // 2. A TypeName referring to a struct (from struct construction)
    
    std::vector<ast::TypeNode*> elementTypes;
    
    if (auto tupleType = dynamic_cast<ast::TupleTypeNode*>(rhsType)) {
        // Case 1: TupleTypeNode from multi-value return
        for (auto& elemType : tupleType->memberTypes) {
            if (elemType) {
                elementTypes.push_back(elemType.get());
            }
        }
    } else if (auto typeName = dynamic_cast<ast::TypeName*>(rhsType)) {
        // Case 2: Struct type - look up fields
        std::string structName = typeName->identifier ? typeName->identifier->name : "";
        
        auto structIt = structFieldTypes.find(structName);
        if (structIt == structFieldTypes.end()) {
            addError("Tuple destructure RHS type '" + structName + "' is not a struct or tuple type.", node);
            return;
        }
        
        const auto& fields = structIt->second;
        if (fields.size() != node->identifiers.size()) {
            addError("Tuple destructure: expected " + std::to_string(fields.size()) +
                     " elements but got " + std::to_string(node->identifiers.size()), node);
            return;
        }
        
        // Use field types as element types
        for (auto& [fieldName, fieldType] : fields) {
            elementTypes.push_back(fieldType);
        }
    } else {
        addError("Tuple destructure RHS must be a tuple or struct type, got " + rhsType->toString(), node);
        return;
    }
    
    // Validate element count matches
    if (elementTypes.size() != node->identifiers.size()) {
        addError("Tuple destructure: expected " + std::to_string(elementTypes.size()) +
                 " elements but got " + std::to_string(node->identifiers.size()), node);
        return;
    }
    
    // Validate each identifier and create bindings in current scope
    for (size_t i = 0; i < node->identifiers.size(); ++i) {
        std::string varName = node->identifiers[i]->name;
        
        // Check if variable already exists in current scope — if so, just skip (assignment target)
        if (currentScope->lookupDirect(varName)) {
            continue;
        }
        
        // Create binding in current scope with the element type
        ast::TypeNode* elemType = elementTypes[i];
        currentScope->add(SymbolInfo{SymbolInfo::Kind::Variable, varName, false, 
                                      ast::OwnershipKind::MY, 
                                      elemType ? elemType->clone().release() : nullptr});
    }
}

} // namespace vyb
