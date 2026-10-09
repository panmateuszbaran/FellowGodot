"""Summarise FellowEngine pipeline statistics dumps (FELLOW_PIPELINE_STATS, VK_KHR_pipeline_executable_properties).

Each line of a dump: kind, shader name, SPIR-V hashes per stage, specialization constants, executable index,
executable name, stage flags, subgroup size, then the driver's statistics as name=value.

Usage: python pipeline_stats.py <dump.tsv> [--match SUBSTRING] [--stat NAME] [--top N] [--md OUT]
Prints one row per distinct (shader, executable, statistics) with the number of pipelines that share it, sorted by the
chosen statistic (default: the first one whose name mentions registers).
"""

import argparse
import collections
import sys


def parse(path):
    rows = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 8:
                continue
            kind, name, hashes, spec, idx, exe, stages, subgroup = parts[:8]
            stats = collections.OrderedDict()
            for kv in parts[8:]:
                if "=" in kv:
                    k, v = kv.split("=", 1)
                    stats[k] = v
            rows.append({"kind": kind, "name": name, "hashes": hashes, "spec": spec, "idx": idx, "exe": exe,
                         "stages": stages, "subgroup": subgroup, "stats": stats})
    return rows


def num(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return float("nan")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--match", default="")
    ap.add_argument("--stat", default="")
    ap.add_argument("--top", type=int, default=60)
    ap.add_argument("--md", default="")
    a = ap.parse_args()
    rows = [r for r in parse(a.dump) if a.match.lower() in (r["name"] + " " + r["exe"]).lower()]
    if not rows:
        print("no rows")
        return
    names = list(rows[0]["stats"].keys())
    stat = a.stat or next((n for n in names if "regist" in n.lower()), names[0] if names else "")
    groups = collections.OrderedDict()
    for r in rows:
        key = (r["kind"], r["name"], r["exe"], tuple(r["stats"].items()))
        g = groups.setdefault(key, {"n": 0, "specs": set(), "hashes": set()})
        g["n"] += 1
        g["specs"].add(r["spec"])
        g["hashes"].add(r["hashes"])
    items = sorted(groups.items(), key=lambda kv: -num(dict(kv[0][3]).get(stat)))
    out = []
    out.append(f"{len(rows)} executables in {sum(g['n'] for g in groups.values())} rows, {len(groups)} distinct; sorted by '{stat}'")
    out.append("statistics reported: " + ", ".join(names))
    out.append("")
    hdr = ["kind", "shader", "executable", "pipelines", "SPIR-V", *names]
    out.append("| " + " | ".join(hdr) + " |")
    out.append("|" + "---|" * len(hdr))
    for (kind, name, exe, stats), g in items[: a.top]:
        st = dict(stats)
        out.append("| " + " | ".join([kind, name, exe, str(g["n"]), str(len(g["hashes"])), *[st.get(n, "") for n in names]]) + " |")
    text = "\n".join(out)
    print(text)
    if a.md:
        with open(a.md, "w", encoding="utf-8") as f:
            f.write(text + "\n")


if __name__ == "__main__":
    sys.exit(main())
