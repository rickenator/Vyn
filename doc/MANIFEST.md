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

---

## Signed packages & root authority — Phase 4 (specification)

The TOFU/sha256-pin model above authenticates a module only against the
digest you *first observed*. That is a self-signed-cert-equivalent trust
anchor: it defends against tampering after first attach but not against a
first-use substitution, and nothing vouches for *who* published it. For
**official** bindings a defined **root of authority** and **signed packages**
are required. This is the locked Phase-4 spec — the next implementation step
after the smuggle transport.

### 4.1 Root of authority

- **Root = the official Vyb publisher signing key** (Ed25519, minisign,
  `UNTYPED` PK as published). The CLI pins it as the sole trust anchor for
  package authenticity.
- **Not a TLS CA.** The key *is* the root; TLS remains transport only, and a
  signed module stays authentic even if the TLS layer were compromised.
- **Distribution / pinning (out-of-band):** the public key ships in the SDK
  (`sdk/vendor/publisher_key.minisig.pub`), is embedded in the `vyb` binary as
  the `VYB_PUBLISHER_KEY` default, and is committed to the repo (`bindings/`).
  Rotating it (key compromise) is a documented, versioned, out-of-band event
  (a new key release + `INDEX.json` re-sign), not a runtime ceremony.

### 4.2 Posted-module signed layout

For an "official" binding at `<owner>/<repo>/bindings/<name>/`:

```
bindings/<name>/mod.vyb          # the module (as today)
bindings/INDEX.json              # {"bindings/<name>/mod.vyb": "<sha256>", ...}
bindings/INDEX.json.minisig      # Ed25519 signature over INDEX.json (publisher key)
```

`INDEX.json` is a plain JSON map from module path → lowercase sha256 (content
identity). The `.minisig` is the minisign-canonical detached signature of the
exact `INDEX.json` bytes under the publisher key.

### 4.3 Verification algorithm (`vyb mod install`)

1. **Fetch** the module + relative-import siblings (as today).
2. **Fetch** the posted `INDEX.json` and `INDEX.json.minisig`.
3. **Verify the signature first**: check `.minisig` validates against the
   pinned publisher key over the exact `INDEX.json` bytes. A bad signature is a
   hard error (`Error: publisher signature invalid — module is not official`).
4. **Verify the module hash**: `sha256(fetched bytes)` must equal
   `INDEX.json[module_path]`. Mismatch → hard error with both digests.
5. **Record** `name -> { source, sha256, index_sha256, publisher_sig: true }`
   in `vyb.lock`; re-verify all of it on re-install.
6. An explicit `@sha256:` pin on the spec AND/OR the signed-INDEX check may
   both apply; any failing condition is a hard error.

### 4.4 Trust tiers (explicit, user-selectable)

| Tier | Model | Default | Use |
| --- | --- | --- | --- |
| `T0` | TOFU pin (current) | yes | unofficial / convenience remotes |
| `T1` | signed-INDEX under pinned publisher key | opt-in (`--require-signed`) | **official** bindings; install fails without a valid signature |
| `T2` | registry-held publisher keys | future | beyond Phase 4 |

`--require-signed` (or a project `vyb.toml` `[mod] require_signed = true`) turns
T1 on: a module whose posted INDEX has no valid signature or whose signature or
hash does not verify is rejected outright. Without it, T0 TOFU remains available.

### 4.5 Threat model

- **TOFU (T0)** defeats tampering *after* first attach but not first-use
  substitution, and gives no publisher identity.
- **Signed (T1)** defeats substitution at any point *provided* the pinned root
  key itself was obtained safely (the point of out-of-band pinning in 4.1).
  Ordering matters: signature is verified independently of TLS, so a module is
  authentic **regardless** of TLS state.
- **Root-key compromise** is total (as with any single-root scheme); the
  mitigation is versioned key rotation out-of-band (4.1) and, later, T2.

### 4.6 Acceptance (Phase-4 gate)

- `vyb mod install --require-signed github:owner/repo/bindings/sqlite/mod.vyb`
  (with a committed publisher key + signed `INDEX.json` for `bindings/sqlite`) —
  when the transport fetch/verify is wired: a tampered module OR a forged/bad
  signature errors with the exact failing stage named; otherwise (staged) it
  refuses to install rather than silently skip verification (§4.7).
- `vyb mod verify-signed <INDEX.json>` validates an INDEX against the pinned
  publisher key (live today).
- The publisher public key ships in the SDK + is pinned by `vyb`.
- TOFU (T0) path is unchanged and stays green; full suite passes; docs-gate
  passes.

### 4.7 Implementation status

- **Landed:** pinned publisher key (`bindings/publisher_key.pub`) + Ed25519
  signature verification via `vyb mod verify-signed <INDEX.json>` (the crypto/root
  mechanism), and a signed `bindings/INDEX.json` (+ sibling `.sig`) committed.
- **Staged:** the transport-level fetch+verify (`vyb mod install --require-signed
  github:...` fetching + verifying the posted `INDEX.json`) — until wired,
  `--require-signed` refuses to install rather than silently skip verification.
- The T0 TOFU path is unchanged.

