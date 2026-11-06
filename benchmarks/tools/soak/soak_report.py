#!/usr/bin/env python3
"""A soak run's report (milestone 55): the bench's --mode=soak result read, summarised, charted.

    python benchmarks/tools/soak/soak_report.py <result .json> [--out DIR]

Writes <out>/soak.csv (one row a second), <out>/soak.png (frame time a second — mean, 99th percentile, max — with
the hitches marked and the weather as bands; memory below: static, video), <out>/soak_summary.json, and prints the
summary. Pillow draws the chart; without it there is no picture.
"""

import argparse
import csv
import json
import os
import sys

WEATHER_COLOURS = {"clear": (250, 236, 180), "cloudy": (225, 228, 232), "overcast": (200, 203, 210),
                   "rain": (180, 200, 225), "storm": (150, 160, 200), "snow": (235, 240, 250), "blizzard": (210, 220, 240)}


def chart(rows, hitches, path, title):
    try:
        from PIL import Image, ImageDraw  # noqa: PLC0415
    except ImportError:
        return False
    W, H, PAD = 1600, 900, 60
    top_h = 520
    im = Image.new("RGB", (W, H), (255, 255, 255))
    d = ImageDraw.Draw(im)
    t_max = max(r["t"] for r in rows) or 1.0

    def x_of(t):
        return PAD + (W - 2 * PAD) * t / t_max

    # Weather bands behind the frame times.
    prev, start = None, 0.0
    for r in rows + [{"t": t_max, "state": {"weather": None}}]:
        w = r.get("state", {}).get("weather")
        if w != prev:
            if prev is not None:
                d.rectangle([x_of(start), PAD, x_of(r["t"]), PAD + top_h], fill=WEATHER_COLOURS.get(prev, (240, 240, 240)))
                d.text((x_of(start) + 3, PAD + 3), str(prev), fill=(90, 90, 90))
            prev, start = w, r["t"]
    ms_top = max(max(r["p99_ms"] for r in rows) * 1.25, 1.0)
    ms_top = min(ms_top, max(r["mean_ms"] for r in rows) * 4.0)

    def y_ms(v):
        return PAD + top_h - top_h * min(v, ms_top) / ms_top

    for key, colour in (("max_ms", (230, 160, 160)), ("p99_ms", (200, 80, 60)), ("mean_ms", (30, 60, 160))):
        pts = [(x_of(r["t"]), y_ms(r[key])) for r in rows]
        d.line(pts, fill=colour, width=2 if key == "mean_ms" else 1)
    for h in hitches:
        x = x_of(h["t"])
        d.line([(x, PAD + top_h - 8), (x, PAD + top_h)], fill=(200, 0, 0), width=1)
    for v in (ms_top * k / 4 for k in range(5)):
        d.text((5, y_ms(v) - 6), f"{v:.1f}", fill=(0, 0, 0))
    d.text((PAD, 15), title + " - frame time a second (ms): mean (blue), 99th percentile (red), max (pink); hitches (ticks)", fill=(0, 0, 0))
    # Memory.
    m_top = PAD + top_h + 50
    m_h = H - m_top - 40
    mems = [("static_mb", (40, 120, 40)), ("video_mb", (120, 40, 160))]
    m_max = max(max(r[k] for r in rows) for k, _ in mems) * 1.1 or 1.0
    for key, colour in mems:
        pts = [(x_of(r["t"]), m_top + m_h - m_h * r[key] / m_max) for r in rows]
        d.line(pts, fill=colour, width=2)
    d.text((PAD, m_top - 18), f"memory (MiB, top {m_max:.0f}): static (green), video (purple)", fill=(0, 0, 0))
    for k in range(0, int(t_max) + 1, max(60, int(t_max / 12) // 60 * 60 or 60)):
        d.text((x_of(k) - 10, H - 25), f"{k // 60} min", fill=(0, 0, 0))
    im.save(path)
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("result")
    ap.add_argument("--out", default="")
    a = ap.parse_args()
    with open(a.result, encoding="utf-8") as f:
        res = json.load(f)
    soak = res.get("soak")
    if not soak or not soak.get("rows"):
        print(json.dumps({"ok": False, "error": "no soak rows in " + a.result}))
        return 2
    out = a.out or os.path.dirname(os.path.abspath(a.result))
    os.makedirs(out, exist_ok=True)
    rows = soak["rows"]
    keys = [k for k in rows[0] if k != "state"]
    with open(os.path.join(out, "soak.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(keys + ["weather", "daytime"])
        for r in rows:
            w.writerow([r[k] for k in keys] + [r.get("state", {}).get("weather", ""), r.get("state", {}).get("daytime", "")])
    summary = dict(soak["summary"])
    summary["scene"] = res.get("scene")
    summary["resolution"] = res.get("resolution")
    summary["adapter"] = res.get("adapter", {}).get("name")
    summary["engine"] = res.get("engine", {}).get("string") if isinstance(res.get("engine"), dict) else res.get("engine")
    summary["worst_hitches"] = soak.get("worst_hitches", [])[:10]
    with open(os.path.join(out, "soak_summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)
    title = f"{res.get('scene')} {res.get('resolution')} on {summary['adapter']}"
    summary["chart"] = os.path.join(out, "soak.png") if chart(rows, soak.get("hitches", soak.get("worst_hitches", [])), os.path.join(out, "soak.png"), title) else None
    print(json.dumps(summary, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
