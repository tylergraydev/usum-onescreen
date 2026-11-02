#!/usr/bin/env bash
# Decompile functions in a code.bin with the UltraSunnyMoon Ghidra install (WSL).
# Usage: decompile.sh <code.bin> <out.txt> <addr_hex> [addr_hex ...]
set -euo pipefail

TOOLS="${ULTRASUN_TOOL_ROOT:-$HOME/.local/share/ultrasunnymoon}"
GHIDRA="$TOOLS/ghidra_12.1.4_PUBLIC"
JAVA_HOME="$TOOLS/jdk"
[ -x "$JAVA_HOME/bin/java" ] || JAVA_HOME="$(dirname "$(dirname "$(find "$TOOLS/jdk" -path '*/bin/java' | head -1)")")"
export JAVA_HOME PATH="$JAVA_HOME/bin:$PATH"

CODE="$1"; OUT="$2"; shift 2
SCRIPTS="$(cd "$(dirname "$0")" && pwd)"
PROJ="$(mktemp -d /tmp/sunnymoon-ghidra.XXXXXX)"
trap 'rm -rf "$PROJ"' EXIT

"$GHIDRA/support/analyzeHeadless" "$PROJ" usun -import "$CODE" \
    -loader BinaryLoader -loader-baseAddr 0x100000 -processor ARM:LE:32:v6 -noanalysis \
    -scriptPath "$SCRIPTS" -postScript DecompileAt.java "$OUT" "$@" \
    -deleteProject 2>&1 | grep -E "ERROR|Exception" || true

ls -la "$OUT"
