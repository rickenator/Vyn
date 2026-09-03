#!/bin/bash
# Conformance tests for the vyb.toml manifest + dependency contract (#164/#165).
#
# Covers: scaffold + plain build; local PATH dependency accepted; git/version
# dependency REJECTED with the #165 diagnostic; array value REJECTED with the
# #164 line-numbered diagnostic. Exit 0 when all pass, 1 otherwise.
set -u
cd "$(dirname "$0")" || exit 2
ROOT=$(pwd)
VYB="$ROOT/build/vyb"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

pass=0; fail=0
ok()  { echo "  ok: $1";  pass=$((pass+1)); }
bad() { echo "FAIL: $1"; fail=$((fail+1)); }

# --- 1. scaffold + plain build ---
( cd "$WORK" && "$VYB" new plainapp >/dev/null 2>&1 )
if [ -f "$WORK/plainapp/vyb.toml" ] && [ -f "$WORK/plainapp/src/main.vyb" ]; then
    ok "vyb new scaffolds project + src/main.vyb"
else
    bad "vyb new scaffold"
fi
if ( cd "$WORK/plainapp" && "$VYB" build >/dev/null 2>&1 ); then
    ok "plain build (no deps) succeeds"
else
    bad "plain build"
fi

# --- 2. local PATH dependency is accepted ---
mkdir -p "$WORK/libsrc/src"
printf 'export maybe? no -- a trivial top-level value\nlib_version<Int> -> { return 7 }\n' > "$WORK/libsrc/src/lib.vyb"
( cd "$WORK" && "$VYB" new pathapp >/dev/null 2>&1 )
printf '\n[dependencies]\nlib = { path = "./libsrc" }\n' >> "$WORK/pathapp/vyb.toml"
if ( cd "$WORK/pathapp" && "$VYB" build >/dev/null 2>&1 ); then
    ok "local path dependency accepted by vyb build"
else
    bad "local path dependency"
fi

# --- 3. git dependency REJECTED with #165 diagnostic ---
( cd "$WORK" && "$VYB" new gitapp >/dev/null 2>&1 )
printf '\n[dependencies]\ng = { git = "https://example.com/repo.git" }\n' >> "$WORK/gitapp/vyb.toml"
gitout="$(cd "$WORK/gitapp" && "$VYB" build 2>&1)"
if [ $? -ne 0 ] && grep -q "not supported yet (#165)" <<<"$gitout"; then
    ok "git dependency rejected with #165 diagnostic"
else
    bad "git dependency rejection (output: $gitout)"
fi

# --- 4. unsupported array value REJECTED with #164 line diagnostic ---
[ -d "$WORK/arrproj" ] || mkdir -p "$WORK/arrproj/src"
printf '[package]\nname = "arrproj"\nversion = "0.1.0"\nfeatures = ["a", "b"]\n[[bin]]\nname = "arrproj"\npath = "src/main.vyb"\n' > "$WORK/arrproj/vyb.toml"
printf 'main()<Int> -> { return 0 }\n' > "$WORK/arrproj/src/main.vyb"
arrout="$(cd "$WORK/arrproj" && "$VYB" build 2>&1)"
if [ $? -ne 0 ] && grep -qE "unsupported TOML array value for key 'features' at line 4:.*#164" <<<"$arrout"; then
    ok "array value rejected with #164 line-numbered diagnostic"
else
    bad "array value rejection (output: $arrout)"
fi

# --- 5. CLI help + version smoke ---
if "$VYB" help >/dev/null 2>&1 && "$VYB" build --help >/dev/null 2>&1 && "$VYB" --version >/dev/null 2>&1; then
    ok "vyb help / build --help / --version"
else
    bad "CLI help/version smoke"
fi

# --- 6. #204: package-level freedom boundary (privileged dep requires smuggle) ---
mkdir -p "$WORK/gpulib"
printf '[package]\nname = "gpu"\nversion = "0.1.0"\n\n[mod]\nboundary = ["freedom"]\ncapabilities = ["ffi", "cuda-driver"]\n' > "$WORK/gpulib/vyb.toml"
printf 'share(all)\ngpu_value()<Int> -> { return 42 }\n' > "$WORK/gpulib/gpu.vyb"
( cd "$WORK" && "$VYB" new privapp >/dev/null 2>&1 )
printf '\n[dependencies]\ngpu = { path = "../gpulib" }\n' >> "$WORK/privapp/vyb.toml"
# consumer that IMPORTS the privileged package -> must fail with the smuggle remedy
printf 'import gpu::{gpu_value}\nmain()<Int> -> { println(gpu_value()); return 0 }\n' > "$WORK/privapp/src/main.vyb"
impout="$(cd "$WORK/privapp" && "$VYB" build 2>&1)"
if [ $? -ne 0 ] && grep -q "is privileged (freedom): consume it through smuggle" <<<"$impout"; then
    ok "#204 import of privileged package rejected with smuggle remedy"
else
    bad "#204 import rejection (output: $impout)"
fi
# consumer that SMUGGLES the privileged package -> builds and the exe prints 42
printf 'smuggle gpu::{gpu_value}\nmain()<Int> -> { println(gpu_value()); return 0 }\n' > "$WORK/privapp/src/main.vyb"
exe="$WORK/privapp/target/privapp"
if smout="$(cd "$WORK/privapp" && "$VYB" build 2>&1)" && [ -x "$exe" ]; then
    got="$( "$exe" )"
    if grep -q "42" <<<"$got"; then
        ok "#204 smuggle of privileged package builds and runs (got '$got')"
    else
        bad "#204 smuggle exe output (got '$got')"
    fi
else
    bad "#204 smuggle build (output: $smout)"
fi

echo
if [ "$fail" -eq 0 ]; then
    echo "Manifest conformance passed: $pass / $((pass+fail))"
    exit 0
else
    echo "Manifest conformance FAILED: $fail failure(s), $pass passed"
    exit 1
fi
