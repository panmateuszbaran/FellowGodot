#!/bin/bash
# Texture streaming (milestone 45) on lavapipe: Streaming Lab's fixed cameras without streaming, with it and no budget
# (the same images expected), and with a budget. streaming_check.sh <outdir> [budget MiB] [bench args]
# For the comparison pass --streaming_fixed_wait=<frames> (the same frame count in every run).
# Needs the generated textures: python3 benchmarks/scripts/make_streaming_textures.py --out
# "$FELLOW_PROJECT/external/streaming" (or a symlink to them).
. "$(dirname "$0")/common.sh"
OUT=$1; BUDGET=${2:-64}; shift 2
rm -rf "$OUT"
fellow_run "$OUT/a" "$DEMOTE" "" --scene=streaming --mode=capture "$@"
fellow_run "$OUT/b" "$DEMOTE
fellow/streaming/textures=true" "" --scene=streaming --mode=capture "$@"
python3 "$(dirname "$0")/cmp.py" "$OUT"
fellow_run "$OUT/c" "$DEMOTE
fellow/streaming/textures=true
fellow/streaming/texture_budget_mb=$BUDGET" "" --scene=streaming --mode=capture "$@"
python3 - "$OUT" <<'EOF'
import glob, json, os, sys
out = sys.argv[1]
for run in ("a", "b", "c"):
    for p in sorted(glob.glob(os.path.join(out, run, "*.json"))):
        r = json.load(open(p))
        ts = r.get("texture_streaming", {})
        print(run, os.path.basename(p), "textures loaded in %.1f ms" % r["scene_desc"]["info"].get("texture_load_ms", 0),
              "first frame %.0f ms" % r.get("startup_to_first_frame_ms", 0), "wait frames", r.get("texture_streaming_wait_frames", "-"),
              "unsettled", r.get("texture_streaming_unsettled", 0))
        for cam, m in r.get("camera_mem", {}).items():
            d = ts.get(cam, {})
            print("   %-14s texture mem %7.1f MiB  streamed resident %7.1f / wanted %7.1f / full %7.1f MiB  bias %.1f  loads %d" % (
                cam, m.get("texture_mem_used", 0) / 2**20, d.get("resident_bytes", 0) / 2**20, d.get("wanted_bytes", 0) / 2**20,
                d.get("full_bytes", 0) / 2**20, d.get("budget_bias", 0), d.get("loads", 0)))
EOF
