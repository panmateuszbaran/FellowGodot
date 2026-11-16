extends SceneTree
## FellowEngine milestone 130 check (decision 0254): FellowHLOD3D's merged proxies baked into files.
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 960x540 -s res://bench/hlod_merged_files_check.gd
##       -- --out=<dir> [--houses=1600]
##
## A town: --houses MeshInstance3D nodes of three house meshes (saved as files) on a grid, under a FellowHLOD3D that
## merges them per cell of 50 m with detail levels from 60 m (instancing off: every house merged). (A) build() at run
## time: its time, the picture. (B) bake_merged_proxies() into files, the town saved as a scene. (C) That scene loaded
## again (nothing from the resource cache) and built, with its merged proxies cleared (merged at run time again, twice)
## and with them taken from their files: the builds' times, the pictures against each other (two run-time merges of the
## same scene differ too on lavapipe: the floor).
## One JSON line: HLOD_MERGED_FILES_CHECK {...}; exit code 1 when a check fails.

var args := {}
var out_dir := ""
var result := {}
var world: Node3D


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	ProjectSettings.set_setting("rendering/fellow/autotune/enabled", false)
	out_dir = _arg("out", "user://hlod_merged_files_check").replace("\\", "/") # (a Windows path's backslashes: written into a .tres's paths, they break it)
	DirAccess.make_dir_recursive_absolute(out_dir)
	_run.call_deferred()


# A house: a body, a roof, a chimney (two materials), as a file.
func _house(i: int) -> Mesh:
	var st := SurfaceTool.new()
	var body := BoxMesh.new()
	body.size = Vector3(5 + i, 4 + i, 6)
	body.subdivide_width = 6
	body.subdivide_height = 6
	body.subdivide_depth = 6
	var roof := PrismMesh.new()
	roof.size = Vector3(6 + i, 2.5, 7)
	roof.subdivide_width = 6
	roof.subdivide_depth = 6
	var chimney := CylinderMesh.new()
	chimney.top_radius = 0.3
	chimney.bottom_radius = 0.3
	chimney.height = 2.0
	chimney.radial_segments = 16
	var mesh := ArrayMesh.new()
	var walls := StandardMaterial3D.new()
	walls.albedo_color = Color(0.85, 0.8, 0.7).darkened(0.1 * i)
	var tiles := StandardMaterial3D.new()
	tiles.albedo_color = Color(0.6, 0.22, 0.15)
	# The materials as files of their own, as a project keeps them (a merged mesh saved into a file then refers to
	# them instead of carrying copies).
	ResourceSaver.save(walls, out_dir.path_join("walls_%d.tres" % i))
	walls = load(out_dir.path_join("walls_%d.tres" % i))
	if i == 0:
		ResourceSaver.save(tiles, out_dir.path_join("tiles.tres"))
	tiles = load(out_dir.path_join("tiles.tres"))
	for part in [[body, Vector3(0, (4 + i) * 0.5, 0), walls], [roof, Vector3(0, 4 + i + 1.25, 0), tiles], [chimney, Vector3(1.5, 4 + i + 2.0, 1.0), tiles]]:
		var a: Array = part[0].get_mesh_arrays()
		var v: PackedVector3Array = a[Mesh.ARRAY_VERTEX]
		for k in v.size():
			v[k] += part[1]
		a[Mesh.ARRAY_VERTEX] = v
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, a)
		mesh.surface_set_material(mesh.get_surface_count() - 1, part[2])
	var path := out_dir.path_join("house_%d.res" % i)
	ResourceSaver.save(mesh, path)
	return load(path)


func _town(houses: Array) -> Node3D:
	var hlod = ClassDB.instantiate("FellowHLOD3D")
	hlod.name = "Town"
	hlod.set("build_on_ready", false)
	hlod.set("instancing", false)
	hlod.set("distance", 60.0)
	hlod.set("cell_size", 50.0)
	var n := int(_arg("houses", "1600"))
	var side := int(ceil(sqrt(n)))
	var rng := RandomNumberGenerator.new()
	rng.seed = 254
	for i in n:
		var mi := MeshInstance3D.new()
		mi.name = "House%d" % i
		mi.mesh = houses[rng.randi() % houses.size()]
		mi.position = Vector3((i % side) * 12.0 - side * 6.0, 0, (i / side) * 12.0)
		mi.rotation.y = rng.randi_range(0, 3) * PI * 0.5
		hlod.add_child(mi)
		mi.owner = hlod
	return hlod


func _shot(name: String) -> Image:
	for i in 10:
		await process_frame
	await RenderingServer.frame_post_draw
	var img := root.get_texture().get_image()
	img.save_png(out_dir.path_join(name + ".png"))
	return img


func _diff(a: Image, b: Image) -> Dictionary:
	var differing := 0
	var worst := 0.0
	for y in a.get_height():
		for x in a.get_width():
			var ca := a.get_pixel(x, y)
			var cb := b.get_pixel(x, y)
			var d := maxf(absf(ca.r - cb.r), maxf(absf(ca.g - cb.g), absf(ca.b - cb.b)))
			if d > 0.0:
				differing += 1
			worst = maxf(worst, d)
	return {"share_differing": snappedf(differing / float(a.get_width() * a.get_height()), 0.00001), "max_diff_levels": roundi(worst * 255.0)}


func _build(hlod: Node) -> Dictionary:
	var t0 := Time.get_ticks_usec()
	hlod.call("build")
	var ms := (Time.get_ticks_usec() - t0) / 1000.0
	var info: Dictionary = hlod.call("get_build_info")
	return {"build_ms": snappedf(ms, 0.1), "merged_proxies": info.get("merged_proxies", 0), "merged_members": info.get("merged_members", 0), "merged_loaded": info.get("merged_loaded", 0), "lod_triangles": info.get("lod_triangles", 0)}


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
	var cam := Camera3D.new()
	cam.far = 2000.0
	world.add_child(cam)
	cam.make_current()
	cam.look_at_from_position(Vector3(0, 60, -60), Vector3(0, 0, 200))
	var houses := []
	for i in 3:
		houses.append(_house(i))

	# A. Built at run time.
	var town := _town(houses)
	world.add_child(town)
	await process_frame
	result["a_run_time"] = _build(town)
	var a := await _shot("a_run_time")

	# B. Baked into files, the town saved as a scene.
	var t0 := Time.get_ticks_usec()
	var saved: int = town.call("bake_merged_proxies", out_dir.path_join("merged"))
	result["b_bake_to_files"] = {"saved": saved, "ms": snappedf((Time.get_ticks_usec() - t0) / 1000.0, 0.1), "built_after": town.call("is_built")}
	town.call("clear")
	var ps := PackedScene.new()
	ps.pack(town)
	ResourceSaver.save(ps, out_dir.path_join("town.scn"))
	town.queue_free()
	await process_frame
	await process_frame

	# C. Loaded again and built: merged at run time again, and taken from the files.
	var pictures := []
	for lane in ["c_run_time", "c_from_files", "c_run_time_again"]:
		var loaded: PackedScene = ResourceLoader.load(out_dir.path_join("town.scn"), "", ResourceLoader.CACHE_MODE_IGNORE_DEEP)
		var t2: Node3D = loaded.instantiate()
		if lane != "c_from_files":
			t2.set("merged_proxies", {})
		world.add_child(t2)
		await process_frame
		result[lane] = _build(t2)
		pictures.append(await _shot(lane))
		t2.queue_free()
		await process_frame
		await process_frame
	result["c_from_files"]["vs_c_run_time"] = _diff(pictures[0], pictures[1])
	result["c_from_files"]["vs_a"] = _diff(a, pictures[1])
	result["c_run_time_again"]["vs_c_run_time"] = _diff(pictures[0], pictures[2])

	var failures := []
	if saved < 1 or saved != result.a_run_time.merged_proxies:
		failures.append("bake into files")
	if result.c_from_files.merged_loaded != result.c_run_time.merged_proxies or result.c_from_files.merged_members != result.c_run_time.merged_members:
		failures.append("merged proxies not taken from their files")
	# (Two run-time builds of the same scene differ too on lavapipe — not in their meshes: the floor.)
	var floor_share: float = result.c_run_time_again.vs_c_run_time.share_differing
	if result.c_from_files.vs_c_run_time.share_differing > maxf(0.01, 1.5 * floor_share):
		failures.append("the picture differs from the run-time merge more than two run-time merges do")
	result["failures"] = failures
	print("HLOD_MERGED_FILES_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("hlod_merged_files_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)
