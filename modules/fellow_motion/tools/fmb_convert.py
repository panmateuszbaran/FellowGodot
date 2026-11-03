#!/usr/bin/env python3
# FellowEngine — convert the released MotionBricks SOMA checkpoints into one .fmb file for modules/fellow_motion.
#
#   python fmb_convert.py --checkpoints <MotionBricks/soma download> --rig <soma_model_rig.npz> \
#       --usd <soma_base_skel_minimal_no_root.usd> [--body soma_mannequin.glb] [--precision f16|f32] --output soma.fmb
#
# The checkpoints come from huggingface.co/nvidia/MotionBricks (soma/), the rig file from the MotionBricks
# repository (motionbricks/assets/soma_model_rig.npz). Both are NVIDIA's, under the NVIDIA Open Model Agreement:
# the .fmb this writes is a derivative of the model. Keep it out of this repository; distribute it only with a copy
# of that agreement and the notice "Licensed by NVIDIA Corporation under the NVIDIA Open Model Agreement."
#
# --usd (MotionBricks' motionbricks/assets/human/soma_base_skel_minimal_no_root.usd, CC-BY-4.0) gives the skeleton the
# engine drives; --body writes the skinned SOMA mannequin from it as a .glb (FellowMotionCharacter3D.body_path).
#
# Needs numpy and safetensors (no PyTorch); usd-core for --usd.

import argparse
import glob
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fmb_plan  # noqa: E402

NOTICE = "Licensed by NVIDIA Corporation under the NVIDIA Open Model Agreement."


def _find(root, pattern):
    hits = sorted(glob.glob(os.path.join(root, "**", pattern), recursive=True))
    if not hits:
        raise SystemExit(f"Not found under {root}: {pattern}")
    return hits[0]


def load_checkpoints(root):
    from safetensors.numpy import load_file

    st = {}
    for kind in ("pose", "root"):
        st[kind] = load_file(_find(root, f"mmm-v6-soma-{kind}-*/version_0/checkpoints/*.safetensors"))
    st["vqvae"] = load_file(_find(root, "vqvae-v6-soma-*/version_0/checkpoints/*.safetensors"))
    stats = os.path.dirname(_find(root, "mmm-v6-soma-pose-*/version_0/stats/motion/mean.npy"))
    mean = np.load(os.path.join(stats, "mean.npy")).astype(np.float32)
    std = np.load(os.path.join(stats, "std.npy")).astype(np.float32)
    for k in st:
        st[k] = {n: v.astype(np.float32) for n, v in st[k].items()}
    return st, mean, std


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--checkpoints", required=True)
    ap.add_argument("--rig", required=True)
    ap.add_argument("--usd", default=None, help="the SOMA skeleton and mesh (soma_base_skel_minimal_no_root.usd)")
    ap.add_argument("--body", default=None, help="write the skinned mannequin here (.glb); needs --usd")
    ap.add_argument("--precision", default="f16", choices=("f16", "f32"))
    ap.add_argument("--output", required=True)
    a = ap.parse_args()
    st, mean, std = load_checkpoints(a.checkpoints)
    rig = dict(np.load(a.rig))
    B, meta = fmb_plan.build(st["pose"], st["root"], st["vqvae"], mean, std, rig, a.precision)
    meta["notice"] = NOTICE
    if a.usd:
        import fmb_usd

        skeleton, mesh = fmb_usd.load(a.usd)
        meta["skeleton"] = skeleton
        if a.body:
            fmb_usd.write_glb(a.body, skeleton, mesh)
            print(f"{a.body}: the SOMA mannequin ({len(mesh['tris'])} triangles, {len(skeleton['names'])} joints)")
    data = fmb_plan.pack(B, meta)
    with open(a.output, "wb") as f:
        f.write(data)
    nm = sum(w.size for w in B.mats.values())
    print(f"{a.output}: {len(data) / 1e6:.1f} MB, {len(B.ops)} ops, {nm / 1e6:.1f} M matrix weights ({a.precision})")


if __name__ == "__main__":
    main()
