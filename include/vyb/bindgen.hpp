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

} // namespace bindgen
} // namespace vyb
