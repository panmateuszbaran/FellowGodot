#!/usr/bin/env python3
"""Milestone 243: the people's skin maps by region (decision 0385).

MakeHuman's skins are one colour texture each (no roughness, no specular), so the whole body shone alike: lips like
cheeks, the nose like the back. MPFB's region masks (CC0, mpfb2/src/mpfb/data/textures/mpfb_*.jpg) are in the base
mesh's default UV layout, which every MakeHuman skin shares, so one set of maps serves every person:

* skin_roughness.png (R): the body 0.62, the face 0.52, the T-zone (forehead and nose) 0.42, the eyelids 0.42, the
  lips 0.34, the nails 0.25; a little noise over metres-scale patches (no two regions flat).
* skin_oil.png: the second, sharper specular lobe drawn with BaseMaterial3D's clearcoat (R: its strength, G: the
  factor of its roughness) — skin's oily film: the T-zone and the lips most, the face some, the body barely.
* skin_transmittance.png (R): where light shows through from behind (the ears, a little the nose's wings).
* eye_occlusion.png (with --eyes): the eyeballs darkened towards the lids (the high-poly eyes' UVs).
* hair_<name>_flow.png (with --hair, --hair_names; milestone 244): each hairstyle's flow map — the direction across
  its painted strands, for the hair's anisotropic highlights (which run across the strands).

    python3 make_skin_maps.py --textures <mpfb2/src/mpfb/data/textures> --out showcase/people [--size 1024] \
        [--eyes <assets>/eyes/high-poly/high-poly.obj]

Pillow and numpy.
"""

import argparse
import os

import numpy as np
from PIL import Image, ImageFilter


def mask(p_dir, p_name, p_size):
    im = Image.open(os.path.join(p_dir, "mpfb_%s.jpg" % p_name)).convert("L").resize((p_size, p_size), Image.BILINEAR)
    return np.asarray(im).astype(np.float32) / 255.0


def smooth_noise(p_size, p_cells, p_seed):
    """Value noise: a p_cells² grid of random values, smoothly enlarged (0-1)."""
    rng = np.random.default_rng(p_seed)
    small = Image.fromarray((rng.random((p_cells, p_cells)) * 255).astype(np.uint8))
    return np.asarray(small.resize((p_size, p_size), Image.BICUBIC)).astype(np.float32) / 255.0


def eye_occlusion(p_obj, p_size):
    """The eyes' occlusion (R, 1 = open) in the high-poly eyes' UVs: each eyeball's front is a disc in them (its centre
    where the eyeball points, its rim 90 degrees away); the lids hide it from about half its radius, and shadow the sclera
    towards them — the upper lid more (the brow and the lashes above). Turning eyes take it with them (the lids do not
    turn): it stays near the lids for the eyes' range (about 25 degrees)."""
    verts, uvs, corners, faces = [], [], [], []
    for line in open(p_obj):
        if line.startswith("v "):
            verts.append([float(x) for x in line.split()[1:4]])
        elif line.startswith("vt "):
            uvs.append([float(x) for x in line.split()[1:3]])
        elif line.startswith("f "):
            face = []
            for t in line.split()[1:]:
                vi, ti = t.split("/")[:2]
                face.append((int(vi) - 1, int(ti) - 1))
            faces.append(face)
            corners += face
    verts = np.array(verts)
    uvs = np.array(uvs)
    # The UV islands (the cornea has its own, small one): the eyeball's is the largest of each eye.
    parent = list(range(len(uvs)))

    def find(p_i):
        while parent[p_i] != p_i:
            parent[p_i] = parent[parent[p_i]]
            p_i = parent[p_i]
        return p_i

    for face in faces:
        for _, ti in face[1:]:
            a, b = find(face[0][1]), find(ti)
            if a != b:
                parent[b] = a
    island = np.array([find(i) for i in range(len(uvs))])
    out = np.ones((p_size, p_size), np.float32)
    tex = Image.open(os.path.join(os.path.dirname(os.path.dirname(p_obj)), "materials", "brown_eye.png")).convert("L")
    tex = np.asarray(tex.resize((256, 256), Image.BILINEAR)).astype(np.float32) / 255.0
    tyy, txx = np.mgrid[0:256, 0:256].astype(np.float32)
    tu = (txx + 0.5) / 256
    tv = 1.0 - (tyy + 0.5) / 256
    tex_dark = 1.0 - tex
    yy, xx = np.mgrid[0:p_size, 0:p_size].astype(np.float32)
    u = (xx + 0.5) / p_size
    v = 1.0 - (yy + 0.5) / p_size
    for side in (-1, 1):
        cs = [(vi, ti) for vi, ti in corners if np.sign(verts[vi][0]) == side]
        vi = np.array([c[0] for c in cs])
        ti = np.array([c[1] for c in cs])
        ids, counts = np.unique(island[ti], return_counts=True)
        keep = island[ti] == ids[np.argmax(counts)]
        vi, ti = vi[keep], ti[keep]
        # The pupil: the island's darkest texels' centre in the pack's iris texture (every colour shares the layout).
        uv_i = uvs[ti]
        lo, hi = uv_i.min(axis=0), uv_i.max(axis=0)
        sel = (tex_dark >= 0.0) & (tu >= lo[0]) & (tu <= hi[0]) & (tv >= lo[1]) & (tv <= hi[1])
        w = np.where(sel, tex_dark, 0.0) ** 8
        centre = np.array([(w * tu).sum() / w.sum(), (w * tv).sum() / w.sum()])
        radius = np.max(np.linalg.norm(uv_i - centre, axis=1))
        # "Up" on the disc: the direction in which the vertices rise (a plane fitted to their height over their UVs).
        A = np.c_[uv_i, np.ones(len(uv_i))]
        coef = np.linalg.lstsq(A, verts[vi, 1], rcond=None)[0][:2]
        up_dir = coef / max(np.linalg.norm(coef), 1e-9)
        r = np.sqrt((u - centre[0]) ** 2 + (v - centre[1]) ** 2) / radius
        up = ((u - centre[0]) * up_dir[0] + (v - centre[1]) * up_dir[1]) / radius
        inside = r < 1.0
        # (in the iris' radii: the iris is about 0.37 of the disc's; the lids open about 2.3 radii sideways, 1.3 up)
        ri = 0.37
        edge = np.clip((r - 1.3 * ri) / (1.1 * ri), 0.0, 1.0)
        edge = edge * edge * (3 - 2 * edge)
        upper = np.clip((up - 0.6 * ri) / (0.7 * ri), 0.0, 1.0)
        dark = np.clip(0.5 * edge + 0.35 * upper, 0.0, 0.75)
        out = np.where(inside, np.minimum(out, 1.0 - dark), out)
    return out


def hair_flow(p_png, p_size=512, p_flip_v=False):
    """Milestone 244: a hairstyle's flow map for BaseMaterial3D's anisotropy — at every texel the direction across the
    painted strands (hair_guides.flow_field: the texture's structure tensor) in the texture's own axes (R along u, G
    along v as the image's rows go: Godot's tangent and binormal), A how sure it is (the anisotropy's strength), the
    hair's alpha counted. A line's direction: its sign does not matter to the highlight."""
    import sys

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import hair_guides

    rgba = np.asarray(Image.open(p_png).convert("RGBA")).astype(np.float32) / 255.0
    flow, sure = hair_guides.flow_field(rgba, 4.0)
    sx = flow[..., 0]
    sy = -flow[..., 1]  # (UV's v up to the image's rows down)
    if p_flip_v:
        sy = -sy
    # Across the strands: Godot's anisotropic GGX is rough along its tangent and smooth across it, and a hair's
    # highlight runs across its strands (each strand a cylinder: smooth along it, every normal round it).
    fx, fy = -sy, sx
    strength = np.sqrt(sure) * np.clip(rgba[..., 3] * 1.5, 0.0, 1.0)
    out = np.stack([fx * 0.5 + 0.5, fy * 0.5 + 0.5, np.zeros_like(fx), strength], axis=-1)
    im = Image.fromarray((np.clip(out, 0.0, 1.0) * 255 + 0.5).astype(np.uint8), "RGBA")
    return im.resize((p_size, p_size), Image.BILINEAR)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--textures", required=True, help="MPFB's data/textures (the mpfb_*.jpg region masks)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", type=int, default=1024)
    ap.add_argument("--eyes", default="", help="the pack's eyes/high-poly/high-poly.obj: also eye_occlusion.png")
    ap.add_argument("--hair", default="", help="milestone 244: the pack's hair/ folder; with --hair_names, each one's "
                    "hair_<name>_flow.png")
    ap.add_argument("--hair_names", default="", help="comma-separated hairstyles (the people's)")
    ap.add_argument("--flip_v", action="store_true", help="the flow maps' G mirrored (a test of the tangent frame)")
    a = ap.parse_args()
    n = a.size
    face = mask(a.textures, "face", n)
    lips = mask(a.textures, "lips", n)
    lids = mask(a.textures, "eyelids", n)
    ears = mask(a.textures, "ears", n)
    nails = np.maximum(mask(a.textures, "fingernails", n), mask(a.textures, "toenails", n))

    # The T-zone in the face's layout (the head's unwrap lies on its side: the forehead towards smaller u, the chin
    # towards larger u, the midline at the eyes' mean v). The eyes' and the lips' centres place it.
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32) / n
    ly, lx = np.nonzero(lids > 0.5)
    my, mx = np.nonzero(lips > 0.5)
    mid_v = ly.mean() / n
    eyes_u = lx.mean() / n
    mouth_u = mx.mean() / n
    span = mouth_u - eyes_u  # the eyes to the mouth
    # The forehead: a band above the brows (about the eyes-to-mouth span up), as wide as the eyes' spacing; the nose:
    # the midline from between the eyes to above the mouth.
    above = (eyes_u - xx) / span
    forehead = np.clip(above / 0.15, 0.0, 1.0) * np.clip((1.3 - above) / 0.4, 0.0, 1.0) * np.exp(-(((yy - mid_v) / 0.05) ** 2))
    nose = ((xx >= eyes_u - 0.1 * span) & (xx < mouth_u - 0.3 * span)) * np.exp(-(((yy - mid_v) / 0.022) ** 2))
    tzone = np.clip(np.maximum(forehead, nose) * face, 0.0, 1.0)
    tzone = np.asarray(Image.fromarray((tzone * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(n / 256))).astype(np.float32) / 255.0

    noise = smooth_noise(n, 48, 7) - 0.5
    rough = np.full((n, n), 0.62, np.float32)
    rough = rough * (1 - face) + 0.52 * face
    rough = rough * (1 - tzone) + 0.42 * tzone
    rough = rough * (1 - lids) + 0.42 * lids
    rough = rough * (1 - lips) + 0.34 * lips
    rough = rough * (1 - nails) + 0.25 * nails
    rough = np.clip(rough + noise * 0.06, 0.0, 1.0)

    oil = 0.05 + 0.12 * face + 0.45 * tzone + 0.5 * lips + 0.3 * nails
    oil = np.clip(oil + noise * 0.08 * face, 0.0, 1.0)
    # (BaseMaterial3D multiplies clearcoat_roughness by G: lower is glossier)
    oil_rough = np.clip(1.0 - 0.4 * tzone - 0.45 * lips - 0.5 * nails, 0.0, 1.0)

    nose_wings = ((xx >= eyes_u + 0.45 * span) & (xx < mouth_u - 0.3 * span)) * np.exp(-(((yy - mid_v) / 0.02) ** 2)) * face
    trans = np.clip(np.maximum(ears, 0.35 * nose_wings), 0.0, 1.0)

    os.makedirs(a.out, exist_ok=True)
    Image.fromarray((rough * 255 + 0.5).astype(np.uint8)).save(os.path.join(a.out, "skin_roughness.png"), optimize=True)
    rg = np.stack([oil, oil_rough, np.zeros_like(oil)], axis=-1)
    Image.fromarray((rg * 255 + 0.5).astype(np.uint8)).save(os.path.join(a.out, "skin_oil.png"), optimize=True)
    Image.fromarray((trans * 255 + 0.5).astype(np.uint8)).save(os.path.join(a.out, "skin_transmittance.png"), optimize=True)
    if a.eyes:
        occ = eye_occlusion(a.eyes, 512)
        Image.fromarray((occ * 255 + 0.5).astype(np.uint8)).save(os.path.join(a.out, "eye_occlusion.png"), optimize=True)
    for name in [h for h in a.hair_names.split(",") if h]:
        import glob

        found = sorted(glob.glob(os.path.join(a.hair, name, "*diffuse*.png")))
        if found:
            hair_flow(found[0], 512, a.flip_v).save(os.path.join(a.out, "hair_%s_flow.png" % name), optimize=True)
    print("make_skin_maps: %d² — eyes at u %.3f, mouth at u %.3f, midline v %.3f" % (n, eyes_u, mouth_u, mid_v))


if __name__ == "__main__":
    main()
