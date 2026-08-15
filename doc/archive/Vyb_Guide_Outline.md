
# Vyb Programmer’s Guide — Detailed Outline

1. **Introduction**
   1.1. Why Vyb? (Goals & Philosophy)
   1.2. Key Features at a Glance
   1.3. Setup & Tooling (Compiler, REPL, Package Manager)

2. **Quick Start**
   2.1. Hello, Vyb World
   2.2. Building & Running
   2.3. First Ownership Example

3. **Core Syntax**
   3.1. Identifiers, Keywords & Comments
   3.2. Modules, Imports & `share(...)`
   3.3. Indentation vs. Braces

4. **Declarations**
   4.1. Default Mutable Shorthand (`<T> name`)
   4.2. Immutable `const<T>`
   4.3. Type Inference (`auto`)
   4.4. Ownership Shorthands (`my<T>`, `our<T>`, `borrow()`, `view()`)

5. **Functions & Control Flow**
   5.1. Function Signatures (`fn<T> name(...) -> …`)
   5.2. Single-Expression vs. Block vs. Indented Bodies
   5.3. Conditionals (`if`/`else`)
   5.4. Loops (`while`, `for … in`)
   5.5. Exception Handling (`try`/`catch`/`finally`)
   5.6. `defer`, `async`/`await`

6. **Types & Generics**
   6.1. Built-in Types (`Int`, `Bool`, `String`, etc.)
   6.2. Composite Types (Tuples, Arrays, Slices)
   6.3. User-defined Types (`struct`, `class`, `enum`)
   6.4. Generics & Type Parameters

7. **Expressions & Operators**
   7.1. Literal Forms (numbers, strings, lists, objects)
   7.2. Binary & Unary Operators
   7.3. Method Calls & Field Access (`x.y`, `T::method`)
   7.4. Comprehensions & Ranges

8. **Ownership & Memory Model**
   8.1. `my<T>`, `our<T>`, `their<T>` Semantics
   8.2. Borrow-Checker Rules
   8.3. `freedom {}` and Raw Pointers (`loc`, `at`, `addr`, `from`)
   8.4. Intrinsics Reference

9. **Concurrency Primitives**
   9.1. Threads & `spawn()`
   9.2. Mutexes & `Mutex<T>`
   9.3. Condition Variables & `Condvar`
   9.4. Channels & Message Passing

10. **Standard Library Overview**
    10.1. Collections (`Vec`, `Map`, `Set`)
    10.2. I/O (`fs`, `net`, `http`)
    10.3. Formatting & Logging
    10.4. Time & Randomness

11. **Patterns & Best Practices**
    11.1. Resource Management
    11.2. Error Handling Idioms
    11.3. Performance Tips
    11.4. Design Patterns in Vyb

12. **Deep Dives**
    12.1. AST Internals & Parser
    12.2. Semantic Analysis
    12.3. Codegen & JIT Integration
    12.4. Embedding Vyb in C/C++

13. **Appendices**
    A. Full EBNF Grammar
    B. Manifest (`vyb.toml`) Format
    C. Cheat-Sheets & Quick References
    D. Glossary
