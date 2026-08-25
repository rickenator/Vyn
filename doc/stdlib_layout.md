# Standard Library Module Layout (Current Foundation)

This document is the canonical stdlib module layout for the current milestone.

## Discovery

Stdlib import resolution uses the normal module resolver search order documented
in `doc/module_visibility.md`, including:

1. `VYB_STDLIB` (if set)
2. Executable-relative probes (`<exe_dir>/../stdlib`, then `<exe_dir>/stdlib`)

No `--module-path` flag is required when one of those stdlib roots is available.

## Layout

```text
stdlib/
  prelude.vyb            # top-level prelude re-export module
  core/
    prelude.vyb          # canonical prelude contents
    result.vyb           # source-compat façade for compiler-builtin Result<T,E>
  collections/
    mod.vyb              # placeholder scaffold
  io/
    mod.vyb              # placeholder scaffold
```

## Prelude behavior (current decision)

Prelude is **explicit-only** right now.

- `core::prelude` is **not auto-imported**.
- `prelude` is **not auto-imported**.
- Users explicitly import whichever prelude path they want:
  - `import core::prelude`
  - `import prelude`

This keeps module behavior deterministic while Result/iterator/core-aspect
work is still evolving.

## Option/Result status

- The Rust-shaped `Option<T>` enum (`Some(value)` / `None`) has been removed;
  optional values use the native `T?` type (present payload / absent `?`, read
  via `else` or a `match`/`?` arm). `Result<T,E>` (`Ok` / `Err`) remains a
  built-in generic data enum registered directly in the compiler; it needs no
  `import`.
- `core::result` is a source-compat façade module retained for `import
  core::result` compatibility; the real `Result<T,E>` is the compiler builtin
  (see above) and takes precedence.
