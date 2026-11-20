# FellowEngine showcase: shared helpers for the Blender scripts that build the rooftops scene's models.
# Run with Blender's Python (a Blender install, or the `bpy` module): see build_rooftops_models.py.

import math
import random

import bmesh
import bpy
from mathutils import Matrix, Vector

# The materials every model may use, by name; the Godot builder (tools/build_rooftops.gd) gives each its shader. The
# colours are what other tools (and the glTF's own preview) see.
MATERIALS = {
	"plaster_ochre": (0.80, 0.58, 0.32, 0.9),
	"plaster_rose": (0.82, 0.55, 0.45, 0.9),
	"plaster_cream": (0.86, 0.78, 0.62, 0.9),
	"plaster_sienna": (0.70, 0.40, 0.24, 0.9),
	"plaster_white": (0.85, 0.83, 0.78, 0.9),
	"stone": (0.72, 0.68, 0.60, 0.85),
	"stone_rustic": (0.62, 0.58, 0.50, 0.95),
	"brick": (0.55, 0.30, 0.20, 0.9),
	"roof": (0.66, 0.33, 0.20, 0.8),
	"wood": (0.38, 0.24, 0.14, 0.8),
	"wood_dark": (0.22, 0.14, 0.09, 0.8),
	"shutter_green": (0.20, 0.32, 0.22, 0.7),
	"shutter_brown": (0.36, 0.20, 0.12, 0.7),
	"glass": (0.05, 0.06, 0.07, 0.15),
	"iron": (0.08, 0.08, 0.08, 0.5),
	"bronze": (0.45, 0.30, 0.14, 0.35),
	"hay": (0.80, 0.66, 0.30, 1.0),
	"ground_stone": (0.55, 0.52, 0.47, 0.9),
	"cobble": (0.45, 0.42, 0.38, 0.9),
	"water_stone": (0.62, 0.60, 0.55, 0.85),
	"cloth": (0.6, 0.15, 0.12, 0.9),
	# Milestone 153: the city's houses beyond the square (instances), their plaster tinted per instance.
	"far_plaster": (0.82, 0.70, 0.55, 0.9),
	# Milestone 158: the coppi laid over the roofs (the roof's face under them its own material, the troughs between).
	"coppo": (0.68, 0.40, 0.28, 0.75),
	# Milestone 162: each tile its own firing — a darker, browner one and a brighter, oranger one.
	"coppo_b": (0.52, 0.30, 0.22, 0.78),
	"coppo_c": (0.76, 0.43, 0.25, 0.72),
	# and the gutters' weathered zinc
	"zinc": (0.42, 0.42, 0.40, 0.5),
}


def reset_scene():
	bpy.ops.wm.read_factory_settings(use_empty=True)
	for name, (r, g, b, rough) in MATERIALS.items():
		m = bpy.data.materials.new(name)
		m.use_nodes = True
		bsdf = m.node_tree.nodes.get("Principled BSDF")
		if bsdf:
			bsdf.inputs["Base Color"].default_value = (r, g, b, 1.0)
			bsdf.inputs["Roughness"].default_value = rough
			if name in ("iron", "bronze", "zinc"):
				bsdf.inputs["Metallic"].default_value = 1.0


MAT_INDEX = {name: i for i, name in enumerate(MATERIALS)}


def mat(name):
	return MAT_INDEX[name]


class Builder:
	"""Accumulates geometry in one bmesh: boxes (bevelled), prisms, cylinders, lathes; then UVs in metres and the mesh."""

	def __init__(self, seed=0):
		self.bm = bmesh.new()
		self.bm.faces.layers.int.new("uvmode")  # 1: a roof's face (UVs along its slope)
		self.rng = random.Random(seed)

	def _assign(self, faces, material, uv=None):
		for f in faces:
			f.material_index = mat(material)
			f.smooth = False
			if uv:
				f[self._uvtag()] = 1

	def _uvtag(self):
		return self.bm.faces.layers.int.get("uvmode")

	def box(self, center, size, material, bevel=0.0, rot_z=0.0, rot=None, segments=1):
		"""A box at center (x, y, z) of size (sx, sy, sz), turned rot_z about Z (or by the matrix rot), its edges bevelled."""
		before = set(self.bm.faces)
		m = Matrix.Translation(Vector(center))
		if rot is not None:
			m = m @ rot
		elif rot_z:
			m = m @ Matrix.Rotation(rot_z, 4, "Z")
		m = m @ Matrix.Diagonal((size[0], size[1], size[2], 1.0))
		res = bmesh.ops.create_cube(self.bm, size=1.0, matrix=m)
		verts = res["verts"]
		if bevel > 0.0:
			edges = list({e for v in verts for e in v.link_edges})
			bmesh.ops.bevel(self.bm, geom=edges, offset=bevel, offset_type="OFFSET", segments=segments, profile=0.5, affect="EDGES", clamp_overlap=True)
		new_faces = [f for f in self.bm.faces if f not in before]
		self._assign(new_faces, material)
		return new_faces

	def poly(self, points, material, uv_slope=False, smooth=False):
		"""A face through the points (a list of Vectors, counter-clockwise seen from its front)."""
		verts = [self.bm.verts.new(Vector(p)) for p in points]
		f = self.bm.faces.new(verts)
		f.material_index = mat(material)
		f.smooth = smooth
		if uv_slope:
			f[self._uvtag()] = 1
		return f

	def prism(self, points, height, material, z0=0.0, bevel=0.0):
		"""An extruded polygon (points in XY, counter-clockwise) from z0 up by height."""
		before = set(self.bm.faces)
		bottom = [self.bm.verts.new((p[0], p[1], z0)) for p in points]
		top = [self.bm.verts.new((p[0], p[1], z0 + height)) for p in points]
		n = len(points)
		self.bm.faces.new(list(reversed(bottom)))
		self.bm.faces.new(top)
		for i in range(n):
			j = (i + 1) % n
			self.bm.faces.new((bottom[i], bottom[j], top[j], top[i]))
		new_faces = [f for f in self.bm.faces if f not in before]
		if bevel > 0.0:
			edges = list({e for f in new_faces for e in f.edges})
			bmesh.ops.bevel(self.bm, geom=edges, offset=bevel, offset_type="OFFSET", segments=1, profile=0.5, affect="EDGES", clamp_overlap=True)
			new_faces = [f for f in self.bm.faces if f not in before]
		self._assign(new_faces, material)
		return new_faces

	def cylinder(self, p0, p1, radius, material, segments=10, cap=True, smooth=True, radius1=None):
		"""A cylinder (or a cone with radius1) from p0 to p1."""
		p0 = Vector(p0)
		p1 = Vector(p1)
		axis = p1 - p0
		length = axis.length
		if length < 1e-6:
			return []
		z = axis / length
		x = z.orthogonal().normalized()
		y = z.cross(x)
		r1 = radius if radius1 is None else radius1
		ring0 = []
		ring1 = []
		for i in range(segments):
			a = 2.0 * math.pi * i / segments
			d = x * math.cos(a) + y * math.sin(a)
			ring0.append(self.bm.verts.new(p0 + d * radius))
			ring1.append(self.bm.verts.new(p1 + d * r1))
		faces = []
		for i in range(segments):
			j = (i + 1) % segments
			f = self.bm.faces.new((ring0[i], ring0[j], ring1[j], ring1[i]))
			f.smooth = smooth
			faces.append(f)
		if cap:
			faces.append(self.bm.faces.new(list(reversed(ring0))))
			if r1 > 1e-5:
				faces.append(self.bm.faces.new(ring1))
		for f in faces:
			f.material_index = mat(material)
		return faces

	def half_tube(self, p0, p1, radius, material, up=Vector((0, 0, 1)), segments=6):
		"""Half a tube from p0 to p1, its open side facing -up (a roof's ridge or eave tile, a coppo)."""
		p0 = Vector(p0)
		p1 = Vector(p1)
		axis = (p1 - p0).normalized()
		side = axis.cross(up).normalized()
		u = side.cross(axis).normalized()
		r0 = []
		r1 = []
		for i in range(segments + 1):
			a = math.pi * i / segments
			d = side * math.cos(a) + u * math.sin(a)
			r0.append(self.bm.verts.new(p0 + d * radius))
			r1.append(self.bm.verts.new(p1 + d * radius))
		faces = []
		for i in range(segments):
			f = self.bm.faces.new((r0[i], r1[i], r1[i + 1], r0[i + 1]))
			f.smooth = True
			f.material_index = mat(material)
			faces.append(f)
		return faces

	def coppo(self, p0, p1, r0, r1, material, up=Vector((0, 0, 1)), segments=6, lip=0.014, concave=False, flat=1.0):
		"""A coppo (milestone 159, the owner's reference): half a tapered tube from p0 (radius r0) to p1 (r1), its lower
		end thick (a lip of the clay's thickness and a little of its inside, so that the course's step reads); convex
		(its open side facing -up: a cover tile) or concave (open side up: a pan tile in the channel between two)."""
		p0 = Vector(p0)
		p1 = Vector(p1)
		axis = (p1 - p0).normalized()
		side = axis.cross(up).normalized()
		u = side.cross(axis).normalized()
		if concave:
			u = -u
		def ring(c, r):
			# (milestone 162: `flat` < 1 a lower, wider arch — the reference's tiles)
			return [c + (side * math.cos(math.pi * i / segments) + u * math.sin(math.pi * i / segments) * flat) * r
					for i in range(segments + 1)]
		o0, o1 = ring(p0, r0), ring(p1, r1)
		i0 = ring(p0, r0 - lip)
		i1 = ring(p0 + axis * 0.06, r0 - lip - (r0 - r1) * 0.06 / max(1e-6, (p1 - p0).length))
		# Milestone 163: the tile's own UVs (the clay's shader, clay_tile.gdshader): u = 2 * its id + the place across
		# the arch (0..1), v along it (0 at the lower end, 1 at the upper; the end's lip below 0); the pans' ids from
		# 4096. Kept by to_object (uvmode 2).
		uvl = self.bm.loops.layers.uv.get("UVMap") or self.bm.loops.layers.uv.new("UVMap")
		self._tiles = getattr(self, "_tiles", 0) + 1
		tid = (self._tiles % 4096) + (4096 if concave else 0)
		faces = []
		def quad(a, b, c, d, uvs):
			vs = [self.bm.verts.new(v) for v in (a, b, c, d)]
			f = self.bm.faces.new(vs)
			f.smooth = True
			f.material_index = mat(material)
			f[self._uvtag()] = 2
			for loop, (s, t) in zip(f.loops, uvs):
				loop[uvl].uv = (2.0 * tid + s, t)
			faces.append(f)
		for i in range(segments):
			a, b = i / segments, (i + 1) / segments
			quad(o0[i], o1[i], o1[i + 1], o0[i + 1], [(a, 0.0), (a, 1.0), (b, 1.0), (b, 0.0)])
			if lip > 0.0:
				quad(i0[i + 1], o0[i + 1], o0[i], i0[i], [(b, -0.05), (b, 0.0), (a, 0.0), (a, -0.05)])
				quad(i1[i], i1[i + 1], i0[i + 1], i0[i], [(a, -0.1), (b, -0.1), (b, -0.05), (a, -0.05)])
		return faces

	def lathe(self, profile, center, material, segments=24, smooth=True):
		"""A surface of revolution about the vertical through center: profile a list of (radius, z) from the bottom."""
		c = Vector(center)
		rings = []
		for r, z in profile:
			ring = []
			for i in range(segments):
				a = 2.0 * math.pi * i / segments
				ring.append(self.bm.verts.new(c + Vector((math.cos(a) * r, math.sin(a) * r, z))))
			rings.append(ring)
		faces = []
		for k in range(len(rings) - 1):
			for i in range(segments):
				j = (i + 1) % segments
				f = self.bm.faces.new((rings[k][i], rings[k][j], rings[k + 1][j], rings[k + 1][i]))
				f.smooth = smooth
				f.material_index = mat(material)
				faces.append(f)
		return faces

	def to_object(self, name, collection=None):
		bm = self.bm
		bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
		bm.normal_update()
		uv = bm.loops.layers.uv.get("UVMap") or bm.loops.layers.uv.new("UVMap")
		tag = bm.faces.layers.int.get("uvmode")
		if tag is not None:
			# Milestone 163: a coppo's end (its lip) sharp against its arch: welded, their normals had been averaged
			# over the whole tile's length (one ring of vertices at each end), shading it from end to end.
			for e in bm.edges:
				fs = e.link_faces
				if len(fs) == 2 and fs[0][tag] == 2 and fs[1][tag] == 2 and fs[0].normal.angle(fs[1].normal, 0.0) > math.radians(50.0):
					e.smooth = False
		for f in bm.faces:
			n = f.normal
			if tag is not None and f[tag] == 2:
				continue  # a coppo's own UVs (Builder.coppo)
			if tag is not None and f[tag] == 1 and abs(n.z) < 0.999:
				# A roof's face: u along the eave, v up the slope (metres), so that the tiles' rows run along it.
				nh = Vector((n.x, n.y, 0.0)).normalized()
				up_slope = (Vector((0, 0, 1)) - n * n.z).normalized()
				t = up_slope.cross(n).normalized()
				for loop in f.loops:
					p = loop.vert.co
					loop[uv].uv = (p.dot(t), p.dot(up_slope))
				continue
			ax = max(range(3), key=lambda i: abs(n[i]))
			for loop in f.loops:
				p = loop.vert.co
				if ax == 0:
					loop[uv].uv = (p.y * (1 if n.x > 0 else -1), p.z)
				elif ax == 1:
					loop[uv].uv = (p.x * (-1 if n.y > 0 else 1), p.z)
				else:
					loop[uv].uv = (p.x, p.y)
		me = bpy.data.meshes.new(name)
		bm.to_mesh(me)
		bm.free()
		for m in MATERIALS:
			me.materials.append(bpy.data.materials[m])
		obj = bpy.data.objects.new(name, me)
		(collection or bpy.context.scene.collection).objects.link(obj)
		return obj


class Collider:
	"""Collision as convex pieces: each box or prism a piece, the faces given one group a piece (a roof's slopes: the hull
	of their points). Exported as objects <name>_<i>; the Godot builder makes a convex shape of each one's points
	(convex shapes: the character's capsule queries are quicker against them than against one trimesh)."""

	def __init__(self, seed=0):
		self.pieces = []
		self.groups = {}

	def _new(self):
		bm = bmesh.new()
		self.pieces.append(bm)
		return bm

	def box(self, center, size, material=None, bevel=0.0, rot_z=0.0, rot=None, segments=1):
		bm = self._new()
		m = Matrix.Translation(Vector(center))
		if rot is not None:
			m = m @ rot
		elif rot_z:
			m = m @ Matrix.Rotation(rot_z, 4, "Z")
		m = m @ Matrix.Diagonal((size[0], size[1], size[2], 1.0))
		bmesh.ops.create_cube(bm, size=1.0, matrix=m)

	def prism(self, points, height, material=None, z0=0.0, bevel=0.0):
		bm = self._new()
		bottom = [bm.verts.new((p[0], p[1], z0)) for p in points]
		top = [bm.verts.new((p[0], p[1], z0 + height)) for p in points]
		bm.faces.new(list(reversed(bottom)))
		bm.faces.new(top)
		n = len(points)
		for i in range(n):
			j = (i + 1) % n
			bm.faces.new((bottom[i], bottom[j], top[j], top[i]))

	def poly(self, points, material=None, group="roof", **_kw):
		bm = self.groups.get(group)
		if bm is None:
			bm = self.groups[group] = self._new()
		bm.faces.new([bm.verts.new(Vector(p)) for p in points])

	def to_object(self, name):
		objs = []
		for i, bm in enumerate(self.pieces):
			bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
			me = bpy.data.meshes.new("%s_%d" % (name, i))
			bm.to_mesh(me)
			bm.free()
			obj = bpy.data.objects.new("%s_%d" % (name, i), me)
			bpy.context.scene.collection.objects.link(obj)
			objs.append(obj)
		return objs


def empty(name, location, rot_z=0.0, parent=None):
	e = bpy.data.objects.new(name, None)
	e.location = location
	e.rotation_euler = (0, 0, rot_z)
	bpy.context.scene.collection.objects.link(e)
	if parent is not None:
		e.parent = parent
	return e


def export_glb(path, objects):
	bpy.ops.object.select_all(action="DESELECT")
	for o in objects:
		o.select_set(True)
	bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_apply=True, export_yup=True,
			export_texcoords=True, export_normals=True, export_materials="EXPORT", export_extras=True)
