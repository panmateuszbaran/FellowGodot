extends SceneTree
## FellowEngine milestone 66 check (decision 0174): proxies baked offline for unloaded streaming cells, and streamed
## meshes (stand-ins far, the full meshes near).
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 640x360 -s res://bench/world_proxies_check.gd
##       -- --out=<dir> [--cells=12] [--load_distance=160] [--mesh_distance=70] [--frames=20]
##
## A world of --cells x --cells cells of 64 m (World Lab's: ten buildings as one mesh of 1.5 m quads, 40 crates;
## Streaming Lab's textures) on a terrain split into regions of the same size, both under streamers. From a corner,
## looking across the world: (A) every cell loaded; (B) streamed to --load_distance; (C) B with proxies baked for the
## cells and the regions; (D) C with the buildings streamed by --mesh_distance. Each: the picture, its difference from
## A, the video, texture and buffer memory, the triangles drawn. (E) Flights across the world at 30 m/s with D's cells,
## the full meshes whenever loaded and then by --mesh_distance: the buffer memory, frame times, the meshes' swaps.
## One JSON line: WORLD_PROXIES_CHECK {...}.

const CELL := 64.0
var args := {}
var out_dir := ""
var result := {}
var n_cells := 12
var frames := 20
var root3d: Node3D
var cam: Camera3D
var mats: Array[StandardMaterial3D] = []
var streamer: Node3D
var ground_streamer: Node3D


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://world_proxies_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	n_cells = int(_arg("cells", "12"))
	frames = int(_arg("frames", "20"))
	_run.call_deferred()


func _settle(n: int) -> void:
	for i in n:
		await process_frame


func _materials() -> bool:
	for i in 48:
		var a = load("res://external/streaming/m%02d_albedo.ctex" % i)
		var nm = load("res://external/streaming/m%02d_normal.ctex" % i)
		if a == null or nm == null:
			return false
		var m := StandardMaterial3D.new()
		m.albedo_texture = a
		m.normal_enabled = true
		m.normal_texture = nm
		m.roughness = 0.75
		m.uv1_scale = Vector3(0.25, 0.25, 1.0)
		mats.append(m)
	return true


func _box_quads(verts: PackedVector3Array, normals: PackedVector3Array, uvs: PackedVector2Array, center: Vector3, size: Vector3, step: float) -> void:
	var h := size * 0.5
	var faces := [
		[Vector3(0, 0, 1), Vector3(1, 0, 0), Vector3(0, 1, 0), size.x, size.y, h.z],
		[Vector3(0, 0, -1), Vector3(-1, 0, 0), Vector3(0, 1, 0), size.x, size.y, h.z],
		[Vector3(1, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0), size.z, size.y, h.x],
		[Vector3(-1, 0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0), size.z, size.y, h.x],
		[Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, -1), size.x, size.z, h.y],
	]
	for f in faces:
		var n: Vector3 = f[0]
		var u: Vector3 = f[1]
		var v: Vector3 = f[2]
		var fw: float = f[3]
		var fh: float = f[4]
		var nu := maxi(1, int(ceil(fw / step)))
		var nv := maxi(1, int(ceil(fh / step)))
		var o: Vector3 = center + n * float(f[5]) - u * fw * 0.5 - v * fh * 0.5
		for j in nv:
			for i in nu:
				var u0 := fw * i / nu
				var u1 := fw * (i + 1) / nu
				var v0 := fh * j / nv
				var v1 := fh * (j + 1) / nv
				var p00 := o + u * u0 + v * v0
				var p10 := o + u * u1 + v * v0
				var p01 := o + u * u0 + v * v1
				var p11 := o + u * u1 + v * v1
				verts.append_array([p00, p11, p10, p00, p01, p11])
				for k in 6:
					normals.append(n)
				uvs.append_array([Vector2(u0, -v0), Vector2(u1, -v1), Vector2(u1, -v0), Vector2(u0, -v0), Vector2(u0, -v1), Vector2(u1, -v1)])


# A World Lab cell, on the ground's heights (the buildings and crates stand on the terrain under them).
func _make_cell(x: int, z: int, terrain) -> Node3D:
	var rng := RandomNumberGenerator.new()
	rng.seed = 46000 + x * 131 + z
	var origin := Vector3((x + 0.5) * CELL, 0, (z + 0.5) * CELL)
	var root := Node3D.new()
	root.name = "cell"
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var uvs := PackedVector2Array()
	for b in 10:
		var w := rng.randf_range(8.0, 16.0)
		var d := rng.randf_range(8.0, 16.0)
		var h := rng.randf_range(10.0, 32.0)
		var cx := rng.randf_range(-CELL * 0.5 + w, CELL * 0.5 - w)
		var cz := rng.randf_range(-CELL * 0.5 + d, CELL * 0.5 - d)
		var y: float = terrain.get_height(origin + Vector3(cx, 0, cz)) - 1.0
		_box_quads(verts, normals, uvs, Vector3(cx, y + h * 0.5, cz), Vector3(w, h, d), 1.5)
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	var am := ArrayMesh.new()
	am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var buildings := MeshInstance3D.new()
	buildings.name = "buildings"
	buildings.mesh = am
	buildings.material_override = mats[(x * 5 + z * 11 + 1) % mats.size()]
	root.add_child(buildings)
	buildings.owner = root
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	var crate := BoxMesh.new()
	crate.size = Vector3(1, 1, 1)
	mm.mesh = crate
	mm.instance_count = 40
	for i in 40:
		var p := Vector3(rng.randf_range(-30, 30), 0, rng.randf_range(-30, 30))
		p.y = terrain.get_height(origin + p) + 0.5
		mm.set_instance_transform(i, Transform3D(Basis(Vector3.UP, rng.randf() * TAU), p))
	var crates := MultiMeshInstance3D.new()
	crates.name = "crates"
	crates.multimesh = mm
	crates.material_override = mats[(x + z * 13 + 2) % mats.size()]
	root.add_child(crates)
	crates.owner = root
	return root


func _terrain():
	var size := n_cells * 64 + 1
	var noise := FastNoiseLite.new()
	noise.seed = 66
	noise.frequency = 0.006
	var data := PackedFloat32Array()
	data.resize(size * size)
	for z in size:
		for x in size:
			data[z * size + x] = noise.get_noise_2d(x, z) * 12.0
	var t = ClassDB.instantiate("FellowTerrain3D")
	t.name = "Ground"
	var l0 = ClassDB.instantiate("FellowTerrainLayer")
	l0.albedo_color = Color(0.4, 0.55, 0.3)
	l0.albedo_texture = mats[3].albedo_texture
	l0.texture_size = 4.0
	t.layers = [l0]
	t.heightmap = Image.create_from_data(size, size, false, Image.FORMAT_RF, data.to_byte_array())
	return t


func _stats() -> Dictionary:
	return {
		"video_mb": snappedf(Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED) / 1048576.0, 0.1),
		"texture_mb": snappedf(Performance.get_monitor(Performance.RENDER_TEXTURE_MEM_USED) / 1048576.0, 0.1),
		"buffer_mb": snappedf(Performance.get_monitor(Performance.RENDER_BUFFER_MEM_USED) / 1048576.0, 0.1),
		"primitives": Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME),
		"draw_calls": Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME),
		"cells_loaded": streamer.get_stats().get("loaded", 0),
		"regions_loaded": ground_streamer.get_stats().get("loaded", 0),
	}


func _wait_settled() -> void:
	for i in 1200:
		await process_frame
		if streamer.is_settled() and ground_streamer.is_settled() and i > 30:
			var s: Dictionary = streamer.get_stats()
			if s.get("streamed_meshes", 0) == 0 or s.get("mesh_loads", 0) > 0 or float(_arg("mesh_distance", "70")) <= 0.0 or i > 300:
				break
	await _settle(frames)


func _shot(name: String) -> Image:
	await RenderingServer.frame_post_draw
	var img := root.get_texture().get_image()
	img.save_png(out_dir.path_join(name + ".png"))
	return img


func _diff(a: Image, b: Image) -> Dictionary:
	var big := 0
	var total := 0.0
	for y in a.get_height():
		for x in a.get_width():
			var ca := a.get_pixel(x, y)
			var cb := b.get_pixel(x, y)
			var d := maxf(absf(ca.r - cb.r), maxf(absf(ca.g - cb.g), absf(ca.b - cb.b)))
			total += d
			if d > 16.0 / 255.0:
				big += 1
	var n := float(a.get_width() * a.get_height())
	return {"share_over_16": snappedf(big / n, 0.0001), "mean": snappedf(total / n, 0.00001)}


func _run() -> void:
	if not _materials():
		print("WORLD_PROXIES_CHECK " + JSON.stringify({"error": "Streaming Lab's textures missing (benchmarks/scripts/make_streaming_textures.py)"}))
		quit(1)
		return
	root3d = Node3D.new()
	root.add_child(root3d)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.55, 0.7, 0.9)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.45, 0.5, 0.55)
	root3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-45, 35, 0)
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 150.0
	root3d.add_child(sun)
	cam = Camera3D.new()
	cam.far = 1500.0
	root3d.add_child(cam)
	cam.make_current()

	# The world: the ground split into regions of a cell, the cells saved as scenes.
	var t0 := Time.get_ticks_msec()
	var level := Node3D.new()
	level.name = "Level"
	root3d.add_child(level)
	var terrain = _terrain()
	level.add_child(terrain)
	await _settle(2)
	ground_streamer = ClassDB.instantiate("FellowWorldStreamer3D")
	ground_streamer.name = "GroundStreamer"
	level.add_child(ground_streamer)
	terrain.split_into_regions(64, out_dir.path_join("regions"), ground_streamer)
	streamer = ClassDB.instantiate("FellowWorldStreamer3D")
	streamer.name = "Streamer"
	level.add_child(streamer)
	DirAccess.make_dir_recursive_absolute(out_dir.path_join("cells"))
	for x in n_cells:
		for z in n_cells:
			var content := _make_cell(x, z, terrain)
			var ps := PackedScene.new()
			ps.pack(content)
			var path := out_dir.path_join("cells").path_join("cell_%02d_%02d.scn" % [x, z])
			ResourceSaver.save(ps, path)
			content.free()
			var cell: Node3D = ClassDB.instantiate("FellowStreamingCell3D")
			cell.name = "cell_%02d_%02d" % [x, z]
			cell.position = Vector3((x + 0.5) * CELL, 0, (z + 0.5) * CELL)
			cell.set("scene_path", path)
			cell.set("bounds", AABB(Vector3(-CELL * 0.5, -20.0, -CELL * 0.5), Vector3(CELL, 70.0, CELL)))
			streamer.add_child(cell)
	terrain.queue_free()
	result["build_ms"] = Time.get_ticks_msec() - t0
	var w := n_cells * CELL
	cam.position = Vector3(20, 45, 20)
	cam.look_at(Vector3(w * 0.75, 0, w * 0.75))

	# A. Everything loaded.
	for s in [streamer, ground_streamer]:
		s.load_distance = 100000.0
		s.load_now()
	await _wait_settled()
	var a := await _shot("a_all_loaded")
	result["a_all_loaded"] = _stats()

	# B. Streamed, nothing beyond the distance (the cells loaded from afar: none kept by the unload margin).
	var view := cam.transform
	for s in [streamer, ground_streamer]:
		s.load_distance = float(_arg("load_distance", "160"))
	cam.position = Vector3(-100000, 0, -100000)
	await _settle(90)
	cam.transform = view
	await _wait_settled()
	result["b_streamed"] = _stats()
	result["b_streamed"]["vs_a"] = _diff(a, await _shot("b_streamed"))

	# C. Proxies baked for the cells and the regions.
	t0 = Time.get_ticks_msec()
	result["bake_regions"] = ground_streamer.bake_proxies(out_dir.path_join("proxies_ground"), {"terrain_quads": 16})
	result["bake_cells"] = streamer.bake_proxies(out_dir.path_join("proxies"), {})
	result["bake_ms"] = Time.get_ticks_msec() - t0
	await _wait_settled()
	result["c_proxies"] = _stats()
	result["c_proxies"]["vs_a"] = _diff(a, await _shot("c_proxies"))

	# D. The large meshes streamed too: the cells' scenes again with stand-ins, the full meshes near. (The cells
	# unloaded first: the camera away from the world.)
	cam.position = Vector3(-100000, 0, -100000)
	await _settle(90)
	result["bake_stream"] = streamer.bake_proxies(out_dir.path_join("proxies_stream"), {"stream_meshes": true})
	streamer.mesh_distance = float(_arg("mesh_distance", "70"))
	cam.transform = view
	await _wait_settled()
	result["d_streamed_meshes"] = _stats()
	result["d_streamed_meshes"]["vs_a"] = _diff(a, await _shot("d_streamed_meshes"))
	result["d_streamed_meshes"]["streamer"] = streamer.get_stats()

	# E. A flight across the world at 30 m/s, low, with the full meshes whenever their cells are loaded and then with
	# them near only: the meshes' swaps on the way, the memory, the frames.
	streamer.mesh_distance = 0.0
	result["e_flight_full"] = await _flight(w)
	streamer.mesh_distance = float(_arg("mesh_distance", "70"))
	result["e_flight_streamed"] = await _flight(w)
	print("WORLD_PROXIES_CHECK " + JSON.stringify(result))
	quit()


func _flight(w: float) -> Dictionary:
	var before: Dictionary = streamer.get_stats()
	cam.position = Vector3(-100000, 0, -100000)
	await _settle(90)
	var from := Vector3(20, 12, 20)
	var to := Vector3(w - 20, 12, w - 20)
	cam.position = from
	cam.look_at(from + (to - from).normalized() + Vector3(0, -0.08, 0))
	await _wait_settled()
	var buffer_max := 0.0
	var buffer_sum := 0.0
	var times := []
	var t_prev := Time.get_ticks_usec()
	var n := int(from.distance_to(to) / 0.5)
	for i in n:
		var p := from.lerp(to, float(i) / n)
		cam.position = p
		cam.look_at(p + (to - from).normalized() + Vector3(0, -0.08, 0))
		await process_frame
		var t := Time.get_ticks_usec()
		times.append(t - t_prev)
		t_prev = t
		var b := Performance.get_monitor(Performance.RENDER_BUFFER_MEM_USED) / 1048576.0
		buffer_max = maxf(buffer_max, b)
		buffer_sum += b
	var order := range(n)
	order.sort_custom(func(x, y): return times[x] > times[y])
	var worst := []
	for k in 5:
		worst.append([order[k], snappedf(times[order[k]] / 1000.0, 0.1)])
	var sorted := times.duplicate()
	sorted.sort()
	var st: Dictionary = streamer.get_stats()
	var loads: int = st.get("mesh_loads", 0) - before.get("mesh_loads", 0)
	var releases: int = st.get("mesh_releases", 0) - before.get("mesh_releases", 0)
	return {
		"frames": n,
		"buffer_mb_max": snappedf(buffer_max, 0.1),
		"buffer_mb_mean": snappedf(buffer_sum / n, 0.1),
		"frame_ms_median": snappedf(sorted[n / 2] / 1000.0, 0.1),
		"frame_ms_p99": snappedf(sorted[int(n * 0.99)] / 1000.0, 0.1),
		"worst_frames": worst,
		"mesh_loads": loads,
		"mesh_releases": releases,
		"mesh_swap_usec_mean": snappedf(float(st.get("mesh_swap_usec", 0) - before.get("mesh_swap_usec", 0)) / maxf(1.0, loads + releases), 0.1),
		"mesh_swap_usec_max_so_far": st.get("mesh_swap_usec_max", 0),
		"cell_loads": st.get("loads", 0) - before.get("loads", 0),
		"activation_usec_max_so_far": st.get("activation_usec_max", 0),
	}
