# Developer tooling: dependencies, formatter, LSP, REPL (#154)

## Dependency resolution — current scope and staging

**Shipped:** local **path** dependencies. A `[dependencies]` entry with
`name = { path = "relative/dir" }` is resolved by `vyb build`
(`project_module_paths`): the directory is added to the module search path and
its modules are importable in the project.

The manifest struct (`include/vyb/manifest.hpp`) already models all three source
kinds -- `path`, `git`, and `version` -- but **only `path` is resolved by the
current build** (the `git` and `version` variants parse but are not fetched).

**Staged (not implemented):** remote git / version resolution. When tackled,
the design is:

- `git` deps: resolve `url` + a `rev`/`tag`/`branch` by fetching the repo into a
  content-addressed cache directory (e.g. `~/.local/share/vyb/deps/` or a
  per-project `.vyb/deps`), checking out the pinned ref, and adding it as a
  module path. The cache is keyed by commit so identical pins are never fetched
  twice.
- `version` deps: resolve against the published **release** of the project
  (git tags/`vX.Y.Z`) -- there is no separate package registry yet; "version"
  means a tagged git commit.
- Lockfile: record the resolved commit hash so builds are reproducible.
- Acceptance: `vyb build` fetches a git dep, uses it, and is reproducible across
  machines; a missing/invalid ref fails with a clear error and no partial state.

Until the resolver lands, remote deps should be pinned by the project owner as
vendored path deps (clone once, reference by path), which already works.

## Post-release tooling roadmap (in order)

After the safety/release validation now in CI (#160 build profiles + #158
CTest/reproducible evidence), add these in order:

1. **Formatter** -- deterministic `vyb fmt` (whitespace/layout only, no
   semantic rewrites) driven by the existing token stream, so it can never
   reorder or drop code. Round-trip must be a no-op (fixpoint) and should per
   file under version control before being trusted.
2. **LSP** -- a `vyb lsp` server over stdio implementing the Language Server
   Protocol on top of the parser + semantic analyzer (diagnostics, then
   hover/go-to-definition once the index is in place). Uses the compiler as a
   library, not a subprocess-per-keystroke.
3. **REPL** -- a `vyb repl` interactive loop that JIT-compiles each statement
   (mirroring the existing single-file JIT pipeline) and prints the result.
