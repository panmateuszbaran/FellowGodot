"""Frustum-culling potential of mesh-cluster settings for any benchmark scene, from the engine's own geometry dump
(bench/main.gd --mode=geodump): camera-pass triangles submitted over the flythrough with stock instance culling and
with FellowEngine mesh clusters (decision 0007) at several (minimum surface size, cluster size) settings.

Units: surfaces below the minimum size are one unit (stock instance culling); larger ones are split into k-d clusters
(median split of triangle centroids on the longest axis, as the engine builds them). A unit is drawn when its world
AABB intersects the view frustum (planes test on the 8 corners, like the engine's conservative test). Counts are of
base-mesh triangles (stock LOD selection is not modelled), so only the ratios between settings are meaningful.

Usage: python cluster_threshold_geodump.py <geodump json> [out.json]
"""

import json
import math
import os
import sys

import numpy as np

SETTINGS = [(None, None), (32768, 4096), (16384, 4096), (8192, 4096), (8192, 2048), (4096, 1024), (2048, 512)]
ASPECT = 16.0 / 9.0


def kd_leaves(cent, ids, t, out):
    if len(ids) <= t:
        out.append(ids)
        return
    c = cent[ids]
    ax = int(np.argmax(c.max(axis=0) - c.min(axis=0)))
    half = len(ids) // 2
    order = np.argpartition(c[:, ax], half)
    kd_leaves(cent, ids[order[:half]], t, out)
    kd_leaves(cent, ids[order[half:]], t, out)


def view_proj(origin, forward, up, fov_deg, near, far):
    f = np.asarray(forward, dtype=np.float64)
    f /= np.linalg.norm(f)
    x = np.cross(f, np.asarray(up, dtype=np.float64))
    x /= np.linalg.norm(x)
    y = np.cross(x, f)
    rot = np.stack([x, y, -f])
    v = np.eye(4)
    v[:3, :3] = rot
    v[:3, 3] = -rot @ np.asarray(origin, dtype=np.float64)
    t = 1.0 / math.tan(math.radians(fov_deg) * 0.5)
    p = np.zeros((4, 4))
    p[0, 0] = t / ASPECT
    p[1, 1] = t
    p[2, 2] = -(far + near) / (far - near)
    p[2, 3] = -2.0 * far * near / (far - near)
    p[3, 2] = -1.0
    return p @ v


def main():
    jpath = sys.argv[1]
    res = json.load(open(jpath))
    g = res["geodump"]
    stem = os.path.join(os.path.dirname(jpath), "%s_geodump" % res["scene"])
    pos = np.fromfile(stem + "_pos.f32", dtype=np.float32).reshape(-1, 3).astype(np.float64)
    idx = np.fromfile(stem + "_idx.u32", dtype=np.int32).reshape(-1, 3).astype(np.int64)
    near, far = float(g["near"]), float(g["far"])
    cams = [view_proj(c["origin"], c["forward"], c["up"], c["fov"], near, far) for c in g["flythrough"]]
    sizes = sorted(s["tris"] for s in g["surfaces"])
    tot = sum(sizes)
    hist = {lim: sum(t for t in sizes if t >= lim) / max(1, tot) for lim in (512, 2048, 8192, 32768, 131072)}
    print("scene", res["scene"], "surfaces", len(sizes), "triangles", tot, "largest", sizes[-5:], flush=True)
    print("share of triangles in surfaces >= N:", {k: round(v, 3) for k, v in hist.items()}, flush=True)
    surf = []
    for s in g["surfaces"]:
        t0 = s["first_index"] // 3
        tris = idx[t0:t0 + s["tris"]] + s["base_vertex"]
        surf.append((s, tris, pos[tris].mean(axis=1) if s["tris"] >= 512 else None))
    report = {"scene": res["scene"], "surfaces": len(sizes), "triangles": tot, "share_in_surfaces_ge": hist, "settings": []}
    base = None
    for min_surface, leaf in SETTINGS:
        mn, mx, cnt = [], [], []
        for s, tris, cent in surf:
            if min_surface is not None and s["tris"] >= min_surface:
                leaves = []
                kd_leaves(cent, np.arange(len(tris)), leaf, leaves)
                for lf in leaves:
                    v = pos[tris[lf].ravel()]
                    mn.append(v.min(axis=0))
                    mx.append(v.max(axis=0))
                    cnt.append(len(lf))
            else:
                v = pos[s["base_vertex"]:s["base_vertex"] + s["vertex_count"]]
                mn.append(v.min(axis=0))
                mx.append(v.max(axis=0))
                cnt.append(s["tris"])
        mn, mx, cnt = np.array(mn), np.array(mx), np.array(cnt)
        corners = np.stack([np.stack([mx[:, 0] if k & 1 else mn[:, 0], mx[:, 1] if k & 2 else mn[:, 1], mx[:, 2] if k & 4 else mn[:, 2], np.ones(len(mn))], axis=1) for k in range(8)], axis=1)
        drawn = 0
        units = 0
        for vp in cams:
            c = corners @ vp.T
            x, y, z, w = c[..., 0], c[..., 1], c[..., 2], c[..., 3]
            out = (x < -w).all(1) | (x > w).all(1) | (y < -w).all(1) | (y > w).all(1) | (z < -w).all(1) | (z > w).all(1)
            drawn += int(cnt[~out].sum())
            units += int((~out).sum())
        if base is None:
            base = drawn
        r = {"min_surface": min_surface, "cluster": leaf, "units": int(len(cnt)), "triangles_drawn": drawn, "vs_stock": drawn / max(1, base), "units_drawn_per_frame": units / len(cams)}
        report["settings"].append(r)
        print(r, flush=True)
    if len(sys.argv) > 2:
        json.dump(report, open(sys.argv[2], "w"), indent=1)


if __name__ == "__main__":
    main()
