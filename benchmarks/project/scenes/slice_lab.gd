extends "res://bench/scene_base.gd"
## SLICE LAB (milestone 55; built in code, deterministic): a vertical slice — one 1 km valley with FellowEngine's
## systems at once, for long runs (--mode=soak: an hour of play with frame times, hitches and memory recorded).
##
##   * terrain: a 1024 m height field (4 m grid, 64 chunks of 128 m: 131 k triangles), hills around a flat valley
##   * a town of 711 buildings (6 materials) under a FellowHLOD3D, 99 street lights that come on at dusk, a square
##     with a campfire (a FellowGasVolume3D and a burning log, FellowBurnable3D, lit again every few minutes) and a
##     crowd of skinned characters (Crowd Lab's: 52 bones, a clip each)
##   * a lake (FellowWater3D) in the valley's low end
##   * a forest of trees on the hills and meadows of grass and bushes, swaying in a FellowWind3D
##   * weather (FellowWeather3D driving FellowRain3D, FellowWind3D, FellowLightning3D; rain, storms, snow)
##   * a sun that crosses the sky (a day of --slice_day seconds, 600), with FellowEngine's atmosphere and clouds when
##     the run turns them on (--fellow=post/atmosphere/enabled=true,post/clouds/enabled=true --sky=atmosphere)
## Options: --slice_weather=clear|cloudy|overcast|rain|storm|snow|blizzard (perf and capture modes: fixed; soak:
## cycled), --slice_day=<seconds>, --slice_daytime=<0..1> (the time of day at t = 0: 0.35 mid-morning),
## --slice_crowd=<n> (48), --slice_trees=<n> (9000), --slice_grass=<clumps per m2 in the meadows> (1.0),
## --slice_without=town,forest,water,weather,fire,crowd (parts left out), --slice_weather_seconds=<s> (120: each
## weather of the soak's cycle), --slice_fire_seconds=<s> (240: the campfire's log burns again), --slice_terrain=node
## (FellowTerrain3D at --slice_terrain_spacing=<m>, 1, instead of the 4 m chunks).
## Stock Godot: the FellowEngine nodes are left out (load_error is not set; the slice is the terrain, town, crowd and
## forest).

const SIZE := 1024.0
const GRID := 4.0
const CHUNK := 128.0
const TOWN := Vector3(-110.0, 0.0, -60.0) # the town's centre
const TOWN_HALF := Vector2(130.0, 110.0)
const BLOCK := 26.0 # streets every 26 m
const LAKE := Vector3(190.0, 0.0, 130.0)
const LAKE_RADIUS := 95.0
const WATER_LEVEL := -1.2
const WEATHERS := ["clear", "cloudy", "overcast", "rain", "storm", "snow", "blizzard"]
# The soak's weather: each for 120 s, turning over 40 s.
const SOAK_WEATHER := [1, 3, 4, 2, 5, 6, 0]
const SOAK_WEATHER_SECONDS := 120.0

var sun: DirectionalLight3D
var noise := FastNoiseLite.new()
var crowd: Node3D = null # Crowd Lab's script, building its characters under this node
var weather: Node = null
var rain: Node = null
var wind: Node = null
var lightning: Node = null
var fire: Node3D = null
var log_burn: Node = null
var _street_lights: Array[OmniLight3D] = []
var _day := 600.0
var _daytime := 0.35
var _soak_weather := -1
var _last_fire := 0.0
var _weather_seconds := SOAK_WEATHER_SECONDS
var _fire_seconds := 240.0
var _day_t := 0.0 # the day's clock: advanced by soak_update() only (perf and capture modes keep --slice_daytime)
var _counts := {}


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "slice"
	_day = maxf(10.0, float(args.get("slice_day", "600")))
	_daytime = float(args.get("slice_daytime", "0.35"))
	_weather_seconds = maxf(5.0, float(args.get("slice_weather_seconds", str(SOAK_WEATHER_SECONDS))))
	_fire_seconds = maxf(2.0, float(args.get("slice_fire_seconds", "240")))
	noise.seed = 55
	noise.frequency = 0.004
	noise.fractal_octaves = 4
	_setup_common()
	camera.far = 1500.0
	_setup_environment()
	var t0 := Time.get_ticks_usec()
	# --slice_without=town,forest,water,weather,fire,crowd: parts left out (what a hitch or a cost comes from).
	var without := str(args.get("slice_without", "")).replace("+", ",").split(",", false)
	_build_terrain()
	if not without.has("town"):
		_build_town()
	if not without.has("forest"):
		_build_vegetation()
	_build_lights()
	var fellow := ClassDB.class_exists("FellowWeather3D")
	if fellow:
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
	info = {"stats": scene_stats(self), "fellow_nodes": fellow, "counts": _counts,
		"build_ms": (Time.get_ticks_usec() - t0) / 1000.0,
		"notes": "Vertical slice: 1 km valley, HLOD town of 711 buildings, 99 street lights, crowd, campfire, lake, forest in the wind, weather, day cycle."}


func _setup_environment() -> void:
	var sky_mat := ProceduralSkyMaterial.new()
	var sky := Sky.new()
	sky.sky_material = sky_mat
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.ssao_enabled = true
	env.glow_enabled = true
	env.fog_enabled = false


func height(x: float, z: float) -> float:
	# A valley: flat where the town and the lake's shore are, hills rising from 260 m to the map's edge.
	var r := Vector2(x, z).length()
	var rim := smoothstep(260.0, 500.0, r) * 60.0
	var h := rim + noise.get_noise_2d(x, z) * (2.0 + rim * 0.35)
	var tx := maxf(absf(x - TOWN.x) - TOWN_HALF.x, 0.0)
	var tz := maxf(absf(z - TOWN.z) - TOWN_HALF.y, 0.0)
	h *= smoothstep(0.0, 40.0, Vector2(tx, tz).length()) # flat under the town
	var lake := Vector2(x - LAKE.x, z - LAKE.z).length()
	h = lerpf(h, -5.0, smoothstep(LAKE_RADIUS + 25.0, LAKE_RADIUS - 30.0, lake))
	return h


func normal_at(x: float, z: float) -> Vector3:
	var e := 1.0
	return Vector3(height(x - e, z) - height(x + e, z), 2.0 * e, height(x, z - e) - height(x, z + e)).normalized()


func _in_town(x: float, z: float, margin := 0.0) -> bool:
	return absf(x - TOWN.x) < TOWN_HALF.x + margin and absf(z - TOWN.z) < TOWN_HALF.y + margin


func _build_terrain() -> void:
	# --slice_terrain=node (milestone 57): FellowTerrain3D, a 1 m grid (--slice_terrain_spacing) with levels of detail,
	# its own collision; the default, chunks: 64 meshes of a 4 m grid.
	if str(args.get("slice_terrain", "chunks")) == "node" and ClassDB.class_exists("FellowTerrain3D"):
		_build_terrain_node()
		return
	var mat := StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.roughness = 0.9
	var per := int(CHUNK / GRID)
	var n := int(SIZE / CHUNK)
	var tris := 0
	for cz in n:
		for cx in n:
			var ox := -SIZE * 0.5 + cx * CHUNK
			var oz := -SIZE * 0.5 + cz * CHUNK
			var verts := PackedVector3Array()
			var norms := PackedVector3Array()
			var cols := PackedColorArray()
			var idx := PackedInt32Array()
			for j in per + 1:
				for i in per + 1:
					var x := ox + i * GRID
					var z := oz + j * GRID
					var y := height(x, z)
					var nrm := normal_at(x, z)
					verts.append(Vector3(x, y, z))
					norms.append(nrm)
					var c := Color(0.27, 0.36, 0.17) # grass
					if nrm.y < 0.86:
						c = Color(0.4, 0.38, 0.35) # rock on the slopes
					if _in_town(x, z, 4.0):
						c = Color(0.36, 0.35, 0.33) # paved
					elif y < WATER_LEVEL + 1.0:
						c = Color(0.42, 0.38, 0.29) # sand at the shore
					cols.append(c)
			for j in per:
				for i in per:
					var a := j * (per + 1) + i
					idx.append_array(PackedInt32Array([a, a + 1, a + per + 1, a + 1, a + per + 2, a + per + 1]))
			var arr := []
			arr.resize(Mesh.ARRAY_MAX)
			arr[Mesh.ARRAY_VERTEX] = verts
			arr[Mesh.ARRAY_NORMAL] = norms
			arr[Mesh.ARRAY_COLOR] = cols
			arr[Mesh.ARRAY_INDEX] = idx
			var mesh := ArrayMesh.new()
			mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
			mesh.surface_set_material(0, mat)
			var mi := MeshInstance3D.new()
			mi.name = "terrain_%d_%d" % [cx, cz]
			mi.mesh = mesh
			add_child(mi)
			tris += per * per * 2
	_counts["terrain_triangles"] = tris
	# The ground collides (the rain's splashes and the crowd need none; physics bodies in the gas volume do).
	var body := StaticBody3D.new()
	var shape := CollisionShape3D.new()
	shape.shape = WorldBoundaryShape3D.new()
	body.add_child(shape)
	add_child(body)


func _build_terrain_node() -> void:
	var spacing := maxf(0.25, float(args.get("slice_terrain_spacing", "1")))
	var n := int(SIZE / spacing) + 1
	var t0 := Time.get_ticks_usec()
	var heights := PackedFloat32Array()
	heights.resize(n * n)
	for j in n:
		var z := -SIZE * 0.5 + j * spacing
		for i in n:
			heights[j * n + i] = height(-SIZE * 0.5 + i * spacing, z)
	var img := Image.create_from_data(n, n, false, Image.FORMAT_RF, heights.to_byte_array())
	# The chunks' colours as layers: grass the base, rock on the slopes (1), paving in the town (2), sand at the shore
	# (3); the splat at a quarter of the heights' resolution.
	var sn := (n - 1) / 4 + 1
	var splat := Image.create(sn, sn, false, Image.FORMAT_RGBA8)
	for j in sn:
		for i in sn:
			var x := -SIZE * 0.5 + i * spacing * 4.0
			var z := -SIZE * 0.5 + j * spacing * 4.0
			var nrm := normal_at(x, z)
			var c := Color(0, 0, 0, 0)
			if _in_town(x, z, 4.0):
				c.g = 1.0
			elif height(x, z) < WATER_LEVEL + 1.0:
				c.b = 1.0
			elif nrm.y < 0.86:
				c.r = 1.0
			splat.set_pixel(i, j, c)
	var t := ClassDB.instantiate("FellowTerrain3D")
	t.name = "Terrain"
	t.position = Vector3(-SIZE * 0.5, 0.0, -SIZE * 0.5)
	t.vertex_spacing = spacing
	var layers := []
	for c in [Color(0.27, 0.36, 0.17), Color(0.4, 0.38, 0.35), Color(0.36, 0.35, 0.33), Color(0.42, 0.38, 0.29)]:
		var l = ClassDB.instantiate("FellowTerrainLayer")
		# A layer's colour is sRGB (as a material's); the chunks' vertex colours are taken as linear.
		l.albedo_color = c.linear_to_srgb()
		l.roughness = 0.9
		layers.append(l)
	layers[1].triplanar = true
	t.layers = layers
	t.heightmap = img
	t.splat_map = splat
	add_child(t)
	_counts["terrain"] = "node"
	_counts["terrain_spacing"] = spacing
	_counts["terrain_build_ms"] = (Time.get_ticks_usec() - t0) / 1000.0
	_counts["terrain_stats"] = t.get_stats()


func _build_town() -> void:
	var hlod: Node3D = null
	var parent: Node3D = self
	if ClassDB.class_exists("FellowHLOD3D"):
		hlod = ClassDB.instantiate("FellowHLOD3D")
		hlod.name = "Town"
		hlod.set("distance", 150.0)
		hlod.set("cell_size", 64.0)
		add_child(hlod)
		parent = hlod
	else:
		var town := Node3D.new()
		town.name = "Town"
		add_child(town)
		parent = town
	var colors := [Color(0.72, 0.68, 0.6), Color(0.6, 0.45, 0.38), Color(0.5, 0.52, 0.55), Color(0.78, 0.76, 0.72), Color(0.42, 0.38, 0.35), Color(0.65, 0.6, 0.5)]
	var boxes: Array[BoxMesh] = []
	for c in colors:
		var b := BoxMesh.new()
		var m := StandardMaterial3D.new()
		m.albedo_color = c
		m.roughness = 0.75
		b.material = m
		boxes.append(b)
	var rng := RandomNumberGenerator.new()
	rng.seed = 551
	var nx := int(TOWN_HALF.x * 2.0 / BLOCK)
	var nz := int(TOWN_HALF.y * 2.0 / BLOCK)
	var buildings := 0
	var square := Vector2i(nx / 2, nz / 2)
	for bz in nz:
		for bx in nx:
			if Vector2i(bx, bz) == square:
				continue # the square
			var bx0 := TOWN.x - TOWN_HALF.x + bx * BLOCK + 3.0
			var bz0 := TOWN.z - TOWN_HALF.y + bz * BLOCK + 3.0
			for k in 9:
				var cell := 20.0 / 3.0
				var w := rng.randf_range(4.0, 6.2)
				var d := rng.randf_range(4.0, 6.2)
				var h := rng.randf_range(5.0, 16.0)
				if rng.randf() < 0.06:
					h = rng.randf_range(24.0, 42.0) # a tower now and then
				var mi := MeshInstance3D.new()
				mi.mesh = boxes[rng.randi() % boxes.size()]
				mi.scale = Vector3(w, h, d)
				mi.position = Vector3(bx0 + (k % 3 + 0.5) * cell, h * 0.5, bz0 + (k / 3 + 0.5) * cell)
				parent.add_child(mi)
				buildings += 1
	_counts["buildings"] = buildings
	# Street lights at the blocks' corners (on at dusk), a shared post.
	var post := CylinderMesh.new()
	post.top_radius = 0.08
	post.bottom_radius = 0.1
	post.height = 5.0
	post.radial_segments = 6
	var post_mat := StandardMaterial3D.new()
	post_mat.albedo_color = Color(0.15, 0.15, 0.16)
	post.material = post_mat
	for bz in nz + 1:
		for bx in nx + 1:
			var p := Vector3(TOWN.x - TOWN_HALF.x + bx * BLOCK + 1.5, 0.0, TOWN.z - TOWN_HALF.y + bz * BLOCK + 1.5)
			var mi := MeshInstance3D.new()
			mi.mesh = post
			mi.position = p + Vector3(0, 2.5, 0)
			add_child(mi)
			var l := OmniLight3D.new()
			l.position = p + Vector3(0, 4.8, 0)
			l.omni_range = 16.0
			l.light_color = Color(1.0, 0.8, 0.55)
			l.light_energy = 0.0
			add_child(l)
			_street_lights.append(l)
	_counts["street_lights"] = _street_lights.size()


func _wind_mat(c: Color, rough: float, two_sided: bool, h: float, bend: float, flutter: float) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = rough
	if two_sided:
		m.cull_mode = BaseMaterial3D.CULL_DISABLED
	if ClassDB.class_exists("FellowWind3D"):
		# FellowEngine's Wind group of the material (decision 0103): swayed by the scene's FellowWind3D.
		m.set("wind", true)
		m.set("wind_height", h)
		m.set("wind_bend", bend)
		m.set("wind_flutter", flutter)
	return m


func _tree_mesh() -> ArrayMesh:
	var mesh := ArrayMesh.new()
	var trunk := CylinderMesh.new()
	trunk.top_radius = 0.18
	trunk.bottom_radius = 0.3
	trunk.height = 5.0
	trunk.radial_segments = 8
	trunk.rings = 1
	var st := SurfaceTool.new()
	st.append_from(trunk, 0, Transform3D(Basis(), Vector3(0, 2.5, 0)))
	st.set_material(_wind_mat(Color(0.35, 0.25, 0.17), 0.9, false, 8.0, 0.3, 0.0))
	st.commit(mesh)
	var crown := SphereMesh.new()
	crown.radius = 1.0
	crown.height = 2.0
	crown.radial_segments = 10
	crown.rings = 6
	var st2 := SurfaceTool.new()
	st2.append_from(crown, 0, Transform3D(Basis().scaled(Vector3(2.2, 1.8, 2.2)), Vector3(0, 5.2, 0)))
	st2.append_from(crown, 0, Transform3D(Basis().scaled(Vector3(1.7, 1.5, 1.7)), Vector3(0.5, 6.6, -0.3)))
	st2.append_from(crown, 0, Transform3D(Basis().scaled(Vector3(1.2, 1.2, 1.2)), Vector3(-0.3, 7.7, 0.2)))
	st2.set_material(_wind_mat(Color(0.16, 0.3, 0.12), 0.85, false, 8.0, 0.6, 0.5))
	st2.commit(mesh)
	return mesh


func _grass_mesh() -> ArrayMesh:
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var idx := PackedInt32Array()
	for b in 6:
		var ang := TAU * float(b) / 6.0 + 0.35 * float(b % 3)
		var dir := Vector3(cos(ang), 0.0, sin(ang))
		var side := Vector3(-dir.z, 0.0, dir.x)
		var root := dir * 0.06
		var h := 0.4 + 0.08 * float(b % 4)
		var lean := dir * (0.12 + 0.04 * float(b % 3))
		var w := 0.035
		var base := verts.size()
		verts.append_array(PackedVector3Array([root - side * w, root + side * w,
			root + lean * 0.45 + Vector3(0, h * 0.55, 0) - side * w * 0.7, root + lean * 0.45 + Vector3(0, h * 0.55, 0) + side * w * 0.7,
			root + lean + Vector3(0, h, 0)]))
		var nrm := (dir * 0.3 + Vector3.UP).normalized()
		for v in 5:
			norms.append(nrm)
		idx.append_array(PackedInt32Array([base, base + 1, base + 2, base + 1, base + 3, base + 2, base + 2, base + 3, base + 4]))
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = verts
	arr[Mesh.ARRAY_NORMAL] = norms
	arr[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	mesh.surface_set_material(0, _wind_mat(Color(0.3, 0.42, 0.16), 0.85, true, 0.5, 1.0, 0.0))
	return mesh


func _multimesh(p_name: String, p_mesh: Mesh, p_xforms: Array[Transform3D], p_range: float, p_margin: float) -> void:
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.mesh = p_mesh
	mm.instance_count = p_xforms.size()
	for i in p_xforms.size():
		mm.set_instance_transform(i, p_xforms[i])
	var mmi := MultiMeshInstance3D.new()
	mmi.name = p_name
	mmi.multimesh = mm
	mmi.extra_cull_margin = p_margin
	if p_range > 0.0:
		mmi.visibility_range_end = p_range
		mmi.visibility_range_end_margin = 10.0
	add_child(mmi)


func _place(x: float, z: float, yaw: float, s: float) -> Transform3D:
	return Transform3D(Basis(Vector3.UP, yaw).scaled(Vector3(s, s, s)), Vector3(x, height(x, z) - 0.05, z))


func _build_vegetation() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 552
	# Trees: on the hills and around the valley, not in the town or the lake.
	var trees: Array[Transform3D] = []
	var want := int(args.get("slice_trees", "9000"))
	var tries := 0
	while trees.size() < want and tries < want * 6:
		tries += 1
		var x := rng.randf_range(-SIZE * 0.49, SIZE * 0.49)
		var z := rng.randf_range(-SIZE * 0.49, SIZE * 0.49)
		if _in_town(x, z, 12.0) or Vector2(x - LAKE.x, z - LAKE.z).length() < LAKE_RADIUS + 12.0:
			continue
		var r := Vector2(x, z).length()
		if r < 220.0 and rng.randf() < 0.85:
			continue # the valley's floor mostly open
		if normal_at(x, z).y < 0.8:
			continue
		trees.append(_place(x, z, rng.randf() * TAU, rng.randf_range(0.8, 1.3)))
	var tree_mesh := _tree_mesh()
	# In 4 x 4 MultiMeshes (per region: culled whole by stock, per instance by FellowEngine).
	var regions := {}
	for t in trees:
		var key := Vector2i(int(floor((t.origin.x + SIZE * 0.5) / (SIZE / 4.0))), int(floor((t.origin.z + SIZE * 0.5) / (SIZE / 4.0))))
		if not regions.has(key):
			regions[key] = [] as Array[Transform3D]
		regions[key].append(t)
	for key in regions:
		_multimesh("trees_%d_%d" % [key.x, key.y], tree_mesh, regions[key], 0.0, 0.6)
	_counts["trees"] = trees.size()
	# Grass: meadows between the town and the lake, and around the lake, in 32 m chunks that end at 90 m.
	var grass_mesh := _grass_mesh()
	var density := float(args.get("slice_grass", "1.0"))
	var step := 1.0 / sqrt(maxf(density, 0.01))
	var chunks := {}
	var clumps := 0
	var x0 := -60.0
	while x0 < 300.0:
		var z0 := -40.0
		while z0 < 250.0:
			var x := x0 + rng.randf_range(-0.4, 0.4) * step
			var z := z0 + rng.randf_range(-0.4, 0.4) * step
			var lake := Vector2(x - LAKE.x, z - LAKE.z).length()
			var meadow := noise.get_noise_2d(x * 3.0, z * 3.0) > -0.1
			if meadow and not _in_town(x, z, 6.0) and lake > LAKE_RADIUS - 8.0 and height(x, z) > WATER_LEVEL + 0.3:
				var key := Vector2i(int(floor(x / 32.0)), int(floor(z / 32.0)))
				if not chunks.has(key):
					chunks[key] = [] as Array[Transform3D]
				chunks[key].append(_place(x, z, rng.randf() * TAU, rng.randf_range(0.8, 1.2)))
				clumps += 1
			z0 += step
		x0 += step
	for key in chunks:
		_multimesh("grass_%d_%d" % [key.x, key.y], grass_mesh, chunks[key], 90.0, 0.2)
	_counts["grass_clumps"] = clumps


func _build_lights() -> void:
	sun = DirectionalLight3D.new()
	sun.name = "sun"
	sun.light_energy = 1.6
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 300.0
	sun.light_angular_distance = 0.5
	apply_sun_overrides(sun)
	add_child(sun)


func _build_water() -> void:
	var water: Node3D = ClassDB.instantiate("FellowWater3D")
	water.name = "Lake"
	water.set("extent", Vector2(LAKE_RADIUS * 2.6, LAKE_RADIUS * 2.6))
	water.position = Vector3(LAKE.x, WATER_LEVEL, LAKE.z)
	add_child(water)


func _build_weather() -> void:
	wind = ClassDB.instantiate("FellowWind3D")
	wind.name = "Wind"
	add_child(wind)
	rain = ClassDB.instantiate("FellowRain3D")
	rain.name = "Rain"
	# Dry, without snow, not raining yet, as FellowWeather3D makes its own rain node: the weather sets it in (a rain
	# node's own wet ground is kept, and its default is wet).
	rain.set("rain", 0.0)
	rain.set("wetness", 0.0)
	rain.set("snow", 0.0)
	rain.set("streak_width", 0.004)
	add_child(rain)
	lightning = ClassDB.instantiate("FellowLightning3D")
	lightning.name = "Lightning"
	lightning.set("seed", 55)
	add_child(lightning)
	weather = ClassDB.instantiate("FellowWeather3D")
	weather.name = "Weather"
	world_env.name = "WorldEnvironment"
	weather.set("wind_path", NodePath("../Wind"))
	weather.set("rain_path", NodePath("../Rain"))
	weather.set("lightning_path", NodePath("../Lightning"))
	weather.set("sun_path", NodePath("../sun"))
	weather.set("environment_path", NodePath("../WorldEnvironment"))
	add_child(weather)
	var w := WEATHERS.find(str(args.get("slice_weather", "clear")))
	weather.call("set_weather", maxi(w, 0), 0.0)


func _build_fire() -> void:
	# A campfire in the square: a gas volume with a kindling emitter (Burn Lab's), a log burning on it.
	var nx := int(TOWN_HALF.x * 2.0 / BLOCK)
	var nz := int(TOWN_HALF.y * 2.0 / BLOCK)
	var sq := Vector3(TOWN.x - TOWN_HALF.x + (nx / 2 + 0.5) * BLOCK, 0.0, TOWN.z - TOWN_HALF.y + (nz / 2 + 0.5) * BLOCK)
	fire = ClassDB.instantiate("FellowGasVolume3D")
	fire.name = "Campfire"
	fire.set("size", Vector3(3.2, 4.0, 3.2))
	fire.set("resolution", int(args.get("gas_res", "48")))
	fire.set("turbulence", 5.0)
	fire.set("vorticity", 0.35)
	fire.set("fire_intensity", 18.0)
	fire.set("physics_obstacles", false)
	fire.set("closed_faces", 55)
	fire.position = sq
	add_child(fire)
	var kindling: Node3D = ClassDB.instantiate("FellowGasEmitter3D")
	kindling.set("shape", 2)
	kindling.set("radius", 0.25)
	kindling.set("height", 0.12)
	kindling.position = Vector3(0, 0.08, 0)
	kindling.set("fuel", 2.5)
	kindling.set("temperature", 900.0)
	kindling.set("smoke", 0.1)
	kindling.set("noise", 0.75)
	kindling.set("noise_scale", 0.15)
	fire.add_child(kindling)
	var log_mi := MeshInstance3D.new()
	var cm := CylinderMesh.new()
	cm.top_radius = 0.09
	cm.bottom_radius = 0.095
	cm.height = 1.0
	cm.radial_segments = 16
	log_mi.mesh = cm
	var bark := StandardMaterial3D.new()
	bark.albedo_color = Color(0.26, 0.17, 0.1)
	log_mi.material_override = bark
	log_mi.transform = Transform3D(Basis(Vector3.FORWARD, PI * 0.5), sq + Vector3(0, 0.18, 0))
	add_child(log_mi)
	log_burn = ClassDB.instantiate("FellowBurnable3D")
	log_burn.set("resolution", 32)
	log_mi.add_child(log_burn)
	_counts["campfire"] = sq


func _build_crowd() -> void:
	# Crowd Lab's characters, in the square: its builders, under a node of its script.
	var nx := int(TOWN_HALF.x * 2.0 / BLOCK)
	var nz := int(TOWN_HALF.y * 2.0 / BLOCK)
	var sq := Vector3(TOWN.x - TOWN_HALF.x + (nx / 2 + 0.5) * BLOCK, 0.0, TOWN.z - TOWN_HALF.y + (nz / 2 + 0.5) * BLOCK)
	crowd = load("res://scenes/crowd_lab.gd").new()
	crowd.name = "Crowd"
	crowd.position = sq + Vector3(0, 0, 6.0) # beside the fire
	add_child(crowd)
	crowd.args = {}
	crowd.crowd = int(args.get("slice_crowd", "48"))
	crowd._area = 20.0
	crowd._define_bones()
	crowd._build_mesh()
	crowd._build_clips()
	crowd._build_crowd()
	_counts["characters"] = crowd.crowd


func _define_cameras() -> void:
	var sq: Vector3 = TOWN + Vector3(0, 0, 0)
	if _counts.has("campfire"):
		sq = _counts["campfire"]
	var street_z := TOWN.z - TOWN_HALF.y + 4.0 * BLOCK # the middle of a street (blocks run from 3 m past it to 3 m short of the next)
	fixed_cameras = {
		"slice_overview": {"pos": Vector3(-420, 95, 380), "target": TOWN, "fov": 60.0, "time": 2.0, "purpose": "the valley from its rim: town, lake, forest, sky"},
		"slice_street": {"pos": Vector3(TOWN.x - TOWN_HALF.x + 1.0, 1.8, street_z), "target": Vector3(TOWN.x + TOWN_HALF.x, 3.0, street_z), "fov": 65.0, "time": 2.0, "purpose": "eye level along a street: buildings near and far, street lights"},
		"slice_square": {"pos": sq + Vector3(-9, 2.2, 14), "target": sq + Vector3(0, 0.8, 0), "fov": 60.0, "time": 2.0, "purpose": "the square: the campfire and the crowd"},
		"slice_lake": {"pos": Vector3(LAKE.x - 120, 4.0, LAKE.z - 70), "target": LAKE, "fov": 65.0, "time": 2.0, "purpose": "the lake's shore: water, meadow, the forest beyond"},
		"slice_forest": {"pos": Vector3(320, height(320, -300) + 1.8, -300), "target": Vector3(250, height(250, -240) + 4.0, -240), "fov": 70.0, "time": 2.0, "purpose": "inside the forest on the hills"},
	}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(-420, 95, 380), TOWN, 60.0],
		[15.0, Vector3(-300, 45, 150), TOWN + Vector3(0, 5, 0), 60.0],
		[30.0, Vector3(TOWN.x - TOWN_HALF.x + 1.0, 2.0, street_z), Vector3(TOWN.x + 40.0, 3.0, street_z), 65.0],
		[45.0, sq + Vector3(-9, 2.2, 14), sq + Vector3(0, 0.8, 0), 60.0],
		[57.0, Vector3(40, 3.0, 30), LAKE, 65.0],
		[72.0, Vector3(LAKE.x - 10.0, 4.0, LAKE.z - 60.0), Vector3(LAKE.x + 120.0, 2.0, LAKE.z + 60.0), 65.0],
		[87.0, Vector3(380, 70, 300), Vector3(0, 0, 0), 60.0],
		[102.0, Vector3(0, 80, 460), TOWN, 60.0],
		[120.0, Vector3(-420, 95, 380), TOWN, 60.0],
	])


## The time of day at t: the sun's elevation and azimuth; street lights from dusk to dawn.
func set_time(t: float) -> void:
	var phase := fposmod(_daytime + _day_t / _day, 1.0) # 0 midnight, 0.25 sunrise, 0.5 noon, 0.75 sunset
	var elev := sin((phase - 0.25) * TAU) * 62.0
	var azim := phase * 360.0 + 90.0
	sun.rotation_degrees = Vector3(-elev, azim, 0.0)
	sun.visible = elev > -3.0
	var lit := clampf((8.0 - elev) / 10.0, 0.0, 1.0)
	for l in _street_lights:
		l.light_energy = 1.6 * lit
	if crowd:
		crowd.set_time(t)


## The soak's schedule (main.gd --mode=soak, wall-clock seconds): the weather turns every two minutes, the campfire
## is lit again every four.
func soak_update(t: float) -> Dictionary:
	_day_t = t
	if weather:
		var w: int = SOAK_WEATHER[int(t / _weather_seconds) % SOAK_WEATHER.size()]
		if w != _soak_weather:
			_soak_weather = w
			weather.call("set_weather", w, minf(40.0, _weather_seconds / 3.0))
	if log_burn and t - _last_fire >= _fire_seconds:
		_last_fire = t
		log_burn.call("reset_burning")
	var st := {"weather": WEATHERS[_soak_weather] if _soak_weather >= 0 else str(args.get("slice_weather", "clear")),
		"daytime": snappedf(fposmod(_daytime + _day_t / _day, 1.0), 0.001)}
	if log_burn:
		st["log_burning_voxels"] = int(log_burn.call("get_burning_voxels"))
	return st
