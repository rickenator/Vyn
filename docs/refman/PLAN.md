# Refman Generator — PLAN

A utility that scans the Vyb source and emits a **hyperlinked reference manual**
under `docs/refman/`. Prose is reused from existing `#`/`//` doc comments, but the
**core deliverable is the inter-relationship graph** — modules, files, symbols,
and how they reference each other — even where prose is absent.

## Source scope (v1)

Scan all `stdlib/**/*.vyb`: **18 files** across **13 namespaces** (~3,100 LOC).

- Single-file modules: `agents`, `asyncs`, `channels`, `collections`, `http`,
  `https`, `io`, `network`, `tasks`, `threads`, `time`, `tls` (each `mod.vyb`).
- Root module: `prelude.vyb`.
- Multi-file namespace (a directory = one module page, one section per file):
  - `core/` → `aspects.vyb`, `iter.vyb`, `math.vyb`, `prelude.vyb`, `result.vyb`

**Module identity** = the top-level namespace directory (or root for `prelude`).
An import `core::iter::{Iterator}` is an **edge** from the importing module to the
`core` module, anchored at the `iter.vyb` → `Iterator` symbol.

Compiler-internals (`src/`, C++) is out of scope for v1 (possible v2).

## Relationship model (the center of the tool)

Every entity is a node; the refman renders typed, hyperlinked edges.

### Node kinds
- `module` — namespace dir, `file` — one `*.vyb`, `symbol` — an exported decl.
- `symbol` kinds: `fn`, `macro?` (none yet), `struct`, `enum`, `aspect`, `bind`, `type`.

### Edge types (each becomes a hyperlink in the rendered pages)
1. **import** — `import A::{x, y}` / `import A` / `share(all) import A::…` (re-export)
   → edge module→module(symbol).
2. **export** — `share(all)` decl → edge file→symbol (defines), and module→symbol.
3. **compose** — `struct` field / `enum` member / `bind` extent → edge symbol→type.
4. **implement** — `bind XOps -> X` (and generic `bind<T…>`); aspect `XOps` provides
   the contract, the bind target is the concrete type → edge aspect→struct/type.
5. **uses-type** — any param/return/field type that names a known symbol across
   modules (e.g. `https` returning `HttpResponse` from `http`) → cross-module edge.
6. **calls-runtime** — a function body referencing `vyb_net_*` / `vyb_tls_*` /
   `vyb_io_*` etc. → edge symbol→`runtime/vyb_runtime.c:#L<line>`.
7. **calls / invokes** — a function body calling another *exported* Vyb symbol
   (call-shaped identifier), or `ident.method(` member access → intra/cross edges.
8. **prose-ref** — a backtick identifier (`` `socket_local_port()` ``) in a doc
   comment that matches a known symbol → auto-link.
9. **mentions-type** — generics like `Vec<T>`, `Self`, bound aspects
   (`K<Hashable, Equatable>`) → edge to the bound aspect/type.

### Derived / aggregate views
- **Dependency graph** (imports) + reverse **"imported by"** per module.
- **Uses** per module: every foreign symbol pulled in, with a link to its owner.
- **Fan-in / fan-out** per module and per symbol (referenced by N modules).
- **Core, non-core** split (who depends on `core`, who depends on `error`).
- Cross-module **type consumers** (who builds/handles `HttpResponse`, `TcpStream`…).

## Parsing strategy

No full compiler pass — a lightweight, **brace-aware tokenizer** tuned to the stdlib:

- Strip `#` and `//` comments, but *capture* contiguous comment blocks just above a
  declaration as that symbol's doc text (the existing stdlib convention).
- Recognise top-level declarations:
  `share(all)`, `import`, `struct N`, `enum N`, `aspect N`, `bind[<T>] A -> B`,
  and bare function decls `name(params)<Ret> -> {`.
- Signatures: `name(a<Type>, b<Type>)<Ret>` → record param names/types and return type;
  handle `self<…>`, `<T>` generics, `Self`, `fn(T) -> T`.
- Structural decls (`struct`/`enum`/`aspect`/`bind`) read their member lists between
  the braces.
- Skip function *bodies* (we only mine them for call-shaped identifiers + runtime
  refs via a conservative regex over the lines inside the fn body).

### Runtime map
Scan `runtime/vyb_runtime.c` for `VYB_WEAK … __vyb_name(…)` and record the start
line + the preceding contiguous comment → a `__vyb_* → C line` lookup so
`calls-runtime` edges point into the C source (`…/vyb_runtime.c#L<line>`).

## Output (`docs/refman/`)

Markdown (GitHub-rendered), deterministic, with explicit `<a id="…">` anchors:

- `index.md` — module table (loc, files, exports, fan-in/fan-out), dependency
  overview, counts by node/edge kind, and a cross-index nav line.
- `<module>.md` — per-source-module page: module header prose, **imports**,
  **imported-by**, per-`file` sections, then per-symbol entries (signature + doc +
  relationship list with fan-in and hyperlinks).
- `functions.md`, `types.md`, `aspects.md`, `runtime.md` — cross-module indexes
  (all symbols, grouped, linked back to owners) so discovery is symbol-first.
- `interfaces.md` — shared cross-module structs/enums and who imports, re-exports,
  or uses them in a signature (the "who builds/handles `HttpResponse`" view).
- `graph.json` — machine-readable **nodes** (module/file/symbol records with
  kind, signature, summary, fan-in) and **edges** (for tooling/CI).
- Every generated page carries a provenance header:
  `<!-- generated by tools/refman.py; do not edit -->
     source: <stdlib paths> ; tool: <tools/refman.py> ; commit: <git rev>`.

## Drift / CI

`--check` (and a Makefile/CI target) fails when:
- an imported symbol is not a `share(all)` export of its provider,
- a `prose-ref` or `uses-type` target is unresolved,
- a regenerated `docs/refman/` diffs from the committed tree (catches man drift).

## Tool design

`tools/refman.py` — Python 3, stdlib-only (`re`, `pathlib`, `argparse`, `json`).
`--emit DIR` writes/overwrites `docs/refman/`; `--check` validates only.
Deterministic (sorted output) so a clean tree regenerates byte-identical.

## Build milestones

1. **[x] M0 scanner** — walk stdlib files; tokenize decls, signatures, comments;
   emit a raw JSON symbol table (per module/file/symbol).
2. **[x] M1 relations** — imports, exports, compose/implement, uses-type, runtime
   refs, calls, prose-refs; build the edge set.
3. **[x] M2 render** — index + per-module + cross indexes + anchors; emit `graph.json`.
4. **[x] M3 runtime + check** — runtime map; `--check` drift/validation.
5. **[x] M4 wire** — first-run generate, commit `docs/refman/`; optional CI target.
6. **[x] M5 polish** — per-file sections on multi-file module pages (`core`,
   `error`), module + per-symbol fan-in/fan-out views, a CI `--check` target,
   a cross-module type-consumer view (`interfaces.md`), fuller `graph.json`
   nodes, and a hand-written prose intro for the thin `prelude` header.

## Constraints / gotchas

- Module identity = directory namespace; an import can address a sub-file
  (`core::iter`), so edges anchor at file+symbol.
- `share(all)` re-exports re-export a foreign symbol (e.g. `https` re-exports
  `HttpResponse`); treat as re-edge, not a duplicate owner.
- The compiler has a real module resolver (`src/module_registry.cpp`) whose
  export/closure model surfaced resolution quirks during TLS work — this tool
  is a *documentation* projection, not a substitute; keep the two consistent via
  `--check`.

## Explicit non-goals
- Compiler-C++ internals refman.
- HTML bundle/search, "popularity ranking" of comments (later).
- Executing/normalizing Vyb code to extract true call graphs (conservative
  lexical call-mine only).
