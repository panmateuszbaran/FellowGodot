extends SceneTree
## FellowEngine milestone 80 check (decision 0188): FellowDestructible3D's structural mode — a static object broken
## where it is struck, the rest standing while it is supported.
##
##   godot --path benchmarks/project --headless --fixed-fps 60 -s res://bench/structure_check.gd -- --out=<dir>
##   godot --path benchmarks/project --fixed-fps 60 --resolution 1280x720 -s res://bench/structure_check.gd
##       -- --out=<dir> --pictures=1
##
## Lanes (each object prefractured, static, on the ground): a wall 4 x 3 x 0.25 m in 40 pieces struck by a 75 kg ball
## at 16 m/s (a static object sees blows up to 18 m/s) (a hole, the ball perhaps striking its edge again; the rest stands; the debris does not strike it), a pillar 0.5 x 3 x 0.5 m in 14 pieces
## broken at its foot (the rest loses its support and falls in one piece or a few), a wall struck four times across
## its middle (each blow takes more; the upper half, cut off, falls), the same wall without the structural mode (all of it debris).
## One JSON line: STRUCTURE_CHECK {...}; exit code 1 when a lane did otherwise than expected.

var args := {}
var out_dir := ""
var result := {}
var root3d: Node3D
var lanes := {}
var frame := 0
var pictures := false
var done := false
var cam: Camera3D
var shots := {}


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://structure_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	pictures = _arg("pictures", "0") == "1"
	_build.call_deferred()


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.85
	return m


func _object(name: String, pos: Vector3, size: Vector3, pieces: int, structural: bool, color: Color) -> FellowDestructible3D:
	var body := StaticBody3D.new()
	body.name = name
	body.position = pos
	var mi := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	mi.mesh = mesh
	mi.material_override = null
	mi.set_surface_override_material(0, _material(color))
	body.add_child(mi)
	var cs := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = size
	cs.shape = shape
	body.add_child(cs)
	var d := FellowDestructible3D.new()
	d.piece_count = pieces
	d.seed = 11
	d.prefracture = true
	d.structural = structural
	d.density = 2200.0
	d.break_impulse = 150.0
	d.debris_lifetime = 0.0
	d.interior_material = _material(color.lightened(0.3))
	body.add_child(d)
	root3d.add_child(body)
	return d


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
	sun.rotation_degrees = Vector3(-45, 30, 0)
	sun.shadow_enabled = true
	root3d.add_child(sun)
	var ground := StaticBody3D.new()
	var gcs := CollisionShape3D.new()
	gcs.shape = WorldBoundaryShape3D.new()
	ground.add_child(gcs)
	var gmi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(200, 200)
	gmi.mesh = pm
	gmi.material_override = _material(Color(0.4, 0.42, 0.38))
	ground.add_child(gmi)
	root3d.add_child(ground)
	cam = Camera3D.new()
	root3d.add_child(cam)
	cam.current = true
	cam.look_at_from_position(Vector3(9, 6, 14), Vector3(9, 1.2, 0))
	lanes["wall_hole"] = {"d": _object("WallHole", Vector3(0, 1.5, 0), Vector3(4, 3, 0.25), 40, true, Color(0.7, 0.62, 0.55)), "data": {}}
	lanes["pillar_foot"] = {"d": _object("Pillar", Vector3(6, 1.5, 0), Vector3(0.5, 3, 0.5), 14, true, Color(0.62, 0.62, 0.6)), "data": {}}
	lanes["wall_cut"] = {"d": _object("WallCut", Vector3(12, 1.5, 0), Vector3(4, 3, 0.25), 40, true, Color(0.6, 0.55, 0.65)), "data": {}}
	lanes["wall_plain"] = {"d": _object("WallPlain", Vector3(26, 1.5, 0), Vector3(4, 3, 0.25), 40, false, Color(0.55, 0.6, 0.55)), "data": {}}
	for n in lanes:
		var d: FellowDestructible3D = lanes[n].d
		d.broken.connect(func(pieces: Array) -> void:
			lanes[n].data["breaks"] = lanes[n].data.get("breaks", 0) + 1
			lanes[n].data["bodies"] = lanes[n].data.get("bodies", 0) + pieces.size()
			for b in pieces:
				var meshes := 0
				for c in (b as Node).get_children():
					meshes += 1 if c is MeshInstance3D else 0
				if meshes > 1:
					lanes[n].data["chunks"] = lanes[n].data.get("chunks", 0) + 1
					lanes[n].data["chunk_pieces"] = lanes[n].data.get("chunk_pieces", 0) + meshes
					lanes[n].data["chunk_start_y"] = lanes[n].data.get("chunk_start_y", []) + [snappedf((b as Node3D).global_position.y, 0.01)]
			if not lanes[n].has("made"):
				lanes[n]["made"] = []
			lanes[n].made.append_array(pieces))
	physics_frame.connect(_on_frame)


func _ball(pos: Vector3, velocity: Vector3) -> RigidBody3D:
	var b := RigidBody3D.new()
	b.mass = 75.0
	b.position = pos
	var cs := CollisionShape3D.new()
	var sh := SphereShape3D.new()
	sh.radius = 0.25
	cs.shape = sh
	b.add_child(cs)
	var mi := MeshInstance3D.new()
	var sm := SphereMesh.new()
	sm.radius = 0.25
	sm.height = 0.5
	mi.mesh = sm
	mi.material_override = _material(Color(0.2, 0.2, 0.22))
	b.add_child(mi)
	b.continuous_cd = true
	root3d.add_child(b)
	b.linear_velocity = velocity
	return b


func _on_frame() -> void:
	if done:
		return
	frame += 1
	var t := frame / 60.0
	if frame == 30:
		# The ball at the first wall, left of its middle; the pillar broken at its foot; the plain wall struck.
		_ball(Vector3(-1.0, 1.6, 3.0), Vector3(0, 0, -16))
		lanes.pillar_foot.d.break_apart(Vector3(6.0, 0.3, 0.25), Vector3(0, 0, -400))
		lanes.wall_plain.d.break_apart(Vector3(26.0, 1.5, 0.12), Vector3(0, 0, -400))
	# The second wall struck four times across it at 1.4 m, left to right (0.8 m round each blow): its upper part cut
	# off.
	for k in 4:
		if frame == 30 + k * 30:
			var wc: FellowDestructible3D = lanes.wall_cut.d
			wc.break_radius = 0.8
			wc.break_apart(Vector3(10.2 + k * 1.2, 1.4, 0.12), Vector3(0, 0, -2500)) # a charge: the pieces blown out
			lanes.wall_cut.data["standing_%d" % k] = wc.get_standing_pieces()
	if frame == 90:
		lanes.wall_hole.data["standing_1s"] = lanes.wall_hole.d.get_standing_pieces()

	if pictures and frame in [29, 60, 150, 300]:
		shots[frame] = get_root().get_texture().get_image()
	if frame == 300:
		for n in lanes:
			var d: Variant = lanes[n].d # freed with its body when nothing of it stands
			var data: Dictionary = lanes[n].data
			data["stats"] = d.get_stats() if is_instance_valid(d) else {}
			data["standing"] = d.get_standing_pieces() if is_instance_valid(d) else 0
			# The bodies it made: pieces (one each) and chunks (several), where they came to rest.
			var chunk_y := []
			for b in lanes[n].get("made", []):
				if is_instance_valid(b) and (b as Node).name.contains("Chunk"):
					chunk_y.append(snappedf((b as Node3D).global_position.y, 0.01))
			data["chunk_rest_y"] = chunk_y
		done = true
		_finish.call_deferred()


func _finish() -> void:
	var failures := []
	var out := {}
	for n in lanes:
		var data: Dictionary = lanes[n].data
		var st: Dictionary = data.get("stats", {})
		var ok := false
		match n:
			"wall_hole":
				# A hole: some pieces knocked out, most standing, none falling later (no strike by its own debris).
				ok = st.get("standing", 0) >= 28 and st.get("standing", 0) <= 38 and data.get("standing_1s", -1) == data.get("standing", -2) and data.get("breaks", 0) <= 3
			"pillar_foot":
				# The foot broken, the rest fallen in one or a few chunks, lying on the ground.
				ok = data.get("chunks", 0) >= 1 and data.get("chunk_pieces", 0) >= 5 and data.get("chunk_rest_y", []).size() >= 1 and data.chunk_rest_y.all(func(y): return y < 1.2)
			"wall_cut":
				# Each blow takes more; once the cut is through, the upper half falls (in one part or a few).
				var fell: bool = data.get("chunk_rest_y", []).size() >= 1 and data.chunk_rest_y[0] < data.get("chunk_start_y", [0.0])[0] - 0.3
				ok = data.get("standing_0", 0) > data.get("standing_1", 99) and data.get("standing_1", 0) > data.get("standing_2", 99) and data.get("standing_2", 0) > data.get("standing_3", 99) and data.get("breaks", 0) == 4 and data.get("chunks", 0) >= 1 and data.get("chunk_pieces", 0) >= 8 and fell
			"wall_plain":
				ok = data.get("standing", 1) == 0 and data.get("bodies", 0) == 40
		data["ok"] = ok
		data.erase("made")
		if not ok:
			failures.append(n)
		out[n] = data
	result["lanes"] = out
	result["failures"] = failures
	result["global"] = FellowDestructible3D.get_global_stats()
	result["physics_engine"] = ProjectSettings.get_setting("physics/3d/physics_engine")
	if pictures and not shots.is_empty():
		var keys := shots.keys()
		keys.sort()
		var w: int = shots[keys[0]].get_width()
		var h: int = shots[keys[0]].get_height()
		var strip := Image.create(w * 2, h * 2, false, Image.FORMAT_RGBA8)
		for i in keys.size():
			var img: Image = shots[keys[i]]
			img.convert(Image.FORMAT_RGBA8)
			strip.blit_rect(img, Rect2i(0, 0, w, h), Vector2i((i % 2) * w, (i / 2) * h))
		strip.save_png(out_dir.path_join("structure_strip.png"))
	print("STRUCTURE_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("structure_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)
