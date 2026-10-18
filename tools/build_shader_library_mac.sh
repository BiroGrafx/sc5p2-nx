#!/bin/bash
# macOS version of tools/build_shader_library.sh: the same steps (XenosRecomp with MASSEFFECT_RECOMP and the
# masseffect_hlsl front end, DXC to SPIR-V, me_pack_shaders), with the tools macOS has: Xcode's clang++, a DXC build
# for macOS (DXC=...), perl's alarm for the per-shader time limits (no `timeout`) and xargs -P for the parallel steps
# (macOS's bash 3.2 has no `wait -n`).
#
#   DXC=<dxc> tools/build_shader_library_mac.sh <dump folder> [<dump folder> ...]
#
# Output: out/shaders/sa2_shaders.mesp (+ .idx), the name the renderer loads next to the executable.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
export SHADERS="$ROOT/shaders" XENOS="$ROOT/shaders/XenosRecomp" SDK="$ROOT/sdk" OUT="$ROOT/out/shaders"
CXX=${CXX:-clang++}
export DXC=${DXC:?set DXC to a dxc binary}
JOBS=${JOBS:-8}
[ $# -ge 1 ] || { echo "usage: DXC=<dxc> $0 <dump folder> [...]"; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/in" "$OUT/hlsl" "$OUT/spirv" "$OUT/bin" "$OUT/tmp"

# limit <seconds> <command...>: the command, stopped after that many seconds.
limit() { perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "$@"; }
export -f limit

echo "== building tools"
"$CXX" -std=c++20 -O2 -w -fms-extensions -include "$XENOS/pch_min.h" -I"$SHADERS" -I"$XENOS" \
  -I"$SDK/thirdparty/fmt/include" -I"$SDK/thirdparty/xxHash" -DFMT_HEADER_ONLY -DXXH_INLINE_ALL -DMASSEFFECT_RECOMP \
  "$SHADERS/tools/masseffect_hlsl.cpp" "$XENOS/shader_recompiler.cpp" -o "$OUT/bin/xenos_hlsl" \
  > "$OUT/build_tools.log" 2>&1 || { echo "translator build failed, see $OUT/build_tools.log"; exit 1; }
"$CXX" -std=c++20 -O2 -w -I"$SDK/thirdparty/xxHash" -I"$ROOT/app/src/me/native" \
  "$SHADERS/tools/me_pack_shaders.cpp" "$ROOT/app/src/me/native/sa2_shader_library.cpp" \
  -o "$OUT/bin/me_pack_shaders" >> "$OUT/build_tools.log" 2>&1 || { echo "packer build failed, see $OUT/build_tools.log"; exit 1; }

echo "== gathering containers (vs_/ps_ names)"
for d in "$@"; do
  for f in "$d"/*.bin; do
    [ -e "$f" ] || continue
    b=$(basename "$f" .bin)
    case "$b" in
      vs_*|ps_*) cp "$f" "$OUT/in/$b.bin" ;;
      v_*) cp "$f" "$OUT/in/vs_i${b#v_}.bin" ;;
      p_*) cp "$f" "$OUT/in/ps_i${b#p_}.bin" ;;
    esac
  done
done
echo "   $(ls "$OUT/in" | wc -l | tr -d ' ') containers"

echo "== XenosRecomp (one process per container, so one crash does not stop the rest)"
translate() {
  local f=$1 b; b=$(basename "$f" .bin)
  local t="$OUT/tmp/$b"; mkdir -p "$t/i"; cp "$f" "$t/i/"
  if limit 120 "$OUT/bin/xenos_hlsl" "$t/i" "$t/o" "$XENOS/shader_common.h" > "$t/log.txt" 2>&1 && [ -f "$t/o/$b.hlsl" ]; then
    mv "$t/o/$b.hlsl" "$OUT/hlsl/"; rm -rf "$t"
  else
    echo "$b $(tail -1 "$t/log.txt")" >> "$OUT/translate_failed.txt"
  fi
}
export -f translate
: > "$OUT/translate_failed.txt"
ls "$OUT"/in/*.bin | xargs -P "$JOBS" -I{} bash -c 'translate "$1"' _ {}
echo "   $(ls "$OUT/hlsl" | wc -l | tr -d ' ') translated, $(wc -l < "$OUT/translate_failed.txt" | tr -d ' ') failed"

echo "== DXC"
compile() {
  local f=$1 b t x; b=$(basename "$f" .hlsl)
  case $b in ps_*) t=ps_6_6; x="";; *) t=vs_6_6; x=-fvk-invert-y;; esac
  limit 300 "$DXC" -spirv -T $t -E main -HV 2021 -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -Werror=parameter-usage $x \
    -Fo "$OUT/spirv/$b.spv" "$f" > /dev/null 2> "$OUT/spirv/$b.err" && rm -f "$OUT/spirv/$b.err" || rm -f "$OUT/spirv/$b.spv"
}
export -f compile
ls "$OUT"/hlsl/*.hlsl | xargs -P "$JOBS" -I{} bash -c 'compile "$1"' _ {}
echo "   $(ls "$OUT"/spirv/*.spv 2>/dev/null | wc -l | tr -d ' ') compiled, $(ls "$OUT"/spirv/*.err 2>/dev/null | wc -l | tr -d ' ') failed"

echo "== pack (only containers that compiled)"
mkdir -p "$OUT/in_ok"
for s in "$OUT"/spirv/*.spv; do b=$(basename "$s" .spv); cp "$OUT/in/$b.bin" "$OUT/in_ok/"; done
"$OUT/bin/me_pack_shaders" "$OUT/in_ok" "$OUT/spirv" "$OUT/sa2_shaders.mesp" 2>&1 | tee "$OUT/pack.log"
ls -la "$OUT"/sa2_shaders.mesp* 2>/dev/null
