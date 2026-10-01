"""Milestone 3 sizing (offline, CPU): how many of the triangles that stock Godot submits for Sponza lie outside the
view frustum, and how much of that a per-cluster frustum test would skip.

Stock culls per instance (instance AABB vs frustum) and selects one LOD per surface from the distance to the instance
AABB; a camera inside a large mesh's AABB always gets LOD 0. This script assumes LOD 0 for every surface (upper bound
of what stock draws; the large Sponza meshes contain the camera, so they are LOD 0 in stock as well) and reports, per
camera of the benchmark flythrough and per artist camera:
  stock      triangles of instances whose AABB intersects the frustum
  exact      triangles not entirely outside one frustum plane (what the GPU actually has to rasterise)
  cl<T>      triangles of k-d clusters (<= T triangles, median split on the longest centroid axis) whose AABB
             intersects the frustum, and the number of draw runs (maximal runs of consecutive visible clusters)
Usage: python benchmarks/tools/cluster_potential.py <out.json>   (Windows paths of the FellowGodot checkout, Sponza assets)
"""

import json
import math
import os
import sys
import time

import numpy as np

R = r"C:\Users\matek\Documents\GitHub\FellowGodot"
BASE = os.path.join(R, "benchmarks", "project", "external", "sponza")
PARTS = {
    "main": "main/NewSponza_Main_glTF_003.gltf",
    "curtains": "curtains/NewSponza_Curtains_glTF.gltf",
    "ivy": "ivy/NewSponza_IvyGrowth_glTF.gltf",
    "trees": "trees/NewSponza_CypressTree_glTF.gltf",
}
CLUSTER_SIZES = [4096, 16384, 65536]
LARGE = 32768  # surfaces with at least this many triangles are reported separately ("large")

CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def load_gltf(path):
    d = json.load(open(path, encoding="utf-8"))
    base = os.path.dirname(path)
    bufs = [np.fromfile(os.path.join(base, b["uri"]), dtype=np.uint8) for b in d["buffers"]]

    def acc(i):
        a = d["accessors"][i]
        bv = d["bufferViews"][a["bufferView"]]
        dt = np.dtype(CT[a["componentType"]])
        n = NC[a["type"]]
        off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = bv.get("byteStride", 0) or dt.itemsize * n
        buf = bufs[bv["buffer"]]
        arr = np.ndarray((a["count"], n), dtype=dt, buffer=buf.data, offset=off, strides=(stride, dt.itemsize)).copy()
        if a.get("normalized", False) and dt.kind in "iu":
            info = np.iinfo(dt)
            arr = np.maximum(arr.astype(np.float64) / info.max, -1.0)
        return arr

    return d, acc


def quat_to_mat(q):
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def node_local(n):
    if "matrix" in n:
        return np.array(n["matrix"], dtype=np.float64).reshape(4, 4).T
    m = np.eye(4)
    s = np.array(n.get("scale", [1, 1, 1]), dtype=np.float64)
    r = quat_to_mat(n.get("rotation", [0, 0, 0, 1]))
    m[:3, :3] = r * s[None, :]
    m[:3, 3] = n.get("translation", [0, 0, 0])
    return m


def kd_clusters(cent, tri_ids, T, out):
    """Median split on the longest centroid axis until <= T triangles; appends leaf triangle id arrays in DFS order."""
    if len(tri_ids) <= T:
        out.append(tri_ids)
        return
    c = cent[tri_ids]
    ext = c.max(axis=0) - c.min(axis=0)
    ax = int(np.argmax(ext))
    half = len(tri_ids) // 2
    order = np.argpartition(c[:, ax], half)
    kd_clusters(cent, tri_ids[order[:half]], T, out)
    kd_clusters(cent, tri_ids[order[half:]], T, out)


class Surface:
    pass


def load_part(name, rel, cams):
    d, acc = load_gltf(os.path.join(BASE, rel))
    surfaces = []
    scene = d["scenes"][d.get("scene", 0)]
    stack = [(i, np.eye(4)) for i in scene["nodes"]]
    while stack:
        ni, parent = stack.pop()
        n = d["nodes"][ni]
        world = parent @ node_local(n)
        if "camera" in n:
            cam = d["cameras"][n["camera"]]
            if cam.get("type") == "perspective":
                cams[n.get("name", "cam%d" % ni)] = {"world": world, "yfov": cam["perspective"]["yfov"]}
        if "mesh" in n:
            for pi, p in enumerate(d["meshes"][n["mesh"]]["primitives"]):
                if p.get("mode", 4) != 4 or "indices" not in p:
                    continue
                pos = acc(p["attributes"]["POSITION"]).astype(np.float64)
                idx = acc(p["indices"]).reshape(-1, 3).astype(np.int64)
                wp = (pos @ world[:3, :3].T + world[:3, 3]).astype(np.float32)
                s = Surface()
                s.part = name
                s.name = "%s/%s[%d]" % (name, d["meshes"][n["mesh"]].get("name", n["mesh"]), pi)
                s.mat = d["materials"][p["material"]].get("alphaMode", "OPAQUE") if "material" in p else "OPAQUE"
                s.wp = wp
                s.idx = idx
                s.ntri = len(idx)
                s.aabb = (wp.min(axis=0), wp.max(axis=0))
                s.clusters = {}
                if s.ntri >= 2 * min(CLUSTER_SIZES):
                    cent = wp[idx].mean(axis=1)
                    for T in CLUSTER_SIZES:
                        leaves = []
                        kd_clusters(cent, np.arange(s.ntri), T, leaves)
                        boxes = []
                        counts = []
                        for lf in leaves:
                            v = wp[np.unique(idx[lf].ravel())]
                            boxes.append((v.min(axis=0), v.max(axis=0)))
                            counts.append(len(lf))
                        s.clusters[T] = (np.array([b[0] for b in boxes]), np.array([b[1] for b in boxes]), np.array(counts))
                surfaces.append(s)
        for c in n.get("children", []):
            stack.append((c, world))
    return surfaces


def make_view(pos, target):
    pos = np.asarray(pos, dtype=np.float64)
    f = np.asarray(target, dtype=np.float64) - pos
    f /= np.linalg.norm(f)
    up = np.array([0.0, 1.0, 0.0])
    if abs(f @ up) > 0.999:
        up = np.array([0.0, 0.0, -1.0])
    z = -f
    x = np.cross(up, z)
    x /= np.linalg.norm(x)
    y = np.cross(z, x)
    rot = np.stack([x, y, z])  # world -> view rows
    return rot, pos


def outcodes(pts, rot, pos, fov_deg, aspect=16.0 / 9.0, near=0.05, far=4000.0):
    v = (pts.astype(np.float64) - pos) @ rot.T
    t = 1.0 / math.tan(math.radians(fov_deg) * 0.5)
    cx = v[:, 0] * t / aspect
    cy = v[:, 1] * t
    w = -v[:, 2]
    code = np.zeros(len(pts), dtype=np.uint8)
    code |= (cx > w).astype(np.uint8)
    code |= (cx < -w).astype(np.uint8) << 1
    code |= (cy > w).astype(np.uint8) << 2
    code |= (cy < -w).astype(np.uint8) << 3
    code |= (w < near).astype(np.uint8) << 4
    code |= (w > far).astype(np.uint8) << 5
    return code


def box_codes(mn, mx, rot, pos, fov):
    corners = []
    for i in range(8):
        corners.append(np.where([(i >> 0) & 1, (i >> 1) & 1, (i >> 2) & 1], mx, mn) if mn.ndim == 1 else
                       np.stack([mx[:, 0] if i & 1 else mn[:, 0], mx[:, 1] if i & 2 else mn[:, 1], mx[:, 2] if i & 4 else mn[:, 2]], axis=1))
    if mn.ndim == 1:
        c = outcodes(np.array(corners), rot, pos, fov)
        return np.bitwise_and.reduce(c)
    codes = [outcodes(c, rot, pos, fov) for c in corners]
    acc = codes[0]
    for c in codes[1:]:
        acc = acc & c
    return acc  # per box: nonzero = outside


def cr(p0, p1, p2, p3, u):
    u2 = u * u
    u3 = u2 * u
    return 0.5 * ((2.0 * p1) + (-p0 + p2) * u + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * u2 + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * u3)


FLY = [
    [0.0, (-9.0, 1.7, 0.0), (10.0, 2.5, 0.0), 60.0],
    [6.0, (-2.0, 1.7, 1.5), (5.0, 3.0, -3.0), 60.0],
    [12.0, (5.0, 1.7, -1.5), (12.0, 2.0, 2.0), 60.0],
    [18.0, (10.0, 2.0, 0.0), (0.0, 6.0, 0.0), 60.0],
    [24.0, (6.0, 5.0, 0.0), (-8.0, 6.0, 3.0), 60.0],
    [30.0, (-2.0, 8.0, 0.0), (-9.0, 7.0, 5.0), 60.0],
    [36.0, (-6.0, 4.0, 0.0), (8.0, 1.0, 0.0), 60.0],
    [42.0, (-9.0, 1.7, 0.0), (10.0, 2.5, 0.0), 60.0],
]


def fly_sample(t):
    n = len(FLY)
    i = 0
    while i < n - 1 and FLY[i + 1][0] < t:
        i += 1
    k1, k2 = FLY[i], FLY[min(i + 1, n - 1)]
    k0, k3 = FLY[max(i - 1, 0)], FLY[min(i + 2, n - 1)]
    u = (t - k1[0]) / max(k2[0] - k1[0], 1e-6)
    P = [np.array(k[1]) for k in (k0, k1, k2, k3)]
    Tg = [np.array(k[2]) for k in (k0, k1, k2, k3)]
    return cr(*P, u), cr(*Tg, u), 60.0


def evaluate(surfaces, rot, pos, fov):
    res = {"stock": 0, "exact": 0, "stock_large": 0, "exact_large": 0}
    for T in CLUSTER_SIZES:
        res["cl%d" % T] = 0
        res["runs%d" % T] = 0
        res["clusters_vis%d" % T] = 0
    for s in surfaces:
        if box_codes(s.aabb[0], s.aabb[1], rot, pos, fov) != 0:
            continue
        large = s.ntri >= LARGE
        res["stock"] += s.ntri
        code = outcodes(s.wp, rot, pos, fov)
        tc = code[s.idx[:, 0]] & code[s.idx[:, 1]] & code[s.idx[:, 2]]
        vis = int((tc == 0).sum())
        res["exact"] += vis
        if large:
            res["stock_large"] += s.ntri
            res["exact_large"] += vis
        for T in CLUSTER_SIZES:
            if T in s.clusters:
                mn, mx, cnt = s.clusters[T]
                v = box_codes(mn, mx, rot, pos, fov) == 0
                res["cl%d" % T] += int(cnt[v].sum())
                res["clusters_vis%d" % T] += int(v.sum())
                runs = int(v[0]) + int(np.sum(v[1:] & ~v[:-1]))
                res["runs%d" % T] += runs
            else:
                res["cl%d" % T] += s.ntri
                res["runs%d" % T] += 1
    return res


def main():
    out = sys.argv[1]
    cams = {}
    surfaces = []
    for name, rel in PARTS.items():
        log("loading", name)
        surfaces += load_part(name, rel, cams)
    total = sum(s.ntri for s in surfaces)
    large = sorted([s for s in surfaces if s.ntri >= LARGE], key=lambda s: -s.ntri)
    log("surfaces", len(surfaces), "triangles (instanced)", total, "large surfaces", len(large), sum(s.ntri for s in large))
    report = {"total_tris_instanced": total, "surfaces": len(surfaces),
              "large": [{"name": s.name, "tris": s.ntri, "alpha": s.mat, "aabb": [s.aabb[0].tolist(), s.aabb[1].tolist()]} for s in large[:40]],
              "per_part": {p: sum(s.ntri for s in surfaces if s.part == p) for p in PARTS}, "fly": [], "artist": {}}
    for i in range(84):
        t = i * 0.5
        pos, tgt, fov = fly_sample(t)
        rot, p = make_view(pos, tgt)
        r = evaluate(surfaces, rot, p, fov)
        r["t"] = t
        report["fly"].append(r)
        if i % 12 == 0:
            log("fly", t, r)
    for name, c in cams.items():
        w = c["world"]
        pos = w[:3, 3]
        fwd = -w[:3, 2] / np.linalg.norm(w[:3, 2])
        rot, p = make_view(pos, pos + fwd * 10.0)
        r = evaluate(surfaces, rot, p, math.degrees(c["yfov"]))
        report["artist"][name] = r
        log("cam", name, r)
    keys = [k for k in report["fly"][0] if k != "t"]
    report["fly_mean"] = {k: float(np.mean([r[k] for r in report["fly"]])) for k in keys}
    log("fly_mean", report["fly_mean"])
    json.dump(report, open(out, "w"), indent=1)
    log("DONE")


if __name__ == "__main__":
    main()
