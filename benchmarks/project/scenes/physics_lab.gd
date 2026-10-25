extends "res://bench/scene_base.gd"
## PHYSICS LAB (built in code, deterministic): collisions and rigid-body physics as games use them, one case per run.
##
## Physics runs at the project's fixed tick (60 Hz, one step per frame with --fixed-fps 60); every input the case gives
## the simulation (characters' velocities, the probes' directions, launched balls) is a function of the tick number,
## so a run is reproducible and two builds of one engine can be compared tick for tick (main.gd's "physics" mode hashes
## every body's transform). The engine is the project's physics/3d/physics_engine (GodotPhysics3D or Jolt Physics,
## chosen at start-up through override.cfg).
##
## --physics_case=
##   pile   (default) 2000 rigid bodies (boxes, spheres, capsules, cylinders, convex hulls) dropped from a grid into a
##          20 x 20 m bin: falling, piling up, settling and sleeping. Broad phase, narrow phase, contact solver.
##   stack  25 towers of 16 boxes; from tick 90 a heavy ball launched at each row of towers: stacking stability, then
##          collapse.
##   chars  250 CharacterBody3D capsules walking on a 120 x 120 m trimesh terrain with hills and box obstacles
##          (move_and_slide with floor snapping), each turning every two seconds: character controllers.
##   rays   1500 RayCast3D and 150 ShapeCast3D nodes (engine-side queries every tick) on rotating rigs over a static
##          world of 600 boxes on the trimesh terrain: scene queries.
##   areas  300 Area3D triggers (monitoring, entered/exited signals counted) among 1000 bouncing spheres in a closed
##          box: overlap tracking.
## --physics_scale=<f>: the case's counts multiplied (default 1).
## --physics_visuals=0: no meshes (collision only; the rendering cost out of the measurement).

const BIN := 20.0
const TERRAIN := 120.0

var case_name := "pile"
var scale_f := 1.0
var visuals := true
var bodies: Array[Node3D] = [] # every moving body, in creation order (hashed by main.gd)
var _chars: Array[CharacterBody3D] = []
var _rigs: Array[Node3D] = []
var _balls: Array[RigidBody3D] = []
var _areas_entered := 0
var _areas_exited := 0
var _tick := 0
var _mat_cache := {}


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "physics"
	case_name = str(args.get("physics_case", "pile"))
	scale_f = float(args.get("physics_scale", "1"))
	visuals = str(args.get("physics_visuals", "1")) != "0"
	_setup_common()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.55, 0.62, 0.72)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.6, 0.65, 0.75)
	env.ambient_light_energy = 0.6
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-55, 35, 0)
	sun.light_energy = 1.2
	sun.shadow_enabled = true
	add_child(sun)
	match case_name:
		"pile":
			_build_pile()
		"stack":
			_build_stack()
		"chars":
			_build_chars()
		"rays":
			_build_rays()
		"areas":
			_build_areas()
		_:
			load_error = "unknown --physics_case " + case_name
			return
	info = {"case": case_name, "scale": scale_f, "visuals": visuals, "bodies": bodies.size(),
		"engine": ProjectSettings.get_setting("physics/3d/physics_engine"),
		"notes": "Physics Lab: one deterministic case per run, inputs driven by the tick number."}
	fixed_cameras = {"physics_overview": {"pos": Vector3(0, 26, 34), "target": Vector3(0, 2, 0), "fov": 60.0, "time": 0.0, "purpose": "the whole case"}}
	set_camera_pose(fixed_cameras["physics_overview"]["pos"], fixed_cameras["physics_overview"]["target"], 60.0)


func _count(n: int) -> int:
	return maxi(1, int(round(n * scale_f)))


func _mat(c: Color) -> StandardMaterial3D:
	var key := c.to_html()
	if not _mat_cache.has(key):
		var m := StandardMaterial3D.new()
		m.albedo_color = c
		m.roughness = 0.7
		_mat_cache[key] = m
	return _mat_cache[key]


func _static_box(pos: Vector3, size: Vector3, color: Color, p_parent: Node = self) -> StaticBody3D:
	var sb := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var bs := BoxShape3D.new()
	bs.size = size
	cs.shape = bs
	sb.add_child(cs)
	if visuals:
		var mi := MeshInstance3D.new()
		var bm := BoxMesh.new()
		bm.size = size
		mi.mesh = bm
		mi.material_override = _mat(color)
		sb.add_child(mi)
	sb.position = pos
	p_parent.add_child(sb)
	return sb


func _bin() -> void:
	_static_box(Vector3(0, -0.5, 0), Vector3(BIN + 2, 1, BIN + 2), Color(0.45, 0.45, 0.48))
	var h := 8.0
	for k in 4:
		var horiz := k < 2
		var sgn := 1.0 if k % 2 == 0 else -1.0
		var pos := Vector3(sgn * (BIN * 0.5 + 0.5), h * 0.5, 0) if horiz else Vector3(0, h * 0.5, sgn * (BIN * 0.5 + 0.5))
		var size := Vector3(1, h, BIN + 2) if horiz else Vector3(BIN + 2, h, 1)
		_static_box(pos, size, Color(0.5, 0.5, 0.55, 1))


# Shapes and meshes shared by the rigid bodies (index: 0 box, 1 sphere, 2 capsule, 3 cylinder, 4 convex).
var _shapes: Array[Shape3D] = []
var _meshes: Array[Mesh] = []


func _body_kinds() -> void:
	var box := BoxShape3D.new()
	box.size = Vector3(0.8, 0.8, 0.8)
	var sph := SphereShape3D.new()
	sph.radius = 0.45
	var cap := CapsuleShape3D.new()
	cap.radius = 0.3
	cap.height = 1.2
	var cyl := CylinderShape3D.new()
	cyl.radius = 0.4
	cyl.height = 0.8
	# A convex hull of 20 points on a slightly squashed sphere (deterministic).
	var pts := PackedVector3Array()
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	for i in 20:
		var v := Vector3(rng.randf_range(-1, 1), rng.randf_range(-1, 1), rng.randf_range(-1, 1)).normalized()
		pts.append(Vector3(v.x * 0.5, v.y * 0.35, v.z * 0.45))
	var cvx := ConvexPolygonShape3D.new()
	cvx.points = pts
	_shapes = [box, sph, cap, cyl, cvx]
	if visuals:
		var bm := BoxMesh.new()
		bm.size = box.size
		var sm := SphereMesh.new()
		sm.radius = sph.radius
		sm.height = sph.radius * 2.0
		sm.radial_segments = 16
		sm.rings = 8
		var cm := CapsuleMesh.new()
		cm.radius = cap.radius
		cm.height = cap.height
		cm.radial_segments = 16
		cm.rings = 4
		var ym := CylinderMesh.new()
		ym.top_radius = cyl.radius
		ym.bottom_radius = cyl.radius
		ym.height = cyl.height
		ym.radial_segments = 16
		# The hull as triangles (its debug mesh is lines).
		_meshes = [bm, sm, cm, ym, _hull_mesh(pts)]


func _hull_mesh(p_pts: PackedVector3Array) -> ArrayMesh:
	# Triangulates the convex hull by brute force (20 points): every triangle all other points lie behind.
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var n := p_pts.size()
	var centre := Vector3.ZERO
	for p in p_pts:
		centre += p
	centre /= float(n)
	for i in n:
		for j in range(i + 1, n):
			for k in range(j + 1, n):
				var a := p_pts[i]
				var b := p_pts[j]
				var c := p_pts[k]
				var nrm := (b - a).cross(c - a)
				if nrm.length_squared() < 1e-10:
					continue
				nrm = nrm.normalized()
				if nrm.dot(a - centre) < 0.0:
					nrm = -nrm
					var t := b
					b = c
					c = t
				var ok := true
				for m in n:
					if m != i and m != j and m != k and nrm.dot(p_pts[m] - a) > 1e-5:
						ok = false
						break
				if ok:
					st.set_normal(nrm)
					st.add_vertex(a)
					st.add_vertex(b)
					st.add_vertex(c)
	return st.commit()


func _rigid(kind: int, xf: Transform3D, color: Color, mass := 1.0) -> RigidBody3D:
	var rb := RigidBody3D.new()
	rb.mass = mass
	var cs := CollisionShape3D.new()
	cs.shape = _shapes[kind]
	rb.add_child(cs)
	if visuals:
		var mi := MeshInstance3D.new()
		mi.mesh = _meshes[kind]
		mi.material_override = _mat(color)
		rb.add_child(mi)
	rb.transform = xf
	add_child(rb)
	bodies.append(rb)
	return rb


const PALETTE := [Color(0.85, 0.35, 0.3), Color(0.3, 0.6, 0.85), Color(0.9, 0.75, 0.3), Color(0.4, 0.75, 0.4), Color(0.7, 0.45, 0.8)]


func _build_pile() -> void:
	_bin()
	_body_kinds()
	var n := _count(2000)
	var rng := RandomNumberGenerator.new()
	rng.seed = 2026
	# A grid of 14 x 14 per layer, 1.2 m apart, layers from 3 m up.
	var per_row := 14
	for i in n:
		var layer := i / (per_row * per_row)
		var r := i % (per_row * per_row)
		var x := (r % per_row - (per_row - 1) * 0.5) * 1.2 + rng.randf_range(-0.15, 0.15)
		var z := (r / per_row - (per_row - 1) * 0.5) * 1.2 + rng.randf_range(-0.15, 0.15)
		var y := 3.0 + layer * 1.3
		var basis := Basis.from_euler(Vector3(rng.randf() * TAU, rng.randf() * TAU, rng.randf() * TAU))
		var kind := i % 5
		_rigid(kind, Transform3D(basis, Vector3(x, y, z)), PALETTE[kind])


func _build_stack() -> void:
	_bin()
	_body_kinds()
	var towers := _count(25)
	var levels := 16
	var side := int(ceil(sqrt(float(towers))))
	var box_kind := 0
	for t in towers:
		var tx := (t % side - (side - 1) * 0.5) * 3.0
		var tz := (t / side - (side - 1) * 0.5) * 3.0
		for l in levels:
			# Boxes 0.8 m, stacked with a small gap, alternate rows rotated by 45 degrees (a stable but non-trivial stack).
			var basis := Basis(Vector3.UP, (PI * 0.25) if l % 2 == 1 else 0.0)
			_rigid(box_kind, Transform3D(basis, Vector3(tx, 0.4 + l * 0.801, tz)), PALETTE[l % 5])
	# The balls (frozen until their launch at tick 90).
	for row in side:
		var b := _rigid(1, Transform3D(Basis(), Vector3(-BIN * 0.5 + 1.5, 2.0 + row * 0.0, (row - (side - 1) * 0.5) * 3.0)), Color(0.2, 0.2, 0.22), 40.0)
		b.freeze = true
		_balls.append(b)


func _terrain_height(x: float, z: float) -> float:
	return 2.5 * sin(x * 0.07) * cos(z * 0.05) + 1.2 * sin(x * 0.19 + z * 0.13)


func _terrain() -> void:
	var n := 60 # cells per side (2 m)
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var cell := TERRAIN / n
	var faces := PackedVector3Array()
	for i in n:
		for j in n:
			var x0 := -TERRAIN * 0.5 + i * cell
			var z0 := -TERRAIN * 0.5 + j * cell
			var p00 := Vector3(x0, _terrain_height(x0, z0), z0)
			var p10 := Vector3(x0 + cell, _terrain_height(x0 + cell, z0), z0)
			var p01 := Vector3(x0, _terrain_height(x0, z0 + cell), z0 + cell)
			var p11 := Vector3(x0 + cell, _terrain_height(x0 + cell, z0 + cell), z0 + cell)
			# Godot's front faces are clockwise as seen from their front: these face up (+y). (Until 2026-10-25 the
			# terrain was wound the other way, facing down: the characters fell through it.)
			for v in [p00, p10, p11, p00, p11, p01]:
				faces.append(v)
	var cw := faces
	var shape := ConcavePolygonShape3D.new()
	shape.set_faces(cw)
	var sb := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	cs.shape = shape
	sb.add_child(cs)
	if visuals:
		for v in cw:
			st.add_vertex(v)
		st.generate_normals()
		var mi := MeshInstance3D.new()
		mi.mesh = st.commit()
		mi.material_override = _mat(Color(0.45, 0.55, 0.38))
		sb.add_child(mi)
	add_child(sb)


func _build_chars() -> void:
	_terrain()
	var rng := RandomNumberGenerator.new()
	rng.seed = 99
	for k in _count(120):
		var x := rng.randf_range(-TERRAIN * 0.45, TERRAIN * 0.45)
		var z := rng.randf_range(-TERRAIN * 0.45, TERRAIN * 0.45)
		var size := Vector3(rng.randf_range(1, 4), rng.randf_range(1, 3), rng.randf_range(1, 4))
		_static_box(Vector3(x, _terrain_height(x, z) + size.y * 0.4, z), size, Color(0.55, 0.5, 0.45))
	var cap := CapsuleShape3D.new()
	cap.radius = 0.35
	cap.height = 1.8
	var mesh: CapsuleMesh = null
	if visuals:
		mesh = CapsuleMesh.new()
		mesh.radius = cap.radius
		mesh.height = cap.height
		mesh.radial_segments = 12
		mesh.rings = 3
	var n := _count(250)
	for i in n:
		var cb := CharacterBody3D.new()
		var cs := CollisionShape3D.new()
		cs.shape = cap
		cb.add_child(cs)
		if visuals:
			var mi := MeshInstance3D.new()
			mi.mesh = mesh
			mi.material_override = _mat(PALETTE[i % 5])
			cb.add_child(mi)
		cb.floor_snap_length = 0.5
		cb.floor_max_angle = deg_to_rad(50)
		var a := float(i) * 2.399963
		var r := 4.0 + sqrt(float(i)) * 3.0
		var x := cos(a) * r
		var z := sin(a) * r
		cb.position = Vector3(x, _terrain_height(x, z) + 1.5, z)
		add_child(cb)
		_chars.append(cb)
		bodies.append(cb)


func _build_rays() -> void:
	_terrain()
	var rng := RandomNumberGenerator.new()
	rng.seed = 314
	for k in _count(600):
		var x := rng.randf_range(-TERRAIN * 0.48, TERRAIN * 0.48)
		var z := rng.randf_range(-TERRAIN * 0.48, TERRAIN * 0.48)
		var size := Vector3(rng.randf_range(0.5, 3), rng.randf_range(0.5, 4), rng.randf_range(0.5, 3))
		_static_box(Vector3(x, _terrain_height(x, z) + size.y * 0.4, z), size, Color(0.55, 0.5, 0.45))
	# Rigs: a grid of 30 rigs over the terrain, each with 50 rays fanning out and down and 5 shape casts.
	var rigs := _count(30)
	var side := int(ceil(sqrt(float(rigs))))
	var sphere := SphereShape3D.new()
	sphere.radius = 0.5
	for r in rigs:
		var rig := Node3D.new()
		var x := (r % side - (side - 1) * 0.5) * (TERRAIN * 0.8 / side)
		var z := (r / side - (side - 1) * 0.5) * (TERRAIN * 0.8 / side)
		rig.position = Vector3(x, _terrain_height(x, z) + 6.0, z)
		add_child(rig)
		for k in 50:
			var rc := RayCast3D.new()
			var a := float(k) * 2.399963
			var d := Vector3(cos(a), -0.6 - 0.4 * float(k % 7) / 7.0, sin(a)).normalized()
			rc.target_position = d * 40.0
			rc.collide_with_areas = false
			rig.add_child(rc)
		for k in 5:
			var sc := ShapeCast3D.new()
			sc.shape = sphere
			var a := float(k) * 1.2566
			sc.target_position = Vector3(cos(a), -0.5, sin(a)).normalized() * 25.0
			sc.max_results = 8
			rig.add_child(sc)
		_rigs.append(rig)
		bodies.append(rig) # the rigs' transforms are inputs; the queries' results are hashed through query_digest()


func _build_areas() -> void:
	# A closed box of 30 x 12 x 30 m: floor, walls and a ceiling.
	var w := 30.0
	var h := 12.0
	_static_box(Vector3(0, -0.5, 0), Vector3(w + 2, 1, w + 2), Color(0.45, 0.45, 0.48))
	_static_box(Vector3(0, h + 0.5, 0), Vector3(w + 2, 1, w + 2), Color(0.45, 0.45, 0.48)).visible = false
	for k in 4:
		var horiz := k < 2
		var sgn := 1.0 if k % 2 == 0 else -1.0
		var pos := Vector3(sgn * (w * 0.5 + 0.5), h * 0.5, 0) if horiz else Vector3(0, h * 0.5, sgn * (w * 0.5 + 0.5))
		var size := Vector3(1, h, w + 2) if horiz else Vector3(w + 2, h, 1)
		_static_box(pos, size, Color(0.5, 0.5, 0.55)).visible = k != 2 # the wall facing the camera hidden
	_body_kinds()
	var bouncy := PhysicsMaterial.new()
	bouncy.bounce = 0.9
	bouncy.friction = 0.1
	var rng := RandomNumberGenerator.new()
	rng.seed = 4242
	for i in _count(1000):
		var b := _rigid(1, Transform3D(Basis(), Vector3(rng.randf_range(-w * 0.45, w * 0.45), rng.randf_range(1, h - 1), rng.randf_range(-w * 0.45, w * 0.45))), PALETTE[i % 5])
		b.physics_material_override = bouncy
		b.linear_velocity = Vector3(rng.randf_range(-6, 6), rng.randf_range(-3, 3), rng.randf_range(-6, 6))
		b.can_sleep = false
	var trig := SphereShape3D.new()
	trig.radius = 1.6
	for i in _count(300):
		var ar := Area3D.new()
		var cs := CollisionShape3D.new()
		cs.shape = trig
		ar.add_child(cs)
		ar.position = Vector3(rng.randf_range(-w * 0.45, w * 0.45), rng.randf_range(1, h - 1), rng.randf_range(-w * 0.45, w * 0.45))
		ar.body_entered.connect(_on_area_entered)
		ar.body_exited.connect(_on_area_exited)
		add_child(ar)


func _on_area_entered(_b: Node) -> void:
	_areas_entered += 1


func _on_area_exited(_b: Node) -> void:
	_areas_exited += 1


func _physics_process(_delta: float) -> void:
	_tick += 1
	match case_name:
		"chars":
			for i in _chars.size():
				var cb := _chars[i]
				# A new heading every two seconds (120 ticks), staggered by character.
				var seg := (_tick + i * 17) / 120
				var a := float((seg * 7919 + i * 104729) % 3600) * TAU / 3600.0
				var speed := 3.0 + float(i % 4)
				var v := Vector3(cos(a) * speed, cb.velocity.y, sin(a) * speed)
				if cb.is_on_floor():
					v.y = 0.0
				else:
					v.y -= 9.8 / 60.0
				cb.velocity = v
				cb.move_and_slide()
		"rays":
			for r in _rigs.size():
				_rigs[r].rotation = Vector3(0, float(_tick) * 0.02 + float(r), 0)
		"stack":
			if _tick == 90:
				for b in _balls:
					b.freeze = false
					b.linear_velocity = Vector3(14, 3, 0)


## The case's counters for the results (areas' signals, the rays' hits: what the queries returned).
func physics_counters() -> Dictionary:
	var d := {"tick": _tick}
	if case_name == "areas":
		d["area_entered"] = _areas_entered
		d["area_exited"] = _areas_exited
	if case_name == "rays":
		var hits := 0
		var shape_hits := 0
		for rig in _rigs:
			for c in rig.get_children():
				if c is RayCast3D and c.is_colliding():
					hits += 1
				elif c is ShapeCast3D:
					shape_hits += c.get_collision_count()
		d["ray_hits"] = hits
		d["shape_hits"] = shape_hits
	return d


## Bytes of every moving body's transform (and for the rays case the queries' hit points), for the exactness hash.
func physics_state_bytes() -> PackedByteArray:
	var f := PackedFloat32Array()
	for b in bodies:
		var t: Transform3D = b.global_transform
		f.append_array([t.basis.x.x, t.basis.x.y, t.basis.x.z, t.basis.y.x, t.basis.y.y, t.basis.y.z, t.basis.z.x, t.basis.z.y, t.basis.z.z, t.origin.x, t.origin.y, t.origin.z])
	if case_name == "rays":
		for rig in _rigs:
			for c in rig.get_children():
				if c is RayCast3D:
					var p: Vector3 = c.get_collision_point() if c.is_colliding() else Vector3(INF, INF, INF)
					f.append_array([p.x, p.y, p.z])
				elif c is ShapeCast3D:
					f.append(float(c.get_collision_count()))
					f.append(c.get_closest_collision_safe_fraction())
	return f.to_byte_array()
