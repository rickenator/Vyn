// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

namespace vyb {
namespace bindgen {

// Generates a Vyb module from a C header's declarations.
//
// `vyb bindgen <header.h>` parses a supported C subset (function declarations,
// enum/struct typedefs, scalar/pointer types) and returns a `.vyb` source
// string containing `extern "C" { ... }` blocks, `#[repr(C)] struct`s, and
// `enum`s, all `share(all)` so they re-export on import.
//
// Supported subset (MVP): typedefs, `struct`/`enum` declarations, simple
// scalar/pointer parameter and return types, and trailing varargs (`...`).
// Preprocessor directives and comments are ignored. Complex constructs that
// cannot be mapped (function pointers, bitfields, arrays-as-params) are
// skipped with a warning collected in the returned diagnostics.
// Returns the generated Vyb source. If `warnings` is non-null, it is filled
// with human-readable notes about declarations that could not be mapped.
std::string generateBindings(const std::string& headerSource,
                             std::vector<std::string>* warnings = nullptr);

#ifdef VYB_BINDGEN_LIBCLANG
// libclang-based full-preprocessor backend, selected with `vyb bindgen
// <header.h> --full`. Reads the header from disk so the preprocessor can
// expand `#include` (e.g. `<stdint.h>` typedefs) and evaluate conditionals
// (`#if`/`#ifdef`), mapping typedefs through their canonical types
// (`int32_t` -> CInt on this platform, `uint64_t` -> CULong on LP64, and
// similarly for the other <stdint.h> widths). Object-like `#define` constants
// (numeric, string, or integer constant expressions) bind as shared constant
// functions; function-like macros bind as `Int`-typed Vyb functions over
// integer arithmetic. `cmdArgs` holds extra compiler flags passed to libclang
// (e.g. `-DUSE_64`); only declarations whose source is the input header are
// emitted (system/libc types are resolved but not rebound). Returns the
// generated Vyb source; parse failures return "" with a diagnostic in
// `warnings`.
std::string generateBindingsFull(const std::string& headerPath,
                                 const std::vector<std::string>& cmdArgs,
                                 std::vector<std::string>* warnings = nullptr);
#endif // VYB_BINDGEN_LIBCLANG

} // namespace bindgen
} // namespace vyb
