#!/usr/bin/env bash
#
# env.sh — source this file to set up the Vyb SDK environment.
#
#   source env.sh
#
# Derives its own directory from BASH_SOURCE, so it works whether copied into a
# packaged SDK (../bin, ../stdlib) or run from the repo's sdk/ directory
# (which points at the same ../bin and ../stdlib).
#
# Sets:
#   PATH             prepends <sdk>/bin
#   VYB_STDLIB       points at <sdk>/stdlib
#   LD_LIBRARY_PATH  guarded (appended) — only touched if the bin dir has .so libs
#
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    echo "env.sh is meant to be sourced, not executed:  source env.sh" >&2
    return 1 2>/dev/null || exit 1
fi

# Directory this file lives in (resolves symlinks).
_ENV_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd -P)"
# The packaged layout places env.sh at the SDK root, with bin/ and stdlib/ as siblings.
_SDK_ROOT="$(cd "$_ENV_DIR/.." >/dev/null 2>&1 && pwd -P)"

# Prepend bin to PATH (avoid duplicates).
case ":$PATH:" in
    *":$_SDK_ROOT/bin:"*) : ;;
    *) PATH="$_SDK_ROOT/bin:$PATH" ;;
esac
export PATH

# Export stdlib location.
export VYB_STDLIB="$_SDK_ROOT/stdlib"

# Guard LD_LIBRARY_PATH — only append if the SDK ships shared libs.
if compgen -G "$_SDK_ROOT/bin/*.so" >/dev/null; then
    case ":${LD_LIBRARY_PATH:-}:" in
        *":$_SDK_ROOT/bin:"*) : ;;
        *) export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:+$LD_LIBRARY_PATH:}$_SDK_ROOT/bin" ;;
    esac
fi

unset _ENV_DIR _SDK_ROOT
