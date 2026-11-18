# FellowEngine showcase: the rooftops scene's character, made by code in Blender — an articulated mannequin 1.8 m tall:
# smooth shells (lofted superellipse sections: the pelvis and the chest one torso bending with the spine, the limbs'
# segments, the head), ball joints at the shoulders, elbows, wrists, hips, knees and ankles, hands with a thumb and four
# fingers of two segments; on a skeleton named as Godot's humanoid profile (Root, Hips, Spine, Chest, UpperChest, Neck,
# Head, LeftShoulder, LeftUpperArm, ..., LeftUpperLeg, LeftLowerLeg, LeftFoot, LeftToes and the fingers).
#
# Blender's axes: Z up, the character facing -Y, its left +X (glTF: facing +Z).

import math

import bmesh
import bpy
from mathutils import Matrix, Vector

# name: (parent, head, tail, roll axis (the bone's Z), mirrored)
SKELETON = [
	("Root", None, (0, 0, 0), (0, 0, 0.25), (0, -1, 0)),
	("Hips", "Root", (0, 0, 0.965), (0, 0, 1.07), (0, -1, 0)),
	("Spine", "Hips", (0, 0, 1.07), (0, 0.008, 1.2), (0, -1, 0)),
	("Chest", "Spine", (0, 0.008, 1.2), (0, 0.004, 1.33), (0, -1, 0)),
	("UpperChest", "Chest", (0, 0.004, 1.33), (0, 0.012, 1.46), (0, -1, 0)),
	("Neck", "UpperChest", (0, 0.012, 1.46), (0, 0.0, 1.575), (0, -1, 0)),
	("Head", "Neck", (0, 0.0, 1.575), (0, 0.0, 1.77), (0, -1, 0)),
]
SIDE = [
	("Shoulder", "UpperChest", (0.022, 0.004, 1.42), (0.168, 0.016, 1.445), (0, 0, 1)),
	("UpperArm", "Shoulder", (0.175, 0.018, 1.445), (0.455, 0.018, 1.445), (0, 0, 1)),
	("LowerArm", "UpperArm", (0.455, 0.018, 1.445), (0.712, 0.018, 1.445), (0, 0, 1)),
	("Hand", "LowerArm", (0.712, 0.018, 1.445), (0.8, 0.016, 1.445), (0, 0, 1)),
	("ThumbMetacarpal", "Hand", (0.728, -0.012, 1.437), (0.762, -0.043, 1.43), (0, 0, 1)),
	("ThumbProximal", "ThumbMetacarpal", (0.762, -0.043, 1.43), (0.792, -0.064, 1.425), (0, 0, 1)),
	("IndexProximal", "Hand", (0.8, -0.012, 1.445), (0.846, -0.014, 1.445), (0, 0, 1)),
	("IndexIntermediate", "IndexProximal", (0.846, -0.014, 1.445), (0.89, -0.015, 1.445), (0, 0, 1)),
	("MiddleProximal", "Hand", (0.803, 0.008, 1.445), (0.853, 0.008, 1.445), (0, 0, 1)),
	("MiddleIntermediate", "MiddleProximal", (0.853, 0.008, 1.445), (0.9, 0.008, 1.445), (0, 0, 1)),
	("RingProximal", "Hand", (0.8, 0.028, 1.445), (0.846, 0.029, 1.445), (0, 0, 1)),
	("RingIntermediate", "RingProximal", (0.846, 0.029, 1.445), (0.888, 0.03, 1.445), (0, 0, 1)),
	("LittleProximal", "Hand", (0.794, 0.046, 1.443), (0.83, 0.049, 1.443), (0, 0, 1)),
	("LittleIntermediate", "LittleProximal", (0.83, 0.049, 1.443), (0.864, 0.051, 1.443), (0, 0, 1)),
	("UpperLeg", "Hips", (0.092, 0.0, 0.94), (0.092, -0.006, 0.51), (0, 1, 0)),
	("LowerLeg", "UpperLeg", (0.092, -0.006, 0.51), (0.092, 0.014, 0.085), (0, 1, 0)),
	("Foot", "LowerLeg", (0.092, 0.014, 0.085), (0.096, -0.112, 0.024), (0, 1, 0)),
	("Toes", "Foot", (0.096, -0.112, 0.024), (0.098, -0.18, 0.02), (0, 1, 0)),
]


def skeleton_table():
	"""Every bone: (name, parent, head, tail, roll axis), the left side's and the right's (x mirrored)."""
	out = list(SKELETON)
	for prefix, sx in (("Left", 1.0), ("Right", -1.0)):
		for name, parent, h, t, roll in SIDE:
			p = parent if parent in ("UpperChest", "Hips") else prefix + parent
			out.append((prefix + name, p, (h[0] * sx, h[1], h[2]), (t[0] * sx, t[1], t[2]), (roll[0] * sx, roll[1], roll[2])))
	return out


def build_armature(name="Mannequin"):
	data = bpy.data.armatures.new(name + "Armature")
	obj = bpy.data.objects.new(name, data)
	bpy.context.scene.collection.objects.link(obj)
	bpy.context.view_layer.objects.active = obj
	obj.select_set(True)
	bpy.ops.object.mode_set(mode="EDIT")
	ebs = {}
	for bname, parent, h, t, roll in skeleton_table():
		eb = data.edit_bones.new(bname)
		eb.head = h
		eb.tail = t
		eb.align_roll(Vector(roll))
		if parent:
			eb.parent = ebs[parent]
			eb.use_connect = False
		ebs[bname] = eb
	bpy.ops.object.mode_set(mode="OBJECT")
	return obj


# ------------------------------------------------------------------ the shells


class Shell:
	"""The mesh: lofted parts, balls; each vertex's weights by bone."""

	def __init__(self):
		self.bm = bmesh.new()
		self.weights = []  # per vertex: {bone: weight}
		self.mat_layer = None

	def _vert(self, p, w):
		v = self.bm.verts.new(p)
		self.weights.append(w)
		return v

	def loft(self, sections, bone, material=0, segments=24, cap0=True, cap1=True, weight_fn=None):
		"""sections: (centre, axis, side, a, b_front, b_back, exponent) — a superellipse ring in the plane across axis,
		a along side, b towards front (axis x side) or back. weight_fn(point) -> {bone: w} overrides the bone."""
		rings = []
		for c, axis, side, a, bf, bb, ex in sections:
			c = Vector(c)
			axis = Vector(axis).normalized()
			side = (Vector(side) - axis * Vector(side).dot(axis)).normalized()
			front = axis.cross(side)
			ring = []
			for i in range(segments):
				th = 2.0 * math.pi * i / segments
				co, si = math.cos(th), math.sin(th)
				x = math.copysign(abs(co) ** (2.0 / ex), co) * a
				b = bf if si >= 0.0 else bb
				y = math.copysign(abs(si) ** (2.0 / ex), si) * b
				p = c + side * x + front * y
				ring.append(self._vert(p, weight_fn(p) if weight_fn else {bone: 1.0}))
			rings.append(ring)
		faces = []
		for k in range(len(rings) - 1):
			for i in range(segments):
				j = (i + 1) % segments
				faces.append(self.bm.faces.new((rings[k][i], rings[k][j], rings[k + 1][j], rings[k + 1][i])))
		for end, ring, sec in ((cap0, rings[0], sections[0]), (cap1, rings[-1], sections[-1])):
			if not end:
				continue
			c = Vector(sec[0])
			pole = self._vert(c, weight_fn(c) if weight_fn else {bone: 1.0})
			for i in range(segments):
				j = (i + 1) % segments
				if ring is rings[0]:
					faces.append(self.bm.faces.new((ring[j], ring[i], pole)))
				else:
					faces.append(self.bm.faces.new((ring[i], ring[j], pole)))
		for f in faces:
			f.smooth = True
			f.material_index = material
		return faces

	def capsule_sections(self, p0, p1, r0, r1, side, flat=1.0, bulge=None, ends=4, ex=2.0):
		"""A rounded segment from p0 to p1: hemispherical ends (scaled), radii from r0 to r1, `bulge`: (t, r) extra
		sections along it."""
		p0 = Vector(p0)
		p1 = Vector(p1)
		axis = (p1 - p0).normalized()
		length = (p1 - p0).length
		secs = []
		for k in range(ends, 0, -1):
			a = math.pi * 0.5 * k / ends
			d = r0 * math.sin(a) * 0.9
			r = r0 * math.cos(a)
			secs.append((p0 - axis * d, axis, side, max(r, 1e-4), max(r * flat, 1e-4), max(r * flat, 1e-4), ex))
		mids = [(0.0, r0)] + (bulge or []) + [(1.0, r1)]
		for t, r in mids:
			secs.append((p0 + axis * (length * t), axis, side, r, r * flat, r * flat, ex))
		for k in range(1, ends + 1):
			a = math.pi * 0.5 * k / ends
			d = r1 * math.sin(a) * 0.9
			r = r1 * math.cos(a)
			secs.append((p1 + axis * d, axis, side, max(r, 1e-4), max(r * flat, 1e-4), max(r * flat, 1e-4), ex))
		return secs

	def ball(self, centre, radius, bone, material=1, segments=20, rings=12):
		res = bmesh.ops.create_uvsphere(self.bm, u_segments=segments, v_segments=rings, radius=radius, matrix=Matrix.Translation(Vector(centre)))
		for v in res["verts"]:
			self.weights.append({bone: 1.0})
		faces = {f for v in res["verts"] for f in v.link_faces}
		for f in faces:
			f.smooth = True
			f.material_index = material

	def to_object(self, name, armature, materials):
		bm = self.bm
		bm.verts.index_update()
		bm.normal_update()
		uv = bm.loops.layers.uv.new("UVMap")
		for f in bm.faces:
			n = f.normal
			ax = max(range(3), key=lambda i: abs(n[i]))
			for loop in f.loops:
				p = loop.vert.co
				loop[uv].uv = (p.y, p.z) if ax == 0 else ((p.x, p.z) if ax == 1 else (p.x, p.y))
		me = bpy.data.meshes.new(name)
		bm.to_mesh(me)
		for m in materials:
			me.materials.append(m)
		obj = bpy.data.objects.new(name, me)
		bpy.context.scene.collection.objects.link(obj)
		groups = {}
		for i, w in enumerate(self.weights):
			for b, x in w.items():
				if x <= 0.0:
					continue
				if b not in groups:
					groups[b] = obj.vertex_groups.new(name=b)
				groups[b].add([i], x, "REPLACE")
		obj.parent = armature
		mod = obj.modifiers.new("Armature", "ARMATURE")
		mod.object = armature
		bm.free()
		return obj


def _smooth(a, b, x):
	t = min(1.0, max(0.0, (x - a) / (b - a)))
	return t * t * (3.0 - 2.0 * t)


def torso_weights(p):
	"""The torso's weights by height: the hips, the spine, the chest and the upper chest blended."""
	z = p.z
	w_spine = _smooth(1.0, 1.1, z)
	w_chest = _smooth(1.16, 1.26, z)
	w_upper = _smooth(1.31, 1.41, z)
	out = {"Hips": 1.0 - w_spine}
	out["Spine"] = w_spine * (1.0 - w_chest)
	out["Chest"] = w_chest * (1.0 - w_upper)
	out["UpperChest"] = w_upper
	# The flanks low on the pelvis follow the thighs a little (no crease where the leg swings).
	return {k: v for k, v in out.items() if v > 1e-3}


def build_mesh(armature):
	S = Shell()
	X = Vector((1, 0, 0))
	Y = Vector((0, 1, 0))
	Z = Vector((0, 0, 1))
	# The torso: (z, half width, front, back, centre y, exponent).
	prof = [
		(0.835, 0.06, 0.045, 0.05, 0.0, 2.0),
		(0.85, 0.105, 0.07, 0.08, 0.0, 2.2),
		(0.88, 0.148, 0.085, 0.1, 0.004, 2.3),
		(0.925, 0.17, 0.094, 0.112, 0.006, 2.4),
		(0.975, 0.168, 0.094, 0.108, 0.004, 2.4),
		(1.025, 0.155, 0.094, 0.094, 0.0, 2.3),
		(1.075, 0.139, 0.092, 0.085, 0.0, 2.2),
		(1.125, 0.137, 0.095, 0.085, -0.002, 2.2),
		(1.18, 0.145, 0.102, 0.09, -0.004, 2.3),
		(1.24, 0.158, 0.112, 0.096, -0.006, 2.4),
		(1.3, 0.168, 0.121, 0.102, -0.006, 2.5),
		(1.35, 0.172, 0.121, 0.104, -0.004, 2.6),
		(1.4, 0.17, 0.112, 0.1, 0.0, 2.6),
		(1.44, 0.158, 0.097, 0.094, 0.004, 2.5),
		(1.47, 0.14, 0.08, 0.082, 0.008, 2.3),
		(1.49, 0.105, 0.066, 0.066, 0.01, 2.1),
		(1.505, 0.072, 0.056, 0.056, 0.01, 2.0),
		(1.515, 0.056, 0.05, 0.05, 0.01, 2.0),
	]
	secs = [((0.0, cy, z), Z, X, a, bf, bb, ex) for z, a, bf, bb, cy, ex in prof]
	# Facing -Y: the loft's "front" is axis x side = Z x X = +Y — so swap: front values to the back.
	secs = [(c, ax, sd, a, bb, bf, ex) for c, ax, sd, a, bf, bb, ex in secs]
	S.loft(secs, "Hips", 0, segments=36, weight_fn=torso_weights)
	# The neck and the head.
	S.loft(S.capsule_sections((0, 0.012, 1.47), (0, 0.0, 1.6), 0.054, 0.046, X, ends=3), "Neck", 0, segments=20)
	head = [
		(1.565, 0.03, 0.03, 0.025, -0.02),
		(1.585, 0.055, 0.06, 0.03, -0.018),
		(1.61, 0.068, 0.085, 0.055, -0.012),
		(1.645, 0.076, 0.096, 0.082, -0.008),
		(1.69, 0.081, 0.1, 0.095, -0.004),
		(1.73, 0.077, 0.092, 0.092, 0.0),
		(1.76, 0.064, 0.074, 0.076, 0.002),
		(1.782, 0.042, 0.048, 0.05, 0.002),
		(1.792, 0.018, 0.02, 0.022, 0.002),
	]
	S.loft([((0.0, cy, z), Z, X, a, bb, bf, 2.2) for z, a, bf, bb, cy in head], "Head", 0, segments=28)
	# A brow, a nose ridge and the jaw's line, faint: which way the head looks.
	S.loft(S.capsule_sections((0.0, -0.097, 1.655), (0.0, -0.104, 1.69), 0.012, 0.009, X, ends=3), "Head", 0, segments=10)
	for sx, prefix in ((1.0, "Left"), (-1.0, "Right")):
		def P(x, y, z):
			return Vector((x * sx, y, z))

		ax = Vector((sx, 0, 0))
		# The shoulder's ball, the upper arm (the deltoid's swell), the elbow, the forearm, the wrist, the hand.
		S.ball(P(0.184, 0.018, 1.445), 0.047, prefix + "Shoulder")
		S.loft(S.capsule_sections(P(0.222, 0.018, 1.447), P(0.438, 0.018, 1.445), 0.05, 0.036, Vector((0, 0, 1)), flat=0.92,
				bulge=[(0.1, 0.053), (0.3, 0.049), (0.6, 0.043)], ends=4), prefix + "UpperArm", 0, segments=20)
		S.ball(P(0.455, 0.018, 1.445), 0.036, prefix + "LowerArm")
		S.loft(S.capsule_sections(P(0.472, 0.018, 1.445), P(0.698, 0.018, 1.445), 0.036, 0.024, Vector((0, 0, 1)), flat=0.82,
				bulge=[(0.18, 0.042), (0.45, 0.036)], ends=4), prefix + "LowerArm", 0, segments=20)
		S.ball(P(0.712, 0.018, 1.445), 0.023, prefix + "Hand", segments=16, rings=10)
		palm = [
			(0.72, 0.03, 0.013, 3.0),
			(0.745, 0.04, 0.016, 3.2),
			(0.78, 0.045, 0.016, 3.4),
			(0.8, 0.044, 0.013, 3.4),
			(0.808, 0.036, 0.008, 3.0),
		]
		S.loft([(P(x, 0.016, 1.445), ax, Vector((0, 1, 0)), a, b, b, ex) for x, a, b, ex in palm], prefix + "Hand", 0, segments=20)
		fingers = [
			("ThumbMetacarpal", (0.728, -0.012, 1.437), (0.762, -0.043, 1.43), 0.012, 0.011),
			("ThumbProximal", (0.764, -0.045, 1.43), (0.796, -0.067, 1.425), 0.0105, 0.0085),
			("IndexProximal", (0.802, -0.012, 1.445), (0.845, -0.014, 1.445), 0.0095, 0.009),
			("IndexIntermediate", (0.848, -0.014, 1.445), (0.886, -0.015, 1.445), 0.0088, 0.0075),
			("MiddleProximal", (0.805, 0.008, 1.445), (0.852, 0.008, 1.445), 0.0098, 0.0093),
			("MiddleIntermediate", (0.855, 0.008, 1.445), (0.896, 0.008, 1.445), 0.009, 0.0077),
			("RingProximal", (0.802, 0.028, 1.445), (0.845, 0.029, 1.445), 0.0093, 0.0088),
			("RingIntermediate", (0.848, 0.029, 1.445), (0.884, 0.03, 1.445), 0.0085, 0.0072),
			("LittleProximal", (0.796, 0.046, 1.443), (0.829, 0.049, 1.443), 0.0082, 0.0077),
			("LittleIntermediate", (0.832, 0.049, 1.443), (0.861, 0.051, 1.443), 0.0075, 0.0064),
		]
		for bone, a, b, r0, r1 in fingers:
			S.loft(S.capsule_sections(P(*a), P(*b), r0, r1, Vector((0, 0, 1)), flat=0.85, ends=3), prefix + bone, 0, segments=10)
		# The hip's ball, the thigh, the knee, the shin (the calf behind), the ankle, the foot and its toes.
		S.ball(P(0.092, 0.0, 0.935), 0.066, prefix + "UpperLeg", segments=22, rings=14)
		thigh = [
			(0.93, 0.082, 0.082, 0.082),
			(0.88, 0.085, 0.086, 0.084),
			(0.8, 0.077, 0.08, 0.074),
			(0.7, 0.066, 0.068, 0.062),
			(0.6, 0.055, 0.056, 0.052),
			(0.55, 0.049, 0.05, 0.047),
		]
		secs = [(P(0.092 - 0.004 * (0.93 - z), -0.006 * (0.94 - z) / 0.43, z), Vector((0, 0, -1)), Vector((sx, 0, 0)), a, bf, bb, 2.2)
				for z, a, bf, bb in thigh]
		# The loft's front (axis x side) for axis -Z, side ±X: (-Z) x (X) = -Y: the character's front. Right: +Y — swap.
		if sx < 0:
			secs = [(c, axz, sd, a, bb, bf, ex) for c, axz, sd, a, bf, bb, ex in secs]
		cap_top = [(secs[0][0] + Vector((0, 0, 0.03)), secs[0][1], secs[0][2], 0.06, 0.06, 0.06, 2.0)]
		S.loft(cap_top + secs + [(P(0.092, -0.006, 0.525), Vector((0, 0, -1)), Vector((sx, 0, 0)), 0.03, 0.03, 0.03, 2.0)],
				prefix + "UpperLeg", 0, segments=24)
		S.ball(P(0.092, -0.006, 0.51), 0.046, prefix + "LowerLeg", segments=20, rings=12)
		shin = [
			(0.49, 0.045, 0.044, 0.048),
			(0.44, 0.049, 0.046, 0.062),
			(0.37, 0.047, 0.042, 0.058),
			(0.28, 0.039, 0.036, 0.042),
			(0.18, 0.031, 0.031, 0.032),
			(0.115, 0.028, 0.03, 0.03),
		]
		secs = [(P(0.092, -0.006 + 0.02 * (0.51 - z) / 0.425, z), Vector((0, 0, -1)), Vector((sx, 0, 0)), a, bf, bb, 2.1) for z, a, bf, bb in shin]
		if sx < 0:
			secs = [(c, axz, sd, a, bb, bf, ex) for c, axz, sd, a, bf, bb, ex in secs]
		top = [(secs[0][0] + Vector((0, 0, 0.012)), secs[0][1], secs[0][2], 0.03, 0.03, 0.03, 2.0)]
		bot = [(secs[-1][0] - Vector((0, 0, 0.012)), secs[-1][1], secs[-1][2], 0.02, 0.02, 0.02, 2.0)]
		S.loft(top + secs + bot, prefix + "LowerLeg", 0, segments=22)
		S.ball(P(0.092, 0.014, 0.085), 0.03, prefix + "Foot", segments=16, rings=10)
		# The foot: sections along -Y from the heel, (y, half width, half height, centre z); its sole flat at z = 0.
		foot = [
			(0.06, 0.012, 0.016, 0.03),
			(0.05, 0.028, 0.03, 0.04),
			(0.025, 0.036, 0.042, 0.05),
			(-0.02, 0.043, 0.04, 0.044),
			(-0.07, 0.047, 0.03, 0.034),
			(-0.105, 0.047, 0.024, 0.026),
		]
		S.loft([(P(0.094, y, zc), Vector((0, -1, 0)), Vector((sx, 0, 0)), a, b, b, 2.6) for y, a, b, zc in foot], prefix + "Foot", 0, segments=18, cap0=True, cap1=True)
		toes = [
			(-0.108, 0.045, 0.021, 0.022),
			(-0.14, 0.043, 0.019, 0.02),
			(-0.17, 0.036, 0.015, 0.016),
			(-0.188, 0.022, 0.01, 0.012),
		]
		S.loft([(P(0.097, y, zc), Vector((0, -1, 0)), Vector((sx, 0, 0)), a, b, b, 2.6) for y, a, b, zc in toes], prefix + "Toes", 0, segments=18)
	shell = bpy.data.materials.new("mannequin_shell")
	joint = bpy.data.materials.new("mannequin_joint")
	for m, c, r in ((shell, (0.78, 0.76, 0.72, 1), 0.45), (joint, (0.12, 0.12, 0.13, 1), 0.35)):
		m.use_nodes = True
		bsdf = m.node_tree.nodes.get("Principled BSDF")
		bsdf.inputs["Base Color"].default_value = c
		bsdf.inputs["Roughness"].default_value = r
	return S.to_object("MannequinBody", armature, [shell, joint])
