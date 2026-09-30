#!/usr/bin/env python3
"""A/B statistics for decision records: per scene/resolution, each variant vs. the reference build.

  python benchmarks/tools/ab_stats.py results/fellow-m1_perf.json [--ref stock] [--metric frame_ms|gpu_ms] [--md out.md]
  python benchmarks/tools/ab_stats.py results/fellow-m1_profile.json --passes "TAA" --ref stock

For every (scene, resolution, variant): mean of the per-run means, standard deviation across runs, coefficient of
variation, delta vs. the reference build (ms and %), Welch's t-test on the per-run means, and a verdict:
  "faster" / "slower"  : p < 0.05 and |delta| > 1 %   (a statistically clear difference that is also practically visible)
  "within noise"       : otherwise
  "noisy"              : either side has CV > 3 % (the runner's own threshold) - treat the delta with caution.
With --passes, reports per-pass GPU times from profile sessions instead (one profiled run per configuration: no t-test,
per-frame spread is shown instead).
"""

import argparse
import json
import math
import statistics
import sys


def welch_p(a, b):
    """Two-sided p-value of Welch's t-test (Student t CDF via the regularized incomplete beta function)."""
    if len(a) < 2 or len(b) < 2:
        return None
    ma, mb = statistics.fmean(a), statistics.fmean(b)
    va, vb = statistics.variance(a), statistics.variance(b)
    na, nb = len(a), len(b)
    se2 = va / na + vb / nb
    if se2 <= 0:
        return 0.0 if ma != mb else 1.0
    t = (ma - mb) / math.sqrt(se2)
    df = se2 ** 2 / ((va / na) ** 2 / (na - 1) + (vb / nb) ** 2 / (nb - 1))
    x = df / (df + t * t)
    return _betainc(df / 2.0, 0.5, x)


def _betainc(a, b, x):
    # Regularized incomplete beta I_x(a, b) by continued fraction (Numerical Recipes).
    if x <= 0:
        return 0.0
    if x >= 1:
        return 1.0
    lbeta = math.lgamma(a + b) - math.lgamma(a) - math.lgamma(b) + a * math.log(x) + b * math.log(1 - x)
    if x < (a + 1) / (a + b + 2):
        return math.exp(lbeta) * _cf(a, b, x) / a
    return 1.0 - math.exp(lbeta) * _cf(b, a, 1 - x) / b


def _cf(a, b, x, it=200, eps=3e-14):
    qab, qap, qam = a + b, a + 1, a - 1
    c, d = 1.0, 1 - qab * x / qap
    d = 1 / (d if abs(d) > 1e-300 else 1e-300)
    h = d
    for m in range(1, it + 1):
        m2 = 2 * m
        aa = m * (b - m) * x / ((qam + m2) * (a + m2))
        d = 1 + aa * d
        d = 1 / (d if abs(d) > 1e-300 else 1e-300)
        c = 1 + aa / c if abs(1 + aa / c) > 1e-300 else 1e-300
        h *= d * c
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2))
        d = 1 + aa * d
        d = 1 / (d if abs(d) > 1e-300 else 1e-300)
        c = 1 + aa / c if abs(1 + aa / c) > 1e-300 else 1e-300
        de = d * c
        h *= de
        if abs(de - 1) < eps:
            break
    return h


def run_means(entry, metric):
    out = []
    for r in entry.get("runs", []):
        s = r.get("summary") if isinstance(r, dict) else None
        if r.get("valid") and s and metric in s:
            out.append(s[metric]["mean"])
    return out


def perf_table(sess, ref, metric):
    res = sess["results"]
    keys = sorted({(e["scene"], e["resolution"]) for e in res.values()})
    builds = sorted({e["build"] for e in res.values()}, key=lambda b: (b != ref, b))
    lines = [f"| Scene | Res | Build | {metric} mean | std | CV | Δ vs {ref} | Δ % | p (Welch) | verdict | runs |",
             "|---|---|---|---:|---:|---:|---:|---:|---:|---|---:|"]
    rows = []
    for scene, resol in keys:
        base = res.get(f"{scene}|{resol}|{ref}")
        bm = run_means(base, metric) if base else []
        for b in builds:
            e = res.get(f"{scene}|{resol}|{b}")
            if not e:
                continue
            m = run_means(e, metric)
            if not m:
                lines.append(f"| {scene} | {resol} | {b} | – | – | – | – | – | – | {str(e.get('runs', [{}])[0].get('error', 'no valid run'))[:40]} | 0 |")
                continue
            mean = statistics.fmean(m)
            sd = statistics.stdev(m) if len(m) > 1 else 0.0
            cv = sd / mean if mean else 0.0
            if b == ref or not bm:
                lines.append(f"| {scene} | {resol} | {b} | {mean:.3f} | {sd:.3f} | {cv * 100:.1f}% | | | | reference | {len(m)} |")
                continue
            bmean = statistics.fmean(bm)
            bcv = (statistics.stdev(bm) / bmean) if len(bm) > 1 and bmean else 0.0
            d = mean - bmean
            dp = d / bmean * 100
            p = welch_p(m, bm)
            verdict = ("faster" if d < 0 else "slower") if (p is not None and p < 0.05 and abs(dp) > 1.0) else "within noise"
            if cv > 0.03 or bcv > 0.03:
                verdict += " (noisy)"
            rows.append({"scene": scene, "res": resol, "build": b, "delta_ms": d, "delta_pct": dp, "p": p, "verdict": verdict})
            lines.append(f"| {scene} | {resol} | {b} | {mean:.3f} | {sd:.3f} | {cv * 100:.1f}% | {d:+.3f} | {dp:+.1f}% | "
                         f"{'–' if p is None else f'{p:.3f}'} | {verdict} | {len(m)} |")
    return "\n".join(lines), rows


def pass_table(sess, ref, passes):
    res = sess["results"]
    keys = sorted({(e["scene"], e["resolution"]) for e in res.values()})
    builds = sorted({e["build"] for e in res.values()}, key=lambda b: (b != ref, b))
    lines = ["| Scene | Res | Pass | " + " | ".join(builds) + " | " + " | ".join(f"Δ {b}" for b in builds if b != ref) + " |",
             "|---|---|---|" + "---:|" * (2 * len(builds) - 1)]
    for scene, resol in keys:
        for p in passes:
            vals = {}
            for b in builds:
                e = res.get(f"{scene}|{resol}|{b}")
                g = (e or {}).get("aggregate", {}).get("gpu_passes_ms", {}).get(p)
                vals[b] = (g["mean"] if isinstance(g, dict) else g) if g is not None else None
            if all(v is None for v in vals.values()):
                continue
            cells = ["–" if vals[b] is None else f"{vals[b]:.3f}" for b in builds]
            deltas = []
            for b in builds:
                if b == ref:
                    continue
                if vals[b] is None or vals.get(ref) in (None, 0):
                    deltas.append("–")
                else:
                    deltas.append(f"{vals[b] - vals[ref]:+.3f} ({(vals[b] - vals[ref]) / vals[ref] * 100:+.0f}%)")
            lines.append(f"| {scene} | {resol} | {p} | " + " | ".join(cells) + " | " + " | ".join(deltas) + " |")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("session_json")
    ap.add_argument("--ref", default="stock")
    ap.add_argument("--metric", default="frame_ms")
    ap.add_argument("--passes", default=None, help="comma list of pass names (profile sessions)")
    ap.add_argument("--md", default=None)
    a = ap.parse_args()
    sess = json.load(open(a.session_json, encoding="utf-8"))
    if a.passes:
        out = pass_table(sess, a.ref, [p.strip() for p in a.passes.split(",")])
    else:
        out, _ = perf_table(sess, a.ref, a.metric)
    print(out)
    if a.md:
        with open(a.md, "w", encoding="utf-8") as f:
            f.write(out + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
