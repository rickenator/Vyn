#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/parser/ast.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DerivedTypes.h>

namespace vyb {

// Implementation for string concatenation
llvm::Value* LLVMCodegen::generateStringConcatenation(llvm::Value* leftStr, llvm::Value* rightStr, SourceLocation loc) {
    if (!leftStr || !rightStr) {
        logError(loc, "Invalid string operands for concatenation");
        return nullptr;
    }

    // If either operand is a String struct {ptr, len}, extract the pointer
    if (leftStr->getType()->isStructTy()) {
        llvm::StructType* st = llvm::cast<llvm::StructType>(leftStr->getType());
        if (st->getNumElements() == 2 && st->getElementType(0)->isPointerTy()) {
            leftStr = builder->CreateExtractValue(leftStr, 0, "left.str.data");
        }
    }
    if (rightStr->getType()->isStructTy()) {
        llvm::StructType* st = llvm::cast<llvm::StructType>(rightStr->getType());
        if (st->getNumElements() == 2 && st->getElementType(0)->isPointerTy()) {
            rightStr = builder->CreateExtractValue(rightStr, 0, "right.str.data");
        }
    }

    // Create function signature for string concatenation helper
    llvm::FunctionType* concatFuncType = llvm::FunctionType::get(
        int8PtrType,                                 // Return type: char* (string)
        {int8PtrType, int8PtrType},                  // Parameters: (char*, char*)
        false                                        // Not vararg
    );

    // Get or declare the runtime string concatenation function
    llvm::Function* concatFunc = module->getFunction("__vyb_string_concat");

    if (!concatFunc) {
        concatFunc = llvm::Function::Create(
            concatFuncType,
            llvm::Function::ExternalLinkage,
            "__vyb_string_concat",
            module.get()
        );

        // Set parameter names for better readability
        auto args = concatFunc->arg_begin();
        args->setName("left");
        (++args)->setName("right");
    }

    // Create call to string concatenation function with our two operands
    std::vector<llvm::Value*> args = {leftStr, rightStr};
    return builder->CreateCall(concatFunc, args, "strcattmp");
}

// Implementation for string comparison (==, !=, <, <=, >, >=)
llvm::Value* LLVMCodegen::generateStringComparison(llvm::Value* leftStr, llvm::Value* rightStr, vyb::TokenType op) {
    if (!leftStr || !rightStr) {
        return llvm::ConstantInt::getFalse(*context);
    }

    // Extract data pointers and lengths from String structs { ptr, len }
    llvm::Value* leftData = builder->CreateExtractValue(leftStr, 0, "left.str.data");
    llvm::Value* leftLen = builder->CreateExtractValue(leftStr, 1, "left.str.len");
    llvm::Value* rightData = builder->CreateExtractValue(rightStr, 0, "right.str.data");
    llvm::Value* rightLen = builder->CreateExtractValue(rightStr, 1, "right.str.len");

    // For equality/inequality, check lengths first for quick return
    if (op == vyb::TokenType::EQEQ || op == vyb::TokenType::NOTEQ) {
        // Save the current block (where length comparison happens)
        llvm::BasicBlock* lenCheckBB = builder->GetInsertBlock();

        llvm::Value* lenEqual = builder->CreateICmpEQ(leftLen, rightLen, "str.len.eq");

        // Create basic blocks for conditional memcmp
        llvm::BasicBlock* memcmpBB = llvm::BasicBlock::Create(*context, "str.memcmp", currentFunction);
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(*context, "str.cmp.end", currentFunction);

        builder->CreateCondBr(lenEqual, memcmpBB, endBB);

        // memcmp block: lengths are equal, compare content
        builder->SetInsertPoint(memcmpBB);

        // Declare memcmp: int memcmp(const void* s1, const void* s2, size_t n)
        llvm::FunctionType* memcmpType = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(*context),
            {llvm::PointerType::get(*context, 0), llvm::PointerType::get(*context, 0), llvm::Type::getInt64Ty(*context)},
            false
        );
        llvm::Function* memcmpFunc = module->getFunction("memcmp");
        if (!memcmpFunc) {
            memcmpFunc = llvm::Function::Create(memcmpType, llvm::Function::ExternalLinkage, "memcmp", module.get());
        }

        // Call memcmp
        llvm::Value* cmpResult = builder->CreateCall(memcmpFunc, {leftData, rightData, leftLen}, "str.memcmp.result");
        llvm::Value* contentEqual = builder->CreateICmpEQ(cmpResult, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0), "str.content.eq");

        builder->CreateBr(endBB);
        llvm::BasicBlock* memcmpEndBB = builder->GetInsertBlock();

        // End block: merge results
        builder->SetInsertPoint(endBB);
        llvm::PHINode* phi = builder->CreatePHI(llvm::Type::getInt1Ty(*context), 2, "str.eq.result");
        phi->addIncoming(llvm::ConstantInt::getFalse(*context), lenCheckBB);  // lengths not equal
        phi->addIncoming(contentEqual, memcmpEndBB);  // lengths equal, use memcmp result

        // For NOTEQ, invert the result
        if (op == vyb::TokenType::NOTEQ) {
            return builder->CreateNot(phi, "str.ne.result");
        }
        return phi;
    }

    // For ordering comparisons (<, <=, >, >=), use memcmp on the shorter length
    // then compare lengths if memcmp returns 0
    llvm::Value* minLen = builder->CreateSelect(
        builder->CreateICmpULT(leftLen, rightLen),
        leftLen, rightLen, "str.minlen"
    );

    // Declare memcmp
    llvm::FunctionType* memcmpType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*context),
        {llvm::PointerType::get(*context, 0), llvm::PointerType::get(*context, 0), llvm::Type::getInt64Ty(*context)},
        false
    );
    llvm::Function* memcmpFunc = module->getFunction("memcmp");
    if (!memcmpFunc) {
        memcmpFunc = llvm::Function::Create(memcmpType, llvm::Function::ExternalLinkage, "memcmp", module.get());
    }

    // Call memcmp with minimum length
    llvm::Value* cmpResult = builder->CreateCall(memcmpFunc, {leftData, rightData, minLen}, "str.memcmp.result");

    // Create blocks for memcmp result handling
    llvm::BasicBlock* cmpZeroBB = llvm::BasicBlock::Create(*context, "str.cmp.zero", currentFunction);
    llvm::BasicBlock* cmpNonZeroBB = llvm::BasicBlock::Create(*context, "str.cmp.nonzero", currentFunction);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(*context, "str.cmp.end", currentFunction);

    llvm::Value* cmpIsZero = builder->CreateICmpEQ(cmpResult, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0), "str.cmp.iszero");
    builder->CreateCondBr(cmpIsZero, cmpZeroBB, cmpNonZeroBB);

    // If memcmp == 0, compare lengths
    builder->SetInsertPoint(cmpZeroBB);
    llvm::Value* lenCmpResult;
    switch (op) {
        case vyb::TokenType::LT:
            lenCmpResult = builder->CreateICmpULT(leftLen, rightLen, "str.len.lt");
            break;
        case vyb::TokenType::LTEQ:
            lenCmpResult = builder->CreateICmpULE(leftLen, rightLen, "str.len.le");
            break;
        case vyb::TokenType::GT:
            lenCmpResult = builder->CreateICmpUGT(leftLen, rightLen, "str.len.gt");
            break;
        case vyb::TokenType::GTEQ:
            lenCmpResult = builder->CreateICmpUGE(leftLen, rightLen, "str.len.ge");
            break;
        default:
            lenCmpResult = llvm::ConstantInt::getFalse(*context);
            break;
    }
    builder->CreateBr(endBB);
    llvm::BasicBlock* cmpZeroEndBB = builder->GetInsertBlock();

    // If memcmp != 0, use memcmp result directly
    builder->SetInsertPoint(cmpNonZeroBB);
    llvm::Value* memcmpBoolResult;
    switch (op) {
        case vyb::TokenType::LT:
            memcmpBoolResult = builder->CreateICmpSLT(cmpResult, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0), "str.memcmp.lt");
            break;
        case vyb::TokenType::LTEQ:
            memcmpBoolResult = builder->CreateICmpSLE(cmpResult, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0), "str.memcmp.le");
            break;
        case vyb::TokenType::GT:
            memcmpBoolResult = builder->CreateICmpSGT(cmpResult, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0), "str.memcmp.gt");
            break;
        case vyb::TokenType::GTEQ:
            memcmpBoolResult = builder->CreateICmpSGE(cmpResult, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0), "str.memcmp.ge");
            break;
        default:
            memcmpBoolResult = llvm::ConstantInt::getFalse(*context);
            break;
    }
    builder->CreateBr(endBB);
    llvm::BasicBlock* cmpNonZeroEndBB = builder->GetInsertBlock();

    // End block: merge results
    builder->SetInsertPoint(endBB);
    llvm::PHINode* phi = builder->CreatePHI(llvm::Type::getInt1Ty(*context), 2, "str.cmp.final");
    phi->addIncoming(lenCmpResult, cmpZeroEndBB);
    phi->addIncoming(memcmpBoolResult, cmpNonZeroEndBB);

    return phi;
}

// Helper method to resolve type aliases to their base type names
std::string LLVMCodegen::resolveTypeAliasToBaseName(vyb::ast::TypeNode* typeNode) {
    if (!typeNode) return "";

    if (auto typeName = dynamic_cast<vyb::ast::TypeName*>(typeNode)) {
        if (typeName->identifier) {
            std::string name = typeName->identifier->name;

            // Check if this is a type alias and resolve it
            auto aliasIt = typeAliasMap.find(name);
            if (aliasIt != typeAliasMap.end()) {
                llvm::Type* resolvedType = aliasIt->second;

                // Convert LLVM type back to string name
                if (resolvedType == int64Type) return "Int";
                if (resolvedType == int8Type) return "Int8";
                if (resolvedType == int32Type) return "Int32";
                if (resolvedType == int1Type) return "Bool";
                if (resolvedType == floatType) return "Float";
                if (resolvedType == doubleType) return "Double";
                if (resolvedType == int8PtrType) return "String";

                return name; // Fallback to original name
            }

            return name; // Not an alias, return original name
        }
    }

    return ""; // Could not resolve
}

// Helper method to generate toString call for a given value and type
llvm::Value* LLVMCodegen::generateToStringCall(llvm::Value* value, llvm::Type* valueType, vyb::ast::TypeNode* astType, SourceLocation loc) {
    if (!value || !valueType) {
        logError(loc, "Invalid value or type for toString conversion");
        return nullptr;
    }

    // Check if value is a String struct type
    if (valueType->isStructTy()) {
        llvm::StructType* structType = llvm::cast<llvm::StructType>(valueType);
        if (structType->getNumElements() == 2) {
            // A tagged-union enum is also a 2-element struct { i64 tag, [N x i8] data }.
            // Distinguish it from a Vyb String { ptr, len } before extracting the "data".
            // Prefer a name-based lookup: LLVM deduplicates identical struct
            // layouts, so two C-like enums both lower to `{ i64, [1 x i8] }` and
            // a purely structural match could resolve the wrong variant names.
            const TaggedEnumInfo* enumInfo = astType ? findTaggedEnum(astType) : nullptr;
            if (!enumInfo) enumInfo = findTaggedEnum(valueType);
            if (enumInfo) {
                std::string typeName = astType ? astType->toString() : "";
                return generateTaggedEnumToString(value, *enumInfo, typeName, loc);
            }
            // Only treat it as a String struct {ptr, len} when the first field is a
            // pointer; otherwise fall through to the custom-struct JSON path below.
            if (structType->getElementType(0)->isPointerTy()) {
                llvm::Value* dataPtr = builder->CreateExtractValue(value, 0, "str.data_for_concat");
                return dataPtr;
            }
        }

        // Check if this is a complex/custom struct type that needs JSON serialization
        if (astType) {
            std::string typeName = astType->toString();
            // Look up if this is a user-defined struct
            auto structIt = monomorphizedStructs.find(typeName);
            if (structIt != monomorphizedStructs.end()) {
                // This is a custom struct - serialize to JSON
                VYB_CDBG << "DEBUG: Generating JSON serialization for struct type: " << typeName << std::endl;

                // Call __vyb_complex_to_json(void* instance, const char* type_name)
                llvm::PointerType* int8PtrType = llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0);
                llvm::FunctionType* jsonFuncType = llvm::FunctionType::get(
                    int8PtrType,
                    {llvm::PointerType::get(*context, 0), int8PtrType},  // void* instance, const char* type_name
                    false
                );
                llvm::Function* jsonFunc = module->getFunction("__vyb_complex_to_json");
                if (!jsonFunc) {
                    jsonFunc = llvm::Function::Create(jsonFuncType,
                        llvm::Function::ExternalLinkage, "__vyb_complex_to_json", module.get());
                }

                // Allocate space for the struct if it's a value (not already a pointer)
                llvm::Value* structPtr = value;
                if (!valueType->isPointerTy()) {
                    structPtr = builder->CreateAlloca(valueType, nullptr, "struct.tmp");
                    builder->CreateStore(value, structPtr);
                }

                // Cast to void*
                llvm::Value* voidPtr = builder->CreateBitCast(structPtr, llvm::PointerType::get(*context, 0), "struct.void_ptr");

                // Create type name constant
                llvm::Constant* typeNameStrConst = llvm::ConstantDataArray::getString(*context, typeName, /*AddNull=*/true);
                llvm::GlobalVariable* typeNameGlobal = new llvm::GlobalVariable(
                    *module, typeNameStrConst->getType(), true,
                    llvm::GlobalValue::PrivateLinkage, typeNameStrConst,
                    ".str.typename." + typeName
                );
                llvm::Value* typeNamePtr = builder->CreateBitCast(typeNameGlobal, int8PtrType);

                // Call JSON serialization with instance and type name
                llvm::Value* jsonCStr = builder->CreateCall(jsonFunc, {voidPtr, typeNamePtr}, "json.cstr");

                // Convert char* to Vyb String struct {char* data, int64_t length}
                llvm::StructType* stringStructType = llvm::StructType::get(*context, {
                    int8PtrType,  // data
                    llvm::Type::getInt64Ty(*context)  // length
                });

                // Call strlen to get length
                llvm::FunctionType* strlenType = llvm::FunctionType::get(
                    llvm::Type::getInt64Ty(*context),
                    {int8PtrType},
                    false
                );
                llvm::Function* strlenFunc = module->getFunction("strlen");
                if (!strlenFunc) {
                    strlenFunc = llvm::Function::Create(strlenType,
                        llvm::Function::ExternalLinkage, "strlen", module.get());
                }
                llvm::Value* jsonLen = builder->CreateCall(strlenFunc, {jsonCStr}, "json.len");

                // Create String struct
                llvm::Value* stringStruct = llvm::UndefValue::get(stringStructType);
                stringStruct = builder->CreateInsertValue(stringStruct, jsonCStr, 0, "string.with_data");
                stringStruct = builder->CreateInsertValue(stringStruct, jsonLen, 1, "string.complete");

                return stringStruct;
            }
        }
    }

    // C-like enums are a scalar i64 tag but a distinct nominal type: render them
    // as `Enum::Variant` using the AST type name to disambiguate from plain Int.
    if (astType) {
        const TaggedEnumInfo* enumInfo = findTaggedEnum(astType);
        if (enumInfo && enumInfo->isScalar) {
            return generateTaggedEnumToString(value, *enumInfo, astType->toString(), loc);
        }
    }

    // Get the base type name, resolving type aliases
    std::string typeName = resolveTypeAliasToBaseName(astType);
    if (typeName.empty() && astType) {
        // Fallback to AST type string representation
        typeName = astType->toString();
    }

    // Determine appropriate toString function based on LLVM type and AST type
    std::string toStringFuncName;
    llvm::FunctionType* toStringFuncType = nullptr;

    if (valueType == int64Type || typeName == "Int") {
        toStringFuncName = "__vyb_int_to_string";
        toStringFuncType = llvm::FunctionType::get(int8PtrType, {int64Type}, false);
    } else if (valueType == int8Type || typeName == "Int8") {
        toStringFuncName = "__vyb_int_to_string";  // Reuse same function, will cast
        toStringFuncType = llvm::FunctionType::get(int8PtrType, {int64Type}, false);
        value = builder->CreateSExt(value, int64Type, "int8_to_int64");
    } else if (valueType == int32Type || typeName == "Int32") {
        toStringFuncName = "__vyb_int_to_string";  // Reuse same function, will cast
        toStringFuncType = llvm::FunctionType::get(int8PtrType, {int64Type}, false);
        value = builder->CreateSExt(value, int64Type, "int32_to_int64");
    } else if (valueType == int1Type || typeName == "Bool") {
        toStringFuncName = "__vyb_bool_to_string";
        toStringFuncType = llvm::FunctionType::get(int8PtrType, {int1Type}, false);
    } else if (valueType->isFloatingPointTy() || typeName == "Float" || typeName == "Double") {
        toStringFuncName = "__vyb_float_to_string";
        toStringFuncType = llvm::FunctionType::get(int8PtrType, {doubleType}, false);
        // Cast to double if needed
        if (valueType != doubleType) {
            value = builder->CreateFPExt(value, doubleType, "todouble");
        }
    } else if (valueType == int8PtrType || typeName == "String") {
        toStringFuncName = "__vyb_string_to_string";
        toStringFuncType = llvm::FunctionType::get(int8PtrType, {int8PtrType}, false);
    } else {
        logError(loc, "Unsupported type for toString conversion: " + (astType ? astType->toString() : "unknown"));
        return nullptr;
    }

    // Get or declare the toString function
    llvm::Function* toStringFunc = module->getFunction(toStringFuncName);
    if (!toStringFunc) {
        toStringFunc = llvm::Function::Create(
            toStringFuncType,
            llvm::Function::ExternalLinkage,
            toStringFuncName,
            module.get()
        );
    }

    // Cast value to correct type if needed
    if (value->getType() != toStringFuncType->getParamType(0)) {
        if (value->getType()->isIntegerTy() && toStringFuncType->getParamType(0)->isIntegerTy()) {
            // Integer types - extend or truncate as needed
            if (value->getType()->getIntegerBitWidth() < toStringFuncType->getParamType(0)->getIntegerBitWidth()) {
                value = builder->CreateSExt(value, toStringFuncType->getParamType(0), "sext");
            } else if (value->getType()->getIntegerBitWidth() > toStringFuncType->getParamType(0)->getIntegerBitWidth()) {
                value = builder->CreateTrunc(value, toStringFuncType->getParamType(0), "trunc");
            }
        }
    }

    // Call the toString function
    return builder->CreateCall(toStringFunc, {value}, "tostring");
}

// Render an enum value as a readable string like `Shape::Circle(2.5)` or
// `Shape::Circle` for a unit variant. Works for both data-carrying enums (a
// `{ i64 tag, [N x i8] data }` struct) and C-like scalar enums (a single i64
// tag). Payload fields are converted individually via generateToStringCall so
// nested enums/strings render correctly.
llvm::Value* LLVMCodegen::generateTaggedEnumToString(llvm::Value* value, const TaggedEnumInfo& info,
                                                     const std::string& typeName, SourceLocation loc) {
    if (!value) return nullptr;

    // For a scalar (C-like) enum the value *is* the i64 tag; for a data enum it
    // is the `{ i64 tag, [N x i8] data }` struct whose first field is the tag.
    llvm::Value* tag = info.isScalar
        ? value
        : builder->CreateExtractValue(value, 0, "enum.tostring.tag");
    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* origBB = builder->GetInsertBlock();

    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "enum.tostring.merge", currentFunc);
    builder->SetInsertPoint(mergeBB);
    llvm::PHINode* resultPhi = builder->CreatePHI(int8PtrType, info.variantTags.size() + 1, "enum.tostring.result");
    builder->SetInsertPoint(origBB);

    auto concatFragment = [&](llvm::Value* acc, llvm::Value* fragment) -> llvm::Value* {
        if (!acc) return fragment;
        return generateStringConcatenation(acc, fragment, loc);
    };

    std::map<unsigned, llvm::BasicBlock*> tagToBB;
    for (const auto& vt : info.variantTags) {
        llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "enum.tostring." + vt.first, currentFunc);
        tagToBB[vt.second] = bb;
    }
    llvm::BasicBlock* defaultBB = llvm::BasicBlock::Create(*context, "enum.tostring.unknown", currentFunc);

    const std::string prefix = typeName.empty() ? "enum" : typeName;
    for (const auto& vt : info.variantTags) {
        const std::string& variantName = vt.first;
        builder->SetInsertPoint(tagToBB[vt.second]);

        llvm::Value* acc = builder->CreateGlobalStringPtr(prefix, "enum.tostring.prefix");
        acc = concatFragment(acc, builder->CreateGlobalStringPtr("::", "enum.tostring.sep"));
        acc = concatFragment(acc, builder->CreateGlobalStringPtr(variantName, "enum.tostring.variant"));

        auto payIt = info.variantPayloadTypes.find(variantName);
        if (payIt != info.variantPayloadTypes.end() && payIt->second) {
            llvm::StructType* payloadTy = payIt->second;
            acc = concatFragment(acc, builder->CreateGlobalStringPtr("(", "enum.tostring.lparen"));
            for (unsigned i = 0; i < payloadTy->getNumElements(); ++i) {
                if (i > 0) {
                    acc = concatFragment(acc, builder->CreateGlobalStringPtr(", ", "enum.tostring.comma"));
                }
                llvm::Value* fieldStr = builder->CreateGlobalStringPtr("?", "enum.tostring.q");
                llvm::Value* fieldVal = extractEnumVariantField(value, payloadTy, i);
                if (fieldVal) {
                    llvm::Value* converted = generateToStringCall(fieldVal, fieldVal->getType(), nullptr, loc);
                    if (converted) fieldStr = converted;
                }
                acc = concatFragment(acc, fieldStr);
            }
            acc = concatFragment(acc, builder->CreateGlobalStringPtr(")", "enum.tostring.rparen"));
        }

        if (!acc) acc = builder->CreateGlobalStringPtr("", "enum.tostring.empty");
        builder->CreateBr(mergeBB);
        resultPhi->addIncoming(acc, tagToBB[vt.second]);
    }

    builder->SetInsertPoint(defaultBB);
    llvm::Value* unknownStr = builder->CreateGlobalStringPtr(prefix, "enum.tostring.unknownstr");
    builder->CreateBr(mergeBB);
    resultPhi->addIncoming(unknownStr, defaultBB);

    builder->SetInsertPoint(origBB);
    llvm::SwitchInst* switchInst = builder->CreateSwitch(tag, defaultBB, static_cast<unsigned>(tagToBB.size()));
    for (const auto& kv : tagToBB) {
        auto* tagConst = llvm::cast<llvm::ConstantInt>(llvm::ConstantInt::get(int64Type, kv.first, true));
        switchInst->addCase(tagConst, kv.second);
    }

    // Continue emitting the rest of the function inside the merge block so the
    // forwarded enum string is dominated and the block terminates normally.
    builder->SetInsertPoint(mergeBB);
    return resultPhi;
}

// Enhanced string concatenation that handles mixed types by converting non-strings to strings
llvm::Value* LLVMCodegen::generateMixedStringConcatenation(llvm::Value* leftValue, llvm::Value* rightValue,
                                                         vyb::ast::TypeNode* leftTypeNode, vyb::ast::TypeNode* rightTypeNode, SourceLocation loc) {
    if (!leftValue || !rightValue) {
        logError(loc, "Invalid operands for mixed string concatenation");
        return nullptr;
    }

    llvm::Value* leftString = nullptr;
    llvm::Value* rightString = nullptr;

    // Determine if left operand is already a string
    std::string leftTypeName = resolveTypeAliasToBaseName(leftTypeNode);
    bool leftIsString = (leftValue->getType() == int8PtrType) || (leftTypeName == "String");
    // Also check for String struct type
    if (leftValue->getType()->isStructTy()) {
        llvm::StructType* st = llvm::cast<llvm::StructType>(leftValue->getType());
        if (st->getNumElements() == 2 && st->getElementType(0)->isPointerTy()) leftIsString = true;
    }

    // Determine if right operand is already a string
    std::string rightTypeName = resolveTypeAliasToBaseName(rightTypeNode);
    bool rightIsString = (rightValue->getType() == int8PtrType) || (rightTypeName == "String");
    // Also check for String struct type
    if (rightValue->getType()->isStructTy()) {
        llvm::StructType* st = llvm::cast<llvm::StructType>(rightValue->getType());
        if (st->getNumElements() == 2 && st->getElementType(0)->isPointerTy()) rightIsString = true;
    }

    // Convert left operand to string if needed
    if (leftIsString) {
        leftString = leftValue;
    } else {
        leftString = generateToStringCall(leftValue, leftValue->getType(), leftTypeNode, loc);
        if (!leftString) {
            logError(loc, "Failed to convert left operand to string");
            return nullptr;
        }
    }

    // Convert right operand to string if needed
    if (rightIsString) {
        rightString = rightValue;
    } else {
        rightString = generateToStringCall(rightValue, rightValue->getType(), rightTypeNode, loc);
        if (!rightString) {
            logError(loc, "Failed to convert right operand to string");
            return nullptr;
        }
    }

    // Now concatenate the two strings
    return generateStringConcatenation(leftString, rightString, loc);
}

} // namespace vyb
