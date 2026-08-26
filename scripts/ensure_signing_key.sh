#!/bin/bash
# SDK signing-key bootstrap (Phase 4, doc/MANIFEST.md §4).
#
# Ensures an out-of-tree Ed25519 signing key for this SDK build, generating it
# ONCE and persisting it across builds (so building the SDK repeatedly does NOT
# rotate the key). Rotation is an explicit --rotate, never implicit.
#
# Storage: $VYB_SIGNING_DIR (default ~/.vyb/signing/), directory 0700, private
# key 0600, OUTSIDE the source tree so it survives rebuilds and is never
# committed. The public key is written alongside (sdk.pub) for pinning.
#
# Default trust: the SDK still verifies under the OFFICIAL pinned publisher key
# unless the builder opts into their own via VYB_SIGNING_KEY / `--key`.
set -euo pipefail

VYB="${1:?usage: ensure_signing_key.sh <path/to/vyb> [--rotate]}"
MODE="${2:-}"
H="${VYB_SIGNING_DIR:-$HOME/.vyb/signing}"
KEY="$H/sdk.key"
PUB="$H/sdk.pub"

mkdir -p "$H"
chmod 700 "$H" 2>/dev/null || true

if [ "$MODE" = "--rotate" ]; then
  if [ -f "$KEY" ]; then
    mv "$KEY" "$KEY.bak.$(date +%s)"; rm -f "$PUB"
    echo "Rotated: previous key archived as sdk.key.bak.<ts> in $H."
  fi
  out="$("$VYB" mod gen-key "$KEY")"
  printf '%s\n' "$out" | grep -i 'public key' > "$PUB"
  echo "$out"
  exit 0
fi

if [ -f "$KEY" ]; then
  echo "Reusing existing SDK signing key at $KEY (not regenerated; rotation is explicit --rotate)."
  cat "$PUB" 2>/dev/null || true
  exit 0
fi

out="$("$VYB" mod gen-key "$KEY")"
printf '%s\n' "$out" | grep -i 'public key' > "$PUB"
printf '%s\n' "$out"
echo
echo "Secure this key (it signs your official packages) and pin the public key above"
echo "via VYB_SIGNING_KEY / vyb mod ... --key. See doc/MANIFEST.md §4."
