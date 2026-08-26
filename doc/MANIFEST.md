# Manifest format (`vyb.toml`) — #164

`vyb build` / `vyb new` read a project manifest named `vyb.toml`. The parser is a
**deliberate, documented subset of TOML** — Vyb does not adopt a full TOML
implementation. This page is the contract: what is supported, what is rejected,
and the compatibility policy.

## Fields and tables

```
[package]                 # exactly one, first
  name    = "name"        # string
  version = "1.2.3"       # string (not a semver tuple)

[[bin]]                   # repeatable array-of-tables
  name = "bin-name"       # string
  path = "src/main.vyb"   # string

[dependencies]            # at most one
  <name> = { path = "../dir" }          # local path dependency (RESOLVED)
  <name> = { git = "https://…" }        # parsed, but REJECTED (#165)
  <name> = { version = "x.y.z" }        # parsed, but REJECTED (#165)
  <name> = "x.y.z"                      # shorthand → version → REJECTED (#165)
```

Unknown tables and unknown keys in any table are **ignored** (forward
compatibility): they never fail the build.

## Supported value forms (exact)

A `key = value` line may use exactly these value shapes:

- bare or double-quoted **strings** (`mylib`, `"mylib"`),
- **integers** (`0`, `42`) — parsed but only used where numeric,
- **inline tables** `{ k = v, k2 = v2 , ... }` — flat, one level, comma-separated.

## Rejected constructs (precise diagnostics)

Any value that is not one of the above is rejected with a line-numbered error,
so a user expecting full TOML gets an actionable message instead of silent
mis-parsing:

- **arrays** `[ … ]` / `[ [ … ] ]` → `unsupported TOML array value … at line N`.
- malformed table headers, unparseable `key = value`, keys before any `[section]`
  → line-numbered errors.
- Nested/dotted tables, multi-line or single-quoted strings, booleans, and floats
  are outside the subset (they are not detected specially; if they don't parse as
  a supported form they fall through as errors where applicable — do not rely on
  their exact wording).

## Compatibility policy

- A `vyb.toml` that stays within the subset is stable across versions.
- Writing full TOML (arrays, nested tables, dotted keys) is **not** supported and
  is treated as an error, not silently accepted — so a manifest can never
  accidentally rely on semantics the parser ignores.
- Local `path` dependencies are the only **resolved** source today; `git` and
  `version` sources parse but are rejected at build with `#165` (see
  `doc/DEVELOPER_TOOLING.md` for the staged resolver).
