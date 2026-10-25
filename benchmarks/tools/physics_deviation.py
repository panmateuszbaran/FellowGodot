#!/usr/bin/env python3
"""FellowEngine physics: how far a variant's Physics Lab state is from the reference's (for the fast mode, whose results
are not stock's). Reads a physics_bench.py session JSON whose runs were made with --extra "--physics_dump=30,600":
per engine, case and dumped tick, the bodies' position difference (RMS and largest, metres) and, in `rays`, the rays'
hit points and the shape casts' safe fractions.

  physics_deviation.py benchmarks/results/<session>.json [--ref stock]
"""
import argparse
import json
import math


def body_count(state, case):
    if case != "rays":
        return len(state) // 12
    return None


def compare(ref, var, case, n_bodies):
    out = {}
    nb = n_bodies if n_bodies is not None else len(ref) // 12
    d = []
    for b in range(nb):
        o = b * 12 + 9
        if o + 2 >= len(ref) or o + 2 >= len(var):
            break
        d.append(math.dist(ref[o:o + 3], var[o:o + 3]))
    if d:
        out["bodies"] = nb
        out["pos_rms_m"] = math.sqrt(sum(x * x for x in d) / len(d))
        out["pos_max_m"] = max(d)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("session")
    ap.add_argument("--ref", default=None)
    a = ap.parse_args()
    d = json.load(open(a.session))
    res = d["results"]
    ref = a.ref or d.get("reference")
    for key, e in sorted(res.items()):
        eng, case, var = key.split("|")
        if var == ref:
            continue
        r = res.get(f"{eng}|{case}|{ref}")
        if not r:
            continue
        rs = next((x.get("states") for x in r["runs"] if x.get("states")), None)
        vs = next((x.get("states") for x in e["runs"] if x.get("states")), None)
        if not rs or not vs:
            continue
        for tick in sorted(rs, key=int):
            if tick not in vs:
                continue
            A, B = rs[tick], vs[tick]
            line = f"{eng:5s} {case:6s} {var:8s} tick {tick:>4s}: "
            if case == "rays":
                # Rays: the rigs' transforms (12 floats each), then rig by rig 50 rays (3 floats: the hit point, inf
                # for none) and 5 shape casts (2 floats: the collision count and the safe fraction).
                pts, fr, hitdiff, cnt = [], [], 0, 0
                i = (len(A) // (12 + 50 * 3 + 5 * 2)) * 12
                while i < len(A) and i < len(B):
                    for _ in range(50):
                        p, q = A[i:i + 3], B[i:i + 3]
                        if any(math.isinf(x) for x in p) != any(math.isinf(x) for x in q):
                            hitdiff += 1
                        elif not any(math.isinf(x) for x in p):
                            pts.append(math.dist(p, q))
                        i += 3
                    for _ in range(5):
                        fr.append(abs(A[i + 1] - B[i + 1]))
                        cnt += A[i] != B[i]
                        i += 2
                if pts:
                    line += f"ray hit points max {max(pts):.2e} m ({hitdiff} rays hit/miss differently); "
                if fr:
                    line += f"shape cast safe fraction diff max {max(fr):.4f}, mean {sum(fr) / len(fr):.5f} ({cnt} of {len(fr)} collision counts differ)"
            else:
                c = compare(A, B, case, None)
                line += f"{c.get('bodies', 0)} bodies, position RMS {c.get('pos_rms_m', 0):.4f} m, max {c.get('pos_max_m', 0):.4f} m"
            print(line)


if __name__ == "__main__":
    main()
