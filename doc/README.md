# Vyb Documentation Index

> **Not authoritative.** The files under `doc/` are design notes, proposals, and
> dated reviews. They may describe features as planned or stubbed and can lag
> reality. The **authoritative** reference is the [Programmer's Guide and the
> generated pages under `docs/refman/`](../docs/refman/PROGRAMMERS_GUIDE.md).
> See [DOCS_POLICY.md](DOCS_POLICY.md) for the maintenance checklist.

This directory contains design and review documentation for the Vyb programming
language. Superseded documents are folded into the repository history.

---

## Start Here

| Document | Purpose |
|----------|---------|
| [`../README.md`](../README.md) | Project overview, quick start, examples |
| [`../TODO.md`](../TODO.md) | Living road-to-1.0 checklist |
| [`../CHANGELOG.md`](../CHANGELOG.md) | Changelog of shipped features and fixes |
| [`FEATURE_STATUS.md`](FEATURE_STATUS.md) | Current implementation status per feature |

---

## Language Reference

| Document | Purpose |
|----------|---------|
| [`Canonical_Reference_Syntax.md`](Canonical_Reference_Syntax.md) | **Authoritative syntax** (`my`/`our`/`their`, `freedom`, sized types, signatures) |
| [`MATCH_SYNTAX.md`](MATCH_SYNTAX.md) | Pattern matching (`match`/`select`) |
| [`LAMBDAS.md`](LAMBDAS.md) | Lambda expressions and closures |
| [`VEC_ITERATION.md`](VEC_ITERATION.md) | `Vec<T>` and `for (item in vec)` |
| [`Memory_Operations.md`](Memory_Operations.md) | `freedom` blocks and raw pointer operations |
| [`OWNERSHIP_MILD.md`](OWNERSHIP_MILD.md) | `mild<T>` weak references |
| [`STRING_IMPLEMENTATION.md`](STRING_IMPLEMENTATION.md) | `String` representation and methods |
| [`Intrinsics.md`](Intrinsics.md) | Intrinsic functions and core syntax |

---

## Design Documents

| Document | Purpose |
|----------|---------|
| [`ERROR_TRAP.md`](ERROR_TRAP.md) | `fail`/`trap` error propagation design |
| [`TRAIT_SYSTEM_DESIGN.md`](TRAIT_SYSTEM_DESIGN.md) | Aspect system design (`aspect`/`bind`) |
| [`WHY_TRAITS_NOT_CLASSES.md`](WHY_TRAITS_NOT_CLASSES.md) | Rationale for aspect/bind over classes |
| [`ASPECT_BOUNDS.md`](ASPECT_BOUNDS.md) | Aspect bounds and bounded generics |
| [`MONOMORPHIZATION_DESIGN.md`](MONOMORPHIZATION_DESIGN.md) | Compile-time monomorphization design |
| [`FFI_DESIGN.md`](FFI_DESIGN.md) | Foreign Function Interface design |
| [`MODULE_FFI_BINARY_ROADMAP.md`](MODULE_FFI_BINARY_ROADMAP.md) | Module system / FFI / binary roadmap |
| [`INTROSPECTION_DESIGN.md`](INTROSPECTION_DESIGN.md) | `typeof` / `typename` introspection |
| [`Async_Programming_Debug_System.md`](Async_Programming_Debug_System.md) | Async/await design and debugging |
| [`AGENTS_DESIGN.md`](AGENTS_DESIGN.md) | Lightweight isolated message-passing units (agents) |
| [`Auto_Serialization_Main_Returns.md`](Auto_Serialization_Main_Returns.md) | Typeful JSON serialization of returns |
| [`VRE.md`](VRE.md) | Vyb Runtime Environment internals |
| [`RUNTIME.md`](RUNTIME.md) | Runtime design (mutability, ownership, references) |
| [`bundles_and_sharing.md`](bundles_and_sharing.md) | Module `bundle` and `share` visibility |
| [`module_visibility.md`](module_visibility.md) | Module resolution and visibility notes |
| [`stdlib_layout.md`](stdlib_layout.md) | Standard library module layout |
| [`HTTP_CAPABILITY_BOUNDARIES.md`](HTTP_CAPABILITY_BOUNDARIES.md) | HTTP/HTTPS client & server capability boundary, non-goals, and error model |
| [`NETWORK_ERROR_CONTRACTS.md`](NETWORK_ERROR_CONTRACTS.md) | `network` module raw-socket / async-I/O / UDP-peer error contracts |

---

## AST Reference

| Document | Purpose |
|----------|---------|
| [`AST_Overview.md`](AST_Overview.md) | AST node hierarchy overview |
| [`AST_Core.md`](AST_Core.md) | Core AST nodes |
| [`AST_Declarations.md`](AST_Declarations.md) | Declaration nodes |
| [`AST_Expressions.md`](AST_Expressions.md) | Expression nodes |
| [`AST_Statements.md`](AST_Statements.md) | Statement nodes |
| [`AST_Types.md`](AST_Types.md) | Type nodes |
| [`AST_Literals.md`](AST_Literals.md) | Literal nodes |
| [`AST_Patterns.md`](AST_Patterns.md) | Pattern nodes |
| [`AST_Design_Considerations.md`](AST_Design_Considerations.md) | AST design considerations |

---

## Development

| Document | Purpose |
|----------|---------|
| [`../UPDATE_LOG.md`](../UPDATE_LOG.md) | Working implementation audit / backlog |
| [`FUTURES_PROPOSALS.md`](FUTURES_PROPOSALS.md) | Non-committal post-milestone proposals (LSP, REPL, fmt/doc/test, registry, language features, debt) |
| [`Development_Guide.md`](Development_Guide.md) | Development guide |
| [`Test_Harness_Guide.md`](Test_Harness_Guide.md) | Test harness and analysis system |
| [`TEST_PLAN.md`](TEST_PLAN.md) | Test plan and quality assurance |

---

*Vyb is not Rust. It is not C++. It is its own thing.*
