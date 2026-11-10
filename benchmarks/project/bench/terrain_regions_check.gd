extends SceneTree
## FellowEngine milestone 65 check (decision 0173): a FellowTerrain3D split into regions streamed with the world's cells.
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 640x360 -s res://bench/terrain_regions_check.gd
##       -- --out=<dir> [--size=1025] [--region=256] [--frames=12] [--load_distance=450]
##
## The same terrain (noise, three layers) whole and split into regions of --region quads, each a streaming cell of a
## FellowWorldStreamer3D: the same views over a red sky (the share of the ground's pixels where the sky shows through:
## cracks along the regions' edges), the pictures' difference from the whole's; heights on both sides of every shared
## edge; a raise and a smoothing brush across an edge (the edges equal after); a flight across the map (the cells
## loaded and freed, the frames' longest, the activations' time, the terrain's texture memory). One JSON line:
## TERRAIN_REGIONS_CHECK {...}.

var args := {}
var out_dir := ""
var result := {}
var size := 1025
var region := 256
var frames := 12
var root3d: Node3D
var cam: Camera3D
var views := []


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://terrain_regions_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	size = int(_arg("size", "1025"))
	region = int(_arg("region", "256"))
	frames = int(_arg("frames", "12"))
	_run.call_deferred()


func _height(x: float, z: float, noise: FastNoiseLite) -> float:
	var n := noise.get_noise_2d(x, z)
	var ridge := 1.0 - absf(noise.get_noise_2d(x * 0.5 + 300.0, z * 0.5 - 200.0))
	return n * 25.0 + ridge * ridge * 40.0 + 0.02 * x


func _make_terrain():
	var noise := FastNoiseLite.new()
	noise.seed = 65
	noise.frequency = 0.004
	noise.fractal_octaves = 5
	var data := PackedFloat32Array()
	data.resize(size * size)
	for z in size:
		for x in size:
			data[z * size + x] = _height(x, z, noise)
	var splat := Image.create(size, size, false, Image.FORMAT_RGBA8)
	for z in size:
		for x in size:
			var hx := data[z * size + mini(x + 1, size - 1)] - data[z * size + maxi(x - 1, 0)]
			var hz := data[mini(z + 1, size - 1) * size + x] - data[maxi(z - 1, 0) * size + x]
			var slope := sqrt(hx * hx + hz * hz) * 0.5
			var rock := clampf((slope - 0.45) * 4.0, 0.0, 1.0)
			var snow := clampf((data[z * size + x] - 55.0) * 0.1, 0.0, 1.0) * (1.0 - rock)
			splat.set_pixel(x, z, Color(rock, snow, 0.0, 0.0))
	var t = ClassDB.instantiate("FellowTerrain3D")
	t.name = "Ground"
	var layers := []
	for c in [Color(0.33, 0.48, 0.24), Color(0.5, 0.48, 0.45), Color(0.92, 0.94, 0.97)]:
		var l = ClassDB.instantiate("FellowTerrainLayer")
		l.albedo_color = c
		layers.append(l)
	t.layers = layers
	t.heightmap = Image.create_from_data(size, size, false, Image.FORMAT_RF, data.to_byte_array())
	t.splat_map = splat
	return t


func _world() -> void:
	root3d = Node3D.new()
	root.add_child(root3d)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(1, 0, 0)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.35, 0.38, 0.42)
	root3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40, 35, 0)
	root3d.add_child(sun)
	cam = Camera3D.new()
	cam.far = 3000.0
	root3d.add_child(cam)
	cam.make_current()


func _settle(n: int) -> void:
	for i in n:
		await process_frame


# The views (position, target): along an edge, onto a corner where four regions meet, straight down on an edge, low
# across the map's middle.
func _views() -> Array:
	var e := float(region)
	return [
		["edge_along", Vector3(e - 25, 75, e - 60), Vector3(e, 20, e + 160)],
		["corner", Vector3(e * 2 + 40, 55, e * 2 + 70), Vector3(e * 2, 25, e * 2)],
		["down", Vector3(e, 140, e * 2 + 40), Vector3(e, 0, e * 2 + 40.01)],
		["low", Vector3(70, 55, 70), Vector3(600, 15, 640)],
	]


func _capture(prefix: String) -> Dictionary:
	var out := {}
	for v in _views():
		cam.position = v[1]
		cam.look_at(v[2], Vector3.UP if absf((v[2] - v[1]).normalized().y) < 0.99 else Vector3.FORWARD)
		await _settle(frames)
		await RenderingServer.frame_post_draw
		var img := root.get_texture().get_image()
		img.save_png(out_dir.path_join("%s_%s.png" % [prefix, v[0]]))
		# Below the horizon every pixel should be ground (the camera looks down): red ones are cracks.
		var red := 0
		var below := 0
		for y in img.get_height():
			for x in img.get_width():
				var ray := cam.project_ray_normal(Vector2(x, y))
				if ray.y > -0.02:
					continue
				below += 1
				var c := img.get_pixel(x, y)
				if c.r > 0.9 and c.g < 0.1 and c.b < 0.1:
					red += 1
		out[v[0]] = {"red": red, "below": below}
		views.append(img)
	return out


func _run() -> void:
	_world()
	# 1. Whole.
	var whole = _make_terrain()
	root3d.add_child(whole)
	var t0 := Time.get_ticks_usec()
	await _settle(2)
	result["whole_build_ms"] = (Time.get_ticks_usec() - t0) / 1000.0
	result["whole"] = await _capture("whole")
	var whole_pictures := views.duplicate()
	views.clear()
	result["whole_texture_mb"] = snappedf(Performance.get_monitor(Performance.RENDER_TEXTURE_MEM_USED) / 1048576.0, 0.1)
	result["whole_terrain_textures_mb"] = snappedf(size * size * (4.0 + 2.0 + 4.0) * 4.0 / 3.0 / 1048576.0, 0.1)

	# 2. Split into regions, each a streaming cell.
	var streamer = ClassDB.instantiate("FellowWorldStreamer3D")
	streamer.name = "Streamer"
	streamer.load_distance = float(_arg("load_distance", "450"))
	root3d.add_child(streamer)
	t0 = Time.get_ticks_usec()
	var cells: Array = whole.split_into_regions(region, out_dir.path_join("regions"), streamer)
	result["split_ms"] = (Time.get_ticks_usec() - t0) / 1000.0
	result["cells"] = cells.size()
	var region_bytes := 0
	for f in DirAccess.get_files_at(out_dir.path_join("regions")):
		region_bytes += FileAccess.get_file_as_bytes(out_dir.path_join("regions").path_join(f)).size()
	result["region_files_mb"] = snappedf(region_bytes / 1048576.0, 0.01)
	whole.queue_free()
	await _settle(2)

	# 3. The same views with every region loaded.
	streamer.load_distance = 5000.0
	streamer.load_now()
	await _settle(4)
	var regions := _regions(streamer)
	result["regions_loaded"] = regions.size()
	result["regions"] = await _capture("regions")
	var diffs := {}
	var names := _views().map(func(v): return v[0])
	for i in names.size():
		diffs[names[i]] = _diff(whole_pictures[i], views[i])
	result["regions_vs_whole"] = diffs

	# 4. Heights along every shared edge, from both sides.
	result["edges"] = _edges(regions)

	# 5. Brushes across an edge: raise, then smooth.
	var e := float(region)
	var r00 = _region_at(regions, Vector3(e - 10, 0, e - 10))
	var brush := Vector3(e + 1.5, 0, e * 0.5)
	var before_l: float = r00.get_height(Vector3(e - 3, 0, e * 0.5))
	_height_before_right = _region_at(regions, Vector3(e + 3, 0, e * 0.5)).get_height(Vector3(e + 3, 0, e * 0.5))
	r00.sculpt(brush, 12.0, 3.0, 0) # SCULPT_RAISE through the left region
	var raised := _edges(regions)
	r00.sculpt(brush + Vector3(0, 0, 8), 10.0, 1.0, 2) # SCULPT_SMOOTH
	var smoothed := _edges(regions)
	result["brush"] = {
		"neighbors": r00.get_region_neighbors().size(),
		"left_raised_by": r00.get_height(Vector3(e - 3, 0, e * 0.5)) - before_l,
		"right_raised_by": _region_at(regions, Vector3(e + 3, 0, e * 0.5)).get_height(Vector3(e + 3, 0, e * 0.5)) - _height_before_right,
		"edges_after_raise": raised,
		"edges_after_smooth": smoothed,
	}

	# 6. A flight across the map with the streamer's own distance, from its start with only the cells around it.
	streamer.load_distance = float(_arg("load_distance", "450"))
	cam.position = Vector3(20, 80, 20)
	cam.look_at(Vector3(size - 20, 60, size - 20))
	for i in 900:
		await process_frame
		if streamer.is_settled() and _regions(streamer).size() <= 9 and i > 60:
			break
	result["flight_start_regions"] = _regions(streamer).size()
	result["flight"] = await _flight(streamer)
	print("TERRAIN_REGIONS_CHECK " + JSON.stringify(result))
	quit()


var _height_before_right := 0.0


func _regions(streamer) -> Array:
	var out := []
	for cell in streamer.get_children():
		if cell.has_method("get_content") and cell.get_content():
			out.append(cell.get_content())
	return out


func _region_at(regions: Array, p: Vector3):
	for t in regions:
		var l: Vector3 = t.global_transform.affine_inverse() * p
		var b: AABB = t.get_aabb()
		if l.x >= 0.0 and l.z >= 0.0 and l.x <= b.size.x and l.z <= b.size.z:
			return t
	return null


# For each pair of regions sharing an edge, the largest difference of their heights along it.
func _edges(regions: Array) -> Dictionary:
	var worst := 0.0
	var pairs := 0
	var samples := 0
	for a in regions:
		var ab: AABB = a.get_aabb()
		var ao: Vector3 = a.global_position
		for b in regions:
			if a == b:
				continue
			var bo: Vector3 = b.global_position
			if is_equal_approx(bo.x, ao.x + ab.size.x) and is_equal_approx(bo.z, ao.z):
				pairs += 1
				for i in range(0, region + 1, 3):
					var p := Vector3(bo.x, 0, ao.z + i + 0.37)
					worst = maxf(worst, absf(a.get_height(p) - b.get_height(p)))
					samples += 1
			elif is_equal_approx(bo.z, ao.z + ab.size.z) and is_equal_approx(bo.x, ao.x):
				pairs += 1
				for i in range(0, region + 1, 3):
					var p := Vector3(ao.x + i + 0.37, 0, bo.z)
					worst = maxf(worst, absf(a.get_height(p) - b.get_height(p)))
					samples += 1
	return {"pairs": pairs, "samples": samples, "max_difference_m": worst}


func _diff(a: Image, b: Image) -> Dictionary:
	var differing := 0
	var big := 0
	var cracks := 0
	var total := 0.0
	for y in a.get_height():
		for x in a.get_width():
			var ca := a.get_pixel(x, y)
			var cb := b.get_pixel(x, y)
			var d := maxf(absf(ca.r - cb.r), maxf(absf(ca.g - cb.g), absf(ca.b - cb.b)))
			if d > 0.0:
				differing += 1
				total += d
			if d > 8.0 / 255.0:
				big += 1
			# The sky in the regions' picture where the whole's has ground: a crack along an edge.
			if cb.r > 0.9 and cb.g < 0.1 and cb.b < 0.1 and not (ca.r > 0.9 and ca.g < 0.1 and ca.b < 0.1):
				cracks += 1
	var n := float(a.get_width() * a.get_height())
	return {"share_differing": snappedf(differing / n, 0.0001), "share_over_8": snappedf(big / n, 0.0001), "mean": snappedf(total / n, 0.00001), "sky_only_in_regions": cracks}


func _flight(streamer) -> Dictionary:
	# Diagonally across the map at 30 m/s, 60 m up, looking ahead.
	var from := Vector3(20, 80, 20)
	var to := Vector3(size - 20, 80, size - 20)
	var seconds := from.distance_to(to) / 30.0
	var steps := int(seconds * 60.0)
	var longest := 0.0
	var times := []
	var max_loaded := 0
	var max_tex := 0.0
	var max_region_mb := 0.0
	var longest_at := -1
	var seen := {}
	var entered := []
	for r in _regions(streamer):
		seen[r.get_instance_id()] = true # loaded before the flight
	var s0: Dictionary = streamer.get_stats()
	var last := Time.get_ticks_usec()
	for i in steps + 1:
		cam.position = from.lerp(to, float(i) / steps)
		cam.look_at(cam.position + (to - from).normalized() + Vector3(0, -0.25, 0))
		await process_frame
		var now := Time.get_ticks_usec()
		if (now - last) / 1000.0 > longest:
			longest_at = i
		longest = maxf(longest, (now - last) / 1000.0)
		times.append((now - last) / 1000.0)
		last = now
		var loaded := _regions(streamer)
		max_loaded = maxi(max_loaded, loaded.size())
		for r in loaded:
			if not seen.has(r.get_instance_id()):
				seen[r.get_instance_id()] = true
				entered.append(r.get_stats().get("enter_usec", 0))
		var mb := 0.0
		for r in loaded:
			var s: Vector2i = r.get_stats().get("map_size", Vector2i())
			mb += s.x * s.y * (4.0 + 2.0 + 4.0) * 4.0 / 3.0 / 1048576.0 # heights RF, normals RG8, a splat map RGBA8, their mips
		max_region_mb = maxf(max_region_mb, mb)
		max_tex = maxf(max_tex, Performance.get_monitor(Performance.RENDER_TEXTURE_MEM_USED))
	var s: Dictionary = streamer.get_stats()
	times.sort()
	entered.sort()
	var mean_enter := 0.0
	for x in entered:
		mean_enter += x
	mean_enter /= maxf(1.0, entered.size())
	var enter := {"regions": entered.size(), "mean_usec": int(mean_enter), "max_usec": entered[-1] if entered.size() > 0 else 0,
		"activations_usec_mean": int((s["activation_usec"] - s0["activation_usec"]) / maxf(1.0, s["loads"] - s0["loads"])),
		"loads": s["loads"] - s0["loads"], "releases": s["releases"] - s0["releases"]}
	return {"frames": steps, "longest_frame_ms": snappedf(longest, 0.01), "frame_ms_p50": snappedf(times[times.size() / 2], 0.01),
		"frame_ms_p99": snappedf(times[int(times.size() * 0.99)], 0.01), "frames_over_100ms": times.filter(func(x): return x > 100.0).size(), "longest_at_frame": longest_at,
		"regions_entered": enter, "max_regions_loaded": max_loaded, "max_region_textures_mb": snappedf(max_region_mb, 0.1),
		"max_texture_mb": snappedf(max_tex / 1048576.0, 0.1), "streamer": s}
