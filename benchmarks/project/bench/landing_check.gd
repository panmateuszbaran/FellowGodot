extends SceneTree
## FellowEngine milestone 123 check (decision 0247): FellowDestructible3D's landing_break_speed — the parts a
## structural object drops break where they land.
##
##   godot --path benchmarks/project --headless --fixed-fps 60 -s res://bench/landing_check.gd -- --out=<dir>
##   godot --path benchmarks/project --fixed-fps 60 --resolution 1280x720 -s res://bench/landing_check.gd
##       -- --out=<dir> --pictures=1
##
## Lanes (each object prefractured, static, on the ground, 2200 kg/m³):
## * a pillar 0.5 x 3 x 0.5 m in 14 pieces broken at its foot (decision 0188's lane): its upper part topples; with
##   landing_break_speed 3 m/s it breaks where it strikes the ground, without (0) it lies whole;
## * a balcony 2 x 0.25 x 1.5 m held out 3 m up from a building's wall, 24 pieces, too weak for itself (300 kPa;
##   anchored to the wall): it breaks off and falls 3 m (about 7.7 m/s); at 3 m/s it breaks where it lands, at 10 m/s
##   it lies whole.
## One JSON line: LANDING_CHECK {...}; exit code 1 when a lane did otherwise than expected.

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
	out_dir = _arg("out", "user://landing_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	pictures = _arg("pictures", "0") == "1"
	_build.call_deferred()


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.85
	return m


func _object(name: String, pos: Vector3, size: Vector3, pieces: int, strength: float, landing: float, color: Color, anchor := FellowDestructible3D.ANCHOR_LOWEST_POINT) -> FellowDestructible3D:
	var body := StaticBody3D.new()
	body.name = name
	body.position = pos
	var mi := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	mi.mesh = mesh
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
	d.structural = true
	d.strength = strength
	d.landing_break_speed = landing
	d.anchor_mode = anchor
	d.density = 2200.0
	d.break_impulse = 1e6 # struck only by the check
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
	cam.look_at_from_position(Vector3(8, 6, 15), Vector3(8, 1.0, 0))
	FellowDestructible3D.set_max_pieces_per_frame(0)
	lanes["pillar_breaks"] = {"d": _object("PillarBreaks", Vector3(0, 1.5, 0), Vector3(0.5, 3, 0.5), 14, 0.0, 3.0, Color(0.62, 0.62, 0.6)), "data": {}}
	lanes["pillar_whole"] = {"d": _object("PillarWhole", Vector3(3, 1.5, 0), Vector3(0.5, 3, 0.5), 14, 0.0, 0.0, Color(0.6, 0.6, 0.66)), "data": {}}
	var building := StaticBody3D.new()
	building.position = Vector3(6.75, 2.0, 0)
	var bcs := CollisionShape3D.new()
	var bshape := BoxShape3D.new()
	bshape.size = Vector3(0.5, 4, 6)
	bcs.shape = bshape
	building.add_child(bcs)
	var bmi := MeshInstance3D.new()
	var bmesh := BoxMesh.new()
	bmesh.size = Vector3(0.5, 4, 6)
	bmi.mesh = bmesh
	bmi.set_surface_override_material(0, _material(Color(0.5, 0.5, 0.52)))
	building.add_child(bmi)
	root3d.add_child(building)
	lanes["balcony_breaks"] = {"d": _object("BalconyBreaks", Vector3(8, 3.125, -1.5), Vector3(2, 0.25, 1.5), 24, 300.0, 3.0, Color(0.66, 0.6, 0.55), FellowDestructible3D.ANCHOR_STATIC_BODIES), "data": {}}
	lanes["balcony_whole"] = {"d": _object("BalconyWhole", Vector3(8, 3.125, 1.5), Vector3(2, 0.25, 1.5), 24, 300.0, 10.0, Color(0.6, 0.62, 0.56), FellowDestructible3D.ANCHOR_STATIC_BODIES), "data": {}}
	for n in lanes:
		var d: FellowDestructible3D = lanes[n].d
		d.broken.connect(func(pieces: Array) -> void:
			lanes[n].data["broken_signals"] = lanes[n].data.get("broken_signals", 0) + 1
			if not lanes[n].has("made"):
				lanes[n]["made"] = []
			lanes[n].made.append_array(pieces))
	physics_frame.connect(_on_frame)


func _on_frame() -> void:
	if done:
		return
	frame += 1
	if frame == 30:
		lanes.pillar_breaks.d.break_apart(Vector3(0.0, 0.3, 0.25), Vector3(0, 0, -400))
		lanes.pillar_whole.d.break_apart(Vector3(3.0, 0.3, 0.25), Vector3(0, 0, -400))
		for n in ["balcony_breaks", "balcony_whole"]:
			(lanes[n].d as FellowDestructible3D).update_structure()
	# Each destructible's stats while it is there (one that nothing of stands is freed with its body once its parts
	# have landed).
	for n in lanes:
		if is_instance_valid(lanes[n].d):
			var st: Dictionary = (lanes[n].d as FellowDestructible3D).get_stats()
			lanes[n].data["landing_breaks"] = st.landing_breaks_total
			lanes[n].data["standing"] = st.standing
			lanes[n].data["fallen_total"] = st.fallen_total
			lanes[n].data["falling_parts"] = st.falling_parts
		elif not lanes[n].data.has("freed_frame"):
			lanes[n].data["freed_frame"] = frame
	if pictures and frame in [29, 50, 90, 300]:
		shots[frame] = get_root().get_texture().get_image()
	if frame == 300:
		for n in lanes:
			var data: Dictionary = lanes[n].data
			# The bodies it made that are still there: single pieces and parts of several, and how high they lie.
			var bodies := 0
			var parts := 0
			var largest := 0
			var top := 0.0
			for b in lanes[n].get("made", []):
				if not is_instance_valid(b):
					continue
				bodies += 1
				var meshes := 0
				for c in (b as Node).get_children():
					meshes += 1 if c is MeshInstance3D else 0
				parts += 1 if meshes > 1 else 0
				largest = maxi(largest, meshes)
				top = maxf(top, (b as Node3D).global_position.y)
			data["bodies"] = bodies
			data["parts"] = parts
			data["largest_part"] = largest
			data["highest_body_y"] = snappedf(top, 0.01)
		done = true
		_finish.call_deferred()


func _finish() -> void:
	var failures := []
	var out := {}
	for n in lanes:
		var data: Dictionary = lanes[n].data
		var ok := false
		match n:
			"pillar_breaks":
				ok = data.get("landing_breaks", 0) >= 1 and data.get("largest_part", 99) < 10 and data.get("bodies", 0) >= 6
			"pillar_whole":
				ok = data.get("landing_breaks", -1) == 0 and data.get("parts", 0) >= 1 and data.get("largest_part", 0) >= 8
			"balcony_breaks":
				ok = data.get("landing_breaks", 0) >= 1 and data.get("largest_part", 99) < data.get("fallen_total", 0) and data.get("highest_body_y", 9.0) < 1.0
			"balcony_whole":
				ok = data.get("landing_breaks", -1) == 0 and data.get("largest_part", 0) == data.get("fallen_total", -1) and data.get("fallen_total", 0) >= 12
		data["ok"] = ok
		data.erase("made")
		if not ok:
			failures.append(n)
		out[n] = data
	result["lanes"] = out
	result["failures"] = failures
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
		strip.save_png(out_dir.path_join("landing_strip.png"))
	print("LANDING_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("landing_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)
