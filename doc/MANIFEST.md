# Manifest format (`vyb.toml`) — #164

`vyb build` / `vyb new` read a project manifest named `vyb.toml`. The parser is a
**deliberate, documented subset of TOML** — Vyb does not adopt a full TOML
implementation. This page is the contract: what is supported, what is rejected,
and the compatibility policy.

> New: remote modules fetched by `vyb mod install` are recorded into the same
> `[dependencies]` table — see
> [Remote module import (vyb mod install)](#remote-module-import-vyb-mod-install).

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

## Remote module import (`vyb mod install`)

Phase 2 of the SDK plan (issue **#175**): `vyb mod install` fetches a remote
`.vyb` module and registers it as a local `path` dependency so that it works
through the exact same `[dependencies]` machinery described above.

### Module spec grammar

A remote module is addressed by a spec of the form

```
github:owner/repo/path
```

where `path` is a single `.vyb` module file posted in that repository's file
tree (by convention under the repo's `bindings/` directory). The spec may carry
an optional integrity pin appended as a suffix:

```
github:owner/repo/path@sha256:HEX
```

`HEX` is the lowercase hex SHA-256 of the module file's bytes.

### What the command does, end to end

`vyb mod install github:owner/repo/path[@sha256:HEX]`:

1. **Fetch** — retrieves the specified `.vyb` module file, plus any sibling
   `.vyb` files it imports by relative path, from the repository's GitHub file
   tree. Fetching goes over the HTTPS standard library: the module spec's
   `owner/repo/path` is mapped onto `raw.githubusercontent.com`, i.e. the
   `/owner/repo/<branch>/<path>` web path for that file.

   > **Transport & trust:** the fetch uses the HTTPS stdlib's *verified* client
   > (`https_get_full_verified`) against the system CA bundle (`VYB_CA_BUNDLE` or
   > `/etc/ssl/certs/ca-certificates.crt`), so the peer certificate is validated
   > at the transport layer. Integrity is additionally pinned with the **sha256
   > TOFU** recorded in `vyb.lock` (computed on first install, re-verified on
   > re-install); a caller can force `@sha256:HEX` for a hard guarantee.

2. **Verify the pin (optional)** — if the spec carries `@sha256:HEX`, the
   fetched bytes are hashed with `crypto::sha256` and compared to the pin. A
   mismatch is a **hard error** that aborts the install and reports **both** the
   expected hexadecimal digest and the actual one computed from the fetched
   bytes, so the discrepancy is visible at a glance.
3. **Materialize** — writes the fetched module (and its relative-import
   siblings) into the project-local directory
   `.vybmod/owner/repo/` (mirroring the spec's `owner/repo`).
4. **Record in the lockfile** — appends `name -> { source, sha256 }` to
   `vyb.lock`, where `source` is the full spec the module was installed from and
   `sha256` its integrity digest.
5. **Register the dependency** — unless an entry already exists, adds
   `name = { path = ".vybmod/owner/repo" }` to the project's `vyb.toml`
   `[dependencies]` table, making the installed module resolve as an ordinary
   local path dependency. If the `name` is already present it is left
   untouched.

### Integrity model

- A provided `@sha256:HEX` pin is **re-verified on every re-install** — the
  fetched bytes are always re-hashed and compared, so a changed or tampered
  upstream file surfaces as the hard error described above.
- Content fetched **without** a pin is still recorded in `vyb.lock` with the
  `sha256` computed over the fetched bytes at install time, so even
  unpinned installs carry a verifiable digest for future re-checking.

### Offline / cache note

Already-fetched modules are **reused rather than re-fetched**: if
`.vybmod/owner/repo/` is already present and up to date, `vyb mod install`
skips the network fetch for that module, so repeat installs (including
pin re-verification against the cached bytes) work offline.

