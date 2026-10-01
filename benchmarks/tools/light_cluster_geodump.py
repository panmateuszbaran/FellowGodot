"""How many positional lights each shaded pixel iterates in the Forward+ light loop, from the engine's own geometry
dump (bench/main.gd --mode=geodump, which also records the visible omni and spot lights): stock's clustering vs a
depth-binned variant, against the lights that actually reach the pixel.

Stock (cluster_builder_rd.cpp, cluster_render.glsl, scene_forward_clustered.glsl): screen tiles of 32 px; per tile,
every light whose volume covers the tile; per pixel, of those, the lights whose depth range in that tile overlaps the
pixel's depth slice, with 32 slices linear in view depth over [0, camera far] (125 m each with Godot's default far of
4000 m). Modelled here with the light's bounding sphere: tile test = sphere against the tile's four side planes, depth
range = [center depth - radius, center depth + radius] (the rasterised per-tile range is at most this).

Variant "log": the same tiles, 32 slices logarithmic in view depth over [near, far], depth range of each light from
its bounding sphere (conservative by construction).

"reach": lights whose sphere contains the pixel's surface point (attenuation > 0 for omni lights; an upper bound for
spot lights, whose cone is not modelled).

Pixel depths come from rasterising the opaque and alpha-scissor geometry (occlusion_raster.c) at the given resolution
(960x540 by default: 16 px tiles there = 32 px tiles at 1920x1080). Sky pixels are excluded (the light loop does not
run there).

Usage: python light_cluster_geodump.py <geodump json> <occlusion_raster binary> <work dir> [out.json] [width height]
"""

import json
import math
import os
import subprocess
import sys

import numpy as np

ASPECT = 16.0 / 9.0
BLEND = (1, 4)
SLICES = 32


def view_matrices(origin, forward, up, fov_deg, near, far):
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
    return v, p


def main():
    jpath, raster, work = sys.argv[1:4]
    out = sys.argv[4] if len(sys.argv) > 4 else None
    W, H = (int(sys.argv[5]), int(sys.argv[6])) if len(sys.argv) > 6 else (960, 540)
    tile = max(1, round(int(os.environ.get("TILE_PX_1080", "32")) * W / 1920))
    os.makedirs(work, exist_ok=True)
    res = json.load(open(jpath))
    g = res["geodump"]
    lights = g.get("lights", [])
    near, far = float(g["near"]), float(g["far"])
    print("scene", res["scene"], "lights", len(lights), "(omni %d, spot %d, shadowed %d)" % (sum(l["type"] == "omni" for l in lights), sum(l["type"] == "spot" for l in lights), sum(l["shadow"] for l in lights)), "near", near, "far", far, flush=True)
    if not lights:
        return
    stem = os.path.join(os.path.dirname(jpath), "%s_geodump" % res["scene"])
    pos = np.fromfile(stem + "_pos.f32", dtype=np.float32).reshape(-1, 3)
    idx = np.fromfile(stem + "_idx.u32", dtype=np.int32).reshape(-1, 3).astype(np.int64)
    occ = []
    for s in g["surfaces"]:
        if s["transparency"] in BLEND or s["blend"]:
            continue
        t0 = s["first_index"] // 3
        occ.append(idx[t0:t0 + s["tris"]] + s["base_vertex"])
    pos.tofile(os.path.join(work, "pos.f32"))
    np.concatenate(occ).astype(np.uint32).tofile(os.path.join(work, "idx.u32"))
    cams = []
    for c in g["flythrough"]:
        cams.append(("fly%.1f" % c["t"],) + view_matrices(c["origin"], c["forward"], c["up"], c["fov"], near, far))
    for name, c in sorted(g["fixed_cameras"].items()):
        p, t = np.array(c["pos"]), np.array(c["target"])
        f = t - p
        up = [0.0, 1.0, 0.0] if abs(f[1]) < 0.999 * np.linalg.norm(f) else [0.0, 0.0, -1.0]
        cams.append((name,) + view_matrices(p, f, up, c["fov"], near, far))
    np.array([pm @ vm for _, vm, pm in cams], dtype=np.float32).tofile(os.path.join(work, "cams.f32"))
    dpath = os.path.join(work, "depth.f32")
    subprocess.check_call([raster, os.path.join(work, "pos.f32"), os.path.join(work, "idx.u32"), os.path.join(work, "cams.f32"), str(W), str(H), str(near), dpath])
    depth = np.fromfile(dpath, dtype=np.float32).reshape(len(cams), H, W)

    lc = np.array([l["position"] for l in lights], dtype=np.float64)
    lr = np.array([l["range"] for l in lights], dtype=np.float64)
    shadowed = np.array([l["shadow"] for l in lights])
    # Pixel rays in view space (y up): x_v = ndc_x * w / P00, y_v = ndc_y * w / P11, z_v = -w.
    px = (np.arange(W) + 0.5) / W * 2.0 - 1.0
    py = 1.0 - (np.arange(H) + 0.5) / H * 2.0
    tx, ty = (W + tile - 1) // tile, (H + tile - 1) // tile
    tile_of = (np.arange(H)[:, None] // tile) * tx + (np.arange(W)[None, :] // tile)
    log_scale = SLICES / math.log2(far / near)

    def lin_slice(z):
        return np.clip(np.floor(z / far * SLICES), 0, SLICES - 1).astype(np.int64)

    def log_slice(z):
        return np.clip(np.floor(np.log2(np.maximum(z, near) / near) * log_scale), 0, SLICES - 1).astype(np.int64)

    report = {"scene": res["scene"], "lights": len(lights), "width": W, "height": H, "tile_px": tile, "cameras": []}
    for ci, (name, vm, pm) in enumerate(cams):
        d = depth[ci]
        geo = np.isfinite(d)
        npx = int(geo.sum())
        if npx == 0:
            continue
        # Light spheres in view space.
        cv = (vm[:3, :3] @ lc.T).T + vm[:3, 3]
        cdepth = -cv[:, 2]
        # Tile test: sphere against the four side planes of each tile's view volume (planes through the eye).
        x0 = (np.arange(tx) * tile) / W * 2.0 - 1.0
        x1 = np.minimum((np.arange(tx) + 1) * tile, W) / W * 2.0 - 1.0
        y1 = 1.0 - (np.arange(ty) * tile) / H * 2.0
        y0 = 1.0 - np.minimum((np.arange(ty) + 1) * tile, H) / H * 2.0
        a, b = pm[0, 0], pm[1, 1]

        def side(ndc, k, sign):
            # Plane through the eye containing the view-space lines x_v = ndc * w / k (or y_v), normal pointing out.
            # x_v * k - ndc * w = 0 with w = -z_v -> normal (k, 0, ndc) (times sign).
            n = np.zeros((len(ndc), 3))
            n[:, 0 if sign[0] == "x" else 1] = k * sign[1]
            n[:, 2] = ndc * sign[1]
            return n / np.linalg.norm(n, axis=1, keepdims=True)

        nl = side(x0, a, ("x", -1.0))  # left: outside when x_v * a < x0 * w
        nr = side(x1, a, ("x", 1.0))
        nb = side(y0, b, ("y", -1.0))
        nt = side(y1, b, ("y", 1.0))
        out_x = (nl @ cv.T > lr[None, :]) | (nr @ cv.T > lr[None, :])  # [tx, L]
        out_y = (nb @ cv.T > lr[None, :]) | (nt @ cv.T > lr[None, :])  # [ty, L]
        in_front = cdepth + lr > near
        tile_ok = (~out_y[:, None, :] & ~out_x[None, :, :]).reshape(ty * tx, -1) & in_front[None, :]  # [tiles, L]
        # Pixel surface points in view space.
        w = np.where(geo, d, 0.0).astype(np.float64)
        pv = np.stack([px[None, :] * w / a, py[:, None] * w / b, -w], axis=-1)
        zlin = lin_slice(w)
        zlog = log_slice(w)
        lo, hi = np.maximum(cdepth - lr, 0.0), cdepth + lr
        lin_lo, lin_hi = lin_slice(lo), lin_slice(hi)
        lin_lo[cdepth - lr < near] = 0
        log_lo, log_hi = log_slice(lo), log_slice(hi)
        log_lo[cdepth - lr < near] = 0
        cnt = {"stock": 0, "log": 0, "reach": 0, "stock_shadowed": 0, "log_shadowed": 0, "reach_shadowed": 0, "tile": 0}
        for li in range(len(lights)):
            t_ok = tile_ok[tile_of, li] & geo
            if not t_ok.any():
                continue
            s_ok = t_ok & (zlin >= lin_lo[li]) & (zlin <= lin_hi[li])
            g_ok = t_ok & (zlog >= log_lo[li]) & (zlog <= log_hi[li])
            r_ok = geo & (np.sum((pv - cv[li]) ** 2, axis=-1) < lr[li] ** 2)
            cnt["tile"] += int(t_ok.sum())
            cnt["stock"] += int(s_ok.sum())
            cnt["log"] += int(g_ok.sum())
            cnt["reach"] += int(r_ok.sum())
            if shadowed[li]:
                cnt["stock_shadowed"] += int(s_ok.sum())
                cnt["log_shadowed"] += int(g_ok.sum())
                cnt["reach_shadowed"] += int(r_ok.sum())
            if (r_ok & ~g_ok).any():
                print("  WARNING: reaching light outside the log candidate set:", name, li, int((r_ok & ~g_ok).sum()), flush=True)
        r = {"camera": name, "pixels": npx}
        r.update({k: v / npx for k, v in cnt.items()})
        report["cameras"].append(r)
        if ci % 10 == 0 or not name.startswith("fly"):
            print("%-24s px %6d  per-pixel lights: stock %6.2f  log %6.2f  reach %6.2f  (shadowed: %5.2f / %5.2f / %5.2f)" % (name, npx, r["stock"], r["log"], r["reach"], r["stock_shadowed"], r["log_shadowed"], r["reach_shadowed"]), flush=True)
    fly = [r for r in report["cameras"] if r["camera"].startswith("fly")]
    report["fly_mean"] = {k: float(np.mean([r[k] for r in fly])) for k in ("stock", "log", "reach", "stock_shadowed", "log_shadowed", "reach_shadowed", "tile")}
    print("fly mean:", json.dumps({k: round(v, 3) for k, v in report["fly_mean"].items()}), flush=True)
    if out:
        json.dump(report, open(out, "w"), indent=1)


if __name__ == "__main__":
    main()
