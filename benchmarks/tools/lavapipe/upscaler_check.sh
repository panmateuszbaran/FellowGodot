#!/bin/bash
# Temporal upscalers (milestone 47) on lavapipe: PBR Lab's temporal sequences with TAA at the native resolution and
# with FSR 2.2 and FSR 3.1 at a render scale, against a ground truth accumulated from 16 jittered renders a frame
# (no TAA), and the fixed cameras' stills against theirs.
#   upscaler_check.sh <outdir> [scale] [bench args]
# SEQS (the sequences), CAMS (the stills), VARIANTS (taa fsr2 fsr3 ...; a variant fsr3_<name> adds the override lines
# in $FSR3_<name>), RES (e.g. 960x540). The ground truth is rendered once into <outdir>/ref and reused.
. "$(dirname "$0")/common.sh"
OUT=$1
SCALE=${2:-0.5}
shift 2
TOOLS="$(cd "$(dirname "$0")/.." && pwd)"
SEQS=${SEQS:-moving_object,camera_pan,thin_dolly,specular_orbit,foliage_strafe}
CAMS=${CAMS:-pbr_overview,pbr_thin,pbr_specular}
VARIANTS=${VARIANTS:-taa fsr2 fsr3}
UPSCALE="--set=vp.scaling_3d_mode=2,vp.scaling_3d_scale=$SCALE"

if [ ! -d "$OUT/ref" ]; then
	fellow_run "$OUT/ref" "" "" --scene=pbr --mode=reference --preset=high_noaa --seqs=$SEQS --cameras=$CAMS "$@"
	python3 "$TOOLS/accumulate.py" "$OUT/ref" --delete-inputs
fi

for v in $VARIANTS; do
	case $v in
		taa) ov=""; set_args="" ;;
		fsr2) ov=""; set_args=$UPSCALE ;;
		fsr3) ov="fellow/upscaling/fsr3=true"; set_args=$UPSCALE ;;
		fsr3_*)
			var="FSR3_${v#fsr3_}"
			ov="fellow/upscaling/fsr3=true
${!var}"
			set_args=$UPSCALE
			;;
		*) echo "unknown variant $v"; continue ;;
	esac
	if [ ! -d "$OUT/$v" ]; then
		fellow_run "$OUT/$v" "$ov" "" --scene=pbr --mode=temporal --seqs=$SEQS $set_args "$@"
		mv "$OUT/$v.log" "$OUT/$v.temporal.log"
		fellow_run "$OUT/$v" "$ov" "" --scene=pbr --mode=capture --freeze_exposure=1 --cameras=$CAMS $set_args "$@"
	fi
	for s in ${SEQS//,/ }; do
		python3 "$TOOLS/temporal.py" --test "$OUT/$v" --ref "$OUT/ref" --seq "$s" --out "$OUT/analysis/$v" --labels "$v" > /dev/null
	done
done

python3 - "$OUT" "$CAMS" $VARIANTS <<'PYEOF'
import glob, json, os, sys
import numpy as np
from PIL import Image
out, cams, variants = sys.argv[1], sys.argv[2].split(","), sys.argv[3:]
rows = {}
for v in variants:
    for p in sorted(glob.glob(os.path.join(out, "analysis", v, "temporal_*.json"))):
        d = json.load(open(p))
        rows.setdefault(d["sequence"], {})[v] = d["results"][v]["aggregate"]
for k, fmt in [("err_motion", "%10.5f"), ("err_static", "%10.5f"), ("flicker", "%10.5f"), ("psnr", "%10.2f"), ("ssim_every4", "%10.4f")]:
    print(k.ljust(16) + "".join(v.rjust(10) for v in variants))
    for seq, per in rows.items():
        print("  " + seq.ljust(14) + "".join((fmt % per[v][k]) if v in per and per[v].get(k) is not None else "-".rjust(10) for v in variants))


def load(p):
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.float64) / 255.0


print("still PSNR dB".ljust(16) + "".join(v.rjust(10) for v in variants))
for cam in cams:
    refs = glob.glob(os.path.join(out, "ref", "*_ref-cam-%s.png" % cam))
    if not refs:
        continue
    r = load(refs[0])
    line = "  " + cam.ljust(14)
    for v in variants:
        t = glob.glob(os.path.join(out, v, "*_%s.png" % cam))
        if not t:
            line += "-".rjust(10)
            continue
        mse = np.mean((load(t[0]) - r) ** 2)
        line += "%10.2f" % (10 * np.log10(1.0 / max(mse, 1e-12)))
    print(line)
PYEOF
