// =============================================================================
// DESIGN: Trait monomorphization (compile-time specialization)
// Vyb uses compile-time monomorphization for traits - NOT runtime polymorphism.
// When a trait method is called on a concrete type, a specialized version of the
// method is generated at compile time. This is similar to Rust's monomorphic
// dispatch or C++ template instantiation.
//
// NO vtables, NO dynamic dispatch, NO trait objects.
// All trait resolution happens at compile time via type substitution.
// =============================================================================

#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/semantic.hpp"
#include <sstream>
#include <algorithm>

namespace vyb {

// Parse a type string like "Box<Int>" into {base: "Box", args: ["Int"]}
LLVMCodegen::TypePattern LLVMCodegen::TypePattern::parse(const std::string& typeStr) {
    TypePattern pattern;

    size_t anglePos = typeStr.find('<');
    if (anglePos == std::string::npos) {
        // Simple type without generic args
        pattern.base = typeStr;
        return pattern;
    }

    // Extract base type
    pattern.base = typeStr.substr(0, anglePos);

    // Extract generic arguments
    size_t start = anglePos + 1;
    size_t end = typeStr.find_last_of('>');
    if (end == std::string::npos || end <= start) {
        // Malformed, return what we have
        return pattern;
    }

    std::string argsStr = typeStr.substr(start, end - start);

    // Split by comma (simple split, doesn't handle nested generics yet)
    std::stringstream ss(argsStr);
    std::string arg;
    while (std::getline(ss, arg, ',')) {
        // Trim whitespace
        arg.erase(0, arg.find_first_not_of(" \t"));
        arg.erase(arg.find_last_not_of(" \t") + 1);
        if (!arg.empty()) {
            pattern.args.push_back(arg);
        }
    }

    return pattern;
}

// Check if a concrete type matches this pattern, extracting type substitutions
// Example: pattern="Box<T>", concrete="Box<Int>" -> true, subst={"T":"Int"}
bool LLVMCodegen::TypePattern::matchesPattern(const TypePattern& concrete,
                                              std::map<std::string, std::string>& substitutions) const {
    // Base types must match exactly
    if (this->base != concrete.base) {
        return false;
    }

    // Number of type arguments must match
    if (this->args.size() != concrete.args.size()) {
        return false;
    }

    // Match each argument
    for (size_t i = 0; i < this->args.size(); ++i) {
        const std::string& patternArg = this->args[i];
        const std::string& concreteArg = concrete.args[i];

        // Check if pattern arg is a type parameter (single uppercase letter or capitalized identifier)
        // Simple heuristic: if it's a single char or starts with capital and no '<', it's a type param
        bool isTypeParam = (patternArg.length() == 1 && std::isupper(patternArg[0])) ||
                          (patternArg.find('<') == std::string::npos && std::isupper(patternArg[0]));

        if (isTypeParam) {
            // This is a type parameter - record the substitution
            auto it = substitutions.find(patternArg);
            if (it != substitutions.end()) {
                // Already have a substitution for this param - must be consistent
                if (it->second != concreteArg) {
                    return false;
                }
            } else {
                substitutions[patternArg] = concreteArg;
            }
        } else {
            // Concrete type - must match exactly
            if (patternArg != concreteArg) {
                return false;
            }
        }
    }

    return true;
}

// Convert TypePattern to mangled name: Box<Int> -> Box_Int
std::string LLVMCodegen::TypePattern::toMangled() const {
    if (args.empty()) {
        return base;
    }

    std::string result = base;
    for (const auto& arg : args) {
        result += "_" + arg;
    }
    return result;
}

vyb::ast::TypeNodePtr LLVMCodegen::typePatternToTypeNode(const TypePattern& pattern,
                                                   const SourceLocation& loc) {
    std::vector<vyb::ast::TypeNodePtr> args;
    for (const auto& arg : pattern.args) {
        TypePattern argPattern = TypePattern::parse(arg);
        args.push_back(typePatternToTypeNode(argPattern, loc));
    }

    return std::make_unique<vyb::ast::TypeName>(
        loc,
        std::make_unique<vyb::ast::Identifier>(loc, pattern.base),
        std::move(args)
    );
}

// Extract base pattern from concrete type: "Box<Int>" -> "Box"
std::string LLVMCodegen::extractBasePattern(const std::string& concreteType) {
    TypePattern parsed = TypePattern::parse(concreteType);
    return parsed.base;
}

// Get full type name from an expression (e.g., variable reference)
std::string LLVMCodegen::getFullTypeName(vyb::ast::Expression* expr) {
    if (!expr) return "";

    // Try to get type from the expression itself
    if (expr->type) {
        return expr->type->toString();
    }

    // For identifiers, check if we have type info
    if (auto ident = dynamic_cast<ast::Identifier*>(expr)) {
        if (ident->type) {
            return ident->type->toString();
        }
    }

    return "";
}

// Monomorphize a trait method for a concrete type
llvm::Function* LLVMCodegen::monomorphizeTraitMethod(const std::string& concreteType,
                                                     const std::string& traitName,
                                                     const std::string& methodName) {
    // Check cache first
    std::string cacheKey = concreteType + "::" + methodName;
    auto cacheIt = monomorphizedMethods.find(cacheKey);
    if (cacheIt != monomorphizedMethods.end()) {
        return cacheIt->second;
    }

    // Get semantic analyzer from driver
    if (!driver_.hasSemanticAnalyzer()) {
        logError(SourceLocation(), "SemanticAnalyzer not available for trait monomorphization");
        return nullptr;
    }

    SemanticAnalyzer* semantic = driver_.getSemanticAnalyzer();

    // Parse the concrete type to extract pattern matching info
    TypePattern concretePattern = TypePattern::parse(concreteType);



    // Search through generic trait impls to find a matching pattern
    const auto& genericImpls = semantic->getGenericTraitImpls();
    for (const auto& typeEntry : genericImpls) {
        const std::string& pattern = typeEntry.first;
        TypePattern templatePattern = TypePattern::parse(pattern);



        // Try to match the pattern
        std::map<std::string, std::string> typeSubstitutions;
        if (templatePattern.matchesPattern(concretePattern, typeSubstitutions)) {


            // Check if this pattern has an impl for the requested trait
            const auto& traitMap = typeEntry.second;
            auto traitIt = traitMap.find(traitName);
            if (traitIt != traitMap.end()) {
                const GenericImplInfo* implInfo = traitIt->second.get();

                // Check if the method exists in this impl
                auto methodIt = implInfo->methods.find(methodName);
                if (methodIt != implInfo->methods.end()) {
                    ast::FunctionDeclaration* methodAST = methodIt->second;



                    // Clone the method AST for modification
                    // Since we don't have a deep clone method for FunctionDeclaration,
                    // we'll work with the original and generate specialized code directly

                    // Build specialized function name: TypeName_MethodName (e.g., "Box_Int_show")
                    std::string specializedName = concretePattern.toMangled() + "_" + methodName;


                    // Get the method's signature
                    std::vector<llvm::Type*> paramTypes;

                    // First parameter is always Self (the object type)
                    llvm::Type* selfType = resolveTypeForMonomorphization(concretePattern, typeSubstitutions);
                    if (!selfType) {
                        logError(SourceLocation(), "Failed to resolve Self type for " + concreteType);
                        return nullptr;
                    }
                    paramTypes.push_back(selfType);

                    // Add remaining parameters. The receiver is always the first
                    // source-level parameter for aspect/bind methods and is already
                    // represented by the concrete Self argument above.
                    std::vector<size_t> nonReceiverParamIndices;
                    for (size_t i = 1; i < methodAST->params.size(); ++i) {
                        const auto& param = methodAST->params[i];

                        // Substitute type parameters in this parameter's type
                        llvm::Type* paramType = resolveParameterTypeWithSubstitution(
                            param.typeNode.get(), typeSubstitutions);
                        if (!paramType) {
                            logError(SourceLocation(), "Failed to resolve parameter type for " + param.name->name);
                            return nullptr;
                        }
                        paramTypes.push_back(paramType);
                        nonReceiverParamIndices.push_back(i);
                    }

                    // Determine return type with substitution
                    llvm::Type* returnType = llvm::Type::getVoidTy(*context);
                    if (methodAST->returnTypeNode) {
                        returnType = resolveReturnTypeWithSubstitution(
                            methodAST->returnTypeNode.get(), typeSubstitutions);
                        if (!returnType) {
                            logError(SourceLocation(), "Failed to resolve return type");
                            return nullptr;
                        }
                    }

                    // Create the specialized function
                    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, false);
                    llvm::Function* specializedFunc = llvm::Function::Create(
                        funcType,
                        llvm::Function::ExternalLinkage,
                        specializedName,
                        module.get()
                    );

                                        // Cache it before generating body
                    monomorphizedMethods[cacheKey] = specializedFunc;

                    // Generate the function body with type substitution active.
                    // Monomorphized bind methods need the same active function and
                    // impl context as normal bind methods so return statements, Self,
                    // and associated type references resolve against the specialized
                    // function instead of the caller currently being generated.
                    auto currentImplConcreteTypeNode = typePatternToTypeNode(concretePattern, methodAST->loc);

                    auto savedTypeSubstitutions = currentTypeSubstitutions;
                    auto savedNamedValues = namedValues;
                    llvm::Function* savedFunction = currentFunction;
                    ast::FunctionDeclaration* savedFunctionAST = currentFunctionAST;
                    ast::TypeNode* savedImplTypeNode = m_currentImplTypeNode;
                    std::string savedImplTraitName = m_currentImplTraitName;
                    llvm::BasicBlock* savedInsertBlock = builder->GetInsertBlock();
                    llvm::BasicBlock::iterator savedInsertPoint = builder->GetInsertPoint();

                    currentTypeSubstitutions = typeSubstitutions;
                    currentFunction = specializedFunc;
                    currentFunctionAST = methodAST;
                    m_currentImplTypeNode = currentImplConcreteTypeNode.get();
                    m_currentImplTraitName = traitName;

                    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", specializedFunc);
                    builder->SetInsertPoint(entry);
                    size_t savedScopeDepth = scopeStack.size();
                    enterScope();
                    generatePushFrameCall(specializedName, methodAST->loc);

                    namedValues.clear();

                    size_t argIdx = 0;
                    for (auto& arg : specializedFunc->args()) {
                        if (argIdx == 0) {
                            arg.setName("self");
                            llvm::AllocaInst* alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(
                                createEntryBlockAlloca(specializedFunc, "self", arg.getType()));
                            if (!alloca) {
                                logError(methodAST->loc, "Failed to create receiver alloca for monomorphized bind method");
                                if (scopeStack.size() > savedScopeDepth) exitScope();
                                namedValues = savedNamedValues;
                                currentTypeSubstitutions = savedTypeSubstitutions;
                                currentFunction = savedFunction;
                                currentFunctionAST = savedFunctionAST;
                                m_currentImplTypeNode = savedImplTypeNode;
                                m_currentImplTraitName = savedImplTraitName;
                                return nullptr;
                            }
                            builder->CreateStore(&arg, alloca);
                            namedValues["self"] = alloca;
                            valueTypeMap[alloca] = std::shared_ptr<ast::TypeNode>(currentImplConcreteTypeNode->clone());
                        } else {
                            size_t paramListIdx = argIdx - 1;
                            if (paramListIdx < nonReceiverParamIndices.size()) {
                                const auto& param = methodAST->params[nonReceiverParamIndices[paramListIdx]];
                                arg.setName(param.name->name);
                                llvm::AllocaInst* alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(
                                    createEntryBlockAlloca(specializedFunc, param.name->name, arg.getType()));
                                if (!alloca) {
                                    logError(param.name->loc, "Failed to create parameter alloca for " + param.name->name);
                                    if (scopeStack.size() > savedScopeDepth) exitScope();
                                    namedValues = savedNamedValues;
                                    currentTypeSubstitutions = savedTypeSubstitutions;
                                    currentFunction = savedFunction;
                                    currentFunctionAST = savedFunctionAST;
                                    m_currentImplTypeNode = savedImplTypeNode;
                                    m_currentImplTraitName = savedImplTraitName;
                                    return nullptr;
                                }
                                builder->CreateStore(&arg, alloca);
                                namedValues[param.name->name] = alloca;
                                if (param.typeNode) {
                                    valueTypeMap[alloca] = std::shared_ptr<ast::TypeNode>(param.typeNode->clone());
                                }
                            }
                        }
                        argIdx++;
                    }

                    if (methodAST->body) {
                        methodAST->body->accept(*this);
                    }

                    if (!builder->GetInsertBlock()->getTerminator()) {
                        if (scopeStack.size() > savedScopeDepth) {
                            exitScope();
                        }
                        generatePopFrameCall();
                        if (returnType->isVoidTy()) {
                            builder->CreateRetVoid();
                        } else {
                            builder->CreateRet(llvm::Constant::getNullValue(returnType));
                        }
                    } else if (scopeStack.size() > savedScopeDepth) {
                        // Explicit returns clean up the active function scope. If a
                        // no-return body path left it active, restore only the scope
                        // introduced for this monomorphized method, not the caller's.
                        exitScope();
                    }

                    namedValues = savedNamedValues;
                    currentTypeSubstitutions = savedTypeSubstitutions;
                    currentFunction = savedFunction;
                    currentFunctionAST = savedFunctionAST;
                    m_currentImplTypeNode = savedImplTypeNode;
                    m_currentImplTraitName = savedImplTraitName;
                    if (savedInsertBlock) {
                        if (savedInsertPoint != savedInsertBlock->end()) {
                            builder->SetInsertPoint(savedInsertBlock, savedInsertPoint);
                        } else {
                            builder->SetInsertPoint(savedInsertBlock);
                        }
                    }

                    return specializedFunc;
                }
            }
        }
    }


    return nullptr;
}

// Helper: Resolve the concrete type for monomorphization (e.g., Box<Int> -> %struct.Box_Int)
llvm::Type* LLVMCodegen::resolveTypeForMonomorphization(const TypePattern& pattern,
                                                        const std::map<std::string, std::string>& substitutions) {
    std::string mangledName = pattern.toMangled();

    // Check if struct type already exists (without "struct." prefix - the name used in monomorphizeStruct)
    if (llvm::StructType* structType = llvm::StructType::getTypeByName(*context, mangledName)) {
        return structType;
    }

    // Also try with "struct." prefix for compatibility
    std::string structName = "struct." + mangledName;
    if (llvm::StructType* structType = llvm::StructType::getTypeByName(*context, structName)) {
        return structType;
    }

    // Built-in generic runtime types such as Vec<T> do not have named
    // StructType instances. Reconstruct an AST type and let the normal type
    // codegen path produce the canonical LLVM representation.
    auto concreteTypeNode = typePatternToTypeNode(pattern, SourceLocation());
    if (llvm::Type* resolvedType = codegenType(concreteTypeNode.get())) {
        return resolvedType;
    }

    // For now, return generic pointer if we can't find the struct.
    // In production, this should trigger struct monomorphization.
    return llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0);
}

// Helper: Resolve parameter type with substitution (e.g., T -> Int)
llvm::Type* LLVMCodegen::resolveParameterTypeWithSubstitution(vyb::ast::TypeNode* typeNode,
                                                              const std::map<std::string, std::string>& substitutions) {
    if (!typeNode) {
        return nullptr;
    }

    // Check if this is a type parameter that needs substitution
    if (auto typeName = dynamic_cast<ast::TypeName*>(typeNode)) {
        if (typeName->identifier) {
            const std::string& name = typeName->identifier->name;

            // Check if it's a type parameter
            auto it = substitutions.find(name);
            if (it != substitutions.end()) {
                // Resolve the concrete type by creating a TypeName node
                auto concreteTypeNode = std::make_unique<ast::TypeName>(
                    SourceLocation(),
                    std::make_unique<ast::Identifier>(SourceLocation(), it->second)
                );
                return codegenType(concreteTypeNode.get());
            }
        }
    }

    // Otherwise, use normal type resolution
    return codegenType(typeNode);
}

// Helper: Resolve return type with substitution
llvm::Type* LLVMCodegen::resolveReturnTypeWithSubstitution(vyb::ast::TypeNode* typeNode,
                                                           const std::map<std::string, std::string>& substitutions) {
    // Same logic as parameter resolution
    return resolveParameterTypeWithSubstitution(typeNode, substitutions);
}

} // namespace vyb
