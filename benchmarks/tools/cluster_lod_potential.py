"""Milestone 3 sizing (offline, CPU): what per-cluster LOD would save in the Sponza flythrough at stock's LOD rule.

Every opaque surface is split into k-d clusters (<= 4096 triangles, as decision 0007). Each cluster gets a LOD chain
simplified on its own with its border locked (meshoptimizer, SIMPLIFY_LOCK_BORDER | SIMPLIFY_ERROR_ABSOLUTE: any mix
of levels between neighbouring clusters stays watertight; attribute seams are open edges in the index data and stay
locked too, so the estimate is conservative). Per camera, each frustum-visible cluster uses the coarsest level whose
geometric error projects to at most `threshold` pixels at the cluster's nearest point (stock Godot's mesh LOD rule,
threshold 1 px by default, applied per cluster instead of per surface). Compared with LOD 0 for every cluster, which
is what stock draws for surfaces whose AABB contains the camera.

Usage: python cluster_lod_potential.py <sponza dir> <out.json> [threshold_px]
"""

import json
import math
import os
import sys
import time

import meshoptimizer as mo
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cluster_potential as cp  # noqa: E402

cp.CLUSTER_SIZES = [1 << 62]

T = 4096
RATIOS = [0.5, 0.25, 0.125, 0.0625]
HEIGHT_PX = 1080


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def main():
    base, out = sys.argv[1:3]
    thr = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
    cp.BASE = base
    cams = {}
    surfaces = []
    for name, rel in cp.PARTS.items():
        log("loading", name)
        surfaces += cp.load_part(name, rel, cams)
    opaque = [s for s in surfaces if s.mat in ("OPAQUE", "MASK")]
    clusters = []  # (mn, mx, [(tris, err), ...] from LOD0, surface name)
    for si, s in enumerate(opaque):
        if s.ntri > T:
            cent = s.wp[s.idx].mean(axis=1)
            leaves = []
            cp.kd_clusters(cent, np.arange(s.ntri), T, leaves)
        else:
            leaves = [np.arange(s.ntri)]
        for lf in leaves:
            tri = s.idx[lf]
            verts, inv = np.unique(tri.ravel(), return_inverse=True)
            local = inv.reshape(-1, 3).astype(np.uint32).ravel()
            pos = np.ascontiguousarray(s.wp[verts], dtype=np.float32)
            chain = [(len(lf), 0.0)]
            for r in RATIOS:
                target = max(3, int(len(local) * r) // 3 * 3)
                dst = np.zeros(len(local), dtype=np.uint32)
                err = np.zeros(1, dtype=np.float32)
                n = mo.simplify(dst, local, pos, target_index_count=target, target_error=1e9, options=mo.SIMPLIFY_LOCK_BORDER | mo.SIMPLIFY_ERROR_ABSOLUTE, result_error=err)
                if n // 3 >= chain[-1][0]:
                    break
                chain.append((n // 3, float(err[0])))
            clusters.append((pos.min(axis=0), pos.max(axis=0), chain, s.name))
        if si % 50 == 0:
            log("surface", si, "/", len(opaque), "clusters", len(clusters))
    mn = np.array([c[0] for c in clusters])
    mx = np.array([c[1] for c in clusters])
    lod0 = np.array([c[2][0][0] for c in clusters])
    report = {"threshold_px": thr, "clusters": len(clusters), "triangles": int(lod0.sum()),
              "chain_reduction": float(np.sum([c[2][-1][0] for c in clusters]) / lod0.sum()), "cameras": []}
    camlist = []
    for i in range(84):
        pos, tgt, fov = cp.fly_sample(i * 0.5)
        camlist.append(("fly%.1f" % (i * 0.5), pos, tgt, fov))
    for name, c in sorted(cams.items()):
        w = c["world"]
        pos = w[:3, 3]
        fwd = -w[:3, 2] / np.linalg.norm(w[:3, 2])
        camlist.append((name, pos, pos + fwd * 10.0, math.degrees(c["yfov"])))
    for name, pos, tgt, fov in camlist:
        rot, p = cp.make_view(pos, tgt)
        vis = cp.box_codes(mn, mx, rot, p, fov) == 0
        closest = np.clip(p[None, :], mn, mx)
        dist = np.maximum(np.linalg.norm(closest - p[None, :], axis=1), 1e-3)
        px_per_unit = HEIGHT_PX * 0.5 / math.tan(math.radians(fov) * 0.5)
        lod_tris = 0
        for k in np.nonzero(vis)[0]:
            best = clusters[k][2][0][0]
            for tris, err in clusters[k][2][1:]:
                if err / dist[k] * px_per_unit <= thr:
                    best = tris
                else:
                    break
            lod_tris += best
        r = {"camera": name, "lod0": int(lod0[vis].sum()), "cluster_lod": int(lod_tris)}
        report["cameras"].append(r)
        if not name.startswith("fly") or name.endswith(".0") and int(float(name[3:])) % 6 == 0:
            log(name, r)
    fly = [r for r in report["cameras"] if r["camera"].startswith("fly")]
    report["fly_mean"] = {k: float(np.mean([r[k] for r in fly])) for k in ("lod0", "cluster_lod")}
    log("fly_mean", report["fly_mean"], "chain_reduction", report["chain_reduction"])
    json.dump(report, open(out, "w"), indent=1)
    log("DONE")


if __name__ == "__main__":
    main()
