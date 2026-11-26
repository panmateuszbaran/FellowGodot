extends Node3D
## Milestone 213 (the owner: "and maybe some open buildings with a real interior?"): CasaRosa's ground floor an osteria,
## open to the square through its two shop arches. Made at run time on this node (a child of the building: its frame,
## the front +Z):
##  * the arches opened: their shop windows' glass taken out of the building's mesh (a copy of it), the solid ground
##    floor's collider replaced by the room's walls;
##  * the room behind them (9.5 by 8.4 m, 4 m high): a floor of terracotta tiles, plastered walls on a stone skirting,
##    a plank ceiling on beams; a stone fireplace with a hood at the back, three logs burning in it (FellowGasVolume3D,
##    FellowBurnable3D: the fire lights the room and casts shadows); a counter, shelves of bottles and jugs behind it,
##    barrels on a rack; four tables with benches, jugs, cups, plates, bread and a candle each (a small flickering
##    light); an iron chandelier of candles; hams and garlic hanging from a beam;
##  * its people (rooftops_crowd.gd takes them: get_people()): the host behind the counter, guests at the tables, two
##    at the counter, one warming at the fire; and its shelter (get_shelter()): in the rain the square's people come in
##    through the arches and stand inside.
## The materials are the town's shaders (rooftops/shaders) on metric UVs, as the buildings'.

const ROOM_X := 4.75 # the walls' inner faces: x ±ROOM_X
const ROOM_BACK := -4.25 # z
const ROOM_FRONT := 4.15 # z: where the arches' reveals end (their glass was)
const ROOM_TOP := 4.0
const WALL_OUT := 5.1 # the building's ground floor: x ±5.1, z ±4.6, 0..4.4
const FRONT_OUT := 4.6
const BACK_OUT := -4.6
const FLOOR_TOP := 4.4
const LAYER_WORLD := 1
const FLOOR := 0.08 # the room's floor: the pavement's collider under the house (its top 0.08 m up)

var _mats := {}
var _arches: Array = [] # [x0, x1, top]
var _people: Array = [] # {kind: "seat" | "stand", xform (global), clip}
var _shelter: Array = [] # {out, in, spot (global Transform3D)}
var _candles: Array = [] # [OmniLight3D, base energy, phase]
var _t := 0.0
var _fire: Node3D
var _rn: Node3D # the room: everything built, FLOOR up


func _ready() -> void:
	add_to_group(&"osteria")
	_rn = Node3D.new()
	_rn.name = "Room"
	_rn.position = Vector3(0, FLOOR, 0)
	add_child(_rn)
	_materials()
	_open_front()
	_hollow()
	_room()
	_fireplace()
	_counter()
	_tables()
	_hanging()
	_people_and_shelter()


# ------------------------------------------------------------------ materials


func _shader(p_name: String, p_params: Dictionary) -> ShaderMaterial:
	var m := ShaderMaterial.new()
	m.shader = load("res://rooftops/shaders/%s.gdshader" % p_name)
	for k in p_params:
		m.set_shader_parameter(k, p_params[k])
	return m


func _std(p_color: Color, p_rough: float, p_metal := 0.0) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = p_color
	m.roughness = p_rough
	m.metallic = p_metal
	return m


func _materials() -> void:
	_mats.floor = _shader("masonry", {"color": Color(0.56, 0.3, 0.19), "joint_color": Color(0.42, 0.36, 0.3), "variation": 0.16,
			"hue_variation": 0.05, "block_size": Vector2(0.3, 0.3), "row_offset": 0.0, "joint_width": 0.004, "joint_depth": 0.004,
			"chamfer": 0.006, "face_rough": 0.0015, "slab_tilt": 0.004, "grime": 0.45, "ground_dirt": 0.0, "roughness": 0.78, "seed": 5.0})
	_mats.wall = _shader("plaster", {"color": Color(0.84, 0.76, 0.6), "variation": 0.18, "stains": 0.6, "fallen": 0.12,
			"brick_color": Color(0.52, 0.3, 0.2), "ground_dirt": 0.9, "seed": 7.0})
	_mats.stone = _shader("masonry", {"color": Color(0.6, 0.57, 0.51), "joint_color": Color(0.4, 0.38, 0.34), "block_size": Vector2(0.62, 0.3),
			"row_offset": 0.5, "grime": 0.35, "ground_dirt": 0.0, "seed": 2.0})
	_mats.soot = _shader("masonry", {"color": Color(0.16, 0.14, 0.13), "joint_color": Color(0.08, 0.07, 0.07), "block_size": Vector2(0.4, 0.22),
			"row_offset": 0.5, "grime": 0.8, "ground_dirt": 0.0, "seed": 4.0})
	_mats.beam = _shader("wood", {"color": Color(0.26, 0.16, 0.09), "bleach": 0.05, "seed": 1.0})
	_mats.planks = _shader("wood", {"color": Color(0.33, 0.21, 0.12), "bleach": 0.05, "board_width": 0.19, "seed": 3.0})
	_mats.table = _shader("wood", {"color": Color(0.42, 0.28, 0.16), "bleach": 0.1, "board_width": 0.2, "seed": 5.0})
	_mats.counter = _shader("wood", {"color": Color(0.3, 0.18, 0.1), "bleach": 0.05, "board_width": 0.16, "seed": 8.0})
	_mats.barrel = _shader("wood", {"color": Color(0.36, 0.22, 0.12), "bleach": 0.08, "board_width": 0.11, "seed": 9.0})
	_mats.iron = _std(Color(0.12, 0.11, 0.1), 0.55, 0.7)
	_mats.clay = _std(Color(0.62, 0.32, 0.18), 0.75)
	_mats.glaze = _std(Color(0.86, 0.82, 0.72), 0.35)
	_mats.bread = _std(Color(0.66, 0.44, 0.22), 0.85)
	_mats.ham = _std(Color(0.5, 0.24, 0.17), 0.7)
	_mats.garlic = _std(Color(0.88, 0.84, 0.76), 0.8)
	_mats.wax = _std(Color(0.93, 0.89, 0.78), 0.5)
	var flame := StandardMaterial3D.new()
	flame.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	flame.albedo_color = Color(1.0, 0.7, 0.32)
	flame.emission_enabled = true
	flame.emission = Color(1.0, 0.62, 0.25)
	flame.emission_energy_multiplier = 6.0
	_mats.flame = flame
	for c in [["bottle_green", Color(0.16, 0.32, 0.14, 0.82)], ["bottle_brown", Color(0.36, 0.2, 0.08, 0.85)], ["bottle_clear", Color(0.75, 0.8, 0.72, 0.5)]]:
		var g := StandardMaterial3D.new()
		g.albedo_color = c[1]
		g.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		g.roughness = 0.08
		g.metallic_specular = 0.7
		_mats[c[0]] = g
	_mats.wine = _std(Color(0.3, 0.04, 0.06), 0.15)


# ------------------------------------------------------------------ the building opened


## The front's shop arches (their glass on the ground, at ROOM_FRONT) out of a copy of the building's mesh.
func _open_front() -> void:
	var mi := get_parent().get_node_or_null(^"Mesh") as MeshInstance3D
	if mi == null or mi.mesh == null:
		return
	var src := mi.mesh as ArrayMesh
	var glass := -1
	for i in src.get_surface_count():
		var m := src.surface_get_material(i)
		if m and m.resource_path.ends_with("/glass.tres"):
			glass = i
	if glass < 0:
		return
	var arrays := src.surface_get_arrays(glass)
	var v: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	if idx.is_empty():
		return
	# The shops' glass: on the front at ROOM_FRONT, from the ground up (the triangles touching the ground give each
	# arch's span; every front triangle within a span is its glass).
	var spans: Array = [] # [x0, x1, top]
	var front := func(t: int) -> bool:
		for k in 3:
			if absf(v[idx[t * 3 + k]].z - ROOM_FRONT) > 0.03:
				return false
		return true
	for t in idx.size() / 3:
		if not front.call(t):
			continue
		var lo := minf(v[idx[t * 3]].y, minf(v[idx[t * 3 + 1]].y, v[idx[t * 3 + 2]].y))
		if lo > 0.05:
			continue
		var x0 := minf(v[idx[t * 3]].x, minf(v[idx[t * 3 + 1]].x, v[idx[t * 3 + 2]].x))
		var x1 := maxf(v[idx[t * 3]].x, maxf(v[idx[t * 3 + 1]].x, v[idx[t * 3 + 2]].x))
		var merged := false
		for sp in spans:
			if x1 > sp[0] - 0.05 and x0 < sp[1] + 0.05:
				sp[0] = minf(sp[0], x0)
				sp[1] = maxf(sp[1], x1)
				merged = true
		if not merged:
			spans.append([x0, x1, 0.0])
	if spans.is_empty():
		return
	var keep := PackedInt32Array()
	for t in idx.size() / 3:
		var cx := (v[idx[t * 3]].x + v[idx[t * 3 + 1]].x + v[idx[t * 3 + 2]].x) / 3.0
		var arch = null
		if front.call(t):
			for sp in spans:
				if cx > sp[0] and cx < sp[1]:
					arch = sp
		if arch != null:
			for k in 3:
				arch[2] = maxf(arch[2], v[idx[t * 3 + k]].y)
			continue
		keep.append_array(PackedInt32Array([idx[t * 3], idx[t * 3 + 1], idx[t * 3 + 2]]))
	_arches = spans
	_arches.sort_custom(func(p, q): return p[0] < q[0])
	var mesh := src.duplicate() as ArrayMesh
	var mat := mesh.surface_get_material(glass)
	mesh.surface_remove(glass)
	if keep.size() > 0:
		arrays[Mesh.ARRAY_INDEX] = keep
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		mesh.surface_set_material(mesh.get_surface_count() - 1, mat)
	mi.mesh = mesh


## The solid ground floor's collider off; the room's walls, the front round the arches and the ceiling instead.
func _hollow() -> void:
	for c in get_parent().get_children():
		var cs := c as CollisionShape3D
		if cs == null or cs.shape == null:
			continue
		var pts: PackedVector3Array = cs.shape.get("points") if cs.shape is ConvexPolygonShape3D else PackedVector3Array()
		if pts.is_empty():
			continue
		var box := AABB(pts[0], Vector3())
		for p in pts:
			box = box.expand(p)
		if box.position.y < 0.05 and box.end.y > 4.0 and box.end.y < 4.6 and box.size.x > 9.0 and box.size.z > 8.0:
			cs.disabled = true
	var body := StaticBody3D.new()
	body.name = "RoomBody"
	body.collision_layer = 3 # (as the building's: the world and the climbable)
	body.collision_mask = 0
	_rn.add_child(body)
	_collide(body, Vector3(-WALL_OUT, 0, BACK_OUT), Vector3(WALL_OUT, FLOOR_TOP, ROOM_BACK)) # the back
	_collide(body, Vector3(-WALL_OUT, 0, ROOM_BACK), Vector3(-ROOM_X, FLOOR_TOP, ROOM_FRONT)) # the left
	_collide(body, Vector3(ROOM_X, 0, ROOM_BACK), Vector3(WALL_OUT, FLOOR_TOP, ROOM_FRONT)) # the right
	_collide(body, Vector3(-WALL_OUT, ROOM_TOP, ROOM_BACK), Vector3(WALL_OUT, FLOOR_TOP, ROOM_FRONT)) # the ceiling
	# The front between and over the arches.
	var x := -WALL_OUT
	for a in _arches:
		_collide(body, Vector3(x, 0, ROOM_FRONT), Vector3(a[0], FLOOR_TOP, FRONT_OUT))
		var r: float = (a[1] - a[0]) * 0.5
		_collide(body, Vector3(a[0], a[2] - r * 0.3, ROOM_FRONT), Vector3(a[1], FLOOR_TOP, FRONT_OUT))
		x = a[1]
	_collide(body, Vector3(x, 0, ROOM_FRONT), Vector3(WALL_OUT, FLOOR_TOP, FRONT_OUT))


func _collide(p_body: StaticBody3D, p_lo: Vector3, p_hi: Vector3) -> CollisionShape3D:
	var cs := CollisionShape3D.new()
	var b := BoxShape3D.new()
	b.size = (p_hi - p_lo).abs()
	cs.shape = b
	cs.position = (p_lo + p_hi) * 0.5
	p_body.add_child(cs)
	return cs


# ------------------------------------------------------------------ the mesh builder (metric UVs, as the town's)


var _st := {} # material key -> SurfaceTool


func _tool(p_mat: String) -> SurfaceTool:
	if not _st.has(p_mat):
		var st := SurfaceTool.new()
		st.begin(Mesh.PRIMITIVE_TRIANGLES)
		_st[p_mat] = st
	return _st[p_mat]


## A triangle facing p_n (Godot's front faces are clockwise seen from the front).
func _tri(p_mat: String, a: Vector3, b: Vector3, c: Vector3, ua: Vector2, ub: Vector2, uc: Vector2, p_n: Vector3) -> void:
	var st := _tool(p_mat)
	if (b - a).cross(c - a).dot(p_n) > 0.0:
		var tmp := b
		b = c
		c = tmp
		var tu := ub
		ub = uc
		uc = tu
	for pair in [[a, ua], [b, ub], [c, uc]]:
		st.set_normal(p_n)
		st.set_uv(pair[1])
		st.add_vertex(pair[0])


## A quad a b c d (in order round it) facing p_n; its UVs along p_u and p_v (metres).
func _quad(p_mat: String, a: Vector3, b: Vector3, c: Vector3, d: Vector3, p_n: Vector3, p_u: Vector3, p_v: Vector3) -> void:
	var f := func(p: Vector3) -> Vector2: return Vector2(p.dot(p_u), p.dot(p_v))
	_tri(p_mat, a, b, c, f.call(a), f.call(b), f.call(c), p_n)
	_tri(p_mat, a, c, d, f.call(a), f.call(c), f.call(d), p_n)


## A box (its centre, size, turned by p_basis); its UVs' u along its longest side (the grain).
func _box(p_mat: String, p_center: Vector3, p_size: Vector3, p_basis := Basis()) -> void:
	var h := p_size * 0.5
	var long := 0 if p_size.x >= p_size.y and p_size.x >= p_size.z else (1 if p_size.y >= p_size.z else 2)
	for axis in 3:
		for s in [-1.0, 1.0]:
			var n := Vector3()
			n[axis] = s
			var u_ax := (axis + 1) % 3
			var v_ax := (axis + 2) % 3
			if long != axis:
				# (u along the box's long side where the face has it)
				if v_ax == long:
					var tmp := u_ax
					u_ax = v_ax
					v_ax = tmp
			var du := Vector3()
			du[u_ax] = h[u_ax]
			var dv := Vector3()
			dv[v_ax] = h[v_ax]
			var c := n * h[axis]
			var corners := [c - du - dv, c + du - dv, c + du + dv, c - du + dv]
			var w := []
			for p in corners:
				w.append(p_center + p_basis * p)
			var ue := Vector3()
			ue[u_ax] = 1.0
			var ve := Vector3()
			ve[v_ax] = 1.0
			var nw := (p_basis * n).normalized()
			var uw := (p_basis * ue).normalized()
			var vw := (p_basis * ve).normalized()
			_quad(p_mat, w[0], w[1], w[2], w[3], nw, uw, vw)


## A cylinder along +Y (a barrel's staves, a cup), p_seg sides, capped; UVs round it in metres.
func _cylinder(p_mat: String, p_base: Vector3, p_r0: float, p_r1: float, p_h: float, p_seg := 12, p_basis := Basis(), p_caps := true) -> void:
	var circ := TAU * maxf(p_r0, p_r1)
	for i in p_seg:
		var a0 := TAU * i / p_seg
		var a1 := TAU * (i + 1) / p_seg
		var b0 := p_base + p_basis * Vector3(cos(a0) * p_r0, 0, sin(a0) * p_r0)
		var b1 := p_base + p_basis * Vector3(cos(a1) * p_r0, 0, sin(a1) * p_r0)
		var t0 := p_base + p_basis * Vector3(cos(a0) * p_r1, p_h, sin(a0) * p_r1)
		var t1 := p_base + p_basis * Vector3(cos(a1) * p_r1, p_h, sin(a1) * p_r1)
		var nm := (p_basis * Vector3(cos((a0 + a1) * 0.5), (p_r0 - p_r1) / maxf(p_h, 1e-3), sin((a0 + a1) * 0.5))).normalized()
		var u0 := circ * i / p_seg
		var u1 := circ * (i + 1) / p_seg
		_tri(p_mat, b0, b1, t1, Vector2(u0, 0), Vector2(u1, 0), Vector2(u1, p_h), nm)
		_tri(p_mat, b0, t1, t0, Vector2(u0, 0), Vector2(u1, p_h), Vector2(u0, p_h), nm)
		if p_caps:
			var up := (p_basis * Vector3.UP).normalized()
			var ct := p_base + p_basis * Vector3(0, p_h, 0)
			_tri(p_mat, ct, t0, t1, Vector2(), Vector2(cos(a0), sin(a0)) * p_r1, Vector2(cos(a1), sin(a1)) * p_r1, up)
			_tri(p_mat, p_base, b1, b0, Vector2(), Vector2(cos(a1), sin(a1)) * p_r0, Vector2(cos(a0), sin(a0)) * p_r0, -up)


## A barrel lying or standing: bulging staves (p_seg round, 6 along), iron hoops.
func _barrel(p_center: Vector3, p_len: float, p_r: float, p_basis: Basis) -> void:
	var rings := 6
	for k in rings:
		var y0 := -p_len * 0.5 + p_len * k / rings
		var y1 := y0 + p_len / rings
		var r0 := p_r * (0.86 + 0.14 * sin(PI * float(k) / rings))
		var r1 := p_r * (0.86 + 0.14 * sin(PI * float(k + 1) / rings))
		_cylinder("barrel", p_center + p_basis * Vector3(0, y0, 0), r0, r1, y1 - y0, 16, p_basis, k == 0 or k == rings - 1)
	for y in [-0.38, -0.2, 0.2, 0.38]:
		var r := p_r * (0.86 + 0.14 * sin(PI * (y + 0.5))) + 0.006
		_cylinder("iron", p_center + p_basis * Vector3(0, y * p_len - 0.02, 0), r, r, 0.04, 16, p_basis, false)


func _commit(p_name: String) -> void:
	for key in _st:
		var st: SurfaceTool = _st[key]
		st.generate_tangents()
		var mi := MeshInstance3D.new()
		mi.name = "%s_%s" % [p_name, key]
		mi.mesh = st.commit()
		mi.material_override = _mats[key]
		_rn.add_child(mi)
	_st.clear()


# ------------------------------------------------------------------ the room


func _room() -> void:
	var y0 := 0.0
	# The floor (cotto) and the ceiling (planks).
	_quad("floor", Vector3(-ROOM_X, y0, ROOM_BACK), Vector3(ROOM_X, y0, ROOM_BACK), Vector3(ROOM_X, y0, ROOM_FRONT), Vector3(-ROOM_X, y0, ROOM_FRONT),
			Vector3.UP, Vector3.RIGHT, Vector3.BACK)
	_quad("planks", Vector3(-ROOM_X, ROOM_TOP, ROOM_BACK), Vector3(ROOM_X, ROOM_TOP, ROOM_BACK), Vector3(ROOM_X, ROOM_TOP, ROOM_FRONT),
			Vector3(-ROOM_X, ROOM_TOP, ROOM_FRONT), Vector3.DOWN, Vector3.BACK, Vector3.RIGHT)
	# The walls: the back, the sides (a stone skirting under the plaster), the front round the arches.
	var skirt := 0.16
	for side in [[Vector3(-ROOM_X, 0, ROOM_BACK), Vector3(ROOM_X, 0, ROOM_BACK), Vector3.BACK, Vector3.RIGHT],
			[Vector3(-ROOM_X, 0, ROOM_FRONT), Vector3(-ROOM_X, 0, ROOM_BACK), Vector3.RIGHT, Vector3.FORWARD],
			[Vector3(ROOM_X, 0, ROOM_BACK), Vector3(ROOM_X, 0, ROOM_FRONT), Vector3.LEFT, Vector3.BACK]]:
		var a: Vector3 = side[0]
		var b: Vector3 = side[1]
		var n: Vector3 = side[2]
		var u: Vector3 = side[3]
		_quad("stone", a, b, b + Vector3.UP * skirt, a + Vector3.UP * skirt, n, u, Vector3.UP)
		_quad("wall", a + Vector3.UP * skirt, b + Vector3.UP * skirt, b + Vector3.UP * ROOM_TOP, a + Vector3.UP * ROOM_TOP, n, u, Vector3.UP)
	_front_wall()
	# The beams: across the room every 1.05 m, under the planks; a wall plate along each side.
	var z := ROOM_BACK + 0.55
	while z < ROOM_FRONT - 0.3:
		_box("beam", Vector3(0, ROOM_TOP - 0.12, z), Vector3(ROOM_X * 2.0, 0.24, 0.17))
		z += 1.05
	for s in [-1.0, 1.0]:
		_box("beam", Vector3(s * (ROOM_X - 0.08), ROOM_TOP - 0.3, (ROOM_BACK + ROOM_FRONT) * 0.5), Vector3(0.16, 0.16, ROOM_FRONT - ROOM_BACK))
	_commit("Room")


## The front wall's inside (facing into the room) round the arches: a grid split at their edges, the cells in an
## arch's box left out, its spandrels filled (as the generator's wall(), tools/blender/ac_buildings.py).
func _front_wall() -> void:
	var z := ROOM_FRONT
	var n := Vector3.FORWARD
	var u := Vector3.LEFT
	var xs := [-ROOM_X, ROOM_X]
	var ys := [0.0, ROOM_TOP]
	for a in _arches:
		xs.append_array([a[0], a[1]])
		ys.append(a[2])
	xs.sort()
	ys.sort()
	for i in xs.size() - 1:
		for k in ys.size() - 1:
			var xa: float = xs[i]
			var xb: float = xs[i + 1]
			var ya: float = ys[k]
			var yb: float = ys[k + 1]
			if xb - xa < 1e-4 or yb - ya < 1e-4:
				continue
			var xm := (xa + xb) * 0.5
			var ym := (ya + yb) * 0.5
			var inside := false
			for a in _arches:
				if xm > a[0] and xm < a[1] and ym < a[2]:
					inside = true
			if inside:
				continue
			var mat := "stone" if yb <= 0.161 else "wall"
			_quad(mat, Vector3(xa, ya, z), Vector3(xb, ya, z), Vector3(xb, yb, z), Vector3(xa, yb, z), n, u, Vector3.UP)
	for a in _arches:
		# (the floor's edge in the arch: down to the reveal's sill)
		_quad("stone", Vector3(a[0], -FLOOR, z), Vector3(a[1], -FLOOR, z), Vector3(a[1], 0.0, z), Vector3(a[0], 0.0, z), Vector3.BACK, Vector3.RIGHT, Vector3.UP)
	for a in _arches:
		var r: float = (a[1] - a[0]) * 0.5
		var xm: float = (a[0] + a[1]) * 0.5
		var zs: float = a[2] - r
		var seg := 12
		var pts: Array = []
		for i in seg + 1:
			pts.append(Vector3(xm + r * cos(PI * i / seg), zs + r * sin(PI * i / seg), z))
		var half := seg / 2
		var right_corner := Vector3(a[1], a[2], z)
		var left_corner := Vector3(a[0], a[2], z)
		for i in seg:
			var corner := right_corner if i < half else left_corner
			var p: Vector3 = pts[i]
			var q: Vector3 = pts[i + 1]
			_tri("wall", corner, p, q, Vector2(corner.dot(u), corner.y), Vector2(p.dot(u), p.y), Vector2(q.dot(u), q.y), n)


# ------------------------------------------------------------------ the fireplace


func _fireplace() -> void:
	var zb := ROOM_BACK
	var w := 1.9 # the opening's width
	var hearth := 0.22
	var open_h := 1.25
	var depth := 0.75
	# The hearth slab out into the room, the opening's sides and back (sooty), the jambs, the mantel, the hood.
	_box("stone", Vector3(0, hearth * 0.5, zb + depth * 0.5 + 0.12), Vector3(w + 0.9, hearth, depth + 0.24))
	_quad("soot", Vector3(-w * 0.5, hearth, zb + 0.005), Vector3(w * 0.5, hearth, zb + 0.005), Vector3(w * 0.5, hearth + open_h, zb + 0.005),
			Vector3(-w * 0.5, hearth + open_h, zb + 0.005), Vector3.BACK, Vector3.RIGHT, Vector3.UP)
	for s in [-1.0, 1.0]:
		_box("stone", Vector3(s * (w * 0.5 + 0.17), hearth + open_h * 0.5, zb + depth * 0.5), Vector3(0.34, open_h, depth))
		_quad("soot", Vector3(s * w * 0.5, hearth, zb), Vector3(s * w * 0.5, hearth, zb + depth), Vector3(s * w * 0.5, hearth + open_h, zb + depth),
				Vector3(s * w * 0.5, hearth + open_h, zb), Vector3(-s, 0, 0), Vector3.BACK, Vector3.UP)
	_box("stone", Vector3(0, hearth + open_h + 0.12, zb + depth * 0.5 + 0.04), Vector3(w + 0.9, 0.24, depth + 0.08))
	_quad("soot", Vector3(-w * 0.5, hearth + open_h, zb), Vector3(w * 0.5, hearth + open_h, zb), Vector3(w * 0.5, hearth + open_h, zb + depth),
			Vector3(-w * 0.5, hearth + open_h, zb + depth), Vector3.DOWN, Vector3.RIGHT, Vector3.BACK)
	# The hood: from the mantel up to the ceiling, narrowing.
	var y0 := hearth + open_h + 0.24
	var y1 := ROOM_TOP
	var hw0 := w * 0.5 + 0.4
	var hw1 := w * 0.5 - 0.1
	var d0 := depth + 0.02
	var d1 := 0.45
	var fl := Vector3(-hw0, y0, zb + d0)
	var fr := Vector3(hw0, y0, zb + d0)
	var tl := Vector3(-hw1, y1, zb + d1)
	var tr := Vector3(hw1, y1, zb + d1)
	_quad("wall", fl, fr, tr, tl, Vector3(0, d0 - d1, y1 - y0).normalized(), Vector3.RIGHT, Vector3.UP)
	for s in [-1.0, 1.0]:
		var b0 := Vector3(s * hw0, y0, zb)
		var b1 := Vector3(s * hw1, y1, zb)
		var f0 := fr if s > 0 else fl
		var f1 := tr if s > 0 else tl
		var nm := (f0 - b0).cross(b1 - b0).normalized()
		if nm.x * s < 0.0:
			nm = -nm
		_quad("wall", b0, f0, f1, b1, nm, Vector3.BACK, Vector3.UP)
	# The andirons and the logs, the fire in its gas (closed but for the front and the flue above).
	for s in [-1.0, 1.0]:
		_box("iron", Vector3(s * 0.3, hearth + 0.11, zb + 0.38), Vector3(0.04, 0.04, 0.5))
		_box("iron", Vector3(s * 0.3, hearth + 0.22, zb + 0.62), Vector3(0.04, 0.22, 0.04))
	_commit("Fireplace")
	if not ClassDB.class_exists(&"FellowGasVolume3D"):
		return
	var gas: Node3D = ClassDB.instantiate(&"FellowGasVolume3D")
	gas.name = "FireGas"
	gas.set("size", Vector3(w - 0.04, 2.4, depth))
	gas.set("resolution", 48)
	gas.set("closed_faces", 1 | 2 | 4 | 16) # the sides, the hearth, the back: the room in front and the flue above open
	gas.set("smoke_dissipation", 0.5)
	gas.set("edge_fade", 0.5)
	gas.set("prewarm_steps", 240)
	gas.set("turbulence", 3.0)
	gas.set("fire_intensity", 16.0)
	gas.set("physics_obstacles", false)
	gas.set("fire_light_shadows", true)
	gas.set("fire_light_range", 9.0)
	gas.set("fire_light_smoothing", 0.15)
	gas.set("fire_light_energy", 1.4)
	gas.set("sleep_distance", 45.0)
	gas.position = Vector3(0, hearth, zb + depth * 0.5)
	_rn.add_child(gas)
	_fire = gas
	# Milestone 228: its light's shadow a dual paraboloid's, its seam level with the fire (the light's own z up: the
	# hemispheres the hood over it and the room under its height) — with the seam upright, through the hood's sides,
	# milestone 219 had found the hood's walls shaded wrongly; level, the room looks as with the cube's six faces.
	var fl := gas.call("get_fire_light_node") as OmniLight3D
	if fl:
		fl.omni_shadow_mode = OmniLight3D.SHADOW_DUAL_PARABOLOID
		fl.global_basis = Basis(Vector3.RIGHT, -PI * 0.5)
	var e: Node3D = ClassDB.instantiate(&"FellowGasEmitter3D")
	e.set("shape", 1)
	e.set("box_size", Vector3(0.6, 0.1, 0.3))
	e.position = Vector3(0, 0.06, 0.04)
	e.set("fuel", 1.8)
	e.set("temperature", 900.0)
	e.set("smoke", 0.06)
	e.set("noise", 0.7)
	e.set("noise_scale", 0.15)
	gas.add_child(e)
	var log_mesh := CylinderMesh.new()
	log_mesh.top_radius = 0.07
	log_mesh.bottom_radius = 0.075
	log_mesh.height = 0.85
	log_mesh.radial_segments = 12
	var wood := _std(Color(0.3, 0.2, 0.12), 0.9)
	for i in 3:
		var mi := MeshInstance3D.new()
		mi.mesh = log_mesh
		mi.material_override = wood
		var yaw: float = [0.08, -0.1, 0.0][i]
		mi.transform = Transform3D(Basis(Vector3.UP, yaw) * Basis(Vector3.BACK, PI * 0.5), Vector3([0.0, 0.0, 0.02][i], hearth + [0.2, 0.2, 0.33][i], zb + [0.3, 0.52, 0.41][i]))
		_rn.add_child(mi)
		var burn: Node = ClassDB.instantiate(&"FellowBurnable3D")
		burn.set("resolution", 24)
		burn.set("pyrolysis_rate", 0.012)
		mi.add_child(burn)
		burn.set("gas_volume_path", burn.get_path_to(gas))


# ------------------------------------------------------------------ the counter, the shelves, the barrels


const COUNTER_X := -3.45
const COUNTER_Z := [-2.3, 1.5]

func _counter() -> void:
	var zc: float = (COUNTER_Z[0] + COUNTER_Z[1]) * 0.5
	var zl: float = COUNTER_Z[1] - COUNTER_Z[0]
	_box("counter", Vector3(COUNTER_X, 0.5, zc), Vector3(0.55, 1.0, zl))
	_box("counter", Vector3(COUNTER_X + 0.02, 1.03, zc), Vector3(0.68, 0.06, zl + 0.1))
	# The shelves on the wall behind, their bottles and jugs.
	var rng := RandomNumberGenerator.new()
	rng.seed = 213
	var shelf_x := -ROOM_X + 0.15
	for y in [1.25, 1.7, 2.15]:
		_box("counter", Vector3(shelf_x, y, zc), Vector3(0.3, 0.04, zl))
		var z: float = COUNTER_Z[0] + 0.15
		while z < COUNTER_Z[1] - 0.1:
			var kind := rng.randi() % 5
			var at := Vector3(shelf_x + rng.randf_range(-0.05, 0.05), y + 0.02, z)
			if kind < 3:
				var mat: String = ["bottle_green", "bottle_brown", "bottle_clear"][kind]
				_cylinder(mat, at, 0.04, 0.04, 0.2, 10)
				_cylinder(mat, at + Vector3(0, 0.2, 0), 0.04, 0.014, 0.06, 10)
				_cylinder(mat, at + Vector3(0, 0.26, 0), 0.014, 0.014, 0.06, 8)
				z += 0.11
			elif kind == 3:
				_cylinder("clay", at, 0.06, 0.085, 0.13, 12)
				_cylinder("clay", at + Vector3(0, 0.13, 0), 0.085, 0.045, 0.1, 12)
				z += 0.2
			else:
				for k in 3:
					_cylinder("glaze", at + Vector3(0, 0.012 * k, 0), 0.11, 0.12, 0.012, 16)
				z += 0.26
	# Bottles and cups on the counter.
	for k in 5:
		var at := Vector3(COUNTER_X + rng.randf_range(-0.15, 0.15), 1.06, COUNTER_Z[0] + 0.4 + k * 0.7 + rng.randf_range(-0.1, 0.1))
		if k % 2 == 0:
			_cylinder("bottle_green", at, 0.04, 0.04, 0.2, 10)
			_cylinder("bottle_green", at + Vector3(0, 0.2, 0), 0.04, 0.014, 0.07, 10)
		else:
			_cylinder("clay", at, 0.035, 0.04, 0.09, 10)
	# The barrels: three lying on a rack in the back corner, one standing at the counter's end.
	_box("beam", Vector3(-ROOM_X + 0.9, 0.12, ROOM_BACK + 0.55), Vector3(1.6, 0.1, 0.1))
	_box("beam", Vector3(-ROOM_X + 0.9, 0.12, ROOM_BACK + 1.25), Vector3(1.6, 0.1, 0.1))
	var lying := Basis(Vector3.RIGHT, PI * 0.5)
	for k in 2:
		_barrel(Vector3(-ROOM_X + 0.5 + k * 0.82, 0.55, ROOM_BACK + 0.9), 0.95, 0.4, lying)
	_barrel(Vector3(-ROOM_X + 0.9, 1.25, ROOM_BACK + 0.9), 0.9, 0.36, lying)
	_barrel(Vector3(COUNTER_X + 0.05, 0.42, COUNTER_Z[1] + 0.55), 0.84, 0.3, Basis())
	_commit("Counter")
	var body := _rn.get_node("RoomBody") as StaticBody3D
	_collide(body, Vector3(COUNTER_X - 0.34, 0, COUNTER_Z[0] - 0.05), Vector3(COUNTER_X + 0.34, 1.06, COUNTER_Z[1] + 0.05))
	_collide(body, Vector3(-ROOM_X, 0, ROOM_BACK), Vector3(-ROOM_X + 1.75, 1.6, ROOM_BACK + 1.4))


# ------------------------------------------------------------------ the tables


## The tables: centre, length along x. Their benches 0.62 m in front and behind (0.47 m high: the sitting clip's).
const TABLES := [[Vector2(2.55, 1.45), 1.9], [Vector2(2.55, -1.55), 1.9], [Vector2(-0.85, 0.55), 1.7], [Vector2(-0.6, -2.1), 1.6]]
const BENCH_OFF := 0.62

func _tables() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 2130
	var body := _rn.get_node("RoomBody") as StaticBody3D
	for t in TABLES:
		var c := Vector3(t[0].x, 0, t[0].y)
		var l: float = t[1]
		_box("table", c + Vector3(0, 0.735, 0), Vector3(l, 0.05, 0.82))
		for sx in [-1.0, 1.0]:
			for sz in [-1.0, 1.0]:
				_box("table", c + Vector3(sx * (l * 0.5 - 0.1), 0.355, sz * 0.31), Vector3(0.07, 0.71, 0.07))
		_box("table", c + Vector3(0, 0.15, 0), Vector3(l - 0.2, 0.06, 0.05))
		for sz in [-1.0, 1.0]:
			var bc := c + Vector3(0, 0, sz * BENCH_OFF)
			_box("table", bc + Vector3(0, 0.45, 0), Vector3(l, 0.045, 0.3))
			for sx in [-1.0, 1.0]:
				_box("table", bc + Vector3(sx * (l * 0.5 - 0.12), 0.215, 0), Vector3(0.06, 0.43, 0.24))
		_collide(body, c + Vector3(-l * 0.5, 0, -0.41), c + Vector3(l * 0.5, 0.76, 0.41))
		for sz in [-1.0, 1.0]:
			_collide(body, c + Vector3(-l * 0.5, 0, sz * BENCH_OFF - 0.15), c + Vector3(l * 0.5, 0.47, sz * BENCH_OFF + 0.15))
		# On it: a jug, cups, plates with bread, a candle.
		var top := 0.76
		_cylinder("clay", c + Vector3(rng.randf_range(-0.3, 0.3), top, rng.randf_range(-0.1, 0.1)), 0.065, 0.09, 0.14, 14)
		for k in 4:
			var at := c + Vector3(-l * 0.4 + k * l * 0.27 + rng.randf_range(-0.06, 0.06), top, (0.22 if k % 2 == 0 else -0.22) + rng.randf_range(-0.04, 0.04))
			_cylinder("glaze", at, 0.1, 0.12, 0.015, 16)
			if rng.randf() < 0.5:
				_cylinder("bread", at + Vector3(0.02, 0.015, 0.0), 0.05, 0.035, 0.05, 10)
			_cylinder("clay", at + Vector3(0.14, 0, 0.0), 0.035, 0.04, 0.09, 10)
			_cylinder("wine", at + Vector3(0.14, 0.07, 0.0), 0.033, 0.033, 0.012, 10)
		var cp := c + Vector3(rng.randf_range(-0.15, 0.15), top, rng.randf_range(-0.05, 0.05))
		_candle(cp, 1.0)
	_commit("Tables")


func _candle(p_at: Vector3, p_energy: float) -> void:
	_cylinder("iron", p_at, 0.04, 0.04, 0.015, 10)
	_cylinder("wax", p_at + Vector3(0, 0.015, 0), 0.016, 0.016, 0.13, 8)
	var flame := MeshInstance3D.new()
	var sm := SphereMesh.new()
	sm.radius = 0.008
	sm.height = 0.035
	sm.radial_segments = 8
	sm.rings = 4
	flame.mesh = sm
	flame.material_override = _mats.flame
	flame.position = p_at + Vector3(0, 0.165, 0)
	flame.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_rn.add_child(flame)
	var light: OmniLight3D = null
	if p_energy > 0.0:
		# (a light of its own; the chandelier's candles share one)
		light = OmniLight3D.new()
		light.light_color = Color(1.0, 0.66, 0.34)
		light.light_energy = 0.45 * p_energy
		light.omni_range = 2.6
		light.omni_attenuation = 1.6
		light.position = p_at + Vector3(0, 0.22, 0)
		_rn.add_child(light)
	_candles.append([light, light.light_energy if light else 0.0, randf() * 10.0, flame])


# ------------------------------------------------------------------ the chandelier, the hams


func _hanging() -> void:
	# An iron ring of six candles from the middle beam, on a chain.
	var c := Vector3(0.9, 2.75, -0.3)
	var seg := 24
	for i in seg:
		var a0 := TAU * i / seg
		var a1 := TAU * (i + 1) / seg
		var p0 := c + Vector3(cos(a0), 0, sin(a0)) * 0.42
		var p1 := c + Vector3(cos(a1), 0, sin(a1)) * 0.42
		var mid := (p0 + p1) * 0.5
		var dir := (p1 - p0)
		_box("iron", mid, Vector3(dir.length(), 0.025, 0.025), Basis(Vector3.UP, -atan2(dir.z, dir.x)))
	for i in 4:
		var a := TAU * i / 4 + 0.4
		var foot := c + Vector3(cos(a), 0, sin(a)) * 0.42
		var top := c + Vector3(0, 0.55, 0)
		var d := top - foot
		_box("iron", (foot + top) * 0.5, Vector3(0.012, d.length(), 0.012), _aim(d))
	_box("iron", c + Vector3(0, 0.55 + (ROOM_TOP - 0.24 - c.y - 0.55) * 0.5, 0), Vector3(0.015, ROOM_TOP - 0.24 - c.y - 0.55, 0.015))
	for i in 6:
		var a := TAU * i / 6
		_candle(c + Vector3(cos(a), 0, sin(a)) * 0.42 + Vector3(0, 0.012, 0), 0.0)
	var big := OmniLight3D.new()
	big.light_color = Color(1.0, 0.68, 0.38)
	big.light_energy = 1.3
	big.omni_range = 7.5
	big.omni_attenuation = 1.3
	big.shadow_enabled = true
	big.position = c + Vector3(0, 0.25, 0)
	_rn.add_child(big)
	_candles.append([big, big.light_energy, 3.3, null])
	# Hams and garlic from the beam over the counter.
	var bz := ROOM_BACK + 0.55 + 1.05 * 3
	for k in 4:
		var at := Vector3(COUNTER_X + 0.6 + k * 0.32, ROOM_TOP - 0.24, bz + (0.0 if k % 2 == 0 else 0.06))
		_box("beam", at + Vector3(0, -0.2, 0), Vector3(0.008, 0.4, 0.008))
		if k % 2 == 0:
			_cylinder("ham", at + Vector3(0, -0.85, 0), 0.03, 0.12, 0.45, 12)
		else:
			for g in 5:
				var gp := at + Vector3(sin(g * 2.1) * 0.03, -0.42 - g * 0.07, cos(g * 2.1) * 0.03)
				_cylinder("garlic", gp, 0.025, 0.035, 0.045, 8)
				_cylinder("garlic", gp + Vector3(0, 0.045, 0), 0.035, 0.006, 0.03, 8)
	_commit("Hanging")


func _aim(p_dir: Vector3) -> Basis:
	var y := p_dir.normalized()
	var x := y.cross(Vector3.FORWARD)
	if x.length() < 1e-3:
		x = Vector3.RIGHT
	x = x.normalized()
	return Basis(x, y, x.cross(y))


func _process(delta: float) -> void:
	_t += delta
	for c in _candles:
		var l: OmniLight3D = c[0]
		var ph: float = c[2]
		var f := 0.88 + 0.08 * sin(_t * 9.1 + ph) + 0.05 * sin(_t * 23.7 + ph * 1.7) + 0.03 * sin(_t * 3.3 + ph * 0.6)
		if l:
			l.light_energy = float(c[1]) * f
		if c[3]:
			(c[3] as Node3D).scale = Vector3(1, 0.85 + 0.25 * f, 1)


# ------------------------------------------------------------------ its people and its shelter


## Where the people sit and stand (global): the host behind the counter, guests at the tables, two at the counter, one
## at the fire.
func _people_and_shelter() -> void:
	var g := _rn.global_transform
	var host := Transform3D(Basis.looking_at(Vector3(1, 0, 0), Vector3.UP), Vector3(COUNTER_X - 0.62, 0, -0.4))
	_people.append({"kind": "stand", "xform": g * host, "clip": "idle"})
	var rng := RandomNumberGenerator.new()
	rng.seed = 21300
	var ti := 0
	for t in TABLES:
		var c := Vector3(t[0].x, 0, t[0].y)
		var l: float = t[1]
		var n := 2 if ti % 2 == 0 else 3
		for k in n:
			var sz := 1.0 if k % 2 == 0 else -1.0
			var x := -l * 0.3 + l * 0.6 * float(k) / maxf(n - 1, 1) + rng.randf_range(-0.08, 0.08)
			# (facing the table; on the ground in front of the bench: the sitting clip's hips 0.16 m behind)
			var face := Vector3(0, 0, -sz)
			var at := c + Vector3(x, 0, sz * (BENCH_OFF - 0.16))
			_people.append({"kind": "seat", "xform": g * Transform3D(Basis.looking_at(face, Vector3.UP), at), "clip": "sit"})
		ti += 1
	for k in 2:
		var at := Vector3(COUNTER_X + 0.62, 0, -1.2 + k * 0.85)
		_people.append({"kind": "stand", "xform": g * Transform3D(Basis.looking_at(Vector3(-1, 0, 0.3 - 0.6 * k), Vector3.UP), at), "clip": "talk"})
	_people.append({"kind": "stand", "xform": g * Transform3D(Basis.looking_at(Vector3(-0.3, 0, -1), Vector3.UP), Vector3(1.05, 0, ROOM_BACK + 1.6)), "clip": "warm_hands"})
	# The shelter: in through each arch, standing along the front wall inside.
	for a in _arches:
		var xm: float = (a[0] + a[1]) * 0.5
		var out := g * Vector3(xm, 0, FRONT_OUT + 0.9)
		var inn := g * Vector3(xm, 0, ROOM_FRONT - 0.7)
		for k in 3:
			var sx := xm + (-0.75 + 0.75 * k)
			var spot := Transform3D(Basis.looking_at(Vector3(0, 0, 1), Vector3.UP), Vector3(sx, 0, ROOM_FRONT - 0.55 - 0.45 * (k % 2)))
			_shelter.append({"out": out, "in": inn, "spot": g * spot, "taken": false})


## (rooftops_crowd.gd) the people inside: {kind "seat" | "stand", xform (global: where they stand or sit, facing its
## -Z), clip}.
func get_people() -> Array:
	return _people


## (rooftops_crowd.gd) the places to shelter from the rain: {out, in (global), spot (global Transform3D), taken}.
func get_shelter() -> Array:
	return _shelter


## (tools) the room: its arches, its fire's power.
func get_stats() -> Dictionary:
	return {"arches": _arches, "people": _people.size(), "shelter": _shelter.size(), "lights": _candles.size(),
			"fire_power": snappedf(float(_fire.call("get_fire_power")), 0.01) if _fire else 0.0}
