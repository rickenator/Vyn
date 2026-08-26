# Getting Started with Vyb

Vyb is a systems programming language and LLVM-backend compiler. The SDK is a
**transportable development environment**: everything you need to read, write,
and run Vyb is in one tarball — the `vyb` compiler (Release), the standard
library sources, the docs, and an environment helper. Carry it to any supported
machine and `source env.sh` to start.

## What is in the SDK

`vyb-sdk-<version>-<os>-<arch>.tar.gz` extracts to a directory named
`vyb-sdk-<version>`:

```
vyb-sdk-<version>/
  bin/vyb              the compiler (Release build)
  bin/vyb-bindgen      thin wrapper for `vyb bindgen`
  lib/                 runtime objects / static archives (when the build ships them)
  stdlib/              the standard library modules (pure Vyb source)
  include/vendor/      bundled native headers/libs for optional bindings (future)
  docs/                the SDK documentation set (see below)
  env.sh               source this to set up the shell
  manifest.json        package metadata + a per-file sha256 manifest
```

The `manifest.json` records the SDK version, the target OS/arch, the compiler
version, the git revision the documentation was generated from, and a `sha256`
digest for every file — use it to verify an SDK after download or before
shipping a release.

## Quick start

1. Extract the SDK: `tar -xzf vyb-sdk-<version>-<os>-<arch>.tar.gz`
2. Enter it and source the environment helper:

   ```bash
   cd vyb-sdk-<version>
   source env.sh
   ```

   `env.sh` prepends `bin/` to `PATH` and points `VYB_STDLIB` at `stdlib/`, so
   the compiler and the modules it needs are both found from anywhere.

3. Confirm the install:

   ```bash
   vyb --version
   vyb help
   ```

4. Run a program — either directly by file, or as a project:

   ```bash
   # single file (JIT)
   vyb hello.vyb

   # project with a vyb.toml manifest
   vyb new my_project && cd my_project && vyb build
   ```

## Where the docs are

- `docs/refman/PROGRAMMERS_GUIDE.md` and the generated pages under `docs/refman/`
  are the **authoritative** reference (language tour, the standard-library
  module pages, and the full-form cross-indexes).
- `docs/MANIFEST.md` — the `vyb.toml` project/dependency format.
- `docs/GETTING_STARTED.md` and `docs/INSTALL.md` — what you are reading and how
  to install the SDK (they ship under `docs/sdk/` in the source tree).
- `docs/DOCS_POLICY.md` — how the documentation is kept current as the language
  changes.

See `INSTALL.md` for per-platform installation details.
