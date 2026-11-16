extends SceneTree
## FellowEngine milestone 129 check (decision 0253): FellowHLOD3D's impostors baked into files.
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 960x540 -s res://bench/hlod_impostor_files_check.gd
##       -- --out=<dir> [--trees=1000]
##
## A forest: --trees instances of one tree (a trunk and a crown, about 2 300 triangles, saved as a file) in a
## MultiMeshInstance3D under a FellowHLOD3D with impostors from 60 m. (A) build() at run time: the frames until its
## impostors are baked (one view a frame), the longest frame meanwhile, the picture. (B) bake_impostors() into files,
## the forest saved as a scene. (C) That scene loaded again (nothing from the cache) and built, with its impostors
## cleared (baked at run time again) and with them taken from their files, twice: the frames until they are shown,
## the build's time, the pictures (each 60 frames after its impostors are there) — the files' against the run-time
## bake's, and against each other (lavapipe's floor: the forest's instances culled from frame to frame).
## One JSON line: HLOD_IMPOSTOR_FILES_CHECK {...}; exit code 1 when a check fails.

var args := {}
var out_dir := ""
var result := {}
var cam: Camera3D
var world: Node3D


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	# Autotune's evaluations (frame times: lavapipe's frames of seconds while a bake runs) switch rendering options of
	# their own between the pictures: off, so that they compare the impostors alone.
	ProjectSettings.set_setting("rendering/fellow/autotune/enabled", false)
	out_dir = _arg("out", "user://hlod_impostor_files_check").replace("\\", "/") # (a Windows path's backslashes: written into a .tres's paths, they break it)
	DirAccess.make_dir_recursive_absolute(out_dir)
	_run.call_deferred()


func _tree_mesh() -> ArrayMesh:
	var trunk := CylinderMesh.new()
	trunk.top_radius = 0.2
	trunk.bottom_radius = 0.35
	trunk.height = 4.0
	trunk.radial_segments = 24
	trunk.rings = 8
	var crown := SphereMesh.new()
	crown.radius = 2.2
	crown.height = 5.0
	crown.radial_segments = 40
	crown.rings = 24
	var mesh := ArrayMesh.new()
	var a := trunk.get_mesh_arrays()
	var v: PackedVector3Array = a[Mesh.ARRAY_VERTEX]
	for i in v.size():
		v[i] += Vector3(0, 2.0, 0)
	a[Mesh.ARRAY_VERTEX] = v
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, a)
	var b := crown.get_mesh_arrays()
	var w: PackedVector3Array = b[Mesh.ARRAY_VERTEX]
	for i in w.size():
		w[i] += Vector3(0, 5.5, 0)
	b[Mesh.ARRAY_VERTEX] = w
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, b)
	var bark := StandardMaterial3D.new()
	bark.albedo_color = Color(0.35, 0.24, 0.15)
	var leaves := StandardMaterial3D.new()
	leaves.albedo_color = Color(0.2, 0.42, 0.16)
	leaves.roughness = 0.9
	mesh.surface_set_material(0, bark)
	mesh.surface_set_material(1, leaves)
	return mesh


func _forest(mesh: Mesh) -> Node3D:
	var hlod = ClassDB.instantiate("FellowHLOD3D")
	hlod.name = "Forest"
	hlod.set("build_on_ready", false)
	hlod.set("distance", 40.0)
	hlod.set("impostor_distance", 60.0)
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.mesh = mesh
	var n := int(_arg("trees", "1000"))
	mm.instance_count = n
	var rng := RandomNumberGenerator.new()
	rng.seed = 253
	var side := int(ceil(sqrt(n)))
	for i in n:
		var p := Vector3((i % side) * 6.0 - side * 3.0 + rng.randf_range(-2, 2), 0, (i / side) * 6.0 + rng.randf_range(-2, 2))
		var s := rng.randf_range(0.8, 1.2)
		mm.set_instance_transform(i, Transform3D(Basis(Vector3.UP, rng.randf() * TAU).scaled(Vector3(s, s, s)), p))
	var mmi := MultiMeshInstance3D.new()
	mmi.name = "Trees"
	mmi.multimesh = mm
	hlod.add_child(mmi)
	mmi.owner = hlod
	return hlod


func _shot(name: String) -> Image:
	await RenderingServer.frame_post_draw
	var img := root.get_texture().get_image()
	img.save_png(out_dir.path_join(name + ".png"))
	return img


func _diff(a: Image, b: Image) -> Dictionary:
	var differing := 0
	var worst := 0.0
	var total := 0.0
	for y in a.get_height():
		for x in a.get_width():
			var ca := a.get_pixel(x, y)
			var cb := b.get_pixel(x, y)
			var d := maxf(absf(ca.r - cb.r), maxf(absf(ca.g - cb.g), absf(ca.b - cb.b)))
			total += d
			if d > 0.0:
				differing += 1
			worst = maxf(worst, d)
	return {"share_differing": snappedf(differing / float(a.get_width() * a.get_height()), 0.00001), "max_diff_levels": roundi(worst * 255.0), "mean_levels": snappedf(total * 255.0 / float(a.get_width() * a.get_height()), 0.001)}


# Builds and waits for its impostors: the frames, the longest frame meanwhile.
func _build_and_wait(hlod: Node) -> Dictionary:
	var t0 := Time.get_ticks_usec()
	hlod.call("build")
	var build_ms := (Time.get_ticks_usec() - t0) / 1000.0
	var frames := 0
	var longest := 0.0
	var prev := Time.get_ticks_usec()
	while hlod.call("is_baking"):
		await process_frame
		var now := Time.get_ticks_usec()
		longest = maxf(longest, (now - prev) / 1000.0)
		prev = now
		frames += 1
	var info: Dictionary = hlod.call("get_build_info")
	return {"build_ms": snappedf(build_ms, 0.01), "frames_until_impostors": frames, "longest_frame_ms": snappedf(longest, 0.1),
		"impostor_bakes": info.get("impostor_bakes", 0), "impostors_loaded": info.get("impostors_loaded", 0), "until_impostors_ms": snappedf((Time.get_ticks_usec() - t0) / 1000.0, 0.1)}


func _run() -> void:
	world = Node3D.new()
	root.add_child(world)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.55, 0.7, 0.9)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.5, 0.55, 0.6)
	world.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	world.add_child(sun)
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(800, 800)
	ground.mesh = plane
	ground.position = Vector3(0, 0, 150)
	world.add_child(ground)
	cam = Camera3D.new()
	cam.far = 1000.0
	world.add_child(cam)
	cam.make_current()
	cam.look_at_from_position(Vector3(0, 25, -30), Vector3(0, 0, 120))

	# The tree as a file (impostors are keyed by their meshes' files).
	ResourceSaver.save(_tree_mesh(), out_dir.path_join("tree.res"))
	var mesh: Mesh = load(out_dir.path_join("tree.res"))
	result["tree_triangles"] = (mesh.surface_get_array_index_len(0) + mesh.surface_get_array_index_len(1)) / 3

	# A. Built at run time.
	var forest := _forest(mesh)
	world.add_child(forest)
	await process_frame
	result["a_run_time"] = await _build_and_wait(forest)
	for i in 60:
		await process_frame
	var a := await _shot("a_run_time")

	# B. Baked into files, the forest saved as a scene.
	var t0 := Time.get_ticks_usec()
	var queued: int = forest.call("bake_impostors", out_dir.path_join("impostors"))
	var frames := 0
	var baked := [-1]
	forest.connect("impostors_baked", func(count): baked[0] = count, CONNECT_ONE_SHOT)
	while baked[0] < 0 and frames < 2000:
		await process_frame
		frames += 1
	var files := DirAccess.get_files_at(out_dir.path_join("impostors"))
	var bytes := 0
	for f in files:
		bytes += FileAccess.get_file_as_bytes(out_dir.path_join("impostors").path_join(f)).size()
	forest.call("clear")
	var ps := PackedScene.new()
	ps.pack(forest)
	ResourceSaver.save(ps, out_dir.path_join("forest.scn"))
	result["b_bake_to_files"] = {"queued": queued, "saved": baked[0], "frames": frames, "ms": snappedf((Time.get_ticks_usec() - t0) / 1000.0, 0.1), "files": files.size(), "bytes": bytes}
	forest.queue_free()
	await process_frame
	await process_frame

	# C. The scene loaded again (its resources not taken from the cache) and built: once with its impostors cleared
	# (baked at run time again: the reference picture, drawn at the same point of the run), once with them.
	var c_pictures := []
	for lane in ["c_run_time", "c_from_files", "c_from_files_again"]:
		var loaded: PackedScene = ResourceLoader.load(out_dir.path_join("forest.scn"), "", ResourceLoader.CACHE_MODE_IGNORE_DEEP)
		var forest2: Node3D = loaded.instantiate()
		if lane == "c_run_time":
			forest2.set("impostors", {})
		world.add_child(forest2)
		await process_frame
		result[lane] = await _build_and_wait(forest2)
		# (60 frames once the impostors are there: their pipelines compiled meanwhile — the ubershader draws until
		# then, not quite the same picture.)
		for i in 60:
			await process_frame
		c_pictures.append(await _shot(lane))
		forest2.queue_free()
		await process_frame
		await process_frame
	result["c_from_files"]["vs_c_run_time"] = _diff(c_pictures[0], c_pictures[1])
	result["c_from_files_again"]["vs_c_from_files"] = _diff(c_pictures[1], c_pictures[2])
	result["c_from_files"]["vs_a"] = _diff(a, c_pictures[1])

	var failures := []
	if result.a_run_time.impostor_bakes < 1:
		failures.append("no impostor baked at run time")
	if result.b_bake_to_files.saved != result.b_bake_to_files.queued or result.b_bake_to_files.queued < 1:
		failures.append("bake into files")
	if result.c_from_files.impostors_loaded != result.a_run_time.impostor_bakes or result.c_from_files.impostor_bakes != 0 or result.c_from_files.frames_until_impostors != 0:
		failures.append("impostors not taken from their files")
	# (Two loads of the same files differ too on lavapipe — the forest's instances drawn from frame to frame by the
	# GPU's culling: the run-time bake's picture must be no farther than that.)
	var floor_share: float = result.c_from_files_again.vs_c_from_files.share_differing
	if result.c_from_files.vs_c_run_time.share_differing > maxf(0.01, 1.5 * floor_share):
		failures.append("the picture differs from the run-time bake more than two loads of the files do")
	result["failures"] = failures
	print("HLOD_IMPOSTOR_FILES_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("hlod_impostor_files_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)
