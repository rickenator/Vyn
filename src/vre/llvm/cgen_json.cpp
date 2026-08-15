// SPDX-License-Identifier: Apache-2.0

#include "vyb/vre/llvm/codegen.hpp"
#include "vyb/parser/ast.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DerivedTypes.h>

namespace vyb {

// Implementation for print function (without newline) in the Vyb language
llvm::Function* LLVMCodegen::getPrintFunction() {
    // Check if the print function has already been declared in the module
    llvm::Function* printFunc = module->getFunction("print");

    if (!printFunc) {
        // Create function signature for print
        std::vector<llvm::Type*> paramTypes = {int8PtrType};
        llvm::FunctionType* printType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),  // Return type: void
            paramTypes,                       // Parameters: (char*)
            false                             // Not vararg
        );

        // Declare the function in the module
        printFunc = llvm::Function::Create(
            printType,
            llvm::Function::ExternalLinkage,
            "print",
            module.get()
        );

        // Set parameter names for readability
        auto args = printFunc->arg_begin();
        args->setName("str");
    }

    return printFunc;
}

// Implementation for printing integers
llvm::Function* LLVMCodegen::getPrintIntFunction() {
    // Check if the print_int function has already been declared in the module
    llvm::Function* printIntFunc = module->getFunction("print_int");

    if (!printIntFunc) {
        // Create function signature for print_int
        std::vector<llvm::Type*> paramTypes = {int64Type}; // Assuming 64-bit integers
        llvm::FunctionType* printIntType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),  // Return type: void
            paramTypes,                       // Parameters: (int64)
            false                             // Not vararg
        );

        // Declare the function in the module
        printIntFunc = llvm::Function::Create(
            printIntType,
            llvm::Function::ExternalLinkage,
            "print_int",
            module.get()
        );

        // Set parameter names for readability
        auto args = printIntFunc->arg_begin();
        args->setName("value");
    }

    return printIntFunc;
}

// Implementation for beginning a JSON object
llvm::Function* LLVMCodegen::getBeginJsonObjectFunction() {
    // Check if the function has already been declared in the module
    llvm::Function* beginJsonFunc = module->getFunction("__vyb_begin_json_object");

    if (!beginJsonFunc) {
        // Create function signature
        llvm::FunctionType* funcType = llvm::FunctionType::get(
            int8PtrType,                            // Return type: void* (json object handle)
            {},                                     // No parameters
            false                                   // Not vararg
        );

        // Declare the function in the module
        beginJsonFunc = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            "__vyb_begin_json_object",
            module.get()
        );
    }

    return beginJsonFunc;
}

// Implementation for adding a field to a JSON object
llvm::Function* LLVMCodegen::getAddJsonFieldFunction() {
    // Check if the function has already been declared in the module
    llvm::Function* addFieldFunc = module->getFunction("__vyb_add_json_field");

    if (!addFieldFunc) {
        // Create function signature
        std::vector<llvm::Type*> paramTypes = {
            int8PtrType,                            // void* json_obj
            int8PtrType,                            // const char* field_name
            llvm::PointerType::getUnqual(int8Type), // void* value
            int8PtrType                             // const char* type_name
        };

        llvm::FunctionType* funcType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),        // Return type: void
            paramTypes,                             // Parameters
            false                                   // Not vararg
        );

        // Declare the function in the module
        addFieldFunc = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            "__vyb_add_json_field",
            module.get()
        );

        // Set parameter names for readability
        auto args = addFieldFunc->arg_begin();
        args->setName("json_obj");
        (++args)->setName("field_name");
        (++args)->setName("value");
        (++args)->setName("type_name");
    }

    return addFieldFunc;
}

// Implementation for finalizing a JSON object
llvm::Function* LLVMCodegen::getEndJsonObjectFunction() {
    // Check if the function has already been declared in the module
    llvm::Function* endJsonFunc = module->getFunction("__vyb_end_json_object");

    if (!endJsonFunc) {
        // Create function signature
        std::vector<llvm::Type*> paramTypes = {
            int8PtrType                            // void* json_obj
        };

        llvm::FunctionType* funcType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*context),       // Return type: void
            paramTypes,                            // Parameters
            false                                  // Not vararg
        );

        // Declare the function in the module
        endJsonFunc = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            "__vyb_end_json_object",
            module.get()
        );

        // Set parameter names for readability
        auto args = endJsonFunc->arg_begin();
        args->setName("json_obj");
    }

    return endJsonFunc;
}

} // namespace vyb
