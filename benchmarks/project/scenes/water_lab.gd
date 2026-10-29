extends "res://bench/scene_base.gd"
## BENCHMARK - Water Lab (milestone 24): a lake in wooded hills fed by a river (FellowEngine FellowWater3D over the
## lake, the river's FellowShallowWater3D simulation blended into it). Stock Godot has neither: "unsupported".
##
## 640 m x 640 m of terrain; the lake (330 m x 270 m, up to 9 m deep) has a sandy beach with a wooden pier in the south
## and a steeper, rocky shore in the north; the river (12-18 m wide) leaves a culvert in a retaining wall and runs
## 80 m past two boulders and a bobbing buoy into the lake. The simulation covers the river and its mouth (96 m square).
## Options:
##   --water=full|basic|none  full: FellowWater3D (default); basic: the milestone-1 simulated surface on the river and a
##                            StandardMaterial3D plane (transparent, refraction) on the lake, what a Godot project would
##                            use; none: no water (the scene's cost without it)
##   --water_sim_res=<n>      simulation grid (384 cells: 25 cm)
##   --water_preroll=<s>      simulated seconds before the first frame (40: the river reaches the lake)
##   --water_wind=<m/s>       wind speed (5), --water_waves=<n> wave grid per cascade (256)
##   --water_ssr=0, --water_caustics=0, --water_refraction=0 (refraction_strength 0), --water_ssr_steps=<n> (32)
##   --water_debug=<n>        FellowWater3D.debug_view (1 reflections, 2 foam, 3 refraction, 4 normals, 5 simulation, 6 roughness)
## Extra per-frame metrics: water_waves_gpu_ms, fluid_sim_gpu_ms.

const SIZE := 640.0
const LAKE_C := Vector2(60.0, 0.0)
const LAKE_R := Vector2(165.0, 135.0)
const SIM_C := Vector2(-132.0, 0.0)
const SIM_SIZE := 96.0
const CULVERT_X := -176.0
const BOULDERS := [Vector3(-150.0, 0.0, 2.6), Vector3(-127.0, 0.0, -2.8)]
const BOULDER_R := 1.1

var mode := "full"
var water  # FellowWater3D
var sim  # FellowShallowWater3D
var sun: DirectionalLight3D
var buoy: MeshInstance3D
var sim_res := 384
var _visuals: Array[Node3D] = []
const TERRAIN_N := 320
var _grid := PackedFloat32Array()  # the rendered terrain's heights (the simulation's bed is sampled from its triangles)


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "water"
	mode = str(args.get("water", "full"))
	if mode != "none" and not ClassDB.class_exists("FellowWater3D"):
		load_error = "unsupported: FellowWater3D not available (stock Godot has no water module)"
		return
	sim_res = int(args.get("water_sim_res", "384"))
	_setup_common()
	camera.far = 3000.0
	_setup_environment()
	_build_terrain()
	_build_props()
	_build_trees()
	if mode != "none":
		_build_simulation()
		if mode == "full":
			_build_water()
		else:
			_build_basic_lake()
	_define_cameras()
	set_time(0.0)
	await _preroll()
	info = {
		"mode": mode,
		"simulation_resolution": sim_res,
		"simulation_domain_m": SIM_SIZE,
		"wave_resolution": water.wave_resolution if water else 0,
		"significant_wave_height_m": water.get_significant_wave_height() if water else 0.0,
		"stats": scene_stats(self),
	}


func _setup_environment() -> void:
	var sky := Sky.new()
	var sm := ProceduralSkyMaterial.new()
	sm.sky_top_color = Color(0.18, 0.34, 0.62)
	sm.sky_horizon_color = Color(0.62, 0.70, 0.78)
	sm.ground_horizon_color = Color(0.36, 0.38, 0.36)
	sm.ground_bottom_color = Color(0.12, 0.13, 0.12)
	sm.sun_angle_max = 8.0
	sky.sky_material = sm
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.glow_enabled = true
	env.ssao_enabled = true
	env.ambient_light_energy = 0.7
	env.fog_enabled = true
	env.fog_density = 0.0006
	env.fog_aerial_perspective = 0.6
	env.fog_sky_affect = 0.0
	sun = DirectionalLight3D.new()
	sun.name = "sun"
	var e := deg_to_rad(32.0)
	var d := Vector3(0.6 * cos(e), -sin(e), -0.8 * cos(e)).normalized()  # from the south-west towards the north-east
	sun.basis = Basis.looking_at(d, Vector3.UP)
	sun.light_energy = 1.8
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 220.0
	add_child(sun)
	apply_sun_overrides(sun)


func _smin(a: float, b: float, k: float) -> float:
	var h := clampf(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
	return lerpf(b, a, h) - k * h * (1.0 - h)


func _river_z(x: float) -> float:
	return 5.0 * sin((x + 180.0) / 33.0)


## Ground height (m) at a world position: hills, the lake's basin, the river's channel.
func _height(x: float, z: float) -> float:
	var hills := 10.0 + 5.0 * sin(x * 0.013 + 1.3) * cos(z * 0.011 - 0.4) + 3.0 * sin(x * 0.031 + z * 0.023) + 1.2 * sin(x * 0.071 - z * 0.053)
	var d := Vector2((x - LAKE_C.x) / LAKE_R.x, (z - LAKE_C.y) / LAKE_R.y).length()
	var south := clampf(z / LAKE_R.y * 0.8 + 0.4, 0.0, 1.0)  # 0 in the north, 1 at the beach
	var depth := lerpf(9.0, 5.0, south)
	var rise := lerpf(28.0, 7.0, south)  # m per unit of d beyond the shore line
	var lake := -depth * (1.0 - d * d) if d < 1.0 else (d - 1.0) * rise + 1.2 * (1.0 - exp(-(d - 1.0) * 45.0))
	var h := _smin(hills, lake, 3.0)
	# The river: a channel from the culvert to the lake.
	if x < -80.0:
		var w := lerpf(6.0, 9.0, clampf((x - CULVERT_X) / 80.0, 0.0, 1.0))
		var dz := absf(z - _river_z(x)) / w
		var bed := lerpf(-1.1, -1.6, clampf((x - CULVERT_X) / 80.0, 0.0, 1.0))
		if x < CULVERT_X - 3.0:
			bed = 0.7  # behind the wall: a dry bed
		var channel := bed + 2.8 * dz * dz
		h = _smin(h, channel, 1.5)
	return h


## Height of the rendered terrain mesh (its triangles, as drawn) at a world position.
func _mesh_height(x: float, z: float) -> float:
	var n := TERRAIN_N
	var fx := clampf((x / SIZE + 0.5) * n, 0.0, n - 0.001)
	var fz := clampf((z / SIZE + 0.5) * n, 0.0, n - 0.001)
	var i := int(fx)
	var j := int(fz)
	var u := fx - i
	var v := fz - j
	var a := _grid[j * (n + 1) + i]
	var b := _grid[j * (n + 1) + i + 1]
	var c := _grid[(j + 1) * (n + 1) + i]
	var d := _grid[(j + 1) * (n + 1) + i + 1]
	if u + v <= 1.0:
		return a + u * (b - a) + v * (c - a)
	return d + (1.0 - u) * (c - d) + (1.0 - v) * (b - d)


func _build_terrain() -> void:
	var n := TERRAIN_N
	_grid.resize((n + 1) * (n + 1))
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for j in range(n + 1):
		for i in range(n + 1):
			var x := (float(i) / n - 0.5) * SIZE
			var z := (float(j) / n - 0.5) * SIZE
			var y := _height(x, z)
			_grid[j * (n + 1) + i] = y
			var slope := absf(_height(x + 1.0, z) - y) + absf(_height(x, z + 1.0) - y)
			var dl := Vector2((x - LAKE_C.x) / LAKE_R.x, (z - LAKE_C.y) / LAKE_R.y).length()
			var sand := (1.0 - smoothstep(0.35, 1.1, y)) * (1.0 - smoothstep(1.02, 1.12, dl))
			var bank := (1.0 - smoothstep(0.5, 2.5, y)) * (1.0 - sand)  # the river's muddy banks
			var rock := smoothstep(0.35, 0.7, slope)
			var grass := Color(0.17, 0.24, 0.09)
			var c := grass.lerp(Color(0.24, 0.20, 0.14), bank).lerp(Color(0.33, 0.29, 0.21), sand).lerp(Color(0.33, 0.31, 0.29), rock)
			if y < -0.2:
				c = c.lerp(Color(0.30, 0.27, 0.20), clampf(-y / 2.5, 0.0, 1.0))  # wet sand and silt under water
			st.set_color(c)
			st.add_vertex(Vector3(x, y, z))
	for j in range(n):
		for i in range(n):
			var a := j * (n + 1) + i
			st.add_index(a)
			st.add_index(a + 1)
			st.add_index(a + n + 1)
			st.add_index(a + 1)
			st.add_index(a + n + 2)
			st.add_index(a + n + 1)
	st.generate_normals()
	var mi := MeshInstance3D.new()
	mi.name = "Terrain"
	mi.mesh = st.commit()
	var mat := StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.roughness = 0.92
	mi.material_override = mat
	add_child(mi)
	_visuals.append(mi)


func _build_props() -> void:
	var stone := StandardMaterial3D.new()
	stone.albedo_color = Color(0.52, 0.50, 0.47)
	stone.roughness = 0.85
	var wood := StandardMaterial3D.new()
	wood.albedo_color = Color(0.36, 0.25, 0.16)
	wood.roughness = 0.8
	# Retaining wall with the culvert the river leaves.
	var wall := MeshInstance3D.new()
	var wb := BoxMesh.new()
	wb.size = Vector3(2.0, 6.0, 40.0)
	wall.mesh = wb
	wall.material_override = stone
	wall.position = Vector3(CULVERT_X - 1.0, 1.4, _river_z(CULVERT_X))
	add_child(wall)
	_visuals.append(wall)
	# Boulders in the river (solid in the simulation) and rocks along the northern shore.
	var rocks: Array = []
	for b in BOULDERS:
		rocks.append([Vector3(b.x, -0.2, _river_z(b.x) + b.z), BOULDER_R])
	var rng := RandomNumberGenerator.new()
	rng.seed = 77
	for i in range(14):
		var a := deg_to_rad(rng.randf_range(200.0, 330.0))
		var p := Vector2(LAKE_C.x + cos(a) * LAKE_R.x * rng.randf_range(0.93, 1.0), LAKE_C.y + sin(a) * LAKE_R.y * rng.randf_range(0.93, 1.0))
		rocks.append([Vector3(p.x, _height(p.x, p.y) - 0.3, p.y), rng.randf_range(0.8, 2.2)])
	for r in rocks:
		var mi := MeshInstance3D.new()
		var sm := SphereMesh.new()
		sm.radius = r[1]
		sm.height = r[1] * 1.5
		sm.radial_segments = 16
		sm.rings = 8
		mi.mesh = sm
		mi.material_override = stone
		mi.position = r[0]
		mi.rotation = Vector3(rng.randf() * 0.6, rng.randf() * TAU, rng.randf() * 0.6)
		add_child(mi)
		_visuals.append(mi)
	# The pier: from the beach 42 m out over the lake, posts every 3 m down to the bed.
	var deck := MeshInstance3D.new()
	var db := BoxMesh.new()
	db.size = Vector3(2.6, 0.2, 42.0)
	deck.mesh = db
	deck.material_override = wood
	deck.position = Vector3(40.0, 0.95, 131.0)
	add_child(deck)
	_visuals.append(deck)
	var post := CylinderMesh.new()
	post.top_radius = 0.13
	post.bottom_radius = 0.13
	post.height = 1.0
	post.radial_segments = 10
	for k in range(15):
		var z := 111.0 + k * 3.0
		for sx in [-1.15, 1.15]:
			var ground := _height(40.0 + sx, z)
			var mi := MeshInstance3D.new()
			mi.mesh = post
			mi.material_override = wood
			var h := 0.95 - ground + 0.3
			mi.scale = Vector3(1.0, h, 1.0)
			mi.position = Vector3(40.0 + sx, ground - 0.3 + h * 0.5, z)
			add_child(mi)
			_visuals.append(mi)
	# A rowing boat moored at the pier's end.
	var boat := MeshInstance3D.new()
	var bm := CapsuleMesh.new()
	bm.radius = 0.75
	bm.height = 4.2
	boat.mesh = bm
	var paint := StandardMaterial3D.new()
	paint.albedo_color = Color(0.72, 0.18, 0.12)
	paint.roughness = 0.5
	boat.material_override = paint
	boat.position = Vector3(43.4, 0.05, 113.0)
	boat.rotation = Vector3(PI * 0.5, 0.0, 0.0)
	boat.scale = Vector3(1.0, 1.0, 0.45)
	add_child(boat)
	_visuals.append(boat)
	# A buoy in the river: bobs and drifts on its chain (one-way coupled into the simulation).
	buoy = MeshInstance3D.new()
	var sb := SphereMesh.new()
	sb.radius = 0.5
	sb.height = 1.0
	buoy.mesh = sb
	var buoy_mat := StandardMaterial3D.new()
	buoy_mat.albedo_color = Color(0.95, 0.45, 0.05)
	buoy_mat.roughness = 0.4
	buoy.material_override = buoy_mat
	add_child(buoy)
	_visuals.append(buoy)


func _build_trees() -> void:
	var trunk := CylinderMesh.new()
	trunk.top_radius = 0.18
	trunk.bottom_radius = 0.28
	trunk.height = 3.0
	trunk.radial_segments = 8
	var crown := CylinderMesh.new()
	crown.top_radius = 0.0
	crown.bottom_radius = 2.2
	crown.height = 7.0
	crown.radial_segments = 10
	var bark := StandardMaterial3D.new()
	bark.albedo_color = Color(0.25, 0.18, 0.12)
	var leaves := StandardMaterial3D.new()
	leaves.albedo_color = Color(0.11, 0.21, 0.09)
	leaves.roughness = 0.9
	var rng := RandomNumberGenerator.new()
	rng.seed = 4242
	var xf: Array[Transform3D] = []
	while xf.size() < 1600:
		var x := rng.randf_range(-SIZE * 0.48, SIZE * 0.48)
		var z := rng.randf_range(-SIZE * 0.48, SIZE * 0.48)
		var y := _height(x, z)
		if y < 2.5 or (x < -60.0 and absf(z - _river_z(x)) < 16.0):
			continue
		if Vector2(x - 40.0, z - 150.0).length() < 25.0:
			continue  # the beach by the pier
		var s := rng.randf_range(0.7, 1.35)
		xf.append(Transform3D(Basis().rotated(Vector3.UP, rng.randf() * TAU).scaled(Vector3(s, s, s)), Vector3(x, y, z)))
	for part in [[trunk, bark, 1.5], [crown, leaves, 6.0]]:
		var mm := MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.mesh = part[0]
		mm.instance_count = xf.size()
		for i in range(xf.size()):
			var t: Transform3D = xf[i]
			mm.set_instance_transform(i, t.translated_local(Vector3(0.0, part[2], 0.0)))
		var mmi := MultiMeshInstance3D.new()
		mmi.multimesh = mm
		mmi.material_override = part[1]
		add_child(mmi)
		_visuals.append(mmi)


func _sim_terrain(n: int) -> Image:
	var img := Image.create_empty(n, n, false, Image.FORMAT_RF)
	for j in range(n):
		for i in range(n):
			var x := SIM_C.x + ((i + 0.5) / n - 0.5) * SIM_SIZE
			var z := SIM_C.y + ((j + 0.5) / n - 0.5) * SIM_SIZE
			var b := _mesh_height(x, z)
			for bo in BOULDERS:
				if Vector2(x - bo.x, z - (_river_z(bo.x) + bo.z)).length() < BOULDER_R * 0.9:
					b = 50.0
			if x < CULVERT_X and x > CULVERT_X - 2.0 and absf(z - _river_z(CULVERT_X)) > 5.5:
				b = 50.0  # the wall beside the culvert
			img.set_pixel(i, j, Color(b, 0, 0))
	return img


func _build_simulation() -> void:
	sim = ClassDB.instantiate("FellowShallowWater3D")
	sim.name = "River"
	sim.position = Vector3(SIM_C.x, 0.0, SIM_C.y)
	sim.resolution = sim_res
	sim.domain_size = Vector2(SIM_SIZE, SIM_SIZE)
	sim.substeps = 2
	sim.time_step = 1.0 / 120.0
	sim.initial_water_level = 0.0
	sim.terrain_image = _sim_terrain(sim_res)
	sim.inflow_depth = 0.3
	sim.inflow_velocity = Vector2(1.1, 0.0)
	sim.inflow_cells = maxi(2, sim_res / 128)
	sim.render_grid = mini(sim_res, 512)
	add_child(sim)


func _build_water() -> void:
	water = ClassDB.instantiate("FellowWater3D")
	water.name = "Lake"
	water.extent = Vector2(SIZE, SIZE)
	water.wind_speed = float(args.get("water_wind", "5.0"))
	water.wind_direction = 40.0
	water.fetch = 2.0
	water.wave_resolution = int(args.get("water_waves", "256"))
	water.screen_space_reflections = args.get("water_ssr", "1") != "0"
	if args.has("water_ssr_steps"):
		water.reflection_steps = int(args["water_ssr_steps"])
	if args.get("water_caustics", "1") == "0":
		water.caustics_strength = 0.0
	if args.get("water_refraction", "1") == "0":
		water.refraction_strength = 0.0
	water.time_auto = false
	water.debug_view = int(args.get("water_debug", "0"))
	add_child(water)
	water.simulation_path = water.get_path_to(sim)


func _build_basic_lake() -> void:
	var plane := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(SIZE, SIZE)
	plane.mesh = pm
	var mat := StandardMaterial3D.new()
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.albedo_color = Color(0.06, 0.2, 0.22, 0.75)
	mat.roughness = 0.05
	mat.metallic_specular = 0.5
	mat.refraction_enabled = true
	mat.refraction_scale = 0.03
	plane.material_override = mat
	plane.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(plane)
	_visuals.append(plane)


## Simulated seconds before the first frame, the scene hidden meanwhile (frames stay cheap): the river fills its
## channel and reaches the lake. Frame-locked like the simulation itself, so the state is the same every run.
func _preroll() -> void:
	if sim == null:
		return
	var seconds := float(args.get("water_preroll", "40"))
	var frames := int(ceil(seconds * 120.0 / 32.0))
	for v in _visuals:
		v.visible = false
	if water:
		water.visible = false
	sim.substeps = 32
	for i in range(frames):
		await get_tree().process_frame
	sim.substeps = 2
	for v in _visuals:
		v.visible = true
	if water:
		water.visible = true


func set_time(t: float) -> void:
	if water:
		water.time = t
	var bx := -138.0 + 0.8 * sin(t * 0.7)
	var bp := Vector3(bx, 0.15 + 0.06 * sin(t * 2.3), _river_z(bx) + 1.2 * sin(t * 0.45))
	var bv := Vector3(0.56 * cos(t * 0.7), 0.0, 0.54 * cos(t * 0.45))
	if buoy:
		buoy.position = bp
	if sim:
		sim.set_collider(0, bp, 0.5, bv)


func sample_extra() -> Dictionary:
	var d := {}
	if water:
		d["water_waves_gpu_ms"] = water.get_waves_gpu_time_ms()
	if sim:
		d["fluid_sim_gpu_ms"] = sim.get_simulation_gpu_time_ms()
	return d


func _define_cameras() -> void:
	fixed_cameras = {
		"water_shore": {"pos": Vector3(22.0, 1.9, 136.5), "target": Vector3(45.0, 0.0, 20.0), "fov": 60.0, "time": 20.0, "purpose": "eye level on the beach: reflections of the far shore, the pier, waves to the horizon"},
		"water_pier": {"pos": Vector3(40.0, 2.6, 111.0), "target": Vector3(42.0, -2.2, 99.0), "fov": 60.0, "time": 20.0, "purpose": "from the pier's end into clear water: refraction, absorption, caustics, the posts"},
		"water_river": {"pos": Vector3(-168.0, 5.0, 4.0), "target": Vector3(-122.0, -0.6, 0.0), "fov": 60.0, "time": 20.0, "purpose": "above the river: the flow carrying the waves, wakes at the boulders and the buoy, the mouth into the lake"},
		"water_aerial": {"pos": Vector3(-90.0, 75.0, 230.0), "target": Vector3(50.0, 0.0, 0.0), "fov": 55.0, "time": 20.0, "purpose": "overview: the lake to its shores, roughness at a distance, the river's plume"},
		"water_glint": {"pos": Vector3(95.0, 3.0, -118.0), "target": Vector3(20.0, 0.0, 0.0), "fov": 60.0, "time": 20.0, "purpose": "looking towards the low sun: the glitter path"},
	}
	var keys := []
	for i in range(13):
		var a := TAU * i / 12.0
		keys.append([i * 3.0, Vector3(LAKE_C.x + cos(a) * 150.0, 12.0 + 8.0 * sin(a * 2.0), sin(a) * 125.0), Vector3(LAKE_C.x * 0.5, 0.0, 0.0), 60.0])
	flythrough = CameraPath.from_array(keys)
	temporal_seqs = {
		"water_flow": {"start": 20.0, "frames": 60, "purpose": "still camera over the river: the flow carrying the waves, the wakes, TAA on moving water",
			"path": CameraPath.from_array([[0.0, Vector3(-150.0, 4.0, 6.0), Vector3(-138.0, -0.4, 1.0), 60.0], [1.0, Vector3(-150.0, 4.0, 6.0), Vector3(-138.0, -0.4, 1.0), 60.0]])},
		"water_pan": {"start": 20.0, "frames": 60, "purpose": "slow pan along the beach: waves and reflections in motion",
			"path": CameraPath.from_array([[0.0, Vector3(22.0, 1.9, 136.5), Vector3(45.0, 0.0, 20.0), 60.0], [1.0, Vector3(25.0, 1.9, 136.0), Vector3(55.0, 0.0, 20.0), 60.0]])},
	}
