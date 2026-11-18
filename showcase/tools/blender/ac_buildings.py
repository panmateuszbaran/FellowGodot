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


def window(B, F, u, z, w=1.0, h=1.8, kind="plain", shutters=None, shutters_closed=False, rng=None):
	"""A window centred at u, its sill at z: the opening (to be cut by wall()), its stone frame, its sill (a handhold),
	a cornice or a pediment over it on the piano nobile, its shutters. Returns the opening."""
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
	# The sill: wider than the frame, standing out (a hand's hold).
	fbox(B, F, x0 - f - 0.08, x1 + f + 0.08, z - 0.09, z, -0.15, 0.02, "stone", bevel=0.02)
	if kind == "nobile":
		# A cornice on brackets over the window.
		top = z + h + f
		fbox(B, F, x0 - f - 0.1, x1 + f + 0.1, top + 0.02, top + 0.16, -0.16, 0.02, "stone", bevel=0.02)
		for bu in (x0 - f * 0.5, x1 + f * 0.5):
			fbox(B, F, bu - 0.05, bu + 0.05, top - 0.16, top + 0.02, -0.12, 0.02, "stone", bevel=0.01)
	elif kind == "pediment":
		top = z + h + f
		fbox(B, F, x0 - f - 0.1, x1 + f + 0.1, top, top + 0.1, -0.14, 0.02, "stone", bevel=0.015)
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
	if shutters:
		t = 0.035
		if shutters_closed:
			fbox(B, F, x0 + 0.01, u - 0.005, z + 0.02, z + h - (0.02 if not arch else w * 0.5), 0.04, 0.04 + t, shutters, bevel=0.006)
			fbox(B, F, u + 0.005, x1 - 0.01, z + 0.02, z + h - (0.02 if not arch else w * 0.5), 0.04, 0.04 + t, shutters, bevel=0.006)
		else:
			sw = w * 0.5
			hh = h if not arch else h - w * 0.5
			fbox(B, F, x0 - f - sw - 0.02, x0 - f - 0.02, z + 0.02, z + hh, -0.05 - t, -0.05, shutters, bevel=0.006)
			fbox(B, F, x1 + f + 0.02, x1 + f + sw + 0.02, z + 0.02, z + hh, -0.05 - t, -0.05, shutters, bevel=0.006)
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


def eaves(B, Bc, facades, w, d, zt, overhang, pitch_deg, kind, rng, chimneys=1):
	"""The roof: a stone cornice at the wall's top, the eave on wooden rafters (a hip or a gable roof), the coppi along
	the eaves and the ridges; returns the ridge's height."""
	tanp = math.tan(math.radians(pitch_deg))
	# The cornice: a moulded band, then the soffit's planks on rafters.
	for i, F in enumerate(facades):
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
		Bc.poly(f, "roof")
	# The coppi: at the eaves, a row of half-tubes down the slope; along the ridges and the hips, a row of caps.
	for i in range(4):
		a, b = Vector((corners[i][0], corners[i][1], z0)), Vector((corners[(i + 1) % 4][0], corners[(i + 1) % 4][1], z0))
		if kind == "gable" and i % 2 == 1:
			continue
		along = b - a
		length = along.length
		t = along / length
		outward = t.cross(UP)
		inward = -outward
		slope = (inward + UP * tanp).normalized()
		n = int(length / 0.26)
		for k in range(1, n):
			p = a + t * (length * k / n)
			if kind == "hip" and (k * length / n < 0.5 or (n - k) * length / n < 0.5):
				continue
			base = p + UP * 0.03
			B.half_tube(base - outward * 0.02, base + slope * 0.5, 0.085, "roof", up=(UP - slope * slope.dot(UP)).normalized(), segments=5)
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
			for u0, u1 in gaps:
				fbox(B, R, u0, u1, 0.0, 0.38, -0.36, 0.02, "stone_rustic", bevel=0.02)
				fbox(B, R, u0 - 0.03, u1 + 0.03, 0.38, 0.47, -0.45, 0.02, "stone", bevel=0.02)
				fbox(Bc, R, u0, u1, 0.0, 0.47, -0.45, 0.02, "stone", bevel=0.0)
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
				opens.append(window(B, F, u, z + 0.95, 1.0, 1.85 if fl == 1 else 1.7, kind, sh, closed, rng))
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
		fbox(B, R, -0.05, R.length + 0.05, 3.85, 4.0, -0.08, 0.12, "stone", bevel=0.02)
	shaft_top = 26.0
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
	B.cylinder(c + Vector((0, 0.06, 0)), c - Vector((0, 0.06, 0)), 1.05, "stone", segments=32, smooth=False)
	B.cylinder(c - Vector((0, 0.06, 0)), c - Vector((0, 0.09, 0)), 0.95, "plaster_white", segments=32, smooth=False)
	for k in range(12):
		a = 2 * math.pi * k / 12
		p = c - Vector((0, 0.1, 0)) + Vector((math.sin(a), 0, math.cos(a))) * 0.82
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
	"""The city beyond the square: blocks of houses between streets (each house a box and a hip roof, its windows
	drawn by the far plaster's shader), towers here and there, a dome on its drum with its lantern, a palace's tower."""
	import random

	import mathutils

	rng = random.Random(seed)
	B = Builder(seed)
	plasters = ["plaster_ochre", "plaster_rose", "plaster_cream", "plaster_sienna", "plaster_white"]

	def house(x, y, w, d, h, rz):
		B.box((x, y, h * 0.5 - 1.0), (w, d, h + 2.0), rng.choice(plasters), rot_z=rz)
		m = mathutils.Matrix.Translation((x, y, h)) @ mathutils.Matrix.Rotation(rz, 4, "Z")
		ex, ey = w * 0.5 + 0.5, d * 0.5 + 0.5
		zr = min(ex, ey) * 0.4
		if ex >= ey:
			r0, r1 = (-(ex - ey), 0, zr), (ex - ey, 0, zr)
			faces = [[(-ex, -ey, 0), (ex, -ey, 0), r1, r0], [(ex, -ey, 0), (ex, ey, 0), r1], [(ex, ey, 0), (-ex, ey, 0), r0, r1], [(-ex, ey, 0), (-ex, -ey, 0), r0]]
		else:
			r0, r1 = (0, -(ey - ex), zr), (0, ey - ex, zr)
			faces = [[(-ex, -ey, 0), (ex, -ey, 0), r0], [(ex, -ey, 0), (ex, ey, 0), r1, r0], [(ex, ey, 0), (-ex, ey, 0), r1], [(-ex, ey, 0), (-ex, -ey, 0), r0, r1]]
		for f in faces:
			B.poly([m @ Vector(p) for p in f], "roof", uv_slope=True)

	def clear(x, y, margin):
		if -40.0 - margin < x < 30.5 + margin and abs(y) < 25.5 + margin:
			return False  # the square and its houses (open to the west)
		if math.hypot(x + 70.0, y - 95.0) < 34.0 + margin:
			return False  # the cathedral
		if math.hypot(x - 110.0, y - 60.0) < 10.0 + margin:
			return False
		return math.hypot(x, y) < 200.0

	# Blocks on a grid of streets, the grid turned a little; two rows of houses back to back along each block.
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
				u = -bw * 0.5
				while u < bw * 0.5 - 4.0:
					w = min(rng.uniform(6.0, 11.0), bw * 0.5 - u)
					d = bd * 0.5 - rng.uniform(0.0, 1.5)
					h = rng.uniform(9.0, 19.0)
					lx, ly = u + w * 0.5, row * d * 0.5
					x, y = bx + lx * cs - ly * sn, by + lx * sn + ly * cs
					if clear(x, y, 4.0):
						house(x, y, w - 0.05, d, h, turn)
					u += w
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
