extends SceneTree
## FellowEngine milestone 74 check (decision 0182): FellowDestructible3D.
##
##   godot --path benchmarks/project --headless --fixed-fps 60 -s res://bench/destruction_check.gd -- --out=<dir>
##   godot --path benchmarks/project --fixed-fps 60 --resolution 1280x720 -s res://bench/destruction_check.gd
##       -- --out=<dir> --pictures=1 [--frames=1] [--grid=1 --shot=640x540]
##   godot ... -s res://bench/destruction_check.gd -- --out=<dir> --many=64 [--pieces=12] [--prefracture=1] [--seconds=4]
##
## Lanes, each a destructible in a situation: a crate told to break at a corner, a pillar struck by a heavy ball, a
## pillar touched by a light one (it stays), a crate dropped on the ground, a heavy crate resting under another (it
## stays), a pane of glass shot through its middle, a wall with a window, a ring (a torus: cuts with holes), a crate
## damaged three times, six prefractured crates sharing one pattern, a crate whose debris lives one second, a sphere
## of 4 000 triangles in 32 pieces. For each piece of a lane: its mesh's volume (the pieces' sum against the source's),
## the same volume about another origin (a closed piece: the same), its lowest point against the ground; how many frames
## the break took. After the lanes: the debris budget cut to 40, the debris left. The cost: the fracture on a worker,
## the meshes and shapes, the nodes (--many=N crates broken at once: the frame times around it). One JSON line:
## DESTRUCTION_CHECK {...} (DESTRUCTION_MANY {...}); exit code 1 when a lane failed.

const CHECK_FRAME := 230
const BUDGET_FRAME := 240
const END_FRAME := 330
const BUDGET := 40

var args := {}
var out_dir := ""
var result := {}
var root3d: Node3D
var lanes: Array = []
var frame := 0
var pictures := false
var save_frames := false
var grid := false
var shot_size := Vector2i(480, 320)
var many := 0
var many_pieces := 12
var many_prefracture := false
var many_seconds := 4.0
var many_frames: PackedFloat64Array = []
var many_last := 0
var many_broken_frame := -1
var many_break_usec := 0
var many_broken_ms := -1.0
var many_nodes: Array = []
var done := false
var shots := {}
var viewports := {}
var strip_lanes := ["crate", "pillar", "dropped", "glass", "wall", "ring"]
var strip_times := [0.45, 0.6, 1.0, 3.5]
var box_mesh: BoxMesh
var wood: StandardMaterial3D
var inside: StandardMaterial3D
var lane_checks := {}
var budget_report := {}


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://destruction_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	pictures = _arg("pictures", "0") == "1"
	save_frames = _arg("frames", "0") == "1"
	grid = _arg("grid", "0") == "1"
	many = int(_arg("many", "0"))
	many_pieces = int(_arg("pieces", "12"))
	many_prefracture = _arg("prefracture", "0") == "1"
	many_seconds = float(_arg("seconds", "4"))
	var ss: PackedStringArray = _arg("shot", "480x320").split("x")
	shot_size = Vector2i(int(ss[0]), int(ss[1]))
	_build.call_deferred()


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.85
	return m


# A body (static or rigid) with a mesh and a shape, and a FellowDestructible3D under it.
func _destructible(body: CollisionObject3D, mesh: Mesh, shape: Shape3D, xf: Transform3D, mat: Material) -> FellowDestructible3D:
	body.transform = xf
	var mi := MeshInstance3D.new()
	mi.name = "Mesh"
	mi.mesh = mesh
	mi.material_override = null
	if mat:
		mi.set_surface_override_material(0, mat)
	body.add_child(mi)
	var cs := CollisionShape3D.new()
	cs.shape = shape
	body.add_child(cs)
	var d := FellowDestructible3D.new()
	d.name = "Destructible"
	d.interior_material = inside
	body.add_child(d)
	root3d.add_child(body)
	return d


func _lane(name: String, d: FellowDestructible3D, x: float) -> Dictionary:
	var lane := {"name": name, "d": d, "x": x, "pieces": [], "broken_frame": -1, "asked_frame": -1, "damaged": 0,
		"source_volume": 0.0, "max_speed": 0.0}
	lanes.append(lane)
	d.broken.connect(func(pieces: Array) -> void:
		lane["pieces"] = pieces
		lane["broken_frame"] = frame
		lane["broken_usec"] = Time.get_ticks_usec()
		lane["stats"] = d.get_stats()
	)
	d.damaged.connect(func(_amount: float, _health: float) -> void:
		lane["damaged"] += 1
	)
	return lane


# The signed volume of a mesh's triangles about an origin (Godot's front faces clockwise: outward normals give a
# negative sum, so it is negated), the area, and how open it is: the sum of its faces' area vectors over the area (a
# closed surface's cancel).
func _mesh_volume(mesh: Mesh, origin: Vector3) -> Array:
	var vol := 0.0
	var area := 0.0
	var asum := Vector3()
	for s in mesh.get_surface_count():
		var arrays := mesh.surface_get_arrays(s)
		var v: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX] if arrays[Mesh.ARRAY_INDEX] != null else PackedInt32Array()
		var n := idx.size() if idx.size() > 0 else v.size()
		for i in range(0, n - 2, 3):
			var a := v[idx[i] if idx.size() > 0 else i] - origin
			var b := v[idx[i + 1] if idx.size() > 0 else i + 1] - origin
			var c := v[idx[i + 2] if idx.size() > 0 else i + 2] - origin
			vol -= a.dot(b.cross(c)) / 6.0
			var cr := (b - a).cross(c - a) * 0.5
			asum += cr
			area += cr.length()
	return [vol, area, asum.length() / maxf(area, 1e-12)]


func _build() -> void:
	root3d = Node3D.new()
	get_root().add_child(root3d)
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.55, 0.65, 0.78)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.6, 0.62, 0.66)
	e.ambient_light_energy = 0.6
	env.environment = e
	root3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 35, 0)
	sun.shadow_enabled = true
	root3d.add_child(sun)
	# The ground: its top at y = 0.
	var ground := StaticBody3D.new()
	var gs := CollisionShape3D.new()
	var gb := BoxShape3D.new()
	gb.size = Vector3(400, 1, 60)
	gs.shape = gb
	ground.add_child(gs)
	var gm := MeshInstance3D.new()
	var gmesh := BoxMesh.new()
	gmesh.size = gb.size
	gm.mesh = gmesh
	gm.material_override = _material(Color(0.42, 0.44, 0.4))
	ground.add_child(gm)
	ground.position = Vector3(100, -0.5, 0)
	root3d.add_child(ground)

	wood = _material(Color(0.62, 0.45, 0.28))
	inside = _material(Color(0.85, 0.72, 0.5))
	box_mesh = BoxMesh.new()
	box_mesh.size = Vector3.ONE
	var box_shape := BoxShape3D.new()
	box_shape.size = Vector3.ONE

	if many > 0:
		_build_many(box_shape)
		physics_frame.connect(_on_frame)
		return

	var x := 0.0
	# Told to break, at a corner.
	var crate := _destructible(StaticBody3D.new(), box_mesh, box_shape, Transform3D(Basis(), Vector3(x, 0.5, 0)), wood)
	crate.piece_count = 12
	crate.break_on_impact = false
	_lane("crate", crate, x)

	# A pillar struck by a 20 kg ball at 10 m/s (200 N·s against 40), and one touched by a 2 kg ball at 3 m/s.
	for k in 2:
		x = 8.0 if k == 0 else 16.0
		var pm := BoxMesh.new()
		pm.size = Vector3(0.5, 3.0, 0.5)
		var ps := BoxShape3D.new()
		ps.size = pm.size
		var pillar := _destructible(StaticBody3D.new(), pm, ps, Transform3D(Basis(), Vector3(x, 1.5, 0)), _material(Color(0.7, 0.7, 0.68)))
		pillar.piece_count = 16
		pillar.impact_focus = 0.6
		pillar.break_impulse = 40.0
		var lane := _lane("pillar" if k == 0 else "pillar_soft", pillar, x)
		var ball := RigidBody3D.new()
		ball.mass = 20.0 if k == 0 else 2.0
		var bcs := CollisionShape3D.new()
		var sph := SphereShape3D.new()
		sph.radius = 0.25
		bcs.shape = sph
		ball.add_child(bcs)
		var bmi := MeshInstance3D.new()
		var bm := SphereMesh.new()
		bm.radius = 0.25
		bm.height = 0.5
		bmi.mesh = bm
		bmi.material_override = _material(Color(0.2, 0.2, 0.22))
		ball.add_child(bmi)
		ball.position = Vector3(x, 1.6, 3.0)
		ball.gravity_scale = 0.0
		root3d.add_child(ball)
		lane["ball"] = ball
		lane["ball_speed"] = 10.0 if k == 0 else 3.0

	# A 40 kg crate dropped from 3.6 m (8.4 m/s, about 340 N·s against 100).
	x = 24.0
	var drop_shape := BoxShape3D.new()
	drop_shape.size = Vector3(0.8, 0.8, 0.8)
	var drop_mesh := BoxMesh.new()
	drop_mesh.size = drop_shape.size
	var rb := RigidBody3D.new()
	rb.mass = 40.0
	var dropped := _destructible(rb, drop_mesh, drop_shape, Transform3D(Basis(Vector3(1, 0.3, 0.2).normalized(), 0.4), Vector3(x, 4.0, 0)), wood)
	dropped.break_impulse = 100.0
	dropped.piece_count = 12
	_lane("dropped", dropped, x)

	# A 600 kg crate on the ground under a 900 kg one, its break impulse 40 N·s (its weight alone is 98 N·s a step).
	x = 32.0
	var heavy := RigidBody3D.new()
	heavy.mass = 600.0
	var resting := _destructible(heavy, drop_mesh, drop_shape, Transform3D(Basis(), Vector3(x, 0.4, 0)), wood)
	resting.break_impulse = 40.0
	_lane("resting", resting, x)
	var top := RigidBody3D.new()
	top.mass = 900.0
	var tcs := CollisionShape3D.new()
	tcs.shape = drop_shape
	top.add_child(tcs)
	var tmi := MeshInstance3D.new()
	tmi.mesh = drop_mesh
	tmi.material_override = _material(Color(0.4, 0.3, 0.2))
	top.add_child(tmi)
	top.position = Vector3(x, 1.2, 0)
	root3d.add_child(top)

	# A pane of glass, shot through its middle: thirty pieces, most around the hole.
	x = 40.0
	var gl := BoxMesh.new()
	gl.size = Vector3(1.6, 1.2, 0.02)
	var gls := BoxShape3D.new()
	gls.size = gl.size
	var glass_mat := _material(Color(0.7, 0.85, 0.9, 0.5))
	glass_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	glass_mat.roughness = 0.05
	var glass := _destructible(StaticBody3D.new(), gl, gls, Transform3D(Basis(), Vector3(x, 1.4, 0)), glass_mat)
	glass.piece_count = 30
	glass.impact_focus = 0.8
	glass.density = 2500.0
	glass.interior_material = glass_mat
	glass.break_on_impact = false
	_lane("glass", glass, x)

	# A wall with a window (CSG, baked): cuts through it parallel to its face are loops with a hole.
	x = 48.0
	if ClassDB.class_exists("CSGBox3D"):
		var csg: Node3D = ClassDB.instantiate("CSGBox3D")
		csg.set("size", Vector3(3.0, 2.5, 0.25))
		var hole: Node3D = ClassDB.instantiate("CSGBox3D")
		hole.set("size", Vector3(1.0, 1.0, 0.6))
		hole.set("operation", 2) # subtraction
		hole.position = Vector3(0.2, 0.15, 0)
		csg.add_child(hole)
		root3d.add_child(csg)
		csg.call("_update_shape")
		var wall_mesh: Mesh = csg.call("bake_static_mesh")
		var wall_shape: Shape3D = csg.call("bake_collision_shape")
		csg.queue_free()
		if wall_mesh and wall_shape:
			var wall := _destructible(StaticBody3D.new(), wall_mesh, wall_shape, Transform3D(Basis(), Vector3(x, 1.25, 0)), _material(Color(0.75, 0.68, 0.6)))
			wall.piece_count = 20
			wall.break_on_impact = false
			_lane("wall", wall, x)

	# A ring lying flat (a torus).
	x = 56.0
	var torus := TorusMesh.new()
	torus.inner_radius = 0.45
	torus.outer_radius = 0.9
	torus.rings = 48
	torus.ring_segments = 16
	var ring := _destructible(StaticBody3D.new(), torus, torus.create_trimesh_shape(), Transform3D(Basis(), Vector3(x, 0.25, 0)), _material(Color(0.6, 0.55, 0.7)))
	ring.piece_count = 14
	ring.break_on_impact = false
	_lane("ring", ring, x)

	# Damaged three times (health 50, 20 a blow).
	x = 64.0
	var damaged := _destructible(StaticBody3D.new(), box_mesh, box_shape, Transform3D(Basis(), Vector3(x, 0.5, 0)), wood)
	damaged.health = 50.0
	damaged.break_on_impact = false
	_lane("damage", damaged, x)

	# Six crates of one mesh, prefractured: one pattern computed, shared.
	FellowDestructible3D.clear_cache()
	var fractures_before: int = FellowDestructible3D.get_global_stats().fractures
	for k in 6:
		x = 72.0 + k * 1.5
		var pf := _destructible(StaticBody3D.new(), box_mesh, box_shape, Transform3D(Basis(), Vector3(x, 0.5, 0)), wood)
		pf.prefracture = true
		pf.seed = 7
		pf.break_on_impact = false
		_lane("prefractured_%d" % k, pf, x)
	result["prefracture_patterns"] = FellowDestructible3D.get_global_stats().fractures - fractures_before

	# Debris that lives one second.
	x = 84.0
	var short := _destructible(StaticBody3D.new(), box_mesh, box_shape, Transform3D(Basis(), Vector3(x, 0.5, 0)), wood)
	short.debris_lifetime = 1.0
	short.break_on_impact = false
	_lane("lifetime", short, x)

	# A sphere of 4 000 triangles in 32 pieces.
	x = 92.0
	var sm := SphereMesh.new()
	sm.radius = 0.8
	sm.height = 1.6
	sm.radial_segments = 64
	sm.rings = 32
	var ss := SphereShape3D.new()
	ss.radius = 0.8
	var sphere := _destructible(StaticBody3D.new(), sm, ss, Transform3D(Basis(), Vector3(x, 0.8, 0)), _material(Color(0.5, 0.6, 0.5)))
	sphere.piece_count = 32
	sphere.break_on_impact = false
	_lane("sphere_hd", sphere, x)

	for lane in lanes:
		var mi: MeshInstance3D = lane.d.get_parent().get_node("Mesh")
		lane["source_volume"] = _mesh_volume(mi.mesh, Vector3())[0] * absf(mi.global_basis.determinant())

	var cam := Camera3D.new()
	root3d.add_child(cam)
	cam.look_at_from_position(Vector3(46, 12, 30), Vector3(46, 1, 0))
	if pictures:
		_make_viewports()
	physics_frame.connect(_on_frame)


func _build_many(box_shape: BoxShape3D) -> void:
	var cols := int(ceil(sqrt(float(many))))
	FellowDestructible3D.clear_cache()
	var t0 := Time.get_ticks_usec()
	for i in many:
		var p := Vector3(float(i % cols) * 2.5, 0.5, float(i / cols) * 2.5)
		var d := _destructible(StaticBody3D.new(), box_mesh, box_shape, Transform3D(Basis(), p), wood)
		d.piece_count = many_pieces
		d.break_on_impact = false
		d.prefracture = many_prefracture
		d.seed = i if not many_prefracture else 0
		many_nodes.append(d)
	result["setup_ms"] = snappedf(float(Time.get_ticks_usec() - t0) / 1000.0, 0.01)
	var cam := Camera3D.new()
	root3d.add_child(cam)
	cam.look_at_from_position(Vector3(-6, 10, -6), Vector3(cols * 1.25, 0, cols * 1.25))


func _make_viewports() -> void:
	var grid_box: GridContainer
	if grid:
		grid_box = GridContainer.new()
		grid_box.columns = 3
		grid_box.add_theme_constant_override("h_separation", 0)
		grid_box.add_theme_constant_override("v_separation", 0)
		get_root().add_child(grid_box)
	for name in strip_lanes:
		var lane: Dictionary = _find(name)
		if lane.is_empty():
			continue
		var vp := SubViewport.new()
		vp.size = shot_size
		vp.world_3d = get_root().world_3d
		vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		var cam := Camera3D.new()
		cam.fov = 50
		vp.add_child(cam)
		var x: float = lane.x
		match name:
			"crate":
				cam.look_at_from_position(Vector3(x + 2.5, 2.2, 3.5), Vector3(x, 0.4, 0))
			"pillar":
				cam.look_at_from_position(Vector3(x + 5.0, 2.4, 1.5), Vector3(x, 1.2, 0))
			"dropped":
				cam.look_at_from_position(Vector3(x + 2.5, 2.4, 4.0), Vector3(x, 0.8, 0))
			"glass":
				cam.look_at_from_position(Vector3(x + 1.5, 1.8, 3.2), Vector3(x, 1.0, 0))
			"wall":
				cam.look_at_from_position(Vector3(x + 2.0, 2.2, 5.0), Vector3(x, 1.0, 0))
			"ring":
				cam.look_at_from_position(Vector3(x + 1.8, 2.4, 2.8), Vector3(x, 0.2, 0))
		if grid:
			var cell := Control.new()
			cell.custom_minimum_size = Vector2(shot_size)
			var holder := SubViewportContainer.new()
			holder.size = Vector2(shot_size)
			holder.add_child(vp)
			cell.add_child(holder)
			var label := Label.new()
			label.text = String(name)
			label.add_theme_font_size_override("font_size", 20)
			label.add_theme_color_override("font_outline_color", Color.BLACK)
			label.add_theme_constant_override("outline_size", 6)
			label.position = Vector2(10, 6)
			cell.add_child(label)
			grid_box.add_child(cell)
		else:
			get_root().add_child(vp)
		viewports[name] = vp


func _find(name: String) -> Dictionary:
	for lane in lanes:
		if lane.name == name:
			return lane
	return {}


func _on_frame() -> void:
	if done:
		return
	frame += 1
	if many > 0:
		_many_tick()
		return
	if frame == 27:
		for lane in lanes:
			if lane.has("ball"):
				lane.ball.linear_velocity = Vector3(0, 0, -lane.ball_speed)
	if frame == 30:
		for lane in lanes:
			var n: String = lane.name
			var x: float = lane.x
			if n == "crate":
				lane.d.break_apart(Vector3(x + 0.45, 0.95, 0.45))
			elif n == "glass":
				lane.d.break_apart(Vector3(x, 1.4, 0), Vector3(0, 0, -40))
			elif n == "wall":
				lane.d.break_apart(Vector3(x - 0.9, 1.6, 0.12), Vector3(0, 0, -200))
			elif n == "ring":
				lane.d.break_apart(Vector3(x + 0.7, 0.4, 0))
			elif n == "lifetime" or n == "sphere_hd":
				lane.d.break_apart(Vector3(x, 1.0, 0.3))
			elif n.begins_with("prefractured"):
				var before: int = lane.pieces.size()
				lane.d.break_apart(Vector3(x, 0.9, 0.3))
				lane["same_frame"] = lane.pieces.size() > before
			else:
				continue
			lane["asked_frame"] = frame
			lane["asked_usec"] = Time.get_ticks_usec()
	if frame == 30 or frame == 40 or frame == 50:
		var dl: Dictionary = _find("damage")
		dl.d.damage(20.0, Vector3(dl.x + 0.5, 0.5, 0), Vector3(-10, 0, 0))
		if frame == 50:
			dl["asked_frame"] = 50
			dl["asked_usec"] = Time.get_ticks_usec()
	for lane in lanes:
		for p in lane.pieces:
			if is_instance_valid(p) and lane.broken_frame >= 0 and frame <= lane.broken_frame + 2:
				lane["max_speed"] = maxf(lane.max_speed, (p as RigidBody3D).linear_velocity.length())
	if frame == CHECK_FRAME:
		for lane in lanes:
			lane_checks[lane.name] = _measure(lane)
	if frame == BUDGET_FRAME:
		budget_report["debris_before"] = FellowDestructible3D.get_debris_count()
		budget_report["faded_before"] = FellowDestructible3D.get_global_stats().faded
		FellowDestructible3D.set_max_debris(BUDGET)
	if pictures:
		_shoot()
	if frame >= END_FRAME:
		done = true
		_finish.call_deferred()


func _measure(lane: Dictionary) -> Dictionary:
	var e := {}
	var vol := 0.0
	var worst_open := 0.0
	var lowest := INF
	var finite := true
	var mass := 0.0
	var count := 0
	for p in lane.pieces:
		if not is_instance_valid(p):
			continue
		count += 1
		var body := p as RigidBody3D
		mass += body.mass
		for c in body.get_children():
			if c is MeshInstance3D:
				var mi := c as MeshInstance3D
				var s := absf(mi.global_basis.determinant())
				var va: Array = _mesh_volume(mi.mesh, Vector3())
				vol += float(va[0]) * s
				worst_open = maxf(worst_open, float(va[2]))
			elif c is CollisionShape3D:
				var cs := c as CollisionShape3D
				var xf := cs.global_transform
				for q in (cs.shape as ConvexPolygonShape3D).points:
					var w := xf * q
					lowest = minf(lowest, w.y)
					finite = finite and is_finite(w.x) and is_finite(w.y) and is_finite(w.z)
	e["pieces"] = count
	e["volume"] = snappedf(vol, 0.0001)
	e["source_volume"] = snappedf(lane.source_volume, 0.0001)
	e["volume_error"] = snappedf(absf(vol - lane.source_volume) / maxf(lane.source_volume, 1e-9), 0.00001) if count > 0 else 1.0
	e["open"] = snappedf(worst_open, 0.00001)
	e["below_ground_cm"] = snappedf(-lowest * 100.0, 0.01) if count > 0 else 0.0
	e["mass"] = snappedf(mass, 0.01)
	e["finite"] = finite
	return e


func _shoot() -> void:
	for name in viewports:
		var vp: SubViewport = viewports[name]
		if save_frames:
			DirAccess.make_dir_recursive_absolute(out_dir.path_join("frames_" + name))
			vp.get_texture().get_image().save_png(out_dir.path_join("frames_%s/%04d.png" % [name, frame]))
		for k in strip_times.size():
			if frame == int(strip_times[k] * 60.0):
				if not shots.has(name):
					shots[name] = {}
				shots[name][k] = vp.get_texture().get_image()


func _finish() -> void:
	var failures := []
	var out := {}
	for lane in lanes:
		var d: FellowDestructible3D = lane.d if is_instance_valid(lane.d) else null
		var e: Dictionary = lane_checks.get(lane.name, {}).duplicate()
		e["broken"] = lane.broken_frame >= 0
		e["frames_to_break"] = lane.broken_frame - lane.asked_frame if lane.broken_frame >= 0 and lane.asked_frame >= 0 else -1
		e["max_speed"] = snappedf(lane.max_speed, 0.01)
		# From the call to the pieces (a fracture on a worker: a few frames at 60 fps; prefractured: at once).
		e["break_ms"] = snappedf(float(lane.broken_usec - lane.asked_usec) / 1000.0, 0.01) if lane.has("broken_usec") and lane.has("asked_usec") else -1.0
		e["stats"] = d.get_stats() if d else lane.get("stats", {})
		var st: Dictionary = e.stats
		var n: String = lane.name
		var ok: bool = e.get("finite", true)
		var whole: bool = e.get("volume_error", 1.0) < 0.002 and e.get("open", 1.0) < 0.0005
		# Resting on the ground: a piece sinks a little (the physics engine's slop: Jolt 2 cm, more under a heavy pile), none falls through.
		var rests: bool = e.get("below_ground_cm", 99.0) < 8.0
		match n:
			"crate":
				ok = ok and e.broken and e.pieces == 12 and whole and rests and absf(e.mass - 600.0) < 3.0
			"pillar":
				# The ball through where the pillar stood.
				e["ball_z"] = snappedf(lane.ball.position.z, 0.01)
				ok = ok and e.broken and e.pieces == 16 and whole and rests
			"pillar_soft":
				ok = ok and not e.broken and st.get("last_impact", 0.0) > 3.0 and st.get("last_impact", 0.0) < 40.0
				e["last_impact"] = st.get("last_impact", 0.0)
			"dropped":
				ok = ok and e.broken and e.pieces >= 10 and whole and rests and lane.max_speed < 9.0
			"resting":
				ok = ok and not e.broken
				e["last_impact"] = st.get("last_impact", 0.0)
			"glass":
				ok = ok and e.broken and e.pieces >= 26 and whole and rests
			"wall":
				ok = ok and e.broken and e.pieces >= 16 and whole and rests
			"ring":
				ok = ok and e.broken and e.pieces >= 10 and whole and rests
			"damage":
				ok = ok and e.broken and lane.damaged == 3 and lane.broken_frame >= 50
			"lifetime":
				# Its pieces gone by the end (asleep past a second, or two seconds old: shrunk and freed).
				var left := 0
				for p in lane.pieces:
					left += 1 if is_instance_valid(p) else 0
				e["left_at_end"] = left
				ok = ok and e.broken and left == 0
			"sphere_hd":
				ok = ok and e.broken and e.pieces >= 28 and whole and rests
			_:
				if n.begins_with("prefractured"):
					ok = ok and e.broken and lane.get("same_frame", false) and e.pieces == 12 and whole and rests and st.get("prefractured", false)
		e["ok"] = ok
		if not ok:
			failures.append(n)
		out[n] = e
	if result.get("prefracture_patterns", 0) != 1:
		failures.append("prefracture_patterns")
	budget_report["debris_end"] = FellowDestructible3D.get_debris_count()
	budget_report["faded_end"] = FellowDestructible3D.get_global_stats().faded
	if budget_report.debris_end > BUDGET or budget_report.faded_end <= budget_report.faded_before:
		failures.append("budget")
	FellowDestructible3D.set_max_debris(256)
	result["lanes"] = out
	result["budget"] = budget_report
	result["failures"] = failures
	result["global"] = FellowDestructible3D.get_global_stats()
	result["physics_engine"] = ProjectSettings.get_setting("physics/3d/physics_engine")
	if pictures:
		_save_strip()
	print("DESTRUCTION_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("destruction_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)


func _save_strip() -> void:
	var w := shot_size.x
	var h := shot_size.y
	var strip := Image.create(w * strip_times.size(), h * strip_lanes.size(), false, Image.FORMAT_RGBA8)
	for r in strip_lanes.size():
		for k in strip_times.size():
			var img: Image = shots.get(strip_lanes[r], {}).get(k)
			if img == null:
				continue
			img.convert(Image.FORMAT_RGBA8)
			strip.blit_rect(img, Rect2i(0, 0, w, h), Vector2i(k * w, r * h))
	strip.save_png(out_dir.path_join("destruction_strip.png"))


func _many_tick() -> void:
	var now := Time.get_ticks_usec()
	if frame == 60:
		FellowDestructible3D.reset_global_stats()
		many_break_usec = now
		for d in many_nodes:
			var dd: FellowDestructible3D = d
			dd.break_apart(dd.get_parent().global_position + Vector3(0.4, 0.4, 0.4))
		many_last = now
		return
	if frame < 60:
		return
	many_frames.append(float(now - many_last) / 1000.0)
	many_last = now
	if many_broken_frame < 0:
		var all := true
		for d in many_nodes:
			if is_instance_valid(d) and not (d as FellowDestructible3D).get_stats().broken:
				all = false
				break
		if all:
			many_broken_frame = frame - 60
			many_broken_ms = float(now - many_break_usec) / 1000.0
	# Until every crate has broken (a headless frame can be far shorter than the workers' fractures), then the
	# seconds asked for with the debris; at most two minutes.
	var waited := float(now - many_break_usec) / 1e6
	if (many_broken_frame >= 0 and frame >= 60 + many_broken_frame + int(many_seconds * 60.0)) or waited > 120.0:
		done = true
		var st := FellowDestructible3D.get_global_stats()
		var head := many_frames.slice(0, 10)
		var breaking := many_frames.slice(0, maxi(1, many_broken_frame + 1))
		var tail := many_frames.slice(maxi(0, many_broken_frame) + 30)
		var r := {"crates": many, "pieces_each": many_pieces, "prefracture": many_prefracture, "stats": st,
			"setup_ms": result.get("setup_ms", 0.0), "frames_to_break_all": many_broken_frame, "ms_to_break_all": snappedf(many_broken_ms, 0.01),
			"frame_ms_max_breaking": snappedf(_max(breaking), 0.01),
			"fracture_ms_each": snappedf(float(st.fracture_usec) / maxf(1.0, float(st.fractures)) / 1000.0, 0.001),
			"build_ms_each": snappedf(float(st.build_usec) / maxf(1.0, float(st.fractures)) / 1000.0, 0.001),
			"spawn_ms_each": snappedf(float(st.spawn_usec) / maxf(1.0, float(st.breaks)) / 1000.0, 0.001),
			"first_frames_ms": head, "frame_ms_max_first10": snappedf(_max(head), 0.01),
			"frame_ms_mean_after": snappedf(_mean(tail), 0.01), "frame_ms_max_after": snappedf(_max(tail), 0.01),
			"debris": FellowDestructible3D.get_debris_count(), "threads": OS.get_processor_count(),
			"physics_engine": ProjectSettings.get_setting("physics/3d/physics_engine")}
		print("DESTRUCTION_MANY " + JSON.stringify(r))
		var f := FileAccess.open(out_dir.path_join("destruction_many.json"), FileAccess.WRITE)
		f.store_string(JSON.stringify(r, "  "))
		f.close()
		quit(0)


func _mean(a: PackedFloat64Array) -> float:
	var s := 0.0
	for v in a:
		s += v
	return s / maxf(1.0, float(a.size()))


func _max(a: PackedFloat64Array) -> float:
	var m := 0.0
	for v in a:
		m = maxf(m, v)
	return m
