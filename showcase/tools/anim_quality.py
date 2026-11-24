#!/usr/bin/env python3
"""The showcase's animation against its gates (docs/showcase_animation_quality.md).

  python3 showcase/tools/anim_quality.py run --godot bin/godot.linuxbsd.editor.x86_64 --out DIR [--baseline DIR]
  python3 showcase/tools/anim_quality.py report --out DIR [--baseline DIR]

`run` measures (tools/anim_quality.gd: the gaits and the climb, headless) and reports; `report` reads DIR's JSON again.
The report: every gate with its value and PASS/FAIL, against a baseline's value when one is given (a metric worse than
the baseline's by more than its tolerance is a regression), and plots of the joints over the stride (DIR/*.png).
Exit status 0 only when every gate passes and nothing regressed.
"""

import argparse
import json
import os
import subprocess
import sys

BINS = 50

# name, segment, metric, low, high (None: unbounded), tolerance for a regression (in the metric's unit), what it is
GATES = [
	("walk knee straight", "walk", "knee_straight_share", None, 0.12, 0.02, "share of frames with a knee within 5° of straight"),
	("walk knee loading", "walk", "knee_loading", 5.0, 20.0, 2.0, "°, its most bent in the first 20 % of the stride"),
	("walk knee toe-off", "walk", "knee_toeoff", 30.0, 45.0, 2.0, "° as the toe leaves the ground"),
	("walk knee swing", "walk", "knee_swing_peak", 55.0, 70.0, 2.0, "° at its most bent in the swing"),
	("walk hip strike", "walk", "hip_contact", 25.0, 35.0, 2.0, "° flexed at the heel's strike"),
	("walk hip push", "walk", "hip_extension", 10.0, 30.0, 2.0, "° extended at its most, in stance"),
	("walk ankle loading", "walk", "ankle_loading_pf", 10.0, 25.0, 2.0, "° plantarflexed after the strike (0-15 %)"),
	("walk ankle toe-off", "walk", "ankle_toeoff_pf", 15.0, 30.0, 2.0, "° plantarflexed as the toe leaves"),
	("walk skate", "walk", "skate_cm", None, 2.0, 0.3, "cm a planted foot moves in a stance (median)"),
	("walk feet in ground", "walk", "g_min_cm", -2.0, None, 0.5, "cm, the feet's lowest point against the ground"),
	("walk left-right", "walk", "right_phase", 0.47, 0.53, 0.01, "the right foot's contact in the left's stride"),
	("run knee least", "run", "knee_least", 15.0, None, 2.0, "°, the knee's least flexion over the stride"),
	("run knee contact", "run", "knee_contact", 15.0, 45.0, 2.0, "° at contact"),
	("run knee stance", "run", "knee_stance_peak", 40.0, 65.0, 2.0, "° at its most bent in stance"),
	("run knee swing", "run", "knee_swing_peak", 90.0, 125.0, 3.0, "° at its most bent in the swing"),
	("run knee straight", "run", "knee_straight_share", None, 0.0, 0.01, "share of frames within 5° of straight"),
	("run skate", "run", "skate_cm", None, 2.0, 0.3, "cm"),
	("run feet in ground", "run", "g_min_cm", -2.0, None, 0.5, "cm"),
	("sprint knee least", "sprint", "knee_least", 15.0, None, 2.0, "°"),
	("sprint knee swing", "sprint", "knee_swing_peak", 110.0, 130.0, 3.0, "°"),
	("sprint knee straight", "sprint", "knee_straight_share", None, 0.0, 0.01, "share of frames"),
	("sprint skate", "sprint", "skate_cm", None, 2.0, 0.3, "cm"),
	("walk to run skate", "run_in", "skate_max_cm", None, 5.0, 0.5, "cm, the worst stance"),
	("run to sprint skate", "sprint_in", "skate_max_cm", None, 5.0, 0.5, "cm"),
	("stop skate", "stop", "skate_max_cm", None, 5.0, 0.5, "cm"),
	("walk to run pop", "run_in", "pop", None, 1.5, 0.1, "the joints' fastest against the gaits' fastest"),
	("run to sprint pop", "sprint_in", "pop", None, 1.5, 0.1, ""),
	("stop pop", "stop", "pop", None, 1.5, 0.1, ""),
	("turn pop", "turn", "pop", None, 1.5, 0.1, ""),
	("hanging knee straight", "hanging", "knee_straight_share", None, 0.01, 0.01, "share of frames"),
	("hold to hold knee straight", "hold_to_hold", "knee_straight_share", None, 0.01, 0.01, "share of frames"),
	("mantle knee straight", "mantle", "knee_straight_share", None, 0.01, 0.01, "share of frames"),
	# Milestone 156 (decision 0296): no pops — the fastest joint (its turn against its parent) in each climbing segment
	# (a sprinting knee's fastest is some 1100°/s; the pops measured 3000-9000: a joint turned over in a frame), and the
	# body's jumps (the hips' acceleration: the mantle's had moved them 0.65 m in two frames).
	("approach pop", "approach", "joint_max", None, 2200.0, 100.0, "°/s, the fastest joint (the run, the jump at the wall)"),
	("hanging pop", "hanging", "joint_max", None, 2200.0, 100.0, "°/s"),
	("hold to hold pop", "hold_to_hold", "joint_max", None, 2200.0, 100.0, "°/s"),
	("mantle pop", "mantle", "joint_max", None, 2200.0, 100.0, "°/s"),
	("roof pop", "roof", "joint_max", None, 2200.0, 100.0, "°/s (the scramble up the tiles)"),
	("mantle body jump", "mantle", "hips_acc_max", None, 400.0, 50.0, "m/s², the hips' fastest change of speed"),
]


def mean_curve(strides, j):
	if not strides:
		return []
	return [sum(s[i][j] for s in strides) / len(strides) for i in range(BINS)]


def derive(seg):
	"""The metrics read off a segment's strides."""
	m = dict(seg)
	strides = seg.get("strides", [])
	knee, hip, ankle = mean_curve(strides, 0), mean_curve(strides, 1), mean_curve(strides, 2)
	toe = seg.get("toe_off", [])
	t = sorted(toe)[len(toe) // 2] if toe else 0.6
	ti = min(BINS - 1, max(1, int(round(t * BINS))))
	m["toe_off_at"] = t
	m["knee_straight_share"] = seg["knee_straight_share"]
	m["g_min_cm"] = seg["g_min_cm"]
	m["skate_cm"] = seg.get("skate_cm")
	m["skate_max_cm"] = seg.get("skate_max_cm")
	if knee:
		m["knee_contact"] = knee[0]
		m["knee_loading"] = max(knee[: max(2, BINS // 5)])
		m["knee_stance_peak"] = max(knee[:ti])
		m["knee_toeoff"] = knee[ti]
		m["knee_swing_peak"] = max(knee[ti:])
		m["knee_least"] = min(knee)
		m["hip_contact"] = hip[0]
		m["hip_extension"] = -min(hip[:ti])
		m["ankle_loading_pf"] = max(ankle[: max(2, int(BINS * 0.15))])
		m["ankle_toeoff_pf"] = ankle[ti]
	rp = sorted(seg.get("right_phase", []))
	if rp:
		m["right_phase"] = rp[len(rp) // 2]
	elif "right_phase" in m:
		del m["right_phase"]  # no right contact within a left stride (too few strides): missing, not a list
	m["curves"] = {"knee": knee, "hip": hip, "ankle": ankle}
	return m


def load(d):
	segs = {}
	for prog in ("gaits", "climb"):
		p = os.path.join(d, prog + ".json")
		if os.path.exists(p):
			for k, v in json.load(open(p))["segments"].items():
				segs[k] = derive(v)
	for v in segs.values():
		if v.get("joint_deg_s"):
			v["joint_max"] = v["joint_deg_s"]["max"]
		if v.get("hips_acc"):
			v["hips_acc_max"] = v["hips_acc"]["max"]
	# The pops: each change's fastest joint against the steady gaits' fastest.
	steady = [segs[s]["speed_deg_s"] for s in ("walk", "run", "sprint") if s in segs]
	for s in ("run_in", "sprint_in", "stop", "turn"):
		if s in segs and steady:
			segs[s]["pop"] = max(segs[s]["speed_deg_s"][j] / max(1e-6, max(x[j] for x in steady)) for j in ("knee", "hip", "ankle"))
	return segs


def judge(segs, base):
	rows = []
	ok = True
	for name, seg, metric, lo, hi, tol, what in GATES:
		v = segs.get(seg, {}).get(metric)
		if v is None:
			rows.append((name, "—", "", "MISSING", what))
			ok = False
			continue
		passed = (lo is None or v >= lo) and (hi is None or v <= hi)
		was = base.get(seg, {}).get(metric) if base else None
		regressed = False
		if was is not None and not passed:
			# Worse than the baseline: further from the band.
			def miss(x):
				return max(0.0, (lo - x) if lo is not None else 0.0, (x - hi) if hi is not None else 0.0)
			regressed = miss(v) > miss(was) + tol
		state = "PASS" if passed else ("REGRESSED" if regressed else "FAIL")
		ok = ok and passed
		band = f"{'' if lo is None else f'{lo:g}'}..{'' if hi is None else f'{hi:g}'}"
		rows.append((name, f"{v:.3g}", "" if was is None else f"{was:.3g}", state, f"{band} {what}"))
	return rows, ok


def plot(segs, out):
	try:
		import matplotlib
		matplotlib.use("Agg")
		import matplotlib.pyplot as plt
	except ImportError:
		return
	bands = {  # (joint, phase %, low, high) from the gates, drawn where they apply
		"walk": [("knee", 10, 5, 20), ("knee", None, 30, 45), ("knee", 72, 55, 70), ("hip", 0, 25, 35), ("ankle", 7, 10, 25), ("ankle", None, 15, 30)],
		"run": [("knee", 0, 15, 45), ("knee", 15, 40, 65), ("knee", 70, 90, 125)],
		"sprint": [("knee", 70, 110, 130)],
	}
	for g in ("walk", "run", "sprint"):
		s = segs.get(g)
		if not s or not s["curves"]["knee"]:
			continue
		fig, axes = plt.subplots(1, 3, figsize=(13, 3.6))
		x = [100.0 * i / BINS for i in range(BINS)]
		for ax, j in zip(axes, ("knee", "hip", "ankle")):
			ax.plot(x, s["curves"][j], color="#1f5fa8", lw=2, label="mean")
			ax.axvline(100 * s["toe_off_at"], color="#888", ls="--", lw=1)
			for bj, ph, lo, hi in bands.get(g, []):
				if bj != j:
					continue
				p = 100 * s["toe_off_at"] if ph is None else ph
				ax.add_patch(plt.Rectangle((p - 2, lo), 4, hi - lo, color="#3a9a4a", alpha=0.35))
			ax.set_title(f"{g}: {j} ({'flexion' if j != 'ankle' else 'plantarflexion'}, °)")
			ax.set_xlabel("% of the stride from the contact (dashed: toe-off)")
			ax.grid(alpha=0.3)
		fig.tight_layout()
		fig.savefig(os.path.join(out, f"curves_{g}.png"), dpi=90)
		plt.close(fig)


def report(out, baseline):
	segs = load(out)
	base = load(baseline) if baseline else None
	rows, ok = judge(segs, base)
	w = max(len(r[0]) for r in rows)
	lines = [f"{'gate':<{w}}  {'value':>8}  {'baseline':>8}  state      band"]
	for r in rows:
		lines.append(f"{r[0]:<{w}}  {r[1]:>8}  {r[2]:>8}  {r[3]:<9}  {r[4]}")
	passed = sum(1 for r in rows if r[3] == "PASS")
	lines.append(f"{passed}/{len(rows)} gates pass" + ("" if ok else "; " + ", ".join(r[0] for r in rows if r[3] == "REGRESSED") + (" regressed" if any(r[3] == "REGRESSED" for r in rows) else "")))
	# Milestone 156: the whole body's smoothness per segment (not gated yet): the fastest joint per frame (its turn against
	# its parent, deg/s) and the hips' acceleration (m/s²), against the baseline's.
	lines.append("")
	lines.append(f"{'smoothness':<14} {'joint p95':>9} {'p99':>6} {'max':>6}   {'hips acc p95':>12} {'p99':>6} {'max':>6}   (baseline max: joint, hips)")
	for k in sorted(segs):
		j = segs[k].get("joint_deg_s")
		h = segs[k].get("hips_acc")
		if not j or not h:
			continue
		bj = (base or {}).get(k, {}).get("joint_deg_s")
		bh = (base or {}).get(k, {}).get("hips_acc")
		was = f"   ({bj['max']:.0f}, {bh['max']:.1f})" if bj and bh else ""
		lines.append(f"{k:<14} {j['p95']:>9.0f} {j['p99']:>6.0f} {j['max']:>6.0f}   {h['p95']:>12.1f} {h['p99']:>6.1f} {h['max']:>6.1f}{was}")
	# Milestone 195 (the owner: the character's limbs through the building while climbing): the share of a segment's frames
	# with a joint behind a surface seen from the hips (not gated yet), its deepest, and which joints.
	lines.append("")
	lines.append(f"{'through walls':<14} {'share':>6} {'deepest':>8}   joints (baseline share)")
	for k in sorted(segs):
		if "inside_share" not in segs[k] or k in ("walk", "run", "sprint", "run_in", "sprint_in", "stop", "turn", "start"):
			continue
		b = (base or {}).get(k, {}).get("inside_share")
		bones = ", ".join(f"{n} {c}" for n, c in sorted(segs[k].get("inside_bones", {}).items(), key=lambda x: -x[1]))
		lines.append(f"{k:<14} {100 * segs[k]['inside_share']:>5.1f}% {segs[k].get('inside_depth_cm', 0):>6.1f}cm   {bones or '—'}" + (f" ({100 * b:.1f}%)" if b is not None else ""))
	text = "\n".join(lines)
	open(os.path.join(out, "report.txt"), "w").write(text + "\n")
	print(text)
	plot(segs, out)
	return ok and not any(r[3] == "REGRESSED" for r in rows)


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("cmd", choices=["run", "report"])
	ap.add_argument("--godot")
	ap.add_argument("--out", required=True)
	ap.add_argument("--baseline")
	a = ap.parse_args()
	here = os.path.dirname(os.path.abspath(__file__))
	project = os.path.dirname(here)
	if a.cmd == "run":
		os.makedirs(a.out, exist_ok=True)
		for prog, extra in (("gaits", []), ("climb", ["--demo", "--demo_from=house"])):
			cmd = [a.godot, "--headless", "--path", project, "--fixed-fps", "120", "-s", "res://tools/anim_quality.gd", "--",
					f"--program={prog}", f"--out={os.path.abspath(a.out)}"] + extra
			r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
			errors = [l for l in (r.stdout + r.stderr).splitlines() if "SCRIPT ERROR" in l]
			if errors or not os.path.exists(os.path.join(a.out, prog + ".json")):
				print("\n".join(errors) or r.stdout[-2000:], file=sys.stderr)
				sys.exit(2)
	sys.exit(0 if report(a.out, a.baseline) else 1)


if __name__ == "__main__":
	main()
