#!/usr/bin/env python3
"""Milestone 203: the forces the showcase's climbing asks of the body (the owner: "why does the character jump face first
into the wall, and why is its push up so effortless — assess the forces needed for the jump and make it realistic").

Reads climb_look.gd's frames.json (the demo at 60 fps: `-- --demo --demo_from=house --images=0 --lite=1`) and reports,
for a body of BODY_MASS kg whose centre of mass is taken as its hips:

* the run and the jump at the wall: the approach's speed at the takeoff, the takeoff's vertical speed, the body's
  horizontal deceleration against the wall (in g: the body stopped by the stone or by its legs), and how close the face,
  the chest and the knees come to the stone (from the head's, the upper chest's points; negative: in it);
* each move from hold to hold: the hips' rise, their fastest climb, the support force (body weights: 1 + a/g where the
  body is held — the hands on a hold or the feet on stone) at its peak and averaged over the push, the take-off speed
  of a flight and the deceleration in it against gravity (a body in the air slows at g — a slower one floats), the
  elbows' most bend while both hands still hold (an arm that pulls bends), and the most sudden change (a "snap": the
  largest acceleration over three frames);
* gates (human limits — climbers' explosive pull-ups peak at 1.4-1.7 body weights, PeerJ 11:e15886; a strong climber's
  arms and legs together at about 2.5).

Milestone 204 (the owner: "not 100 % realism but arcade fun, with the animations relatively realistic"): `--profile
arcade` (the showcase's) keeps the gates that make a motion read as real — the run braked at the wall, the face off it,
a flight under gravity, no snap beyond the push itself — and lets the hero be stronger than a person: a leap's push to
6.5 body weights (a dyno of about 2.5 m), at a game's pace (the demo's first eight moves from hold to hold within 10 s).

    python3 showcase/tools/effort.py <frames.json> [--baseline <frames.json>] [--json <out.json>] [--profile human|arcade]
"""
import argparse
import json
import math

G = 9.81
BODY_MASS = 75.0
FPS = 60.0
FACE = 0.10  # m: the face in front of the head's point (the mannequin's skull, its nose)

GATES = [
	# (name, key, low, high, what)
	("approach speed at the takeoff", "approach_speed", None, 4.5, "m/s: a run braked for the wall (a sprint into it at 7 had)"),
	("the body stopped against the wall", "wall_decel_g", None, 3.0, "g: horizontal, legs absorbing (the stone: tens of g)"),
	("the face off the wall", "face_gap_min", 0.12, None, "m, its least from the jump to the hang settled"),
	("support force at its peak", "support_peak_bw", None, 2.6, "body weights, any move from hold to hold"),
	("flight's deceleration", "flight_g_ratio", 0.85, 1.15, "of g, the hips in the air (1: ballistic; less: floating)"),
	("snaps", "snap_g", None, 3.5, "g, the largest change of speed over 3 frames (on a hold or in the air)"),
	("leaps a person can make", "leap_rise_max", None, 1.45, "m, the hips' rise from the push to the dead point"),
]

ARCADE = {
	# (key: (name, low, high, what)) — the gates the arcade profile changes or adds
	"support_peak_bw": ("support force at its peak", None, 6.5, "body weights: an arcade dyno's push (a person's: 2.6)"),
	"snap_g": ("snaps", None, 6.5, "g over 3 frames: none beyond the push itself (a catch lifted 9 cm in a frame: 30)"),
	"leap_rise_max": ("leaps the hero can make", None, 2.6, "m, the hips' rise from the push to the dead point"),
	"pace_s": ("the climb's pace", None, 10.0, "s, the demo's first eight moves from hold to hold (milestone 202: 8.5)"),
}


def gates(profile):
	if profile != "arcade":
		return GATES
	out = []
	for name, key, lo, hi, what in GATES:
		if key in ARCADE:
			n, l, h, w = ARCADE[key]
			out.append((n, key, l, h, w))
		else:
			out.append((name, key, lo, hi, what))
	n, l, h, w = ARCADE["pace_s"]
	out.append((n, "pace_s", l, h, w))
	return out


def load(path):
	with open(path) as f:
		return json.load(f)["frames"]


def segments(fr):
	out = []
	prev = None
	start = 0
	for k, f in enumerate(fr):
		key = (f["state"], bool(f.get("move")))
		if key != prev:
			if prev is not None:
				out.append((start, k - 1, prev[0], prev[1]))
			prev = key
			start = k
	out.append((start, len(fr) - 1, prev[0], prev[1]))
	return out


def elbow(f, s):
	l = f["limbs"][s]
	a, b, c = l["a"], l["b"], l["c"]
	u = [a[i] - b[i] for i in range(3)]
	v = [c[i] - b[i] for i in range(3)]
	d = sum(u[i] * v[i] for i in range(3)) / max(1e-6, math.dist(a, b) * math.dist(c, b))
	return 180.0 - math.degrees(math.acos(max(-1.0, min(1.0, d))))


def smooth(xs, n=1):
	if n <= 0:
		return xs
	out = list(xs)
	for k in range(1, len(xs) - 1):
		out[k] = (xs[k - 1] + 2 * xs[k] + xs[k + 1]) / 4.0
	return smooth(out, n - 1)


def analyse(fr):
	dt = 1.0 / FPS
	# (the hips' heights low-passed at about 6 Hz — four passes of a 1-2-1 filter at 60 fps — as motion capture's are before
	# forces are read from them: unfiltered, a centimetre's step of the IK reads as a body weight)
	hy = smooth([f["hips"][1] for f in fr], 4)
	vy = [0.0] + [(hy[k + 1] - hy[k - 1]) / (2 * dt) for k in range(1, len(fr) - 1)] + [0.0]
	ay = [0.0] + [(hy[k + 1] - 2 * hy[k] + hy[k - 1]) / dt ** 2 for k in range(1, len(fr) - 1)] + [0.0]
	r = {"moves": []}
	segs = segments(fr)
	# The approach and the jump at the wall.
	jw = next((s for s in segs if s[2] == "jump_wall"), None)
	if jw:
		a, b = jw[0], jw[1]
		v0 = fr[a]["vel"]
		r["approach_speed"] = math.hypot(v0[0], v0[2])
		r["takeoff_vy"] = v0[1]
		fwd = fr[a].get("fwd", [0, 0, -1])
		# (from the takeoff: the run's own strides before it brake and push as a runner's do)
		hv = [fr[k]["vel"][0] * fwd[0] + fr[k]["vel"][2] * fwd[2] for k in range(a, min(len(fr), b + 40))]
		dec = 0.0
		for k in range(1, len(hv)):
			dec = max(dec, (hv[k - 1] - hv[k]) / dt / G)
		# (the body: the capsule's speed into the wall; the hips', where the traversal moves the capsule)
		hz = [fr[k]["hips"][0] * fwd[0] + fr[k]["hips"][2] * fwd[2] for k in range(max(0, a - 4), min(len(fr), b + 40))]
		hz = smooth(hz, 2)
		for k in range(2, len(hz) - 2):
			v1 = (hz[k] - hz[k - 2]) / (2 * dt)
			v2 = (hz[k + 2] - hz[k]) / (2 * dt)
			dec = max(dec, (v1 - v2) / (2 * dt) / G)
		r["wall_decel_g"] = dec
		end = b + 1
		while end < len(fr) and fr[end]["state"] in ("hang_catch", "hang") and not fr[end].get("move"):
			end += 1
		gaps = [fr[k]["gaps"] for k in range(a, min(len(fr), end + 1)) if "gaps" in fr[k]]
		if gaps:
			r["face_gap_min"] = min(g["Head"] for g in gaps) - FACE
			r["chest_gap_min"] = min(g["UpperChest"] for g in gaps)
			r["knee_gap_min"] = min(min(g["LeftLowerLeg"], g["RightLowerLeg"]) for g in gaps)
	# The moves from hold to hold.
	peak_all = 0.0
	snap_all = 0.0
	flight_ratios = []
	rise_max = 0.0
	for a, b, st, mv in segs:
		if not mv or not st.startswith("hang_"):
			continue
		m = {"state": st, "from": a, "to": b, "time": (b - a + 1) * dt, "rise": hy[b] - hy[a]}
		held = []  # frames the body is held: a hand gripping, or the feet on the wall
		air = []
		for k in range(a, b + 1):
			f = fr[k]
			grip = f.get("hand_grips", [1, 1])
			feet = f.get("foot_weights", [0, 0])
			if max(grip) > 0.5 or max(feet) > 0.5:
				held.append(k)
			else:
				air.append(k)
		m["vy_max"] = max(vy[a:b + 1])
		forces = [1.0 + ay[k] / G for k in held]
		m["support_peak_bw"] = max(forces) if forces else 1.0
		# the push: from the move's start to its fastest climb while held
		push = [k for k in held if k <= max(range(a, b + 1), key=lambda k: vy[k])]
		m["push_mean_bw"] = (sum(1.0 + ay[k] / G for k in push) / len(push)) if push else 1.0
		both = [k for k in range(a, b + 1) if min(fr[k].get("hand_grips", [1, 1])) > 0.5]
		m["elbow_pull"] = max((min(elbow(fr[k], 0), elbow(fr[k], 1)) for k in both), default=0.0)
		if len(air) >= 6:
			# the flight: from the take-off (the first frame in the air) to the dead point (or the catch); milestone 206:
			# from the hips' fastest climb after it, if later — the legs still pushing as the hands leave (a short leap's)
			# are not the flight
			k0 = air[0]
			top = max(range(k0, b + 1), key=lambda k: hy[k])
			k0 = max(range(k0, top + 1), key=lambda k: vy[k])
			fl = [k for k in air if k0 <= k <= top]
			if len(fl) >= 4:
				dec = -sum(ay[k] for k in fl[1:-1]) / max(1, len(fl) - 2)
				m["flight_g_ratio"] = dec / G
				m["takeoff_speed"] = vy[k0]
				m["leap_rise"] = hy[top] - hy[a]
				flight_ratios.append(m["flight_g_ratio"])
				rise_max = max(rise_max, m["leap_rise"])
		snap = 0.0
		for k in range(a + 2, b - 1):
			snap = max(snap, abs(vy[k + 1] - vy[k - 2]) / (3 * dt) / G)
		m["snap_g"] = snap
		peak_all = max(peak_all, m["support_peak_bw"])
		snap_all = max(snap_all, snap)
		r["moves"].append(m)
	r["support_peak_bw"] = peak_all
	r["snap_g"] = snap_all
	if flight_ratios:
		r["flight_g_ratio"] = min(flight_ratios, key=lambda x: -abs(1.0 - x))
	r["leap_rise_max"] = rise_max
	if len(r["moves"]) >= 8:
		r["pace_s"] = (r["moves"][7]["to"] - r["moves"][0]["from"] + 1) * dt
	return r


def gate_state(v, lo, hi):
	if v is None:
		return "—"
	if lo is not None and v < lo:
		return "FAIL"
	if hi is not None and v > hi:
		return "FAIL"
	return "PASS"


def report(r, base=None, profile="human"):
	lines = ["gate                                   value  baseline  state  band"]
	passed = 0
	total = 0
	for name, key, lo, hi, what in gates(profile):
		v = r.get(key)
		bv = base.get(key) if base else None
		st = gate_state(v, lo, hi)
		total += 1 if v is not None else 0
		passed += 1 if st == "PASS" else 0
		band = f"{'' if lo is None else lo}..{'' if hi is None else hi}"
		lines.append(f"{name:36s} {('%.2f' % v) if v is not None else '—':>8s} {('%.2f' % bv) if bv is not None else '':>9s}  {st:5s}  {band} {what}")
	lines.append(f"{passed}/{total} gates pass")
	lines.append("")
	lines.append("the jump at the wall: approach %.2f m/s, takeoff %.2f m/s up; stopped at %.1f g; face %.2f m, chest %.2f m, knees %.2f m from the stone at the closest" % (
		r.get("approach_speed", 0), r.get("takeoff_vy", 0), r.get("wall_decel_g", 0), r.get("face_gap_min", 0), r.get("chest_gap_min", 0), r.get("knee_gap_min", 0)))
	lines.append("move              frames      time  rise  climb  peak  push  elbows  takeoff  flight   snap")
	lines.append("                                      m   m/s    BW    BW  pulled     m/s     /g      g")
	for m in r["moves"]:
		lines.append("%-15s %4d-%4d %6.2fs %5.2f %6.2f %5.2f %5.2f %6.0f° %8s %7s %6.1f" % (
			m["state"], m["from"], m["to"], m["time"], m["rise"], m["vy_max"], m["support_peak_bw"], m["push_mean_bw"], m["elbow_pull"],
			("%.2f" % m["takeoff_speed"]) if "takeoff_speed" in m else "—", ("%.2f" % m["flight_g_ratio"]) if "flight_g_ratio" in m else "—", m["snap_g"]))
	return "\n".join(lines)


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("frames")
	ap.add_argument("--baseline")
	ap.add_argument("--json")
	ap.add_argument("--profile", default="human", choices=["human", "arcade"])
	a = ap.parse_args()
	r = analyse(load(a.frames))
	base = analyse(load(a.baseline)) if a.baseline else None
	print(report(r, base, a.profile))
	if a.json:
		with open(a.json, "w") as f:
			json.dump(r, f, indent=1)


if __name__ == "__main__":
	main()
