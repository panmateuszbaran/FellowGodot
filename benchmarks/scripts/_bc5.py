"""Lossless green-channel inversion of BC5 (ATI2) DDS normal maps (DirectX -> OpenGL convention).

BC5 stores two independent BC4 blocks (X then Y). Inverting a channel (v -> 255 - v) is done in the
compressed domain by swapping + inverting the two endpoints (which preserves the interpolation mode)
and remapping the 3-bit palette indices:
    8-value mode (e0 > e1): 0<->1, k -> 9-k for k = 2..7
    6-value mode (e0 <= e1): 0<->1, k -> 7-k for k = 2..5, 6<->7   (explicit 0 / 255 entries)
Decoded values are identical up to the +-1 rounding of the BC4 interpolation. Authored mipmaps are kept.
"""

import numpy as np

MAP8 = np.array([1, 0, 7, 6, 5, 4, 3, 2], dtype=np.uint64)
MAP6 = np.array([1, 0, 5, 4, 3, 2, 7, 6], dtype=np.uint64)


def _flip_bc4(blk: np.ndarray) -> None:
    """blk: (N, 8) uint8 view of BC4 blocks, modified in place."""
    e0 = blk[:, 0].copy()
    e1 = blk[:, 1].copy()
    mode8 = e0 > e1
    bits = np.zeros(blk.shape[0], dtype=np.uint64)
    for i in range(6):
        bits |= blk[:, 2 + i].astype(np.uint64) << np.uint64(8 * i)
    out = np.zeros_like(bits)
    for k in range(16):
        idx = (bits >> np.uint64(3 * k)) & np.uint64(7)
        new = np.where(mode8, MAP8[idx.astype(np.int64)], MAP6[idx.astype(np.int64)])
        out |= new << np.uint64(3 * k)
    blk[:, 0] = 255 - e1
    blk[:, 1] = 255 - e0
    for i in range(6):
        blk[:, 2 + i] = ((out >> np.uint64(8 * i)) & np.uint64(0xFF)).astype(np.uint8)


def flip_green_dds(src: str, dst: str) -> bool:
    with open(src, "rb") as f:
        data = bytearray(f.read())
    if data[:4] != b"DDS " or data[84:88] not in (b"ATI2", b"BC5U"):
        return False
    payload = np.frombuffer(memoryview(data)[128:], dtype=np.uint8)
    n = payload.size // 16
    blocks = payload[: n * 16].reshape(n, 16).copy()
    g = blocks[:, 8:16].copy()
    _flip_bc4(g)
    blocks[:, 8:16] = g
    data[128:128 + n * 16] = blocks.tobytes()
    with open(dst, "wb") as f:
        f.write(data)
    return True


def _decode_bc4(blk: np.ndarray) -> np.ndarray:
    e0 = blk[:, 0].astype(np.int32)
    e1 = blk[:, 1].astype(np.int32)
    pal = np.zeros((blk.shape[0], 8), dtype=np.int32)
    pal[:, 0] = e0
    pal[:, 1] = e1
    m8 = e0 > e1
    for k in range(2, 8):
        pal[:, k] = np.where(m8, ((8 - k) * e0 + (k - 1) * e1) // 7, 0)
    for k in range(2, 6):
        pal[:, k] = np.where(m8, pal[:, k], ((6 - k) * e0 + (k - 1) * e1) // 5)
    pal[:, 6] = np.where(m8, pal[:, 6], 0)
    pal[:, 7] = np.where(m8, pal[:, 7], 255)
    bits = np.zeros(blk.shape[0], dtype=np.uint64)
    for i in range(6):
        bits |= blk[:, 2 + i].astype(np.uint64) << np.uint64(8 * i)
    out = np.zeros((blk.shape[0], 16), dtype=np.int32)
    for k in range(16):
        idx = ((bits >> np.uint64(3 * k)) & np.uint64(7)).astype(np.int64)
        out[:, k] = pal[np.arange(blk.shape[0]), idx]
    return out


def self_test(seed: int = 1) -> int:
    rng = np.random.default_rng(seed)
    blk = rng.integers(0, 256, size=(20000, 8), dtype=np.uint8)
    before = _decode_bc4(blk)
    flipped = blk.copy()
    _flip_bc4(flipped)
    after = _decode_bc4(flipped)
    return int(np.abs((255 - before) - after).max())


if __name__ == "__main__":
    print("max abs error after flip:", self_test())
