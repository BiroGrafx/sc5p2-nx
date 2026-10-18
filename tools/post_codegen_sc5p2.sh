#!/bin/bash
# The optimised flavour's post-codegen steps for Space Channel 5 Part 2 (app_sc5p2), in tools/post_codegen.py's order
# (sa2-nx's driver is written for app/ and SA2's two modules; this game has one module and no localized copies).
# Run after every tools/codegen_sc5p2.sh. Every step is idempotent.
#   - pch_no_volatile + loop_barriers (required together: without the barriers GCC turns guest polling loops into
#     infinite loops), pch_cold_traps, direct_calls (calls between unhooked functions become direct, so GCC can inline
#     them), guest_cas, flush_mode_flow (with the image: the entry modes), vec_byteswap, pch_neon_shuffles,
#     ro_constants, float_loads, frame_traffic (marks only; its elisions stay off), loop_locals, leaf_locals (last: it
#     renames registers);
#   - indirect_calls tags every bctrl (no promotion list for this game yet).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
GEN=app_sc5p2/generated/default
PCH=$GEN/sc5p2_pch.h
IMAGE=out/sc5p2_image.bin
export REX_APP_DIR=app_sc5p2 REX_PROJECT=sc5p2 REXSDK_DIR="$ROOT/sdk"
step() { echo "== $*"; python3 "$@" | tail -2; }
step tools/pch_no_volatile.py "$PCH"
step tools/indirect_calls.py --gen "$GEN"
step tools/loop_barriers.py "$GEN"
step tools/pch_cold_traps.py "$PCH"
step tools/direct_calls.py --redo --gen "$GEN"
step tools/guest_cas.py --gen "$GEN"
step tools/flush_mode_flow.py --redo --gen "$GEN" --image "$IMAGE" --sources app_sc5p2/src sdk/src sdk/include \
  --tomls app_sc5p2
step tools/vec_byteswap.py --gen "$GEN"
step tools/pch_neon_shuffles.py "$PCH"
step tools/ro_constants.py --gen "$GEN" --image "$IMAGE"
step tools/float_loads.py --gen "$GEN"
step tools/frame_traffic.py --gen "$GEN"
step tools/loop_locals.py --gen "$GEN"
step tools/leaf_locals.py --gen "$GEN"
echo "optimised codegen (step C: + non-argument locals)" > app_sc5p2/generated/codegen_flavor.txt
echo "post_codegen_sc5p2: done"
