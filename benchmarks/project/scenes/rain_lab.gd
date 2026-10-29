extends "res://bench/scene_base.gd"
## BENCHMARK - Rain Lab (milestone 25): a town street in the rain (FellowEngine FellowRain3D: drops around the camera,
## every opaque material wetted where the rain reaches it, puddles with ripples; a FellowWater3D basin). Stock Godot
## has no rain: --rain=off is the scene dry, as stock draws it.
##
## A 140 m street between two rows of buildings (plaster, brick, flat roofs), asphalt with sidewalks, a cobbled square
## with a fountain basin, an arcade along the south row (its floor must stay dry), a bus shelter, a parked car (metal
## paint: not porous), benches, trees on grass, street lamps.
## Options:
##   --rain=on|dry|off   on: raining and wet (default); dry: FellowRain3D present but no rain, dry (its cost when idle);
##                       off: no FellowRain3D
##   --rain_amount=<0..1> (0.7), --rain_wetness=<0..1> (1), --rain_puddles=<0..1> (0.55), --rain_drops=<n> (24000)
##   --rain_light=day|dusk  overcast day (default) or dusk with the street lamps on
##   --rain_occlusion=moved|always  the shelter heightfield rendered when it moves with the camera (default) or every frame
## Extra per-frame metrics: none (the drops are GPU particles; the wetting is in the scene's passes).

const LENGTH := 140.0
const STREET_W := 9.0
const WALK_W := 4.0
const SQUARE := Vector2(30.0, 22.0) # the square's size (x, z), centred at x = 20 on the north side

var mode := "on"
var light := "day"
var rain  # FellowRain3D
var basin  # FellowWater3D
var sun: DirectionalLight3D
var _mats := {}
var _rng := RandomNumberGenerator.new()


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "rain"
	mode = str(args.get("rain", "on"))
	light = str(args.get("rain_light", "day"))
	if mode != "off" and not ClassDB.class_exists("FellowRain3D"):
		load_error = "unsupported: FellowRain3D not available (stock Godot has no rain)"
		return
	_rng.seed = 2510
	_setup_common()
	camera.far = 1500.0
	_setup_environment()
	_make_materials()
	_build_ground()
	_build_buildings()
	_build_arcade()
	_build_props()
	_build_basin()
	if mode != "off":
		rain = ClassDB.instantiate("FellowRain3D")
		rain.name = "Rain"
		rain.time_auto = false
		rain.rain = float(args.get("rain_amount", "0.7")) if mode == "on" else 0.0
		rain.wetness = float(args.get("rain_wetness", "1.0")) if mode == "on" else 0.0
		rain.puddles = float(args.get("rain_puddles", "0.55"))
		rain.max_drops = int(args.get("rain_drops", "24000"))
		rain.occlusion_always_update = str(args.get("rain_occlusion", "moved")) == "always"
		rain.wind = Vector3(1.2, 0.0, 0.6)
		rain.drop_color = Color(0.70, 0.74, 0.78, 0.40) if light == "day" else Color(0.55, 0.55, 0.6, 0.32)
		add_child(rain)
	_define_cameras()
	set_time(0.0)
	info = {"mode": mode, "light": light, "stats": scene_stats(self)}


func _setup_environment() -> void:
	var sky := Sky.new()
	var sm := ProceduralSkyMaterial.new()
	if light == "dusk":
		sm.sky_top_color = Color(0.05, 0.07, 0.12)
		sm.sky_horizon_color = Color(0.20, 0.18, 0.20)
		sm.ground_horizon_color = Color(0.12, 0.11, 0.11)
	else:
		sm.sky_top_color = Color(0.36, 0.40, 0.45)
		sm.sky_horizon_color = Color(0.58, 0.60, 0.62)
		sm.ground_horizon_color = Color(0.30, 0.30, 0.30)
	sm.ground_bottom_color = Color(0.1, 0.1, 0.1)
	sm.sun_angle_max = 30.0
	sky.sky_material = sm
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.ssao_enabled = true
	env.ssr_enabled = true
	env.ssr_max_steps = 64
	env.glow_enabled = true
	env.fog_enabled = true
	env.fog_density = 0.006 if light == "day" else 0.008
	env.fog_light_color = Color(0.55, 0.57, 0.6) if light == "day" else Color(0.12, 0.12, 0.15)
	env.fog_sky_affect = 0.3
	sun = DirectionalLight3D.new()
	sun.name = "sun"
	var e := deg_to_rad(38.0 if light == "day" else 6.0)
	sun.basis = Basis.looking_at(Vector3(0.5 * cos(e), -sin(e), -0.85 * cos(e)).normalized(), Vector3.UP)
	sun.light_energy = 0.45 if light == "day" else 0.08
	sun.light_color = Color(0.9, 0.92, 1.0) if light == "day" else Color(1.0, 0.6, 0.4)
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 120.0
	add_child(sun)
	apply_sun_overrides(sun)


func _noise_texture(seed: int, scale: float, lo: float, hi: float) -> ImageTexture:
	var n := 128
	var img := Image.create_empty(n, n, false, Image.FORMAT_L8)
	var rng := RandomNumberGenerator.new()
	rng.seed = seed
	var grid := PackedFloat32Array()
	var g := 16
	grid.resize(g * g)
	for i in range(g * g):
		grid[i] = rng.randf()
	for y in range(n):
		for x in range(n):
			var fx := x * g / float(n)
			var fy := y * g / float(n)
			var ix := int(fx)
			var iy := int(fy)
			var tx := fx - ix
			var ty := fy - iy
			var a := grid[(iy % g) * g + ix % g]
			var b := grid[(iy % g) * g + (ix + 1) % g]
			var c := grid[((iy + 1) % g) * g + ix % g]
			var d := grid[((iy + 1) % g) * g + (ix + 1) % g]
			var v := lerpf(lerpf(a, b, tx), lerpf(c, d, tx), ty) * 0.7 + rng.randf() * 0.3
			v = lerpf(lo, hi, v)
			img.set_pixel(x, y, Color(v, v, v))
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)


func _mat(key: String, color: Color, rough: float, metal: float = 0.0, tex_seed: int = 0, uv_scale: float = 1.0) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = color
	m.roughness = rough
	m.metallic = metal
	if tex_seed != 0:
		m.albedo_texture = _noise_texture(tex_seed, 1.0, 0.75, 1.15)
		m.uv1_triplanar = true
		m.uv1_scale = Vector3(uv_scale, uv_scale, uv_scale)
	_mats[key] = m
	return m


func _make_materials() -> void:
	_mat("asphalt", Color(0.11, 0.11, 0.115), 0.75, 0.0, 11, 0.6)
	_mat("walk", Color(0.42, 0.41, 0.39), 0.85, 0.0, 12, 0.5)
	_mat("cobble", Color(0.33, 0.29, 0.25), 0.8, 0.0, 13, 1.2)
	_mat("plaster", Color(0.72, 0.66, 0.56), 0.9, 0.0, 14, 0.2)
	_mat("plaster2", Color(0.55, 0.60, 0.62), 0.9, 0.0, 15, 0.2)
	_mat("brick", Color(0.42, 0.18, 0.12), 0.85, 0.0, 16, 0.8)
	_mat("roof", Color(0.25, 0.24, 0.24), 0.8)
	_mat("window", Color(0.08, 0.1, 0.12), 0.08, 0.0)
	_mat("stone", Color(0.5, 0.49, 0.46), 0.75, 0.0, 17, 0.5)
	_mat("wood", Color(0.36, 0.24, 0.14), 0.75)
	_mat("grass", Color(0.18, 0.28, 0.10), 0.9)
	_mat("leaves", Color(0.12, 0.22, 0.09), 0.85)
	_mat("bark", Color(0.22, 0.16, 0.11), 0.9)
	_mat("car", Color(0.55, 0.06, 0.05), 0.3, 0.6)
	_mat("chrome", Color(0.8, 0.8, 0.8), 0.15, 1.0)
	_mat("glass", Color(0.05, 0.07, 0.08), 0.05, 0.0)
	_mat("metal", Color(0.2, 0.21, 0.22), 0.4, 0.8)
	var lamp := StandardMaterial3D.new()
	lamp.albedo_color = Color(1.0, 0.85, 0.6)
	lamp.emission_enabled = true
	lamp.emission = Color(1.0, 0.8, 0.55)
	lamp.emission_energy_multiplier = 0.4 if light == "day" else 6.0
	_mats["lamp"] = lamp


func _box(size: Vector3, pos: Vector3, mat: String, rot_y: float = 0.0) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var b := BoxMesh.new()
	b.size = size
	mi.mesh = b
	mi.material_override = _mats[mat]
	mi.position = pos
	mi.rotation.y = rot_y
	add_child(mi)
	return mi


func _build_ground() -> void:
	# The road along x, sidewalks either side (15 cm kerb), the square north of the north walk, grass beyond.
	_box(Vector3(LENGTH, 0.2, STREET_W), Vector3(0, -0.1, 0), "asphalt")
	for side in [-1.0, 1.0]:
		_box(Vector3(LENGTH, 0.35, WALK_W), Vector3(0, -0.025, side * (STREET_W + WALK_W) * 0.5), "walk")
	_box(Vector3(SQUARE.x, 0.35, SQUARE.y), Vector3(20.0, -0.025, STREET_W * 0.5 + WALK_W + SQUARE.y * 0.5), "cobble")
	_box(Vector3(LENGTH + 60.0, 0.2, 200.0), Vector3(0, -0.2, 0), "grass")


func _build_buildings() -> void:
	var mats := ["plaster", "plaster2", "brick"]
	for side in [-1.0, 1.0]:
		var x := -LENGTH * 0.5 + 4.0
		while x < LENGTH * 0.5 - 4.0:
			var w := _rng.randf_range(9.0, 15.0)
			var h := _rng.randf_range(8.0, 16.0)
			var d := 10.0
			var z: float = side * (STREET_W * 0.5 + WALK_W + d * 0.5)
			# The square's frontage is open on the north side.
			if side > 0.0 and absf(x + w * 0.5 - 20.0) < SQUARE.x * 0.5 + w * 0.5:
				x += w + 1.0
				continue
			var cx := x + w * 0.5
			_box(Vector3(w, h, d), Vector3(cx, h * 0.5, z), mats[_rng.randi() % 3])
			_box(Vector3(w + 0.4, 0.3, d + 0.4), Vector3(cx, h + 0.15, z), "roof")
			# Windows on the street face: dark glass, smooth (not porous: they glisten, they do not darken).
			var face: float = z - side * (d * 0.5 + 0.02)
			var floors := int(h / 3.2)
			for f in range(floors):
				for k in range(int(w / 3.0)):
					var wx := x + 1.5 + k * 3.0
					_box(Vector3(1.4, 1.6, 0.05), Vector3(wx, 1.8 + f * 3.2, face), "window")
			x += w + 1.0


func _build_arcade() -> void:
	# Along the south row, x from -40 to -10: a 4 m roof 3.2 m wide over the sidewalk, columns every 3 m. Its floor (the
	# sidewalk under it) must stay dry; the rain falls just outside.
	var z := -(STREET_W * 0.5 + WALK_W * 0.5)
	_box(Vector3(30.0, 0.3, WALK_W + 0.2), Vector3(-25.0, 4.0, z), "stone")
	var x := -39.5
	while x <= -10.0:
		var c := MeshInstance3D.new()
		var cm := CylinderMesh.new()
		cm.top_radius = 0.22
		cm.bottom_radius = 0.26
		cm.height = 4.0
		c.mesh = cm
		c.material_override = _mats["stone"]
		c.position = Vector3(x, 2.0, z + WALK_W * 0.5 - 0.3)
		add_child(c)
		x += 3.0


func _build_props() -> void:
	# A bus shelter on the north walk (glass roof on metal posts).
	var zn := STREET_W * 0.5 + WALK_W * 0.5
	_box(Vector3(4.0, 0.08, 1.8), Vector3(-5.0, 2.6, zn), "glass")
	for px in [-6.9, -3.1]:
		_box(Vector3(0.08, 2.6, 0.08), Vector3(px, 1.3, zn + 0.8), "metal")
	# A parked car on the south lane.
	var car := Node3D.new()
	car.position = Vector3(8.0, 0.0, -2.4)
	add_child(car)
	for part in [[Vector3(4.2, 0.7, 1.8), Vector3(0, 0.6, 0), "car"], [Vector3(2.4, 0.6, 1.6), Vector3(-0.2, 1.25, 0), "glass"], [Vector3(4.3, 0.1, 1.85), Vector3(0, 0.3, 0), "chrome"]]:
		var mi := MeshInstance3D.new()
		var b := BoxMesh.new()
		b.size = part[0]
		mi.mesh = b
		mi.material_override = _mats[part[2]]
		mi.position = part[1]
		car.add_child(mi)
	for wx in [-1.4, 1.4]:
		for wz in [-0.85, 0.85]:
			var wheel := MeshInstance3D.new()
			var cm := CylinderMesh.new()
			cm.top_radius = 0.34
			cm.bottom_radius = 0.34
			cm.height = 0.25
			wheel.mesh = cm
			wheel.material_override = _mats["metal"]
			wheel.position = Vector3(wx, 0.34, wz)
			wheel.rotation.x = PI * 0.5
			car.add_child(wheel)
	# Benches and trees around the square, street lamps along the north walk.
	for bx in [12.0, 20.0, 28.0]:
		_box(Vector3(1.8, 0.08, 0.5), Vector3(bx, 0.45, zn + WALK_W * 0.5 + 18.0), "wood")
		for lx in [-0.75, 0.75]:
			_box(Vector3(0.08, 0.45, 0.45), Vector3(bx + lx, 0.22, zn + WALK_W * 0.5 + 18.0), "metal")
	for t in range(6):
		var tx := 8.0 + t * 5.0
		var tz: float = zn + WALK_W * 0.5 + (2.5 if t % 2 == 0 else 19.5)
		var trunk := MeshInstance3D.new()
		var tm := CylinderMesh.new()
		tm.top_radius = 0.12
		tm.bottom_radius = 0.18
		tm.height = 3.0
		trunk.mesh = tm
		trunk.material_override = _mats["bark"]
		trunk.position = Vector3(tx, 1.5, tz)
		add_child(trunk)
		var crown := MeshInstance3D.new()
		var sm := SphereMesh.new()
		sm.radius = 1.8
		sm.height = 3.2
		crown.mesh = sm
		crown.material_override = _mats["leaves"]
		crown.position = Vector3(tx, 4.2, tz)
		add_child(crown)
	var lx := -60.0
	while lx <= 60.0:
		var pole := MeshInstance3D.new()
		var pm := CylinderMesh.new()
		pm.top_radius = 0.06
		pm.bottom_radius = 0.09
		pm.height = 5.0
		pole.mesh = pm
		pole.material_override = _mats["metal"]
		pole.position = Vector3(lx, 2.5, STREET_W * 0.5 + 0.5)
		add_child(pole)
		var bulb := MeshInstance3D.new()
		var bm := SphereMesh.new()
		bm.radius = 0.22
		bm.height = 0.44
		bulb.mesh = bm
		bulb.material_override = _mats["lamp"]
		bulb.position = Vector3(lx, 5.0, STREET_W * 0.5 + 0.2)
		add_child(bulb)
		if light == "dusk":
			var ol := OmniLight3D.new()
			ol.position = Vector3(lx, 4.8, STREET_W * 0.5 + 0.2)
			ol.light_color = Color(1.0, 0.78, 0.5)
			ol.light_energy = 3.0
			ol.omni_range = 16.0
			ol.shadow_enabled = false
			add_child(ol)
		lx += 15.0


func _build_basin() -> void:
	# A fountain basin on the square: a stone rim, water 40 cm deep (FellowWater3D, its extent the basin's).
	var c := Vector3(20.0, 0.0, STREET_W * 0.5 + WALK_W + SQUARE.y * 0.5)
	var size := Vector2(8.0, 5.0)
	for s in [[Vector3(size.x + 0.6, 0.6, 0.3), Vector3(0, 0.3, size.y * 0.5 + 0.15)], [Vector3(size.x + 0.6, 0.6, 0.3), Vector3(0, 0.3, -size.y * 0.5 - 0.15)], [Vector3(0.3, 0.6, size.y), Vector3(size.x * 0.5 + 0.15, 0.3, 0)], [Vector3(0.3, 0.6, size.y), Vector3(-size.x * 0.5 - 0.15, 0.3, 0)]]:
		_box(s[0], c + s[1], "stone")
	_box(Vector3(size.x, 0.1, size.y), c + Vector3(0, -0.25, 0), "stone")
	if ClassDB.class_exists("FellowWater3D"):
		basin = ClassDB.instantiate("FellowWater3D")
		basin.name = "Basin"
		basin.position = c + Vector3(0, 0.4, 0)
		basin.extent = size
		basin.wind_speed = 1.5
		basin.fetch = 0.01
		basin.wave_resolution = 128
		basin.time_auto = false
		add_child(basin)


func set_time(t: float) -> void:
	if rain:
		rain.time = t
	if basin:
		basin.time = t


func _define_cameras() -> void:
	var zn := STREET_W * 0.5 + WALK_W * 0.5
	fixed_cameras = {
		"rain_street": {"pos": Vector3(-30.0, 1.7, 1.5), "target": Vector3(20.0, 0.5, -1.0), "fov": 60.0, "time": 10.0, "purpose": "eye level down the wet street: puddles reflecting the buildings, the car"},
		"rain_arcade": {"pos": Vector3(-36.0, 1.7, -(STREET_W * 0.5 + WALK_W * 0.5) - 0.5), "target": Vector3(-12.0, 0.8, -1.0), "fov": 65.0, "time": 10.0, "purpose": "under the arcade: its floor dry, the street wet, the drops beyond the columns"},
		"rain_puddle": {"pos": Vector3(2.0, 1.6, -1.0), "target": Vector3(5.0, -0.3, 1.5), "fov": 55.0, "time": 10.0, "purpose": "looking down at the asphalt: puddles, ripples, the film of water"},
		"rain_square": {"pos": Vector3(6.0, 2.2, zn + 4.0), "target": Vector3(20.0, 0.3, zn + WALK_W * 0.5 + 11.0), "fov": 60.0, "time": 10.0, "purpose": "the square: wet cobbles, the basin's water with the rain's ripples, the benches, trees"},
		"rain_aerial": {"pos": Vector3(-45.0, 28.0, -30.0), "target": Vector3(0.0, 0.0, 4.0), "fov": 55.0, "time": 10.0, "purpose": "overview: roofs (wet, puddles), the street, the arcade's dry strip"},
	}
	var keys := []
	for i in range(9):
		var f := i / 8.0
		keys.append([i * 3.0, Vector3(lerpf(-60.0, 55.0, f), 1.8 + 1.5 * sin(f * TAU), lerpf(-1.5, 1.5, f)), Vector3(lerpf(-40.0, 75.0, f), 1.0, 0.0), 60.0])
	flythrough = CameraPath.from_array(keys)
	temporal_seqs = {
		"rain_walk": {"start": 10.0, "frames": 60, "purpose": "walking down the street in the rain: the drops, the ripples, TAA on them",
			"path": CameraPath.from_array([[0.0, Vector3(-30.0, 1.7, 1.5), Vector3(20.0, 0.5, -1.0), 60.0], [1.0, Vector3(-28.8, 1.7, 1.5), Vector3(21.0, 0.5, -1.0), 60.0]])},
	}
