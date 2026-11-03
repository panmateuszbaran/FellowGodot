#!/usr/bin/env python3
# FellowEngine — the GPU executor (FellowMotionModel, via fmb_gpu_run.gd) against the NumPy plan and PyTorch.
#
#   python fmb_parity_gpu.py case <parity.npz from fmb_parity_torch.py --save> <case dir>
#   python fmb_parity_gpu.py compare <parity.npz> <out dir of fmb_gpu_run.gd>

import os
import sys

import numpy as np

NAMES_F = ("global_root_values", "local_root_values", "local_poses")
NAMES_I = ("has_global_root_values", "has_local_root_values", "has_local_poses", "allowed_pred_num_tokens")


def write_case(npz, d):
    z = np.load(npz)
    os.makedirs(d, exist_ok=True)
    for k in NAMES_F:
        z["in." + k].astype("<f4").tofile(os.path.join(d, k + ".bin"))
    for k in NAMES_I:
        z["in." + k].astype("<i4").tofile(os.path.join(d, k + ".bin"))
    print("wrote", d, "N =", z["in.global_root_values"].shape[0])


def compare(npz, d, n=None):
    z = np.load(npz)
    N = z["in.global_root_values"].shape[0] if n is None else n
    out = np.fromfile(os.path.join(d, "pred_global_motions.bin"), "<f4").reshape(-1, 64, 366)[:N]
    T = np.fromfile(os.path.join(d, "pred_num_tokens.bin"), "<i4")[:N]
    rout = np.fromfile(os.path.join(d, "act_r.out.bin"), "<f4").reshape(-1, 64, 5)[:N]
    lrv = np.fromfile(os.path.join(d, "act_c.lrv.bin"), "<f4").reshape(-1, 64, 4)[:N]
    vout = np.fromfile(os.path.join(d, "act_v.out.bin"), "<f4").reshape(-1, 64, 365)[:N]
    ok = True
    for ref in ("torch", "plan"):
        Tr = z[ref + ".pred_num_tokens"].astype(int)[:N]
        print(f"vs {ref}: tokens {Tr.tolist()} gpu {T.tolist()}")
        if not np.array_equal(Tr, T):
            if ref == "plan":
                ok = False
            continue
        for b in range(N):
            n = Tr[b] * 4
            e = dict(root=np.abs(z[ref + ".root_grv"][b, :n] - rout[b, :n]).max(),
                     lrv=np.abs(z[ref + ".root_lrv"][b, :n] - lrv[b, :n]).max(),
                     vq=np.abs(z[ref + ".vq"][b, :n] - vout[b, :n]).max(),
                     motion=np.abs(z[ref + ".pred_global_motions"][b, :n] - out[b, :n]).max())
            print(f"  req {b}: " + "  ".join(f"{k} {v:.2e}" for k, v in e.items()))
            if ref == "plan" and e["vq"] > 5e-3:
                ok = False
    print("GPU PARITY (against the plan of the same weights)", "OK" if ok else "DIFFERS")
    return ok


if __name__ == "__main__":
    if sys.argv[1] == "case":
        write_case(sys.argv[2], sys.argv[3])
    else:
        sys.exit(0 if compare(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else None) else 1)
