extends SceneTree
## FellowEngine's weather sound in a storm (milestone 178, decision 0320): a pier over a rough sea in the rain and
## wind, lightning striking at 2 km, 600 m and 1.2 km; the camera walks along the pier in the open, under a roof and out
## again. Every sound synthesised as the scene runs (rain — on the water, on the pier, on the roof over the camera —,
## wind, thunder, the crates in the water). Record it with Movie Maker (frames and sound):
##
##   VK_ICD_FILENAMES=.../lvp_icd.json xvfb-run -a godot --path benchmarks/tools/world_sound --rendering-driver vulkan \
##       --resolution 960x540 --fixed-fps 30 --write-movie <dir>/m.png -s storm_demo.gd [-- --seconds=16]
##
## The weather nodes' sound statistics printed at the end; --timing: the viewport's GPU and CPU render times from 2 s
## ("TIMING ..."); --silent: every sound off (for the timing without it).

var _seconds := 16.0
var _timing := false
var _silent := false
var _gpu_ms := []
var _cpu_ms := []
var _frame := 0
var _cam: Camera3D
var _rain: Node3D
var _wind: Node3D
var _lightning: Node3D
var _water: Node3D
var _strikes := [[1.2, 2000.0, 200.0], [5.6, 600.0, 250.0], [10.8, 1200.0, 300.0]]


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--seconds="):
			_seconds = float(a.get_slice("=", 1))
		elif a == "--timing":
			_timing = true
		elif a == "--silent":
			_silent = true
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var sm := ProceduralSkyMaterial.new()
	sm.sky_top_color = Color(0.16, 0.18, 0.22)
	sm.sky_horizon_color = Color(0.32, 0.34, 0.37)
	sm.ground_horizon_color = Color(0.2, 0.21, 0.22)
	sky.sky_material = sm
	e.sky = sky
	e.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	e.ambient_light_energy = 0.7
	e.tonemap_mode = Environment.TONE_MAPPER_AGX
	e.fog_enabled = true
	e.fog_density = 0.004
	e.fog_light_color = Color(0.3, 0.32, 0.35)
	env.environment = e
	root.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50.0, -120.0, 0.0)
	sun.light_energy = 0.35
	sun.shadow_enabled = true
	root.add_child(sun)
	_water = ClassDB.instantiate("FellowWater3D")
	_water.set("wind_speed", 11.0)
	_water.set("fetch", 8.0)
	_water.set("wind_direction", 200.0)
	root.add_child(_water)
	_water.set("sun_path", _water.get_path_to(sun))
	_wind = ClassDB.instantiate("FellowWind3D")
	_wind.set("speed", 13.0)
	_wind.set("gustiness", 0.7)
	_wind.rotation_degrees = Vector3(0, 200, 0)
	root.add_child(_wind)
	_rain = ClassDB.instantiate("FellowRain3D")
	_rain.set("rain", 0.8)
	_rain.set("wind_from_scene", true)
	root.add_child(_rain)
	_lightning = ClassDB.instantiate("FellowLightning3D")
	_lightning.set("strikes_per_minute", 0.0)
	root.add_child(_lightning)
	# The pier: a deck on posts running out over the water, a roofed shelter halfway along it.
	var wood := StandardMaterial3D.new()
	wood.albedo_color = Color(0.36, 0.27, 0.19)
	wood.roughness = 0.85
	_box(Vector3(3.0, 0.25, 30.0), Vector3(0.0, 1.0, 0.0), wood)
	for z in range(-14, 15, 4):
		for x in [-1.3, 1.3]:
			_box(Vector3(0.25, 4.0, 0.25), Vector3(x, -1.0, float(z)), wood)
	_box(Vector3(4.0, 0.2, 6.0), Vector3(0.0, 4.0, 2.0), wood) # the roof
	for p in [Vector3(-1.4, 2.5, -0.8), Vector3(1.4, 2.5, -0.8), Vector3(-1.4, 2.5, 4.8), Vector3(1.4, 2.5, 4.8)]:
		_box(Vector3(0.2, 3.0, 0.2), p, wood)
	# Crates in the water beside it.
	var crate := StandardMaterial3D.new()
	crate.albedo_color = Color(0.55, 0.4, 0.24)
	var rng := RandomNumberGenerator.new()
	rng.seed = 11
	for i in 6:
		_crate(Vector3(rng.randf_range(3.0, 9.0) * (1 if i % 2 == 0 else -1), 0.6, rng.randf_range(-10.0, 10.0)), crate)
	if _silent:
		for n: Node in [_water, _wind, _rain, _lightning]:
			n.set("sound_enabled", false)
	_cam = Camera3D.new()
	_cam.fov = 60.0
	_cam.far = 4000.0
	root.add_child(_cam)
	_place_camera(0.0)


func _box(p_size: Vector3, p_at: Vector3, p_mat: Material) -> void:
	var b := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var s := BoxShape3D.new()
	s.size = p_size
	cs.shape = s
	b.add_child(cs)
	var mi := MeshInstance3D.new()
	var m := BoxMesh.new()
	m.size = p_size
	mi.mesh = m
	mi.material_override = p_mat
	b.add_child(mi)
	b.position = p_at
	root.add_child(b)


func _crate(p_at: Vector3, p_mat: Material) -> void:
	var body := RigidBody3D.new()
	var cs := CollisionShape3D.new()
	var s := BoxShape3D.new()
	s.size = Vector3(0.7, 0.7, 0.7)
	cs.shape = s
	body.add_child(cs)
	var mi := MeshInstance3D.new()
	var m := BoxMesh.new()
	m.size = s.size
	mi.mesh = m
	mi.material_override = p_mat
	body.add_child(mi)
	body.mass = 400.0 * 0.343
	body.add_child(ClassDB.instantiate("FellowBuoyancy3D"))
	body.position = p_at
	root.add_child(body)


# Along the pier: the open end (z -12) to under the roof (z 2) by 6 s, under it until 11 s, out to z 10 by 16 s.
func _place_camera(p_t: float) -> void:
	var z: float
	if p_t < 6.0:
		z = lerpf(-12.0, 2.0, smoothstep(0.5, 6.0, p_t))
	elif p_t < 11.0:
		z = 2.0
	else:
		z = lerpf(2.0, 10.0, smoothstep(11.0, 16.0, p_t))
	var at := Vector3(0.0, 2.7, z)
	_cam.look_at_from_position(at, at + Vector3(-0.6, -0.12, -1.0))


func _process(_d: float) -> bool:
	var t := float(_frame) / 30.0
	_place_camera(t)
	for s in _strikes:
		if s.size() == 3 and t >= s[0]:
			_lightning.call("strike", s[1], s[2])
			s.append(true)
	_frame += 1
	if _timing:
		var vp := root.get_viewport_rid()
		if _frame == 1:
			RenderingServer.viewport_set_measure_render_time(vp, true)
		elif t >= 2.0:
			_gpu_ms.append(RenderingServer.viewport_get_measured_render_time_gpu(vp))
			_cpu_ms.append(RenderingServer.viewport_get_measured_render_time_cpu(vp))
	if t >= _seconds:
		if _timing:
			var g := 0.0
			var c := 0.0
			for v in _gpu_ms:
				g += v
			for v in _cpu_ms:
				c += v
			print("TIMING frames %d gpu_ms_mean %.3f cpu_ms_mean %.3f %s" % [_gpu_ms.size(), g / maxf(1.0, _gpu_ms.size()), c / maxf(1.0, _cpu_ms.size()), "silent" if _silent else "sound"])
		print("rain: ", _rain.call("get_sound_stats"))
		print("wind: ", _wind.call("get_sound_stats"))
		print("thunder: ", _lightning.call("get_sound_stats"))
		print("water: ", _water.call("get_sound_stats"))
		return true
	return false
