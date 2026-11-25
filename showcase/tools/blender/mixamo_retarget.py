# FellowEngine showcase (milestone 192): Mixamo's motion-captured clips on the rooftops' mannequin. Each FBX of a folder
# (downloaded by the owner from mixamo.com: the Y Bot, "Without Skin", 30 fps, no keyframe reduction, in place) is
# retargeted onto the mannequin's skeleton (mannequin.py) and the clips exported as one .glb that
# showcase/tools/build_mocap_clips.gd turns into the player's library of mocap clips.
#
# Mixamo's licence lets a game use its clips but not hand out the files: neither the FBX nor what is made from them is
# committed (the folder and the output are the owner's). Without them the showcase plays its own clips.
#
#   python mixamo_retarget.py <mixamo_dir> <out_dir>        (with the `bpy` module installed)
#   blender --background --python mixamo_retarget.py -- <mixamo_dir> <out_dir>
#
# The retarget: every frame each mapped bone turns, in the world, as Mixamo's turns from its rest pose (both skeletons
# rest in a T-pose standing on +Z, facing -Y); the hips move as Mixamo's scaled by the hips' heights; the legs then
# reach by IK for Mixamo's ankles scaled the same way (the shins' lengths differ: the feet would float or sink), the
# knees towards Mixamo's, the feet turned as Mixamo's. A loop's cycle is found (the left ankle's way back and forth),
# cut to one stride and, for the gaits, started at the left foot's strike and played over a second (the player's gait
# tree mixes the same moments of strides); clips.json beside the .glb gives each gait's speed and stride's length.

import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402
import addon_utils  # noqa: E402  (after bpy: the module's)
from mathutils import Matrix, Quaternion, Vector  # noqa: E402

from mannequin import build_armature, build_mesh  # noqa: E402
from mannequin_rig import FPS, Rig, write_action  # noqa: E402

SRC_FPS = 30

# Mixamo's bone (after its "mixamorig:" prefix) for each of the mannequin's.
MAP = {
	"Hips": "Hips",
	"Spine": "Spine",
	"Chest": "Spine1",
	"UpperChest": "Spine2",
	"Neck": "Neck",
	"Head": "Head",
}
for _side in ("Left", "Right"):
	MAP.update({
		_side + "Shoulder": _side + "Shoulder",
		_side + "UpperArm": _side + "Arm",
		_side + "LowerArm": _side + "ForeArm",
		_side + "Hand": _side + "Hand",
		_side + "ThumbMetacarpal": _side + "HandThumb1",
		_side + "ThumbProximal": _side + "HandThumb2",
		_side + "IndexProximal": _side + "HandIndex1",
		_side + "IndexIntermediate": _side + "HandIndex2",
		_side + "MiddleProximal": _side + "HandMiddle1",
		_side + "MiddleIntermediate": _side + "HandMiddle2",
		_side + "RingProximal": _side + "HandRing1",
		_side + "RingIntermediate": _side + "HandRing2",
		_side + "LittleProximal": _side + "HandPinky1",
		_side + "LittleIntermediate": _side + "HandPinky2",
		_side + "UpperLeg": _side + "UpLeg",
		_side + "LowerLeg": _side + "Leg",
		_side + "Foot": _side + "Foot",
		_side + "Toes": _side + "ToeBase",
	})

# The clips: the mannequin's clip, the Mixamo file names it may come from (lower case, the first found), how it is cut.
# kind: "gait" (a cycle, one stride over a second from the left foot's strike), "loop" (a cycle, one period),
# "once" (as it is, from `start` s before the feet leave the ground for "takeoff"), "land" (from the feet's touchdown).
CLIPS = [
	("idle", ["breathing idle", "idle", "happy idle"], "loop"),
	("gait_walk", ["walking", "walk", "standard walk"], "gait"),
	("gait_jog", ["slow run", "jog forward", "jogging"], "gait"),  # Mixamo's "Jogging" is barely faster than its walk
	("gait_run", ["running", "standard run", "run"], "gait"),
	("gait_sprint", ["fast run", "sprint", "running fast"], "gait"),
	("jump", ["jump", "standing jump"], "takeoff"),
	("jump_run", ["running jump"], "takeoff"),
	("fall", ["falling idle", "falling"], "loop"),
	("land", ["falling to landing", "landing"], "land"),
	("land_hard", ["hard landing"], "land"),
	("roll", ["falling to roll", "stand to roll", "running forward roll", "roll"], "land"),
	# Milestone 196: Mixamo's "Braced Hang" is a jump up into the hang and its settling (1.3 s), not a hang at rest —
	# milestone 194's loop of its last 60 % bobbed the hips 17 cm every 0.6 s. The hang at rest: its settled moment,
	# swayed forth and back over four seconds; the catch: from the hands on the ledge to settled.
	("hang_idle", ["braced hang"], "settle", {"from": 1.0, "to": 1.133, "length": 4.0}),
	("hang_catch", ["braced hang"], "span", {"from": 0.47, "to": 1.3}),
	("hang_shimmy_l", ["left braced hang shimmy", "braced hang shimmy left"], "loop"),
	("hang_shimmy_r", ["right braced hang shimmy", "braced hang shimmy right", "braced hang shimmy"], "loop"),
	# Milestone 198: the hops from their push (the hips' first rise) to settled on the next hold, with their timing for the
	# traversal (the hips' way along the move, their lift, the hands' let-go and catch: clips.json's "leap").
	("hang_leap_up", ["braced hang hop up"], "anchored", {"from": 0.13, "to": 1.33, "leap": "z"}),
	("hang_leap_l", ["braced hang hop left"], "anchored", {"from": 0.07, "to": 1.2, "leap": "x"}),
	("hang_leap_r", ["braced hang hop right"], "anchored", {"from": 0.07, "to": 1.2, "leap": "x"}),
	("climb_move", ["climbing up wall", "climbing"], "loop"),
	# Milestone 196: hand over hand from hold to hold — a half of Mixamo's climbing cycle each (the left hand up with the
	# right foot, then the right with the left), and across from Mixamo's shimmies (a step each).
	("hang_reach_l", ["climbing up wall"], "anchored", {"from": 0.0, "to": 0.933}),
	("hang_reach_r", ["climbing up wall"], "anchored", {"from": 0.933, "to": 2.0}),
	("hang_across_l", ["left braced hang shimmy"], "anchored", {"from": 0.0, "to": 1.167}),
	("hang_across_r", ["right braced hang shimmy"], "anchored", {"from": 0.0, "to": 1.167}),
	# Milestone 197: the jump at a wall for its first hold — "Run To Start Climbing" from its last stride to the hands
	# reaching up with a foot on the wall, the hips held (the player's jump moves the body; the traversal catches the hold).
	("jump_wall", ["run to start climbing"], "anchored", {"from": 0.67, "to": 1.27}),
	# Milestone 196: root motion (the traversal's clips: its path is their root's, scaled onto the ledge) — up onto the
	# ledge from a hang (the hips where the hang holds them at the start), over an obstacle with a hand planted.
	("hang_climb", ["braced hang to crouch"], "root", {"start_offset": (0.13, 0.915)}),
	# (the vault from the hand's reach for the top to the feet's touchdown: its run-in and run-out are the player's own —
	# whole, its 4.4 m of travel scaled onto the traversal's 1.4 m had slowed the body to 1.3 m/s over the wall)
	("vault", ["vault"], "root", {"from": 0.33, "to": 0.93, "root": "feet"}),
	("start_walk", ["start walking"], "travel"),
	("stop_walk", ["stop walking"], "travel"),
	("stop_run", ["run to stop"], "travel"),
]
# The hips where the mannequin's own clips of these hold them (y back from the wall, z up from the root, m): the player's
# traversal puts the body against the wall and the hands on the ledge from them (Mixamo's hang hangs 0.13 m higher, its
# climb 0.77 m).
HIPS_AT = {"hang_idle": (0.13, 0.915), "hang_catch": (0.13, 0.915), "hang_shimmy_l": (0.13, 0.915), "hang_shimmy_r": (0.13, 0.915),
		"hang_leap_up": (0.13, 0.915), "hang_leap_l": (0.13, 0.915), "hang_leap_r": (0.13, 0.915),
		"climb_move": (-0.08, 0.885), "hang_reach_l": (0.13, 0.915), "hang_reach_r": (0.13, 0.915),
		"hang_across_l": (0.13, 0.915), "hang_across_r": (0.13, 0.915)}
# kind "settle": a loop of `length` s swaying from `from` to `to` s of the source and back (a cosine: still at either end).
# "span": once, from `from` to `to` s, the hips' sway kept about their mean. "loop_tail": a loop from 40 % of the clip on. "anchored": the hips held where they start (the player's traversal
# moves the body from hold to hold; the limbs move about the hips as Mixamo's). "travel": in place, the hips' travel
# forward recorded (clips.json: m/s every 60th of a second) for the player to move the body by.


def strip(name):
	return name.split(":")[-1]


def import_fbx(path):
	before = set(bpy.data.objects)
	bpy.ops.import_scene.fbx(filepath=path, use_anim=True, ignore_leaf_bones=True, automatic_bone_orientation=False)
	new = [o for o in bpy.data.objects if o not in before]
	arm = next(o for o in new if o.type == "ARMATURE")
	return arm, new


class Source:
	"""A Mixamo armature sampled in the world: its bones' rest and posed matrices."""

	def __init__(self, arm):
		self.arm = arm
		self.bones = {strip(b.name): b.name for b in arm.data.bones}
		act = arm.animation_data.action
		self.first, self.last = act.frame_range
		self.length = (self.last - self.first) / SRC_FPS
		mw = arm.matrix_world
		self.turn = Matrix.Identity(4)
		rest = {k: mw @ arm.data.bones[v].matrix_local for k, v in self.bones.items()}
		if rest["LeftUpLeg"].translation.x < rest["Hips"].translation.x:
			# Facing +Y: turned to face -Y as the mannequin does (its left on +X).
			self.turn = Matrix.Rotation(math.pi, 4, "Z")
		self.rest = {k: self.turn @ m for k, m in rest.items()}
		self.rest_q = {k: m.to_3x3().normalized().to_quaternion() for k, m in self.rest.items()}

	def sample(self, t):
		f = self.first + t * SRC_FPS
		i = int(math.floor(f))
		bpy.context.scene.frame_set(i, subframe=f - i)
		mw = self.turn @ self.arm.matrix_world
		return {k: mw @ self.arm.pose.bones[v].matrix for k, v in self.bones.items()}


def retarget(rig, src, t, scale, ankle_dz, centre, root=None):
	"""The mannequin's pose at Mixamo's time t (s); root: the root bone's place (milestone 196: a clip's root motion)."""
	pose = src.sample(t)
	rig.reset()
	if root is not None:
		rig.loc["Root"] = rig.rest_q["Root"].inverted() @ root
	cache = {}
	for name in rig.names:
		m = MAP.get(name)
		if name == "Hips":
			p = pose["Hips"].translation
			want = Vector(((p.x - centre.x) * scale, (p.y - centre.y) * scale, (p.z - centre.z) * scale))
			base = rig.matrix("Root", cache) @ rig.rest_rel["Hips"]
			rig.loc["Hips"] = base.to_quaternion().inverted() @ (want - base.translation)
		if m is None or m not in pose:
			rig.matrix(name, cache)
			continue
		delta = pose[m].to_3x3().normalized().to_quaternion() @ src.rest_q[m].inverted()
		rig.set_global_rot(name, delta @ rig.rest_q[name], cache)
		cache.pop(name, None)
		rig.matrix(name, cache)
	# The legs: Mixamo's ankles, scaled, reached by IK; the knees towards Mixamo's; the feet and toes turned as its.
	for side in ("Left", "Right"):
		up, lo, ft = pose[side + "UpLeg"].translation, pose[side + "Leg"].translation, pose[side + "Foot"].translation
		target = Vector(((ft.x - centre.x) * scale, (ft.y - centre.y) * scale, (ft.z - centre.z) * scale + ankle_dz))
		mid = (up + ft) * 0.5
		pole = lo - mid
		if pole.length < 1e-4:
			pole = Vector((0, -1, 0))
		rig.ik(side + "UpperLeg", side + "LowerLeg", side + "Foot", target, pole.normalized(), (0, -1, 0))
		cache = rig.fk()
		for b, m in ((side + "Foot", side + "Foot"), (side + "Toes", side + "ToeBase")):
			if m in pose:
				delta = pose[m].to_3x3().normalized().to_quaternion() @ src.rest_q[m].inverted()
				rig.set_global_rot(b, delta @ rig.rest_q[b], cache)
				cache = rig.fk()
	return pose


def ankle_track(src, step):
	"""The left and right ankles' world positions every step seconds."""
	out = []
	t = 0.0
	while t <= src.length + 1e-6:
		p = src.sample(t)
		out.append((t, p["LeftFoot"].translation.copy(), p["RightFoot"].translation.copy(), p["Hips"].translation.copy()))
		t += step
	return out


def find_period(track):
	"""A cycle's period (s): the lag at which the left ankle's forward position best repeats itself."""
	ys = [a[1].y for a in track]
	n = len(ys)
	step = track[1][0] - track[0][0]
	mean = sum(ys) / n
	best, best_lag = -1e9, n - 1
	for lag in range(int(0.4 / step), n - int(0.2 / step)):
		num = sum((ys[i] - mean) * (ys[i + lag] - mean) for i in range(n - lag))
		den = math.sqrt(sum((ys[i] - mean) ** 2 for i in range(n - lag)) * sum((ys[i + lag] - mean) ** 2 for i in range(n - lag))) + 1e-9
		c = num / den
		if c > best + 0.02:
			best, best_lag = c, lag
		if best > 0.9 and c < best - 0.2:
			break
	return best_lag * step, best


def strike_time(track, period):
	"""The left foot's strike in the first period: where the left ankle is furthest forward (-Y) relative to the hips."""
	best, bt = 1e9, 0.0
	for t, la, ra, hp in track:
		if t > period:
			break
		y = la.y - hp.y
		if y < best:
			best, bt = y, t
	return bt


def stance_speed(track, period, t0):
	"""The body's speed implied by a planted foot sliding back under the hips (an in-place clip): the left ankle's
	backward speed relative to the hips over its lowest third."""
	pts = [(t, la.y - hp.y, la.z) for t, la, ra, hp in track if t0 <= t <= t0 + period]
	zmin = min(p[2] for p in pts)
	zmax = max(p[2] for p in pts)
	low = [p for p in pts if p[2] < zmin + (zmax - zmin) * 0.25]
	if len(low) < 3:
		return 0.0
	low.sort()
	speeds = []
	for a, b in zip(low, low[1:]):
		if b[0] - a[0] < 0.05:
			speeds.append((b[1] - a[1]) / (b[0] - a[0]))
	speeds.sort()
	return speeds[len(speeds) // 2] if speeds else 0.0


def flight(track):
	"""A jump's flight: the longest stretch with both ankles more than 8 cm over their lowest (a run's strides fly
	for a tenth of a second, a jump for half of one) — its start and end (s), (0, 0) if none."""
	z0 = min(min(a[1].z, a[2].z) for a in track)
	best, cur = (0.0, 0.0), None
	for t, la, ra, hp in track:
		up = la.z > z0 + 0.08 and ra.z > z0 + 0.08
		if up and cur is None:
			cur = t
		if (not up or t == track[-1][0]) and cur is not None:
			if t - cur > best[1] - best[0]:
				best = (cur, t)
			cur = None
	return best


def leave_ground(track):
	return flight(track)[0]


def touch_down(track):
	"""The first time after the feet's highest that an ankle comes within 3 cm of its lowest (a landing), else 0."""
	z0 = min(min(a[1].z, a[2].z) for a in track)
	high = max(range(len(track)), key=lambda i: min(track[i][1].z, track[i][2].z))
	for t, la, ra, hp in track[high:]:
		if min(la.z, ra.z) < z0 + 0.03:
			return t
	return 0.0


def blend_snapshots(a, b, w):
	"""Two of the rig's snapshots mixed: b's share w."""
	out = {}
	for n in a:
		la, qa = a[n]
		lb, qb = b[n]
		out[n] = (la.lerp(lb, w), qa.slerp(qb, w))
	return out


def smooth_step(x):
	x = min(1.0, max(0.0, x))
	return x * x * (3.0 - 2.0 * x)


SEAM = 0.4  # s: a loop that is not a cycle (an idle, a fall) has its end crossfaded into its start


def bake_clip(rig, src, name, kind, scale, ankle_dz, info, opt=None):
	opt = opt or {}
	track = ankle_track(src, 1.0 / SRC_FPS)
	start, length, loop, play = 0.0, src.length, False, 1.0
	seam = 0.0
	if kind in ("settle", "span"):
		start = opt["from"]
		length = min(src.length, opt["to"]) - start
		loop = kind == "settle"
		info["clips"][name] = {"kind": kind, "from": start, "to": start + length}
	elif kind == "root":
		start = opt.get("from", 0.0)
		length = min(src.length, opt.get("to", src.length)) - start
		info["clips"][name] = {"kind": kind, "from": start, "to": start + length}
	elif kind == "gait":
		# Mixamo's in-place gaits loop as they are: one cycle (two steps), or several (a shorter period repeating).
		period, corr = src.length, 1.0
		if src.length > 1.6:
			p, c = find_period(track)
			if c > 0.9 and p < src.length * 0.6:
				period, corr = p, c
		loop = True
		length = period
		start = strike_time(track, period)
		v = abs(stance_speed(track, period, start))
		info["gaits"][name] = {"period": period, "speed": v, "stride": v * period, "repeat": corr}
		play = period  # a second of the clip is a stride
		info["clips"][name] = {"kind": kind, "period": period, "repeat": corr}
	elif kind in ("loop", "loop_tail"):
		loop = True
		start = src.length * 0.4 if kind == "loop_tail" else 0.0
		seam = min(SEAM, (src.length - start) * 0.25)
		length = src.length - start - seam
		info["clips"][name] = {"kind": kind, "seam": seam, "from": start}
	elif kind == "takeoff":
		t, t_land = flight(track)
		start = max(0.0, t - 0.12)
		# To a little past the apex: the player's fall takes it from there.
		air = [a for a in track if t <= a[0] <= max(t_land, t + 0.1)]
		apex = max(air, key=lambda a: a[3].z)[0] if air else t
		length = min(src.length - start, apex - start + 0.25)
		info["clips"][name] = {"kind": kind, "takeoff": t, "apex": apex, "from": start}
	elif kind == "land":
		t = touch_down(track)
		start = max(0.0, t - 0.05)
		length = src.length - start
		info["clips"][name] = {"kind": kind, "touchdown": t, "from": start}
	elif kind == "anchored":
		# A hop's wind-up, flight and catch (its first fifth and last quarter hang still): the traversal plays it over the
		# move's time, which the whole clip would have sped up three times (the head at 2,300°/s). Milestone 196: or the
		# span given (`from`, `to`: a half of a climbing cycle, a shimmy's step).
		start = opt.get("from", src.length * 0.2)
		length = (min(src.length, opt["to"]) - start) if "to" in opt else src.length * 0.55
		info["clips"][name] = {"kind": kind, "from": start}
	else:
		info["clips"][name] = {"kind": kind}
	# The flight's height taken out (the player's body rises and falls; the clip's hips stay at their takeoff's or
	# their touchdown's height in the air, the legs and arms moving about them).
	hz = {round(a[0] * SRC_FPS): a[3].z for a in track}

	def hips_z(t):
		return hz.get(round(t * SRC_FPS), track[-1][3].z)

	lift = None
	if kind == "takeoff":
		t_off = info["clips"][name]["takeoff"]
		lift = lambda t: max(0.0, hips_z(t) - hips_z(t_off)) if t > t_off else 0.0  # noqa: E731
	elif kind == "land":
		t_on = info["clips"][name]["touchdown"]
		lift = lambda t: max(0.0, hips_z(t) - hips_z(t_on)) if t < t_on else 0.0  # noqa: E731
	# The centre the hips move about, in place (the player's body moves the character): a loop's drift taken out (a
	# straight line through its hips), a one-shot's travel (a clip Mixamo has no "in place" for, a roll's) as its hips'
	# average over half a second about each moment — their sway kept.
	hips = [(a[0], a[3]) for a in track if start - 0.3 <= a[0] <= start + length + 0.3]
	root_at = None
	if kind == "root":
		# The root (the body's feet, where the traversal moves the body) under the hips by an offset eased from the start's
		# (the hang's, or the pose's own: the hips over the ankles) to the end's (the pose's own): the hips' whole travel
		# is the root's, less the change of the hips' place over the feet.
		h0 = src.sample(start)["Hips"].translation
		centre_fixed = Vector((h0.x, h0.y, 0.0))

		def centre_at(t):
			return centre_fixed

		def own_offset(t):
			ps = src.sample(t)
			h = ps["Hips"].translation
			a = (ps["LeftFoot"].translation + ps["RightFoot"].translation) * 0.5
			return Vector((0.0, (h.y - a.y) * scale, (h.z - a.z) * scale + rig.head["LeftFoot"].z))

		off0 = Vector((0.0, opt["start_offset"][0], opt["start_offset"][1])) if "start_offset" in opt else own_offset(start)
		off1 = own_offset(start + length)
		info["clips"][name]["offsets"] = [list(off0), list(off1)]

		def root_at(t):
			ps = src.sample(t)
			h = ps["Hips"].translation
			want = Vector(((h.x - centre_fixed.x) * scale, (h.y - centre_fixed.y) * scale, h.z * scale))
			if opt.get("root") == "feet":
				# (over an obstacle: under the hips, at the lower foot's height over the line from the take-off's to the
				# landing's — on the ground at either end, over the top as the legs swing across it)
				def low_at(tt):
					pp = src.sample(tt)
					return min(pp["LeftFoot"].translation.z, pp["RightFoot"].translation.z) * scale + ankle_dz - rig.head["LeftFoot"].z

				u = max(0.0, min(1.0, (t - start) / max(length, 1e-3)))
				base = low_at(start) * (1.0 - u) + low_at(start + length) * u
				return Vector((want.x, want.y, max(0.0, low_at(t) - base)))
			u = smooth_step(max(0.0, min(1.0, (t - start) / max(length, 1e-3))))
			return want - off0.lerp(off1, u)
	elif kind in ("settle", "span"):
		inside = [h[1] for h in hips if start <= h[0] <= start + length] or [hips[0][1]]
		cx = sum(h.x for h in inside) / len(inside)
		cy = sum(h.y for h in inside) / len(inside)

		def centre_at(t):
			return Vector((cx, cy, 0.0))
	elif loop:
		ts = [h[0] for h in hips]
		tm = sum(ts) / len(ts)
		den = sum((x - tm) ** 2 for x in ts) or 1.0
		mx = sum(h[1].x for h in hips) / len(hips)
		my = sum(h[1].y for h in hips) / len(hips)
		mz = sum(h[1].z for h in hips) / len(hips)
		bx = sum((h[0] - tm) * (h[1].x - mx) for h in hips) / den
		by = sum((h[0] - tm) * (h[1].y - my) for h in hips) / den
		bz = sum((h[0] - tm) * (h[1].z - mz) for h in hips) / den if kind != "gait" else 0.0  # the gaits as milestone 192's

		info["clips"][name]["drift"] = [round(bx, 3), round(by, 3), round(bz, 3)]

		def centre_at(t):
			# (the height's drift too: a climb up a wall in place)
			return Vector((mx + bx * (t - tm), my + by * (t - tm), bz * (t - tm)))
	elif kind == "anchored":
		h0 = min(hips, key=lambda h: abs(h[0] - start))[1]

		def centre_at(t):
			h = min(hips, key=lambda x: abs(x[0] - t))[1]
			return Vector((h.x, h.y, h.z - h0.z))
	else:

		def centre_at(t):
			near = [h[1] for h in hips if abs(h[0] - t) <= 0.25] or [min(hips, key=lambda h: abs(h[0] - t))[1]]
			return Vector((sum(h.x for h in near) / len(near), sum(h.y for h in near) / len(near), lift(t) if lift else 0.0))
	if name in HIPS_AT and kind != "root":
		# The hips' mean over the cut moved to the mannequin's own (the same offset to every joint).
		ty, tz = HIPS_AT[name]
		ts = [start + length * k / 16.0 for k in range(17)]
		hy = sum(src.sample(t)["Hips"].translation.y - centre_at(t).y for t in ts) / len(ts)
		hz = sum(src.sample(t)["Hips"].translation.z - centre_at(t).z for t in ts) / len(ts)
		off = Vector((0.0, hy - ty / scale, hz - tz / scale))
		base_centre = centre_at

		def centre_at(t):
			return base_centre(t) + off
	out_len = 1.0 if kind == "gait" else (opt["length"] if kind == "settle" else length)
	n = max(2, int(round(out_len * FPS)))
	frames = []
	for i in range(n if loop else n + 1):
		u = min(out_len, i / FPS) / out_len
		t = start + u * length
		if kind == "settle":
			t = start + length * (0.5 - 0.5 * math.cos(2.0 * math.pi * u))
		elif loop:
			t = start + math.fmod(u * length, length)
			if t > src.length:
				t -= length
		retarget(rig, src, min(t, src.length), scale, ankle_dz, centre_at(t), root_at(t) if root_at else None)
		snap = rig.snapshot()
		if seam > 0.0 and t < start + seam:
			# The loop's start: from where its end goes on (t + length) into the clip's own (t).
			retarget(rig, src, min(t + length, src.length), scale, ankle_dz, centre_at(t + length))
			snap = blend_snapshots(rig.snapshot(), snap, smooth_step((t - start) / seam))
		frames.append(snap)
	debug = os.environ.get("MIXAMO_DEBUG")
	if debug:
		# The source's joints (scaled about the centre) and the mannequin's at eight moments, for a look (stick figures).
		dump = []
		for k in range(8):
			t = start + length * k / 8.0
			centre = centre_at(t)
			pose = retarget(rig, src, min(t, src.length), scale, ankle_dz, centre)
			cache = rig.fk()
			sj = {m: [(p.translation.x - centre.x) * scale, (p.translation.y - centre.y) * scale, (p.translation.z - centre.z) * scale] for m, p in pose.items()}
			tj = {b: list(m.translation) for b, m in cache.items()}
			dump.append({"t": t, "src": sj, "tgt": tj})
		os.makedirs(debug, exist_ok=True)
		with open(os.path.join(debug, name + ".json"), "w") as f:
			json.dump({"map": MAP, "parent": rig.parent, "frames": dump}, f)
	write_action(rig, name, frames, loop)
	if "leap" in opt:
		# Milestone 198: the leap's timing (60 samples a second over the cut): the hips' way along the move's axis (0 at
		# the start, 1 at the end), their lift over the straight line from the start's height to the end's (m, at the
		# mannequin's scale; for a leap up, over the end's height), the hands' let-go (10 cm from where they held)
		# and catch (slowed under 0.6 m/s after their fastest) as shares of the time — the leading hand first.
		axis = opt["leap"]
		m = max(2, int(round(length * FPS)) + 1)
		ts = [start + length * k / (m - 1) for k in range(m)]
		hp = [src.sample(t)["Hips"].translation for t in ts]
		a0, a1 = getattr(hp[0], axis), getattr(hp[-1], axis)
		span = (a1 - a0) if abs(a1 - a0) > 1e-4 else 1.0
		progress = [round((getattr(h, axis) - a0) / span, 4) for h in hp]
		if axis == "z":
			# (up: the way to the end's height, then the rise over it in metres — a higher leap goes no further over)
			lift = [round(max(0.0, h.z - hp[-1].z) * scale, 4) for h in hp]
			progress = [min(1.0, p) for p in progress]
		else:
			lift = [round((h.z - (hp[0].z + (hp[-1].z - hp[0].z) * k / (m - 1))) * scale, 4) for k, h in enumerate(hp)]
		times = {}
		dt = length / (m - 1)
		for side in ("Left", "Right"):
			hs = [src.sample(t)[side + "Hand"].translation for t in ts]
			sp = [(hs[min(m - 1, k + 1)] - hs[max(0, k - 1)]).length * scale / (dt * (min(m - 1, k + 1) - max(0, k - 1))) for k in range(m)]
			# (let go: 10 cm from where it held; caught: slowed under 0.6 m/s after its fastest)
			rel = next((k for k in range(m) if (hs[k] - hs[0]).length * scale > 0.10), 0)
			fast = max(range(rel, m), key=lambda k: sp[k])
			cat = next((k for k in range(fast, m) if sp[k] < 0.6), m - 1)
			times[side] = (rel / (m - 1), cat / (m - 1))
		# (the leading hand: along or across, the one on the side it goes to — the traversal's lead; up, the first to go)
		if axis == "z":
			lead, trail = ("Left", "Right") if times["Left"][0] <= times["Right"][0] else ("Right", "Left")
		else:
			p0 = src.sample(ts[0])
			side_l = getattr(p0["LeftHand"].translation, axis) - getattr(p0["Hips"].translation, axis)
			lead, trail = ("Left", "Right") if side_l * (a1 - a0) > 0.0 else ("Right", "Left")
		info["clips"][name]["leap"] = {"axis": axis, "time": length, "reach": round(abs(a1 - a0) * scale, 4), "progress": progress, "lift": lift,
				"hands": [round(times[lead][0], 3), round(times[lead][1], 3), round(times[trail][0], 3), round(times[trail][1], 3)],
				"lead": lead}
	if kind == "travel":
		# The body's speed forward (−Y, scaled) as the hips' third-of-a-second average moves (at Mixamo's samples, a
		# central difference over a tenth of a second), every 60th of a second.
		ys = [a[3].y for a in track]
		step = 1.0 / SRC_FPS
		w = max(1, int(round(0.17 / step)))
		avg = [sum(ys[max(0, i - w) : i + w + 1]) / len(ys[max(0, i - w) : i + w + 1]) for i in range(len(ys))]
		vel = [-(avg[min(len(avg) - 1, i + 2)] - avg[max(0, i - 2)]) / ((min(len(avg) - 1, i + 2) - max(0, i - 2)) * step) * scale for i in range(len(avg))]
		travel = []
		for i in range(n + 1):
			f = (start + min(out_len, i / FPS)) / step
			k = min(len(vel) - 2, int(f))
			travel.append(round(vel[k] + (vel[k + 1] - vel[k]) * (f - k), 3))
		info["clips"][name]["travel"] = travel
	info["clips"][name].update({"length": out_len, "loop": loop, "source_length": src.length})
	return play


def main():
	argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else sys.argv[1:]
	src_dir = os.path.abspath(argv[0])
	out = os.path.abspath(argv[1] if len(argv) > 1 else "mocap")
	os.makedirs(out, exist_ok=True)
	addon_utils.enable("io_scene_fbx", default_set=True)
	files = {os.path.splitext(f)[0].lower(): os.path.join(src_dir, f) for f in os.listdir(src_dir) if f.lower().endswith(".fbx")}
	bpy.ops.wm.read_factory_settings(use_empty=True)
	scene = bpy.context.scene
	scene.render.fps = SRC_FPS
	arm = build_armature()
	body = build_mesh(arm)
	rig = Rig(arm)
	info = {"gaits": {}, "clips": {}, "files": {}}
	end = 1
	for entry in CLIPS:
		name, choices, kind = entry[:3]
		opt = entry[3] if len(entry) > 3 else None
		path = next((files[c] for c in choices if c in files), None)
		if path is None:
			continue
		src_arm, objs = import_fbx(path)
		src = Source(src_arm)
		# The scale: the hips' heights at rest; the ankles' rest heights matched.
		scale = rig.head["Hips"].z / src.rest["Hips"].translation.z
		ankle_dz = rig.head["LeftFoot"].z - src.rest["LeftFoot"].translation.z * scale
		bake_clip(rig, src, name, kind, scale, ankle_dz, info, opt)
		info["files"][name] = os.path.basename(path)
		act = bpy.data.actions.get(name)
		end = max(end, int(act.frame_range[1]))
		print(f"{name}: {os.path.basename(path)} {info['clips'][name]}", flush=True)
		for o in objs:
			bpy.data.objects.remove(o, do_unlink=True)
	if not info["clips"]:
		print("no Mixamo clips found in", src_dir)
		return
	scene.render.fps = FPS
	scene.frame_start = 1
	scene.frame_end = end
	bpy.ops.object.select_all(action="DESELECT")
	arm.select_set(True)
	body.select_set(True)
	bpy.context.view_layer.objects.active = arm
	bpy.ops.export_scene.gltf(filepath=os.path.join(out, "Mocap.glb"), export_format="GLB", use_selection=True,
			export_yup=True, export_skins=True, export_animations=True, export_animation_mode="NLA_TRACKS",
			export_force_sampling=True, export_frame_step=1, export_def_bones=False, export_optimize_animation_size=False,
			export_anim_slide_to_zero=True, export_bake_animation=False, export_reset_pose_bones=True,
			export_rest_position_armature=True)
	with open(os.path.join(out, "clips.json"), "w") as f:
		json.dump(info, f, indent=1)


main()
