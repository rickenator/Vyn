#include "vre/llvm/codegen.hpp"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <cassert>
#include <stack>

using namespace vyn;

LLVMCodegen::LLVMCodegen(const std::string& moduleName)
    : context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>(moduleName, *context)),
      builder(std::make_unique<llvm::IRBuilder<>>(*context)) {}

LLVMCodegen::~LLVMCodegen() = default;

void LLVMCodegen::generate(Module* root) {
    // Traverse the AST and emit IR for each top-level statement/declaration
    for (const auto& stmt : root->body) {
        codegen(stmt.get());
    }
}

std::unique_ptr<llvm::Module> LLVMCodegen::takeModule() {
    return std::move(module);
}

// --- Type Mapping ---

// --- TypeNode Codegen ---
llvm::Type* LLVMCodegen::codegenType(TypeNode* type) {
    if (!type) return nullptr;
    using TC = TypeNode::TypeCategory;
    switch (type->category) {
        case TC::IDENTIFIER:
            if (!type->name) return nullptr;
            // 1. Check type parameter map (for generics)
            if (typeParameterMap.count(type->name->name)) {
                return codegenType(typeParameterMap[type->name->name]);
            }
            // 2. Built-in types
            if (type->name->name == "Int" || type->name->name == "int" || type->name->name == "i64")
                return llvm::Type::getInt64Ty(*context);
            if (type->name->name == "Float" || type->name->name == "float" || type->name->name == "f64")
                return llvm::Type::getDoubleTy(*context);
            if (type->name->name == "Bool" || type->name->name == "bool")
                return llvm::Type::getInt1Ty(*context);
            if (type->name->name == "String" || type->name->name == "string")
                return llvm::Type::getInt8PtrTy(*context);
            // 3. User-defined types (struct/class/enum)
            if (userTypeMap.count(type->name->name)) {
                // If already instantiated, return cached LLVM type
                if (userTypeMap[type->name->name].llvmType)
                    return userTypeMap[type->name->name].llvmType;
                // Otherwise, instantiate the type (struct/class/enum)
                // TODO: Support generic instantiation (type arguments)
                // For now, assume non-generic struct/class
                const auto& userType = userTypeMap[type->name->name];
                std::vector<llvm::Type*> fieldTypes;
                for (const auto& field : userType.fields) {
                    fieldTypes.push_back(codegenType(field.typeNode.get()));
                }
                llvm::StructType* structTy = llvm::StructType::create(*context, fieldTypes, type->name->name);
                userTypeMap[type->name->name].llvmType = structTy;
                return structTy;
            }
            // TODO: Error for unknown type
            return nullptr;
        case TC::GENERIC_INSTANCE:
            // Instantiate a generic/template type with concrete type arguments
            // Example: MyStruct<Int, Float>
            if (!type->name || !userTypeMap.count(type->name->name)) return nullptr;
            // Build a unique name for the instantiation
            std::string instName = type->name->name + "<";
            for (size_t i = 0; i < type->genericArguments.size(); ++i) {
                if (i > 0) instName += ",";
                instName += type->genericArguments[i]->toString();
            }
            instName += ">";
            // Check if already instantiated
            if (userTypeMap.count(instName) && userTypeMap[instName].llvmType)
                return userTypeMap[instName].llvmType;
            // Substitute type parameters and instantiate
            const auto& genericType = userTypeMap[type->name->name];
            std::unordered_map<std::string, TypeNode*> oldTypeParamMap = typeParameterMap;
            for (size_t i = 0; i < genericType.typeParameters.size(); ++i) {
                if (i < type->genericArguments.size())
                    typeParameterMap[genericType.typeParameters[i]] = type->genericArguments[i].get();
            }
            std::vector<llvm::Type*> fieldTypes;
            for (const auto& field : genericType.fields) {
                fieldTypes.push_back(codegenType(field.typeNode.get()));
            }
            llvm::StructType* structTy = llvm::StructType::create(*context, fieldTypes, instName);
            userTypeMap[instName] = genericType;
            userTypeMap[instName].llvmType = structTy;
            // Emit RTTI
            std::vector<std::string> fieldNames, fieldTypeNames;
            for (const auto& field : genericType.fields) {
                fieldNames.push_back(field.name);
                fieldTypeNames.push_back(field.typeNode->toString());
            }
            llvm::GlobalVariable* rtti = getOrCreateTypeDescriptor(instName, fieldNames, fieldTypeNames, "", "");
            userTypeMap[instName].rtti = rtti;
            typeParameterMap = oldTypeParamMap; // Restore
            return structTy;
        case TC::ARRAY:
            if (!type->arrayElementType) return nullptr;
            // For now, use a fixed size if arraySizeExpression is a constant integer
            if (type->arraySizeExpression) {
                // Try to evaluate the size expression as a constant integer
                if (auto* intLit = dynamic_cast<IntegerLiteral*>(type->arraySizeExpression.get())) {
                    return llvm::ArrayType::get(codegenType(type->arrayElementType.get()), intLit->value);
                }
            }
            // Fallback: pointer to element type
            return llvm::PointerType::getUnqual(codegenType(type->arrayElementType.get()));
        case TC::TUPLE:
            {
                std::vector<llvm::Type*> elems;
                for (const auto& elem : type->tupleElementTypes) {
                    elems.push_back(codegenType(elem.get()));
                }
                return llvm::StructType::get(*context, elems);
            }
        case TC::FUNCTION_SIGNATURE:
            {
                std::vector<llvm::Type*> params;
                for (const auto& param : type->functionParameters) {
                    params.push_back(codegenType(param.get()));
                }
                llvm::Type* retTy = type->functionReturnType ? codegenType(type->functionReturnType.get()) : llvm::Type::getVoidTy(*context);
                return llvm::FunctionType::get(retTy, params, false);
            }
        case TC::OWNERSHIP_WRAPPED:
            // For now, treat as pointer to wrapped type
            if (type->wrappedType)
                return llvm::PointerType::getUnqual(codegenType(type->wrappedType.get()));
            return nullptr;
        case TC::STRUCT:
        case TC::CLASS:
        case TC::ENUM:
            // User-defined type declaration: create LLVM struct type if not already present
            if (!type->name) return nullptr;
            {
                auto it = userTypeMap.find(type->name->name);
                if (it != userTypeMap.end()) return it->second;
                // Create a new LLVM struct type (opaque for now)
                llvm::StructType* structTy = llvm::StructType::create(*context, type->name->name);
                userTypeMap[type->name->name] = structTy;
                // TODO: Fill in struct fields after all types are registered (handle recursive types)
                // For now, leave as opaque
                return structTy;
            }
        default:
            return nullptr;
    }
}

// --- User-defined Type/Class/Struct/Enum/Template Codegen (Scaffold) ---
// Called for top-level declarations
void LLVMCodegen::codegen(ClassDeclaration* decl) {
    UserTypeInfo info;
    info.name = decl->id->name;
    std::vector<std::string> fieldNames, fieldTypeNames;
    for (const auto& param : decl->typeParameters) {
        info.typeParameters.push_back(param->name);
    }
    for (const auto& field : decl->fields) {
        info.fields.push_back({field->id->name, field->typeNode.get()});
        fieldNames.push_back(field->id->name);
        fieldTypeNames.push_back(field->typeNode->toString());
    }
    userTypeMap[info.name] = info;
    // Add RTTI pointer as first field in struct
    std::vector<llvm::Type*> llvmFields;
    llvmFields.push_back(getRTTIStructType()->getPointerTo());
    for (const auto& field : decl->fields) {
        llvmFields.push_back(codegenType(field->typeNode.get()));
    }
    llvm::StructType* structTy = llvm::StructType::create(*context, llvmFields, info.name);
    userTypeMap[info.name].llvmType = structTy;
    // Emit RTTI
    llvm::GlobalVariable* rtti = getOrCreateTypeDescriptor(info.name, fieldNames, fieldTypeNames, "", "");
    // Optionally, store RTTI pointer for later use
    userTypeMap[info.name].rtti = rtti;
}
void LLVMCodegen::codegen(StructDeclaration* decl) {
    UserTypeInfo info;
    info.name = decl->id->name;
    std::vector<std::string> fieldNames, fieldTypeNames;
    for (const auto& param : decl->typeParameters) {
        info.typeParameters.push_back(param->name);
    }
    for (const auto& field : decl->fields) {
        info.fields.push_back({field->id->name, field->typeNode.get()});
        fieldNames.push_back(field->id->name);
        fieldTypeNames.push_back(field->typeNode->toString());
    }
    userTypeMap[info.name] = info;
    std::vector<llvm::Type*> llvmFields;
    llvmFields.push_back(getRTTIStructType()->getPointerTo());
    for (const auto& field : decl->fields) {
        llvmFields.push_back(codegenType(field->typeNode.get()));
    }
    llvm::StructType* structTy = llvm::StructType::create(*context, llvmFields, info.name);
    userTypeMap[info.name].llvmType = structTy;
    llvm::GlobalVariable* rtti = getOrCreateTypeDescriptor(info.name, fieldNames, fieldTypeNames, "", "");
    userTypeMap[info.name].rtti = rtti;
}
void LLVMCodegen::codegen(EnumDeclaration* decl) {
    UserTypeInfo info;
    info.name = decl->id->name;
    userTypeMap[info.name] = info;
    llvm::GlobalVariable* rtti = getOrCreateTypeDescriptor(info.name, {}, {}, "", "");
    userTypeMap[info.name].rtti = rtti;
}
void LLVMCodegen::codegen(TemplateDeclaration* decl) {
    UserTypeInfo info;
    info.name = decl->id->name;
    for (const auto& param : decl->typeParameters) {
        info.typeParameters.push_back(param->name);
    }
    for (const auto& field : decl->fields) {
        info.fields.push_back({field->id->name, field->typeNode.get()});
    }
    userTypeMap[info.name] = info;
    // RTTI for template itself is not emitted; instantiations will emit RTTI
}
// --- End user-defined/generic support ---

// --- Node Codegen Dispatch ---

llvm::Value* LLVMCodegen::codegen(Node* node) {
    if (!node) return nullptr;
    switch (node->getType()) {
        case NodeType::INTEGER_LITERAL:
            return codegen(static_cast<IntegerLiteral*>(node));
        case NodeType::FLOAT_LITERAL:
            return codegen(static_cast<FloatLiteral*>(node));
        case NodeType::STRING_LITERAL:
            return codegen(static_cast<StringLiteral*>(node));
        case NodeType::BOOLEAN_LITERAL:
            return codegen(static_cast<BooleanLiteral*>(node));
        case NodeType::NIL_LITERAL:
            return codegen(static_cast<NilLiteral*>(node));
        case NodeType::BINARY_EXPRESSION:
            return codegen(static_cast<BinaryExpression*>(node));
        case NodeType::UNARY_EXPRESSION:
            return codegen(static_cast<UnaryExpression*>(node));
        case NodeType::ASSIGNMENT_EXPRESSION:
            return codegen(static_cast<AssignmentExpression*>(node));
        case NodeType::CALL_EXPRESSION:
            return codegen(static_cast<CallExpression*>(node));
        case NodeType::MEMBER_EXPRESSION:
            return codegen(static_cast<MemberExpression*>(node));
        case NodeType::IDENTIFIER:
            return codegen(static_cast<Identifier*>(node));
        case NodeType::ARRAY_LITERAL_NODE:
            return codegen(static_cast<ArrayLiteralNode*>(node));
        case NodeType::OBJECT_LITERAL_NODE:
            return codegen(static_cast<ObjectLiteral*>(node));
        // Statements
        case NodeType::EXPRESSION_STATEMENT:
            codegen(static_cast<ExpressionStatement*>(node)); return nullptr;
        case NodeType::BLOCK_STATEMENT:
            codegen(static_cast<BlockStatement*>(node)); return nullptr;
        case NodeType::IF_STATEMENT:
            codegen(static_cast<IfStatement*>(node)); return nullptr;
        case NodeType::WHILE_STATEMENT:
            return codegen(static_cast<WhileStatement*>(node));
        case NodeType::FOR_STATEMENT:
            return codegen(static_cast<ForStatement*>(node));
        case NodeType::RETURN_STATEMENT:
            codegen(static_cast<ReturnStatement*>(node)); return nullptr;
        case NodeType::BREAK_STATEMENT:
            codegen(static_cast<BreakStatement*>(node)); return nullptr;
        case NodeType::CONTINUE_STATEMENT:
            codegen(static_cast<ContinueStatement*>(node)); return nullptr;
        // Declarations
        case NodeType::VARIABLE_DECLARATION:
            codegen(static_cast<VariableDeclaration*>(node)); return nullptr;
        case NodeType::FUNCTION_DECLARATION:
            codegen(static_cast<FunctionDeclaration*>(node)); return nullptr;
        case NodeType::TYPE_ALIAS_DECLARATION:
            codegen(static_cast<TypeAliasDeclaration*>(node)); return nullptr;
        case NodeType::MODULE:
            codegen(static_cast<Module*>(node)); return nullptr;
        case NodeType::CLASS_DECLARATION:
            codegen(static_cast<ClassDeclaration*>(node)); return nullptr;
        case NodeType::STRUCT_DECLARATION:
            codegen(static_cast<StructDeclaration*>(node)); return nullptr;
        case NodeType::ENUM_DECLARATION:
            codegen(static_cast<EnumDeclaration*>(node)); return nullptr;
        case NodeType::TEMPLATE_DECLARATION:
            codegen(static_cast<TemplateDeclaration*>(node)); return nullptr;
        default:
            // TODO: Add all node types
            return nullptr;
    }
}

// --- Core Expression Codegen ---
llvm::Value* LLVMCodegen::codegen(IntegerLiteral* lit) {
    // Return a constant int64 value
    return llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), lit->value);
}
llvm::Value* LLVMCodegen::codegen(FloatLiteral* lit) {
    // Return a constant double value
    return llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), lit->value);
}
llvm::Value* LLVMCodegen::codegen(BooleanLiteral* lit) {
    // Return a constant i1 value
    return llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), lit->value ? 1 : 0);
}
llvm::Value* LLVMCodegen::codegen(NilLiteral* /*lit*/) {
    // Represent nil as a null pointer (could use special type)
    return llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(*context));
}
llvm::Value* LLVMCodegen::codegen(StringLiteral* lit) {
    // Create a global string and return a pointer to it
    return builder->CreateGlobalStringPtr(lit->value);
}
llvm::Value* LLVMCodegen::codegen(Identifier* id) {
    auto it = namedValues.find(id->name);
    if (it == namedValues.end()) return nullptr;
    return builder->CreateLoad(it->second->getType()->getPointerElementType(), it->second, id->name);
}
llvm::Value* LLVMCodegen::codegen(ArrayLiteralNode* arr) {
    // For now, create a stack-allocated array of i64 (or pointer to i64)
    size_t n = arr->elements.size();
    llvm::ArrayType* arrTy = llvm::ArrayType::get(llvm::Type::getInt64Ty(*context), n);
    llvm::AllocaInst* alloca = builder->CreateAlloca(arrTy, nullptr, "arraylit");
    for (size_t i = 0; i < n; ++i) {
        llvm::Value* elem = codegen(arr->elements[i].get());
        if (!elem) elem = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        llvm::Value* idxs[] = {
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0),
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), i)
        };
        llvm::Value* ptr = builder->CreateGEP(arrTy, alloca, idxs, "elemptr");
        builder->CreateStore(elem, ptr);
    }
    return alloca;
}
llvm::Value* LLVMCodegen::codegen(ObjectLiteral* obj) {
    // For now, create a struct with all fields as i64 (or pointer to i64)
    size_t n = obj->properties.size();
    std::vector<llvm::Type*> fieldTypes(n, llvm::Type::getInt64Ty(*context));
    llvm::StructType* structTy = llvm::StructType::create(*context, fieldTypes, "objlit");
    llvm::AllocaInst* alloca = builder->CreateAlloca(structTy, nullptr, "objlit");
    for (size_t i = 0; i < n; ++i) {
        llvm::Value* val = codegen(obj->properties[i].value.get());
        if (!val) val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        llvm::Value* ptr = builder->CreateStructGEP(structTy, alloca, i, "fieldptr");
        builder->CreateStore(val, ptr);
    }
    return alloca;
}

// --- Concrete Node Codegen (Scaffold) ---

// ExpressionStatement
void LLVMCodegen::codegen(ExpressionStatement* stmt) {
    // Evaluate the expression for side effects, discard result
    codegen(stmt->expression.get());
    // No return value needed for statements
}

// BinaryExpression
llvm::Value* LLVMCodegen::codegen(BinaryExpression* expr) {
    // Generate code for left and right operands
    llvm::Value* lhs = codegen(expr->left.get());
    llvm::Value* rhs = codegen(expr->right.get());
    if (!lhs || !rhs) return nullptr;
    // Handle basic arithmetic for int64/double (extend for type checks)
    switch (expr->op.type) {
        case token::TokenType::PLUS:
            return builder->CreateAdd(lhs, rhs, "addtmp");
        case token::TokenType::MINUS:
            return builder->CreateSub(lhs, rhs, "subtmp");
        case token::TokenType::STAR:
            return builder->CreateMul(lhs, rhs, "multmp");
        case token::TokenType::SLASH:
            return builder->CreateSDiv(lhs, rhs, "divtmp");
        case token::TokenType::EQUAL_EQUAL:
            return builder->CreateICmpEQ(lhs, rhs, "eqtmp");
        case token::TokenType::BANG_EQUAL:
            return builder->CreateICmpNE(lhs, rhs, "netmp");
        case token::TokenType::LESS:
            return builder->CreateICmpSLT(lhs, rhs, "lttmp");
        case token::TokenType::LESS_EQUAL:
            return builder->CreateICmpSLE(lhs, rhs, "letmp");
        case token::TokenType::GREATER:
            return builder->CreateICmpSGT(lhs, rhs, "gttmp");
        case token::TokenType::GREATER_EQUAL:
            return builder->CreateICmpSGE(lhs, rhs, "getmp");
        default:
            return nullptr;
    }
}
llvm::Value* LLVMCodegen::codegen(UnaryExpression* expr) {
    llvm::Value* operand = codegen(expr->operand.get());
    if (!operand) return nullptr;
    switch (expr->op.type) {
        case token::TokenType::MINUS:
            return builder->CreateNeg(operand, "negtmp");
        case token::TokenType::BANG:
            return builder->CreateNot(operand, "nottmp");
        // --- Raw location dereference: loc(expr) or at(expr) ---
        case token::TokenType::KEYWORD_LOC: // loc(expr) as dereference
        case token::TokenType::KEYWORD_AT:  // at(expr) as dereference
        case token::TokenType::STAR:        // *expr as dereference (if used for loc<T>)
            // The operand must be a raw location (address). Load the value at that address.
            // NOTE: Type checking for raw location should be enforced in semantic analysis.
            // For now, assume operand is an address of the correct type.
            // The type to load should be inferred from context or type info (not shown here).
            // For demo, assume i64 (should be improved for real type info).
            return builder->CreateLoad(llvm::Type::getInt64Ty(*context), operand, "rawloc_deref");
        default:
            return nullptr;
    }
}
llvm::Value* LLVMCodegen::codegen(CallExpression* expr) {
    llvm::Value* callee = codegen(expr->callee.get());
    std::vector<llvm::Value*> args;
    for (const auto& arg : expr->arguments) {
        args.push_back(codegen(arg.get()));
    }
    // TODO: Check callee type and call
    // Example: builder->CreateCall(callee, args)
    return nullptr;
}
llvm::Value* LLVMCodegen::codegen(MemberExpression* expr) {
    // Codegen for obj.field access
    llvm::Value* base = codegen(expr->object.get());
    if (!base) return nullptr;
    llvm::StructType* structTy = nullptr;
    llvm::Value* basePtr = base;
    if (base->getType()->isPointerTy()) {
        llvm::Type* elemTy = base->getType()->getPointerElementType();
        structTy = llvm::dyn_cast<llvm::StructType>(elemTy);
    }
    if (!structTy) return nullptr;
    int fieldIndex = -1;
    auto it = userTypeMap.find(expr->object->inferredTypeName);
    if (it != userTypeMap.end()) {
        const auto& fields = it->second.fields;
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == expr->property->name) {
                fieldIndex = static_cast<int>(i) + 1; // +1 for RTTI field
                break;
            }
        }
    }
    if (fieldIndex < 1) return nullptr;
    llvm::Value* fieldPtr = builder->CreateStructGEP(structTy, basePtr, fieldIndex, "fieldptr");
    // Load the field value
    return builder->CreateLoad(fieldPtr->getType()->getPointerElementType(), fieldPtr, expr->property->name);
}
llvm::Value* LLVMCodegen::codegen(AssignmentExpression* expr) {
    // Support assignment to identifiers, member expressions, array elements, and raw location deref
    llvm::Value* varAddr = nullptr;
    // Helper for multidimensional array access: flatten indices
    auto flattenArrayAccess = [&](Node* node, llvm::Value*& basePtr, std::vector<llvm::Value*>& indices) -> bool {
        // Recursively collect indices from nested ArrayElementExpression
        if (auto* arrElem = dynamic_cast<ArrayElementExpression*>(node)) {
            if (!flattenArrayAccess(arrElem->array.get(), basePtr, indices)) return false;
            llvm::Value* idxVal = codegen(arrElem->index.get());
            if (!idxVal) return false;
            indices.push_back(builder->CreateIntCast(idxVal, llvm::Type::getInt32Ty(*context), false));
            return true;
        } else if (auto* id = dynamic_cast<Identifier*>(node)) {
            auto it = namedValues.find(id->name);
            if (it == namedValues.end()) return false;
            basePtr = it->second;
            return true;
        } else if (auto* member = dynamic_cast<MemberExpression*>(node)) {
            // Support arr.field[i] or obj.field[i] (struct/array in struct)
            llvm::Value* base = codegen(member->object.get());
            if (!base) return false;
            llvm::StructType* structTy = nullptr;
            llvm::Value* baseStructPtr = base;
            if (base->getType()->isPointerTy()) {
                llvm::Type* elemTy = base->getType()->getPointerElementType();
                structTy = llvm::dyn_cast<llvm::StructType>(elemTy);
            }
            if (!structTy) return false;
            int fieldIndex = -1;
            auto it = userTypeMap.find(member->object->inferredTypeName);
            if (it != userTypeMap.end()) {
                const auto& fields = it->second.fields;
                for (size_t i = 0; i < fields.size(); ++i) {
                    if (fields[i].name == member->property->name) {
                        fieldIndex = static_cast<int>(i) + 1; // +1 for RTTI field
                        break;
                    }
                }
            }
            if (fieldIndex < 1) return false;
            basePtr = builder->CreateStructGEP(structTy, baseStructPtr, fieldIndex, "fieldptr");
            return true;
        }
        // TODO: Pointer deref, etc.
        return false;
    };
    if (auto* id = dynamic_cast<Identifier*>(expr->left.get())) {
        auto it = namedValues.find(id->name);
        if (it == namedValues.end()) return nullptr;
        varAddr = it->second;
    } else if (auto* member = dynamic_cast<MemberExpression*>(expr->left.get())) {
        llvm::Value* base = codegen(member->object.get());
        if (!base) return nullptr;
        llvm::StructType* structTy = nullptr;
        llvm::Value* basePtr = base;
        if (base->getType()->isPointerTy()) {
            llvm::Type* elemTy = base->getType()->getPointerElementType();
            structTy = llvm::dyn_cast<llvm::StructType>(elemTy);
        }
        if (!structTy) return nullptr;
        int fieldIndex = -1;
        auto it = userTypeMap.find(member->object->inferredTypeName);
        if (it != userTypeMap.end()) {
            const auto& fields = it->second.fields;
            for (size_t i = 0; i < fields.size(); ++i) {
                if (fields[i].name == member->property->name) {
                    fieldIndex = static_cast<int>(i) + 1; // +1 for RTTI field
                    break;
                }
            }
        }
        if (fieldIndex < 1) return nullptr;
        varAddr = builder->CreateStructGEP(structTy, basePtr, fieldIndex, "fieldptr");
    } else if (auto* arrElem = dynamic_cast<ArrayElementExpression*>(expr->left.get())) {
        // Multidimensional array assignment: flatten indices
        llvm::Value* basePtr = nullptr;
        std::vector<llvm::Value*> indices;
        if (!flattenArrayAccess(expr->left.get(), basePtr, indices)) return nullptr;
        if (!basePtr || indices.empty()) return nullptr;
        // For static arrays, first index is always 0 (for alloca arrays)
        llvm::Type* arrTy = basePtr->getType();
        if (arrTy->isPointerTy()) arrTy = arrTy->getPointerElementType();
        std::vector<llvm::Value*> gepIndices;
        if (arrTy->isArrayTy()) {
            gepIndices.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
        }
        for (auto idx : indices) gepIndices.push_back(idx);
        llvm::Value* elemPtr = builder->CreateGEP(arrTy, basePtr, gepIndices, "elemptr");
        varAddr = elemPtr;
    } else if (auto* unary = dynamic_cast<UnaryExpression*>(expr->left.get())) {
        // Assignment to dereferenced raw location: loc(expr) = value or at(expr) = value
        if (unary->op.type == token::TokenType::KEYWORD_LOC ||
            unary->op.type == token::TokenType::KEYWORD_AT ||
            unary->op.type == token::TokenType::STAR) {
            llvm::Value* ptr = codegen(unary->operand.get());
            if (!ptr) return nullptr;
            varAddr = ptr;
        } else {
            return nullptr;
        }
    } else {
        // TODO: Support pointer deref, etc.
        return nullptr;
    }
    // 2. Codegen the right-hand side
    llvm::Value* rhs = codegen(expr->right.get());
    if (!rhs) return nullptr;
    // 3. Store the value
    builder->CreateStore(rhs, varAddr);
    return rhs;
}

// --- Statement Codegen ---
void LLVMCodegen::codegen(ExpressionStatement* stmt) {
    codegen(stmt->expression.get());
}
void LLVMCodegen::codegen(BlockStatement* block) {
    for (const auto& stmt : block->body) {
        codegen(stmt.get());
    }
}
void LLVMCodegen::codegen(IfStatement* stmt) {
    llvm::Value* condVal = codegen(stmt->test.get());
    if (!condVal) return;
    // Convert condition to bool (i1)
    condVal = builder->CreateICmpNE(condVal, llvm::ConstantInt::get(condVal->getType(), 0), "ifcond");
    llvm::Function* fn = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*context, "then", fn);
    llvm::BasicBlock* elseBB = stmt->alternate ? llvm::BasicBlock::Create(*context, "else") : nullptr;
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "ifcont");
    if (elseBB)
        builder->CreateCondBr(condVal, thenBB, elseBB);
    else
        builder->CreateCondBr(condVal, thenBB, mergeBB);
    // Emit then block
    builder->SetInsertPoint(thenBB);
    codegen(stmt->consequent.get());
    if (!thenBB->getTerminator()) builder->CreateBr(mergeBB);
    if (elseBB) {
        fn->getBasicBlockList().push_back(elseBB);
        builder->SetInsertPoint(elseBB);
        codegen(stmt->alternate.get());
        if (!elseBB->getTerminator()) builder->CreateBr(mergeBB);
    }
    fn->getBasicBlockList().push_back(mergeBB);
    builder->SetInsertPoint(mergeBB);
}
void LLVMCodegen::codegen(WhileStatement* stmt) {
    llvm::Function* fn = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "while.cond", fn);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*context, "while.body");
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "while.end");
    loopStack.push({afterBB, condBB});
    builder->CreateBr(condBB);
    builder->SetInsertPoint(condBB);
    llvm::Value* condVal = codegen(stmt->test.get());
    if (!condVal) condVal = llvm::ConstantInt::getFalse(*context);
    condVal = builder->CreateICmpNE(condVal, llvm::ConstantInt::get(condVal->getType(), 0), "whilecond");
    builder->CreateCondBr(condVal, bodyBB, afterBB);
    fn->getBasicBlockList().push_back(bodyBB);
    builder->SetInsertPoint(bodyBB);
    codegen(stmt->body.get());
    if (!bodyBB->getTerminator()) builder->CreateBr(condBB);
    loopStack.pop();
    fn->getBasicBlockList().push_back(afterBB);
    builder->SetInsertPoint(afterBB);
}
void LLVMCodegen::codegen(ReturnStatement* stmt) {
    llvm::Value* retVal = nullptr;
    if (stmt->argument) retVal = codegen(stmt->argument.get());
    if (retVal) builder->CreateRet(retVal);
    else builder->CreateRetVoid();
}
void LLVMCodegen::codegen(BreakStatement* /*stmt*/) {
    if (loopStack.empty()) return;
    builder->CreateBr(loopStack.top().breakTarget);
    // After a break, no more code should be emitted in this block
    llvm::BasicBlock* unreachableBB = llvm::BasicBlock::Create(*context, "unreachable");
    builder->SetInsertPoint(unreachableBB);
}
void LLVMCodegen::codegen(ContinueStatement* /*stmt*/) {
    if (loopStack.empty()) return;
    builder->CreateBr(loopStack.top().continueTarget);
    llvm::BasicBlock* unreachableBB = llvm::BasicBlock::Create(*context, "unreachable");
    builder->SetInsertPoint(unreachableBB);
}

// --- ForStatement Codegen ---
void LLVMCodegen::codegen(ForStatement* stmt) {
    llvm::Function* fn = builder->GetInsertBlock()->getParent();
    // For loop structure: for (init; test; update) body
    // 1. Emit init (if any)
    if (stmt->init) codegen(stmt->init.get());
    // 2. Create blocks
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*context, "for.cond", fn);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*context, "for.body");
    llvm::BasicBlock* updateBB = llvm::BasicBlock::Create(*context, "for.update");
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*context, "for.end");
    loopStack.push({afterBB, updateBB});
    builder->CreateBr(condBB);
    // 3. Condition
    builder->SetInsertPoint(condBB);
    llvm::Value* condVal = stmt->test ? codegen(stmt->test.get()) : llvm::ConstantInt::getTrue(*context);
    condVal = builder->CreateICmpNE(condVal, llvm::ConstantInt::get(condVal->getType(), 0), "forcond");
    builder->CreateCondBr(condVal, bodyBB, afterBB);
    // 4. Body
    fn->getBasicBlockList().push_back(bodyBB);
    builder->SetInsertPoint(bodyBB);
    codegen(stmt->body.get());
    if (!bodyBB->getTerminator()) builder->CreateBr(updateBB);
    // 5. Update
    fn->getBasicBlockList().push_back(updateBB);
    builder->SetInsertPoint(updateBB);
    if (stmt->update) codegen(stmt->update.get());
    builder->CreateBr(condBB);
    loopStack.pop();
    fn->getBasicBlockList().push_back(afterBB);
    builder->SetInsertPoint(afterBB);
}

// --- Declaration Codegen ---
void LLVMCodegen::codegen(VariableDeclaration* decl) {
    // Only handle local (stack) variables for now
    llvm::Type* llvmType = codegenType(decl->typeNode.get());
    if (!llvmType) {
        // Default to i64 if type is missing (for demo)
        llvmType = llvm::Type::getInt64Ty(*context);
    }
    llvm::AllocaInst* alloca = builder->CreateAlloca(llvmType, nullptr, decl->id->name);
    // If there's an initializer, store its value
    if (decl->init) {
        llvm::Value* initVal = codegen(decl->init.get());
        if (initVal) builder->CreateStore(initVal, alloca);
    }
    namedValues[decl->id->name] = alloca;
}
void LLVMCodegen::codegen(FunctionDeclaration* decl) {
    // 1. Get function name and return type
    std::string funcName = decl->id ? decl->id->name : "anon_func";
    llvm::Type* retType = decl->returnTypeNode ? codegenType(decl->returnTypeNode.get()) : llvm::Type::getVoidTy(*context);
    // 2. Collect argument types
    std::vector<llvm::Type*> argTypes;
    for (const auto& param : decl->params) {
        llvm::Type* argTy = param.typeNode ? codegenType(param.typeNode.get()) : llvm::Type::getInt64Ty(*context);
        argTypes.push_back(argTy);
    }
    // 3. Create function type and function
    llvm::FunctionType* fnType = llvm::FunctionType::get(retType, argTypes, false);
    llvm::Function* fn = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, funcName, module.get());
    // 4. Name arguments
    unsigned idx = 0;
    for (auto& arg : fn->args()) {
        if (idx < decl->params.size() && decl->params[idx].name)
            arg.setName(decl->params[idx].name->name);
        ++idx;
    }
    // 5. Create entry block and set builder
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", fn);
    builder->SetInsertPoint(entry);
    // 6. Allocate space for arguments and store them
    idx = 0;
    for (auto& arg : fn->args()) {
        llvm::AllocaInst* alloca = builder->CreateAlloca(arg.getType(), nullptr, arg.getName());
        builder->CreateStore(&arg, alloca);
        // TODO: Add to symbol table for lookup
        // namedValues[arg.getName().str()] = alloca;
        ++idx;
    }
    // 7. Codegen function body
    if (decl->body) {
        codegen(decl->body.get());
    }
    // 8. Ensure function ends with return (if needed)
    if (!entry->getTerminator()) {
        if (retType->isVoidTy()) {
            builder->CreateRetVoid();
        } else {
            builder->CreateRet(llvm::UndefValue::get(retType));
        }
    }
    // 9. Verify function
    llvm::verifyFunction(*fn);
}
void LLVMCodegen::codegen(TypeAliasDeclaration* decl) {
    // TODO: Implement type alias codegen
}
void LLVMCodegen::codegen(Module* module) {
    for (const auto& stmt : module->body) {
        codegen(stmt.get());
    }
}

// --- RTTI: Type Descriptor Generation ---
// RTTI struct: { i8* name, i64 fieldCount, i8** fieldNames, i8** fieldTypeNames, i8* vtablePtr, i8* traitInfo }
llvm::StructType* LLVMCodegen::getRTTIStructType() {
    static llvm::StructType* cached = nullptr;
    if (cached) return cached;
    llvm::Type* namePtrTy = llvm::Type::getInt8PtrTy(*context);
    llvm::Type* countTy = llvm::Type::getInt64Ty(*context);
    llvm::Type* ptrToNamePtrTy = namePtrTy->getPointerTo(); // i8**
    cached = llvm::StructType::create(*context, {namePtrTy, countTy, ptrToNamePtrTy, ptrToNamePtrTy, namePtrTy, namePtrTy}, "_vyn_rtti");
    return cached;
}
llvm::GlobalVariable* LLVMCodegen::getOrCreateTypeDescriptor(const std::string& typeName, const std::vector<std::string>& fieldNames, const std::vector<std::string>& fieldTypeNames, const std::string& vtablePtr, const std::string& traitInfo) {
    std::string globalName = "_vyn_typeinfo_" + typeName;
    if (auto* gv = module->getGlobalVariable(globalName)) return gv;
    llvm::StructType* rttiTy = getRTTIStructType();
    llvm::Type* namePtrTy = llvm::Type::getInt8PtrTy(*context);
    llvm::Type* countTy = llvm::Type::getInt64Ty(*context);
    // Field names array
    std::vector<llvm::Constant*> fieldNameGlobals;
    for (const auto& fname : fieldNames) {
        fieldNameGlobals.push_back(builder->CreateGlobalStringPtr(fname, globalName + "_field_" + fname));
    }
    llvm::ArrayType* fieldNamesArrTy = llvm::ArrayType::get(namePtrTy, fieldNames.size());
    llvm::Constant* fieldNamesArr = llvm::ConstantArray::get(fieldNamesArrTy, fieldNameGlobals);
    auto* fieldNamesGV = new llvm::GlobalVariable(*module, fieldNamesArrTy, true, llvm::GlobalValue::InternalLinkage, fieldNamesArr, globalName + "_fieldnames");
    llvm::Constant* fieldNamesPtr = llvm::ConstantExpr::getPointerCast(fieldNamesGV, namePtrTy->getPointerTo());
    // Field type names array
    std::vector<llvm::Constant*> fieldTypeNameGlobals;
    for (const auto& ftn : fieldTypeNames) {
        fieldTypeNameGlobals.push_back(builder->CreateGlobalStringPtr(ftn, globalName + "_ftype_" + ftn));
    }
    llvm::ArrayType* fieldTypeNamesArrTy = llvm::ArrayType::get(namePtrTy, fieldTypeNames.size());
    llvm::Constant* fieldTypeNamesArr = llvm::ConstantArray::get(fieldTypeNamesArrTy, fieldTypeNameGlobals);
    auto* fieldTypeNamesGV = new llvm::GlobalVariable(*module, fieldTypeNamesArrTy, true, llvm::GlobalValue::InternalLinkage, fieldTypeNamesArr, globalName + "_fieldtypenames");
    llvm::Constant* fieldTypeNamesPtr = llvm::ConstantExpr::getPointerCast(fieldTypeNamesGV, namePtrTy->getPointerTo());
    // Name, count, vtable, trait info
    llvm::Constant* nameVal = builder->CreateGlobalStringPtr(typeName, globalName + "_str");
    llvm::Constant* countVal = llvm::ConstantInt::get(countTy, fieldNames.size());
    llvm::Constant* vtableVal = vtablePtr.empty() ? llvm::ConstantPointerNull::get(namePtrTy) : builder->CreateGlobalStringPtr(vtablePtr, globalName + "_vtable");
    llvm::Constant* traitVal = traitInfo.empty() ? llvm::ConstantPointerNull::get(namePtrTy) : builder->CreateGlobalStringPtr(traitInfo, globalName + "_trait");
    llvm::Constant* rttiVal = llvm::ConstantStruct::get(rttiTy, {nameVal, countVal, fieldNamesPtr, fieldTypeNamesPtr, vtableVal, traitVal});
    auto* gv = new llvm::GlobalVariable(
        *module, rttiTy, true, llvm::GlobalValue::InternalLinkage, rttiVal, globalName
    );
    return gv;
}

// --- Comments/TODOs for RTTI extension ---
// TODO: Store RTTI pointer in type map for later use (e.g., dynamic casts)
// TODO: Extend RTTI struct to include field names/types, vtable ptr, trait info, etc.
// TODO: Emit RTTI for generic instantiations with unique names
