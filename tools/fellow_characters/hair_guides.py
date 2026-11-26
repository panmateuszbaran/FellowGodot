"""Milestone 244: a MakeHuman hairstyle traced into strand guides (decision 0386).

MakeHuman's hair is a shell over the head and cards hanging from it, its strands painted in one texture. FellowEngine's
strand hair (FellowHair3D) wants guides: curves from the scalp out along the hair. They are traced here from the hair
mesh itself, so that each person keeps its hairstyle:

* **The flow.** The painted strands' direction at every texel, from the texture's structure tensor (the direction
  across which its brightness changes least), smoothed; carried onto the mesh through each triangle's UV mapping.
* **The roots.** The body's vertices near the head whose normal meets the hair mesh close above them (where the hair
  covers the scalp, and its texture is opaque there), spread by farthest-point sampling.
* **A guide.** Up from its root to its depth under the hair's surface, then along the flow over the surface (each step
  projected back to the same depth under it, the sign of the flow kept from step to step, a little gravity), until the
  texture is transparent there (the strands' ends painted), the surface is left (its edge) or it is too long.

It needs Blender's mathutils (a BVH of the hair mesh), numpy and scipy; make_characters.py calls trace() with the export's
data and writes <name>_hair.json beside the .glb (the guides in glTF's axes).
"""

import json
import math

import numpy as np


def flow_field(p_rgba, p_sigma=3.0):
    """The painted strands' direction at every texel (a unit vector in UV space: +u right, +v up) and how sure it is
    (0-1), from the structure tensor of the texture's luminance (its alpha counted)."""
    from scipy.ndimage import gaussian_filter

    lum = (0.3 * p_rgba[..., 0] + 0.59 * p_rgba[..., 1] + 0.11 * p_rgba[..., 2]) * p_rgba[..., 3]
    gy, gx = np.gradient(lum)

    def blur(p_a):
        return gaussian_filter(p_a, p_sigma)

    jxx = blur(gx * gx)
    jxy = blur(gx * gy)
    jyy = blur(gy * gy)
    theta = 0.5 * np.arctan2(2.0 * jxy, jxx - jyy) + 0.5 * math.pi  # (across the gradient: along the strands)
    lam = np.sqrt((jxx - jyy) ** 2 + 4.0 * jxy * jxy)
    coherence = lam / np.maximum(jxx + jyy, 1e-12)
    # image (x right, y down) to UV (u right, v up)
    return np.stack([np.cos(theta), -np.sin(theta)], axis=-1), np.clip(coherence, 0.0, 1.0)


class _Mesh:
    def __init__(self, p_tris, p_uvs):
        from mathutils import Vector
        from mathutils.bvhtree import BVHTree

        self.tris = np.asarray(p_tris, np.float64)  # (n, 3, 3)
        self.uvs = np.asarray(p_uvs, np.float64)  # (n, 3, 2)
        verts = [Vector(v) for v in self.tris.reshape(-1, 3)]
        polys = [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(len(self.tris))]
        self.bvh = BVHTree.FromPolygons(verts, polys, all_triangles=True, epsilon=0.0)
        # Each triangle's UV-to-space Jacobian: d(position) / d(u, v).
        e1 = self.tris[:, 1] - self.tris[:, 0]
        e2 = self.tris[:, 2] - self.tris[:, 0]
        t1 = self.uvs[:, 1] - self.uvs[:, 0]
        t2 = self.uvs[:, 2] - self.uvs[:, 0]
        det = t1[:, 0] * t2[:, 1] - t2[:, 0] * t1[:, 1]
        det = np.where(np.abs(det) < 1e-12, 1e-12, det)
        self.dpdu = (e1 * t2[:, 1:2] - e2 * t1[:, 1:2]) / det[:, None]
        self.dpdv = (e2 * t1[:, 0:1] - e1 * t2[:, 0:1]) / det[:, None]

    def nearest(self, p):
        from mathutils import Vector

        loc, nrm, idx, dist = self.bvh.find_nearest(Vector(p))
        if loc is None:
            return None
        return np.array(loc), np.array(nrm), idx, dist

    def ray(self, p, d, far):
        from mathutils import Vector

        loc, nrm, idx, dist = self.bvh.ray_cast(Vector(p), Vector(d), far)
        if loc is None:
            return None
        return np.array(loc), np.array(nrm), idx, dist

    def uv_at(self, idx, p):
        a, b, c = self.tris[idx]
        v0, v1, v2 = b - a, c - a, p - a
        d00, d01, d11 = v0 @ v0, v0 @ v1, v1 @ v1
        d20, d21 = v2 @ v0, v2 @ v1
        den = d00 * d11 - d01 * d01
        if abs(den) < 1e-18:
            return self.uvs[idx].mean(axis=0)
        w1 = (d11 * d20 - d01 * d21) / den
        w2 = (d00 * d21 - d01 * d20) / den
        w0 = 1.0 - w1 - w2
        return w0 * self.uvs[idx][0] + w1 * self.uvs[idx][1] + w2 * self.uvs[idx][2]


def _texel(p_img, p_uv):
    h, w = p_img.shape[:2]
    x = int((p_uv[0] % 1.0) * w) % w
    y = int((1.0 - (p_uv[1] % 1.0)) * h) % h
    return p_img[y, x]


def _resample(p_pts, p_count):
    pts = np.asarray(p_pts)
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    s = np.concatenate([[0.0], np.cumsum(seg)])
    total = s[-1]
    out = []
    for t in np.linspace(0.0, total, p_count):
        k = min(np.searchsorted(s, t, side="right") - 1, len(pts) - 2)
        f = 0.0 if seg[k] < 1e-12 else (t - s[k]) / seg[k]
        out.append(pts[k] * (1.0 - f) + pts[k + 1] * f)
    return np.array(out)


def trace(p_tris, p_uvs, p_rgba, p_body_verts, p_body_normals, p_head_center, p_head_radius, guides=320, step=0.005,
          max_length=0.7, points=16, seed=7, up=(0.0, 0.0, 1.0)):
    """The guides (a list of arrays of `points` points, root first) and the hair's root and tip colours (sRGB), in the
    space of the data given (`up` its vertical)."""
    rng = np.random.default_rng(seed)
    mesh = _Mesh(p_tris, p_uvs)
    flow, sure = flow_field(p_rgba)
    alpha = p_rgba[..., 3]
    up = np.asarray(up, np.float64)
    hc = np.asarray(p_head_center, np.float64)

    # The roots: scalp vertices with the hair's surface (opaque) close over them.
    cand = []
    for v, n in zip(p_body_verts, p_body_normals):
        rel = v - hc
        if np.linalg.norm(rel) > p_head_radius * 1.6 or rel @ up < -p_head_radius * 0.9:
            continue
        hit = mesh.ray(v + n * 0.0005, n, 0.06)
        if hit is None:
            continue
        loc, nrm, idx, dist = hit
        if _texel(alpha, mesh.uv_at(idx, loc)) < 0.5:
            continue
        cand.append((v, n, dist))
    if not cand:
        return [], (0.1, 0.07, 0.05), (0.2, 0.14, 0.1)
    cp = np.array([c[0] for c in cand])
    pick = [int(rng.integers(len(cand)))]
    near = np.linalg.norm(cp - cp[pick[0]], axis=1)
    while len(pick) < min(guides, len(cand)):
        k = int(np.argmax(near))
        if near[k] <= 1e-6:
            break
        pick.append(k)
        near = np.minimum(near, np.linalg.norm(cp - cp[k], axis=1))

    down = -up
    out = []
    roots_col, tips_col = [], []
    for k in pick:
        root, n, shell = cand[k]
        below = min(0.002 + rng.random() * 0.006, shell * 0.7)  # the strand's depth under the hair's surface
        pts = [root]
        first = mesh.nearest(root + n * shell)
        if first is None:
            continue
        loc, nrm, idx, _ = first
        out_n = nrm if nrm @ (loc - hc) >= 0.0 else -nrm
        p = loc - out_n * below
        pts.append(p)
        prev = None
        length = np.linalg.norm(p - root)
        cols = [_texel(p_rgba, mesh.uv_at(idx, loc))[:3]]
        while length < max_length and len(pts) < 400:
            near_ = mesh.nearest(p)
            if near_ is None:
                break
            loc, nrm, idx, _ = near_
            out_n = nrm if nrm @ (loc - hc) >= 0.0 else -nrm
            uv = mesh.uv_at(idx, loc)
            f = _texel(flow, uv)
            t = mesh.dpdu[idx] * f[0] + mesh.dpdv[idx] * f[1]
            t = t - out_n * (t @ out_n)
            tl = np.linalg.norm(t)
            if tl < 1e-9:
                break
            t = t / tl
            if prev is None:
                horiz = (p - hc) - up * ((p - hc) @ up)
                hl = np.linalg.norm(horiz)
                want = down + (horiz / hl * 0.6 if hl > 1e-6 else 0.0)
                if t @ want < 0.0:
                    t = -t
            else:
                if t @ prev < 0.0:
                    t = -t
                # (a weak flow is held to the strand's own direction; a little gravity)
                c = float(_texel(sure, uv))
                t = t * (0.35 + 0.65 * c) + prev * (1.0 - (0.35 + 0.65 * c))
            t = t + down * 0.08
            t = t - out_n * (t @ out_n)
            t = t / max(np.linalg.norm(t), 1e-9)
            q = p + t * step
            hit = mesh.nearest(q)
            if hit is None:
                break
            loc2, nrm2, idx2, d2 = hit
            if d2 > below + 0.004:  # off the surface's edge (the step left it: its nearest point is on the edge)
                break
            uv2 = mesh.uv_at(idx2, loc2)
            if _texel(alpha, uv2) < 0.3:  # the painted strands' end
                break
            out2 = nrm2 if nrm2 @ (loc2 - hc) >= 0.0 else -nrm2
            q = loc2 - out2 * below
            # (never into the head)
            r = q - hc
            rl = np.linalg.norm(r)
            if rl < p_head_radius:
                q = hc + r / max(rl, 1e-9) * p_head_radius
            dq = np.linalg.norm(q - p)
            if dq < step * 0.2:
                break
            prev = (q - p) / dq
            length += dq
            pts.append(q)
            p = q
            cols.append(_texel(p_rgba, uv2)[:3])
        if len(pts) < 3 or length < 0.012:
            continue
        out.append(_resample(pts, points))
        cols = np.array(cols)
        m = max(1, len(cols) // 3)
        roots_col.append(cols[:m].mean(axis=0))
        tips_col.append(cols[-m:].mean(axis=0))
    root_c = tuple(float(x) for x in np.median(np.array(roots_col), axis=0)) if roots_col else (0.1, 0.07, 0.05)
    tip_c = tuple(float(x) for x in np.median(np.array(tips_col), axis=0)) if tips_col else (0.2, 0.14, 0.1)
    return out, root_c, tip_c


def write_json(p_path, p_guides, p_root, p_tip, p_head_center, p_head_radius, p_to_gltf):
    """Blender's Z-up to glTF's Y-up: (x, y, z) -> (x, z, -y)."""
    conv = (lambda p: [float(p[0]), float(p[2]), float(-p[1])]) if p_to_gltf else (lambda p: [float(x) for x in p])
    data = {
        "comment": "Strand guides traced from the person's MakeHuman hair (tools/fellow_characters/hair_guides.py, "
        "milestone 244): each guide's points root first, in the .glb's model space (metres, Y up); the hair's root and "
        "tip colours (sRGB) from its texture; the head's centre and radius (the strands' collider).",
        "points": len(p_guides[0]) if p_guides else 0,
        "guides": [[[round(c, 5) for c in conv(q)] for q in g] for g in p_guides],
        "root_color": [round(c, 4) for c in p_root],
        "tip_color": [round(c, 4) for c in p_tip],
        "head_center": [round(c, 5) for c in conv(p_head_center)],
        "head_radius": round(float(p_head_radius), 5),
    }
    with open(p_path, "w", encoding="utf-8") as f:
        json.dump(data, f, separators=(",", ":"))
