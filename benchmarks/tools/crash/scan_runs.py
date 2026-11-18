"""FellowEngine milestone 146 (decision 0270): a benchmark session's runs — each run's result (or none), its late fence reports and the
Vulkan driver's sync log anomalies from its log, and FellowEngine's late_fences / sync_log_anomalies statistics.

    python3 scan_runs.py <benchmarks/results/raw/<session>> [--full]

A run without a result (it hung or crashed) gets its first late fence report printed (--full: every run's)."""
import glob
import json
import os
import re
import sys


def main():
    root = sys.argv[1]
    full = "--full" in sys.argv
    for build in sorted(d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d))):
        for scene_dir in sorted(glob.glob(os.path.join(root, build, "*"))):
            logs = sorted(glob.glob(os.path.join(scene_dir, "*_run*.log")), key=lambda p: int(re.search(r"run(\d+)", p).group(1)))
            for lp in logs:
                run = re.search(r"run(\d+)", lp).group(1)
                js = glob.glob(os.path.join(scene_dir, "*_%s_*_run%s.json" % (build, run)))
                stats = {}
                frame = None
                if js:
                    with open(js[0], encoding="utf-8") as f:
                        r = json.load(f)
                    t = r.get("fellow_stats_process_total", {})
                    stats = {k: t.get(k) for k in ("late_fences", "sync_log_anomalies")}
                    frame = r.get("frame_ms_mean") or r.get("avg_frame_ms")
                with open(lp, encoding="utf-8", errors="replace") as f:
                    lines = f.read().splitlines()
                late = [i for i, l in enumerate(lines) if "fence is not signalled after" in l]
                anomalies = [l for l in lines if "FellowEngine sync log:" in l and "swap chain" not in l]
                errors = [l for l in lines if l.startswith("ERROR") and "DDS" not in l and "audio" not in l.lower()]
                print("%s run %s: %s; %s; late reports %d, anomalies %d, errors %d" % (
                    build, run, "result" if js else "NO RESULT", stats, len(late), len(anomalies), len(errors)))
                for l in anomalies[:6]:
                    print("    " + l[:300])
                for l in errors[:4]:
                    print("    " + l[:300])
                if late and (full or not js):
                    i = late[0]
                    for l in lines[i:i + 70]:
                        print("    | " + l[:400])


if __name__ == "__main__":
    main()
