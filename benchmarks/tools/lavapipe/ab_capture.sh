#!/bin/bash
# Captures of one scene's cameras with two sets of settings: ab_capture.sh <scene> <outdir> "<override a>" "<override b>" [bench args]
# Example (an exact option against its absence): ab_capture.sh pbr /tmp/ab "$DEMOTE" "$DEMOTE
# fellow/postfx/ss_interleave_exact_bounds=false" --settle=60
. "$(dirname "$0")/common.sh"
SC=$1; OUT=$2; OA=$3; OB=$4; shift 4
rm -rf "$OUT"
fellow_run "$OUT/a" "$OA" "" --scene=$SC --mode=capture "$@"
fellow_run "$OUT/b" "$OB" "" --scene=$SC --mode=capture "$@"
python3 "$(dirname "$0")/cmp.py" "$OUT"
