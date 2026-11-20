# FellowEngine showcase: the rooftops scene's buildings, made by code in Blender — Renaissance houses in the manner of
# Florence's (plastered walls over a rusticated ground floor, stone string courses at every floor, framed windows with
# sills and shutters, arched doors and shops, deep eaves on wooden rafters, terracotta roofs with their coppi at the
# eaves and the ridges, chimneys, balconies, roof terraces with an altana), a bell tower with an open belfry and a beam
# out over the square, and the square's furniture (a hay cart, a fountain, market stalls, barrels and crates) and a
# skyline with a dome. Each model also gets a collision mesh ("<name>_col": the walls flat, the ledges, the roofs).

import math

from mathutils import Vector

from ac_common import Builder, Collider, empty

UP = Vector((0.0, 0.0, 1.0))


class Facade:
	"""A wall's outer plane: origin at its left foot (seen from outside), right along it, normal out of it."""

	def __init__(self, origin, right, normal, length):
		self.o = Vector(origin)
		self.r = Vector(right).normalized()
		self.n = Vector(normal).normalized()
		self.length = length

	def p(self, u, z, w=0.0):
		"""A point u along the wall, z up, w into it (negative: out of it)."""
		return self.o + self.r * u + UP * z - self.n * w

	def offset(self, out):
		"""The same wall moved out by `out` (its ends extended so that the corners still meet)."""
		return Facade(self.o - self.r * out + self.n * out, self.r, self.n, self.length + 2.0 * out)


def rect_facades(w, d, out=0.0):
	"""The four walls of a w x d footprint centred on the origin: front (-Y), right (+X), back (+Y), left (-X)."""
	hw = w * 0.5 + out
	hd = d * 0.5 + out
	return [
		Facade((-hw, -hd, 0), (1, 0, 0), (0, -1, 0), 2 * hw),
		Facade((hw, -hd, 0), (0, 1, 0), (1, 0, 0), 2 * hd),
		Facade((hw, hd, 0), (-1, 0, 0), (0, 1, 0), 2 * hw),
		Facade((-hw, hd, 0), (0, -1, 0), (-1, 0, 0), 2 * hd),
	]


def opening(x0, x1, z0, z1, arch=False, back="glass", depth=0.22, through=False):
	return {"x0": x0, "x1": x1, "z0": z0, "z1": z1, "arch": arch, "back": back, "depth": depth, "through": through}


def arch_points(o, segments=10):
	r = (o["x1"] - o["x0"]) * 0.5
	xm = (o["x0"] + o["x1"]) * 0.5
	zs = o["z1"] - r
	return [(xm + r * math.cos(math.pi * i / segments), zs + r * math.sin(math.pi * i / segments)) for i in range(segments + 1)], zs


def wall(B, F, z0, z1, openings, material, reveal="stone"):
	"""The wall's surface between z0 and z1 with its openings: the faces round them (a grid split at their edges), their
	arches' spandrels, the reveals into them and their backs."""
	xs = sorted({0.0, F.length} | {o["x0"] for o in openings} | {o["x1"] for o in openings})
	zs_ = sorted({z0, z1} | {o["z0"] for o in openings} | {o["z1"] for o in openings})
	for i in range(len(xs) - 1):
		for k in range(len(zs_) - 1):
			ua, ub, za, zb = xs[i], xs[i + 1], zs_[k], zs_[k + 1]
			if ub - ua < 1e-4 or zb - za < 1e-4:
				continue
			um, zm = (ua + ub) * 0.5, (za + zb) * 0.5
			if any(o["x0"] < um < o["x1"] and o["z0"] < zm < o["z1"] for o in openings):
				continue
			B.poly([F.p(ua, za), F.p(ub, za), F.p(ub, zb), F.p(ua, zb)], material)
	for o in openings:
		d = o["depth"]
		x0, x1, oz0, oz1 = o["x0"], o["x1"], o["z0"], o["z1"]
		if d < 1e-4:
			if o["arch"]:
				pts, zsp = arch_points(o)
				half = len(pts) // 2
				left = list(reversed(pts[half:]))
				for a, b in zip(left[:-1], left[1:]):
					B.poly([F.p(x0, oz1), F.p(a[0], a[1]), F.p(b[0], b[1])], material)
				right = list(reversed(pts[: half + 1]))
				for a, b in zip(right[:-1], right[1:]):
					B.poly([F.p(x1, oz1), F.p(a[0], a[1]), F.p(b[0], b[1])], material)
			continue
		if o["arch"]:
			pts, zsp = arch_points(o)
			top = zsp
			# The spandrels: from the bounding box's top corners to the arch.
			half = len(pts) // 2
			left = list(reversed(pts[half:]))  # from (x0, zs) to the apex
			for a, b in zip(left[:-1], left[1:]):
				B.poly([F.p(x0, oz1), F.p(a[0], a[1]), F.p(b[0], b[1])], material)
			right = list(reversed(pts[: half + 1]))  # from the apex to (x1, zs)
			for a, b in zip(right[:-1], right[1:]):
				B.poly([F.p(x1, oz1), F.p(a[0], a[1]), F.p(b[0], b[1])], material)
			# The arch's soffit.
			for a, b in zip(pts[:-1], pts[1:]):
				B.poly([F.p(a[0], a[1]), F.p(b[0], b[1]), F.p(b[0], b[1], d), F.p(a[0], a[1], d)], reveal)
		else:
			top = oz1
			B.poly([F.p(x1, top), F.p(x0, top), F.p(x0, top, d), F.p(x1, top, d)], reveal)
		B.poly([F.p(x0, oz0), F.p(x0, oz0, d), F.p(x0, top, d), F.p(x0, top)], reveal)
		B.poly([F.p(x1, oz0), F.p(x1, top), F.p(x1, top, d), F.p(x1, oz0, d)], reveal)
		B.poly([F.p(x0, oz0), F.p(x1, oz0), F.p(x1, oz0, d), F.p(x0, oz0, d)], reveal)
		if not o["through"] and o["back"]:
			if o["arch"]:
				pts, zsp = arch_points(o)
				back = [F.p(x0, oz0, d), F.p(x1, oz0, d)] + [F.p(a[0], a[1], d) for a in pts]
				B.poly(back, o["back"])
			else:
				B.poly([F.p(x0, oz0, d), F.p(x1, oz0, d), F.p(x1, oz1, d), F.p(x0, oz1, d)], o["back"])


def fbox(B, F, u0, u1, z0, z1, w0, w1, material, bevel=0.015):
	"""A box on the wall: u0..u1 along it, z0..z1 up, w0..w1 into it (negative: out of it)."""
	c = F.p((u0 + u1) * 0.5, (z0 + z1) * 0.5, (w0 + w1) * 0.5)
	rot_z = math.atan2(F.r.y, F.r.x)
	B.box(c, (abs(u1 - u0), abs(w1 - w0), abs(z1 - z0)), material, bevel=bevel, rot_z=rot_z)


def louvred_shutter(B, F, u0, u1, z0, z1, w0, w1, material, tilt=38.0, pitch=0.05):
	"""Milestone 158 (the owner's reference: its shutters' slats): a louvred shutter, u0..u1 along the wall, z0..z1 up,
	w0..w1 into it — stiles, rails top, middle and bottom, and between them slats every `pitch` tilted `tilt` degrees
	(their shadows' lines across it)."""
	import mathutils

	rz = math.atan2(F.r.y, F.r.x)
	sw = 0.05
	fbox(B, F, u0, u0 + sw, z0, z1, w0, w1, material, bevel=0.004)
	fbox(B, F, u1 - sw, u1, z0, z1, w0, w1, material, bevel=0.004)
	zm = (z0 + z1) * 0.5
	for a, b in ((z0, z0 + 0.07), (zm - 0.03, zm + 0.03), (z1 - 0.06, z1)):
		fbox(B, F, u0 + sw, u1 - sw, a, b, w0, w1, material, bevel=0.004)
	wm = (w0 + w1) * 0.5
	for a, b in ((z0 + 0.07, zm - 0.03), (zm + 0.03, z1 - 0.06)):
		n = max(1, int((b - a) / pitch))
		for k in range(n):
			zc = a + (k + 0.5) * (b - a) / n
			c = F.p((u0 + u1) * 0.5, zc, wm)
			rot = mathutils.Matrix.Rotation(rz, 4, "Z") @ mathutils.Matrix.Rotation(math.radians(-tilt), 4, "X")
			B.box(c, (u1 - u0 - 2 * sw + 0.004, 0.009, (b - a) / n * 1.25), material, rot=rot)


def corbel(B, F, u, z_top, depth, height, width, material="stone"):
	"""Milestone 158 (the reference's balconette): a carved corbel under a slab — a scroll: flat under the slab, out to
	`depth`, curving down and back to the wall with a curl at its foot; `width` across, centred at u."""
	pts = [(0.0, 0.0), (depth, 0.0), (depth, 0.1 * height)]
	for k in range(1, 9):
		a = k / 8.0 * math.pi * 0.5
		pts.append((0.38 * depth + 0.62 * depth * math.cos(a), 0.1 * height + 0.62 * height * math.sin(a)))
	for k in range(1, 7):
		a = math.pi * 0.5 + k / 6.0 * math.pi * 1.25
		r = 0.12 * height
		pts.append((0.38 * depth + r * 0.9 * math.cos(a) + 0.02, 0.72 * height + r + r * math.sin(a) * -1.0 + 0.0))
	pts += [(0.18 * depth, height), (0.0, height)]
	# the profile in the wall's (w out, z) plane, extruded across u0..u1
	u0, u1 = u - width * 0.5, u + width * 0.5
	ring0 = [F.p(u0, z_top - y, -x) for x, y in pts]
	ring1 = [F.p(u1, z_top - y, -x) for x, y in pts]
	B.poly(list(reversed(ring0)), material)
	B.poly(ring1, material)
	n = len(pts)
	for k in range(n):
		j = (k + 1) % n
		B.poly([ring0[k], ring0[j], ring1[j], ring1[k]], material)


def casement(B, F, x0, x1, z0, z1, depth=0.13, material="wood"):
	"""Milestone 158: a window's wooden casement in its opening (the reference's: two leaves of three panes) — the frame,
	the leaves' meeting stiles, two glazing bars across each."""
	f = 0.055
	w0, w1 = depth - 0.05, depth
	fbox(B, F, x0, x0 + f, z0, z1, w0, w1, material, bevel=0.005)
	fbox(B, F, x1 - f, x1, z0, z1, w0, w1, material, bevel=0.005)
	fbox(B, F, x0 + f, x1 - f, z0, z0 + f * 1.3, w0, w1, material, bevel=0.005)
	fbox(B, F, x0 + f, x1 - f, z1 - f, z1, w0, w1, material, bevel=0.005)
	um = (x0 + x1) * 0.5
	fbox(B, F, um - 0.035, um + 0.035, z0 + f, z1 - f, w0 - 0.01, w1, material, bevel=0.005)
	for k in (1, 2):
		zz = z0 + f * 1.3 + (z1 - z0 - f * 2.3) * k / 3.0
		fbox(B, F, x0 + f, x1 - f, zz - 0.016, zz + 0.016, w0 + 0.01, w1, material, bevel=0.003)


def window(B, F, u, z, w=1.0, h=1.8, kind="plain", shutters=None, shutters_closed=False, rng=None, Bc=None):
	"""A window centred at u, its sill at z: the opening (to be cut by wall()), its stone frame, its sill (a handhold),
	a cornice over it on the piano nobile, a hood over a plain one (handholds too), or a pediment; its shutters. With
	Bc, the holds go into the collision (the climbing's ledges: FellowTraversal3D hangs from them and reaches or leaps
	from one to the next). Returns the opening."""
	x0, x1 = u - w * 0.5, u + w * 0.5
	arch = kind == "arched"
	o = opening(x0, x1, z, z + h, arch=arch, back="glass", depth=0.2)
	f = 0.11  # the frame's width
	if arch:
		pts, zs = arch_points(o, 8)
		fbox(B, F, x0 - f, x0, z, zs, -0.04, 0.02, "stone")
		fbox(B, F, x1, x1 + f, z, zs, -0.04, 0.02, "stone")
		for a, b in zip(pts[:-1], pts[1:]):
			# The arch's voussoirs: a box along each segment, outside the opening.
			mx, mz = (a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5
			cx, cz = u, zs
			d = Vector((mx - cx, mz - cz)).normalized()
			seg = Vector((b[0] - a[0], b[1] - a[1])).length
			c = F.p(mx + d.x * f * 0.5, mz + d.y * f * 0.5, -0.01)
			import mathutils

			rz = math.atan2(F.r.y, F.r.x)
			ang = math.atan2(d.y, d.x)
			rot = mathutils.Matrix.Rotation(rz, 4, "Z") @ mathutils.Matrix.Rotation(-(ang - math.pi * 0.5), 4, "Y")
			B.box(c, (seg + 0.02, 0.06, f), "stone", bevel=0.01, rot=rot)
		fbox(B, F, u - 0.09, u + 0.09, z + h - 0.02, z + h + 0.16, -0.06, 0.02, "stone")  # the keystone
	else:
		fbox(B, F, x0 - f, x0, z, z + h + f, -0.04, 0.02, "stone")
		fbox(B, F, x1, x1 + f, z, z + h + f, -0.04, 0.02, "stone")
		fbox(B, F, x0, x1, z + h, z + h + f, -0.04, 0.02, "stone")
	# The sill: wider than the frame, standing out on two small brackets (a hand's hold).
	fbox(B, F, x0 - f - 0.08, x1 + f + 0.08, z - 0.12, z, -0.17, 0.02, "stone", bevel=0.02)
	for bu in (x0 - f * 0.4, x1 + f * 0.4):
		fbox(B, F, bu - 0.05, bu + 0.05, z - 0.26, z - 0.12, -0.11, 0.02, "stone", bevel=0.01)
	if Bc is not None:
		fbox(Bc, F, x0 - f - 0.08, x1 + f + 0.08, z - 0.12, z, -0.17, 0.0, "stone", bevel=0.0)
	if kind == "nobile":
		# A cornice on brackets over the window.
		top = z + h + f
		fbox(B, F, x0 - f - 0.1, x1 + f + 0.1, top + 0.02, top + 0.16, -0.16, 0.02, "stone", bevel=0.02)
		for bu in (x0 - f * 0.5, x1 + f * 0.5):
			fbox(B, F, bu - 0.05, bu + 0.05, top - 0.16, top + 0.02, -0.12, 0.02, "stone", bevel=0.01)
		if Bc is not None:
			fbox(Bc, F, x0 - f - 0.1, x1 + f + 0.1, top + 0.02, top + 0.16, -0.16, 0.0, "stone", bevel=0.0)
	elif kind == "plain":
		# A hood over the frame: a moulded lintel standing out.
		top = z + h + f
		fbox(B, F, x0 - f - 0.06, x1 + f + 0.06, top, top + 0.12, -0.15, 0.02, "stone", bevel=0.02)
		if Bc is not None:
			fbox(Bc, F, x0 - f - 0.06, x1 + f + 0.06, top, top + 0.12, -0.15, 0.0, "stone", bevel=0.0)
	elif kind == "pediment":
		top = z + h + f
		fbox(B, F, x0 - f - 0.1, x1 + f + 0.1, top, top + 0.1, -0.14, 0.02, "stone", bevel=0.015)
		if Bc is not None:
			# Its base: a hold too (milestone 152).
			fbox(Bc, F, x0 - f - 0.1, x1 + f + 0.1, top, top + 0.1, -0.14, 0.0, "stone", bevel=0.0)
		half = (w + 2 * f + 0.2) * 0.5
		rise = 0.38
		pts = [(u - half, top + 0.1), (u + half, top + 0.1), (u, top + 0.1 + rise)]
		for k in range(3):
			a, b = pts[k], pts[(k + 1) % 3]
			# The pediment's raking and bottom edges as one triangular slab.
		front = [F.p(p[0], p[1], -0.12) for p in pts]
		back = [F.p(p[0], p[1], 0.02) for p in pts]
		B.poly(front, "stone")
		for k in range(3):
			a, b = k, (k + 1) % 3
			B.poly([front[b], front[a], back[a], back[b]], "stone")
	if not arch:
		casement(B, F, x0, x1, z, z + h)
	if shutters:
		t = 0.035
		if shutters_closed:
			hh = h - (0.02 if not arch else w * 0.5)
			louvred_shutter(B, F, x0 + 0.01, u - 0.005, z + 0.02, z + hh, 0.04, 0.04 + t, shutters)
			louvred_shutter(B, F, u + 0.005, x1 - 0.01, z + 0.02, z + hh, 0.04, 0.04 + t, shutters)
		else:
			sw = w * 0.5
			hh = h if not arch else h - w * 0.5
			louvred_shutter(B, F, x0 - f - sw - 0.02, x0 - f - 0.02, z + 0.02, z + hh, -0.05 - t, -0.05, shutters)
			louvred_shutter(B, F, x1 + f + 0.02, x1 + f + sw + 0.02, z + 0.02, z + hh, -0.05 - t, -0.05, shutters)
			# The hinges' pins, iron.
			for zz in (z + 0.25, z + hh - 0.25):
				fbox(B, F, x0 - f - 0.03, x0 - f, zz - 0.03, zz + 0.03, -0.07, -0.02, "iron", bevel=0.0)
				fbox(B, F, x1 + f, x1 + f + 0.03, zz - 0.03, zz + 0.03, -0.07, -0.02, "iron", bevel=0.0)
	return o


def string_course(B, facades, z, out=0.14, h=0.22, material="stone"):
	"""A moulded stone band round the building at z (a ledge to stand and hang on): two steps."""
	for i, F in enumerate(facades):
		ext = out if i % 2 == 0 else -0.001
		fbox(B, F, -ext, F.length + ext, z - h * 0.5, z, -out * 0.6, 0.02, material, bevel=0.015)
		fbox(B, F, -ext, F.length + ext, z, z + h * 0.5, -out, 0.02, material, bevel=0.02)


def quoins(B, facades, z0, z1, a=0.62, b=0.32, h=0.34, out=0.035):
	"""Long and short stone blocks in turn up each corner."""
	n = len(facades)
	for i in range(n):
		F = facades[i]
		G = facades[(i + 1) % n]
		corner = F.p(F.length, 0)
		k = 0
		z = z0
		while z + h <= z1 + 1e-3:
			la, lb = (a, b) if k % 2 == 0 else (b, a)
			# Along F (back from the corner) by la, along G by lb, out of both by out.
			p0 = corner - F.r * la + F.n * out
			p1 = corner + G.r * lb + G.n * out
			# The block as the box spanning both arms (an L would be two boxes; one box covering the corner suffices).
			fbox(B, F, F.length - la, F.length + out, z + 0.01, z + h - 0.01, -out, 0.05, "stone", bevel=0.02)
			fbox(B, G, -out, lb, z + 0.01, z + h - 0.01, -out, 0.05, "stone", bevel=0.02)
			z += h
			k += 1


def eaves(B, Bc, facades, w, d, zt, overhang, pitch_deg, kind, rng, chimneys=1, cornice=True):
	"""The roof: a stone cornice at the wall's top (unless cornice is False: the rafters straight out of the wall), the
	eave on wooden rafters (a hip or a gable roof), the coppi along the eaves and the ridges; returns the ridge's
	height."""
	tanp = math.tan(math.radians(pitch_deg))
	# The cornice: a moulded band, then the soffit's planks on rafters.
	for i, F in enumerate(facades if cornice else []):
		ext = 0.26 if i % 2 == 0 else -0.001
		fbox(B, F, -ext, F.length + ext, zt - 0.36, zt - 0.2, -0.12, 0.02, "stone", bevel=0.02)
		fbox(B, F, -ext, F.length + ext, zt - 0.2, zt - 0.06, -0.24, 0.02, "stone", bevel=0.02)
	ex = w * 0.5 + overhang
	ey = d * 0.5 + overhang
	z0 = zt + 0.08
	hw, hd = w * 0.5, d * 0.5
	# The soffit (planks, seen from below) and the fascia.
	B.poly([(-ex, -ey, zt), (-hw, -hd, zt), (hw, -hd, zt), (ex, -ey, zt)], "wood")
	B.poly([(ex, -ey, zt), (hw, -hd, zt), (hw, hd, zt), (ex, ey, zt)], "wood")
	B.poly([(ex, ey, zt), (hw, hd, zt), (-hw, hd, zt), (-ex, ey, zt)], "wood")
	B.poly([(-ex, ey, zt), (-hw, hd, zt), (-hw, -hd, zt), (-ex, -ey, zt)], "wood")
	corners = [(-ex, -ey), (ex, -ey), (ex, ey), (-ex, ey)]
	for i in range(4):
		a, b = corners[i], corners[(i + 1) % 4]
		B.poly([(a[0], a[1], zt), (b[0], b[1], zt), (b[0], b[1], z0), (a[0], a[1], z0)], "wood_dark")
	# The rafters under the soffit, every 0.55 m.
	for F in facades:
		n = int(F.length / 0.55)
		for k in range(1, n):
			u = F.length * k / n
			fbox(B, F, u - 0.06, u + 0.06, zt - 0.16, zt - 0.005, -(overhang - 0.04), 0.0, "wood_dark", bevel=0.01)
	faces = []
	if kind == "hip":
		if ex >= ey:
			zr = z0 + ey * tanp
			r0, r1 = Vector((-(ex - ey), 0, zr)), Vector((ex - ey, 0, zr))
			faces = [
				[(-ex, -ey, z0), (ex, -ey, z0), tuple(r1), tuple(r0)],
				[(ex, -ey, z0), (ex, ey, z0), tuple(r1)],
				[(ex, ey, z0), (-ex, ey, z0), tuple(r0), tuple(r1)],
				[(-ex, ey, z0), (-ex, -ey, z0), tuple(r0)],
			]
		else:
			zr = z0 + ex * tanp
			r0, r1 = Vector((0, -(ey - ex), zr)), Vector((0, ey - ex, zr))
			faces = [
				[(-ex, -ey, z0), (ex, -ey, z0), tuple(r0)],
				[(ex, -ey, z0), (ex, ey, z0), tuple(r1), tuple(r0)],
				[(ex, ey, z0), (-ex, ey, z0), tuple(r1)],
				[(-ex, ey, z0), (-ex, -ey, z0), tuple(r0), tuple(r1)],
			]
		ridges = [(r0, r1)] + [(Vector((c[0], c[1], z0)), r0 if (c[0] < 0) == (ex >= ey) or ex < ey and c[1] < 0 else r1) for c in corners]
		if ex < ey:
			ridges = [(r0, r1)] + [(Vector((c[0], c[1], z0)), r0 if c[1] < 0 else r1) for c in corners]
		else:
			ridges = [(r0, r1)] + [(Vector((c[0], c[1], z0)), r0 if c[0] < 0 else r1) for c in corners]
	else:  # gable, its ridge along X
		zr = z0 + ey * tanp
		r0, r1 = Vector((-ex, 0, zr)), Vector((ex, 0, zr))
		faces = [
			[(-ex, -ey, z0), (ex, -ey, z0), tuple(r1), tuple(r0)],
			[(ex, ey, z0), (-ex, ey, z0), tuple(r0), tuple(r1)],
		]
		ridges = [(r0, r1)]
		# The gables: the side walls up to the roof (plastered), the verges' boards.
		for sx in (-1, 1):
			x = sx * hw
			pts = [(x, -hd, zt), (x, hd, zt), (x, 0, zr - 0.05)] if sx > 0 else [(x, hd, zt), (x, -hd, zt), (x, 0, zr - 0.05)]
			B.poly(pts, facades[1].plaster if hasattr(facades[1], "plaster") else "plaster_cream")
			for sy in (-1, 1):
				a = Vector((sx * ex, sy * ey, z0 - 0.1))
				b = Vector((sx * ex, 0, zr - 0.1))
				dv = b - a
				import mathutils

				B.box((a + b) * 0.5, (0.08, dv.length, 0.22), "wood_dark", rot=mathutils.Matrix.Rotation(math.atan2(dv.z, dv.y), 4, "X"))
	for f in faces:
		B.poly(f, "roof", uv_slope=True)
		# The collision at the coppi's mean height (milestone 158: they stand 3.5-12 cm over the face), so that hands and
		# feet come down on them, not through them.
		fv = [Vector(v) for v in f]
		fn = (fv[1] - fv[0]).cross(fv[2] - fv[0]).normalized()
		if fn.z < 0.0:
			fn = -fn
		Bc.poly([tuple(v + fn * 0.08) for v in fv], "roof")
	# The fascia under the eaves' level edges, in the collision (milestone 152): the eave's lip a ledge to climb over
	# onto the roof (its front a face, the tiles behind it a top).
	for i in range(4):
		a, b = corners[i], corners[(i + 1) % 4]
		along_x = abs(b[0] - a[0]) > abs(b[1] - a[1])
		if kind != "hip" and not along_x:
			continue  # a gable's verge, not level
		mx, my = (a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5
		ix = -0.03 if mx > 0 and not along_x else (0.03 if mx < 0 and not along_x else 0.0)
		iy = -0.03 if my > 0 and along_x else (0.03 if my < 0 and along_x else 0.0)
		ln = abs(b[0] - a[0]) if along_x else abs(b[1] - a[1])
		# (Milestone 158: up to the coppi's height, as the roof's collision now is: the lip's top meets the tiles' face.)
		Bc.box((mx + ix, my + iy, (zt + z0 + 0.08) * 0.5), (ln, 0.06, z0 - zt + 0.08) if along_x else (0.06, ln, z0 - zt + 0.08), "wood_dark")
	# The coppi (milestone 158, the owner's reference: the roofs covered with them, not only their eaves): on every face,
	# columns 0.26 m apart up the slope, each a course of half-tubes 0.45 m long laid 0.38 m apart (each over the next's
	# head), a little out of true; kept within the face (a hip's triangles), the eave's course standing out over the lip;
	# along the ridges and the hips, a row of caps.
	for f in faces:
		pts = [Vector(v) for v in f]
		# the face's eave: its lowest edge
		lo = min(range(len(pts)), key=lambda k: pts[k].z + pts[(k + 1) % len(pts)].z)
		a, b = pts[lo], pts[(lo + 1) % len(pts)]
		along = b - a
		t = along.normalized()
		outward = t.cross(UP)
		inward = -outward
		slope = (inward + UP * tanp).normalized()
		nrm = t.cross(slope).normalized()
		if nrm.z < 0.0:
			nrm = -nrm
		# the face in (t, s) coordinates from a
		poly2 = [((q - a).dot(t), (q - a).dot(slope)) for q in pts]

		def inside(x, y):
			c = False
			for k in range(len(poly2)):
				x1, y1 = poly2[k]
				x2, y2 = poly2[(k + 1) % len(poly2)]
				if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1 + 1e-12) + x1:
					c = not c
			return c

		xs = [q[0] for q in poly2]
		ys = [q[1] for q in poly2]
		col = 0.31
		row = 0.4
		ncol = int((max(xs) - min(xs)) / col)
		x_start = min(xs) + ((max(xs) - min(xs)) - ncol * col) * 0.5
		up = (UP - slope * slope.dot(UP)).normalized()
		for ci in range(ncol + 1):
			x = x_start + ci * col
			y = -0.02
			k = 0
			while y < max(ys):
				y0, y1 = y, y + 0.48
				if inside(x, y0 + 0.05) and inside(x, min(y1, max(ys) - 0.02) - 0.05) and inside(x - 0.08, y0 + 0.2) and inside(x + 0.08, y0 + 0.2):
					# Milestone 159 (the reference's roof close up: each coppo's lower end a thick rounded step over
					# the one below, the channels between the rows dark): tapered cover tiles, wide end down, lifted
					# at their lower end over the course below; a pan tile in each channel, concave, under them.
					jx = rng.uniform(-0.012, 0.012)
					# the cover's edges down on the pans' rims; its wide lower end over the narrow head of the one below
					base = a + t * (x + jx) + slope * y0 + nrm * (0.03 + rng.uniform(-0.004, 0.004))
					tip = base + slope * (min(y1, max(ys) - 0.02) - y0) - nrm * 0.016 + t * rng.uniform(-0.01, 0.01)
					if k == 0:
						base = base - slope * 0.04
					# Milestone 162 (the reference's roof beside ours: its tiles wide, low rounded humps in columns, a
					# narrow dark valley between them, each tile its own shade, each course's end a thick rounded step):
					# the cover 0.28 m wide (beside the reference's climber's hand: half again as wide as it) and 9 cm
					# high, tapering, tilted so that its wide end stands proud of the course below; a narrow pan in the
					# valley under its edges; the clay one of three firings.
					rr = rng.uniform(-0.004, 0.004)
					m = rng.choices(("coppo", "coppo_b", "coppo_c"), weights=(0.5, 0.25, 0.25))[0]
					B.coppo(base, tip, 0.14 + rr, 0.124 + rr, m, up=up, segments=7, lip=0.02, flat=0.62)
					if inside(x + col * 0.5, y0 + 0.2) and inside(x + col * 0.5 + 0.1, y0 + 0.2):
						# (the pan low in its valley: the valley's floor deep and in shade, as the reference's)
						pb = a + t * (x + col * 0.5) + slope * y0 + nrm * 0.05
						pt = pb + slope * (min(y1, max(ys) - 0.02) - y0)
						B.coppo(pb, pt, 0.065, 0.06, "coppo_b", up=up, segments=3, lip=0.0, concave=True)
				y += row
				k += 1
	for a, b in ridges:
		B.half_tube(a + UP * 0.05, b + UP * 0.05, 0.12, "roof", up=UP, segments=6)
	# Chimneys.
	for c in range(chimneys):
		cx = (rng.uniform(-0.3, 0.3)) * w
		cy = (rng.uniform(-0.25, 0.25)) * d
		top = zr + rng.uniform(0.6, 1.2)
		B.box((cx, cy, (z0 + top) * 0.5), (0.7, 0.6, top - z0), "plaster_cream", bevel=0.02)
		B.box((cx, cy, top + 0.06), (0.9, 0.8, 0.12), "stone", bevel=0.02)
		for sx in (-1, 1):
			B.box((cx + sx * 0.28, cy, top + 0.3), (0.12, 0.6, 0.36), "plaster_cream", bevel=0.01)
		B.box((cx, cy, top + 0.53), (0.95, 0.85, 0.1), "roof", bevel=0.02)
		Bc.box((cx, cy, (z0 + top) * 0.5), (0.7, 0.6, top - z0), "plaster_cream")
	return zr


def terrace(B, Bc, facades, w, d, zt, rng, altana=True, plaster="plaster_cream"):
	"""A flat roof with a parapet (its coping a ledge to climb onto) and, on part of it, an altana: a loggia of wooden
	posts under a small tiled roof."""
	hw, hd = w * 0.5, d * 0.5
	B.poly([(-hw, -hd, zt), (hw, -hd, zt), (hw, hd, zt), (-hw, hd, zt)], "ground_stone")
	ph = 0.95
	t = 0.3
	for i, F in enumerate(facades):
		ext = 0.0
		wall(B, F, zt, zt + ph, [], plaster)
		inner = Facade(F.p(F.length - t, 0, t), -F.r, -F.n, F.length - 2 * t)
		wall(B, inner, zt, zt + ph, [], plaster)
		fbox(B, F, -0.06, F.length + 0.06, zt + ph, zt + ph + 0.12, -0.08, t + 0.04, "stone", bevel=0.02)
		fbox(Bc, F, 0.0, F.length, zt, zt + ph + 0.12, -0.08, t + 0.04, "stone", bevel=0.0)
	# The cornice under the parapet.
	for i, F in enumerate(facades):
		ext = 0.24 if i % 2 == 0 else -0.001
		fbox(B, F, -ext, F.length + ext, zt - 0.3, zt - 0.12, -0.14, 0.02, "stone", bevel=0.02)
		fbox(B, F, -ext, F.length + ext, zt - 0.12, zt + 0.02, -0.22, 0.02, "stone", bevel=0.02)
	if altana:
		ax0, ax1 = hw * 0.1, hw - 0.5
		ay0, ay1 = -hd + 0.5, hd - 0.5
		zh = zt + 2.6
		for x in (ax0, (ax0 + ax1) * 0.5, ax1):
			for y in (ay0, ay1):
				B.box((x, y, (zt + zh) * 0.5), (0.18, 0.18, zh - zt), "wood_dark", bevel=0.02)
				Bc.box((x, y, (zt + zh) * 0.5), (0.18, 0.18, zh - zt), "wood_dark")
		for y in (ay0, ay1):
			B.box(((ax0 + ax1) * 0.5, y, zh + 0.08), (ax1 - ax0 + 0.3, 0.2, 0.16), "wood_dark", bevel=0.02)
		for x in (ax0, ax1):
			B.box((x, 0, zh + 0.08), (0.2, ay1 - ay0 + 0.3, 0.16), "wood_dark", bevel=0.02)
		# Its roof: a low hip.
		ex2, ey2 = (ax1 - ax0) * 0.5 + 0.5, (ay1 - ay0) * 0.5 + 0.5
		cx = (ax0 + ax1) * 0.5
		z0 = zh + 0.16
		zr = z0 + min(ex2, ey2) * 0.4
		if ex2 >= ey2:
			r0, r1 = (cx - (ex2 - ey2), 0, zr), (cx + (ex2 - ey2), 0, zr)
		else:
			r0, r1 = (cx, -(ey2 - ex2), zr), (cx, ey2 - ex2, zr)
		c = [(cx - ex2, -ey2, z0), (cx + ex2, -ey2, z0), (cx + ex2, ey2, z0), (cx - ex2, ey2, z0)]
		if ex2 >= ey2:
			roof = [[c[0], c[1], r1, r0], [c[1], c[2], r1], [c[2], c[3], r0, r1], [c[3], c[0], r0]]
		else:
			roof = [[c[0], c[1], r0], [c[1], c[2], r1, r0], [c[2], c[3], r1], [c[3], c[0], r0, r1]]
		for f in roof:
			B.poly(f, "roof", uv_slope=True)
			Bc.poly(f, "roof", group="altana_roof")
		B.poly([c[0], c[3], c[2], c[1]], "wood")
		B.half_tube(Vector(r0) + UP * 0.04, Vector(r1) + UP * 0.04, 0.1, "roof")
		# Pots of plants and a bench on the terrace.
		for k in range(3):
			px = rng.uniform(-hw + 0.8, ax0 - 0.5)
			py = rng.uniform(-hd + 0.8, hd - 0.8)
			B.lathe([(0.0, 0.0), (0.18, 0.0), (0.24, 0.4), (0.26, 0.42), (0.0, 0.42)], (px, py, zt), "roof", segments=12)
	return zt + ph + 0.12


def house(name, w, d, floors, plaster, roof="hip", seed=1, shutters="shutter_green", ground="shops", nobile="nobile",
		balcony=False, altana=False, overhang=0.85, pitch=21.0, banner=None, lamps=True, arched_upper=False, benches=False):
	"""A palazzo: a rusticated ground floor (4.4 m) with doors and shops, floors of 3.4 m above."""
	import random

	rng = random.Random(seed)
	B = Builder(seed)
	Bc = Collider(seed)
	gh = 4.4
	fh = 3.4
	zt = gh + fh * (floors - 1)
	facades = rect_facades(w, d)
	rustic = [F.offset(0.1) for F in facades]
	for i, F in enumerate(facades):
		F.plaster = plaster
	# The ground floor: rusticated stone, shops and doors in arches.
	for fi, R in enumerate(rustic):
		opens = []
		n = max(1, int((R.length - 1.0) / 3.4))
		for k in range(n):
			u = R.length * (k + 0.5) / n
			kind = rng.random()
			if ground == "shops" and kind < 0.65:
				ww = rng.choice([2.2, 2.4, 2.6])
				opens.append(opening(u - ww * 0.5, u + ww * 0.5, 0.0, 3.4, arch=True, back="glass", depth=0.45))
			elif kind < 0.85:
				ww = 1.5
				opens.append(opening(u - ww * 0.5, u + ww * 0.5, 0.0, 3.0, arch=True, back="wood_dark", depth=0.4))
			else:
				# A small barred window, high.
				opens.append(opening(u - 0.45, u + 0.45, 2.2, 3.2, arch=False, back="glass", depth=0.3))
				for b in range(5):
					bu = u - 0.36 + 0.18 * b
					fbox(B, R, bu - 0.015, bu + 0.015, 2.2, 3.2, 0.1, 0.13, "iron", bevel=0.0)
		wall(B, R, 0.0, gh, opens, "stone_rustic", reveal="stone_rustic")
		# The impost band at the arches' springing, between the openings: the first ledge up from the street.
		spans = sorted([(o["x0"], o["x1"]) for o in opens if o["z0"] < 2.2])
		prev = -0.05
		for a, b in spans + [(R.length + 0.05, R.length + 0.05)]:
			if a - prev > 0.5:
				fbox(B, R, prev, a, 2.06, 2.22, -0.13, 0.02, "stone", bevel=0.02)
				fbox(Bc, R, prev, a, 2.06, 2.22, -0.13, 0.0, "stone", bevel=0.0)
			prev = b
		# Steps at the doors and the shops (on the pavement), stone benches along the front between them (Florence's
		# palaces' panche di via).
		for o in opens:
			if o["z0"] < 0.01:
				deep = 0.4 if o["back"] == "wood_dark" else 0.26
				fbox(B, R, o["x0"] - 0.14, o["x1"] + 0.14, 0.0, 0.17, -deep, 0.02, "stone", bevel=0.015)
		if benches and fi == 0:
			edges = sorted([(o["x0"], o["x1"]) for o in opens if o["z0"] < 0.01])
			gaps = []
			prev = 0.6
			for a, b in edges + [(R.length - 0.6, R.length)]:
				if a - 0.35 - (prev + 0.35) > 1.2:
					gaps.append((prev + 0.35, a - 0.35))
				prev = b
			for gi, (u0, u1) in enumerate(gaps):
				fbox(B, R, u0, u1, 0.0, 0.38, -0.36, 0.02, "stone_rustic", bevel=0.02)
				fbox(B, R, u0 - 0.03, u1 + 0.03, 0.38, 0.47, -0.45, 0.02, "stone", bevel=0.02)
				fbox(Bc, R, u0, u1, 0.0, 0.47, -0.45, 0.02, "stone", bevel=0.0)
				# Seats on the bench for the square's people (rooftops_crowd.gd): on the ground in front of it, facing out.
				n_seats = max(1, int((u1 - u0) / 0.9))
				for k in range(n_seats):
					p = R.p(u0 + (u1 - u0) * (k + 0.5) / n_seats, 0.0, -0.4)
					empty(f"{name}_seat_{gi}_{k}", (p.x, p.y, p.z), rot_z=math.atan2(R.n.y, R.n.x))
		for o in opens:
			if o["arch"]:
				# The arch's voussoirs, rusticated: radial blocks.
				pts, zs = arch_points(o, 9)
				fbox(B, R, (o["x0"] + o["x1"]) * 0.5 - 0.14, (o["x0"] + o["x1"]) * 0.5 + 0.14, o["z1"] - 0.05, o["z1"] + 0.32, -0.05, 0.05, "stone", bevel=0.02)
			if lamps and o["back"] == "wood_dark" and rng.random() < 0.6:
				lu = o["x1"] + 0.45
				if lu < R.length - 0.3:
					fbox(B, R, lu - 0.03, lu + 0.03, 2.55, 2.61, -0.45, 0.0, "iron", bevel=0.0)
					p = R.p(lu, 2.35, -0.45)
					B.box((p.x, p.y, p.z), (0.18, 0.18, 0.28), "iron", bevel=0.01)
					e = empty(f"{name}_lamp_{fi}_{len(opens)}", (p.x, p.y, p.z))
		# The ledge on the rustication's top.
		fbox(B, R, -0.05, R.length + 0.05, gh - 0.12, gh, -0.06, 0.1, "stone", bevel=0.02)
		fbox(Bc, R, -0.05, R.length + 0.05, gh - 0.14, gh, -0.08, 0.0, "stone", bevel=0.0)
	# Upper floors.
	for fl in range(1, floors):
		z = gh + fh * (fl - 1)
		for fi, F in enumerate(facades):
			opens = []
			n = max(1, int((F.length - 0.8) / 2.7))
			for k in range(n):
				u = F.length * (k + 0.5) / n
				kind = nobile if fl == 1 else ("arched" if arched_upper else "plain")
				closed = rng.random() < 0.25
				if balcony and fl == 1 and fi == 0 and k == n // 2:
					# A balcony: the window a door to it, a stone slab on brackets, an iron railing.
					opens.append(opening(u - 0.6, u + 0.6, z + 0.05, z + 2.6, arch=False, back="glass", depth=0.2))
					fbox(B, F, u - 0.6 - 0.11, u - 0.6, z, z + 2.71, -0.04, 0.02, "stone")
					fbox(B, F, u + 0.6, u + 0.71, z, z + 2.71, -0.04, 0.02, "stone")
					fbox(B, F, u - 0.6, u + 0.6, z + 2.6, z + 2.71, -0.04, 0.02, "stone")
					fbox(B, F, u - 1.2, u + 1.2, z - 0.15, z + 0.03, -0.95, 0.02, "stone", bevel=0.03)
					for bu in (u - 1.0, u, u + 1.0):
						fbox(B, F, bu - 0.08, bu + 0.08, z - 0.55, z - 0.15, -0.7, 0.02, "stone", bevel=0.02)
					for kk in range(17):
						bu = u - 1.12 + kk * 0.14
						fbox(B, F, bu - 0.012, bu + 0.012, z + 0.03, z + 1.0, -0.92, -0.9, "iron", bevel=0.0)
					fbox(B, F, u - 1.15, u + 1.15, z + 0.98, z + 1.04, -0.94, -0.88, "iron", bevel=0.0)
					for side in (-1, 1):
						for kk in range(6):
							ww = -0.88 + kk * 0.14
							bu = u + side * 1.13
							fbox(B, F, bu - 0.012, bu + 0.012, z + 0.03, z + 1.0, ww - 0.012, ww + 0.012, "iron", bevel=0.0)
					Bc.box(F.p(u, z - 0.06, -0.47), (2.4 if abs(F.r.x) > 0.5 else 0.95, 0.95 if abs(F.r.x) > 0.5 else 2.4, 0.18), "stone")
					continue
				sh = shutters if rng.random() < 0.85 else None
				opens.append(window(B, F, u, z + 0.95, 1.0, 1.85 if fl == 1 else 1.7, kind, sh, closed, rng, Bc=Bc))
			wall(B, F, z, z + fh, opens, plaster)
		if fl < floors - 1:
			string_course(B, facades, z + fh, out=0.13, h=0.2)
		quoins(B, facades, z + 0.1, z + fh - 0.12)
		# Wooden beam ends (old putlog holes' beams) under some windows: handholds.
		for fi, F in enumerate(facades):
			if rng.random() < 0.4:
				u = rng.uniform(0.6, F.length - 0.6)
				fbox(B, F, u - 0.08, u + 0.08, z + 0.45, z + 0.61, -0.3, 0.0, "wood_dark", bevel=0.015)
	# Banner poles.
	if banner:
		for fi, u, fl in banner:
			F = facades[fi]
			z = gh + fh * (fl - 1) + 2.9
			p0 = F.p(u, z, 0.0)
			p1 = F.p(u, z, -1.3)
			B.cylinder(p0, p1, 0.025, "iron", segments=6)
			B.box(tuple(p1), (0.06, 0.06, 0.06), "bronze", bevel=0.0)
			empty(f"{name}_banner_{fi}_{fl}", tuple(p1), rot_z=math.atan2(F.n.y, F.n.x))
	# The collision: the walls flat (the windows not cut), the rustication's ledge, the string courses, the roof.
	Bc.box((0, 0, gh * 0.5), (w + 0.2, d + 0.2, gh), "stone")
	Bc.box((0, 0, (gh + zt) * 0.5), (w, d, zt - gh), "stone")
	for fl in range(1, floors - 1):
		z = gh + fh * fl
		Bc.box((0, 0, z), (w + 0.26, d + 0.26, 0.2), "stone")
	if roof == "terrace":
		top = terrace(B, Bc, facades, w, d, zt, rng, altana=altana, plaster=plaster)
	else:
		top = eaves(B, Bc, facades, w, d, zt, overhang, pitch, roof, rng, chimneys=rng.choice([1, 2]))
		Bc.box((0, 0, zt - 0.2), (w + 0.48, d + 0.48, 0.32), "stone")
	obj = B.to_object(name)
	col = Bc.to_object(name + "_col")
	return obj, col, top


def reference_house_measures():
	"""Milestone 160: the reference's house as measured through its matchmoved camera (reference_performance.json's
	"house", in metres: positions along the front from its left corner, heights from the ground)."""
	import json
	import os

	path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "reference_performance.json")
	with open(path) as f:
		return json.load(f)["house"]


def casa_riferimento(name="CasaRiferimento", seed=17):
	"""The house of the owner's reference video (milestone 157), built to its measures — since milestone 160 as its
	front was measured through the video's own camera, matchmoved from its background (reference_performance.json's
	"house"): a low house of honey sandstone ashlar; on its front, near the left corner, an arched door under a tall
	window with louvred shutters open; near the right corner the climbed window — a stone frame, a casement, no
	shutters — over a balconette on two carved corbels with a mask between them; a stone bench below it; no cornice,
	the eave's rafters straight out of the wall, a zinc gutter at its lip; a gable roof of coppi. The front faces -Y;
	the replica's frame in reference_performance.json is this model's."""
	import random

	H = reference_house_measures()
	rng = random.Random(seed)
	B = Builder(seed)
	Bc = Collider(seed)
	w, d = H["w"], H["d"]
	lip_out, pitch = H["lip_out"], H["pitch"]
	zt = H["zt"]
	facades = rect_facades(w, d)
	for F in facades:
		F.plaster = "plaster_cream"
	front = facades[0]
	opens = []
	# The door: arched, oak, in a frame of voussoirs.
	d0, d1, dtop = H["door"]
	od = window(B, front, (d0 + d1) * 0.5, 0.0, d1 - d0, dtop, "arched", None, False, rng)
	od["back"] = "wood_dark"
	od["depth"] = 0.3
	opens.append(od)
	fbox(B, front, d0 - 0.25, d1 + 0.25, 0.0, 0.12, -0.25, 0.02, "stone", bevel=0.015)
	# The tall window over it: its sill, frame and hood; louvred shutters open.
	a0, a1, az0, az1 = H["window1"]
	opens.append(window(B, front, (a0 + a1) * 0.5, az0, a1 - a0, az1 - az0, "plain", "shutter_brown", False, rng, Bc=Bc))
	# The climbed window: its opening, a stone frame up to a lintel, a casement; the balconette under it.
	x0, x1, wz0, wz1 = H["window"]
	sill = H["sill"]
	opens.append(opening(x0, x1, wz0, wz1, arch=False, back="glass", depth=0.22))
	ftop = H["window_frame_top"]
	f = 0.1
	fbox(B, front, x0 - f, x0, wz0, ftop, -0.05, 0.02, "stone")
	fbox(B, front, x1, x1 + f, wz0, ftop, -0.05, 0.02, "stone")
	fbox(B, front, x0 - f, x1 + f, wz1, ftop, -0.05, 0.02, "stone")
	fbox(B, front, x0 - f - 0.05, x1 + f + 0.05, ftop, ftop + 0.08, -0.09, 0.02, "stone", bevel=0.015)
	casement(B, front, x0, x1, wz0, wz1)
	# The balconette: a slab with a moulded roll under its front, on two carved corbels; a mask between them.
	b0, b1, bt, bout = H["balconette"]
	fbox(B, front, b0, b1, sill - bt, sill, -bout, 0.02, "stone", bevel=0.025)
	B.cylinder(front.p(b0, sill - bt - 0.01, -bout + 0.03), front.p(b1, sill - bt - 0.01, -bout + 0.03), 0.022, "stone", segments=8)
	cb = H["corbel_bottom"]
	for bu in H["corbels"]:
		corbel(B, front, bu, sill - bt, bout - 0.04, sill - bt - cb, 0.14)
	mu, mz0, mz1 = H["mask"]
	mr = (mz1 - mz0) * 0.5
	B.cylinder(front.p(mu, (mz0 + mz1) * 0.5, 0.0), front.p(mu, (mz0 + mz1) * 0.5, -0.07), mr, "stone", segments=14)
	B.cylinder(front.p(mu, (mz0 + mz1) * 0.5 - mr * 0.15, -0.07), front.p(mu, (mz0 + mz1) * 0.5 - mr * 0.15, -0.12), mr * 0.55, "stone", segments=12)
	fbox(Bc, front, b0, b1, sill - bt, sill, -bout, 0.0, "stone", bevel=0.0)
	# The bench along the front under the climbed window (Florence's panche di via).
	s0, s1, sh, sd = H["bench"]
	s1 = min(s1, w - 0.15)
	fbox(B, front, s0, s1, 0.0, sh - 0.08, -sd + 0.06, 0.02, "stone_rustic", bevel=0.02)
	fbox(B, front, s0 - 0.03, s1 + 0.03, sh - 0.08, sh, -sd, 0.02, "stone", bevel=0.02)
	fbox(Bc, front, s0 - 0.03, s1 + 0.03, 0.0, sh, -sd, 0.0, "stone", bevel=0.0)
	wall(B, front, 0.0, zt, opens, "plaster_cream")
	# The other walls: a window each.
	for F in facades[1:]:
		o = [window(B, F, F.length * 0.5, sill, 1.0, wz1 - wz0, "plain", "shutter_brown", rng.random() < 0.5, rng, Bc=Bc)]
		wall(B, F, 0.0, zt, o, "plaster_cream")
	quoins(B, facades, 0.1, zt - 0.1)
	# A plinth along the walls' foot.
	for F in facades:
		fbox(B, F, -0.04, F.length + 0.04, 0.0, 0.3, -0.04, 0.02, "stone_rustic", bevel=0.02)
	Bc.box((0, 0, zt * 0.5), (w, d, zt), "stone")
	# (Milestone 162: no chimney — the reference's roof has none where its climber crosses it.)
	top = eaves(B, Bc, facades, w, d, zt, lip_out, pitch, "gable", rng, chimneys=0, cornice=False)
	# The gutter at the front's and back's eaves: a half-round of weathered zinc hung just under the lip, open above.
	for sy in (-1, 1):
		y = sy * (d * 0.5 + lip_out + 0.045)
		B.half_tube((-w * 0.5 - lip_out, y, zt + 0.01), (w * 0.5 + lip_out, y, zt + 0.01), 0.05, "zinc", up=Vector((0, 0, -1)), segments=8)
	obj = B.to_object(name)
	col = Bc.to_object(name + "_col")
	return obj, col, top


def campanile(name="Campanile", seed=7):
	"""A bell tower 6.2 m square: a rusticated base with a door, a brick shaft with stone pilasters at its corners and
	string courses (ledges) every six metres, slit windows and a clock; an open belfry (two arches a side, the bell on
	its beam, the floor's wooden beam out over the square), a cornice, a pyramid roof and an iron finial."""
	import random

	rng = random.Random(seed)
	B = Builder(seed)
	Bc = Collider(seed)
	w = 6.2
	facades = rect_facades(w, w)
	rustic = [F.offset(0.12) for F in facades]
	for fi, R in enumerate(rustic):
		opens = []
		if fi == 0:
			opens.append(opening(R.length * 0.5 - 0.8, R.length * 0.5 + 0.8, 0.0, 3.2, arch=True, back="wood_dark", depth=0.6))
		wall(B, R, 0.0, 4.0, opens, "stone_rustic", reveal="stone_rustic")
		fbox(B, R, -0.05, R.length + 0.05, 3.85, 4.0, -0.1, 0.12, "stone", bevel=0.02)
		fbox(Bc, R, -0.05, R.length + 0.05, 3.84, 4.0, -0.1, 0.0, "stone", bevel=0.0)
	shaft_top = 26.0
	# The climb up the front (Assassin's Creed's way: from hold to hold): under the belfry's right arch, a column of
	# holds — stone corbels in the rustication and the lower shaft, beams across old putlogs higher up — a reach apart
	# (about 1.1 m) or a leap (1.6 m, to each string course), each wide enough for both hands.
	F0 = facades[0]
	R0 = rustic[0]
	cu = F0.length * 0.72  # under the right arch (the beam goes out of the left one), clear of the clock
	holds = [(R0, 2.4, 0.0, "corbel"), (R0, 3.25, 0.04, "corbel")]
	for z0 in (4.0, 10.0, 16.0):
		for k, dz in enumerate((1.1, 2.2, 3.3, 4.4)):
			holds.append((F0, z0 + dz, 0.05 if k % 2 else -0.05, "corbel" if z0 < 12.0 else "beam"))
	holds += [(F0, 23.1, -0.05, "beam"), (F0, 24.2, 0.05, "beam"), (F0, 25.1, -0.04, "beam")]
	for Fh, z, du, kind in holds:
		u = cu + du
		# Every hold stands out as far from the shaft's face (0.22 m; the rustication is 0.12 m proud of it): the body
		# hanging from one has room past the others.
		out = 0.22 if Fh is F0 else 0.14
		if kind == "corbel":
			# A stone block on a smaller one under it.
			fbox(B, Fh, u - 0.38, u + 0.38, z - 0.15, z, -out + 0.01, 0.02, "stone", bevel=0.02)
			fbox(B, Fh, u - 0.2, u + 0.2, z - 0.3, z - 0.15, -out * 0.6, 0.02, "stone", bevel=0.015)
		else:
			# Two beam ends out of their putlog holes, a beam laid across them.
			for su in (-0.26, 0.26):
				fbox(B, Fh, u + su - 0.07, u + su + 0.07, z - 0.29, z - 0.15, -0.26, 0.05, "wood_dark", bevel=0.015)
			fbox(B, Fh, u - 0.42, u + 0.42, z - 0.15, z, -0.25, -0.05, "wood_dark", bevel=0.015)
		fbox(Bc, Fh, u - 0.38, u + 0.38, z - 0.15, z, -out, 0.0, "stone", bevel=0.0)
	foot = R0.p(cu, 0.0, -0.9)
	empty(name + "_climb", (foot.x, foot.y, foot.z))
	levels = [4.0, 10.0, 16.0, 22.0, shaft_top]
	for li in range(len(levels) - 1):
		z0, z1 = levels[li], levels[li + 1]
		for fi, F in enumerate(facades):
			opens = []
			if li in (1, 2):
				u = F.length * 0.5
				opens.append(opening(u - 0.22, u + 0.22, z0 + 2.2, z0 + 3.6, arch=True, back="glass", depth=0.5))
				fbox(B, F, u - 0.36, u + 0.36, z0 + 2.08, z0 + 2.2, -0.1, 0.02, "stone", bevel=0.02)
			wall(B, F, z0, z1, opens, "brick")
		string_course(B, facades, z1 - 0.12, out=0.2, h=0.26)
	# Pilasters up the corners.
	for i, F in enumerate(facades):
		fbox(B, F, -0.12, 0.62, 4.0, shaft_top - 0.25, -0.12, 0.02, "stone", bevel=0.02)
		fbox(B, F, F.length - 0.62, F.length + 0.12, 4.0, shaft_top - 0.25, -0.12, 0.02, "stone", bevel=0.02)
	# The clock on the front.
	c = facades[0].p(w * 0.5, 19.0, -0.06)
	import mathutils

	rot = mathutils.Matrix.Rotation(math.radians(90), 4, "X")
	B.cylinder(c + Vector((0, 0.06, 0)), c - Vector((0, 0.06, 0)), 0.95, "stone", segments=32, smooth=False)
	B.cylinder(c - Vector((0, 0.06, 0)), c - Vector((0, 0.09, 0)), 0.86, "plaster_white", segments=32, smooth=False)
	for k in range(12):
		a = 2 * math.pi * k / 12
		p = c - Vector((0, 0.1, 0)) + Vector((math.sin(a), 0, math.cos(a))) * 0.74
		B.box(tuple(p), (0.06, 0.02, 0.14), "iron", bevel=0.0, rot=mathutils.Matrix.Rotation(-a, 4, "Y"))
	B.box(tuple(c - Vector((0, 0.11, 0)) + Vector((0.2, 0, 0.2))), (0.05, 0.02, 0.62), "bronze", bevel=0.0, rot=mathutils.Matrix.Rotation(math.radians(-45), 4, "Y"))
	B.box(tuple(c - Vector((0, 0.12, 0)) + Vector((-0.15, 0, 0.1))), (0.05, 0.02, 0.4), "bronze", bevel=0.0, rot=mathutils.Matrix.Rotation(math.radians(60), 4, "Y"))
	# The belfry: walls 0.7 m thick, two arches a side through them.
	bz0, bz1 = shaft_top, shaft_top + 6.0
	t = 0.7
	for fi, F in enumerate(facades):
		opens = []
		for u in (F.length * 0.3, F.length * 0.7):
			opens.append(opening(u - 0.85, u + 0.85, bz0 + 0.9, bz0 + 4.9, arch=True, back=None, depth=t, through=True))
		wall(B, F, bz0, bz1, opens, "brick")
		inner = Facade(F.p(F.length - t, 0, t), -F.r, -F.n, F.length - 2 * t)
		inner_opens = [opening(inner.length - (o["x1"] - t), inner.length - (o["x0"] - t), o["z0"], o["z1"], arch=True, back=None, depth=0.0, through=True) for o in opens]
		wall(B, inner, bz0, bz1, inner_opens, "brick")
		for o in opens:
			# The balustrade's sill in each arch, and the columns' capitals.
			fbox(B, F, o["x0"], o["x1"], o["z0"] - 0.12, o["z0"], -0.08, t, "stone", bevel=0.02)
	# The floor and the ceiling inside.
	B.poly([(-w * 0.5 + t, -w * 0.5 + t, bz0 + 0.9), (w * 0.5 - t, -w * 0.5 + t, bz0 + 0.9), (w * 0.5 - t, w * 0.5 - t, bz0 + 0.9), (-w * 0.5 + t, w * 0.5 - t, bz0 + 0.9)], "wood")
	B.poly([(-w * 0.5 + t, -w * 0.5 + t, bz1 - 0.2), (-w * 0.5 + t, w * 0.5 - t, bz1 - 0.2), (w * 0.5 - t, w * 0.5 - t, bz1 - 0.2), (w * 0.5 - t, -w * 0.5 + t, bz1 - 0.2)], "wood")
	string_course(B, facades, bz0 + 0.12, out=0.32, h=0.3)
	# The bell on its beam.
	B.box((0, 0, bz1 - 1.0), (w - 2 * t, 0.28, 0.32), "wood_dark", bevel=0.02)
	B.lathe([(0.04, 0.0), (0.95, 0.05), (0.88, 0.25), (0.62, 0.7), (0.52, 1.25), (0.5, 1.55), (0.3, 1.7), (0.0, 1.72)], (0, 0, bz1 - 3.0), "bronze", segments=28)
	B.box((0, 0, bz1 - 1.2), (0.25, 0.25, 0.3), "iron", bevel=0.01)
	# The beam out over the square: from the front's left arch's sill, 2.4 m out.
	F0 = facades[0]
	u = F0.length * 0.3
	z = bz0 + 0.9
	# Its top flush with the arch's sill and the belfry's floor: walked out along.
	fbox(B, F0, u - 0.13, u + 0.13, z - 0.22, z + 0.005, -2.4, t + 0.6, "wood", bevel=0.02)
	fbox(Bc, F0, u - 0.13, u + 0.13, z - 0.22, z, -2.4, t + 0.6, "wood", bevel=0.0)
	end = F0.p(u, z, -2.25)
	empty(name + "_viewpoint", (end.x, end.y, end.z))
	# The cornice, the roof, the finial.
	string_course(B, facades, bz1, out=0.45, h=0.4)
	ex = w * 0.5 + 0.35
	zr = bz1 + 0.2 + 6.0
	c4 = [(-ex, -ex), (ex, -ex), (ex, ex), (-ex, ex)]
	top = (0.0, 0.0, zr)
	for i in range(4):
		a, b = c4[i], c4[(i + 1) % 4]
		B.poly([(a[0], a[1], bz1 + 0.2), (b[0], b[1], bz1 + 0.2), top], "roof", uv_slope=True)
		Bc.poly([(a[0], a[1], bz1 + 0.2), (b[0], b[1], bz1 + 0.2), top], "roof")
		B.half_tube(Vector((a[0], a[1], bz1 + 0.25)), Vector(top) + UP * 0.02, 0.1, "roof")
	B.poly([(c4[0][0], c4[0][1], bz1 + 0.2), (c4[3][0], c4[3][1], bz1 + 0.2), (c4[2][0], c4[2][1], bz1 + 0.2), (c4[1][0], c4[1][1], bz1 + 0.2)], "stone")
	B.lathe([(0.0, 0.0), (0.14, 0.0), (0.12, 0.3), (0.2, 0.42), (0.0, 0.62)], (0, 0, zr - 0.1), "bronze", segments=12)
	B.cylinder((0, 0, zr + 0.5), (0, 0, zr + 1.9), 0.035, "iron", segments=6)
	B.box((0, 0, zr + 1.55), (0.6, 0.05, 0.05), "iron", bevel=0.0)
	# Collision: the shaft (its ledges), the belfry's floor and its piers, the roof.
	Bc.box((0, 0, 2.0), (w + 0.24, w + 0.24, 4.0), "stone")
	Bc.box((0, 0, (4.0 + shaft_top) * 0.5), (w, w, shaft_top - 4.0), "brick")
	for z in levels[1:]:
		Bc.box((0, 0, z - 0.12), (w + 0.4, w + 0.4, 0.26), "stone")
	Bc.box((0, 0, bz0 + 0.45), (w, w, 0.9), "stone")
	for F in facades:
		for (u0, u1) in ((0.0, F.length * 0.3 - 0.85), (F.length * 0.3 + 0.85, F.length * 0.7 - 0.85), (F.length * 0.7 + 0.85, F.length)):
			fbox(Bc, F, u0, u1, bz0, bz1, 0.0, t, "brick", bevel=0.0)
		fbox(Bc, F, 0.0, F.length, bz0 + 4.9, bz1, 0.0, t, "brick", bevel=0.0)
	Bc.box((0, 0, bz1 + 0.1), (w + 0.9, w + 0.9, 0.2), "stone")
	obj = B.to_object(name)
	col = Bc.to_object(name + "_col")
	return obj, col, zr


def hay_cart(name="HayCart", seed=3):
	"""A wooden cart full of hay (the leap's landing)."""
	import random

	rng = random.Random(seed)
	B = Builder(seed)
	Bc = Collider(seed)
	# The bed and its sides.
	B.box((0, 0, 0.75), (2.6, 1.5, 0.12), "wood", bevel=0.02)
	for sy in (-1, 1):
		B.box((0, sy * 0.72, 0.98), (2.6, 0.06, 0.38), "wood", bevel=0.01)
	for sx in (-1, 1):
		B.box((sx * 1.27, 0, 0.98), (0.06, 1.5, 0.38), "wood", bevel=0.01)
	for sx in (-1.25, -0.6, 0.0, 0.6, 1.25):
		for sy in (-1, 1):
			B.box((sx, sy * 0.76, 1.0), (0.08, 0.06, 0.5), "wood_dark", bevel=0.01)
	# Wheels with spokes.
	for sx in (-0.75, 0.75):
		for sy in (-1, 1):
			c = Vector((sx, sy * 0.86, 0.52))
			B.cylinder(c - Vector((0, 0.05, 0)), c + Vector((0, 0.05, 0)), 0.52, "wood_dark", segments=20, smooth=False)
			for k in range(8):
				a = math.pi * k / 8
				B.box(tuple(c + Vector((0, sy * 0.06, 0))), (0.05, 0.05, 0.96), "wood", bevel=0.0, rot=__import__("mathutils").Matrix.Rotation(a, 4, "Y"))
			B.cylinder(c - Vector((0, 0.09, 0)) * sy, c + Vector((0, 0.11, 0)) * sy, 0.1, "iron", segments=10)
	B.box((0, 0, 0.5), (0.12, 1.9, 0.1), "wood_dark", bevel=0.01)
	# The shafts.
	for sy in (-0.45, 0.45):
		B.box((-2.1, sy, 0.62), (1.7, 0.08, 0.08), "wood", bevel=0.01, rot=__import__("mathutils").Matrix.Rotation(math.radians(8), 4, "Y"))
	# The hay: a mound, bumpy, overflowing the sides.
	segs = 24
	rings = 10
	profile = []
	for k in range(rings + 1):
		t = k / rings
		profile.append((1.0 - t * t, 0.9 + 1.25 * math.sin(t * math.pi * 0.5)))
	verts = []
	bm = B.bm
	ring_verts = []
	for k, (r, z) in enumerate(profile):
		ring = []
		for i in range(segs):
			a = 2 * math.pi * i / segs
			n = 1.0 + 0.08 * math.sin(a * 5 + k * 1.7) + 0.05 * math.sin(a * 11 + k * 3.1) + rng.uniform(-0.03, 0.03)
			x = math.cos(a) * r * 1.5 * n
			y = math.sin(a) * r * 0.95 * n
			ring.append(bm.verts.new((x, y, z + rng.uniform(-0.04, 0.04) * (1 - k / rings))))
		ring_verts.append(ring)
	from ac_common import mat

	for k in range(rings):
		for i in range(segs):
			j = (i + 1) % segs
			f = bm.faces.new((ring_verts[k][i], ring_verts[k][j], ring_verts[k + 1][j], ring_verts[k + 1][i]))
			f.material_index = mat("hay")
			f.smooth = True
	Bc.box((0, 0, 0.9), (2.6, 1.5, 1.0), "hay")
	obj = B.to_object(name)
	col = Bc.to_object(name + "_col")
	return obj, col


def fountain(name="Fountain"):
	"""An octagonal basin, a pedestal and a bowl."""
	B = Builder(11)
	Bc = Collider(11)
	r_out, r_in = 3.0, 2.65
	pts_out = [(r_out * math.cos(math.pi / 8 + k * math.pi / 4), r_out * math.sin(math.pi / 8 + k * math.pi / 4)) for k in range(8)]
	for k in range(8):
		a, b = pts_out[k], pts_out[(k + 1) % 8]
		c = ((a[0] + b[0]) * 0.5 * 0.94, (a[1] + b[1]) * 0.5 * 0.94, 0.33)
		length = math.dist(a, b)
		B.box(c, (length + 0.1, 0.38, 0.66), "water_stone", bevel=0.03, rot_z=math.atan2(b[1] - a[1], b[0] - a[0]))
		B.box((c[0], c[1], 0.7), (length + 0.2, 0.5, 0.1), "stone", bevel=0.025, rot_z=math.atan2(b[1] - a[1], b[0] - a[0]))
	B.prism([(r_in * math.cos(k * math.pi / 4 + math.pi / 8), r_in * math.sin(k * math.pi / 4 + math.pi / 8)) for k in range(8)], 0.12, "water_stone", z0=0.0)
	B.lathe([(0.0, 0.0), (0.7, 0.0), (0.7, 0.3), (0.45, 0.4), (0.38, 1.2), (0.5, 1.35), (0.5, 1.45), (1.4, 1.65), (1.45, 1.8), (1.2, 1.85), (0.3, 1.8), (0.22, 2.4), (0.32, 2.6), (0.0, 2.7)], (0, 0, 0), "water_stone", segments=32)
	Bc.prism([(r_out * math.cos(math.pi / 8 + k * math.pi / 4), r_out * math.sin(math.pi / 8 + k * math.pi / 4)) for k in range(8)], 0.75, "stone")
	return B.to_object(name), Bc.to_object(name + "_col")


def stall(name, seed=5):
	"""A market stall: four posts, a counter, crates of produce; the awning is the scene's cloth."""
	import random

	rng = random.Random(seed)
	B = Builder(seed)
	Bc = Collider(seed)
	for x in (-1.4, 1.4):
		for y in (-0.8, 0.8):
			B.box((x, y, 1.15 if y < 0 else 1.35), (0.1, 0.1, 2.3 if y < 0 else 2.7), "wood", bevel=0.015)
	B.box((0, -0.55, 0.9), (2.9, 0.6, 0.08), "wood", bevel=0.015)
	B.box((0, -0.55, 0.45), (2.8, 0.55, 0.9), "wood_dark", bevel=0.015)
	for k in range(4):
		x = -1.05 + k * 0.7
		B.box((x, -0.55, 1.05), (0.6, 0.45, 0.22), "wood", bevel=0.01)
		for j in range(6):
			c = (x + rng.uniform(-0.2, 0.2), -0.55 + rng.uniform(-0.15, 0.15), 1.2 + rng.uniform(0, 0.06))
			B.lathe([(0.0, -0.07), (0.07, -0.05), (0.08, 0.0), (0.06, 0.05), (0.0, 0.07)], c, rng.choice(["roof", "hay", "shutter_green"]), segments=8)
	for x in (-1.0, 0.8):
		B.lathe([(0.0, 0.0), (0.26, 0.0), (0.31, 0.4), (0.26, 0.8), (0.0, 0.8)], (x, 0.6, 0.0), "wood", segments=14)
	Bc.box((0, -0.55, 0.47), (2.9, 0.6, 0.94), "wood")
	return B.to_object(name), Bc.to_object(name + "_col")


def skyline(name="Skyline", seed=21):
	"""The city beyond the square: towers here and there, a dome on its drum with its lantern, the city's walls, a
	palace's tower (milestone 153: its houses are instances of skyline_houses()'s, no longer boxes here)."""
	B = Builder(seed)

	# Bell towers of other churches.
	for tx, ty, th in ((-120.0, -40.0, 34.0), (60.0, 140.0, 30.0), (150.0, -70.0, 28.0)):
		B.box((tx, ty, th * 0.5 - 1.0), (5.0, 5.0, th + 2.0), "brick")
		B.box((tx, ty, th + 0.3), (5.8, 5.8, 0.6), "stone")
		B.prism([(tx - 2.9, ty - 2.9), (tx + 2.9, ty - 2.9), (tx + 2.9, ty + 2.9), (tx - 2.9, ty + 2.9)], 0.2, "stone", z0=th + 0.6)
		for k in range(4):
			a = k * math.pi / 2
			B.poly([(tx + 2.9 * math.cos(a) - 2.9 * math.sin(a), ty + 2.9 * math.sin(a) + 2.9 * math.cos(a), th + 0.8),
					(tx + 2.9 * math.cos(a + math.pi / 2) - 2.9 * math.sin(a + math.pi / 2), ty + 2.9 * math.sin(a + math.pi / 2) + 2.9 * math.cos(a + math.pi / 2), th + 0.8),
					(tx, ty, th + 6.0)], "roof", uv_slope=True)
	# The dome: an octagonal drum, a pointed ribbed dome, a lantern.
	cx, cy = -70.0, 95.0
	B.prism([(cx + 16 * math.cos(k * math.pi / 4 + math.pi / 8), cy + 16 * math.sin(k * math.pi / 4 + math.pi / 8)) for k in range(8)], 34.0, "plaster_white", z0=-1.0)
	B.prism([(cx + 30 * math.cos(k * math.pi / 4), cy + 14 * math.sin(k * math.pi / 4)) for k in range(8)], 22.0, "plaster_white", z0=-1.0)
	prof = []
	for k in range(13):
		t = k / 12
		ang = t * math.pi * 0.5
		prof.append((15.5 * (1 - math.sin(ang)) ** 0.85 + 2.0 * t, 33.0 + 21.0 * math.sin(ang * 1.05)))
	B.lathe(prof, (cx, cy, 0), "roof", segments=8, smooth=False)
	for k in range(8):
		a = k * math.pi / 4 + math.pi / 8
		pts = [(cx + (p[0] + 0.35) * math.cos(a), cy + (p[0] + 0.35) * math.sin(a), p[1]) for p in prof]
		for p0, p1 in zip(pts[:-1], pts[1:]):
			B.cylinder(p0, p1, 0.35, "plaster_white", segments=5)
	B.lathe([(2.6, 54.0), (2.6, 58.0), (1.0, 58.5), (0.6, 61.0), (0.0, 62.5)], (cx, cy, 0), "plaster_white", segments=16)
	# The city's walls round it (213 m out): brick curtain walls 11 m high with their walkway's battlements, a square
	# tower at each turn, a gate with its tower on the south road.
	n = 24
	wr = 213.0
	pts = [(wr * math.cos(2 * math.pi * k / n + 0.13), wr * math.sin(2 * math.pi * k / n + 0.13)) for k in range(n)]
	for k in range(n):
		a, b = Vector((pts[k][0], pts[k][1], 0)), Vector((pts[(k + 1) % n][0], pts[(k + 1) % n][1], 0))
		mid = (a + b) * 0.5
		length = (b - a).length
		rz = math.atan2(b.y - a.y, b.x - a.x)
		B.box((mid.x, mid.y, 5.0), (length, 2.6, 12.0), "brick", rot_z=rz)
		out = Vector((math.cos(rz - math.pi / 2), math.sin(rz - math.pi / 2), 0))
		if out.dot(mid) < 0:
			out = -out
		tdir = (b - a).normalized()
		m = 1
		while m * 2.4 < length - 4.0:
			c = a + tdir * (m * 2.4 + 2.0) + out * 1.05
			B.box((c.x, c.y, 11.55), (1.3, 0.5, 1.1), "brick", rot_z=rz)
			m += 1
		B.box((mid.x + out.x * 1.1, mid.y + out.y * 1.1, 10.8), (length, 0.7, 0.4), "stone", rot_z=rz)
	for k in range(n):
		x, y = pts[k]
		rz = 2 * math.pi * k / n + 0.13
		big = k == 18
		th = 22.0 if big else 15.5
		ts = 11.0 if big else 7.5
		B.box((x, y, th * 0.5 - 1.0), (ts, ts, th + 2.0), "stone_rustic" if big else "brick", rot_z=rz)
		B.box((x, y, th + 0.2), (ts + 0.8, ts + 0.8, 0.5), "stone", rot_z=rz)
		for i in range(-2, 3):
			for sx, sy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
				px, py = (sx * (ts / 2 + 0.2), i * ts / 5.2) if sx else (i * ts / 5.2, sy * (ts / 2 + 0.2))
				cx = x + px * math.cos(rz) - py * math.sin(rz)
				cy = y + px * math.sin(rz) + py * math.cos(rz)
				B.box((cx, cy, th + 1.0), (0.8, 0.8, 1.1), "brick", rot_z=rz)
		if big:
			# The gate's arch (dark), its roof.
			gx, gy = x * 1.03, y * 1.03
			B.box((gx, gy, 3.5), (4.0, 0.4, 7.0), "wood_dark", rot_z=rz + math.pi / 2)
	# A palace's tower, far: stone, its battlemented gallery standing out on corbels, a belfry on it.
	tx, ty = 110.0, 60.0
	B.box((tx, ty, 23.0), (7.0, 7.0, 48.0), "stone_rustic")
	B.box((tx, ty, 47.6), (8.6, 8.6, 1.2), "stone")
	B.box((tx, ty, 50.0), (8.4, 8.4, 3.6), "stone")
	for k in range(-3, 4):
		for sx, sy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
			px = tx + (sx * 4.3 if sx else k * 1.15)
			py = ty + (sy * 4.3 if sy else k * 1.15)
			B.box((px, py, 52.3), (0.6, 0.6, 1.0), "stone")
	B.box((tx, ty, 56.0), (3.6, 3.6, 7.0), "stone")
	B.prism([(tx - 2.2, ty - 2.2), (tx + 2.2, ty - 2.2), (tx + 2.2, ty + 2.2), (tx - 2.2, ty + 2.2)], 0.3, "stone", z0=59.5)
	B.lathe([(2.0, 59.8), (0.0, 64.5)], (tx, ty, 0), "roof", segments=4, smooth=False)
	return B.to_object(name)



# ------------------------------------------------------------------ the city's houses (milestone 153)

FAR_WIDTHS = (6.0, 7.2, 8.4, 9.6, 10.8)
FAR_FLOORS = (3, 4, 5)
FAR_DEPTH = 10.0


def far_house(name, w, floors, seed, roof="hip", detail=True, d=FAR_DEPTH):
	"""A house of the city beyond the square, made once and placed many times (instances, each with its plaster's
	colour): its front (-Y) and back with windows in openings (reveals, glass, sills; shutters open or closed; nearer
	ones with stone frames and hoods), string courses, a rusticated or plastered ground floor with its doors and shop
	arches, a top floor's loggia on some; the side walls with fewer windows; a moulded cornice, deep eaves on rafters,
	a hip or gable roof with its ridges' tiles and a roll of tiles along the eaves, or a terrace behind a parapet;
	chimneys. Centred on the origin. detail False: the far ones (shutters drawn flat, no frames, rafters or railings)."""
	import random

	rng = random.Random(seed)
	B = Builder(seed)
	gh = rng.choice([3.9, 4.2, 4.5])
	fh = rng.choice([3.1, 3.3, 3.5])
	zt = gh + fh * (floors - 1)
	hw, hd = w * 0.5, d * 0.5
	facades = rect_facades(w, d)
	front, right, back, left = facades
	rustic = rng.random() < 0.4
	shut = rng.choice(["shutter_green", "shutter_green", "shutter_brown", "shutter_brown", None])
	frames = detail and rng.random() < 0.6
	hoods = frames and rng.random() < 0.5
	courses = rng.random() < 0.75
	loggia = roof != "terrace" and floors >= 4 and rng.random() < 0.3
	balcony = detail and rng.random() < 0.35
	n = max(2, int(round(w / rng.uniform(2.3, 2.9))))
	ww = rng.uniform(0.9, 1.1)
	wh = rng.uniform(1.6, 1.85)
	reveal = "stone" if frames else "far_plaster"
	ground_wall = "stone_rustic" if rustic else "far_plaster"

	def shutters(F, x0, x1, z, h, state):
		if not shut or state == "none":
			return
		t = 0.035
		sw = (x1 - x0) * 0.5
		if not detail:
			# Drawn flat on the wall (a quad each, 1 cm out) or in the opening.
			if state == "open":
				for a, b in ((x0 - sw - 0.04, x0 - 0.04), (x1 + 0.04, x1 + sw + 0.04)):
					B.poly([F.p(a, z, -0.012), F.p(b, z, -0.012), F.p(b, z + h, -0.012), F.p(a, z + h, -0.012)], shut)
			else:
				B.poly([F.p(x0, z, 0.06), F.p(x1, z, 0.06), F.p(x1, z + h, 0.06), F.p(x0, z + h, 0.06)], shut)
			return
		if state == "open":
			fbox(B, F, x0 - sw - 0.05, x0 - 0.05, z + 0.02, z + h, -0.06 - t, -0.06, shut, bevel=0.0)
			fbox(B, F, x1 + 0.05, x1 + sw + 0.05, z + 0.02, z + h, -0.06 - t, -0.06, shut, bevel=0.0)
		elif state == "half":
			fbox(B, F, x0 - sw - 0.05, x0 - 0.05, z + 0.02, z + h, -0.06 - t, -0.06, shut, bevel=0.0)
			fbox(B, F, (x0 + x1) * 0.5, x1 - 0.01, z + 0.02, z + h - 0.02, 0.05, 0.05 + t, shut, bevel=0.0)
		else:
			fbox(B, F, x0 + 0.01, x1 - 0.01, z + 0.02, z + h - 0.02, 0.05, 0.05 + t, shut, bevel=0.0)

	def windows(F, count, floors_from, with_shutters=True, side=False):
		"""The openings of the upper floors on F (count a floor), their sills, frames, hoods and shutters."""
		ops = []
		for f in range(floors_from, floors):
			z = gh + (f - 1) * fh + (0.85 if f > 1 else 0.75)
			h = wh + (0.3 if f == 1 and not side else 0.0)
			top_loggia = loggia and f == floors - 1 and not side and F is front
			if top_loggia:
				# The top floor's loggia: an arcade of wide openings on piers, dark within.
				k = count + 1
				pier = 0.42
				span = (F.length - pier * (k + 1)) / k
				for i in range(k):
					x0 = pier + i * (span + pier)
					o = opening(x0, x0 + span, gh + (f - 1) * fh + 0.35, gh + f * fh - 0.45, arch=True, back="wood_dark", depth=1.4)
					ops.append(o)
					if detail:
						fbox(B, F, x0 - 0.04, x0 + span + 0.04, o["z0"] - 0.05, o["z0"] + 0.9, 0.0, 0.08, "stone", bevel=0.0)
				continue
			for i in range(count):
				u = F.length * (i + 0.5) / count
				x0, x1 = u - ww * 0.5, u + ww * 0.5
				ops.append(opening(x0, x1, z, z + h, back="glass", depth=0.2))
				fbox(B, F, x0 - 0.1, x1 + 0.1, z - 0.09, z, -0.11, 0.02, "stone", bevel=0.0)
				if frames:
					fbox(B, F, x0 - 0.1, x0, z, z + h + 0.1, -0.03, 0.02, "stone", bevel=0.0)
					fbox(B, F, x1, x1 + 0.1, z, z + h + 0.1, -0.03, 0.02, "stone", bevel=0.0)
					fbox(B, F, x0, x1, z + h, z + h + 0.1, -0.03, 0.02, "stone", bevel=0.0)
					if hoods:
						fbox(B, F, x0 - 0.16, x1 + 0.16, z + h + 0.1, z + h + 0.22, -0.13, 0.02, "stone", bevel=0.0)
				if with_shutters:
					r = rng.random()
					shutters(F, x0, x1, z, h, "open" if r < 0.55 else ("closed" if r < 0.8 else ("half" if r < 0.9 else "none")))
				if balcony and f == 1 and i == count // 2 and F is front:
					# A balcony on brackets, its iron railing.
					fbox(B, F, x0 - 0.35, x1 + 0.35, z - 0.14, z, -0.75, 0.02, "stone", bevel=0.0)
					for bu in (x0 - 0.2, x1 + 0.2):
						fbox(B, F, bu - 0.06, bu + 0.06, z - 0.45, z - 0.14, -0.6, 0.02, "stone", bevel=0.0)
					fbox(B, F, x0 - 0.33, x1 + 0.33, z + 0.9, z + 0.94, -0.74, -0.7, "iron", bevel=0.0)
					for k2 in range(int((x1 - x0 + 0.66) / 0.14) + 1):
						bu = x0 - 0.33 + k2 * 0.14
						fbox(B, F, bu - 0.012, bu + 0.012, z, z + 0.9, -0.73, -0.71, "iron", bevel=0.0)
					for sx in (x0 - 0.33, x1 + 0.31):
						fbox(B, F, sx, sx + 0.02, z + 0.9, z + 0.94, -0.72, 0.0, "iron", bevel=0.0)
		return ops

	# Its footing, under the street (the ground is not level everywhere).
	B.box((0.0, 0.0, -0.8), (w, d, 1.6), ground_wall)
	# The front: the ground floor's doors and shops, the floors' windows.
	g = []
	kind = rng.choice(["shops", "shops", "doors", "door"])
	if kind == "shops":
		k = max(1, int(w / 3.2))
		for i in range(k):
			u = w * (i + 0.5) / k
			sw = min(2.3, w / k - 0.9)
			g.append(opening(u - sw * 0.5, u + sw * 0.5, 0.0, gh - 0.75, arch=rng.random() < 0.7, back="wood_dark", depth=0.35))
	elif kind == "doors":
		k = max(2, int(w / 2.6))
		for i in range(k):
			u = w * (i + 0.5) / k
			arch = i == k // 2
			g.append(opening(u - 0.65, u + 0.65, 0.0, 2.7 if not arch else 3.1, arch=arch, back="wood_dark" if arch else "glass", depth=0.3))
	else:
		g.append(opening(w * 0.5 - 0.8, w * 0.5 + 0.8, 0.0, 3.2, arch=True, back="wood_dark", depth=0.35))
		for u in (w * 0.18, w * 0.82):
			g.append(opening(u - 0.45, u + 0.45, 1.6, 2.9, back="glass", depth=0.2))
	wall(B, front, 0.0, gh, g, ground_wall, reveal="stone")
	if detail:
		for o in g:
			if o["z0"] > 0.5:
				fbox(B, front, o["x0"] - 0.08, o["x1"] + 0.08, o["z0"] - 0.08, o["z0"], -0.09, 0.02, "stone", bevel=0.0)
				# Iron bars on the ground floor's windows.
				for b in range(4):
					bu = o["x0"] + (o["x1"] - o["x0"]) * (b + 0.5) / 4
					fbox(B, front, bu - 0.012, bu + 0.012, o["z0"], o["z1"], -0.02, 0.0, "iron", bevel=0.0)
	if not rustic:
		fbox(B, front, -0.02, w + 0.02, 0.0, 0.55, -0.05, 0.02, "stone", bevel=0.0)
	wall(B, front, gh, zt, windows(front, n, 1), "far_plaster", reveal=reveal)
	# The back: fewer windows, no shutters on the far ones.
	bg = [opening(w * 0.5 - 0.6, w * 0.5 + 0.6, 0.0, 2.5, back="wood_dark", depth=0.25)]
	wall(B, back, 0.0, gh, bg, ground_wall, reveal="stone")
	wall(B, back, gh, zt, windows(back, max(2, n - 1), 1, with_shutters=detail), "far_plaster", reveal=reveal)
	# The sides: a column of windows (seen above a lower neighbour, or at a block's end).
	for F in (right, left):
		ns = 2 if detail else 1
		wall(B, F, 0.0, gh, [], ground_wall)
		wall(B, F, gh, zt, windows(F, ns, 1, with_shutters=detail, side=True), "far_plaster", reveal=reveal)
	if courses:
		for f in range(1, floors):
			z = gh + (f - 1) * fh
			for F in (front, back):
				fbox(B, F, -0.02, F.length + 0.02, z - 0.12, z + 0.04, -0.1, 0.02, "stone", bevel=0.0)
			if detail:
				for F in (right, left):
					fbox(B, F, 0.0, F.length, z - 0.12, z + 0.04, -0.1, 0.02, "stone", bevel=0.0)
	if rustic:
		for F in (front, back):
			fbox(B, F, -0.02, F.length + 0.02, gh - 0.16, gh, -0.12, 0.02, "stone", bevel=0.0)

	# The top: the cornice, the eaves and the roof, or a terrace.
	if roof == "terrace":
		ph = 0.95
		for F in facades:
			fbox(B, F, -0.02, F.length + 0.02, zt - 0.26, zt - 0.12, -0.12, 0.02, "stone", bevel=0.0)
			fbox(B, F, -0.02, F.length + 0.02, zt - 0.12, zt, -0.2, 0.02, "stone", bevel=0.0)
			wall(B, F, zt, zt + ph, [], "far_plaster")
			fbox(B, F, -0.06, F.length + 0.06, zt + ph, zt + ph + 0.1, -0.06, 0.3, "stone", bevel=0.0)
		B.poly([(-hw + 0.25, -hd + 0.25, zt + 0.15), (hw - 0.25, -hd + 0.25, zt + 0.15), (hw - 0.25, hd - 0.25, zt + 0.15), (-hw + 0.25, hd - 0.25, zt + 0.15)], "ground_stone")
		for F in facades:
			inner = Facade(F.p(F.length - 0.25, 0, 0.25), -F.r, -F.n, F.length - 0.5)
			wall(B, inner, zt + 0.15, zt + ph, [], "far_plaster")
		if rng.random() < 0.6:
			# An altana: posts, a low tiled roof.
			ax0, ax1 = -hw + 0.6, -hw + 0.6 + min(4.0, w * 0.5)
			ay0, ay1 = -hd + 1.2, hd - 1.0
			zh = zt + 2.5
			for x in (ax0, ax1):
				for y in (ay0, ay1):
					B.box((x, y, (zt + zh) * 0.5), (0.18, 0.18, zh - zt), "wood_dark")
			ex2, ey2 = (ax1 - ax0) * 0.5 + 0.4, (ay1 - ay0) * 0.5 + 0.4
			cx = (ax0 + ax1) * 0.5
			z0 = zh
			zr = z0 + min(ex2, ey2) * 0.38
			c = [(cx - ex2, -ey2, z0), (cx + ex2, -ey2, z0), (cx + ex2, ey2, z0), (cx - ex2, ey2, z0)]
			if ex2 >= ey2:
				r0, r1 = (cx - (ex2 - ey2), 0, zr), (cx + (ex2 - ey2), 0, zr)
				faces = [[c[0], c[1], r1, r0], [c[1], c[2], r1], [c[2], c[3], r0, r1], [c[3], c[0], r0]]
			else:
				r0, r1 = (cx, -(ey2 - ex2), zr), (cx, ey2 - ex2, zr)
				faces = [[c[0], c[1], r0], [c[1], c[2], r1, r0], [c[2], c[3], r1], [c[3], c[0], r0, r1]]
			for f in faces:
				B.poly(f, "roof", uv_slope=True)
			B.poly([c[0], c[3], c[2], c[1]], "wood")
		return B.to_object(name), zt + ph
	for F in (front, back):
		fbox(B, F, -0.02, F.length + 0.02, zt - 0.34, zt - 0.2, -0.1, 0.02, "stone", bevel=0.0)
		fbox(B, F, -0.02, F.length + 0.02, zt - 0.2, zt - 0.06, -0.22, 0.02, "stone", bevel=0.0)
	ov, ovs = rng.uniform(0.7, 0.95), 0.12
	ex, ey = hw + ovs, hd + ov
	pitch = math.radians(rng.uniform(18.0, 24.0))
	z0 = zt + 0.08
	# The soffit's planks and the fascia, front and back.
	for sy in (-1, 1):
		y0, y1 = sy * hd, sy * ey
		pts = [(-ex, y1, zt), (ex, y1, zt), (ex, y0, zt), (-ex, y0, zt)]
		B.poly(list(reversed(pts)) if sy < 0 else pts, "wood")
		fa = [(-ex, y1, zt), (ex, y1, zt), (ex, y1, z0), (-ex, y1, z0)]
		B.poly(fa if sy < 0 else list(reversed(fa)), "wood_dark")
		if detail:
			k = int(w / 0.75)
			for i in range(1, k):
				x = -hw + w * i / k
				B.box((x, sy * (hd + (ov - 0.05) * 0.5), zt - 0.07), (0.1, ov - 0.05, 0.14), "wood_dark")
	if roof == "hip":
		zr = z0 + min(ex, ey) * math.tan(pitch)
		if ex >= ey:
			r0, r1 = (-(ex - ey), 0.0, zr), (ex - ey, 0.0, zr)
			faces = [[(-ex, -ey, z0), (ex, -ey, z0), r1, r0], [(ex, -ey, z0), (ex, ey, z0), r1], [(ex, ey, z0), (-ex, ey, z0), r0, r1], [(-ex, ey, z0), (-ex, -ey, z0), r0]]
			hips = [((-ex, -ey, z0), r0), ((-ex, ey, z0), r0), ((ex, -ey, z0), r1), ((ex, ey, z0), r1)]
		else:
			r0, r1 = (0.0, -(ey - ex), zr), (0.0, ey - ex, zr)
			faces = [[(-ex, -ey, z0), (ex, -ey, z0), r0], [(ex, -ey, z0), (ex, ey, z0), r1, r0], [(ex, ey, z0), (-ex, ey, z0), r1], [(-ex, ey, z0), (-ex, -ey, z0), r0, r1]]
			hips = [((-ex, -ey, z0), r0), ((ex, -ey, z0), r0), ((-ex, ey, z0), r1), ((ex, ey, z0), r1)]
		# The side eaves' soffits (narrow).
		for sx in (-1, 1):
			pts = [(sx * ex, -hd, zt), (sx * ex, hd, zt), (sx * hw, hd, zt), (sx * hw, -hd, zt)]
			B.poly(list(reversed(pts)) if sx > 0 else pts, "wood")
			fa = [(sx * ex, -ey, zt), (sx * ex, ey, zt), (sx * ex, ey, z0), (sx * ex, -ey, z0)]
			B.poly(fa if sx > 0 else list(reversed(fa)), "wood_dark")
		ridges = [(r0, r1)] + hips
	else:
		zr = z0 + ey * math.tan(pitch)
		r0, r1 = (-ex, 0.0, zr), (ex, 0.0, zr)
		faces = [[(-ex, -ey, z0), (ex, -ey, z0), r1, r0], [(ex, ey, z0), (-ex, ey, z0), r0, r1]]
		ridges = [(r0, r1)]
		zh = z0 + (ey - hd) * math.tan(pitch) - 0.02  # the roof's height over the front and back walls
		for sx in (-1, 1):
			x = sx * hw
			gable = [(x, -hd, zt), (x, hd, zt), (x, hd, zh), (x, 0.0, zr - 0.02), (x, -hd, zh)]
			B.poly(gable if sx > 0 else [(p[0], -p[1], p[2]) for p in gable], "far_plaster")
			# The verge: the roof's edge boards over the gable.
			for sy in (-1, 1):
				a = Vector((sx * ex, sy * ey, z0 - 0.06))
				b = Vector((sx * ex, 0.0, zr - 0.06))
				dv = b - a
				import mathutils

				B.box((a + b) * 0.5, (0.08, dv.length, 0.16), "wood_dark", rot=mathutils.Matrix.Rotation(math.atan2(dv.z, dv.y), 4, "X"))
	for f in faces:
		B.poly(f, "roof", uv_slope=True)
	segs = 5 if detail else 3
	for a, b in ridges:
		B.half_tube(Vector(a) + UP * 0.04, Vector(b) + UP * 0.04, 0.12, "roof", up=UP, segments=segs)
	# A roll of tiles along the front's and back's eaves.
	for sy in (-1, 1):
		a, b = Vector((-ex + 0.05, sy * (ey - 0.05), z0 + 0.03)), Vector((ex - 0.05, sy * (ey - 0.05), z0 + 0.03))
		B.half_tube(a, b, 0.07, "roof", up=UP, segments=segs)
	# Chimneys.
	for c in range(rng.choice([1, 2, 2, 3]) if detail else 1):
		cx = rng.uniform(-0.35, 0.35) * w
		cy = rng.uniform(-0.25, 0.25) * d
		top = zr + rng.uniform(0.5, 1.3)
		B.box((cx, cy, (zt + top) * 0.5), (0.7, 0.55, top - zt), "far_plaster")
		B.box((cx, cy, top + 0.05), (0.88, 0.72, 0.1), "stone")
		if detail:
			for sx in (-1, 1):
				B.box((cx + sx * 0.28, cy, top + 0.28), (0.12, 0.55, 0.36), "far_plaster")
			B.box((cx, cy, top + 0.5), (0.95, 0.8, 0.1), "roof")
	return B.to_object(name), zr


def skyline_houses(seed=21):
	"""The city's houses: prototypes (FAR_WIDTHS x FAR_FLOORS x two of each, a near set and a far one) and their
	places in the blocks between the streets (the layout skyline() had), a row of houses back to back along each side
	of a block, each row filled to the block's length (the houses stretched a little to fit), each house its plaster's
	colour. Returns (objects, places): places a list of [prototype, x, y, turn (radians), stretch, colour (r, g, b),
	seed]."""
	import random

	rng = random.Random(seed)
	objects = []
	protos = []
	k = 0
	for w in FAR_WIDTHS:
		for fl in FAR_FLOORS:
			for v in range(2):
				roof = ("hip", "gable", "terrace")[(k * 7 + v * 2) % 3] if fl < 5 or v == 0 else "hip"
				for near in (True, False):
					name = ("HN%02d" if near else "HF%02d") % k
					obj, _top = far_house(name, w, fl, 1000 + k * 13, roof=roof, detail=near)
					objects.append(obj)
				protos.append((k, w, fl))
				k += 1
	# The plasters' colours (sRGB): ochres, roses, creams, siennas, a few pale ones.
	colours = [(0.8, 0.58, 0.36), (0.82, 0.55, 0.45), (0.86, 0.76, 0.58), (0.74, 0.47, 0.32), (0.84, 0.81, 0.74),
			(0.88, 0.72, 0.45), (0.78, 0.66, 0.52), (0.84, 0.6, 0.5), (0.7, 0.42, 0.3), (0.8, 0.7, 0.6), (0.86, 0.66, 0.4),
			(0.76, 0.6, 0.5)]

	def clear(x, y, margin):
		if -42.0 - margin < x < 40.0 + margin and abs(y) < 34.0 + margin:
			return False  # the square and its houses (open to the west)
		if math.hypot(x + 70.0, y - 95.0) < 34.0 + margin:
			return False  # the cathedral
		if math.hypot(x - 110.0, y - 60.0) < 10.0 + margin:
			return False
		return math.hypot(x, y) < 200.0

	places = []
	turn = 0.06
	cs, sn = math.cos(turn), math.sin(turn)
	bw, bd, street = 26.0, 22.0, 6.0
	for gi in range(-8, 9):
		for gj in range(-9, 10):
			cx0 = gi * (bw + street) + rng.uniform(-1.5, 1.5)
			cy0 = gj * (bd + street) + rng.uniform(-1.5, 1.5)
			bx, by = cx0 * cs - cy0 * sn, cx0 * sn + cy0 * cs
			if not clear(bx, by, 0.0):
				continue
			for row in (-1, 1):
				widths = []
				while sum(widths) < bw - 3.0:
					widths.append(rng.choice(FAR_WIDTHS))
				stretch = bw / sum(widths)
				u = -bw * 0.5
				for wd in widths:
					w = wd * stretch
					fl = rng.choices(FAR_FLOORS, weights=(3, 5, 3))[0]
					cand = [p for p in protos if p[1] == wd and p[2] == fl]
					pk = rng.choice(cand)[0]
					lx, ly = u + w * 0.5, row * (bd * 0.5 - FAR_DEPTH * 0.5)
					x, y = bx + lx * cs - ly * sn, by + lx * sn + ly * cs
					if clear(x, y, 4.0):
						c = rng.choice(colours)
						j = rng.uniform(0.94, 1.05)
						col = (min(1.0, c[0] * j), min(1.0, c[1] * j * rng.uniform(0.97, 1.03)), min(1.0, c[2] * j))
						places.append([pk, round(x, 3), round(y, 3), round(turn + (math.pi if row > 0 else 0.0), 5), round(stretch, 4),
								[round(v, 3) for v in col], round(rng.random(), 3)])
					u += w
	return objects, places


def bollard(name="Bollard"):
	"""A paracarro: a stone post, round, its top domed, worn."""
	B = Builder(31)
	Bc = Collider(31)
	B.lathe([(0.0, 0.0), (0.17, 0.0), (0.17, 0.06), (0.15, 0.08), (0.14, 0.45), (0.13, 0.52), (0.1, 0.6), (0.05, 0.64), (0.0, 0.65)], (0, 0, 0), "stone", segments=16)
	Bc.prism([(0.15 * math.cos(k * math.pi / 4), 0.15 * math.sin(k * math.pi / 4)) for k in range(8)], 0.62, "stone")
	return B.to_object(name), Bc.to_object(name + "_col")


def barrel(name="Barrel"):
	"""An oak barrel: its staves bulging, iron hoops."""
	B = Builder(32)
	Bc = Collider(32)
	prof = [(0.0, 0.0), (0.24, 0.0)] + [(0.24 + 0.05 * math.sin(math.pi * k / 8), 0.85 * k / 8) for k in range(9)] + [(0.0, 0.85)]
	B.lathe(prof, (0, 0, 0), "wood", segments=18)
	for z in (0.08, 0.26, 0.59, 0.77):
		r = 0.24 + 0.05 * math.sin(math.pi * z / 0.85) + 0.006
		B.lathe([(r, z - 0.025), (r, z + 0.025)], (0, 0, 0), "iron", segments=18, smooth=False)
	B.cylinder((0, 0, 0.84), (0, 0, 0.86), 0.235, "wood_dark", segments=18)
	Bc.prism([(0.28 * math.cos(k * math.pi / 4), 0.28 * math.sin(k * math.pi / 4)) for k in range(8)], 0.85, "wood")
	return B.to_object(name), Bc.to_object(name + "_col")


def crate(name="Crate"):
	"""A crate of slats on a frame."""
	B = Builder(33)
	Bc = Collider(33)
	s = 0.6
	for x in (-1, 1):
		for y in (-1, 1):
			B.box((x * (s / 2 - 0.03), y * (s / 2 - 0.03), s * 0.25), (0.06, 0.06, s * 0.5), "wood_dark", bevel=0.005)
	for k in range(3):
		z = 0.06 + k * 0.16
		for sy in (-1, 1):
			B.box((0, sy * (s / 2 - 0.01), z), (s, 0.02, 0.1), "wood", bevel=0.004)
			B.box((sy * (s / 2 - 0.01), 0, z), (0.02, s, 0.1), "wood", bevel=0.004)
	B.box((0, 0, 0.015), (s - 0.04, s - 0.04, 0.03), "wood", bevel=0.004)
	Bc.box((0, 0, 0.25), (s, s, 0.5), "wood")
	return B.to_object(name), Bc.to_object(name + "_col")


def drain(name="Drain"):
	"""An iron grate in the paving (its top flush with it), its stone frame."""
	B = Builder(34)
	B.box((0, 0, -0.01), (0.62, 0.62, 0.03), "stone", bevel=0.01)
	B.box((0, 0, -0.012), (0.42, 0.42, 0.03), "glass")
	for k in range(7):
		x = -0.18 + k * 0.06
		B.box((x, 0, 0.0), (0.025, 0.42, 0.012), "iron", bevel=0.0)
	B.box((0, 0.205, 0.0), (0.44, 0.02, 0.014), "iron", bevel=0.0)
	B.box((0, -0.205, 0.0), (0.44, 0.02, 0.014), "iron", bevel=0.0)
	return B.to_object(name)
