// SPDX-License-Identifier: Apache-2.0

#ifdef VYB_BINDGEN_LIBCLANG

// Standalone `vyb-libclang` helper: runs the libclang full-preprocessor bindgen
// backend in its own process. The main `vyb` binary statically links LLVM, and
// a second copy of LLVM's CommandLine options inside libclang would collide at
// startup, so the helper keeps clang isolated and is spawned by `vyb bindgen
// --full`. Prints the generated `.vyb` to stdout and diagnostics to stderr.
#include "vyb/bindgen.hpp"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <header.h> [-D NAME[=VAL]] ...\n", argv[0]);
        return 2;
    }
    std::string headerPath = argv[1];
    std::vector<std::string> defs;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a.compare(0, 2, "-D") == 0) defs.push_back(a);
    }

    std::vector<std::string> warnings;
    std::string bindings = vyb::bindgen::generateBindingsFull(headerPath, defs, &warnings);
    if (bindings.empty()) {
        for (const auto& w : warnings) std::fprintf(stderr, "error: %s\n", w.c_str());
        return 1;
    }
    std::fputs(bindings.c_str(), stdout);
    for (const auto& w : warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
    return 0;
}

#endif // VYB_BINDGEN_LIBCLANG
