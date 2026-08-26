# Installing the Vyb SDK

The SDK ships as a per-platform tarball: `vyb-sdk-<version>-<os>-<arch>.tar.gz`.
The supported matrix is:

| Target | Artifact suffix |
| --- | --- |
| Linux / x86_64 | `linux-x86_64` |
| macOS / arm64 | `macos-aarch64` |

## Steps

1. **Download** the tarball matching your OS/arch from the release, plus its
   `.manifest.json` and `.sha256` (attached to the same release).

2. **Verify integrity**:

   ```bash
   # compare against the recorded digest (per-file sha256 in the manifest)
   sha256sum vyb-sdk-<version>-<os>-<arch>.tar.gz
   # and, once extracted, re-check the manifest's per-file digests:
   python3 -c "import json; m=json.load(open('manifest.json')); import hashlib,os; \
   [print(('OK ' if hashlib.sha256(open(f,'rb').read()).hexdigest()==d else 'BAD ')+f) \
   for f in map(lambda e:e['path'],m['files'])]" 2>/dev/null | grep BAD || echo "all files match manifest"
   ```

3. **Extract** to a location of your choice:

   ```bash
   tar -xzf vyb-sdk-<version>-<os>-<arch>.tar.gz
   cd vyb-sdk-<version>
   ```

4. **Set up the environment** by sourcing `env.sh` (adds `bin/` to `PATH` and
   sets `VYB_STDLIB`):

   ```bash
   source env.sh
   ```

5. **Verify** the toolchain responds:

   ```bash
   vyb --version       # prints version + build configuration
   vyb help            # subcommand overview
   vyb hello.vyb       # run a program (JIT)
   ```

## Environment

`sdk/env.sh` is meant to be `source`d, not executed. It:
- prepends `bin/` to `PATH` (idempotent — running it twice is safe);
- exports `VYB_STDLIB` to the bundled `stdlib/`;
- only touches `LD_LIBRARY_PATH` if the SDK actually ships shared libraries.

Prefer `source env.sh` for interactive use. In scripts, equivalently:

```bash
export PATH="<sdk>/bin:$PATH"
export VYB_STDLIB="<sdk>/stdlib"
```

## Contents

The SDK bundles `bin/vyb` (Release compiler) and `bin/vyb-bindgen`, the standard
library sources, the runtime objects, an empty `include/vendor/` for optional
bindings, the full documentation set under `docs/` (refman + manifest +
getting-started), and `manifest.json`. See `GETTING_STARTED.md` for the quick
tour.
