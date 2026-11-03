#!/usr/bin/env python3
# FellowEngine — the .fmb plan (NumPy executor) against NVIDIA's MotionBricks SOMA reference (PyTorch).
#
#   python fmb_parity_torch.py --ref <a copy of motionbricks tools/onnx as package "mbref", parent dir>
#       --checkpoints <MotionBricks/soma> --fmb soma_f32.fmb [--cases cases.npz] [--save out.npz]
#
# Needs PyTorch and the reference exporter's sources (Apache-2.0, not part of this repository).

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import fmb_plan  # noqa: E402

NAMES = ("global_root_values", "has_global_root_values", "local_root_values", "has_local_root_values", "local_poses",
         "has_local_poses", "allowed_pred_num_tokens")


def example_cases(seed=0):
    """The exporter's trace inputs, then varied ones: masks, token ranges, rotated / moving roots, noisy poses."""
    rng = np.random.default_rng(seed)
    N = 6
    grv = np.zeros((N, 8, 5), np.float32)
    grv[..., 1] = 1.0
    grv[..., 3] = 1.0
    lrv = np.zeros((N, 8, 4), np.float32)
    lrv[..., 3] = 1.0
    lp = np.zeros((N, 8, 267), np.float32)
    lp[..., 87:] = np.tile(np.array([1, 0, 0, 0, 1, 0], np.float32), 30)
    masks = [np.ones((N, 8), np.int32) for _ in range(3)]
    for m in masks:
        m[:2, 4:6] = 0
    allowed = np.zeros((N, 11), np.int32)
    allowed[0, 0] = 1
    allowed[1, 10] = 1
    allowed[2:, 2:9] = 1
    allowed[4, :] = 1
    for b in range(2, N):
        # A root walking forward along a heading, the target 1-2 m away, noisy joints.
        h = rng.uniform(-np.pi, np.pi)
        sp = rng.uniform(0.0, 3.0)
        for f in range(8):
            t = f / 30.0 if f < 4 else 0.6 + (f - 4) / 30.0 + rng.uniform(0, 0.6)
            grv[b, f, 0] = np.sin(h) * sp * t + 0.3
            grv[b, f, 2] = np.cos(h) * sp * t - 0.2
            grv[b, f, 1] = 0.95 + 0.02 * np.sin(7 * t)
            grv[b, f, 3] = np.cos(h)
            grv[b, f, 4] = np.sin(h)
            lrv[b, f, 1] = np.sin(h) * sp * 0
            lrv[b, f, 1] = np.sin(h) * sp
            lrv[b, f, 2] = np.cos(h) * sp
            lrv[b, f, 3] = grv[b, f, 1]
        lp[b, :, :87] = rng.normal(0, 0.3, (8, 87))
        lp[b, :, 87:] += rng.normal(0, 0.1, (8, 180)).astype(np.float32)
    masks[0][3, 6:] = 0
    masks[1][3, 6:] = 0
    masks[2][3, 6:] = 0
    masks[1][:, 3] = 0
    masks[1][5, 7] = 1
    return dict(zip(NAMES, [grv, masks[0], lrv, masks[1], lp, masks[2], allowed]))


def run_torch(ref_parent, checkpoints, case):
    sys.path.insert(0, ref_parent)
    import torch
    from mbref.checkpoints import checkpoint_root, load_sources
    from mbref.checksums import CHECKPOINTS
    from mbref.models.soma import build_soma

    torch.set_num_threads(os.cpu_count() or 4)
    root = checkpoint_root(__import__("pathlib").Path(checkpoints), CHECKPOINTS["soma"])
    model = build_soma(load_sources(root, CHECKPOINTS["soma"]))
    inner = model.model
    rec = {}

    def wrap(name, fn):
        def f(*a, **k):
            r = fn(*a, **k)
            rec[name] = [x.detach().clone() if hasattr(x, "detach") else x for x in r]
            return r
        return f

    inner._predict_root_trajectories = wrap("root", inner._predict_root_trajectories)
    inner._predict_pose_tokens = wrap("pose", inner._predict_pose_tokens)
    vq = inner._vqvae_pose_model
    orig = vq.forward_decoder

    def dec(*a, **k):
        r = orig(*a, **k)
        rec["vq"] = r["recon_state"].detach().clone()
        return r

    vq.forward_decoder = dec
    with torch.no_grad():
        out = model(*[torch.from_numpy(np.ascontiguousarray(case[n])) for n in NAMES])
    res = dict(pred_global_motions=out[0].numpy(), pred_num_tokens=out[1].numpy(),
               root_T=rec["root"][0].numpy(), root_grv=rec["root"][1].numpy(), root_lrv=rec["root"][2].numpy(),
               pose_tokens=rec["pose"][0].numpy(), vq=rec["vq"].numpy())
    return res


def run_plan(fmb, case):
    head, W, P = fmb_plan.load(fmb)
    N = case["global_root_values"].shape[0]
    A = fmb_plan.Acts(head, N)
    ctx = fmb_plan.prepare(head["meta"], case, A)
    fmb_plan.run(head, W, P, A)
    out, T = fmb_plan.decode(head["meta"], A, ctx)
    return dict(pred_global_motions=out, pred_num_tokens=T, root_T=A["meta.T"][:, 0, 0], root_grv=A["r.out"].copy(),
                root_lrv=A["c.lrv"].copy(), pose_tokens=A.tokens, vq=A["v.out"].copy()), A


def compare(ref, got):
    ok = True
    T = ref["pred_num_tokens"].astype(int)
    print("tokens  torch", T.tolist(), " plan", got["pred_num_tokens"].astype(int).tolist())
    if not np.array_equal(T, got["pred_num_tokens"].astype(int)):
        print("  TOKEN COUNT MISMATCH")
        return False
    for b in range(len(T)):
        n = T[b] * 4
        tok_r = ref["pose_tokens"][b, :T[b]]
        tok_g = got["pose_tokens"][b, :T[b]]
        same = (tok_r == tok_g).mean()
        e = {}
        for k in ("root_grv", "root_lrv", "vq", "pred_global_motions"):
            d = np.abs(ref[k][b, :n] - got[k][b, :n])
            e[k] = d.max()
        print(f"  req {b}: T {T[b]:2d}  pose tokens equal {same * 100:5.1f} %  max |d|: " +
              "  ".join(f"{k} {v:.2e}" for k, v in e.items()))
        if same < 1.0:
            ok = False
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", required=True)
    ap.add_argument("--checkpoints", required=True)
    ap.add_argument("--fmb", required=True)
    ap.add_argument("--cases", default=None)
    ap.add_argument("--save", default=None)
    a = ap.parse_args()
    case = dict(np.load(a.cases)) if a.cases else example_cases()
    ref = run_torch(a.ref, a.checkpoints, case)
    got, A = run_plan(a.fmb, case)
    ok = compare(ref, got)
    if a.save:
        np.savez(a.save, **{"in." + k: v for k, v in case.items()}, **{"torch." + k: v for k, v in ref.items()},
                 **{"plan." + k: v for k, v in got.items()})
    print("PARITY", "OK" if ok else "DIFFERS")


if __name__ == "__main__":
    main()
