extends "res://scenes/slice_lab.gd"
## FRONTIER LAB (milestone 67; generated in code, deterministic, cached): an open world at the scale of an AAA title,
## streamed — the scale test of the readiness assessment's path (section 5, item 6).
##
##   * 4 km x 4 km (--frontier_size=<m>, a multiple of 256: 1024 for a quick world): a FellowTerrain3D at 1 m (heights
##     from noise, flattened for the settlements, a lake carved) with six layers (grass, rock, paving, sand; meadows of
##     grass and forests of trees scattered by the terrain, the trees as impostors past 140 m), split into regions of
##     256 m under a FellowWorldStreamer3D
##   * 1024 cells of 128 m under another streamer: towns (blocks of buildings of 2 m quads, windows of a modular kit as
##     instances, street lights and their posts, props, crates), villages (houses, lights, props, fences) and the wild
##     (rocks, logs); proxies baked for the cells and the regions, the towns' buildings streamed by distance
##   * Slice Lab's systems in the first town: a campfire (gas, a burning log), a crowd, a lake (FellowWater3D) beside
##     it; FellowWeather3D (rain, storms, snow), wind, the sun's day, street lights from dusk to dawn
##
## Generated into res://external/frontier_<size>/ the first time (or with --frontier_generate=1): a few minutes, a few
## hundred MB; needs Streaming Lab's textures (benchmarks/scripts/make_streaming_textures.py). Options:
## --frontier_cells=<m> (the cells' load distance, 450), --frontier_meshes=<m> (the buildings' full meshes, 160),
## --frontier_regions=<m> (the terrain regions' load distance, 1000), --frontier_trees=<m> (the trees' range, 1000),
## --frontier_far=<m> (the camera's far plane, 4000); Slice Lab's --slice_weather, --slice_daytime, --slice_day,
## --slice_crowd, --slice_without=water,weather,fire,crowd.

const F_VERSION := 6
const F_CELL := 128.0
const F_REGION := 256
const F_TEX_DIR := "res://external/streaming"
const F_BLOCK := 32.0
const F_QUAD := 2.0

var world := 4096.0
var fdir := ""
var hn := 0 # height samples per side
var hstep := 1.0 # their spacing: 1 m while generating, 4 m after (heights4.bin)
var heights := PackedFloat32Array()
var sites: Array = [] # {kind, c: Vector2, r, h}
var lake_c := Vector2.ZERO
var lake_r := 140.0
var lake_level := 0.0
var level: Node3D = null
var cell_streamer: Node3D = null
var ground_streamer: Node3D = null
var streamers: Array = [] # main.gd's captures wait for both
var gen_info := {}
var _mats: Array = []
var _shared := {}
var _tris := {}
var _soak_prev := {}


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "frontier"
	world = maxf(1024.0, floorf(float(args.get("frontier_size", "4096")) / F_REGION) * F_REGION)
	fdir = "res://external/frontier_%d" % int(world)
	_day = maxf(10.0, float(args.get("slice_day", "600")))
	_daytime = float(args.get("slice_daytime", "0.35"))
	_weather_seconds = maxf(5.0, float(args.get("slice_weather_seconds", str(SOAK_WEATHER_SECONDS))))
	_fire_seconds = maxf(2.0, float(args.get("slice_fire_seconds", "240")))
	_setup_common()
	camera.far = float(args.get("frontier_far", "4000"))
	_setup_environment()
	env.fog_enabled = true
	env.fog_light_color = Color(0.62, 0.7, 0.8)
	env.fog_density = 0.00025
	env.fog_aerial_perspective = 0.6
	env.fog_sky_affect = 0.3
	if not ClassDB.class_exists("FellowWorldStreamer3D") or not ClassDB.class_exists("FellowTerrain3D"):
		load_error = "Frontier Lab needs FellowEngine (FellowWorldStreamer3D, FellowTerrain3D)."
		return
	var t0 := Time.get_ticks_usec()
	_plan_sites()
	var stamp := fdir.path_join("frontier.json")
	var regenerate := str(args.get("frontier_generate", "0")) == "1"
	var have := FileAccess.file_exists(stamp) and FileAccess.file_exists(fdir.path_join("level.scn"))
	if have:
		var old = JSON.parse_string(FileAccess.get_file_as_string(stamp))
		have = old is Dictionary and int(old.get("version", 0)) == F_VERSION
		if have:
			gen_info = old
	if regenerate or not have:
		_make_heights()
		if not await _generate():
			return
	else:
		_load_heights()
	var gen_ms := (Time.get_ticks_usec() - t0) / 1000.0
	var t1 := Time.get_ticks_usec()
	var ps: PackedScene = load(fdir.path_join("level.scn"))
	if ps == null:
		load_error = "Frontier Lab: %s did not load" % fdir.path_join("level.scn")
		return
	level = ps.instantiate()
	cell_streamer = level.get_node("Cells")
	ground_streamer = level.get_node("Ground")
	cell_streamer.load_distance = float(args.get("frontier_cells", "450"))
	cell_streamer.unload_margin = 50.0
	cell_streamer.mesh_distance = float(args.get("frontier_meshes", "160"))
	cell_streamer.max_loading = 4
	cell_streamer.max_activations_per_frame = 2
	ground_streamer.load_distance = float(args.get("frontier_regions", "1000"))
	ground_streamer.unload_margin = 100.0
	ground_streamer.max_loading = 2
	streamers = [cell_streamer, ground_streamer]
	add_child(level)
	var place_ms := (Time.get_ticks_usec() - t1) / 1000.0
	_build_lights()
	sun.directional_shadow_max_distance = 400.0
	var without := str(args.get("slice_without", "")).replace("+", ",").split(",", false)
	if not without.has("water"):
		_build_water()
	if not without.has("weather"):
		_build_weather()
	if not without.has("fire"):
		_build_fire()
	if not without.has("crowd"):
		_build_crowd()
	_counts["without"] = without
	_define_cameras()
	set_time(0.0)
	info = {"stats": scene_stats(self), "generated": gen_info, "generate_or_check_ms": gen_ms, "place_ms": place_ms,
		"counts": _counts, "world_m": world, "sites": sites.size(),
		"notes": "Frontier: %d m open world, terrain regions of %d m and cells of %d m streamed with proxies; towns, villages, the wild; Slice Lab's systems in the first town." % [int(world), F_REGION, int(F_CELL)]}


# ---------------------------------------------------------------- the land

func _plan_sites() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 67
	var half := world * 0.5
	var s := world / 4096.0
	sites.clear()
	# The first town (the slice's systems, the cameras' start), south-west of the centre, its lake to its east.
	var r0 := clampf(230.0 * maxf(s, 0.5), 160.0, 230.0)
	sites.append({"kind": "town", "c": Vector2(-0.22 * world, -0.16 * world), "r": r0})
	lake_r = clampf(140.0 * maxf(s, 0.5), 90.0, 140.0)
	lake_c = sites[0]["c"] + Vector2(r0 + lake_r + 70.0, 40.0)
	var towns := maxi(1, int(round(12.0 * s * s)))
	var villages := maxi(2, int(round(40.0 * s * s)))
	for kind in ["town", "village"]:
		var want := towns if kind == "town" else villages
		var placed := 1 if kind == "town" else 0
		var tries := 0
		while placed < want and tries < 4000:
			tries += 1
			var r := rng.randf_range(160.0, 260.0) if kind == "town" else rng.randf_range(45.0, 85.0)
			var c := Vector2(rng.randf_range(-half + r + 64.0, half - r - 64.0), rng.randf_range(-half + r + 64.0, half - r - 64.0))
			var ok := c.distance_to(lake_c) > lake_r + r + 60.0
			for st in sites:
				if c.distance_to(st["c"]) < float(st["r"]) + r + 120.0:
					ok = false
					break
			if ok:
				sites.append({"kind": kind, "c": c, "r": r})
				placed += 1
	_counts["towns"] = towns
	_counts["villages"] = sites.size() - towns


func _raw_height(n1: FastNoiseLite, n2: FastNoiseLite, x: float, z: float) -> float:
	var a := n1.get_noise_2d(x, z)
	var r := 1.0 - absf(n2.get_noise_2d(x, z))
	return a * 90.0 + r * r * 22.0


func _make_heights() -> void:
	var t0 := Time.get_ticks_usec()
	hn = int(world) + 1
	heights.resize(hn * hn)
	var n1 := FastNoiseLite.new()
	n1.seed = 67
	n1.frequency = 0.0009
	n1.fractal_octaves = 5
	var n2 := FastNoiseLite.new()
	n2.seed = 671
	n2.frequency = 0.0035
	n2.fractal_octaves = 3
	var half := world * 0.5
	var k := 0
	for j in hn:
		var z := j - half
		for i in hn:
			var x := i - half
			var a := n1.get_noise_2d(x, z)
			var r := 1.0 - absf(n2.get_noise_2d(x, z))
			heights[k] = a * 90.0 + r * r * 22.0
			k += 1
	# The settlements flattened to their centre's height, over 70 m around them.
	for st in sites:
		var c: Vector2 = st["c"]
		var r: float = st["r"]
		st["h"] = _raw_height(n1, n2, c.x, c.y)
		_flatten(c, r, r + 70.0, func(_d: float) -> float: return st["h"])
	# The lake: a bowl 7 m deep under a level 3 m below its centre's ground, its shore rising gently.
	lake_level = _raw_height(n1, n2, lake_c.x, lake_c.y) - 3.0
	_flatten(lake_c, lake_r, lake_r + 90.0, func(d: float) -> float:
		return lake_level - 7.0 * (1.0 - (d / lake_r) * (d / lake_r)) if d < lake_r else lake_level + (d - lake_r) * 0.08)
	gen_info["heights_ms"] = (Time.get_ticks_usec() - t0) / 1000.0
	# Every fourth sample kept for later runs (the cameras', the square's and the lake's heights).
	var n4 := int(world / 4.0) + 1
	var h4 := PackedFloat32Array()
	h4.resize(n4 * n4)
	for j in n4:
		for i in n4:
			h4[j * n4 + i] = heights[(j * 4) * hn + i * 4]
	DirAccess.make_dir_recursive_absolute(fdir)
	var f := FileAccess.open(fdir.path_join("heights4.bin"), FileAccess.WRITE)
	if f:
		f.store_buffer(h4.to_byte_array())


func _load_heights() -> void:
	hn = int(world / 4.0) + 1
	hstep = 4.0
	heights = FileAccess.get_file_as_bytes(fdir.path_join("heights4.bin")).to_float32_array()
	if heights.size() != hn * hn:
		hstep = 1.0
		_make_heights()
	lake_level = float(gen_info.get("lake_level", 0.0))
	for i in sites.size():
		sites[i]["h"] = height(sites[i]["c"].x, sites[i]["c"].y)


## Blends the heights towards target(d) within `inner` of `c`, back to the land's at `outer`.
func _flatten(c: Vector2, inner: float, outer: float, target: Callable) -> void:
	var half := world * 0.5
	var i0 := maxi(0, int(c.x + half - outer))
	var i1 := mini(hn - 1, int(c.x + half + outer) + 1)
	var j0 := maxi(0, int(c.y + half - outer))
	var j1 := mini(hn - 1, int(c.y + half + outer) + 1)
	for j in range(j0, j1 + 1):
		for i in range(i0, i1 + 1):
			var d := Vector2(i - half, j - half).distance_to(c)
			if d >= outer:
				continue
			var w := 1.0 if d <= inner else smoothstep(outer, inner, d)
			var k := j * hn + i
			heights[k] = lerpf(heights[k], target.call(d), w)


func height(x: float, z: float) -> float:
	var half := world * 0.5
	var fx := clampf((x + half) / hstep, 0.0, hn - 1.001)
	var fz := clampf((z + half) / hstep, 0.0, hn - 1.001)
	var i := int(fx)
	var j := int(fz)
	var u := fx - i
	var v := fz - j
	var k := j * hn + i
	return lerpf(lerpf(heights[k], heights[k + 1], u), lerpf(heights[k + hn], heights[k + hn + 1], u), v)


func normal_at(x: float, z: float) -> Vector3:
	return Vector3(height(x - 2.0, z) - height(x + 2.0, z), 4.0, height(x, z - 2.0) - height(x, z + 2.0)).normalized()


func _site_at(p: Vector2, margin := 0.0) -> Dictionary:
	for st in sites:
		if p.distance_to(st["c"]) < float(st["r"]) + margin:
			return st
	return {}


# ---------------------------------------------------------------- generation

func _generate() -> bool:
	var t0 := Time.get_ticks_usec()
	DirAccess.make_dir_recursive_absolute(fdir.path_join("shared"))
	DirAccess.make_dir_recursive_absolute(fdir.path_join("cells"))
	for i in 48:
		var a = load(F_TEX_DIR.path_join("m%02d_albedo.ctex" % i))
		var nm = load(F_TEX_DIR.path_join("m%02d_normal.ctex" % i))
		if a == null or nm == null:
			load_error = "Frontier Lab: Streaming Lab's textures missing in %s (benchmarks/scripts/make_streaming_textures.py)" % F_TEX_DIR
			return false
		var m := StandardMaterial3D.new()
		m.albedo_texture = a
		m.normal_enabled = true
		m.normal_texture = nm
		m.roughness = 0.8
		m.uv1_scale = Vector3(0.25, 0.25, 1.0)
		_mats.append(_save_shared(m, "building_%02d.tres" % i))
	_shared["detail"] = _save_shared(_detail_texture(), "detail.res")
	_make_shared()
	var gen := Node3D.new()
	gen.name = "Generating"
	add_child(gen)
	level = Node3D.new()
	level.name = "FrontierLevel"
	gen.add_child(level)
	ground_streamer = ClassDB.instantiate("FellowWorldStreamer3D")
	ground_streamer.name = "Ground"
	ground_streamer.streaming = false
	level.add_child(ground_streamer)
	cell_streamer = ClassDB.instantiate("FellowWorldStreamer3D")
	cell_streamer.name = "Cells"
	cell_streamer.streaming = false
	level.add_child(cell_streamer)
	# The terrain, its foliage's impostors baked, split into regions.
	var t1 := Time.get_ticks_usec()
	var terrain = _make_terrain()
	gen.add_child(terrain)
	await get_tree().process_frame
	var queued: int = terrain.bake_foliage_impostors(fdir.path_join("impostors"), 8, 128)
	if queued > 0:
		await terrain.foliage_impostors_baked
	var foliage: Array = terrain.foliage
	for i in foliage.size():
		foliage[i] = _save_shared(foliage[i], "foliage_%d.tres" % i)
	terrain.foliage = foliage
	gen_info["terrain_ms"] = (Time.get_ticks_usec() - t1) / 1000.0
	t1 = Time.get_ticks_usec()
	var regions: Array = terrain.split_into_regions(F_REGION, fdir.path_join("regions"), ground_streamer)
	gen_info["regions"] = regions.size()
	gen_info["split_ms"] = (Time.get_ticks_usec() - t1) / 1000.0
	terrain.queue_free()
	await get_tree().process_frame
	# The cells.
	t1 = Time.get_ticks_usec()
	var n := int(world / F_CELL)
	var half := world * 0.5
	var kinds := {"town": 0, "village": 0, "wild": 0}
	var totals := {"buildings": 0, "windows": 0, "lights": 0, "props": 0, "rocks": 0, "nodes": 0, "triangles": 0, "instanced_triangles": 0}
	for cz in n:
		for cx in n:
			var c := Vector2(-half + (cx + 0.5) * F_CELL, -half + (cz + 0.5) * F_CELL)
			var made := _make_cell(c, cx * 1000 + cz, totals)
			kinds[made[1]] += 1
			var content: Node3D = made[0]
			var ps := PackedScene.new()
			ps.pack(content)
			var path := fdir.path_join("cells").path_join("c_%02d_%02d.scn" % [cx, cz])
			ResourceSaver.save(ps, path)
			content.free()
			var cell: Node3D = ClassDB.instantiate("FellowStreamingCell3D")
			cell.name = "c_%02d_%02d" % [cx, cz]
			cell.position = Vector3(c.x, 0.0, c.y)
			cell.set("scene_path", path)
			var hr := _height_range(c)
			cell.set("bounds", AABB(Vector3(-F_CELL * 0.5, hr.x - 4.0, -F_CELL * 0.5), Vector3(F_CELL, hr.y - hr.x + 54.0, F_CELL)))
			cell_streamer.add_child(cell)
		await get_tree().process_frame
	gen_info["cells"] = n * n
	gen_info["cell_kinds"] = kinds
	gen_info["content"] = totals
	gen_info["cells_ms"] = (Time.get_ticks_usec() - t1) / 1000.0
	# The proxies: the regions' (the far ground) and the cells' (the towns' buildings streamed by distance).
	t1 = Time.get_ticks_usec()
	gen_info["proxies_ground"] = ground_streamer.bake_proxies(fdir.path_join("proxies_ground"), {"terrain_quads": 32, "owner": level})
	gen_info["proxies_cells"] = cell_streamer.bake_proxies(fdir.path_join("proxies_cells"), {"stream_meshes": true, "mesh_min_triangles": 4000, "owner": level})
	gen_info["proxies_ms"] = (Time.get_ticks_usec() - t1) / 1000.0
	ground_streamer.streaming = true
	cell_streamer.streaming = true
	# The level's file: the streamers, their cells and the proxies (the content the split left in the tree stays out).
	for st in [ground_streamer, cell_streamer]:
		st.owner = level
		for cell in st.get_children():
			cell.owner = level
			for ch in cell.get_children():
				if ch.has_meta("_fellow_cell_proxy"):
					ch.owner = level
	var lps := PackedScene.new()
	lps.pack(level)
	ResourceSaver.save(lps, fdir.path_join("level.scn"))
	gen.queue_free()
	level = null
	gen_info["version"] = F_VERSION
	gen_info["world_m"] = world
	gen_info["lake_level"] = lake_level
	gen_info["generate_ms"] = (Time.get_ticks_usec() - t0) / 1000.0
	gen_info["disk_mb"] = snappedf(_dir_bytes(fdir) / 1048576.0, 0.1)
	var f := FileAccess.open(fdir.path_join("frontier.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(gen_info, "\t"))
	f = null
	print("[frontier] generated: ", JSON.stringify(gen_info))
	await get_tree().process_frame
	return true


func _dir_bytes(path: String) -> int:
	var total := 0
	for f in DirAccess.get_files_at(path):
		var fa := FileAccess.open(path.path_join(f), FileAccess.READ)
		if fa:
			total += fa.get_length()
	for d in DirAccess.get_directories_at(path):
		total += _dir_bytes(path.path_join(d))
	return total


func _save_shared(r: Resource, file: String) -> Resource:
	var path := fdir.path_join("shared").path_join(file)
	ResourceSaver.save(r, path)
	r.take_over_path(path) # referenced by the files saved after it, not copied into them
	return r


func _height_range(c: Vector2) -> Vector2:
	var lo := INF
	var hi := -INF
	for j in 9:
		for i in 9:
			var h := height(c.x - F_CELL * 0.5 + i * 16.0, c.y - F_CELL * 0.5 + j * 16.0)
			lo = minf(lo, h)
			hi = maxf(hi, h)
	return Vector2(lo, hi)


func _make_terrain() -> Node3D:
	var t0 := Time.get_ticks_usec()
	var half := world * 0.5
	var sn := int(world / 4.0) + 1
	var b1 := PackedByteArray()
	var b2 := PackedByteArray()
	b1.resize(sn * sn * 4)
	b2.resize(sn * sn * 4)
	var meadow := FastNoiseLite.new()
	meadow.seed = 672
	meadow.frequency = 0.004
	var forest := FastNoiseLite.new()
	forest.seed = 673
	forest.frequency = 0.0022
	for j in sn:
		for i in sn:
			var x := -half + i * 4.0
			var z := -half + j * 4.0
			var p := Vector2(x, z)
			var o := (j * sn + i) * 4
			var nrm := normal_at(x, z)
			var st := _site_at(p, 10.0)
			var h := height(x, z)
			if not st.is_empty():
				b1[o + 1] = 255 if st["kind"] == "town" else 150 # paving (a village's is half its ground's)
			elif h < lake_level + 1.2 and p.distance_to(lake_c) < lake_r + 70.0:
				b1[o + 2] = 255 # sand at the shore
			elif nrm.y < 0.82:
				b1[o] = 255 # rock on the slopes
			elif _site_at(p, 40.0).is_empty() and p.distance_to(lake_c) > lake_r + 40.0 and forest.get_noise_2d(x, z) > -0.05:
				b2[o] = 255 # forest
			elif meadow.get_noise_2d(x, z) > 0.1:
				b1[o + 3] = 255 # meadow
	var t = ClassDB.instantiate("FellowTerrain3D")
	t.name = "Terrain"
	t.position = Vector3(-half, 0.0, -half)
	t.vertex_spacing = 1.0
	var layers := []
	var spec := [[Color(0.27, 0.36, 0.17), 12, 6.0], [Color(0.42, 0.4, 0.37), 30, 8.0], [Color(0.4, 0.39, 0.37), 5, 3.0],
		[Color(0.46, 0.42, 0.33), 40, 4.0], [Color(0.3, 0.4, 0.18), 12, 6.0], [Color(0.22, 0.3, 0.14), 12, 6.0]]
	for k in spec.size():
		var l = ClassDB.instantiate("FellowTerrainLayer")
		l.albedo_color = (spec[k][0] as Color).linear_to_srgb()
		l.albedo_texture = _shared["detail"]
		l.normal_texture = _mats[spec[k][1]].normal_texture
		l.texture_size = spec[k][2]
		l.roughness = 0.9
		layers.append(_save_shared(l, "layer_%d.tres" % k))
	layers[1].triplanar = true
	t.layers = layers
	t.heightmap = Image.create_from_data(hn, hn, false, Image.FORMAT_RF, heights.to_byte_array())
	t.splat_map = Image.create_from_data(sn, sn, false, Image.FORMAT_RGBA8, b1)
	t.splat_map_2 = Image.create_from_data(sn, sn, false, Image.FORMAT_RGBA8, b2)
	var trees = ClassDB.instantiate("FellowTerrainFoliage")
	trees.mesh = _save_shared(_tree_mesh(), "tree.res")
	trees.layer = 5
	trees.min_weight = 0.3
	trees.density = 0.012
	trees.range = float(args.get("frontier_trees", "1000"))
	trees.fade_distance = 40.0
	trees.impostor_distance = 140.0
	trees.scale_min = 0.8
	trees.scale_max = 1.4
	trees.sink = 0.05
	trees.seed = 674
	var grass = ClassDB.instantiate("FellowTerrainFoliage")
	grass.mesh = _save_shared(_grass_mesh(), "grass.res")
	grass.layer = 4
	grass.min_weight = 0.5
	grass.density = 1.2
	grass.range = 80.0
	grass.fade_distance = 12.0
	grass.scale_min = 0.8
	grass.scale_max = 1.2
	grass.sink = 0.05
	grass.seed = 675
	t.foliage = [trees, grass]
	gen_info["splat_ms"] = (Time.get_ticks_usec() - t0) / 1000.0
	return t


func _make_shared() -> void:
	var dark := StandardMaterial3D.new()
	dark.albedo_color = Color(0.15, 0.15, 0.16)
	dark.roughness = 0.6
	_save_shared(dark, "dark.tres")
	var wood := StandardMaterial3D.new()
	wood.albedo_color = Color(0.36, 0.25, 0.16)
	wood.roughness = 0.85
	_save_shared(wood, "wood.tres")
	var glass := StandardMaterial3D.new()
	glass.albedo_color = Color(0.12, 0.16, 0.2)
	glass.metallic = 0.6
	glass.roughness = 0.12
	_save_shared(glass, "glass.tres")
	var frame := StandardMaterial3D.new()
	frame.albedo_color = Color(0.82, 0.8, 0.76)
	frame.roughness = 0.7
	_save_shared(frame, "frame.tres")
	var stone := StandardMaterial3D.new()
	stone.albedo_texture = _shared["detail"]
	stone.normal_enabled = true
	stone.normal_texture = _mats[30].normal_texture
	stone.albedo_color = Color(0.5, 0.48, 0.45)
	stone.roughness = 0.9
	stone.uv1_triplanar = true
	stone.uv1_scale = Vector3(0.5, 0.5, 0.5)
	_save_shared(stone, "stone.tres")
	# A window of a modular kit: a frame of four bars, a sill, the glass (two surfaces, 62 triangles).
	var win := ArrayMesh.new()
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for b in [[Vector3(0, 0.85, 0), Vector3(1.4, 0.12, 0.16)], [Vector3(0, -0.85, 0), Vector3(1.4, 0.12, 0.16)],
			[Vector3(-0.65, 0, 0), Vector3(0.12, 1.6, 0.16)], [Vector3(0.65, 0, 0), Vector3(0.12, 1.6, 0.16)],
			[Vector3(0, -0.95, 0.1), Vector3(1.6, 0.08, 0.3)]]:
		var bm := BoxMesh.new()
		bm.size = b[1]
		st.append_from(bm, 0, Transform3D(Basis(), b[0]))
	st.set_material(frame)
	st.commit(win)
	var glass_q := QuadMesh.new()
	glass_q.size = Vector2(1.2, 1.6)
	var st2 := SurfaceTool.new()
	st2.append_from(glass_q, 0, Transform3D(Basis(), Vector3(0, 0, -0.02)))
	st2.set_material(glass)
	st2.commit(win)
	_shared["window"] = _save_shared(win, "window.res")
	var post := CylinderMesh.new()
	post.top_radius = 0.08
	post.bottom_radius = 0.1
	post.height = 5.0
	post.radial_segments = 8
	post.material = dark
	_shared["post"] = _save_shared(_baked(post), "post.res")
	var barrel := CylinderMesh.new()
	barrel.top_radius = 0.3
	barrel.bottom_radius = 0.3
	barrel.height = 0.9
	barrel.radial_segments = 14
	barrel.material = wood
	_shared["barrel"] = _save_shared(_baked(barrel), "barrel.res")
	var bench := BoxMesh.new()
	bench.size = Vector3(1.8, 0.45, 0.5)
	bench.material = wood
	_shared["bench"] = _save_shared(_baked(bench), "bench.res")
	var crate := BoxMesh.new()
	crate.size = Vector3(1, 1, 1)
	crate.material = wood
	_shared["crate"] = _save_shared(_baked(crate), "crate.res")
	var fence := BoxMesh.new()
	fence.size = Vector3(0.12, 1.2, 0.12)
	fence.material = wood
	_shared["fence"] = _save_shared(_baked(fence), "fence.res")
	var log_m := CylinderMesh.new()
	log_m.top_radius = 0.22
	log_m.bottom_radius = 0.25
	log_m.height = 4.0
	log_m.radial_segments = 12
	log_m.material = wood
	_shared["log"] = _save_shared(_baked(log_m), "log.res")
	_shared["rock"] = _save_shared(_rock_mesh(stone), "rock.res")


## The terrain's and the rocks' detail: a grey tiling noise (0.72-1.0) the layers' colours tint (Streaming Lab's
## textures are coloured patterns for the buildings), 512 x 512 with mipmaps.
func _detail_texture() -> Texture2D:
	var nz := FastNoiseLite.new()
	nz.seed = 677
	nz.frequency = 0.02
	nz.fractal_octaves = 4
	var img := nz.get_seamless_image(512, 512)
	img.convert(Image.FORMAT_RGB8)
	var data := img.get_data()
	for i in data.size():
		data[i] = int(184.0 + 71.0 * data[i] / 255.0)
	img = Image.create_from_data(512, 512, false, Image.FORMAT_RGB8, data)
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)


## A primitive mesh as an ArrayMesh (as a game's assets are): a PrimitiveMesh shared by cells loading on several threads
## builds itself lazily, and a material set meanwhile meets a mesh without its surface yet (an error, milestone 67).
func _baked(m: PrimitiveMesh) -> ArrayMesh:
	var am := ArrayMesh.new()
	am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, m.get_mesh_arrays())
	am.surface_set_material(0, m.material)
	return am


## A rock: a sphere of 32 x 16 segments (about 1 000 triangles) pushed in and out by noise.
func _rock_mesh(mat: Material) -> ArrayMesh:
	var sm := SphereMesh.new()
	sm.radius = 1.0
	sm.height = 1.4
	sm.radial_segments = 32
	sm.rings = 16
	var arrays := sm.get_mesh_arrays()
	var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var nz := FastNoiseLite.new()
	nz.seed = 676
	nz.frequency = 1.3
	for i in verts.size():
		var v := verts[i]
		verts[i] = v * (1.0 + 0.28 * nz.get_noise_3dv(v * 1.7))
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = null
	arrays[Mesh.ARRAY_TANGENT] = null
	var st := SurfaceTool.new()
	st.create_from_arrays(arrays)
	st.generate_normals()
	st.generate_tangents()
	st.set_material(mat)
	return st.commit()


## A cell's content (its root at the cell's centre, at height 0): [Node3D, kind].
func _make_cell(c: Vector2, p_seed: int, totals: Dictionary) -> Array:
	var rng := RandomNumberGenerator.new()
	rng.seed = 67000 + p_seed
	var root := Node3D.new()
	root.name = "cell"
	var st := _site_at(c, F_CELL * 0.5)
	var kind := "wild" if st.is_empty() else String(st["kind"])
	if kind == "town":
		_make_town_cell(root, c, st, rng, totals)
	elif kind == "village":
		_make_village_cell(root, c, st, rng, totals)
	else:
		_make_wild_cell(root, c, rng, totals)
	totals["nodes"] += _count_nodes(root)
	return [root, kind]


func _count_nodes(n: Node) -> int:
	var k := 1
	for ch in n.get_children():
		k += _count_nodes(ch)
	return k


func _add(root: Node3D, n: Node3D) -> void:
	root.add_child(n)
	n.owner = root


func _light(root: Node3D, local: Vector3, totals: Dictionary) -> void:
	var post := MeshInstance3D.new()
	post.mesh = _shared["post"]
	post.position = local + Vector3(0, 2.5, 0)
	_add(root, post)
	var l := OmniLight3D.new()
	l.position = local + Vector3(0, 4.8, 0)
	l.omni_range = 18.0
	l.light_color = Color(1.0, 0.8, 0.55)
	l.light_energy = 1.6
	l.add_to_group("frontier_lights", true)
	_add(root, l)
	totals["lights"] += 1


func _prop(root: Node3D, mesh: Mesh, local: Vector3, yaw: float, totals: Dictionary) -> void:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.transform = Transform3D(Basis(Vector3.UP, yaw), local)
	_add(root, mi)
	totals["props"] += 1


func _instances(root: Node3D, p_name: String, mesh: Mesh, xforms: Array, totals: Dictionary) -> void:
	if xforms.is_empty():
		return
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.mesh = mesh
	mm.instance_count = xforms.size()
	for i in xforms.size():
		mm.set_instance_transform(i, xforms[i])
	var mmi := MultiMeshInstance3D.new()
	mmi.name = p_name
	mmi.multimesh = mm
	_add(root, mmi)
	if not _tris.has(mesh):
		var tris := 0
		for s in mesh.get_surface_count():
			var a := mesh.surface_get_arrays(s)
			tris += (a[Mesh.ARRAY_INDEX].size() if a[Mesh.ARRAY_INDEX] != null else a[Mesh.ARRAY_VERTEX].size()) / 3
		_tris[mesh] = tris
	totals["instanced_triangles"] += int(_tris[mesh]) * xforms.size()


## A building of quads of F_QUAD m on its four walls and roof (indexed, compressed), its windows on the walls.
func _building(v: PackedVector3Array, nrm: PackedVector3Array, uv: PackedVector2Array, idx: PackedInt32Array, windows: Array, centre: Vector3, size: Vector3) -> void:
	var h := size * 0.5
	var faces := [
		[Vector3(0, 0, 1), Vector3(1, 0, 0), Vector3(0, 1, 0), size.x, size.y, h.z],
		[Vector3(0, 0, -1), Vector3(-1, 0, 0), Vector3(0, 1, 0), size.x, size.y, h.z],
		[Vector3(1, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0), size.z, size.y, h.x],
		[Vector3(-1, 0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0), size.z, size.y, h.x],
		[Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, -1), size.x, size.z, h.y],
	]
	for fi in faces.size():
		var f: Array = faces[fi]
		var n: Vector3 = f[0]
		var u: Vector3 = f[1]
		var w: Vector3 = f[2]
		var fw: float = f[3]
		var fh: float = f[4]
		var nu := maxi(1, int(ceil(fw / F_QUAD)))
		var nv := maxi(1, int(ceil(fh / F_QUAD)))
		var o: Vector3 = centre + n * float(f[5]) - u * fw * 0.5 - w * fh * 0.5
		var base := v.size()
		for j in nv + 1:
			for i in nu + 1:
				var a := fw * i / nu
				var b := fh * j / nv
				v.append(o + u * a + w * b)
				nrm.append(n)
				uv.append(Vector2(a, -b))
		for j in nv:
			for i in nu:
				var k := base + j * (nu + 1) + i
				idx.append_array([k, k + nu + 2, k + 1, k, k + nu + 1, k + nu + 2])
		if fi < 4:
			# Windows every 3 m along the wall and 3.5 m up, from the second metre.
			var cols := int((fw - 2.0) / 3.0)
			var rows := int((fh - 2.5) / 3.5)
			for r in rows:
				for cc in cols:
					var p: Vector3 = o + u * (fw * 0.5 + (cc - (cols - 1) * 0.5) * 3.0) + w * (2.2 + r * 3.5) + n * 0.02
					windows.append(Transform3D(Basis(u, w, n), p))


func _commit_buildings(root: Node3D, v: PackedVector3Array, nrm: PackedVector3Array, uv: PackedVector2Array, idx: PackedInt32Array, mat: Material, totals: Dictionary) -> void:
	if idx.is_empty():
		return
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = v
	arrays[Mesh.ARRAY_NORMAL] = nrm
	arrays[Mesh.ARRAY_TEX_UV] = uv
	arrays[Mesh.ARRAY_INDEX] = idx
	var st := SurfaceTool.new()
	st.create_from_arrays(arrays)
	st.generate_tangents()
	st.set_material(mat)
	var am := st.commit(null, Mesh.ARRAY_FLAG_COMPRESS_ATTRIBUTES)
	var mi := MeshInstance3D.new()
	mi.name = "buildings"
	mi.mesh = am
	_add(root, mi)
	totals["triangles"] += idx.size() / 3


func _make_town_cell(root: Node3D, c: Vector2, st: Dictionary, rng: RandomNumberGenerator, totals: Dictionary) -> void:
	var v := PackedVector3Array()
	var nrm := PackedVector3Array()
	var uv := PackedVector2Array()
	var idx := PackedInt32Array()
	var windows := []
	var crates := []
	var sc: Vector2 = st["c"]
	var sr: float = st["r"]
	var nb := int(F_CELL / F_BLOCK)
	var sq := _square_block()
	for bz in nb:
		for bx in nb:
			var b0 := Vector2(-F_CELL * 0.5 + bx * F_BLOCK, -F_CELL * 0.5 + bz * F_BLOCK)
			var bc := c + b0 + Vector2(F_BLOCK, F_BLOCK) * 0.5
			var d := bc.distance_to(sc)
			if d > sr:
				continue
			if bc.distance_to(sq) < 1.0:
				continue # the first town's square: the campfire and the crowd
			# Four lots of 12 m in the block (4 m streets either side of its edges), taller towards the centre.
			for lot in 4:
				if rng.randf() > 0.88:
					continue
				var w := rng.randf_range(8.0, 11.5)
				var dd := rng.randf_range(8.0, 11.5)
				var hgt := rng.randf_range(8.0, 16.0) + 26.0 * pow(maxf(0.0, 1.0 - d / sr), 2.0) * rng.randf()
				var lc := b0 + Vector2(4.0 + 6.0 + 12.0 * (lot % 2), 4.0 + 6.0 + 12.0 * (lot / 2))
				var gy := height(c.x + lc.x, c.y + lc.y) - 0.5
				_building(v, nrm, uv, idx, windows, Vector3(lc.x, gy + hgt * 0.5, lc.y), Vector3(w, hgt, dd))
				totals["buildings"] += 1
			# Props along the block's south street, crates in its north one.
			for k in 2:
				var pl := b0 + Vector2(6.0 + 20.0 * k + rng.randf_range(-2, 2), 1.6)
				var pm: Mesh = _shared["barrel"] if rng.randf() < 0.5 else _shared["bench"]
				_prop(root, pm, Vector3(pl.x, height(c.x + pl.x, c.y + pl.y) + (0.45 if pm == _shared["barrel"] else 0.22), pl.y), rng.randf() * TAU, totals)
			for k in 3:
				var cl := b0 + Vector2(rng.randf_range(4, 28), F_BLOCK - rng.randf_range(0.8, 2.5))
				crates.append(Transform3D(Basis(Vector3.UP, rng.randf() * TAU), Vector3(cl.x, height(c.x + cl.x, c.y + cl.y) + 0.5, cl.y)))
			# A street light at the block's corner.
			var lp := b0 + Vector2(1.6, 1.6)
			_light(root, Vector3(lp.x, height(c.x + lp.x, c.y + lp.y), lp.y), totals)
	_commit_buildings(root, v, nrm, uv, idx, _mats[(int(c.x) / 128 * 7 + int(c.y) / 128 * 13 + 4800) % _mats.size()], totals)
	_instances(root, "windows", _shared["window"], windows, totals)
	_instances(root, "crates", _shared["crate"], crates, totals)
	totals["windows"] += windows.size()


func _make_village_cell(root: Node3D, c: Vector2, st: Dictionary, rng: RandomNumberGenerator, totals: Dictionary) -> void:
	var v := PackedVector3Array()
	var nrm := PackedVector3Array()
	var uv := PackedVector2Array()
	var idx := PackedInt32Array()
	var windows := []
	var fences := []
	var sc: Vector2 = st["c"]
	var sr: float = st["r"]
	var lights := 0
	# Houses on lots of 16 m inside the village's circle.
	for lz in 8:
		for lx in 8:
			var lc := Vector2(-F_CELL * 0.5 + (lx + 0.5) * 16.0, -F_CELL * 0.5 + (lz + 0.5) * 16.0)
			var wp := c + lc
			if wp.distance_to(sc) > sr - 6.0 or rng.randf() < 0.45:
				continue
			var w := rng.randf_range(6.0, 9.0)
			var dd := rng.randf_range(6.0, 9.0)
			var hgt := rng.randf_range(4.5, 8.0)
			var gy := height(wp.x, wp.y) - 0.4
			_building(v, nrm, uv, idx, windows, Vector3(lc.x, gy + hgt * 0.5, lc.y), Vector3(w, hgt, dd))
			totals["buildings"] += 1
			# A fence along the lot's south side, a prop by the door, a light at every other house.
			for k in 8:
				var fp := lc + Vector2(-7.0 + k * 2.0, -7.5)
				fences.append(Transform3D(Basis(), Vector3(fp.x, height(c.x + fp.x, c.y + fp.y) + 0.6, fp.y)))
			var pp := lc + Vector2(rng.randf_range(-3, 3), dd * 0.5 + 1.2)
			_prop(root, _shared["barrel"], Vector3(pp.x, height(c.x + pp.x, c.y + pp.y) + 0.45, pp.y), rng.randf() * TAU, totals)
			if lights % 2 == 0:
				var lp := lc + Vector2(-w * 0.5 - 1.5, dd * 0.5 + 1.5)
				_light(root, Vector3(lp.x, height(c.x + lp.x, c.y + lp.y), lp.y), totals)
			lights += 1
	_commit_buildings(root, v, nrm, uv, idx, _mats[(int(c.x) / 128 * 5 + int(c.y) / 128 * 11 + 4800) % _mats.size()], totals)
	_instances(root, "windows", _shared["window"], windows, totals)
	_instances(root, "fences", _shared["fence"], fences, totals)
	totals["windows"] += windows.size()


func _make_wild_cell(root: Node3D, c: Vector2, rng: RandomNumberGenerator, totals: Dictionary) -> void:
	var rocks := []
	for i in rng.randi_range(14, 34):
		var p := Vector2(rng.randf_range(-F_CELL * 0.5, F_CELL * 0.5), rng.randf_range(-F_CELL * 0.5, F_CELL * 0.5))
		var wp := c + p
		if wp.distance_to(lake_c) < lake_r + 8.0:
			continue
		var s := rng.randf_range(0.5, 3.2)
		var b := Basis(Vector3.UP, rng.randf() * TAU).rotated(Vector3.RIGHT, rng.randf_range(-0.3, 0.3)).scaled(Vector3(s, s * rng.randf_range(0.6, 1.0), s))
		rocks.append(Transform3D(b, Vector3(p.x, height(wp.x, wp.y) - 0.25 * s, p.y)))
	_instances(root, "rocks", _shared["rock"], rocks, totals)
	totals["rocks"] += rocks.size()
	for i in 3:
		var p := Vector2(rng.randf_range(-F_CELL * 0.45, F_CELL * 0.45), rng.randf_range(-F_CELL * 0.45, F_CELL * 0.45))
		if (c + p).distance_to(lake_c) < lake_r + 8.0:
			continue
		var mi := MeshInstance3D.new()
		mi.mesh = _shared["log"]
		mi.transform = Transform3D(Basis(Vector3.UP, rng.randf() * TAU) * Basis(Vector3.FORWARD, PI * 0.5), Vector3(p.x, height(c.x + p.x, c.y + p.y) + 0.2, p.y))
		_add(root, mi)
		totals["props"] += 1


## The block of the first town nearest its centre: its square (no buildings).
func _square_block() -> Vector2:
	var c: Vector2 = sites[0]["c"]
	var half := world * 0.5
	return Vector2(floorf((c.x + half) / F_BLOCK) * F_BLOCK - half + F_BLOCK * 0.5, floorf((c.y + half) / F_BLOCK) * F_BLOCK - half + F_BLOCK * 0.5)


# ---------------------------------------------------------------- the slice's systems in the first town

func _square() -> Vector3:
	var s := _square_block()
	return Vector3(s.x, height(s.x, s.y), s.y)


## The slice's campfire, asleep while the camera is farther than --frontier_fire_sleep (250 m; 0: never) — its gas
## simulation was the largest pass of the frame wherever the camera was (milestone 67).
func _build_fire() -> void:
	super()
	fire.set("sleep_distance", float(args.get("frontier_fire_sleep", "250")))


func _build_water() -> void:
	var water: Node3D = ClassDB.instantiate("FellowWater3D")
	water.name = "Lake"
	water.set("extent", Vector2(lake_r * 2.6, lake_r * 2.6))
	water.position = Vector3(lake_c.x, lake_level, lake_c.y)
	add_child(water)


func _define_cameras() -> void:
	var t: Vector2 = sites[0]["c"]
	var tr: float = sites[0]["r"]
	var th := height(t.x, t.y)
	var sq := _square()
	# A street: the block edge west of the square, northwards.
	var sx := sq.x - F_BLOCK * 0.5
	var street0 := Vector3(sx, height(sx, sq.z - 70.0) + 1.8, sq.z - 70.0)
	var street1 := Vector3(sx, height(sx, sq.z + 90.0) + 3.0, sq.z + 90.0)
	var far_site: Vector2 = sites[1]["c"] if sites.size() > 1 else Vector2(world * 0.3, world * 0.3)
	var centre := Vector3(0, height(0, 0), 0)
	var overlook := Vector3(t.x - tr - 160.0, th + 140.0, t.y - tr - 120.0)
	var lake := Vector3(lake_c.x, lake_level, lake_c.y)
	var shore := Vector3(lake_c.x - lake_r - 30.0, height(lake_c.x - lake_r - 30.0, lake_c.y - 40.0) + 3.0, lake_c.y - 40.0)
	var forest_p := Vector2(t.x - tr - 200.0, t.y + tr + 200.0)
	var forest := Vector3(forest_p.x, height(forest_p.x, forest_p.y) + 1.8, forest_p.y)
	fixed_cameras = {
		"frontier_overlook": {"pos": overlook, "target": Vector3(world * 0.25, 0.0, world * 0.25), "fov": 60.0, "time": 2.0, "purpose": "the first town from above, the world to the horizon (proxies past the streamed cells)"},
		"frontier_street": {"pos": street0, "target": street1, "fov": 65.0, "time": 2.0, "purpose": "eye level along a street: buildings, windows, street lights"},
		"frontier_square": {"pos": sq + Vector3(-9, 2.2, 14), "target": sq + Vector3(0, 0.8, 0), "fov": 60.0, "time": 2.0, "purpose": "the square: the campfire and the crowd"},
		"frontier_lake": {"pos": shore, "target": lake, "fov": 65.0, "time": 2.0, "purpose": "the lake's shore and the town beyond"},
		"frontier_forest": {"pos": forest, "target": forest + Vector3(60, 4, 40), "fov": 70.0, "time": 2.0, "purpose": "at a forest's edge: trees, grass, rocks"},
		"frontier_far": {"pos": centre + Vector3(0, 320, 0), "target": Vector3(-world * 0.5, 0, world * 0.5), "fov": 60.0, "time": 2.0, "purpose": "high over the centre: the far cells' and regions' proxies"},
	}
	# The flight: from the overlook down into the town and its streets, the square, the lake, low over the forest to
	# another settlement, high over the centre and back; keys spaced at about 45 m/s.
	var pts := [
		[overlook, Vector3(t.x, th, t.y)],
		[Vector3(t.x - tr * 0.6, th + 60.0, t.y - tr * 0.6), Vector3(t.x, th, t.y)],
		[street0, street1],
		[Vector3(sx, street1.y - 1.0, sq.z + 60.0), Vector3(sq.x + 120.0, th + 3.0, sq.z + 90.0)],
		[sq + Vector3(-9, 2.2, 14), sq + Vector3(0, 0.8, 0)],
		[shore + Vector3(0, 2, 0), lake],
		[Vector3(lake_c.x + lake_r, lake_level + 25.0, lake_c.y + lake_r), Vector3(far_site.x, 0, far_site.y)],
	]
	var a := Vector3(lake_c.x + lake_r, 0, lake_c.y + lake_r)
	var b := Vector3(far_site.x, 0, far_site.y)
	for k in range(1, 6):
		var p := a.lerp(b, k / 6.0)
		pts.append([Vector3(p.x, height(p.x, p.z) + 35.0, p.z), b + Vector3(0, height(b.x, b.z), 0)])
	pts.append([Vector3(far_site.x, height(far_site.x, far_site.y) + 45.0, far_site.y), centre])
	pts.append([centre + Vector3(0, 260, 0), Vector3(t.x, th, t.y)])
	pts.append([overlook, Vector3(t.x, th, t.y)])
	var keys := []
	var tt := 0.0
	for i in pts.size():
		if i > 0:
			tt += maxf(6.0, (pts[i][0] as Vector3).distance_to(pts[i - 1][0]) / 45.0)
		keys.append([tt, pts[i][0], pts[i][1], 60.0])
	flythrough = CameraPath.from_array(keys)
	_counts["flight_s"] = snappedf(tt, 0.1)


## The time of day, the street lights of the loaded cells (they come and go with their cells).
func set_time(t: float) -> void:
	var phase := fposmod(_daytime + _day_t / _day, 1.0)
	var elev := sin((phase - 0.25) * TAU) * 62.0
	sun.rotation_degrees = Vector3(-elev, phase * 360.0 + 90.0, 0.0)
	sun.visible = elev > -3.0
	var lit := clampf((8.0 - elev) / 10.0, 0.0, 1.0)
	for l in get_tree().get_nodes_in_group("frontier_lights"):
		l.light_energy = 1.6 * lit
		l.visible = lit > 0.0
	if crowd:
		crowd.set_time(t)


## Per frame (perf mode): the streamers' cells and meshes.
func sample_extra() -> Dictionary:
	var cs: Dictionary = cell_streamer.get_stats()
	var gs: Dictionary = ground_streamer.get_stats()
	return {"cells_loaded": cs.get("loaded", 0), "regions_loaded": gs.get("loaded", 0), "cell_loads": cs.get("loads", 0),
		"region_loads": gs.get("loads", 0), "meshes_full": cs.get("streamed_meshes_full", 0), "mesh_loads": cs.get("mesh_loads", 0),
		"cell_activation_usec_max": cs.get("activation_usec_max", 0), "region_activation_usec_max": gs.get("activation_usec_max", 0),
		"lights": get_tree().get_nodes_in_group("frontier_lights").size()}


## The soak (main.gd, after each frame): what the streamers did in it — a hitch's record carries it.
func soak_after_frame() -> Dictionary:
	var st := {}
	var cs: Dictionary = cell_streamer.get_stats()
	var gs: Dictionary = ground_streamer.get_stats()
	var now := {"cell_activation_usec": cs.get("activation_usec", 0), "region_activation_usec": gs.get("activation_usec", 0),
		"cell_release_usec": cs.get("release_usec", 0), "region_release_usec": gs.get("release_usec", 0),
		"cell_loads": cs.get("loads", 0), "region_loads": gs.get("loads", 0), "mesh_swap_usec": cs.get("mesh_swap_usec", 0),
		"streamer_update_usec": int(cs.get("update_usec", 0)) + int(gs.get("update_usec", 0))}
	for k in now:
		st[k] = int(now[k]) - int(_soak_prev.get(k, now[k]))
	_soak_prev = now
	if fire:
		st["fire_sleeping"] = fire.call("is_sleeping")
	return st
