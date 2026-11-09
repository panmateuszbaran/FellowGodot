extends SceneTree
## FellowEngine pipeline states check (decision 0161): the specialized pipelines compiled when what a frame holds
## changes — the rain starting and stopping, an omni light, a shadowed spot light, a decal and a reflection probe shown
## and hidden, the sun set and risen — after a warm-up in the first state (autotune off: its evaluations switch bits of
## their own, decision 0068). With
## rendering/fellow/frame/stable_scene_bits a feature that exists (hidden) keeps its code in the pipelines from the
## start: no state compiles anything; without it each new combination compiles the materials in view again.
##
##   godot --path benchmarks/project --resolution 320x180 -s res://bench/pipeline_states_check.gd
##       -- [--stable=1] [--out=<dir>] [--late=0] [--autotune=0] [--sun_size=0] [--contact=0] [--flash=0]
##
## --sun_size=<deg>: the sun's angular size (soft shadows: PCSS); --contact=1: FellowEngine's contact shadows (decision
## 0107) — with both, the sun setting changes Godot's soft-shadow bit and the contact mask's (decision 0167).
## --flash=1: six more states, a lightning's flash (a second directional light with a shadow and no angular size, as
## FellowLightning3D's) by day and at night — with --sun_size, the flash alone at night changes the soft-shadow bit.
## --late=1 creates the omni light only when its state comes (it did not exist at the start: one compilation then, none
## the second time). Each state's picture is saved once its frames compile nothing more (images: <out>/<state>.png), so
## that a run with --stable=1 and one with --stable=0 can be compared pixel for pixel. One JSON line:
## PIPELINE_STATES_CHECK {...}.

var args := {}
var out_dir := ""
var root3d: Node3D
var rain
var omni: OmniLight3D
var spot: SpotLight3D
var decal: Decal
var probe: ReflectionProbe
var sun: DirectionalLight3D
var flash: DirectionalLight3D
var states := []
var state_i := -1
var frame := 0
var state_frame := 0
var quiet := 0
var last := 0
var start_count := 0
var result := {"states": []}


func _spec() -> int:
	return RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_SPECIALIZATION)


func _all() -> int:
	var n := 0
	for k in [RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_SURFACE, RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_DRAW,
			RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_SPECIALIZATION, RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_MESH]:
		n += RenderingServer.get_rendering_info(k)
	return n


func _material(c: Color, rough: float, metal: float, alpha := false) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = rough
	m.metallic = metal
	if alpha:
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		m.albedo_color.a = 0.6
	return m


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	ProjectSettings.set_setting("rendering/fellow/frame/stable_scene_bits", args.get("stable", "1") == "1")
	# Autotune's evaluations switch pipeline bits of their own (decision 0068 prepares those): off unless asked for.
	ProjectSettings.set_setting("rendering/fellow/autotune/enabled", args.get("autotune", "0") == "1")
	if args.get("contact", "0") == "1":
		ProjectSettings.set_setting("rendering/fellow/post/contact_shadows/enabled", true)
	out_dir = args.get("out", "")
	if out_dir != "":
		DirAccess.make_dir_recursive_absolute(out_dir)
	root3d = Node3D.new()
	root.add_child(root3d)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.35, 0.4, 0.5)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.3, 0.32, 0.35)
	root3d.add_child(env)
	sun = DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-45, 30, 0)
	sun.shadow_enabled = true
	sun.light_angular_distance = float(args.get("sun_size", "0"))
	root3d.add_child(sun)
	var cam := Camera3D.new()
	cam.position = Vector3(0, 5, 9)
	cam.look_at_from_position(cam.position, Vector3(0, 0.5, 0))
	root3d.add_child(cam)
	# Ground, boxes and spheres in a few materials, a MultiMesh of small boxes, a transparent pane.
	var ground := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(30, 30)
	ground.mesh = pm
	ground.material_override = _material(Color(0.4, 0.38, 0.33), 0.9, 0.0)
	root3d.add_child(ground)
	var mats := [_material(Color(0.7, 0.2, 0.2), 0.5, 0.0), _material(Color(0.8, 0.8, 0.8), 0.2, 1.0),
			_material(Color(0.2, 0.5, 0.25), 0.8, 0.0)]
	for i in 6:
		var mi := MeshInstance3D.new()
		mi.mesh = BoxMesh.new() if i % 2 == 0 else SphereMesh.new()
		mi.material_override = mats[i % 3]
		mi.position = Vector3(-4 + i * 1.6, 0.5, -1 + (i % 2) * 1.5)
		root3d.add_child(mi)
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	var small := BoxMesh.new()
	small.size = Vector3(0.2, 0.2, 0.2)
	small.material = _material(Color(0.6, 0.55, 0.2), 0.6, 0.0)
	mm.mesh = small
	mm.instance_count = 64
	for i in 64:
		mm.set_instance_transform(i, Transform3D(Basis(), Vector3(-3.5 + (i % 8), 0.1, 2 + (i / 8) * 0.3)))
	var mmi := MultiMeshInstance3D.new()
	mmi.multimesh = mm
	root3d.add_child(mmi)
	var pane := MeshInstance3D.new()
	var qm := QuadMesh.new()
	qm.size = Vector2(2, 1.5)
	pane.mesh = qm
	pane.material_override = _material(Color(0.3, 0.5, 0.9), 0.1, 0.0, true)
	pane.position = Vector3(2.5, 1, 2)
	root3d.add_child(pane)
	# The features, there from the start but hidden (or, with --late=1, the omni light made when its state comes).
	if ClassDB.class_exists("FellowRain3D"):
		rain = ClassDB.instantiate("FellowRain3D")
		rain.rain = 0.0
		rain.set("wetness_follows_rain", false)
		rain.set("wetness", 0.0)
		root3d.add_child(rain)
	if args.get("late", "0") != "1":
		_make_omni()
	spot = SpotLight3D.new()
	spot.position = Vector3(-2, 4, 2)
	spot.rotation_degrees = Vector3(-70, 0, 0)
	spot.spot_range = 10
	spot.light_energy = 4
	spot.shadow_enabled = true
	spot.visible = false
	root3d.add_child(spot)
	decal = Decal.new()
	decal.size = Vector3(3, 2, 3)
	decal.modulate = Color(0.9, 0.1, 0.1)
	var img := Image.create(8, 8, false, Image.FORMAT_RGBA8)
	img.fill(Color(1, 1, 1, 1))
	decal.texture_albedo = ImageTexture.create_from_image(img)
	decal.position = Vector3(0, 0, 3)
	decal.visible = false
	root3d.add_child(decal)
	probe = ReflectionProbe.new()
	probe.size = Vector3(12, 6, 12)
	probe.position = Vector3(0, 2, 0)
	probe.visible = false
	root3d.add_child(probe)
	states = ["start", "rain", "dry", "omni", "omni_off", "spot", "spot_off", "decal", "decal_off", "probe", "probe_off",
			"night", "day", "rain", "omni", "dry", "omni_off", "spot", "rain", "spot_off", "dry"]
	if args.get("flash", "0") == "1":
		# As FellowLightning3D's flash: hidden, shown for a strike (decision 0167).
		flash = DirectionalLight3D.new()
		flash.rotation_degrees = Vector3(-70, -40, 0)
		flash.shadow_enabled = true
		flash.light_energy = 2.0
		flash.sky_mode = DirectionalLight3D.SKY_MODE_LIGHT_ONLY
		flash.visible = false
		root3d.add_child(flash)
		states.append_array(["flash", "flash_off", "night", "flash", "flash_off", "day"])


func _make_omni() -> void:
	omni = OmniLight3D.new()
	omni.position = Vector3(1, 1.5, 0.5)
	omni.omni_range = 6
	omni.light_energy = 3
	omni.light_color = Color(1, 0.6, 0.3)
	omni.visible = false
	root3d.add_child(omni)


func _apply(s: String) -> void:
	match s:
		"rain":
			if rain:
				rain.set("wetness", 0.8)
		"dry":
			if rain:
				rain.set("wetness", 0.0)
		"omni":
			if omni == null:
				_make_omni()
			omni.visible = true
		"omni_off":
			omni.visible = false
		"spot":
			spot.visible = true
		"spot_off":
			spot.visible = false
		"decal":
			decal.visible = true
		"decal_off":
			decal.visible = false
		"probe":
			probe.visible = true
		"probe_off":
			probe.visible = false
		"night":
			sun.visible = false
		"day":
			sun.visible = true
		"flash":
			flash.visible = true
		"flash_off":
			flash.visible = false


func _process(_delta: float) -> bool:
	frame += 1
	state_frame += 1
	var n := _all()
	if n != last:
		quiet = 0
		last = n
	else:
		quiet += 1
	# A state ends after 20 frames that compiled nothing (at least 30 frames; the warm-up at least 60).
	var min_frames := 60 if state_i <= 0 else 30
	if state_i >= 0 and (state_frame < min_frames or quiet < 20) and state_frame < 2000:
		return false
	if state_i >= 0:
		var s: String = states[state_i]
		var entry := {"state": s, "frames": state_frame, "specialization": _spec() - start_count,
				"from_frame": Engine.get_frames_drawn() - state_frame}
		result["states"].append(entry)
		if out_dir != "":
			var img := root.get_viewport().get_texture().get_image()
			if img:
				img.save_png(out_dir.path_join("%02d_%s.png" % [state_i, s]))
	state_i += 1
	if state_i >= states.size():
		var after := 0
		for e in result["states"].slice(1):
			after += e["specialization"]
		result["after_warmup"] = after
		result["stable"] = args.get("stable", "1") == "1"
		result["late"] = args.get("late", "0") == "1"
		print("PIPELINE_STATES_CHECK ", JSON.stringify(result))
		quit(0)
		return false
	_apply(states[state_i])
	start_count = _spec()
	state_frame = 0
	quiet = 0
	return false
