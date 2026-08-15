// SPDX-License-Identifier: Apache-2.0

// Include necessary headers
#include "vyb/vre/llvm/codegen.hpp"
#include <llvm/IR/Function.h>

namespace vyb {

// Implementation of getCurrentFunction method
llvm::Function* LLVMCodegen::getCurrentFunction() {
    return currentFunction;
}

} // namespace vyb
