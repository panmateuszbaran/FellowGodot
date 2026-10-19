#!/bin/bash
# One process, one camera, consecutive frames compared with each other (a static view must not change once settled):
# repeat.sh <scene> <camera> <outdir> "<override>" [bench args, e.g. --settle=0 --repeat=16 --set=vp.use_taa=false]
# Prints the FellowEngine counters per frame (--keys=a+b, default pipeline_ubershader_draws): an image that changes
# when a draw leaves the ubershader shows a pipeline whose specialized image differs from the ubershader's
# (FELLOW_DEBUG_FORCE_UBERSHADER=1 in EXTRA_ENV draws everything with the ubershader).
. "$(dirname "$0")/common.sh"
SC=$1; CAM=$2; OUT=$3; OV=$4; shift 4
rm -rf "$OUT"
fellow_run "$OUT" "$OV" "LVP_DUMMY=0 $EXTRA_ENV" --scene=$SC --mode=repeat --cameras=$CAM "$@"
grep '\[repeat\]' "$OUT.log"
python3 - "$OUT" <<'PY'
import glob, sys
import numpy as np
from PIL import Image
fs = sorted(glob.glob(sys.argv[1] + "/**/*_r[0-9][0-9].png", recursive=True))
imgs = [np.asarray(Image.open(f)).astype(int)[..., :3] for f in fs]
print("frames", len(imgs), "pixels differing from the previous frame:", [int((np.abs(imgs[i] - imgs[i - 1]).max(-1) > 0).sum()) for i in range(1, len(imgs))])
PY
