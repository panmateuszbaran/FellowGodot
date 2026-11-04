#!/usr/bin/env python3
"""Writes the Streaming Lab's textures (FellowBench, milestone 45): VRAM-compressed CompressedTexture2D files (.ctex).

Each material gets an albedo map (BC1) and a normal map (BC5) of --size x --size texels with a full mip chain, the way
Godot's importer stores a VRAM-compressed 3D texture (DATA_FORMAT_IMAGE: the chain from mip 0 down, one blob). The
content is procedural and seeded: tiles with a hue per material, multi-octave value noise (detail at every mip level,
so a missing level shows) and the tiles' relief in the normal map. Deterministic: the same files on every machine.

    python3 benchmarks/scripts/make_streaming_textures.py --out benchmarks/project/external/streaming

The files are generated, never committed (benchmarks/project/external/ is ignored).
"""

import argparse
import colorsys
import os
import struct
import sys

import numpy as np

FORMAT_DXT1 = 17  # Image::FORMAT_DXT1 (BC1)
FORMAT_RGTC_RG = 21  # Image::FORMAT_RGTC_RG (BC5)
FORMAT_BIT_HAS_MIPMAPS = 1 << 23
DATA_FORMAT_IMAGE = 0


def value_noise(size, cells, rng):
    """Bilinear value noise of `cells` x `cells` random values over a size x size tile (wrapping)."""
    grid = rng.random((cells, cells), dtype=np.float32)
    coords = np.arange(size, dtype=np.float32) * (cells / size)
    i0 = np.floor(coords).astype(np.int32)
    f = coords - i0
    f = f * f * (3.0 - 2.0 * f)
    i0 %= cells
    i1 = (i0 + 1) % cells
    g00 = grid[np.ix_(i0, i0)]
    g01 = grid[np.ix_(i0, i1)]
    g10 = grid[np.ix_(i1, i0)]
    g11 = grid[np.ix_(i1, i1)]
    fy = f[:, None]
    fx = f[None, :]
    return (g00 * (1 - fx) + g01 * fx) * (1 - fy) + (g10 * (1 - fx) + g11 * fx) * fy


def fbm(size, rng, base_cells=4, octaves=8):
    acc = np.zeros((size, size), dtype=np.float32)
    amp = 1.0
    total = 0.0
    cells = base_cells
    for _ in range(octaves):
        if cells > size:
            break
        acc += amp * value_noise(size, cells, rng)
        total += amp
        amp *= 0.6
        cells *= 2
    return acc / total


def make_material(size, index, seed):
    rng = np.random.default_rng(seed * 1009 + index)
    hue = (index * 0.61803398875) % 1.0
    base = np.array(colorsys.hsv_to_rgb(hue, 0.45 + 0.3 * rng.random(), 0.55 + 0.3 * rng.random()), dtype=np.float32)
    tiles = int(rng.choice([4, 6, 8, 12]))
    y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size
    ty = np.floor(y * tiles)
    offset = (ty % 2) * 0.5
    tx = np.floor(x * tiles + offset)
    fy = y * tiles - ty
    fx = x * tiles + offset - tx
    mortar_w = 0.04
    edge = np.minimum(np.minimum(fx, 1 - fx), np.minimum(fy, 1 - fy))
    mortar = np.clip(1.0 - edge / mortar_w, 0.0, 1.0)
    tile_rand = (np.sin(tx * 12.9898 + ty * 78.233 + index) * 43758.5453) % 1.0
    detail = fbm(size, rng, base_cells=8, octaves=9)
    coarse = fbm(size, rng, base_cells=2, octaves=4)
    shade = 0.75 + 0.35 * tile_rand[..., None] * 0.6 + 0.5 * (detail[..., None] - 0.5) + 0.2 * (coarse[..., None] - 0.5)
    albedo = base[None, None, :] * shade
    albedo = albedo * (1.0 - 0.55 * mortar[..., None]) + 0.12 * mortar[..., None]
    albedo = np.clip(albedo, 0.0, 1.0)
    height = (1.0 - mortar) * 0.6 + 0.4 * detail
    gx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * (size / 256.0)
    gy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * (size / 256.0)
    n = np.stack([-gx, -gy, np.ones_like(gx)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    normal = n[..., :2] * 0.5 + 0.5
    return (albedo * 255.0 + 0.5).astype(np.uint8), (np.clip(normal, 0, 1) * 255.0 + 0.5).astype(np.uint8)


def downsample(img):
    h, w = img.shape[:2]
    if h == 1 and w == 1:
        return img
    nh, nw = max(1, h // 2), max(1, w // 2)
    a = img[: nh * 2 if h > 1 else 1, : nw * 2 if w > 1 else 1].astype(np.uint32)
    if h > 1:
        a = a[0::2] + a[1::2]
    else:
        a = a * 2
    if w > 1:
        a = a[:, 0::2] + a[:, 1::2]
    else:
        a = a * 2
    return ((a + 2) // 4).astype(np.uint8)


def to_blocks(img):
    """(H, W, C) -> (H/4 * W/4, 16, C), padding to multiples of 4 by edge replication."""
    h, w, c = img.shape
    ph, pw = (-h) % 4, (-w) % 4
    if ph or pw:
        img = np.pad(img, ((0, ph), (0, pw), (0, 0)), mode="edge")
        h, w = img.shape[:2]
    b = img.reshape(h // 4, 4, w // 4, 4, c).transpose(0, 2, 1, 3, 4).reshape(-1, 16, c)
    return b


def encode_bc1(img):
    blocks = to_blocks(img[..., :3]).astype(np.float32)
    cmax = blocks.max(axis=1)
    cmin = blocks.min(axis=1)

    def to565(c):
        r = np.clip(np.round(c[:, 0] * 31 / 255), 0, 31).astype(np.uint32)
        g = np.clip(np.round(c[:, 1] * 63 / 255), 0, 63).astype(np.uint32)
        b = np.clip(np.round(c[:, 2] * 31 / 255), 0, 31).astype(np.uint32)
        return (r << 11) | (g << 5) | b

    def from565(v):
        r = ((v >> 11) & 31).astype(np.float32) * 255 / 31
        g = ((v >> 5) & 63).astype(np.float32) * 255 / 63
        b = (v & 31).astype(np.float32) * 255 / 31
        return np.stack([r, g, b], axis=-1)

    c0 = to565(cmax)
    c1 = to565(cmin)
    swap = c0 < c1
    c0s = np.where(swap, c1, c0)
    c1s = np.where(swap, c0, c1)
    e0 = from565(c0s)
    e1 = from565(c1s)
    d = e1 - e0
    dd = (d * d).sum(axis=1)
    t = ((blocks - e0[:, None, :]) * d[:, None, :]).sum(axis=2) / np.maximum(dd[:, None], 1e-6)
    q = np.clip(np.round(t * 3), 0, 3).astype(np.uint32)
    code = np.array([0, 2, 3, 1], dtype=np.uint32)[q]
    equal = c0s == c1s
    code[equal] = 0
    shifts = (np.arange(16, dtype=np.uint32) * 2)[None, :]
    indices = (code << shifts).sum(axis=1).astype(np.uint32)
    out = np.zeros((blocks.shape[0], 2), dtype=np.uint32)
    out[:, 0] = c0s | (c1s << 16)
    out[:, 1] = indices
    return out.astype("<u4").tobytes()


def encode_bc4(channel_blocks):
    v = channel_blocks.astype(np.float32)  # (N, 16)
    r0 = v.max(axis=1)
    r1 = v.min(axis=1)
    span = np.maximum(r0 - r1, 1e-6)
    t = (r0[:, None] - v) / span[:, None]
    q = np.clip(np.round(t * 7), 0, 7).astype(np.uint64)
    code = np.where(q == 0, 0, np.where(q == 7, 1, q + 1)).astype(np.uint64)
    code[(r0 - r1) < 0.5] = 0
    shifts = (np.arange(16, dtype=np.uint64) * 3)[None, :]
    bits = (code << shifts).sum(axis=1).astype(np.uint64)
    out = np.zeros((v.shape[0], 8), dtype=np.uint8)
    out[:, 0] = r0.astype(np.uint8)
    out[:, 1] = r1.astype(np.uint8)
    for k in range(6):
        out[:, 2 + k] = ((bits >> np.uint64(8 * k)) & np.uint64(0xFF)).astype(np.uint8)
    return out


def encode_bc5(img):
    blocks = to_blocks(img[..., :2])
    r = encode_bc4(blocks[:, :, 0])
    g = encode_bc4(blocks[:, :, 1])
    return np.concatenate([r, g], axis=1).tobytes()


def write_ctex(path, levels, fmt, encode):
    w = levels[0].shape[1]
    h = levels[0].shape[0]
    data = b"".join(encode(level) for level in levels)
    with open(path, "wb") as f:
        f.write(b"GST2")
        f.write(struct.pack("<IIIIIIII", 1, w, h, FORMAT_BIT_HAS_MIPMAPS, 0xFFFFFFFF, 0, 0, 0))
        f.write(struct.pack("<IHHII", DATA_FORMAT_IMAGE, w, h, len(levels) - 1, fmt))
        f.write(data)


def mip_chain(img):
    levels = [img]
    while levels[-1].shape[0] > 1 or levels[-1].shape[1] > 1:
        levels.append(downsample(levels[-1]))
    return levels


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--materials", type=int, default=48)
    ap.add_argument("--size", type=int, default=2048)
    ap.add_argument("--seed", type=int, default=45)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for i in range(a.materials):
        albedo, normal = make_material(a.size, i, a.seed)
        write_ctex(os.path.join(a.out, "m%02d_albedo.ctex" % i), mip_chain(albedo), FORMAT_DXT1, encode_bc1)
        write_ctex(os.path.join(a.out, "m%02d_normal.ctex" % i), mip_chain(normal), FORMAT_RGTC_RG, encode_bc5)
        print("material %d/%d" % (i + 1, a.materials), flush=True)
    with open(os.path.join(a.out, "SOURCE.txt"), "w") as f:
        f.write("Generated by benchmarks/scripts/make_streaming_textures.py --materials %d --size %d --seed %d\n" % (a.materials, a.size, a.seed))
    return 0


if __name__ == "__main__":
    sys.exit(main())
