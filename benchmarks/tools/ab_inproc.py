#!/usr/bin/env python3
"""Results of in-process A/B runs (perf mode with --ab=..., see main.gd).

  python benchmarks/tools/ab_inproc.py benchmarks/results/<session>.json [--ref name] [--md out.md]

Each run alternates the configurations in blocks of frames along the camera path (rotating per pass), so every
configuration runs every block equally often within one process. Blocks are paired by path position and repeat
(block b, k-th time) between a configuration and the reference; the table gives each configuration's mean, the mean
paired difference, its t statistic over the block pairs, and the per-run differences.
"""
import argparse
import json
import math
import statistics

METRICS = ["frame_ms", "render_cpu_ms", "gpu_ms", "setup_cpu_ms"]


def blocks_by_position(ab, name):
    n = len(ab["configs"])
    out = {}
    for key, v in ab["blocks"].get(name, {}).items():
        p, b = (int(x) for x in key.split(":"))
        out[(b, p // n)] = v
    return out


def t_p(diffs):
    n = len(diffs)
    if n < 3:
        return float("nan"), float("nan")
    m = statistics.fmean(diffs)
    sd = statistics.stdev(diffs)
    if sd == 0:
        return float("inf"), 0.0
    t = m / (sd / math.sqrt(n))
    # Two-sided p from the normal approximation (n is in the hundreds).
    p = math.erfc(abs(t) / math.sqrt(2.0))
    return t, p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("session_json")
    ap.add_argument("--ref", default=None)
    ap.add_argument("--md", default=None)
    args = ap.parse_args()
    with open(args.session_json, encoding="utf-8") as f:
        d = json.load(f)
    lines = []
    for key, res in d["results"].items():
        runs = [r for r in res.get("runs", []) if r.get("ab")]
        if not runs:
            continue
        names = [c["name"] for c in runs[0]["ab"]["configs"]]
        ref = args.ref or names[0]
        lines.append(f"#### {key} — in-process A/B, {len(runs)} run(s), blocks of {runs[0]['ab']['block']} frames "
                     f"({runs[0]['ab']['skip']} skipped), reference `{ref}`")
        lines.append("")
        for metric in METRICS:
            lines.append(f"**{metric}**")
            lines.append("")
            lines.append("| config | mean ms | Δ ms (paired) | Δ % | t | p | block pairs | per-run Δ % |")
            lines.append("|---|---:|---:|---:|---:|---:|---:|---|")
            for name in names:
                vals, diffs, per_run = [], [], []
                for r in runs:
                    a = blocks_by_position(r["ab"], name)
                    b = blocks_by_position(r["ab"], ref)
                    common = sorted(set(a) & set(b))
                    rd = [a[k][metric] - b[k][metric] for k in common]
                    diffs += rd
                    vals += [a[k][metric] for k in a]
                    rm = statistics.fmean(b[k][metric] for k in common) if common else float("nan")
                    per_run.append(100.0 * statistics.fmean(rd) / rm if rd else float("nan"))
                mean = statistics.fmean(vals) if vals else float("nan")
                if name == ref:
                    lines.append(f"| {name} | {mean:.3f} | | | | | | |")
                    continue
                md = statistics.fmean(diffs) if diffs else float("nan")
                refmean = mean - md
                t, p = t_p(diffs)
                lines.append(f"| {name} | {mean:.3f} | {md:+.3f} | {100.0 * md / refmean:+.1f} % | {t:.1f} | {p:.3g} | "
                             f"{len(diffs)} | {', '.join(f'{x:+.1f}' for x in per_run)} |")
            lines.append("")
    text = "\n".join(lines)
    print(text)
    if args.md:
        with open(args.md, "w", encoding="utf-8") as f:
            f.write(text + "\n")


if __name__ == "__main__":
    main()
