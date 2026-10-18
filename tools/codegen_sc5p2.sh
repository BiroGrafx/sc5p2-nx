#!/bin/bash
# Translates Space Channel 5 Part 2 (app_sc5p2) with the host code generator; macOS/Linux counterpart of codegen.ps1.
# The generator is sdk/out/<host>/rexglue (built from sdk/, see docs/building-sc5p2.md); REXGLUE=<path> overrides it.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) HOST=mac-arm64 ;; Darwin-*) HOST=mac-amd64 ;; Linux-aarch64) HOST=linux-arm64 ;; *) HOST=linux-amd64 ;;
esac
REXGLUE="${REXGLUE:-$ROOT/sdk/out/$HOST/rexglue}"
mkdir -p "$ROOT/out"
cd "$ROOT/app_sc5p2"
REX_MAX_JUMP_TABLE_ENTRIES=1024 REXGLUE_DUMP_IMAGE="$ROOT/out/sc5p2_image.bin" \
  "$REXGLUE" codegen sc5p2_manifest.toml > "$ROOT/out/sc5p2_codegen.log" 2>&1
tail -2 "$ROOT/out/sc5p2_codegen.log"
