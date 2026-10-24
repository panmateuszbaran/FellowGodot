#!/usr/bin/env python3
"""FellowEngine physics benchmark (milestone 17): Physics Lab, headless, one physics tick per frame.

Runs each build variant on each engine (GodotPhysics3D, Jolt) and case, `--repeats` times in alternating order, and
reports the physics tick's cost (the Fellow phase counters: sync, _physics_process, step, end; stock builds have only
the frame time) and whether the scene's per-tick state hashes equal the reference variant's.

  physics_bench.py --builds "stock=stock,off=fellow183@physics/distance_face_cull=false;physics/convex_vertex_cache=false,on=fellow183"
                   --engines godot,jolt --cases pile,stack,chars,rays,areas --ticks 600 --repeats 2 --session m17_f247 --ref off

A variant is label=bin[@toggles][%overrides]: bin is a folder of .tools/bin (or a path to an executable), toggles are
FellowEngine settings relative to rendering/fellow/ separated by ';' (the bench's --fellow argument), overrides are
startup settings section/key=value separated by ';' (override.cfg; e.g. threading/worker_pool/max_threads=4).
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_benchmarks as rb  # noqa: E402

ENGINES = {"godot": [], "jolt": ['physics/3d/physics_engine="Jolt Physics"']}


def parse_variants(spec: str) -> list[dict]:
    out = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        label, rest = part.split("=", 1)
        rest, _, overrides = rest.partition("%")
        binname, _, toggles = rest.partition("@")
        exe = binname if os.path.isfile(binname) else rb.exe_for(binname, None)
        out.append({"label": label.strip(), "bin": binname, "exe": exe, "fellow": toggles.replace(";", ","),
                    "overrides": [o for o in overrides.split(";") if o]})
    return out


def summarize(runs: list[dict]) -> dict:
    """Median over repeats of each run's mean per tick."""
    out = {}
    for key in ("physics_ms", "frame_ms", "step_ms", "process_ms", "sync_ms", "end_ms"):
        vals = [r["summary"][key]["mean"] for r in runs if key in r.get("summary", {})]
        if vals:
            out[key] = {"median": statistics.median(vals), "runs": vals}
    phases = {}
    for r in runs:
        for k, v in (r.get("phases_ms") or {}).items():
            phases.setdefault(k, []).append(v)
    if phases:
        out["phases_ms"] = {k: statistics.median(v) for k, v in phases.items()}
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--builds", required=True)
    ap.add_argument("--engines", default="godot,jolt")
    ap.add_argument("--cases", default="pile,stack,chars,rays,areas")
    ap.add_argument("--ticks", type=int, default=600)
    ap.add_argument("--warm", type=int, default=0, help="physics ticks before the measured ones")
    ap.add_argument("--repeats", type=int, default=2)
    ap.add_argument("--ref", default=None, help="variant whose hashes the others are compared with (default: the first)")
    ap.add_argument("--extra", default="", help="more user arguments for the bench (space-separated)")
    ap.add_argument("--session", default=None)
    ap.add_argument("--label", default="")
    ap.add_argument("--cooldown", type=float, default=2.0)
    ap.add_argument("--timeout", type=float, default=900.0)
    args = ap.parse_args()

    variants = parse_variants(args.builds)
    for v in variants:
        if not os.path.exists(v["exe"]):
            rb.log(f"missing executable for {v['label']}: {v['exe']}")
            return 2
    ref = args.ref or variants[0]["label"]
    engines = [e for e in args.engines.split(",") if e]
    cases = [c for c in args.cases.split(",") if c]
    session = args.session or datetime.datetime.now().strftime("%Y%m%d_%H%M%S") + "_physics"
    raw = os.path.join(rb.RESULTS, "raw", session)
    os.makedirs(raw, exist_ok=True)
    hw = rb.hardware_record({v["label"]: v["exe"] for v in variants})
    rb.log(f"session {session}: variants={[v['label'] for v in variants]} engines={engines} cases={cases} ticks={args.ticks} repeats={args.repeats}")
    rb.keep_awake()

    results: dict = {}
    rb.write_override_cfg([])
    try:
        for eng in engines:
            for case in cases:
                runs = {v["label"]: [] for v in variants}
                for i in range(args.repeats):
                    order = variants if i % 2 == 0 else list(reversed(variants))
                    for v in order:
                        out_dir = os.path.join(raw, v["label"], f"{eng}_{case}")
                        os.makedirs(out_dir, exist_ok=True)
                        tag = f"{eng}_{case}"
                        user = ["--scene=physics", "--mode=physics", f"--physics_case={case}", f"--ticks={args.ticks}",
                                f"--physics_warm={args.warm}", f"--out={out_dir}", f"--tag={tag}", f"--run={i}"]
                        if v["fellow"]:
                            user.append(f"--fellow={v['fellow']}")
                        if args.extra:
                            user.extend(args.extra.split())
                        rb.write_override_cfg(ENGINES[eng] + v["overrides"])
                        lp = os.path.join(out_dir, f"run{i}.log")
                        rb.log(f"{eng} {case} {v['label']} run {i} ...")
                        proc = rb.run_godot(v["exe"], ["--headless", "--fixed-fps", "60"], user, args.timeout, lp)
                        jf = os.path.join(out_dir, f"physics_physics_{tag}_64x64_run{i}.json")
                        r = {"valid": False}
                        if os.path.exists(jf):
                            with open(jf, encoding="utf-8") as f:
                                d = json.load(f)
                            p = d.get("physics", {})
                            r = {"valid": bool(p), "summary": p.get("summary", {}), "hashes": p.get("hashes", []),
                                 "counters": p.get("counters", {}), "engine": p.get("engine"), "threads": p.get("threads"), "phases_ms": p.get("phases_ms"),
                                 "wall_s": p.get("wall_s"), "fellow_requested": (d.get("fellow") or {}).get("requested"),
                                 "verify": {k: v2 for k, v2 in (d.get("fellow_stats_process_total") or {}).items() if k.startswith("physics_verify")}}
                        r["_proc"] = proc
                        runs[v["label"]].append(r)
                        s = r.get("summary", {})
                        msg = f"   -> rc={proc['returncode']} valid={r['valid']} wall={proc['wall_s']:.1f}s"
                        if "frame_ms" in s:
                            msg += f" frame={s['frame_ms']['mean']:.3f}ms"
                        if "physics_ms" in s:
                            msg += f" physics={s['physics_ms']['mean']:.3f}ms (step {s['step_ms']['mean']:.3f}, process {s['process_ms']['mean']:.3f})"
                        rb.log(msg)
                        if args.cooldown > 0:
                            time.sleep(args.cooldown)
                ref_hashes = next((r["hashes"] for r in runs[ref] if r.get("valid")), None)
                for v in variants:
                    rs = runs[v["label"]]
                    hs = [r["hashes"] for r in rs if r.get("valid")]
                    entry = {"engine": eng, "case": case, "variant": v["label"], "bin": v["bin"], "fellow": v["fellow"],
                             "valid_runs": len(hs), "stats": summarize([r for r in rs if r.get("valid")]),
                             "repeat_hashes_equal": all(h == hs[0] for h in hs) if hs else None,
                             "hashes_equal_ref": (hs[0] == ref_hashes) if hs and ref_hashes else None,
                             "first_hash_difference_tick": next((a[0] for a, b in zip(hs[0], ref_hashes) if a != b), None) if hs and ref_hashes else None,
                             "runs": rs}
                    results[f"{eng}|{case}|{v['label']}"] = entry
    finally:
        rb.write_override_cfg([])

    summary = {"session": session, "label": args.label, "args": vars(args), "hardware": hw, "reference": ref, "results": results}
    jpath = os.path.join(rb.RESULTS, f"{session}.json")
    with open(jpath, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1, default=str)

    # Markdown: per engine and case, each variant's physics (or frame) time per tick, and the hashes.
    lines = [f"# {session}", "", args.label, "", f"Reference for hashes: `{ref}`. Times: median over {args.repeats} runs of the mean per tick "
             f"({args.ticks} ticks); physics = sync + _physics_process + step + end (FellowEngine counters), frame = the whole frame.", ""]
    head = "| engine | case | " + " | ".join(f"{v['label']} physics (frame) ms" for v in variants) + " | hashes |"
    lines += [head, "|" + "---|" * (3 + len(variants))]
    for eng in engines:
        for case in cases:
            cells = []
            hnote = []
            for v in variants:
                e = results[f"{eng}|{case}|{v['label']}"]
                st = e["stats"]
                ph = st.get("physics_ms", {}).get("median")
                fr = st.get("frame_ms", {}).get("median")
                cells.append((f"{ph:.3f}" if ph is not None else "–") + (f" ({fr:.3f})" if fr is not None else ""))
                if v["label"] != ref:
                    hnote.append(f"{v['label']}: " + ("same" if e["hashes_equal_ref"] else f"DIFFER from tick {e['first_hash_difference_tick']}" if e["hashes_equal_ref"] is False else "?"))
                if e["repeat_hashes_equal"] is False:
                    hnote.append(f"{v['label']} repeats differ")
            lines.append(f"| {eng} | {case} | " + " | ".join(cells) + " | " + "; ".join(hnote) + " |")
    # The step's phases (ms per tick, median over runs) where a variant reports them.
    lines += ["", "Step phases (ms per tick; GodotPhysics3D: integrate forces, broadphase, islands, pair setup, solve, integrate velocities;"
              " Jolt: pre-step, Update, post-step):", ""]
    for eng in engines:
        for case in cases:
            for v in variants:
                ph = results[f"{eng}|{case}|{v['label']}"]["stats"].get("phases_ms")
                if not ph:
                    continue
                pre = "godot_" if eng == "godot" else "jolt_"
                items = [f"{k[len(pre):]} {x:.3f}" for k, x in ph.items() if k.startswith(pre)]
                lines.append(f"* {eng} {case} {v['label']}: " + ", ".join(items))
    mpath = os.path.join(rb.RESULTS, f"{session}.md")
    with open(mpath, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    rb.log(f"wrote {jpath} and {mpath}")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
