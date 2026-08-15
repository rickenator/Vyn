// SPDX-License-Identifier: Apache-2.0

// =============================================================================
// DESIGN: Function monomorphization (compile-time specialization)
// Vyb uses compile-time monomorphization for generic functions - NOT runtime polymorphism.
// When a generic function like printItem<T> is called with concrete types,
// a specialized version is generated at compile time with type parameters substituted.
// This is similar to Rust's monomorphic dispatch or C++ template instantiation.
//
// NO vtables, NO dynamic dispatch, NO trait objects.
// All generic resolution happens at compile time via type substitution.
// =============================================================================

#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/parser/ast.hpp"
#include <sstream>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>

namespace vyb {

// Generate a mangled name for a generic function instantiation
// Example: printItem with [Point] -> "printItem_Point"
//          duplicateAndShow with [Box<Int>] -> "duplicateAndShow_Box_Int"
std::string LLVMCodegen::mangleGenericFunctionName(const std::string& baseName,
                                                    const std::vector<std::string>& typeArgs) {
    std::stringstream ss;
    ss << baseName;

    for (const auto& typeArg : typeArgs) {
        ss << "_";
        // Replace invalid characters for LLVM function names
        std::string sanitized = typeArg;
        for (char& c : sanitized) {
            if (c == '<' || c == '>' || c == ',' || c == ' ') {
                c = '_';
            }
        }
        ss << sanitized;
    }

    return ss.str();
}

// Monomorphize a generic function for concrete type arguments
// Example: printItem<T<Display>> called with Point -> generate printItem_Point
llvm::Function* LLVMCodegen::monomorphizeGenericFunction(const std::string& functionName,
                                                         const std::vector<std::string>& concreteTypeArgs) {
    VYB_CDBG << "DEBUG: Monomorphizing generic function: " << functionName << " with types: ";
    for (const auto& t : concreteTypeArgs) VYB_CDBG << t << " ";
    VYB_CDBG << std::endl;

    // Look up the generic function template
    auto templateIt = genericFunctionTemplates.find(functionName);
    if (templateIt == genericFunctionTemplates.end()) {
        std::cerr << "ERROR: Generic function template '" << functionName << "' not found" << std::endl;
        return nullptr;
    }

    ast::FunctionDeclaration* templateFunc = templateIt->second;

    // Check if we've already monomorphized this combination
    std::string mangledName = mangleGenericFunctionName(functionName, concreteTypeArgs);
    auto cacheIt = monomorphizedFunctions.find(mangledName);
    if (cacheIt != monomorphizedFunctions.end()) {
        VYB_CDBG << "DEBUG: Found cached monomorphized function: " << mangledName << std::endl;
        return cacheIt->second;
    }

    // Build type parameter substitution map: T -> Point, etc.
    std::map<std::string, std::string> typeSubstitutions;
    if (templateFunc->genericParams.size() != concreteTypeArgs.size()) {
        std::cerr << "ERROR: Type argument count mismatch for " << functionName
                  << " (expected " << templateFunc->genericParams.size()
                  << ", got " << concreteTypeArgs.size() << ")" << std::endl;
        return nullptr;
    }

    for (size_t i = 0; i < templateFunc->genericParams.size(); ++i) {
        const auto& param = templateFunc->genericParams[i];
        if (param && param->name) {
            std::string typeParamName = param->name->name;
            typeSubstitutions[typeParamName] = concreteTypeArgs[i];
            VYB_CDBG << "DEBUG: Type substitution: " << typeParamName << " -> " << concreteTypeArgs[i] << std::endl;
        }
    }

    // Store substitutions for use during type resolution
    std::map<std::string, std::string> oldSubstitutions = currentTypeSubstitutions;
    currentTypeSubstitutions = typeSubstitutions;

    // Generate parameter types with substitutions
    std::vector<llvm::Type*> paramTypes;
    std::vector<std::string> paramNames;

    for (const auto& paramNode : templateFunc->params) {
        if (!paramNode.typeNode) {
            std::cerr << "ERROR: Parameter missing type in generic function" << std::endl;
            currentTypeSubstitutions = oldSubstitutions;
            return nullptr;
        }

        // Resolve parameter type with substitutions
        llvm::Type* paramType = resolveParameterTypeWithSubstitution(paramNode.typeNode.get(), typeSubstitutions);
        if (!paramType) {
            std::cerr << "ERROR: Could not resolve parameter type for " << paramNode.name->name << std::endl;
            currentTypeSubstitutions = oldSubstitutions;
            return nullptr;
        }

        paramTypes.push_back(paramType);
        paramNames.push_back(paramNode.name->name);
    }

    // Generate return type with substitutions
    llvm::Type* returnType = nullptr;
    llvm::Type* originalReturnType = nullptr;
    if (templateFunc->returnTypeNode) {
        originalReturnType = resolveReturnTypeWithSubstitution(templateFunc->returnTypeNode.get(), typeSubstitutions);
        if (!originalReturnType) {
            std::cerr << "ERROR: Could not resolve return type" << std::endl;
            currentTypeSubstitutions = oldSubstitutions;
            return nullptr;
        }
        returnType = originalReturnType;
    } else {
        originalReturnType = llvm::Type::getVoidTy(*context);
        returnType = originalReturnType;
    }
    // Mirror normal function codegen: a failable template returns {T, i8*}
    // (or {i1, i8*} for Void) so that a `fail` in the monomorphized body can
    // propagate the error back to the caller's trap instead of hitting the
    // untrapped runtime handler.
    const bool monoNeedsErrorReturn = (templateFunc->needsErrorReturn != 0);
    if (monoNeedsErrorReturn) {
        llvm::Type* errorPtrType = llvm::PointerType::get(*context, 0);
        if (returnType->isVoidTy()) {
            returnType = llvm::StructType::get(*context, {llvm::Type::getInt1Ty(*context), errorPtrType});
        } else {
            returnType = llvm::StructType::get(*context, {returnType, errorPtrType});
        }
    }

    // Create the specialized function
    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    llvm::Function* specializedFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        mangledName,
        module.get()
    );

    VYB_CDBG << "DEBUG: Created specialized function: " << mangledName << std::endl;

    // Generate function body if template has a body
    if (templateFunc->body) {
        // Save current function context
        llvm::Function* oldFunction = currentFunction;
        vyb::ast::FunctionDeclaration* oldFunctionAST = currentFunctionAST;
        llvm::BasicBlock* oldInsertBlock = builder->GetInsertBlock();
        std::map<std::string, llvm::Value*> oldNamedValues;
        oldNamedValues.swap(namedValues);

        // This body is a distinct failable function: point currentFunctionAST at
        // the template so a `fail` here takes the failable-return path.
        currentFunctionAST = templateFunc;

        // This monomorphized body is a distinct function, generated inline while
        // the caller's trap/scope context is still active. Isolate trap contexts
        // and scope tracking so that a `fail` here propagates via the failable
        // ABI (instead of branching into the caller's trap landing pad) and so
        // the function's own scopes are discarded without popping the caller's.
        std::vector<TrapContext> savedTrapStack;
        savedTrapStack.swap(trapStack);
        int savedTrapHandlerIndex = currentTrapHandlerIndex;
        currentTrapHandlerIndex = -1;
        auto savedScopeStack = std::move(scopeStack);
        scopeStack.clear();
        m_functionScopeBaseline = 0;

        currentFunction = specializedFunc;

        // Create entry block
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(*context, "entry", specializedFunc);
        builder->SetInsertPoint(entryBB);

        // Phase 6.4: Push a call frame for the monomorphized function so the
        // pops emitted by its return statements balance (mirrors normal function codegen).
        generatePushFrameCall(mangledName, templateFunc->loc);

        // Initialize scope
        enterScope();

        // Create allocas for parameters
        auto argIt = specializedFunc->arg_begin();
        for (size_t i = 0; i < paramTypes.size(); ++i, ++argIt) {
            llvm::Argument* argVal = &*argIt;
            argVal->setName(paramNames[i]);

            llvm::AllocaInst* alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(
                createEntryBlockAlloca(specializedFunc, paramNames[i], paramTypes[i])
            );
            if (!alloca) {
                std::cerr << "ERROR: Failed to create alloca for parameter " << paramNames[i] << std::endl;
                currentTypeSubstitutions = oldSubstitutions;
                currentFunction = oldFunction;
                namedValues.swap(oldNamedValues);
                exitScope();
                return nullptr;
            }

            builder->CreateStore(argVal, alloca);
            namedValues[paramNames[i]] = alloca;

            // Store type info for parameter with substituted type
            if (templateFunc->params[i].typeNode) {
                // Check if parameter type is a type parameter that needs substitution
                if (auto* paramTypeName = dynamic_cast<ast::TypeName*>(templateFunc->params[i].typeNode.get())) {
                    if (paramTypeName->identifier) {
                        std::string paramTypeStr = paramTypeName->identifier->name;
                        auto substIt = typeSubstitutions.find(paramTypeStr);
                        if (substIt != typeSubstitutions.end()) {
                            // Create a new TypeName with the concrete type
                            auto concreteTypeName = std::make_unique<ast::TypeName>(
                                paramTypeName->loc,
                                std::make_unique<ast::Identifier>(paramTypeName->loc, substIt->second),
                                std::vector<ast::TypeNodePtr>()
                            );
                            valueTypeMap[alloca] = std::shared_ptr<ast::TypeNode>(std::move(concreteTypeName));
                            VYB_CDBG << "DEBUG: Parameter '" << paramNames[i] << "' type substituted: "
                                      << paramTypeStr << " -> " << substIt->second << std::endl;
                        } else {
                            // Not a type parameter, clone as-is
                            valueTypeMap[alloca] = std::shared_ptr<ast::TypeNode>(templateFunc->params[i].typeNode->clone());
                        }
                    } else {
                        valueTypeMap[alloca] = std::shared_ptr<ast::TypeNode>(templateFunc->params[i].typeNode->clone());
                    }
                } else {
                    valueTypeMap[alloca] = std::shared_ptr<ast::TypeNode>(templateFunc->params[i].typeNode->clone());
                }
            }
        }

        // Codegen function body
        templateFunc->body->accept(*this);

        // Ensure function has a return if needed
        bool bodyFellThrough = !builder->GetInsertBlock()->getTerminator();
        if (bodyFellThrough) {
            generatePopFrameCall();
            if (returnType->isVoidTy()) {
                builder->CreateRetVoid();
            } else {
                // This should be caught by semantic analysis
                std::cerr << "WARNING: Non-void function missing return statement" << std::endl;
                builder->CreateRet(llvm::Constant::getNullValue(returnType));
            }
        }

        // Restore context, including the caller's insertion point so that
        // instructions after the call are emitted into the caller's block
        // (not into the freshly monomorphized function's block). Restore the
        // caller's trap context, handler index, and scope tracking wholesale so
        // nothing from this function's generation leaks into the caller.
        trapStack.swap(savedTrapStack);
        currentTrapHandlerIndex = savedTrapHandlerIndex;
        // Release the function's own scope (only for the fell-through path; a
        // returned body already balanced its scopes) before discarding it, so
        // ownership cleanup still runs, then restore the caller's scope stack.
        if (bodyFellThrough) {
            exitScope();
        }
        scopeStack = std::move(savedScopeStack);
        currentFunctionAST = oldFunctionAST;
        currentFunction = oldFunction;
        if (oldInsertBlock) builder->SetInsertPoint(oldInsertBlock);
        namedValues.swap(oldNamedValues);
    }

    // Cache the monomorphized function
    monomorphizedFunctions[mangledName] = specializedFunc;

    // Restore substitutions
    currentTypeSubstitutions = oldSubstitutions;

    VYB_CDBG << "DEBUG: Successfully monomorphized function: " << mangledName << std::endl;
    return specializedFunc;
}

} // namespace vyb
