# FellowEngine showcase: posing the mannequin by code — forward kinematics in rest-aligned axes, two-bone IK for the
# arms and the legs, and actions written from poses sampled per frame. Used by mannequin_anims.py.
#
# A pose is a dict of parameters (see Rig.build): the root's offset, the hips' offset and turn, the spine's lean, twist
# and side bend, the head's look, each arm and leg either by angles ("fk") or by a target for its wrist or ankle ("ik",
# in the root's frame), the hands' and feet's angles, the fingers' curl. Clips are poses sampled at FPS; keyed clips
# interpolate the parameters between keys.

import math

import bpy
from mathutils import Matrix, Quaternion, Vector

FPS = 60


# Soft IK (milestone 155, docs/showcase_animation_quality.md): past SOFT of its reach a chain's target is eased towards
# FULL of it instead of being cut at the reach, so that a limb never snaps straight — a target at the reach bends the
# knee about 11°, one far beyond it 9°; one within SOFT of it is reached exactly (the knee bent 16° or more).
SOFT = 0.99
FULL = 0.997


def soft_reach(d, reach):
	s = SOFT * reach
	if d <= s:
		return d
	top = FULL * reach
	return s + (top - s) * (1.0 - math.exp(-(d - s) / (top - s)))


def Q(axis, deg):
	return Quaternion(Vector(axis), math.radians(deg))


def Rx(d):
	return Q((1, 0, 0), d)


def Ry(d):
	return Q((0, 1, 0), d)


def Rz(d):
	return Q((0, 0, 1), d)


def smooth(t):
	t = min(1.0, max(0.0, t))
	return t * t * (3.0 - 2.0 * t)


def smoother(t):
	t = min(1.0, max(0.0, t))
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


class Rig:
	def __init__(self, obj):
		self.obj = obj
		self.names = [b.name for b in obj.data.bones]
		self.parent = {b.name: (b.parent.name if b.parent else None) for b in obj.data.bones}
		self.rest = {b.name: b.matrix_local.copy() for b in obj.data.bones}
		self.rest_q = {n: m.to_quaternion() for n, m in self.rest.items()}
		self.rest_rel = {}
		for n in self.names:
			p = self.parent[n]
			self.rest_rel[n] = (self.rest[p].inverted() @ self.rest[n]) if p else self.rest[n].copy()
		self.head = {n: self.rest[n].translation.copy() for n in self.names}
		self.length = {b.name: b.length for b in obj.data.bones}
		self.reset()

	def reset(self):
		self.loc = {n: Vector() for n in self.names}
		self.rot = {n: Quaternion() for n in self.names}

	# -------------------------------------------------------------- kinematics

	def matrix(self, name, cache):
		if name in cache:
			return cache[name]
		p = self.parent[name]
		base = (self.matrix(p, cache) @ self.rest_rel[name]) if p else self.rest_rel[name]
		m = base @ Matrix.Translation(self.loc[name]) @ self.rot[name].to_matrix().to_4x4()
		cache[name] = m
		return m

	def fk(self):
		cache = {}
		for n in self.names:
			self.matrix(n, cache)
		return cache

	def set_arm_rot(self, name, q_arm):
		"""The bone turned by q_arm about its head, in axes that are the armature's at rest (carried by its parent)."""
		r = self.rest_q[name]
		self.rot[name] = r.inverted() @ q_arm @ r

	def set_global_rot(self, name, g, cache):
		"""The bone's rotation in the armature's space (its parent already posed)."""
		p = self.parent[name]
		base = (cache[p] @ self.rest_rel[name]) if p else self.rest_rel[name]
		self.rot[name] = base.to_quaternion().inverted() @ g

	def ik(self, upper, lower, end, target, pole_dir, rest_pole):
		"""Two bones (upper, lower) reaching end's head to target; the joint between them bent towards pole_dir (both in
		the armature's space). rest_pole: where the joint points at rest (the hinge's axis from it)."""
		cache = self.fk()
		S = cache[upper].translation.copy()
		L1 = (self.head[lower] - self.head[upper]).length
		L2 = (self.head[end] - self.head[lower]).length
		T = Vector(target)
		d_vec = T - S
		d = d_vec.length
		d = max(soft_reach(d, L1 + L2), abs(L1 - L2) + 1e-4)
		u = d_vec.normalized()
		w = Vector(pole_dir) - u * Vector(pole_dir).dot(u)
		if w.length < 1e-6:
			w = u.orthogonal()
		w.normalize()
		ca = (L1 * L1 + d * d - L2 * L2) / (2.0 * L1 * d)
		ca = min(1.0, max(-1.0, ca))
		sa = math.sqrt(1.0 - ca * ca)
		E = S + (u * ca + w * sa) * L1
		Tn = S + u * d
		h_t = w.cross(u).normalized()
		# The hinge at rest, in each bone's frame.
		u_r = (self.head[end] - self.head[upper]).normalized()
		w_r = Vector(rest_pole) - u_r * Vector(rest_pole).dot(u_r)
		h_r = w_r.normalized().cross(u_r).normalized()

		def frame(bone, y_dir):
			r = self.rest[bone].to_3x3()
			h_l = r.inverted() @ h_r
			y_l = Vector((0, 1, 0))
			h_l = (h_l - y_l * h_l.dot(y_l)).normalized()
			loc = Matrix((h_l, y_l, h_l.cross(y_l))).transposed()
			y_g = y_dir.normalized()
			h_g = (h_t - y_g * h_t.dot(y_g)).normalized()
			glob = Matrix((h_g, y_g, h_g.cross(y_g))).transposed()
			return (glob @ loc.transposed()).to_quaternion()

		g1 = frame(upper, E - S)
		self.set_global_rot(upper, g1, cache)
		cache = self.fk()
		g2 = frame(lower, Tn - E)
		self.set_global_rot(lower, g2, cache)

	# -------------------------------------------------------------- a pose from parameters

	def build(self, P):
		"""P: the pose's parameters (missing ones at rest):
		root: (x, y, z) offset; root_rot: (pitch, yaw, roll) degrees
		hips: (x, y, z) offset (the root's frame); hips_rot: (pitch, yaw, roll)
		spine: (lean, twist, side) spread over Spine, Chest, UpperChest; neck: (pitch, yaw); head: (pitch, yaw, roll)
		arm_L / arm_R: {"fk": (lower, forward, elbow, twist)} or {"ik": (target, pole)} (the root's frame)
		hand_L / hand_R: (flex, deviation, twist); shoulder_L / shoulder_R: (raise, forward)
		leg_L / leg_R: {"fk": (flex, abduction, knee, twist)} or {"ik": (target, pole)}
		foot_L / foot_R: (pitch (toes up +), toe_out, roll) — global under ik, local under fk; toes_L / toes_R: degrees
		grip_L / grip_R: 0 open .. 1 closed; thumb_L / thumb_R
		"""
		self.reset()
		g = P.get
		root = Vector(g("root", (0, 0, 0)))
		rr = g("root_rot", (0, 0, 0))
		q_root = Rz(rr[1]) @ Rx(rr[0]) @ Ry(rr[2])
		self.set_arm_rot("Root", q_root)
		self.loc["Root"] = self.rest_q["Root"].inverted() @ root
		# The hips: offset in the root's frame.
		hr = g("hips_rot", (0, 0, 0))
		self.set_arm_rot("Hips", Rz(hr[1]) @ Rx(hr[0]) @ Ry(hr[2]))
		cache = self.fk()
		base = cache["Root"] @ self.rest_rel["Hips"]
		off_root = q_root @ Vector(g("hips", (0, 0, 0)))
		self.loc["Hips"] = base.to_quaternion().inverted() @ off_root
		lean, twist, side = g("spine", (0, 0, 0))
		for n, share in (("Spine", 0.3), ("Chest", 0.35), ("UpperChest", 0.35)):
			self.set_arm_rot(n, Rz(twist * share) @ Ry(side * share) @ Rx(lean * share))
		npitch, nyaw = g("neck", (0, 0))
		self.set_arm_rot("Neck", Rz(nyaw) @ Rx(npitch))
		hp, hy, hro = g("head", (0, 0, 0))
		self.set_arm_rot("Head", Rz(hy) @ Rx(hp) @ Ry(hro))
		for s, sx in (("L", 1.0), ("R", -1.0)):
			pre = "Left" if s == "L" else "Right"
			raise_, fwd = g("shoulder_" + s, (0, 0))
			self.set_arm_rot(pre + "Shoulder", Rz(-sx * fwd) @ Ry(-sx * raise_))
		# The legs first (the arms' IK reads the torso's pose, which they do not move).
		for s, sx in (("L", 1.0), ("R", -1.0)):
			pre = "Left" if s == "L" else "Right"
			leg = g("leg_" + s, {"fk": (0, 0, 0, 0)})
			fp, fo, fr = g("foot_" + s, (0, 0, 0))
			self._mixed(leg, lambda one: self._leg(pre, sx, one, fp, fo, fr), (pre + "UpperLeg", pre + "LowerLeg", pre + "Foot"))
			self.set_arm_rot(pre + "Toes", Rx(-g("toes_" + s, 0.0)))
		for s, sx in (("L", 1.0), ("R", -1.0)):
			pre = "Left" if s == "L" else "Right"
			arm = g("arm_" + s, {"fk": (75, 0, 12, 0)})
			self._mixed(arm, lambda one: self._arm(pre, sx, one), (pre + "UpperArm", pre + "LowerArm"))
			hf, hd, ht = g("hand_" + s, (0, 0, 0))
			self.set_arm_rot(pre + "Hand", Rx(sx * ht) @ Rz(sx * hd) @ Ry(sx * hf))
			grip = g("grip_" + s, 0.25)
			for f, k in (("Index", 1.0), ("Middle", 1.05), ("Ring", 1.1), ("Little", 1.15)):
				self.set_arm_rot(pre + f + "Proximal", Ry(sx * 70.0 * grip * k))
				self.set_arm_rot(pre + f + "Intermediate", Ry(sx * 85.0 * grip * k))
			th = g("thumb_" + s, grip)
			self.set_arm_rot(pre + "ThumbMetacarpal", Rz(-sx * 20.0 * th) @ Ry(sx * 25.0 * th))
			self.set_arm_rot(pre + "ThumbProximal", Ry(sx * 40.0 * th))

	def _leg(self, pre, sx, leg, fp, fo, fr):
		if "fk" in leg:
			flex, abd, knee, tw = leg["fk"]
			self.set_arm_rot(pre + "UpperLeg", Rz(sx * tw) @ Ry(-sx * abd) @ Rx(-flex))
			self.set_arm_rot(pre + "LowerLeg", Rx(knee))
			self.set_arm_rot(pre + "Foot", Rz(sx * fo) @ Ry(sx * fr) @ Rx(-fp))
		else:
			target, pole = leg["ik"]
			cache = self.fk()
			rm = cache["Root"] @ self.rest["Root"].inverted()
			t_arm = rm @ Vector(target)
			p_arm = rm.to_3x3() @ Vector(pole)
			self.ik(pre + "UpperLeg", pre + "LowerLeg", pre + "Foot", t_arm, p_arm, (0, -1, 0))
			cache = self.fk()
			q = rm.to_quaternion() @ Rz(sx * fo) @ Ry(sx * fr) @ Rx(-fp)
			self.set_global_rot(pre + "Foot", q @ self.rest_q[pre + "Foot"], cache)

	def _arm(self, pre, sx, arm):
		if "fk" in arm:
			lower, fwd, elbow, tw = arm["fk"]
			self.set_arm_rot(pre + "UpperArm", Rx(-fwd) @ Ry(sx * lower) @ Rx(sx * tw))
			self.set_arm_rot(pre + "LowerArm", Rz(-sx * elbow))
		else:
			target, pole = arm["ik"]
			cache = self.fk()
			rm = cache["Root"] @ self.rest["Root"].inverted()
			t_arm = rm @ Vector(target)
			p_arm = rm.to_3x3() @ Vector(pole)
			self.ik(pre + "UpperArm", pre + "LowerArm", pre + "Hand", t_arm, p_arm, (0, 1, 0))

	def _mixed(self, limb, pose_fn, bones):
		"""A limb posed by pose_fn, or, for {"mix": (a, b, t)}, posed both ways and its bones' turns blended."""
		if "mix" not in limb:
			pose_fn(limb)
			return
		a, b, t = limb["mix"]
		pose_fn(a)
		qa = [self.rot[n].copy() for n in bones]
		pose_fn(b)
		for n, q in zip(bones, qa):
			self.rot[n] = q.slerp(self.rot[n], t)

	def snapshot(self):
		return {n: (self.loc[n].copy(), self.rot[n].copy()) for n in self.names}


# ------------------------------------------------------------------ interpolation of parameters


def _lerp(a, b, t):
	if isinstance(a, dict):
		if "fk" in a and "fk" in b:
			return {"fk": tuple(_lerp(x, y, t) for x, y in zip(a["fk"], b["fk"]))}
		if "ik" in a and "ik" in b:
			return {"ik": tuple(_lerp(x, y, t) for x, y in zip(a["ik"], b["ik"]))}
		# Milestone 156: a limb going from FK to IK (or back) — both poses made and their bones' turns blended (it had
		# swapped from one to the other halfway: the arm 42 degrees in a frame as the run's jump at a wall reached up).
		return {"mix": (a, b, t)}
	if isinstance(a, (tuple, list)):
		return tuple(_lerp(x, y, t) for x, y in zip(a, b))
	if isinstance(a, Vector):
		return a.lerp(b, t)
	return a + (b - a) * t


def _catmull(p0, p1, p2, p3, t):
	if isinstance(p1, dict):
		if all(isinstance(p, dict) and "ik" in p for p in (p0, p1, p2, p3)):
			return {"ik": tuple(_catmull(a, b, c, d, t) for a, b, c, d in zip(p0["ik"], p1["ik"], p2["ik"], p3["ik"]))}
		if all(isinstance(p, dict) and "fk" in p for p in (p0, p1, p2, p3)):
			return {"fk": tuple(_catmull(a, b, c, d, t) for a, b, c, d in zip(p0["fk"], p1["fk"], p2["fk"], p3["fk"]))}
		return _lerp(p1, p2, smooth(t))
	if isinstance(p1, (tuple, list, Vector)):
		return tuple(_catmull(a, b, c, d, t) for a, b, c, d in zip(p0, p1, p2, p3))
	t2 = t * t
	t3 = t2 * t
	return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)


def keyed(keys, t, loop=False, length=None):
	"""keys: [(time, params)], each params a full pose (missing parameters taken from the previous key). Parameters
	interpolated by Catmull-Rom splines through the keys (clamped at the ends, wrapped when looping)."""
	full = []
	acc = {}
	for kt, kp in keys:
		acc = dict(acc)
		acc.update(kp)
		full.append((kt, acc))
	if loop:
		full_ext = [(full[-2][0] - length, full[-2][1])] + full + [(full[1][0] + length, full[1][1])]
	else:
		full_ext = [full[0]] + full + [full[-1]]
	n = len(full_ext)
	for i in range(1, n - 2):
		t0, t1 = full_ext[i][0], full_ext[i + 1][0]
		if t0 <= t <= t1 or (i == n - 3 and t > t1):
			u = 0.0 if t1 <= t0 else min(1.0, (t - t0) / (t1 - t0))
			ease = smooth(u) * 0.35 + u * 0.65
			out = {}
			keys_all = set(full_ext[i][1]) | set(full_ext[i + 1][1])
			for k in keys_all:
				a = full_ext[i][1].get(k, full_ext[i + 1][1].get(k))
				b = full_ext[i + 1][1].get(k, a)
				p0 = full_ext[i - 1][1].get(k, a)
				p3 = full_ext[i + 2][1].get(k, b)
				out[k] = _catmull(p0, a, b, p3, ease)
			return out
	return dict(full[0][1])


# ------------------------------------------------------------------ actions


def _new_fcurve(act, obj, dp, index, group):
	"""An F-curve of the action: Blender 4's (act.fcurves), or Blender 4.4 and 5's layered actions (milestone 192: the
	bpy module of Blender 5 has no Action.fcurves)."""
	if hasattr(act, "fcurves"):
		return act.fcurves.new(dp, index=index, action_group=group)
	return act.fcurve_ensure_for_datablock(obj, dp, index=index, group_name=group)


def write_action(rig, name, frames, loop):
	"""An action from the snapshots (one a frame at FPS): rotations as quaternions (each frame on the side of the one
	before it), locations where they move. For a loop the first frame is repeated at the end."""
	if loop:
		frames = frames + [frames[0]]
	act = bpy.data.actions.new(name)
	act.use_fake_user = True
	obj = rig.obj
	if obj.animation_data is None:
		obj.animation_data_create()
	obj.animation_data.action = act
	n = len(frames)
	for bone in rig.names:
		quats = [f[bone][1] for f in frames]
		for i in range(1, n):
			if quats[i].dot(quats[i - 1]) < 0.0:
				quats[i] = -quats[i]
		locs = [f[bone][0] for f in frames]
		moves = any(l.length > 1e-5 for l in locs)
		turns = any(abs(q.w - 1.0) > 1e-6 for q in quats)
		paths = []
		if turns or bone in ("Root", "Hips"):
			paths.append(("rotation_quaternion", 4, [[q[k] for q in quats] for k in range(4)]))
		if moves or bone in ("Root", "Hips"):
			paths.append(("location", 3, [[l[k] for l in locs] for k in range(3)]))
		for path, count, series in paths:
			dp = 'pose.bones["%s"].%s' % (bone, path)
			for k in range(count):
				fc = _new_fcurve(act, obj, dp, k, bone)
				fc.keyframe_points.add(n)
				co = []
				for i in range(n):
					co += [float(i + 1), series[k][i]]
				fc.keyframe_points.foreach_set("co", co)
				for kp in fc.keyframe_points:
					kp.interpolation = "LINEAR"
				fc.update()
	track = obj.animation_data.nla_tracks.new()
	track.name = name
	strip = track.strips.new(name, 1, act)
	strip.name = name
	track.mute = True
	obj.animation_data.action = None
	return act
