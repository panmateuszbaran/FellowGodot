#!/bin/bash
# Reads of memory nothing wrote: the same captures with lavapipe's new allocations zeroed and filled with a constant
# (LVP_POISON_MEMORY=1); a correct frame does not change. poison_scan.sh <scene> <outdir> "<override>" [bench args]
# To find the texture: FELLOW_DEBUG_CLEAR_TEXTURES=a:b in the environment of both runs (EXTRA_ENV) clears the textures
# created without data whose creation index is in [a, b) (FELLOW_DEBUG_TEXTURE_LOG=1 lists them); bisect the range.
. "$(dirname "$0")/common.sh"
SC=$1; OUT=$2; OV=$3; shift 3
rm -rf "$OUT"
fellow_run "$OUT/a" "$OV" "LVP_DUMMY=0 $EXTRA_ENV" --scene=$SC --mode=capture "$@"
fellow_run "$OUT/b" "$OV" "LVP_POISON_MEMORY=1 $EXTRA_ENV" --scene=$SC --mode=capture "$@"
python3 "$(dirname "$0")/cmp.py" "$OUT"
