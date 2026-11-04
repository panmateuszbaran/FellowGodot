#!/usr/bin/env python3
"""Mean and standard deviation of a PNG's luma (0-255), tab-separated: a black or flat frame shows as a spread near
0. Reads 8-bit RGB/RGBA PNGs with the standard library only (zlib), so it runs on any machine with Python 3."""

import struct
import sys
import zlib


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, width, height, ctype = 8, b"", 0, 0, 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos : pos + 8])
        chunk = data[pos + 8 : pos + 8 + n]
        if kind == b"IHDR":
            width, height, depth, ctype = struct.unpack(">IIBB", chunk[:10])
            assert depth == 8 and ctype in (2, 6), "only 8-bit RGB/RGBA"
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + n
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * bpp
    rows, prev = [], bytearray(stride)
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return width, height, bpp, rows


def main():
    w, h, bpp, rows = read_png(sys.argv[1])
    total = total2 = 0.0
    for line in rows:
        for x in range(0, w * bpp, bpp):
            y = 0.2126 * line[x] + 0.7152 * line[x + 1] + 0.0722 * line[x + 2]
            total += y
            total2 += y * y
    n = w * h
    mean = total / n
    print("%.1f\t%.1f" % (mean, max(total2 / n - mean * mean, 0.0) ** 0.5))


if __name__ == "__main__":
    main()
