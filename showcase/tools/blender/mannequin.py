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
	"""The mesh: surfaces from rings of points, balls, tori; each vertex's weights by bone."""

	def __init__(self):
		self.bm = bmesh.new()
		self.weights = []  # per vertex: {bone: weight}

	def _vert(self, p, w):
		v = self.bm.verts.new(p)
		self.weights.append(w)
		return v

	def grid(self, rings, bone, material=0, cap0=True, cap1=True, weight_fn=None, closed=True):
		"""rings: lists of points (each ring closed around), joined ring to ring; the ends capped with a pole at their
		centre. weight_fn(point) -> {bone: w} overrides the bone (evaluated where the point is)."""

		def w(p):
			return weight_fn(p) if weight_fn else {bone: 1.0}

		vs = [[self._vert(p, w(p)) for p in ring] for ring in rings]
		n = len(rings[0])
		faces = []
		for k in range(len(vs) - 1):
			for i in range(n if closed else n - 1):
				j = (i + 1) % n
				faces.append(self.bm.faces.new((vs[k][i], vs[k][j], vs[k + 1][j], vs[k + 1][i])))
		for end, ring, rv in ((cap0, rings[0], vs[0]), (cap1, rings[-1], vs[-1])):
			if not end:
				continue
			c = sum(ring, Vector()) / len(ring)
			pole = self._vert(c, w(c))
			for i in range(n):
				j = (i + 1) % n
				faces.append(self.bm.faces.new((rv[j], rv[i], pole) if rv is vs[0] else (rv[i], rv[j], pole)))
		for f in faces:
			f.smooth = True
			f.material_index = material
		return faces

	def loft(self, sections, bone, material=0, segments=24, cap0=True, cap1=True, weight_fn=None, shape_fn=None):
		"""sections: (centre, axis, side, a, b_front, b_back, exponent) — a superellipse ring in the plane across axis,
		a along side, b towards front (axis x side) or back. shape_fn(point, centre, side, front, x, y) -> point sculpts
		it (x, y: the ring's coordinates before the sculpting)."""
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
				if shape_fn:
					p = shape_fn(p, c, side, front, x, y)
				ring.append(p)
			rings.append(ring)
		return self.grid(rings, bone, material, cap0, cap1, weight_fn)

	def capsule_sections(self, p0, p1, r0, r1, side, flat=1.0, bulge=None, ends=4, ex=2.0, reach=0.9):
		"""A rounded segment from p0 to p1: rounded ends reaching `reach` of their radius beyond p0 and p1, radii from r0
		to r1, `bulge`: (t, r) extra sections along it."""
		p0 = Vector(p0)
		p1 = Vector(p1)
		axis = (p1 - p0).normalized()
		length = (p1 - p0).length
		secs = []
		for k in range(ends, 0, -1):
			a = math.pi * 0.5 * k / ends
			d = r0 * math.sin(a) * reach
			r = r0 * math.cos(a)
			secs.append((p0 - axis * d, axis, side, max(r, 1e-4), max(r * flat, 1e-4), max(r * flat, 1e-4), ex))
		mids = [(0.0, r0)] + (bulge or []) + [(1.0, r1)]
		for t, r in mids:
			secs.append((p0 + axis * (length * t), axis, side, r, r * flat, r * flat, ex))
		for k in range(1, ends + 1):
			a = math.pi * 0.5 * k / ends
			d = r1 * math.sin(a) * reach
			r = r1 * math.cos(a)
			secs.append((p1 + axis * d, axis, side, max(r, 1e-4), max(r * flat, 1e-4), max(r * flat, 1e-4), ex))
		return secs

	def ellipsoid(self, centre, radii, bone, material=1, axes=None, segments=20, rings=12, weights=None):
		"""An ellipsoid: radii along axes (default X, Y, Z)."""
		ax = axes or (Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)))
		c = Vector(centre)
		out = []
		for k in range(1, rings):
			ph = math.pi * k / rings
			ring = []
			for i in range(segments):
				th = 2.0 * math.pi * i / segments
				d = ax[0] * (math.sin(ph) * math.cos(th) * radii[0]) + ax[1] * (math.sin(ph) * math.sin(th) * radii[1])
				ring.append(c + d - ax[2] * (math.cos(ph) * radii[2]))
			out.append(ring)
		bottom = c - ax[2] * radii[2]
		top = c + ax[2] * radii[2]
		faces = self.grid(out, bone, material, cap0=False, cap1=False, weight_fn=(lambda p: weights) if weights else None)
		n = segments
		w = weights or {bone: 1.0}
		pb = self._vert(bottom, dict(w))
		pt = self._vert(top, dict(w))
		first = [v for v in self.bm.verts][-2 - n * (rings - 1) : -2 - n * (rings - 2)]
		last = [v for v in self.bm.verts][-2 - n : -2]
		extra = []
		for i in range(n):
			j = (i + 1) % n
			extra.append(self.bm.faces.new((first[j], first[i], pb)))
			extra.append(self.bm.faces.new((last[i], last[j], pt)))
		for f in extra:
			f.smooth = True
			f.material_index = material
		return faces + extra

	def ball(self, centre, radius, bone, material=1, segments=20, rings=12, weights=None):
		return self.ellipsoid(centre, (radius, radius, radius), bone, material, None, segments, rings, weights)

	def torus(self, centre, axis, major, minor, bone, material=1, segments=28, tube=8, weights=None):
		"""A ring around axis: a seam's dark line."""
		axis = Vector(axis).normalized()
		u = axis.orthogonal().normalized()
		v = axis.cross(u)
		c = Vector(centre)
		rings = []
		for i in range(segments + 1):
			th = 2.0 * math.pi * (i % segments) / segments
			radial = u * math.cos(th) + v * math.sin(th)
			ring = []
			for k in range(tube):
				ph = 2.0 * math.pi * k / tube
				ring.append(c + radial * (major + minor * math.cos(ph)) + axis * (minor * math.sin(ph)))
			rings.append(ring)
		w = weights or {bone: 1.0}
		vs = [[self._vert(p, dict(w)) for p in ring] for ring in rings[:-1]]
		faces = []
		for i in range(segments):
			a, b = vs[i], vs[(i + 1) % segments]
			for k in range(tube):
				m = (k + 1) % tube
				faces.append(self.bm.faces.new((a[k], b[k], b[m], a[m])))
		for f in faces:
			f.smooth = True
			f.material_index = material
		return faces

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


def _gauss(x, s):
	return math.exp(-(x * x) / (2.0 * s * s))


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
	return {k: v for k, v in out.items() if v > 1e-3}


# The torso (milestone 156, after the owner's reference video): one surface, (z, half width, front, back, centre y,
# exponent) from the crotch to the neck, cut into plates — a breastplate to the bottom of the ribs, the abdomen, the
# pelvis cut like briefs — whose edges roll in over a dark core: thin dark seams.
TORSO = [
	# Milestone 158 (the owner: "why does its backside stick out so?"): the pelvis's back as deep as its front, and the
	# hips 7 mm narrower — beside the reference's silhouettes, frame for frame, it had stood out 2-3 cm behind.
	(0.83, 0.146, 0.08, 0.086, 0.002, 2.4),
	(0.87, 0.16, 0.09, 0.093, 0.0, 2.5),
	(0.91, 0.166, 0.094, 0.095, -0.001, 2.6),
	(0.95, 0.161, 0.094, 0.092, -0.001, 2.5),
	(0.985, 0.149, 0.092, 0.087, 0.0, 2.35),
	(1.02, 0.136, 0.09, 0.085, 0.0, 2.25),
	(1.07, 0.123, 0.09, 0.08, -0.002, 2.25),
	(1.11, 0.125, 0.091, 0.081, -0.002, 2.25),
	(1.15, 0.131, 0.095, 0.084, -0.002, 2.3),
	(1.19, 0.141, 0.103, 0.089, -0.003, 2.35),
	(1.24, 0.156, 0.114, 0.095, -0.004, 2.45),
	(1.3, 0.173, 0.125, 0.102, -0.006, 2.55),
	(1.35, 0.184, 0.128, 0.106, -0.004, 2.65),
	(1.4, 0.187, 0.12, 0.107, 0.0, 2.75),
	(1.44, 0.177, 0.104, 0.1, 0.004, 2.75),
	(1.47, 0.148, 0.083, 0.088, 0.008, 2.5),
	(1.49, 0.112, 0.069, 0.073, 0.01, 2.2),
	(1.505, 0.07, 0.056, 0.058, 0.01, 2.0),
]
SEAMS = (0.985, 1.148)  # the waistband, the bottom of the ribs
SEAM_GAP = 0.014  # between two plates' edges (the dark core seen between them some 9 mm, as the reference's seams)
RIM = 0.004  # how far a plate's edge rolls in
RIM_H = 0.0025  # over what height


def torso_at(z):
	"""The torso's section at z: (half width, front, back, centre y, exponent), by Catmull-Rom through TORSO."""
	t = TORSO
	z = min(t[-1][0], max(t[0][0], z))
	i = max(0, min(len(t) - 2, next((k for k in range(len(t) - 1) if t[k][0] <= z <= t[k + 1][0]), 0)))
	u = (z - t[i][0]) / (t[i + 1][0] - t[i][0])
	p0, p1, p2, p3 = t[max(0, i - 1)], t[i], t[i + 1], t[min(len(t) - 1, i + 2)]
	out = []
	for k in range(1, 6):
		a0, a1, a2, a3 = p0[k], p1[k], p2[k], p3[k]
		out.append(0.5 * (2 * a1 + (-a0 + a2) * u + (2 * a0 - 5 * a1 + 4 * a2 - a3) * u * u + (-a0 + 3 * a1 - 3 * a2 + a3) * u ** 3))
	return out


def seam_dip(p, c):
	"""The seams lower in front (the breastplate's edge under the ribs, the briefs' waistband), as the reference's: the
	height a point of the torso is moved down by, the more the further in front it is and the nearer a seam."""
	front = _smooth(0.0, 0.1, c.y - p.y)
	dz = 0.0
	for z_s, depth, reach in ((SEAMS[1], 0.028, 0.07), (SEAMS[0], 0.014, 0.05)):
		w = max(0.0, 1.0 - abs(p.z - z_s) / reach)
		dz -= depth * front * w * w * (3.0 - 2.0 * w)
	return dz


def build_mesh(armature):
	S = Shell()
	X = Vector((1, 0, 0))
	Z = Vector((0, 0, 1))

	def plate(z0, z1, mat, rim0=True, rim1=True, shape_fn=None, inset=0.0, step=0.012, segments=44, caps=(True, True)):
		"""The torso's surface from z0 to z1 (the character faces -Y: the loft's front, Z x X, is +Y — its back), the
		edges rolled in."""
		n = max(2, int(math.ceil((z1 - z0) / step)))
		zs = [z0 + (z1 - z0) * k / n for k in range(n + 1)]
		secs = []

		def sec(z, d):
			a, f, b, cy, ex = torso_at(z)
			return ((0.0, cy, z), Z, X, a - d - inset, b - d - inset, f - d - inset, ex)

		if rim0:
			c, ax, sd, a, bf, bb, ex = sec(z0, RIM)
			secs.append(((c[0], c[1], z0 - RIM_H), ax, sd, a, bf, bb, ex))
		secs += [sec(z, 0.0) for z in zs]
		if rim1:
			c, ax, sd, a, bf, bb, ex = sec(z1, RIM)
			secs.append(((c[0], c[1], z1 + RIM_H), ax, sd, a, bf, bb, ex))
		return S.loft(secs, "Hips", mat, segments=segments, weight_fn=torso_weights, shape_fn=shape_fn, cap0=caps[0], cap1=caps[1])

	def chest_shape(p, c, side, front, x, y):
		# The pectorals (in front, -Y) and the shoulder blades (behind), sculpted onto the plate.
		q = p.copy()
		fwd = _smooth(0.0, 0.09, c.y - p.y)
		bwd = _smooth(0.0, 0.09, p.y - c.y)
		# A man's pectorals: broad and flat, a defined lower edge, fading up to the collarbones and out to the arms.
		ax_ = abs(p.x)
		across = _smooth(0.004, 0.03, ax_) * (1.0 - _smooth(0.115, 0.16, ax_))
		lower = 1.27 + 0.025 * _smooth(0.02, 0.13, ax_)
		down_up = _smooth(lower - 0.012, lower + 0.018, p.z) * (1.0 - _smooth(1.36, 1.45, p.z))
		bump = 0.017 * across * down_up
		q.y -= bump * fwd
		# Behind, the shoulder blades and the spine's groove between the back's muscles.
		blades = 0.014 * _gauss(abs(p.x) - 0.075, 0.045) * _gauss(p.z - 1.37, 0.06)
		spine = 0.007 * _gauss(p.x, 0.014) * _smooth(1.0, 1.12, p.z) * (1.0 - _smooth(1.4, 1.47, p.z))
		q.y += (blades - spine) * bwd
		q.z += seam_dip(p, c)
		return q

	z_lo = TORSO[0][0]

	def pelvis_shape(p, c, side, front, x, y):
		# Cut like briefs: the lower edge rises from the crotch to the hips (the legs' openings), the heights between
		# spread to the waistband; the buttocks behind.
		q = p.copy()
		z_hi = SEAMS[0]
		t = min(1.0, max(0.0, (p.z - (z_lo - RIM_H)) / (z_hi - z_lo)))
		side_k = _smooth(0.3, 0.92, abs(p.x) / 0.17)
		lift = 0.06 * side_k * (1.0 - _smooth(0.0, 1.0, (p.y - c.y) / 0.11) * 0.4)
		# Milestone 158: and behind, higher, to the buttocks' crease (the reference's briefs from behind: small, the
		# thighs' tops showing below them; ours had covered them, broad and low).
		lift += 0.03 * _smooth(0.0, 0.08, p.y - c.y)
		q.z = p.z + lift * (1.0 - t) ** 1.5
		if p.y > c.y:
			k = min(1.0, (p.y - c.y) / 0.1)
			q.y += 0.008 * _gauss(abs(p.x) - 0.07, 0.05) * _gauss(q.z - 0.91, 0.045) * k * k
		q.z += seam_dip(p, c)
		return q

	g = SEAM_GAP * 0.5
	# The breastplate (the neck's opening at its top), the abdomen, the pelvis.
	plate(SEAMS[1] + g, TORSO[-1][0], 0, rim1=False, shape_fn=chest_shape)
	plate(SEAMS[0] + g, SEAMS[1] - g, 0, shape_fn=chest_shape)
	plate(z_lo, SEAMS[0] - g, 0, shape_fn=pelvis_shape)
	# The dark core: in the seams, in the legs' openings and under the pelvis between the thighs.
	plate(z_lo + 0.005, TORSO[-1][0] - 0.004, 1, rim0=False, rim1=False, inset=0.0025, step=0.012, segments=44,
			shape_fn=chest_shape)
	S.loft([
		((0.0, 0.006, 0.826), Z, X, 0.03, 0.03, 0.03, 2.0),
		((0.0, 0.006, 0.84), Z, X, 0.065, 0.055, 0.045, 2.0),
		((0.0, 0.006, 0.87), Z, X, 0.11, 0.075, 0.06, 2.2),
	], "Hips", 1, segments=28)
	# The neck, dark, wider where it meets the shoulders.
	S.loft([
		((0, 0.012, 1.475), Z, X, 0.052, 0.05, 0.05, 2.0),
		((0, 0.01, 1.51), Z, X, 0.045, 0.043, 0.043, 2.0),
		((0, 0.004, 1.56), Z, X, 0.038, 0.037, 0.037, 2.0),
		((0, 0.0, 1.6), Z, X, 0.036, 0.035, 0.035, 2.0),
		((0, -0.002, 1.625), Z, X, 0.02, 0.02, 0.02, 2.0),
	], "Neck", 1, segments=20, weight_fn=lambda p: {"Neck": 1.0} if p.z < 1.575 else {"Head": 1.0})
	# The head: an egg, the chin forward and down, a face's plane flattened onto its front.
	c0 = Vector((0.0, -0.046, 1.56))
	c1 = Vector((0.0, 0.028, 1.797))
	axis = (c1 - c0).normalized()
	length = (c1 - c0).length
	back = axis.cross(X).normalized()  # perpendicular to the axis, towards the back of the head (+Y)
	rings = []
	n_r = 22
	for k in range(1, n_r):
		s = k / n_r
		prof = math.sqrt(max(0.0, s * (1.0 - s))) * 2.0 * (0.8 + 0.3 * s)
		prof = prof ** 0.92
		half_w = 0.093 * prof / 1.06
		half_d = 0.106 * prof / 1.06
		c = c0 + axis * (length * s)
		ring = []
		for i in range(32):
			th = 2.0 * math.pi * i / 32
			xx = math.cos(th) * half_w
			yy = math.sin(th) * half_d  # + behind
			if yy < 0.0:
				# The face: its front pressed into a soft plane between the brow and the chin.
				lim = 0.7 * half_d
				f = -yy
				if f > lim:
					w = _smooth(0.1, 0.25, s) * (1.0 - _smooth(0.72, 0.86, s))
					f = f - (f - lim) * 0.55 * w
				yy = -f
			ring.append(c + X * xx + back * yy)
		rings.append(ring)
	S.grid(rings, "Head", 0)
	for sx, prefix in ((1.0, "Left"), (-1.0, "Right")):
		def P(x, y, z):
			return Vector((x * sx, y, z))

		ax = Vector((sx, 0, 0))
		up = Vector((0, 0, 1))
		# The shoulder: a dark socket in the breastplate, the arm's dome in it.
		S.ellipsoid(P(0.163, 0.016, 1.432), (0.046, 0.062, 0.064), prefix + "Shoulder", 1, segments=22)
		S.ball(P(0.198, 0.018, 1.446), 0.064, prefix + "UpperArm", 0, segments=24, rings=14)
		# The upper arm, the elbow (a dark ball), the forearm, the wrist (dark).
		S.loft(S.capsule_sections(P(0.228, 0.018, 1.446), P(0.405, 0.018, 1.445), 0.057, 0.045, up, flat=0.93,
				bulge=[(0.25, 0.06), (0.55, 0.053)], ends=4), prefix + "UpperArm", 0, segments=24)
		el = P(0.455, 0.018, 1.445)
		S.ball(el, 0.037, prefix + "LowerArm", 1, segments=22)
		S.loft(S.capsule_sections(P(0.507, 0.018, 1.445), P(0.682, 0.018, 1.445), 0.043, 0.03, up, flat=0.85,
				bulge=[(0.15, 0.048), (0.45, 0.042)], ends=4), prefix + "LowerArm", 0, segments=24)
		S.ball(P(0.712, 0.018, 1.445), 0.028, prefix + "Hand", 1, segments=16, rings=10)
		palm = [
			(0.722, 0.032, 0.014, 3.0),
			(0.748, 0.044, 0.018, 3.2),
			(0.782, 0.049, 0.018, 3.4),
			(0.802, 0.048, 0.015, 3.4),
			(0.81, 0.04, 0.009, 3.0),
		]
		S.loft([(P(x, 0.016, 1.445), ax, Vector((0, 1, 0)), a, b, b, ex) for x, a, b, ex in palm], prefix + "Hand", 0, segments=20)
		fingers = [
			("ThumbMetacarpal", (0.728, -0.012, 1.437), (0.762, -0.043, 1.43), 0.013, 0.012),
			("ThumbProximal", (0.764, -0.045, 1.43), (0.796, -0.067, 1.425), 0.0115, 0.0092),
			("IndexProximal", (0.804, -0.013, 1.445), (0.845, -0.014, 1.445), 0.0105, 0.01),
			("IndexIntermediate", (0.848, -0.014, 1.445), (0.887, -0.015, 1.445), 0.0097, 0.0082),
			("MiddleProximal", (0.807, 0.008, 1.445), (0.852, 0.008, 1.445), 0.0108, 0.0102),
			("MiddleIntermediate", (0.855, 0.008, 1.445), (0.897, 0.008, 1.445), 0.0099, 0.0085),
			("RingProximal", (0.804, 0.029, 1.445), (0.845, 0.029, 1.445), 0.0102, 0.0097),
			("RingIntermediate", (0.848, 0.029, 1.445), (0.885, 0.03, 1.445), 0.0094, 0.008),
			("LittleProximal", (0.798, 0.047, 1.443), (0.829, 0.049, 1.443), 0.009, 0.0085),
			("LittleIntermediate", (0.832, 0.049, 1.443), (0.862, 0.051, 1.443), 0.0083, 0.007),
		]
		for bone, a, b, r0, r1 in fingers:
			S.loft(S.capsule_sections(P(*a), P(*b), r0, r1, up, flat=0.85, ends=3), prefix + bone, 0, segments=10)
		# The hip: a dark ball in the pelvis; the leg's opening, a dark ring round the thigh under the briefs' edge.
		S.ball(P(0.092, 0.0, 0.935), 0.074, prefix + "UpperLeg", 1, segments=22, rings=14)
		down = Vector((0, 0, -1))
		thigh = [
			(0.935, 0.075, 0.075, 0.075),
			(0.915, 0.092, 0.1, 0.086),
			(0.87, 0.098, 0.106, 0.09),
			(0.8, 0.092, 0.1, 0.085),
			(0.72, 0.08, 0.086, 0.075),
			(0.64, 0.067, 0.07, 0.062),
			(0.59, 0.058, 0.06, 0.055),
			(0.567, 0.049, 0.05, 0.047),
			(0.556, 0.03, 0.03, 0.03),
		]

		def leg_secs(rows, x_of, y_of, ex):
			secs = [(P(x_of(z), y_of(z), z), down, Vector((sx, 0, 0)), a, bf, bb, ex) for z, a, bf, bb in rows]
			# The loft's front (axis x side) for axis -Z, side +X is -Y, the character's front; for the right leg +Y.
			if sx < 0:
				secs = [(c, axz, sd, a, bb, bf, e) for c, axz, sd, a, bf, bb, e in secs]
			return secs

		S.loft(leg_secs(thigh, lambda z: 0.092 + 0.012 * _smooth(0.6, 0.93, z), lambda z: -0.006 * (0.94 - z) / 0.43, 2.2),
				prefix + "UpperLeg", 0, segments=28)
		# The knee: a grey ball, dark rings where the thigh and the shin meet it.
		kn = P(0.092, -0.006, 0.51)
		S.ball(kn, 0.053, prefix + "LowerLeg", 0, segments=26, rings=16)
		S.torus(kn + Vector((0, 0, 0.041)), Vector((0, 0, 1)), 0.044, 0.007, prefix + "UpperLeg", 1)
		S.torus(kn + Vector((0, 0.001, -0.04)), Vector((0, 0.05, 1)), 0.045, 0.007, prefix + "LowerLeg", 1)
		shin = [
			(0.466, 0.03, 0.03, 0.03),
			(0.456, 0.05, 0.048, 0.053),
			(0.43, 0.055, 0.05, 0.07),
			(0.37, 0.055, 0.048, 0.07),
			(0.29, 0.046, 0.04, 0.052),
			(0.2, 0.036, 0.034, 0.037),
			(0.14, 0.032, 0.032, 0.032),
			(0.126, 0.024, 0.024, 0.024),
		]
		S.loft(leg_secs(shin, lambda z: 0.092, lambda z: -0.006 + 0.02 * (0.51 - z) / 0.425, 2.1), prefix + "LowerLeg", 0, segments=24)
		# The ankle, dark; the foot like a shoe (its sole flat at z = 0), the toes.
		S.ball(P(0.092, 0.014, 0.085), 0.038, prefix + "Foot", 1, segments=18, rings=12)
		foot = [
			(0.076, 0.016, 0.02, 0.038),
			(0.066, 0.035, 0.036, 0.042),
			(0.04, 0.045, 0.047, 0.05),
			(0.0, 0.049, 0.048, 0.051),
			(-0.04, 0.052, 0.041, 0.044),
			(-0.08, 0.054, 0.032, 0.035),
			(-0.106, 0.054, 0.027, 0.029),
		]
		S.loft([(P(0.094, y, zc), Vector((0, -1, 0)), Vector((sx, 0, 0)), a, b, b, 2.7) for y, a, b, zc in foot], prefix + "Foot", 0, segments=20)
		toes = [
			(-0.106, 0.054, 0.027, 0.029),
			(-0.14, 0.052, 0.022, 0.024),
			(-0.17, 0.046, 0.018, 0.019),
			(-0.19, 0.034, 0.013, 0.014),
			(-0.2, 0.017, 0.007, 0.009),
		]
		S.loft([(P(0.097, y, zc), Vector((0, -1, 0)), Vector((sx, 0, 0)), a, b, b, 2.7) for y, a, b, zc in toes], prefix + "Toes", 0, segments=20)
	shell = bpy.data.materials.new("mannequin_shell")
	joint = bpy.data.materials.new("mannequin_joint")
	for m, c, r in ((shell, (0.46, 0.46, 0.47, 1), 0.42), (joint, (0.02, 0.02, 0.024, 1), 0.28)):
		m.use_nodes = True
		bsdf = m.node_tree.nodes.get("Principled BSDF")
		bsdf.inputs["Base Color"].default_value = c
		bsdf.inputs["Roughness"].default_value = r
	return S.to_object("MannequinBody", armature, [shell, joint])
