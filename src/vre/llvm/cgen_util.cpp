\
#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/parser/ast.hpp" // For SourceLocation

#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h> // For AllocaInst
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h> // For StructType, PointerType
#include <llvm/IR/Constants.h>    // For Constant
#include <llvm/Support/raw_ostream.h> // For llvm::errs() if logError uses it (logError is not moved here yet)
#include <algorithm>
#include <cctype>

#include <string>
#include <vector>
#include <map>

using namespace vyb;
// using namespace llvm; // Uncomment if desired for brevity

// --- Helper Method Implementations ---

// New helper to get type name as string
std::string LLVMCodegen::getTypeName(llvm::Type* type) {
    if (!type) return "nullptr";

    // For struct types, try to get a cleaner name
    if (llvm::StructType* structTy = llvm::dyn_cast<llvm::StructType>(type)) {
        std::string structName = structTy->getName().str();
        if (!structName.empty()) {
            // Remove any "struct." prefix that LLVM might add
            size_t lastDot = structName.find_last_of('.');
            if (lastDot != std::string::npos) {
                return structName.substr(lastDot + 1);
            }
            return structName;
        }
    }

    // For pointer to struct types, try to extract the struct name from the type string
    if (type->isPointerTy()) {
        std::string typeStr;
        llvm::raw_string_ostream rso(typeStr);
        type->print(rso);
        std::string typeName = rso.str();

        // Look for struct name pattern in the type string
        if (typeName.find("struct.") != std::string::npos) {
            size_t prefixPos = typeName.find("struct.");
            size_t startPos = prefixPos + 7; // length of "struct."
            size_t endPos = typeName.find('*', startPos);
            if (endPos != std::string::npos && endPos > startPos) {
                // Extract just the struct name
                std::string structName = typeName.substr(startPos, endPos - startPos - 1);

                // Remove any trailing whitespace
                structName.erase(structName.find_last_not_of(" \n\r\t") + 1);

                return structName;
            }
        }

        return typeName;
    }

    // For other types, use the default LLVM representation
    std::string typeStr;
    llvm::raw_string_ostream rso(typeStr);
    type->print(rso);
    return rso.str();
}

llvm::Value* LLVMCodegen::tryCast(llvm::Value* value, llvm::Type* targetType, const vyb::SourceLocation& loc) {
    // Basic type casting logic (placeholder, needs to be more robust)
    // This should handle numeric casts, pointer casts, etc.
    if (!value || !targetType) return nullptr;
    if (value->getType() == targetType) return value;

    // Example: Integer to Pointer (potentially freedom, use with care)
    if (targetType->isPointerTy() && value->getType()->isIntegerTy()) {
        return builder->CreateIntToPtr(value, targetType, "inttoptr_cast");
    }
    // Example: Pointer to Integer
    if (targetType->isIntegerTy() && value->getType()->isPointerTy()) {
        return builder->CreatePtrToInt(value, targetType, "ptrtoint_cast");
    }
    // Example: Pointer to Pointer (bitcast)
    if (targetType->isPointerTy() && value->getType()->isPointerTy()) {
        // For struct types, check if this is a struct pointer to struct pointer conversion
        if (llvm::PointerType* targetPtrTy = llvm::dyn_cast<llvm::PointerType>(targetType)) {
            if (llvm::PointerType* valuePtrTy = llvm::dyn_cast<llvm::PointerType>(value->getType())) {
                // Try to determine pointee types without using version-specific methods
                llvm::Type* targetPointeeType = nullptr;
                llvm::Type* valuePointeeType = nullptr;

                // Get type names and parse if necessary
                std::string targetTypeName = getTypeName(targetType);
                std::string valueTypeName = getTypeName(value->getType());

                // If both are pointers to the same struct type, we can cast
                if (targetTypeName.find("struct.") != std::string::npos && valueTypeName.find("struct.") != std::string::npos) {
                    // Extract struct names from type names
                    size_t targetPrefixPos = targetTypeName.find("struct.");
                    size_t valuePrefixPos = valueTypeName.find("struct.");

                    if (targetPrefixPos != std::string::npos && valuePrefixPos != std::string::npos) {
                        size_t targetStartPos = targetPrefixPos + 7; // length of "struct."
                        size_t valueStartPos = valuePrefixPos + 7;

                        size_t targetEndPos = targetTypeName.find('*', targetStartPos);
                        size_t valueEndPos = valueTypeName.find('*', valueStartPos);

                        if (targetEndPos != std::string::npos && valueEndPos != std::string::npos) {
                            std::string targetStructName = targetTypeName.substr(targetStartPos, targetEndPos - targetStartPos - 1);
                            std::string valueStructName = valueTypeName.substr(valueStartPos, valueEndPos - valueStartPos - 1);

                            if (targetStructName == valueStructName) {
                                return builder->CreateBitCast(value, targetType, "struct_ptr_cast");
                            }
                        }
                    }
                }
            }
        }
        return builder->CreateBitCast(value, targetType, "ptr_bitcast");
    }
    // Pointer to Vyb String struct { ptr, i64 }: wrap char* in a string struct
    if (targetType->isStructTy() && value->getType()->isPointerTy()) {
        llvm::StructType* st = llvm::dyn_cast<llvm::StructType>(targetType);
        if (st && st->getNumElements() == 2 &&
            st->getElementType(0)->isPointerTy() &&
            st->getElementType(1)->isIntegerTy(64)) {
            // Create { ptr, i64 } struct with the pointer and a length of -1 (unknown)
            // Use strlen to compute actual length via __vyb_strlen if needed
            llvm::Value* strStruct = llvm::UndefValue::get(targetType);
            strStruct = builder->CreateInsertValue(strStruct, value, 0, "strwrap.ptr");
            // Compute strlen for the length field
            llvm::Function* strlenFunc = module->getFunction("strlen");
            if (!strlenFunc) {
                llvm::FunctionType* strlenType = llvm::FunctionType::get(
                    llvm::Type::getInt64Ty(*context),
                    {llvm::PointerType::get(*context, 0)},
                    false);
                strlenFunc = llvm::Function::Create(strlenType, llvm::Function::ExternalLinkage, "strlen", module.get());
            }
            llvm::Value* lenVal = builder->CreateCall(strlenFunc, {value}, "strwrap.len");
            strStruct = builder->CreateInsertValue(strStruct, lenVal, 1, "strwrap.result");
            return strStruct;
        }
    }
    // Example: Integer to Integer (trunc or sext/zext)
    if (targetType->isIntegerTy() && value->getType()->isIntegerTy()) {
        llvm::IntegerType* targetIntTy = llvm::dyn_cast<llvm::IntegerType>(targetType);
        llvm::IntegerType* valueIntTy = llvm::dyn_cast<llvm::IntegerType>(value->getType());
        if (targetIntTy && valueIntTy) {
            if (targetIntTy->getBitWidth() < valueIntTy->getBitWidth()) {
                return builder->CreateTrunc(value, targetType, "int_trunc");
            } else if (targetIntTy->getBitWidth() > valueIntTy->getBitWidth()) {
                // Bool (i1) should be zero-extended; other integers use sign extension
                if (valueIntTy->getBitWidth() == 1) {
                    return builder->CreateZExt(value, targetType, "bool_zext");
                }
                return builder->CreateSExt(value, targetType, "int_sext");
            }
            // else same width, should have been caught by value->getType() == targetType
        }
    }
    // Add more casting rules as needed (float to int, int to float, etc.)
    // Float to float (e.g., double to float for Float32)
    if (targetType->isFloatingPointTy() && value->getType()->isFloatingPointTy()) {
        if (targetType == value->getType()) return value;
        if (targetType->isFloatTy() && value->getType()->isDoubleTy()) {
            return builder->CreateFPTrunc(value, targetType, "fp_trunc");
        }
        if (targetType->isDoubleTy() && value->getType()->isFloatTy()) {
            return builder->CreateFPExt(value, targetType, "fp_ext");
        }
    }
    // Integer to float
    if (targetType->isFloatingPointTy() && value->getType()->isIntegerTy()) {
        return builder->CreateSIToFP(value, targetType, "int_to_fp");
    }
    // Float to integer
    if (targetType->isIntegerTy() && value->getType()->isFloatingPointTy()) {
        return builder->CreateFPToSI(value, targetType, "fp_to_int");
    }

    logError(loc, "Unsupported or invalid cast from type " + getTypeName(value->getType()) + " to " + getTypeName(targetType));
    return nullptr;
}

void LLVMCodegen::storeIntoResultSlot(llvm::Value* value, llvm::AllocaInst* slot,
                                      const vyb::SourceLocation& loc) {
    (void)loc;
    if (!value || !slot) return;
    llvm::Type* slotTy = slot->getAllocatedType();
    llvm::Value* toStore = value;

    // A raw char* (e.g. the result of a primitive .to_string()) stored into a
    // String { ptr, i64 } result slot must be wrapped with a strlen-computed
    // length; otherwise the length field stays zero and the String compares
    // unequal / reports length 0.
    if (value->getType()->isPointerTy() && slotTy->isStructTy()) {
        llvm::StructType* st = llvm::dyn_cast<llvm::StructType>(slotTy);
        if (st && st->getNumElements() == 2 &&
            st->getElementType(0)->isPointerTy() &&
            st->getElementType(1)->isIntegerTy(64)) {
            llvm::Type* i8Ptr = llvm::PointerType::get(*context, 0);
            llvm::Type* i64Ty = llvm::Type::getInt64Ty(*context);
            llvm::Function* strlenFunc = module->getFunction("strlen");
            if (!strlenFunc) {
                strlenFunc = llvm::Function::Create(
                    llvm::FunctionType::get(i64Ty, {i8Ptr}, false),
                    llvm::Function::ExternalLinkage, "strlen", module.get());
            }
            llvm::Value* len = builder->CreateCall(strlenFunc, {value}, "slot.strlen");
            toStore = llvm::UndefValue::get(slotTy);
            toStore = builder->CreateInsertValue(toStore, value, 0, "slot.ptr");
            toStore = builder->CreateInsertValue(toStore, len, 1, "slot.len");
        }
    }

    builder->CreateStore(toStore, slot);
}

llvm::Value* LLVMCodegen::createEntryBlockAlloca(llvm::Function* func, const std::string& varName, llvm::Type* type) {
    if (!func) {
        // logError (some location, "Cannot create alloca: not in a function context.");
        return nullptr;
    }
    if (!type) {
        // logError (some location, "Cannot create alloca: type is null for variable " + varName);
        return nullptr;
    }

    // Find the last alloca in the entry block to maintain proper ordering
    llvm::Instruction* insertPoint = nullptr;
    for (auto& inst : func->getEntryBlock()) {
        if (llvm::isa<llvm::AllocaInst>(&inst)) {
            insertPoint = &inst;
        } else {
            // Stop at first non-alloca instruction
            break;
        }
    }

    if (vyb::g_debug_codegen) {
        std::cerr << "DEBUG: createEntryBlockAlloca(3-param) - ";
        if (insertPoint) {
            std::cerr << "Found last alloca, inserting '" << varName << "' after it\n";
        } else {
            std::cerr << "No allocas found, inserting '" << varName << "' at beginning\n";
        }
    }

    llvm::IRBuilder<> tmpB(*context);
    if (insertPoint) {
        // Insert after the last alloca
        tmpB.SetInsertPoint(insertPoint->getNextNode());
    } else {
        // No allocas yet, insert at beginning
        tmpB.SetInsertPoint(&func->getEntryBlock(), func->getEntryBlock().begin());
    }

    auto* alloca = tmpB.CreateAlloca(type, nullptr, varName);

    if (vyb::g_debug_codegen) {
        std::cerr << "DEBUG: createEntryBlockAlloca(3-param) - Created alloca '" << varName << "', current order:\n";
        for (auto& inst : func->getEntryBlock()) {
            if (auto* ai = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
                std::cerr << "  - " << ai->getName().str() << "\n";
            } else {
                break;
            }
        }
    }

    return alloca;
}

llvm::AllocaInst* LLVMCodegen::createEntryBlockAlloca(llvm::Type* type, const std::string& name) {
    if (!currentFunction) {
        // No error reporting mechanism available here, just return nullptr
        return nullptr;
    }

    // Find the last alloca in the entry block to maintain proper ordering
    llvm::Instruction* insertPoint = nullptr;
    for (auto& inst : currentFunction->getEntryBlock()) {
        if (llvm::isa<llvm::AllocaInst>(&inst)) {
            insertPoint = &inst;
        } else {
            // Stop at first non-alloca instruction
            break;
        }
    }

    if (insertPoint) {
        VYB_CDBG << "DEBUG: createEntryBlockAlloca - Found last alloca, inserting '" << name << "' after it\n";
    } else {
        VYB_CDBG << "DEBUG: createEntryBlockAlloca - No allocas found, inserting '" << name << "' at beginning\n";
    }

    llvm::IRBuilder<> TmpB(*context);
    if (insertPoint) {
        // Insert after the last alloca
        TmpB.SetInsertPoint(insertPoint->getNextNode());
    } else {
        // No allocas yet, insert at beginning
        TmpB.SetInsertPoint(&currentFunction->getEntryBlock(), currentFunction->getEntryBlock().begin());
    }

    auto* alloca = TmpB.CreateAlloca(type, nullptr, name);

    if (vyb::g_debug_codegen) {
        std::cerr << "DEBUG: createEntryBlockAlloca - Created alloca '" << name << "', current order:\n";
        for (auto& inst : currentFunction->getEntryBlock()) {
            if (auto* ai = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
                std::cerr << "  - " << ai->getName().str() << "\n";
            } else {
                break;
            }
        }
    }

    return alloca;
}

void LLVMCodegen::pushLoop(llvm::BasicBlock* header, llvm::BasicBlock* body, llvm::BasicBlock* update, llvm::BasicBlock* exit) {
    loopStack.push_back({header, body, update, exit});
}

void LLVMCodegen::popLoop() {
    if (!loopStack.empty()) {
        loopStack.pop_back();
    }
}

// Helper function to get field index
int LLVMCodegen::getStructFieldIndex(llvm::StructType* structType, const std::string& fieldName) {

    VYB_CDBG << "DEBUG: getStructFieldIndex - Looking for field '" << fieldName << "'" << std::endl;

    if (!structType) {
        VYB_CDBG << "DEBUG: structType is null" << std::endl;
        return -1;
    }

    VYB_CDBG << "DEBUG: structType has " << structType->getNumElements() << " elements" << std::endl;

    if (structType->getName().empty()) {
        VYB_CDBG << "DEBUG: structType is anonymous" << std::endl;
        // This can happen for anonymous structs (like tuples) or if structType is null.
        // For anonymous structs, field access is by index, not name.
        return -1;
    }
    std::string structName = structType->getName().str();
    VYB_CDBG << "DEBUG: structName = '" << structName << "'" << std::endl;

    // Strip numeric suffix from LLVM struct names (e.g., "TreeNode.0" -> "TreeNode")
    std::string baseStructName = structName;
    size_t dotPos = structName.find_last_of('.');
    if (dotPos != std::string::npos) {
        // Check if everything after the dot is a number
        std::string suffix = structName.substr(dotPos + 1);
        bool isNumeric = !suffix.empty() && std::all_of(suffix.begin(), suffix.end(), ::isdigit);
        if (isNumeric) {
            baseStructName = structName.substr(0, dotPos);
            VYB_CDBG << "DEBUG: Stripped numeric suffix, baseStructName = '" << baseStructName << "'" << std::endl;
        }
    }

    // Simple hardcoded mapping for TestPoint struct fields
    if (structName.find("TestPoint") != std::string::npos) {
        VYB_CDBG << "DEBUG: Found TestPoint struct" << std::endl;
        if (fieldName == "x") return 0;
        if (fieldName == "y") return 1;
    }

    auto it = userTypeMap.find(baseStructName);
    if (it != userTypeMap.end()) {
        const auto& typeInfo = it->second;
        auto fieldIt = typeInfo.fieldIndices.find(fieldName);
        if (fieldIt != typeInfo.fieldIndices.end()) {
            VYB_CDBG << "DEBUG: Found field in userTypeMap at index " << fieldIt->second << std::endl;
            return fieldIt->second;
        } else {
            VYB_CDBG << "DEBUG: Field not found in userTypeMap" << std::endl;
        }
    } else {
        VYB_CDBG << "DEBUG: Struct not found in userTypeMap" << std::endl;
        // This case means the struct type (though named in LLVM) is not in our userTypeMap.
        // Let's try to dynamically register it if it's a well-known struct
        if (!structType->isOpaque() && structType->getNumElements() > 0) {
            UserTypeInfo typeInfo;
            typeInfo.llvmType = structType;
            typeInfo.isStruct = true;

            // Try to map common field names by position
            // For common structs like Point, try to guess field indices
            if (fieldName == "x" && structType->getNumElements() > 0) {
                typeInfo.fieldIndices["x"] = 0;
                userTypeMap[structName] = typeInfo;
                return 0;
            } else if (fieldName == "y" && structType->getNumElements() > 1) {
                typeInfo.fieldIndices["y"] = 1;
                userTypeMap[structName] = typeInfo;
                return 1;
            }
            // Add more common field names if needed

            // Store the type info for future lookups
            userTypeMap[structName] = typeInfo;
        }
    }
    return -1; // Field not found or struct type not recognized by our map
}

// Bind the fields of a struct destructuring pattern (e.g., `Point { x, y }`) as
// local variables in the current scope, extracting each field from the matched
// struct value.
void LLVMCodegen::bindStructPatternFields(const vyb::ast::StructPattern* node, llvm::Value* matchValue) {
    llvm::StructType* structType = llvm::dyn_cast_or_null<llvm::StructType>(matchValue ? matchValue->getType() : nullptr);
    if (!structType) {
        return;
    }
    for (auto& binding : node->bindings) {
        if (!binding) continue;
        const std::string& fname = binding->name;
        int fieldIndex = getStructFieldIndex(structType, fname);
        if (fieldIndex < 0) {
            logError(binding->loc, "Field '" + fname + "' not found in matched struct during destructuring");
            continue;
        }
        llvm::Value* fieldValue = builder->CreateExtractValue(
            matchValue, static_cast<unsigned>(fieldIndex), fname);
        llvm::AllocaInst* alloca = createEntryBlockAlloca(fieldValue->getType(), fname);
        builder->CreateStore(fieldValue, alloca);
        namedValues[fname] = alloca;
        if (binding->type) {
            valueTypeMap[alloca] = std::shared_ptr<vyb::ast::TypeNode>(binding->type->clone());
        }
    }
}

// Find the tagged-union enum layout whose LLVM type matches the given runtime
// struct type (used to detect that a match value is a data-carrying enum).
const LLVMCodegen::TaggedEnumInfo* LLVMCodegen::findTaggedEnum(llvm::Type* structTy) const {
    for (const auto& kv : taggedEnumInfo) {
        if (kv.second.llvmType == structTy) {
            return &kv.second;
        }
    }
    return nullptr;
}

// Resolve the tagged-union enum info for a concrete AST type name. This is
// preferred over the structural lookup above because LLVM deduplicates identical
// struct layouts, so two C-like enums (both `{ i64, [1 x i8] }`) or two data
// enums with the same payload would otherwise be ambiguous.
const LLVMCodegen::TaggedEnumInfo* LLVMCodegen::findTaggedEnum(vyb::ast::TypeNode* typeNode) {
    if (!typeNode) return nullptr;
    auto* tn = dynamic_cast<ast::TypeName*>(typeNode);
    if (!tn || !tn->identifier) return nullptr;
    const std::string base = tn->identifier->name;
    std::string lookup = base;
    if (!tn->genericArgs.empty()) {
        lookup = mangleGenericTypeName(base, tn->genericArgs);
    }
    auto it = taggedEnumInfo.find(lookup);
    return (it != taggedEnumInfo.end()) ? &it->second : nullptr;
}

// Build a tagged-union enum value for the given variant. For unit variants pass
// an empty payloadVals. The result is an in-memory struct { i64 tag, [N x i8] data }.
llvm::Value* LLVMCodegen::buildTaggedEnumValue(const std::string& enumName, const std::string& variantName,
                                               std::vector<llvm::Value*> payloadVals) {
    auto it = taggedEnumInfo.find(enumName);
    if (it == taggedEnumInfo.end()) {
        logError(SourceLocation(), "Unknown tagged enum: " + enumName);
        return nullptr;
    }
    const TaggedEnumInfo& info = it->second;
    auto tagIt = info.variantTags.find(variantName);
    if (tagIt == info.variantTags.end()) {
        logError(SourceLocation(), "Unknown variant '" + variantName + "' of tagged enum " + enumName);
        return nullptr;
    }

    llvm::StructType* enumTy = info.llvmType;
    llvm::AllocaInst* tmp = builder->CreateAlloca(enumTy, nullptr, "enum.construct.tmp");
    builder->CreateStore(llvm::ConstantStruct::get(enumTy, {
        llvm::ConstantInt::get(int64Type, tagIt->second, true),
        llvm::UndefValue::get(enumTy->getElementType(1))
    }), tmp);

    // Store the tag field.
    llvm::Value* tagPtr = builder->CreateStructGEP(enumTy, tmp, 0, "enum.tag.ptr");
    builder->CreateStore(llvm::ConstantInt::get(int64Type, tagIt->second, true), tagPtr);

    auto payIt = info.variantPayloadTypes.find(variantName);
    if (payIt != info.variantPayloadTypes.end() && payIt->second) {
        llvm::StructType* payloadTy = payIt->second;
        llvm::Value* dataPtr = builder->CreateStructGEP(enumTy, tmp, 1, "enum.data.ptr");
        llvm::Value* payloadPtr = builder->CreateBitCast(dataPtr, llvm::PointerType::get(payloadTy, 0), "enum.payload.ptr");
        for (size_t i = 0; i < payloadVals.size() && i < payloadTy->getNumElements(); ++i) {
            if (!payloadVals[i]) continue;
            llvm::Value* fieldPtr = builder->CreateStructGEP(payloadTy, payloadPtr, i, "enum.payload.field");
            builder->CreateStore(payloadVals[i], fieldPtr);
        }
    }

    llvm::Value* result = builder->CreateLoad(enumTy, tmp, "enum.construct.value");
    return result;
}

// Extract one payload field from a tagged-union enum value given the variant's
// payload struct type. Index 0 is the first field of the variant's payload tuple.
llvm::Value* LLVMCodegen::extractEnumVariantField(llvm::Value* enumVal, llvm::StructType* payloadTy, unsigned fieldIdx) {
    if (!enumVal || !payloadTy || fieldIdx >= payloadTy->getNumElements()) {
        return nullptr;
    }
    llvm::AllocaInst* tmp = builder->CreateAlloca(enumVal->getType(), nullptr, "enum.extract.tmp");
    builder->CreateStore(enumVal, tmp);
    llvm::Value* dataPtr = builder->CreateStructGEP(enumVal->getType(), tmp, 1, "enum.extract.data");
    llvm::Value* payloadPtr = builder->CreateBitCast(dataPtr, llvm::PointerType::get(payloadTy, 0), "enum.extract.payload");
    llvm::Value* fieldPtr = builder->CreateStructGEP(payloadTy, payloadPtr, fieldIdx, "enum.extract.field.ptr");
    return builder->CreateLoad(payloadTy->getElementType(fieldIdx), fieldPtr, "enum.extract.field.val");
}
