"""Milestone 3 sizing (offline, CPU): how many of the triangles that survive per-cluster frustum culling in the Sponza
flythrough are hidden behind nearer opaque geometry, i.e. what a conservative depth-pyramid (Hi-Z) occlusion test of
the same clusters could skip.

The opaque geometry of the scene (glTF alphaMode OPAQUE/MASK, as Godot renders it in the depth pre-pass) is
rasterised per camera into a nearest-depth buffer (occlusion_raster.c). A cluster (k-d leaf of <= 4096 triangles, as
in decision 0007) is counted occluded when its world AABB lies entirely in front of the near plane and every depth
pixel covered by its projected bounding rectangle is nearer than the AABB's nearest point. Its own triangles are in
the depth buffer too but can never be nearer than its own AABB, so they never hide it.

Usage: python occlusion_potential.py <sponza dir> <occlusion_raster binary> <work dir> <out.json> [width height]
"""

import json
import math
import os
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cluster_potential as cp  # noqa: E402

cp.CLUSTER_SIZES = [1 << 62]  # the base loader builds no clusters here; this script builds its own

T = 4096
NEAR = 0.05
FAR = 4000.0
ASPECT = 16.0 / 9.0


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def view_proj(rot, pos, fov_deg):
    """Row-major view-projection with clip.w = view depth (OpenGL-style, like Godot's Projection)."""
    t = 1.0 / math.tan(math.radians(fov_deg) * 0.5)
    v = np.eye(4)
    v[:3, :3] = rot
    v[:3, 3] = -rot @ pos
    p = np.zeros((4, 4))
    p[0, 0] = t / ASPECT
    p[1, 1] = t
    p[2, 2] = -(FAR + NEAR) / (FAR - NEAR)
    p[2, 3] = -2.0 * FAR * NEAR / (FAR - NEAR)
    p[3, 2] = -1.0
    return p @ v


def main():
    base, raster, work, out = sys.argv[1:5]
    W, H = (int(sys.argv[5]), int(sys.argv[6])) if len(sys.argv) > 6 else (480, 270)
    cp.BASE = base
    os.makedirs(work, exist_ok=True)
    cams = {}
    surfaces = []
    for name, rel in cp.PARTS.items():
        log("loading", name)
        surfaces += cp.load_part(name, rel, cams)
    # Clusters for every surface (small surfaces = one cluster), world-space AABBs.
    for s in surfaces:
        if s.ntri > T:
            cent = s.wp[s.idx].mean(axis=1)
            leaves = []
            cp.kd_clusters(cent, np.arange(s.ntri), T, leaves)
            mn, mx, cnt = [], [], []
            for lf in leaves:
                v = s.wp[np.unique(s.idx[lf].ravel())]
                mn.append(v.min(axis=0))
                mx.append(v.max(axis=0))
                cnt.append(len(lf))
            s.cl = (np.array(mn), np.array(mx), np.array(cnt))
        else:
            s.cl = (s.aabb[0][None, :], s.aabb[1][None, :], np.array([s.ntri]))
    occ = [s for s in surfaces if s.mat in ("OPAQUE", "MASK")]
    log("occluder surfaces", len(occ), "triangles", sum(s.ntri for s in occ))
    # Occluder geometry for the rasteriser.
    pos_chunks, idx_chunks, base_v = [], [], 0
    for s in occ:
        pos_chunks.append(s.wp.astype(np.float32))
        idx_chunks.append((s.idx + base_v).astype(np.uint32))
        base_v += len(s.wp)
    np.concatenate(pos_chunks).tofile(os.path.join(work, "pos.f32"))
    np.concatenate(idx_chunks).tofile(os.path.join(work, "idx.u32"))
    # Cameras: flythrough samples + artist cameras.
    camlist = []
    for i in range(84):
        pos, tgt, fov = cp.fly_sample(i * 0.5)
        rot, p = cp.make_view(pos, tgt)
        camlist.append(("fly%.1f" % (i * 0.5), rot, p, fov))
    for name, c in sorted(cams.items()):
        w = c["world"]
        pos = w[:3, 3]
        fwd = -w[:3, 2] / np.linalg.norm(w[:3, 2])
        rot, p = cp.make_view(pos, pos + fwd * 10.0)
        camlist.append((name, rot, p, math.degrees(c["yfov"])))
    np.array([view_proj(r, p, f) for _, r, p, f in camlist], dtype=np.float32).tofile(os.path.join(work, "cams.f32"))
    dpath = os.path.join(work, "depth.f32")
    log("rasterising", len(camlist), "cameras at", W, "x", H)
    subprocess.check_call([raster, os.path.join(work, "pos.f32"), os.path.join(work, "idx.u32"), os.path.join(work, "cams.f32"), str(W), str(H), str(NEAR), dpath])
    depth = np.fromfile(dpath, dtype=np.float32).reshape(len(camlist), H, W)
    report = {"width": W, "height": H, "cluster_triangles": T, "cameras": []}
    for ci, (name, rot, p, fov) in enumerate(camlist):
        vp = view_proj(rot, p, fov)
        d = depth[ci]
        r = {"camera": name, "frustum": 0, "occlusion": 0, "frustum_clusters": 0, "occlusion_clusters": 0}
        for s in occ:
            mn, mx, cnt = s.cl
            fr = cp.box_codes(mn, mx, rot, p, fov) == 0
            if not fr.any():
                continue
            corners = np.stack([np.stack([mx[:, 0] if k & 1 else mn[:, 0], mx[:, 1] if k & 2 else mn[:, 1], mx[:, 2] if k & 4 else mn[:, 2], np.ones(len(mn))], axis=1) for k in range(8)], axis=1)  # [n,8,4]
            clip = corners @ vp.T
            wv = clip[..., 3]
            vis = fr.copy()
            safe = fr & (wv.min(axis=1) > NEAR)
            for k in np.nonzero(safe)[0]:
                sx = (clip[k, :, 0] / wv[k] * 0.5 + 0.5) * W
                sy = (0.5 - clip[k, :, 1] / wv[k] * 0.5) * H
                x0, x1 = max(0, int(math.floor(sx.min()))), min(W - 1, int(math.ceil(sx.max())))
                y0, y1 = max(0, int(math.floor(sy.min()))), min(H - 1, int(math.ceil(sy.max())))
                if x0 > x1 or y0 > y1:
                    continue
                if d[y0:y1 + 1, x0:x1 + 1].max() < wv[k].min() * 0.999:
                    vis[k] = False
            r["frustum"] += int(cnt[fr].sum())
            r["occlusion"] += int(cnt[vis].sum())
            r["frustum_clusters"] += int(fr.sum())
            r["occlusion_clusters"] += int(vis.sum())
        report["cameras"].append(r)
        if ci % 12 == 0 or not name.startswith("fly"):
            log(name, r)
    fly = [r for r in report["cameras"] if r["camera"].startswith("fly")]
    report["fly_mean"] = {k: float(np.mean([r[k] for r in fly])) for k in ("frustum", "occlusion", "frustum_clusters", "occlusion_clusters")}
    log("fly_mean", report["fly_mean"])
    json.dump(report, open(out, "w"), indent=1)
    log("DONE")


if __name__ == "__main__":
    main()
