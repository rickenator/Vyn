#!/usr/bin/env bash
#
# package-sdk.sh — package a transportable Vyb SDK tarball.
#
# Produces:  <out>/vyb-sdk-<version>-<os>-<arch>.tar.gz
# which extracts to a directory  vyb-sdk-<version>  containing:
#   bin/vyb            Release compiler
#   bin/vyb-bindgen    wrapper that runs `vyb bindgen` (or placeholder note)
#   lib/               runtime.o + any static archives (if the build produces them)
#   stdlib/            full stdlib copy
#   include/vendor/    empty (future bindings)
#   docs/              docs/refman + repo doc markdown set
#   env.sh             sourced environment helper
#   manifest.json      package metadata + per-file sha256
#
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ---- defaults -------------------------------------------------------------
DEFAULT_VERSION="$(grep -oE 'VERSION [0-9.]+' "$REPO_DIR/CMakeLists.txt" | head -1 | awk '{print $2}' || true)"
VERSION="${DEFAULT_VERSION:-0.0.0}"
OUT_DIR="$REPO_DIR/dist"
DO_BUILD=1

# ---- arg parsing ----------------------------------------------------------
usage() {
    cat <<'EOF'
Usage: package-sdk.sh [--version V] [--out DIR] [--no-build]

  --version V   SDK version (default: grep of CMakeLists.txt)
  --out DIR     output directory (default: ./dist)
  --no-build    skip the release cmake configure+build; use existing artifacts
  -h, --help    show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)
            [[ $# -ge 2 ]] || { echo "error: --version requires a value" >&2; exit 1; }
            VERSION="$2"; shift 2 ;;
        --out)
            [[ $# -ge 2 ]] || { echo "error: --out requires a value" >&2; exit 1; }
            OUT_DIR="$2"; shift 2 ;;
        --no-build)
            DO_BUILD=0; shift ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "error: unknown argument '$1'" >&2; usage >&2; exit 1 ;;
    esac
done

# ---- derive host os/arch --------------------------------------------------
OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
case "$ARCH" in
    x86_64)      ARCH="x86_64" ;;
    aarch64|arm64) ARCH="aarch64" ;;
    amd64)       ARCH="x86_64" ;;
esac

ARTIFACT="vyb-sdk-$VERSION"
PKG_DIR_NAME="$ARTIFACT"
TARBALL="$OUT_DIR/${ARTIFACT}-${OS}-${ARCH}.tar.gz"

# ---- build ----------------------------------------------------------------
if [[ "$DO_BUILD" -eq 1 ]]; then
    echo "==> Configuring release preset"
    (cd "$REPO_DIR" && cmake --preset release)
    echo "==> Building release preset"
    (cd "$REPO_DIR" && cmake --build --preset release)
else
    echo "==> --no-build: using existing release artifacts (if any)"
fi

# ---- locate artifacts -----------------------------------------------------
RELEASE_DIR="$REPO_DIR/build-release"
VYB_BIN="${VYB_BIN:-$RELEASE_DIR/vyb}"

if [[ ! -f "$VYB_BIN" ]]; then
    echo "error: compiler binary not found at '$VYB_BIN' (build-release/vyb). Run without --no-build, or set VYB_BIN." >&2
    exit 1
fi
if [[ ! -d "$REPO_DIR/stdlib" ]]; then
    echo "error: stdlib directory not found at '$REPO_DIR/stdlib'." >&2
    exit 1
fi

# ---- stage ----------------------------------------------------------------
STAGE="$OUT_DIR/.stage/$PKG_DIR_NAME"
rm -rf "$OUT_DIR/.stage"
mkdir -p "$STAGE/bin" "$STAGE/lib" "$STAGE/stdlib" "$STAGE/include/vendor" "$STAGE/docs/refman"

echo "==> Staging to $STAGE"

# bin/vyb — Release compiler
cp "$VYB_BIN" "$STAGE/bin/vyb"

# bin/vyb-bindgen — real subcommand of vyb
cat > "$STAGE/bin/vyb-bindgen" <<EOF
#!/usr/bin/env bash
# Vyb bindgen entry point. Bindgen is a subcommand of the vyb compiler; this
# thin wrapper forwards all arguments.
set -euo pipefail
DIR="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
exec "\$DIR/vyb" bindgen "\$@"
EOF
chmod +x "$STAGE/bin/vyb-bindgen"

# lib/ — runtime.o + static archives, when the build provides them
shopt -s nullglob
LIBMISSING=1
for lib in "$RELEASE_DIR"/runtime.o \
           "$RELEASE_DIR"/CMakeFiles/vyb.dir/runtime/*.o \
           "$RELEASE_DIR"/*.a; do
    [[ -e "$lib" ]] || continue
    cp "$lib" "$STAGE/lib/"
    LIBMISSING=0
done
shopt -u nullglob
if [[ "$LIBMISSING" -eq 1 ]]; then
    echo "    note: no runtime.o or static archive (.a) present in the release build; lib/ left empty (bindings/runtime objects are statically linked into bin/vyb, or produced by a future phase-1 runtime build)."
fi

# stdlib/ — full copy
cp -r "$REPO_DIR/stdlib/." "$STAGE/stdlib/"

# include/vendor/ — empty, for future bindings
# (directory created above; intentionally left empty)

# docs/ — refman + repo doc markdown set
cp -r "$REPO_DIR/docs/refman/." "$STAGE/docs/refman/"
DOCSET=(MANIFEST DEVELOPER_TOOLING DOCS_POLICY GENERIC_ASPECT_CONFORMANCE FEATURE_STATUS CHANGELOG)
for name in "${DOCSET[@]}"; do
    if [[ -f "$REPO_DIR/doc/$name.md" ]]; then
        cp "$REPO_DIR/doc/$name.md" "$STAGE/docs/$name.md"
    elif [[ -f "$REPO_DIR/$name.md" ]]; then
        cp "$REPO_DIR/$name.md" "$STAGE/docs/$name.md"
    else
        echo "    warning: docs/$name.md not found; skipping" >&2
    fi
done
# docs/sdk/ — real getting-started + install docs (Phase 0; no SDK ships without docs)
mkdir -p "$STAGE/docs/sdk"
for d in GETTING_STARTED INSTALL; do
    if [[ -f "$REPO_DIR/docs/sdk/$d.md" ]]; then
        cp "$REPO_DIR/docs/sdk/$d.md" "$STAGE/docs/sdk/$d.md"
    else
        echo "error: docs/sdk/$d.md not found; no SDK ships without docs" >&2
        exit 1
    fi
done

# env.sh — copy the SDK env helper
cp "$REPO_DIR/sdk/env.sh" "$STAGE/env.sh"

# ---- manifest.json --------------------------------------------------------
# package, version, os, arch, vyb_version, refman_revision, files[].sha256
python3 - "$STAGE" "$VERSION" "$OS" "$ARCH" "$REPO_DIR" <<'PY'
import hashlib, json, os, sys

stage, version, os_name, arch, repo = sys.argv[1:6]
files = []
for root, dirs, names in os.walk(stage):
    dirs.sort()
    for n in sorted(names):
        p = os.path.join(root, n)
        rel = os.path.relpath(p, stage)
        with open(p, "rb") as fh:
            digest = hashlib.sha256(fh.read()).hexdigest()
        files.append({"path": rel, "sha256": digest})

def subprocess_stdout(cmd, cwd):
    import subprocess
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else "unknown"

manifest = {
    "package": "vyb-sdk",
    "version": version,
    "os": os_name,
    "arch": arch,
    "vyb_version": version,
    "refman_revision": subprocess_stdout(["git", "rev-parse", "HEAD"], repo),
    "files": files,
}

out = os.path.join(stage, "manifest.json")
with open(out, "w") as fh:
    json.dump(manifest, fh, indent=2)
    fh.write("\n")
print(f"    manifest.json: {len(files)} files")
PY

# ---- tar ------------------------------------------------------------------
echo "==> Tarballing $TARBALL"
mkdir -p "$OUT_DIR"
tar -C "$OUT_DIR/.stage" -czf "$TARBALL" "$PKG_DIR_NAME"

# ---- summary --------------------------------------------------------------
echo
echo "Artifact: $TARBALL"
echo "Package : vyb-sdk ($VERSION) for $OS/$ARCH"
echo "Manifest summary:"
python3 - "$STAGE/manifest.json" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
print(f"  package          : {m['package']}")
print(f"  version          : {m['version']}")
print(f"  os/arch          : {m['os']}/{m['arch']}")
print(f"  vyb_version      : {m['vyb_version']}")
print(f"  refman_revision  : {m['refman_revision']}")
print(f"  files            : {len(m['files'])}")
PY

echo "==> Cleaning staging dir"
rm -rf "$OUT_DIR/.stage"
