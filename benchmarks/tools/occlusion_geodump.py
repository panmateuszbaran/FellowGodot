"""Offline occlusion-culling estimate for any benchmark scene, from the engine's own geometry dump
(bench/main.gd --mode=geodump): which share of the triangles that survive stock's culling plus FellowEngine's
per-cluster frustum culling (decision 0007) is hidden behind nearer opaque geometry.

Units, as the engine culls them: every instance surface is one unit (stock instance culling) except surfaces with at
least MIN_SURFACE triangles, which are split into k-d clusters of at most T triangles (FellowEngine mesh clusters).

Occluders are rasterised per camera into a nearest-view-depth buffer (occlusion_raster.c). Two occluder sets bound
the estimate: "solid" (materials without any transparency; surfaces without a BaseMaterial3D are counted as solid
only when they have no material at all) and "masked" (additionally alpha-scissor / alpha-hash surfaces as if they
were solid, which over-estimates occlusion behind foliage). A unit is counted occluded when its world AABB lies
entirely beyond the near plane and the depth buffer is nearer than the AABB's nearest point over the unit's projected
bounding rectangle, tested two ways: "exact" (every pixel of the rectangle) and "hiz" (a max-depth pyramid read at the
level where the rectangle spans at most 2x2 texels, as a GPU two-phase occlusion pass would). The depth buffer holds
the current frame's full occluder set, so the numbers are the potential of a perfect two-phase implementation.

Usage: python occlusion_geodump.py <geodump json> <occlusion_raster binary> <work dir> <out.json> [width height]
(the _pos.f32 / _idx.u32 files are found next to the json: <dir>/<scene>_geodump_pos.f32).
"""

import json
import math
import os
import subprocess
import sys
import time

import numpy as np

T = 4096
MIN_SURFACE = 32768
ASPECT = 16.0 / 9.0
BLEND = (1, 4)  # TRANSPARENCY_ALPHA, TRANSPARENCY_ALPHA_DEPTH_PRE_PASS
MASKED = (2, 3)  # TRANSPARENCY_ALPHA_SCISSOR, TRANSPARENCY_ALPHA_HASH


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def kd_leaves(cent, ids, out):
    if len(ids) <= T:
        out.append(ids)
        return
    c = cent[ids]
    ax = int(np.argmax(c.max(axis=0) - c.min(axis=0)))
    half = len(ids) // 2
    order = np.argpartition(c[:, ax], half)
    kd_leaves(cent, ids[order[:half]], out)
    kd_leaves(cent, ids[order[half:]], out)


def view_proj(origin, forward, up, fov_deg, near, far):
    f = np.asarray(forward, dtype=np.float64)
    f /= np.linalg.norm(f)
    u = np.asarray(up, dtype=np.float64)
    x = np.cross(f, u)
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


def max_pyramid(d):
    levels = [d]
    while max(levels[-1].shape) > 1:
        a = levels[-1]
        h, w = a.shape
        a = np.pad(a, ((0, h & 1), (0, w & 1)), mode="edge")
        levels.append(np.maximum(np.maximum(a[0::2, 0::2], a[0::2, 1::2]), np.maximum(a[1::2, 0::2], a[1::2, 1::2])))
    return levels


def main():
    jpath, raster, work, out = sys.argv[1:5]
    W, H = (int(sys.argv[5]), int(sys.argv[6])) if len(sys.argv) > 6 else (480, 270)
    os.makedirs(work, exist_ok=True)
    res = json.load(open(jpath))
    g = res["geodump"]
    stem = os.path.join(os.path.dirname(jpath), "%s_geodump" % res["scene"])
    pos = np.fromfile(stem + "_pos.f32", dtype=np.float32).reshape(-1, 3)
    idx = np.fromfile(stem + "_idx.u32", dtype=np.int32).reshape(-1, 3).astype(np.int64)
    near, far = float(g["near"]), float(g["far"])
    log("scene", res["scene"], "vertices", len(pos), "triangles", len(idx), "surfaces", len(g["surfaces"]), "multimeshes", len(g["multimeshes"]))

    # Units (instance surfaces or k-d clusters of large surfaces).
    mn, mx, cnt, kind = [], [], [], []  # kind: 0 solid, 1 masked, 2 blended
    solid_tris, masked_tris = [], []
    for s in g["surfaces"]:
        tr = s["transparency"]
        k = 2 if tr in BLEND or s["blend"] else 1 if tr in MASKED else 0
        if tr == -1 and s["material"] not in ("",):
            k = 1  # ShaderMaterial or other: unknown coverage, treated like masked geometry.
        t0 = s["first_index"] // 3
        tris = idx[t0:t0 + s["tris"]] + s["base_vertex"]
        if k == 0:
            solid_tris.append(tris)
        elif k == 1:
            masked_tris.append(tris)
        if s["tris"] >= MIN_SURFACE:
            cent = pos[tris].mean(axis=1)
            leaves = []
            kd_leaves(cent, np.arange(len(tris)), leaves)
            for lf in leaves:
                v = pos[np.unique(tris[lf].ravel())]
                mn.append(v.min(axis=0))
                mx.append(v.max(axis=0))
                cnt.append(len(lf))
                kind.append(k)
        else:
            v = pos[s["base_vertex"]:s["base_vertex"] + s["vertex_count"]]
            mn.append(v.min(axis=0))
            mx.append(v.max(axis=0))
            cnt.append(s["tris"])
            kind.append(k)
    mn, mx = np.array(mn, dtype=np.float64), np.array(mx, dtype=np.float64)
    cnt, kind = np.array(cnt), np.array(kind)
    log("units", len(cnt), "solid/masked/blended triangles", [int(cnt[kind == k].sum()) for k in range(3)])

    # Cameras: flythrough + fixed cameras.
    cams = [("fly%.1f" % c["t"], view_proj(c["origin"], c["forward"], c["up"], c["fov"], near, far)) for c in g["flythrough"]]
    for name, c in sorted(g["fixed_cameras"].items()):
        p, t = np.array(c["pos"]), np.array(c["target"])
        f = t - p
        up = [0.0, 1.0, 0.0] if abs(f[1]) < 0.999 * np.linalg.norm(f) else [0.0, 0.0, -1.0]
        cams.append((name, view_proj(p, f, up, c["fov"], near, far)))
    np.array([m for _, m in cams], dtype=np.float32).tofile(os.path.join(work, "cams.f32"))
    pos.astype(np.float32).tofile(os.path.join(work, "pos.f32"))

    depths = {}
    for oname, sets in (("solid", solid_tris), ("masked", solid_tris + masked_tris)):
        ipath = os.path.join(work, "idx_%s.u32" % oname)
        np.concatenate(sets).astype(np.uint32).tofile(ipath)
        dpath = os.path.join(work, "depth_%s.f32" % oname)
        log("rasterising", oname, sum(len(s) for s in sets), "triangles,", len(cams), "cameras at", W, "x", H)
        subprocess.check_call([raster, os.path.join(work, "pos.f32"), ipath, os.path.join(work, "cams.f32"), str(W), str(H), str(near), dpath])
        depths[oname] = np.fromfile(dpath, dtype=np.float32).reshape(len(cams), H, W)

    corners = np.stack([np.stack([mx[:, 0] if k & 1 else mn[:, 0], mx[:, 1] if k & 2 else mn[:, 1], mx[:, 2] if k & 4 else mn[:, 2], np.ones(len(mn))], axis=1) for k in range(8)], axis=1)
    report = {"scene": res["scene"], "width": W, "height": H, "cluster_triangles": T, "min_surface_triangles": MIN_SURFACE, "units": int(len(cnt)), "cameras": []}
    for ci, (name, vp) in enumerate(cams):
        clip = corners @ vp.T  # [n, 8, 4]
        x, y, w = clip[..., 0], clip[..., 1], clip[..., 3]
        outside = (x < -w).all(1) | (x > w).all(1) | (y < -w).all(1) | (y > w).all(1) | (w < near).all(1) | (w > far).all(1)
        fr = ~outside
        safe = fr & (w.min(axis=1) > near)
        sx = (x / np.where(w > near, w, 1.0) * 0.5 + 0.5) * W
        sy = (0.5 - y / np.where(w > near, w, 1.0) * 0.5) * H
        x0 = np.clip(np.floor(sx.min(1)), 0, W - 1).astype(int)
        x1 = np.clip(np.ceil(sx.max(1)), 0, W - 1).astype(int)
        y0 = np.clip(np.floor(sy.min(1)), 0, H - 1).astype(int)
        y1 = np.clip(np.ceil(sy.max(1)), 0, H - 1).astype(int)
        wmin = w.min(axis=1) * 0.999
        r = {"camera": name}
        for k, kn in enumerate(("solid", "masked", "blended")):
            r["frustum_" + kn] = int(cnt[fr & (kind == k)].sum())
        for oname, dep in depths.items():
            d = dep[ci]
            pyr = max_pyramid(d)
            occ_exact = np.zeros(len(cnt), dtype=bool)
            occ_hiz = np.zeros(len(cnt), dtype=bool)
            for u in np.nonzero(safe)[0]:
                if d[y0[u]:y1[u] + 1, x0[u]:x1[u] + 1].max() < wmin[u]:
                    occ_exact[u] = True
                    span = max(x1[u] - x0[u], y1[u] - y0[u]) + 1
                    lv = max(0, int(math.ceil(math.log2(max(span, 1)))) - 1)
                    while lv < len(pyr) - 1 and ((x1[u] >> lv) - (x0[u] >> lv) > 1 or (y1[u] >> lv) - (y0[u] >> lv) > 1):
                        lv += 1
                    p = pyr[lv]
                    if p[y0[u] >> lv:(y1[u] >> lv) + 1, x0[u] >> lv:(x1[u] >> lv) + 1].max() < wmin[u]:
                        occ_hiz[u] = True
            for k, kn in enumerate(("solid", "masked", "blended")):
                sel = kind == k
                r["occluded_exact_%s_by_%s" % (kn, oname)] = int(cnt[occ_exact & sel].sum())
                r["occluded_hiz_%s_by_%s" % (kn, oname)] = int(cnt[occ_hiz & sel].sum())
        report["cameras"].append(r)
        if ci % 12 == 0 or not name.startswith("fly"):
            tot = r["frustum_solid"] + r["frustum_masked"]
            log(name, "frustum", tot, "hiz/solid", r["occluded_hiz_solid_by_solid"] + r["occluded_hiz_masked_by_solid"], "hiz/masked", r["occluded_hiz_solid_by_masked"] + r["occluded_hiz_masked_by_masked"])
    fly = [r for r in report["cameras"] if r["camera"].startswith("fly")]
    keys = [k for k in fly[0] if k != "camera"]
    report["fly_sum"] = {k: int(sum(r[k] for r in fly)) for k in keys}
    s = report["fly_sum"]
    depth_pass = s["frustum_solid"] + s["frustum_masked"]
    summary = {"camera_pass_triangles": depth_pass, "blended_triangles": s["frustum_blended"]}
    for oname in ("solid", "masked"):
        for t in ("exact", "hiz"):
            occ = s["occluded_%s_solid_by_%s" % (t, oname)] + s["occluded_%s_masked_by_%s" % (t, oname)]
            summary["%s_by_%s" % (t, oname)] = occ / max(1, depth_pass)
            summary["%s_by_%s_blended" % (t, oname)] = s["occluded_%s_blended_by_%s" % (t, oname)] / max(1, s["frustum_blended"])
    report["summary"] = summary
    log("summary", json.dumps(summary))
    json.dump(report, open(out, "w"), indent=1)
    log("DONE")


if __name__ == "__main__":
    main()
