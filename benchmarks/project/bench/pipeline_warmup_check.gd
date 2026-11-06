extends SceneTree
## FellowEngine pipeline warm-up check (decision 0162): a ring of 24 objects in 12 kinds of material around the camera,
## which sees a quarter of them; after the warm-up the camera turns once around. Without a warm-up the materials coming
## into view compile their specialized pipelines during the turn (drawn with the ubershader meanwhile); with a
## FellowPipelineWarmer3D (--warmer=1), which drew the scene in every direction while it loaded, nothing should.
##
##   godot --path benchmarks/project --resolution 320x180 -s res://bench/pipeline_warmup_check.gd
##       -- [--warmer=1] [--taa=0] [--msaa=0] [--out=<dir>]
##
## One JSON line: PIPELINE_WARMUP_CHECK {...}: the pipelines compiled during the warm-up and the turn, by kind, and the
## warm-up's frames and milliseconds.

var args := {}
var cam: Camera3D
var warmer
var frame := 0
var phase := "load"
var phase_frame := 0
var load_usec := 0
var turn_frames := 240
var counts_at := {}
var result := {}
var quiet := 0
var last_total := -1


func _counts() -> Dictionary:
	return {
		"surface": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_SURFACE),
		"draw": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_DRAW),
		"specialization": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_SPECIALIZATION),
		"mesh": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_MESH),
	}


func _diff(a: Dictionary, b: Dictionary) -> Dictionary:
	var d := {}
	for k in b:
		d[k] = b[k] - a.get(k, 0)
	return d


func _material(i: int) -> Material:
	# Twelve feature sets (twelve shaders), each twice in other colours.
	var m := StandardMaterial3D.new()
	var k := i % 12
	m.albedo_color = Color.from_hsv(float(i) / 24.0, 0.6, 0.8)
	var img := Image.create(16, 16, true, Image.FORMAT_RGBA8)
	img.fill(Color(0.8, 0.8, 0.8, 1))
	img.generate_mipmaps()
	var tex := ImageTexture.create_from_image(img)
	match k:
		1:
			m.albedo_texture = tex
		2:
			m.metallic = 1.0
			m.roughness = 0.2
		3:
			m.emission_enabled = true
			m.emission = Color(0.4, 0.2, 0.1)
		4:
			m.normal_enabled = true
			m.normal_texture = tex
		5:
			m.rim_enabled = true
		6:
			m.clearcoat_enabled = true
		7:
			m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			m.albedo_color.a = 0.6
		8:
			m.cull_mode = BaseMaterial3D.CULL_DISABLED
		9:
			m.vertex_color_use_as_albedo = true
		10:
			m.uv1_triplanar = true
			m.albedo_texture = tex
		11:
			m.shading_mode = BaseMaterial3D.SHADING_MODE_PER_VERTEX
	return m


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	ProjectSettings.set_setting("rendering/fellow/autotune/enabled", false) # its evaluations switch bits of their own
	var vp: Viewport = root
	vp.use_taa = args.get("taa", "0") == "1"
	vp.msaa_3d = Viewport.MSAA_4X if args.get("msaa", "0") == "1" else Viewport.MSAA_DISABLED
	var root3d := Node3D.new()
	root.add_child(root3d)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.35, 0.4, 0.5)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.3, 0.32, 0.35)
	root3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-45, 30, 0)
	sun.shadow_enabled = true
	root3d.add_child(sun)
	var ground := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(60, 60)
	ground.mesh = pm
	root3d.add_child(ground)
	for i in 24:
		var mi := MeshInstance3D.new()
		mi.mesh = BoxMesh.new() if i % 3 == 0 else (SphereMesh.new() if i % 3 == 1 else CylinderMesh.new())
		mi.material_override = _material(i)
		var a := TAU * float(i) / 24.0
		mi.position = Vector3(sin(a) * 12.0, 0.6, -cos(a) * 12.0)
		root3d.add_child(mi)
	cam = Camera3D.new()
	cam.position = Vector3(0, 1.6, 0)
	cam.fov = 60.0
	root3d.add_child(cam)
	cam.current = true
	if args.get("warmer", "0") == "1" and ClassDB.class_exists("FellowPipelineWarmer3D"):
		warmer = ClassDB.instantiate("FellowPipelineWarmer3D")
		root3d.add_child(warmer)
	counts_at["start"] = _counts()
	load_usec = Time.get_ticks_usec()


func _process(_delta: float) -> bool:
	frame += 1
	phase_frame += 1
	match phase:
		"load":
			# Until the warmer has finished (or, without one, 30 frames), then until 20 frames compile nothing.
			var c := _counts()
			var total: int = c["surface"] + c["draw"] + c["specialization"] + c["mesh"]
			quiet = quiet + 1 if total == last_total else 0
			last_total = total
			var warm_done: bool = warmer == null or warmer.is_finished()
			if (warm_done and phase_frame >= 30 and quiet >= 20) or phase_frame > 3000:
				result["load_frames"] = phase_frame
				result["load_ms"] = (Time.get_ticks_usec() - load_usec) / 1000.0
				result["load"] = _diff(counts_at["start"], c)
				if warmer:
					result["warmer"] = warmer.get_stats()
				counts_at["turn"] = c
				phase = "turn"
				phase_frame = 0
		"turn":
			cam.rotation.y = -TAU * float(phase_frame) / float(turn_frames)
			if phase_frame >= turn_frames + 30:
				result["turn"] = _diff(counts_at["turn"], _counts())
				result["warmer_used"] = warmer != null
				result["taa"] = root.use_taa
				result["msaa"] = root.msaa_3d != Viewport.MSAA_DISABLED
				print("PIPELINE_WARMUP_CHECK ", JSON.stringify(result))
				quit(0)
	return false
