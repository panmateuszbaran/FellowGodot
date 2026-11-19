# FellowEngine showcase: the owner's reference video's performance on the mannequin (milestone 157) — a grey mannequin
# running across a stone square, leaping at a house, up its front by a window's balconette, hanging from the eave and
# over it onto the tiles, 4 seconds. reference_performance.json holds it as it was measured: the mannequin's skeleton
# fitted to every frame of the video (24 fps) — each body segment's rotation, the hips' place — the camera that filmed
# it, the hands' and feet's holds and where they go on the replica house (ac_buildings.casa_riferimento), all in the
# house model's frame. This module makes from it the clip "reference" at FPS (the segments' rotations and the hips'
# path interpolated by Catmull-Rom splines; each hold kept still on its stone by a two-bone IK eased in and out) and
# the camera's path, for ReferenceShot.glb.
#
# The data came from a pose estimate (MediaPipe Pose, run once outside the repository); nothing here depends on it.

import json
import math
import os

import bpy
from mathutils import Matrix, Quaternion, Vector

from mannequin_rig import FPS

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "reference_performance.json")

SEGS = ["p", "t", "h", "uaL", "faL", "hdL", "uaR", "faR", "hdR", "thL", "shL", "ftL", "thR", "shR", "ftR"]
# A hold's point: the bone whose head the IK moves (the wrist, the ankle) and the point on the hand or foot that holds
# (in that bone's rest-aligned axes, from its head).
HOLD = {
	"LH": ("LeftHand", Vector((0.085, 0.0, 0.0))),
	"RH": ("RightHand", Vector((-0.085, 0.0, 0.0))),
	"LFh": ("LeftFoot", Vector((0.0, 0.056, -0.06))),
	"LFt": ("LeftFoot", Vector((0.005, -0.204, -0.073))),
	"RFh": ("RightFoot", Vector((0.0, 0.056, -0.06))),
	"RFt": ("RightFoot", Vector((-0.005, -0.204, -0.073))),
}
EASE = 2.0  # video frames over which a hold's IK eases in before it and out after it
SMOOTH = 3  # the holds' corrections smoothed over time: a Gaussian of SMOOTH frames (at FPS)


def load(path=DATA):
	with open(path) as f:
		return json.load(f)


def _q(v):
	return Quaternion((v[0], v[1], v[2], v[3]))


def _catmull(p0, p1, p2, p3, t):
	t2 = t * t
	t3 = t2 * t
	return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)


def _sample(seq, u):
	"""A Catmull-Rom spline through seq (lists of floats, one per video frame) at u (frames, fractional)."""
	n = len(seq)
	i = max(0, min(n - 2, int(math.floor(u))))
	t = min(1.0, max(0.0, u - i))
	idx = [max(0, min(n - 1, k)) for k in (i - 1, i, i + 1, i + 2)]
	return [_catmull(*(seq[k][c] for k in idx), t) for c in range(len(seq[0]))]


def _quat_seq(seq):
	"""Each quaternion on the side of the one before it."""
	out = []
	for v in seq:
		q = list(v)
		if out and sum(a * b for a, b in zip(q, out[-1])) < 0.0:
			q = [-a for a in q]
		out.append(q)
	return out


def _sample_q(seq, u):
	q = Quaternion(_sample(seq, u))
	q.normalize()
	return q


def _frac(q, t):
	return Quaternion().slerp(q, t)


def _swing_twist(q, axis):
	a = Vector(axis).normalized()
	p = Vector((q.x, q.y, q.z)).dot(a) * a
	tw = Quaternion((q.w, p.x, p.y, p.z))
	if tw.magnitude < 1e-9:
		return q.copy(), Quaternion()
	tw.normalize()
	return q @ tw.inverted(), tw


def _grip(rig, pre, sx, g):
	for fn, k in (("Index", 1.0), ("Middle", 1.05), ("Ring", 1.1), ("Little", 1.15)):
		rig.set_arm_rot(pre + fn + "Proximal", Quaternion(Vector((0, 1, 0)), math.radians(sx * 70.0 * g * k)))
		rig.set_arm_rot(pre + fn + "Intermediate", Quaternion(Vector((0, 1, 0)), math.radians(sx * 85.0 * g * k)))
	rig.set_arm_rot(pre + "ThumbMetacarpal", Quaternion(Vector((0, 0, 1)), math.radians(-sx * 20.0 * g)) @ Quaternion(Vector((0, 1, 0)), math.radians(sx * 25.0 * g)))
	rig.set_arm_rot(pre + "ThumbProximal", Quaternion(Vector((0, 1, 0)), math.radians(sx * 40.0 * g)))


def _twist_angle(q, axis):
	"""The signed angle of q's twist about axis (-pi..pi)."""
	a = Vector(axis).normalized()
	p = Vector((q.x, q.y, q.z)).dot(a)
	return 2.0 * math.atan2(p, q.w)


def forearm_twists(seq):
	"""Per frame and side, the hand's twist about the forearm, unwrapped over the frames (taken from it at a fraction, a
	twist crossing half a turn had swung the forearm round in a frame). seq: [segq] -> [(left, right)] radians."""
	out = []
	prev = [None, None]
	for segq in seq:
		row = []
		for k, (s, sx) in enumerate((("L", 1.0), ("R", -1.0))):
			a = _twist_angle(segq["fa" + s].inverted() @ segq["hd" + s], (sx, 0, 0))
			if prev[k] is not None:
				while a - prev[k] > math.pi:
					a -= 2.0 * math.pi
				while a - prev[k] < -math.pi:
					a += 2.0 * math.pi
			prev[k] = a
			row.append(a)
		out.append(tuple(row))
	return out


def pose_segments(rig, segq, hips_pos, grip, twists=None):
	"""segq: each segment's rotation in the armature's axes (rest-aligned); the trunk's turn spread over Spine, Chest and
	UpperChest, the head's over Neck and Head, the hand's twist about the forearm (twists: forearm_twists's) shared with
	the forearm."""
	rig.reset()
	cache = rig.fk()
	base = cache["Root"] @ rig.rest_rel["Hips"]
	rig.loc["Hips"] = base.to_quaternion().inverted() @ (hips_pos - base.translation)

	def setg(name, q):
		rig.set_global_rot(name, q @ rig.rest_q[name], rig.fk())

	qp, qt, qh = segq["p"], segq["t"], segq["h"]
	setg("Hips", qp)
	rel = qp.inverted() @ qt
	for n, share in (("Spine", 0.3), ("Chest", 0.65), ("UpperChest", 1.0)):
		setg(n, qp @ _frac(rel, share))
	setg("Neck", qt @ _frac(qt.inverted() @ qh, 0.45))
	setg("Head", qh)
	for s, sx in (("L", 1.0), ("R", -1.0)):
		pre = "Left" if s == "L" else "Right"
		setg(pre + "Shoulder", qt)
		setg(pre + "UpperArm", segq["ua" + s])
		qfa, qhd = segq["fa" + s], segq["hd" + s]
		if twists is not None:
			ang = twists[0 if s == "L" else 1]
		else:
			ang = _twist_angle(qfa.inverted() @ qhd, (sx, 0, 0))
		setg(pre + "LowerArm", qfa @ Quaternion(Vector((sx, 0, 0)), 0.6 * ang))
		setg(pre + "Hand", qhd)
		_grip(rig, pre, sx, grip[0 if s == "L" else 1])
		setg(pre + "UpperLeg", segq["th" + s])
		setg(pre + "LowerLeg", segq["sh" + s])
		setg(pre + "Foot", segq["ft" + s])


def _hold_point(rig, cache, point):
	bone, off = HOLD[point]
	m = cache[bone]
	r = rig.rest[bone].to_quaternion()
	# the bone's rotation in rest-aligned axes carries the offset
	q = m.to_quaternion() @ r.inverted()
	return m.translation + q @ off


def _limb(point):
	if point in ("LH", "RH"):
		pre = "Left" if point[0] == "L" else "Right"
		return pre + "UpperArm", pre + "LowerArm", pre + "Hand", (0, 1, 0)
	pre = "Left" if point[0] == "L" else "Right"
	return pre + "UpperLeg", pre + "LowerLeg", pre + "Foot", (0, -1, 0)


def hold_weights(data, u):
	"""Each hold's weight at u (video frames, 0-based): 1 within its frames, eased over EASE frames each side; a heel's or
	a toe's on the ground only while it is the low one (the foot rolls)."""
	out = []
	for c in data["contacts"]:
		a, b = c["from"] - 1, c["to"] - 1
		if u < a - EASE or u > b + EASE:
			continue
		w = 1.0
		if u < a:
			w = 1.0 - (a - u) / EASE
		elif u > b:
			w = 1.0 - (u - b) / EASE
		w = w * w * (3.0 - 2.0 * w)
		out.append((c, w))
	return out


def hold_corrections(rig, data, u):
	"""Per limb, how far its hand or foot should move for its holds to stay still (the weighted mean of its holds'
	corrections, a heel's and a toe's on the ground weighted by which is lower), already scaled by the holds' weight:
	{(upper, lower, end, rest_pole): Vector}."""
	cache = rig.fk()
	by_limb = {}
	for c, w in hold_weights(data, u):
		p = c["point"]
		cur = _hold_point(rig, cache, p)
		corr = Vector(c["target"]) - cur
		if c["kind"] == "ground":
			other = {"LFh": "LFt", "LFt": "LFh", "RFh": "RFt", "RFt": "RFh"}[p]
			dz = cur.z - _hold_point(rig, cache, other).z
			w *= math.exp(-max(0.0, dz) / 0.025)
		by_limb.setdefault(_limb(p), []).append((corr, w))
	out = {}
	for limb, items in by_limb.items():
		ws = sum(w for _, w in items)
		if ws < 1e-4:
			continue
		out[limb] = sum((c * w for c, w in items), Vector()) / ws * min(1.0, ws)
	return out


def move_limb(rig, limb, corr):
	"""The limb's end (the wrist, the ankle) moved by corr with a two-bone IK, its elbow or knee bent the way it was."""
	upper, lower, end, rest_pole = limb
	cache = rig.fk()
	end_q = cache[end].to_quaternion()
	root = cache[upper].translation
	joint = cache[lower].translation
	target = cache[end].translation + corr
	line = (target - root).normalized()
	# the bend's direction as the pose had it (across its own line from the shoulder or hip to the wrist or ankle: the
	# moved target's line can pass by the elbow and turn it over), then across the new line
	raw = (cache[end].translation - root).normalized()
	pole = (joint - root) - raw * (joint - root).dot(raw)
	pole = pole - line * pole.dot(line)
	# A limb nearly straight bends the way it bent the frame before (its elbow or knee had turned over: an arm reaching
	# straight up for the eave 160 degrees in a frame).
	memory = rig.__dict__.setdefault("_poles", {})
	prev = memory.get(upper)
	if prev is not None:
		prev_p = prev - line * prev.dot(line)
		k = min(1.0, pole.length / 0.04)
		if prev_p.length > 1e-6:
			pole = pole * k + prev_p.normalized() * (1.0 - k) * max(pole.length, 1e-3)
			if pole.dot(prev_p) < 0.0 and k < 1.0:
				pole = prev_p
	if pole.length < 1e-6:
		pole = Vector(rest_pole)
	pole.normalize()
	memory[upper] = pole
	rig.ik(upper, lower, end, target, pole, rest_pole)
	if end.endswith("Foot"):
		# a foot keeps its turn in the world (flat on the ground, its toes on the wall); a hand its turn on the forearm
		# (the wrist as the pose had it: kept in the world, a hand moved far had twisted its wrist round)
		rig.set_global_rot(end, end_q, rig.fk())


def bake(rig, data, name="reference"):
	"""The clip at FPS (one snapshot a frame) and the camera's path: [(location, rotation)] at the same frames. The
	holds' corrections are smoothed over time before the IK applies them (eased in and out over SMOOTH frames: a hold
	taken or let go moves its hand or foot over a twentieth of a second, not in a frame)."""
	n = data["frames"]
	segs = {s: _quat_seq(data["segments"][s]) for s in SEGS}
	hips = data["hips"]
	grip = data["grip"]
	cam_p = data["camera_pos"]
	cam_q = _quat_seq(data["camera_rot"])
	length = (n - 1) / data["fps"]
	count = int(round(length * FPS)) + 1
	poses = []
	corrs = []
	for i in range(count):
		u = i / FPS * data["fps"]
		poses.append(({s: _sample_q(segs[s], u) for s in SEGS}, Vector(_sample(hips, u)), _sample(grip, u)))
	twists = forearm_twists([p[0] for p in poses])
	poses = [p + (tw,) for p, tw in zip(poses, twists)]
	for i in range(count):
		pose_segments(rig, *poses[i])
		corrs.append(hold_corrections(rig, data, i / FPS * data["fps"]))
	limbs = set()
	for c in corrs:
		limbs |= set(c)
	smooth = {}
	for limb in limbs:
		raw = [c.get(limb, Vector()) for c in corrs]
		out = []
		for i in range(count):
			acc = Vector()
			ws = 0.0
			for k in range(max(0, i - 3 * SMOOTH), min(count, i + 3 * SMOOTH + 1)):
				w = math.exp(-0.5 * ((k - i) / SMOOTH) ** 2)
				acc += raw[k] * w
				ws += w
			out.append(acc / ws)
		smooth[limb] = out
	frames, cams = [], []
	for i in range(count):
		pose_segments(rig, *poses[i])
		for limb in sorted(limbs):
			c = smooth[limb][i]
			if c.length > 1e-4:
				move_limb(rig, limb, c)
		frames.append(rig.snapshot())
		cams.append((Vector(_sample(cam_p, i / FPS * data["fps"])), _sample_q(cam_q, i / FPS * data["fps"])))
	return frames, cams, length


def smooth_camera(data, sigma=1.5):
	"""The camera's path smoothed (its place each frame came through the fit's depth, which jitters a little; the
	reference's camera moves as a crane does)."""
	pos = data["camera_pos"]
	n = len(pos)
	out = []
	for i in range(n):
		acc = [0.0, 0.0, 0.0]
		ws = 0.0
		for k in range(max(0, i - 6), min(n, i + 7)):
			w = math.exp(-0.5 * ((k - i) / sigma) ** 2)
			ws += w
			for c in range(3):
				acc[c] += w * pos[k][c]
		out.append([a / ws for a in acc])
	data["camera_pos"] = out
	return data


def camera_object(data, cams, name="ReferenceCamera"):
	"""The reference's camera: its lens (the video's horizontal field of view, 16:9) and its path keyed every frame."""
	cd = bpy.data.cameras.new(name)
	cd.sensor_fit = "HORIZONTAL"
	cd.sensor_width = 36.0
	cd.lens = 18.0 / math.tan(math.radians(data["camera_hfov_deg"]) * 0.5)
	cd.clip_start = 0.05
	cd.clip_end = 2000.0
	obj = bpy.data.objects.new(name, cd)
	bpy.context.scene.collection.objects.link(obj)
	obj.rotation_mode = "QUATERNION"
	act = bpy.data.actions.new(name + "_reference")
	obj.animation_data_create()
	obj.animation_data.action = act
	prev = None
	for i, (p, q) in enumerate(cams):
		if prev is not None and q.dot(prev) < 0.0:
			q = -q
		prev = q
		obj.location = p
		obj.rotation_quaternion = q
		obj.keyframe_insert("location", frame=i + 1)
		obj.keyframe_insert("rotation_quaternion", frame=i + 1)
	for fc in act.fcurves:
		for kp in fc.keyframe_points:
			kp.interpolation = "LINEAR"
	track = obj.animation_data.nla_tracks.new()
	track.name = "reference"
	strip = track.strips.new("reference", 1, act)
	strip.name = "reference"
	obj.animation_data.action = None
	return obj
