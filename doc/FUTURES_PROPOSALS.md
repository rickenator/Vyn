# Futures Proposals

This is a **non-committal** list of candidate work for Vyb after the current
milestone. It is *not* a roadmap: nothing here is scheduled, promised, or
ordered. Items are proposals that have been considered and found worth keeping
on the record; they may be picked up in any order, reshaped, deferred, or
dropped as priorities pan out.

**Status legend** (update as things happen):

- `proposed` — on the list, not started
- `in-progress` — actively being worked
- `shipped` — done; move the detail to `UPDATE_LOG.md` / `CHANGELOG.md` and mark here
- `deferred` — deliberately parked (note why)
- `dropped` — rejected or superseded (note what replaced it)

The canonical *committed* plan still lives in `TODO.md` (road-to-1.0) and
`UPDATE_LOG.md` (working audit). This file is the parking lot for everything
that is "someday, maybe, probably" rather than "must ship".

---

## Developer Experience

The language core is largely complete; the biggest remaining value is making
Vyb pleasant to use day-to-day. These items are independent of each other and
can land in any order.

- `proposed` **LSP (`vyb lsp`)** — go-to-definition, hover (type signatures +
  doc comments), completion (aspect methods, struct fields, imports), real-time
  diagnostics. The semantic analyzer already computes types and errors, so the
  LSP should reuse that pipeline rather than duplicate it. Highest-value single
  item in this list.
- `proposed` **REPL (`vyb repl`)** — JIT-backed interactive loop reusing the
  ORC executor; history + multiline editing; a `:type` command to print an
  expression's type. Good for dogfooding and demos.
- `proposed` **`vyb fmt`** — canonical code formatter (AST printer). One
  canonical style for the whole ecosystem.
- `proposed` **`vyb doc`** — HTML documentation generator from `///` doc
  comments; builds on the existing `tools/refman.py` refman generator.
- `proposed` **`vyb test`** — integrated runner for `*.test.vyb` files placed
  alongside source; wraps the existing `test/run_tests.py` harness.
- `proposed` **Parser error recovery** — report multiple errors per file
  instead of throwing on the first (synchronize to the next statement/declaration
  boundary, collect all errors before aborting). Improves LSP diagnostics and
  general DX; a natural prerequisite for good LSP diagnostics.

## Package Ecosystem

- `proposed` **Remote dependency resolution** — version/git dependency fetching
  plus registry-gated lock pins. Today only local `{ path = ... }` dependencies
  resolve (`vyb build` / `vyb.lock`).
- `proposed` **Package registry** — a central registry for published packages,
  the anchor that turns Vyb from "local projects" into an ecosystem.

## Language Features

The least-scoped cluster; each item should get its own design document before
implementation is proposed.

- `proposed` **Macros / metaprogramming** — compile-time code generation.
- `proposed` **Higher-kinded types** — type constructors as parameters.
- `proposed` **Compile-time function evaluation (CTFE)** — constant folding at
  compile time.
- `proposed` **`pipe` operator (`|>`)** — pipeline composition syntax.
- `proposed` **`with` scope blocks** — scoped configuration/ownership blocks.

## Architecture & Technical Debt

Real value, but large and risky; do opportunistically rather than as a release
gate.

- `proposed` **Separate semantic analysis from AST mutation** — immutable AST +
  a separate `TypeTable` (node ID → type), avoiding raw pointer storage in
  `expressionTypes`. Reduces fragile cross-pass dependencies.
- `proposed` **IR optimization as a separate phase** — extract an explicit
  `optimize(module)` step controllable per compilation target; expose `-O0`–`-O3`
  consistently for both JIT and AOT paths.

## Self-Hosting

- `proposed` **Self-hosting compiler** — Vyb written in Vyb. Long-term goal;
  the end-state proof that the language is complete enough to build itself.

---

*Last updated: 2026-08-21 (initial proposal list, post-T?-migration).*
