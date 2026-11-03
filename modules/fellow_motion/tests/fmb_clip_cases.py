#!/usr/bin/env python3
# FellowEngine — realistic test requests from SOMA clips: contexts sampled from one clip, targets from another placed
# ahead of the character (as the locomotion runtime does), encoded by the SOMA glue (tools/fmb_soma.py).
#
#   python fmb_clip_cases.py <clip dir> <out cases.npz>
#
# The clips (NVIDIA Sample Data Evaluation License) are not part of this repository.

import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import fmb_bvh  # noqa: E402
import fmb_soma  # noqa: E402
from fmb_bvh import quat_mul  # noqa: E402


def skeleton_from(b):
    keep = [i for i, n in enumerate(b.names) if n != "Root"]
    remap = {o: k for k, o in enumerate(keep)}
    parents = [remap.get(b.parents[o], -1) for o in keep]
    names = [b.names[o] for o in keep]
    ref_rot = np.zeros((len(keep), 4))
    ref_rot[:, 3] = 1
    return fmb_soma.Skeleton(names, parents, b.offsets[keep], ref_rot), keep


def frames(b, keep, t0, count=4):
    ps, qs = [], []
    for k in range(count):
        p, q = b.sample(t0 + k / 30.0)
        ps.append(p[keep])
        qs.append(q[keep])
    return np.array(ps), np.array(qs)


def heading(skel, p, q):
    wp, wq = skel.world(p, q)
    return fmb_soma.heading_from_legs(wp[..., skel.right_leg, :], wp[..., skel.left_leg, :]), wp[..., 0, :]


def place(skel, tgt, ctx, dist, turn):
    """Move the target's root so its first pose stands dist ahead of the last context pose, turned by turn (rad)."""
    hc, pc = heading(skel, ctx[0][3], ctx[1][3])
    ht, pt = heading(skel, tgt[0][0], tgt[1][0])
    a = hc + turn - ht
    q = fmb_soma.qaxis_y(np.array(a))
    rt = fmb_soma.qrot(q, pt)
    d = hc + turn
    wp = np.array([pc[0] + dist * np.sin(d) - rt[0], 0.0, pc[2] + dist * np.cos(d) - rt[2]])
    p, r = tgt[0].copy(), tgt[1].copy()
    p[:, 0] = wp + fmb_soma.qrot(np.broadcast_to(q, r[:, 0].shape), tgt[0][:, 0])
    r[:, 0] = quat_mul(np.broadcast_to(q, r[:, 0].shape), tgt[1][:, 0])
    return p, r


def build(clip_dir):
    walk = fmb_bvh.BVH(os.path.join(clip_dir, "neutral_walk_ff_180_R_002__A536_M.bvh"))
    jog = fmb_bvh.BVH(os.path.join(clip_dir, "neutral_jog_ff_180_R_001__A534.bvh"))
    idle = fmb_bvh.BVH(os.path.join(clip_dir, "idle_loop_001__A268.bvh"))
    skel, keep = skeleton_from(walk)
    scen = [
        ("walk->jog ahead", walk, 2.0, jog, 1.908333, 1.6, 0.0, 4),
        ("walk->walk turn left", walk, 2.0, walk, 2.55, 1.2, np.pi / 2, 4),
        ("idle->walk", idle, 1.0, walk, 2.55, 1.0, 0.0, 4),
        ("jog->idle stop", jog, 1.908333, idle, 0.0, 1.0, 0.0, 4),
        ("walk->jog one pose", walk, 2.0, jog, 1.908333, 1.6, -0.6, 1),
        ("walk->walk turn back", walk, 2.0, walk, 2.55, 0.8, np.pi, 4),
    ]
    cases = {}
    meta = []
    for name, cc, ct, tc, tt, dist, turn, ntp in scen:
        ctx = frames(cc, keep, ct - 3 / 30.0)
        tgt = place(skel, frames(tc, keep, tt), ctx, dist, turn)
        inp, al = fmb_soma.encode(skel, ctx, tgt, ntp)
        for k, v in inp.items():
            cases.setdefault(k, []).append(v)
        meta.append(dict(name=name, ctx=ctx, tgt=tgt, align=al, ntp=ntp))
    cases = {k: np.concatenate(v, 0) for k, v in cases.items()}
    return skel, cases, meta


if __name__ == "__main__":
    skel, cases, meta = build(sys.argv[1])
    np.savez(sys.argv[2], **cases)
    print("wrote", sys.argv[2], {k: v.shape for k, v in cases.items()})
    for m in meta:
        print(" ", m["name"])
