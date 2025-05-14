# Vyn Language Roadmap

**Last Updated:** May 13, 2025

This document outlines the planned features, ongoing development, and future considerations for the Vyn programming language.

## Core Development Focus
The current primary focus is on:
1.  **Vyn Runtime Environment (VRE):** Designing and implementing the foundational elements of the VRE. See `VRE.md` for preliminary design.
2.  **LLVM Integration:** Beginning the process of compiling Vyn AST to LLVM IR for native code generation.

## Project Structure and Organization

### Proposed Refactoring (Future Task)
As the Vyn project grows, a more structured directory layout will be beneficial for maintainability, readability, and scalability.

-   **Goal:** Improve overall project organization and clearly separate components.
-   **Proposed Source Structure (`src/`):**
    -   `ast/`: Implementations of AST (Abstract Syntax Tree) nodes.
    -   `parser/`: Lexer, parser logic, and related utilities.
    -   `sema/` or `analyzer/`: Semantic analysis, type checking.
    -   `codegen/` or `llvm_backend/`: LLVM IR generation and compilation logic.
    -   `vre/`: Vyn Runtime Environment components (core runtime, standard library C++ implementations).
    -   `core/` or `common/`: Common utilities, data structures, error reporting used across the compiler.
    -   `main/`: Main executable entry point, REPL implementation.
-   **Proposed Include Structure (`include/vyn/`):**
    -   `ast/`: Header files for AST node definitions.
    -   `parser/`: Header files for parser interfaces, token definitions.
    -   `sema/` or `analyzer/`: Header files for semantic analysis components.
    -   `codegen/` or `llvm_backend/`: Header files for code generation interfaces.
    -   `vre/`: Header files for VRE interfaces and public APIs.
    -   `core/` or `common/`: Header files for common utilities.
-   **Considerations:**
    -   This refactoring will require significant updates to `CMakeLists.txt` to correctly identify source files and manage include directories.
    -   All `#include` paths within the project files will need to be updated.
    -   **Timing:** This is a desirable refactoring but should likely be undertaken after initial VRE/LLVM milestones are achieved, or during a dedicated refactoring phase, to avoid disrupting the current development momentum on core features.

### Build System and Includes

-   **`vyn.hpp` as a Central Include:**
    -   The file `include/vyn/vyn.hpp` was initially conceived not just to house the EBNF grammar but to serve as a primary, top-level include file for essential Vyn definitions, core types, and widely used utilities.
    -   Utilities such as `source_location.hpp` and potentially `token.hpp` should ideally be directly included by `vyn.hpp` or be part of a core module that `vyn.hpp` exposes. This approach simplifies include management for different parts of the Vyn compiler and for any external tools that might interact with Vyn's core components.
    -   The EBNF grammar, if kept in `vyn.hpp`, should be clearly demarcated (e.g., within a large comment block) if the file also serves as an active header for code. Alternatively, the EBNF could reside purely in a design document like `AST.md`.

## Future Language & System Considerations

The following points were previously noted in `ROADMAP.txt` and are retained here for future planning:

-   **Template Placement**: Explore allowing template/generic declarations in more contexts beyond just module-level (e.g., within classes, functions) if deemed beneficial for advanced metaprogramming scenarios. Currently, templates are primarily module-level items.
-   **Dedicated Header Files (`.vyh` or similar)**: Investigate the potential need for dedicated header files (e.g., `.vyh`) for separating public interfaces, public template/generic definitions, and type declarations from implementation files (`.vyn`). This could improve organization, reduce compilation dependencies, and potentially speed up compile times for larger Vyn projects by allowing for more explicit module boundaries.

## Documentation
Key design and planning documents for Vyn:

-   `doc/AST.md`: Detailed description of the Vyn Abstract Syntax Tree nodes and structure.
-   `doc/VRE.md`: Preliminary design for the Vyn Runtime Environment.
-   `doc/ROADMAP.md`: This document, outlining project direction and future plans.

---
*This roadmap is a living document and is subject to change as development progresses.*
