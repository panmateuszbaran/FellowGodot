extends "res://bench/scene_base.gd"
## FIRE LAB (built in code; deterministic at a fixed frame rate): FellowEngine's gas volumes, smoke and fire
## (decisions 0117, 0118). Stock Godot has no gas simulation: on stock builds this scene reports "unsupported".
##
##   * a campfire: a FellowGasVolume3D 2.4 x 4.8 x 2.4 m over three logs (static bodies the flame flows around), fuel
##     and heat from a cylinder emitter, its light an omni light at the fire;
##   * a chimney on a house: warm smoke blown by a light wind, its shadow on the roof and the ground;
##   * a sphere (an AnimatableBody3D, driven by set_time()) circling through the campfire's smoke.
## Options: --gas_res=N (the campfire's cells along its longest side, default 64; the chimney 1.25 x), --gas_prewarm=0
## (no prewarm: the volumes start empty), --gas_render=0 (simulated, not drawn), --gas_sim=0 (no simulation),
## --night=1 (a dark sky: the fire light), --gas_chimney=0 (no chimney), --gas_torch=0 (no torch), --gas_sphere=0,
## --gas_embers=0.
##   * a torch carried in a circle (set_time()): a volume in world space, its box following the torch by whole cells, the
##     flame and smoke trailing behind it.
## Extra per-frame metrics: gas_steps (steps simulated by the campfire), fire_power (its emitted luminance).

var sun: DirectionalLight3D
var campfire
var chimney
var sphere: AnimatableBody3D
var torch: Node3D
var torch_gas
var night := false
var gas_res := 64


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "fire"
	if not ClassDB.class_exists("FellowGasVolume3D"):
		load_error = "unsupported: FellowGasVolume3D not available (stock Godot has no gas simulation)"
		return
	night = args.get("night", "0") == "1"
	gas_res = int(args.get("gas_res", "64"))
	_setup_common()
	_setup_environment()
	_build_ground()
	_build_campfire()
	if args.get("gas_chimney", "1") != "0":
		_build_house()
	if args.get("gas_sphere", "1") != "0":
		_build_sphere()
	if args.get("gas_torch", "1") != "0":
		_build_torch()
	_define_cameras()
	set_time(0.0)
	info = {"stats": scene_stats(self), "night": night,
		"campfire_grid": [campfire.get_grid_size().x, campfire.get_grid_size().y, campfire.get_grid_size().z],
		"campfire_cell_m": campfire.get_cell_size(), "campfire_vram_bytes": campfire.get_vram_bytes(),
		"chimney_grid": [chimney.get_grid_size().x, chimney.get_grid_size().y, chimney.get_grid_size().z] if chimney else [],
		"chimney_vram_bytes": chimney.get_vram_bytes() if chimney else 0,
		"notes": "GPU gas volumes: a campfire (fuel, heat, logs as obstacles, fire light) and chimney smoke in wind; one step per physics tick."}


func _setup_environment() -> void:
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var sm := ProceduralSkyMaterial.new()
	if night:
		sm.sky_top_color = Color(0.01, 0.015, 0.03)
		sm.sky_horizon_color = Color(0.03, 0.035, 0.05)
		sm.ground_horizon_color = Color(0.02, 0.02, 0.02)
		sm.ground_bottom_color = Color(0.01, 0.01, 0.01)
	else:
		sm.sky_top_color = Color(0.22, 0.38, 0.62)
		sm.sky_horizon_color = Color(0.62, 0.68, 0.74)
	sm.sun_angle_max = 6.0
	sky.sky_material = sm
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.ssao_enabled = true
	env.glow_enabled = true
	cam_attr.auto_exposure_enabled = false
	sun = DirectionalLight3D.new()
	sun.name = "sun"
	sun.light_energy = 0.04 if night else 2.0
	sun.light_color = Color(0.6, 0.7, 1.0) if night else Color(1.0, 0.97, 0.92)
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 60.0
	apply_sun_overrides(sun)
	add_child(sun)
	sun.rotation_degrees = Vector3(-38.0, 150.0, 0.0)


func _material(p_color: Color, p_roughness: float = 0.85) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = p_color
	m.roughness = p_roughness
	return m


func _static_box(p_size: Vector3, p_xform: Transform3D, p_color: Color) -> void:
	var body := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var sh := BoxShape3D.new()
	sh.size = p_size
	cs.shape = sh
	body.add_child(cs)
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = p_size
	mi.mesh = bm
	mi.material_override = _material(p_color)
	body.add_child(mi)
	body.transform = p_xform
	add_child(body)


func _build_ground() -> void:
	var mi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(400, 400)
	mi.mesh = pm
	mi.material_override = _material(Color(0.34, 0.31, 0.26), 0.95)
	add_child(mi)


func _build_campfire() -> void:
	# Stones around the fire, logs in it (obstacles of the gas).
	var stone := _material(Color(0.42, 0.41, 0.4), 0.8)
	for i in 10:
		var a := TAU * i / 10.0
		var s := MeshInstance3D.new()
		var sm := SphereMesh.new()
		sm.radius = 0.13
		sm.height = 0.2
		s.mesh = sm
		s.material_override = stone
		s.position = Vector3(cos(a) * 0.62, 0.06, sin(a) * 0.62)
		add_child(s)
	var log_color := Color(0.18, 0.11, 0.07)
	for i in 3:
		var a := TAU * i / 3.0 + 0.3
		var basis := Basis(Vector3.UP, a) * Basis(Vector3.FORWARD, 0.35)
		_static_box(Vector3(0.9, 0.11, 0.11), Transform3D(basis, Vector3(cos(a) * 0.08, 0.16, sin(a) * 0.08)), log_color)
	campfire = ClassDB.instantiate("FellowGasVolume3D")
	campfire.name = "Campfire"
	campfire.size = Vector3(2.4, 4.8, 2.4)
	campfire.resolution = gas_res
	campfire.turbulence = 6.0
	campfire.turbulence_scale = 0.3
	campfire.vorticity = 0.35
	campfire.smoke_color = Color(0.5, 0.49, 0.48)
	campfire.smoke_density = 6.0
	campfire.fire_intensity = 18.0
	campfire.prewarm_steps = 0 if args.get("gas_prewarm", "1") == "0" else 120
	campfire.simulation_enabled = args.get("gas_sim", "1") != "0"
	campfire.render_enabled = args.get("gas_render", "1") != "0"
	_scene_wind(campfire)
	var e = ClassDB.instantiate("FellowGasEmitter3D")
	e.shape = 2  # cylinder
	e.radius = 0.3
	e.height = 0.16
	e.position = Vector3(0, 0.14, 0)
	e.fuel = 2.5
	e.temperature = 900.0
	e.smoke = 0.08
	e.noise = 0.75
	e.noise_scale = 0.18
	campfire.add_child(e)
	add_child(campfire)
	if args.get("gas_embers", "1") != "0":
		_build_embers()


## Embers: GPU particles pushed by the campfire's velocity (its velocity texture in a vector field attractor).
func _build_embers() -> void:
	campfire.velocity_texture_enabled = true
	campfire.velocity_texture_scale = 4.0
	var field := GPUParticlesAttractorVectorField3D.new()
	field.size = campfire.size
	field.position = Vector3(0.0, campfire.size.y * 0.5, 0.0)
	field.texture = campfire.get_velocity_texture()
	field.strength = 14.0
	field.attenuation = 1.0
	add_child(field)
	var p := GPUParticles3D.new()
	p.amount = 64
	p.lifetime = 2.2
	p.fixed_fps = 60
	p.interpolate = false
	p.use_fixed_seed = true
	p.seed = 4242
	p.visibility_aabb = AABB(Vector3(-2.0, -0.5, -2.0), Vector3(4.0, 6.0, 4.0))
	if args.has("weather_wind"):  # downwind, as far as the wind (its gusts 1.6 times the mean) carries them in their life
		var wa := deg_to_rad(float(args.get("weather_wind_dir", "40")))
		var drift := Vector3(cos(wa), 0.0, sin(wa)) * float(args["weather_wind"]) * 1.6 * p.lifetime
		var box: AABB = p.visibility_aabb
		p.visibility_aabb = box.merge(AABB(box.position + drift, box.size)).grow(1.0)
	var pm := ParticleProcessMaterial.new()
	if args.has("weather_wind"):  # the embers drift with the scene's wind (decision 0127)
		pm.set("wind_enabled", true)
		pm.set("wind_influence", 1.0)
	pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	pm.emission_sphere_radius = 0.25
	pm.direction = Vector3(0, 1, 0)
	pm.spread = 25.0
	pm.initial_velocity_min = 0.5
	pm.initial_velocity_max = 1.5
	pm.gravity = Vector3(0, -0.6, 0)
	pm.damping_min = 2.5
	pm.damping_max = 3.5
	pm.scale_min = 0.6
	pm.scale_max = 1.0
	var fade := Curve.new()
	fade.add_point(Vector2(0.0, 1.0))
	fade.add_point(Vector2(0.7, 0.8))
	fade.add_point(Vector2(1.0, 0.0))
	var ft := CurveTexture.new()
	ft.curve = fade
	pm.scale_curve = ft
	p.process_material = pm
	var quad := QuadMesh.new()
	quad.size = Vector2(0.03, 0.03)
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	m.albedo_color = Color(1.0, 0.55, 0.18)
	m.emission_enabled = true
	m.emission = Color(1.0, 0.5, 0.15)
	m.emission_energy_multiplier = 8.0
	# Round, soft-edged sparks (a radial gradient: a hot core fading out).
	var g := Gradient.new()
	g.set_color(0, Color(1, 1, 1, 1))
	g.set_color(1, Color(1, 1, 1, 0))
	g.add_point(0.35, Color(1, 1, 1, 0.9))
	var gt := GradientTexture2D.new()
	gt.gradient = g
	gt.fill = GradientTexture2D.FILL_RADIAL
	gt.fill_from = Vector2(0.5, 0.5)
	gt.fill_to = Vector2(1.0, 0.5)
	gt.width = 32
	gt.height = 32
	m.albedo_texture = gt
	quad.material = m
	p.draw_pass_1 = quad
	p.position = Vector3(0.0, 0.25, 0.0)
	add_child(p)


func _build_house() -> void:
	var wall := Color(0.62, 0.58, 0.52)
	_static_box(Vector3(6.0, 5.0, 6.0), Transform3D(Basis(), Vector3(10.0, 2.5, -7.0)), wall)
	_static_box(Vector3(6.6, 0.3, 6.6), Transform3D(Basis(), Vector3(10.0, 5.15, -7.0)), Color(0.35, 0.22, 0.18))
	_static_box(Vector3(0.8, 2.2, 0.8), Transform3D(Basis(), Vector3(11.5, 6.4, -8.0)), Color(0.45, 0.3, 0.25))
	chimney = ClassDB.instantiate("FellowGasVolume3D")
	chimney.name = "Chimney"
	chimney.position = Vector3(11.5, 7.5, -8.0)
	chimney.size = Vector3(6.0, 9.0, 6.0)
	chimney.resolution = int(gas_res * 1.25)
	chimney.closed_faces = 0
	chimney.wind = Vector3(1.4, 0.0, 0.5)
	chimney.wind_rate = 0.3
	chimney.vorticity = 0.25
	chimney.smoke_dissipation = 0.06
	chimney.cooling = 0.6
	chimney.smoke_color = Color(0.55, 0.55, 0.56)
	chimney.smoke_density = 2.5
	chimney.fire_light = false
	chimney.prewarm_steps = 0 if args.get("gas_prewarm", "1") == "0" else 240
	chimney.simulation_enabled = args.get("gas_sim", "1") != "0"
	chimney.render_enabled = args.get("gas_render", "1") != "0"
	_scene_wind(chimney)
	var e = ClassDB.instantiate("FellowGasEmitter3D")
	e.shape = 0
	e.radius = 0.32
	e.position = Vector3(0, 0.35, 0)
	e.fuel = 0.0
	e.temperature = 160.0
	e.smoke = 1.6
	e.noise = 0.4
	e.noise_scale = 0.4
	e.velocity = Vector3(0, 1.2, 0)
	e.velocity_strength = 0.3
	chimney.add_child(e)
	add_child(chimney)


func _build_sphere() -> void:
	sphere = AnimatableBody3D.new()
	sphere.sync_to_physics = false
	var cs := CollisionShape3D.new()
	var sh := SphereShape3D.new()
	sh.radius = 0.35
	cs.shape = sh
	sphere.add_child(cs)
	var mi := MeshInstance3D.new()
	var sm := SphereMesh.new()
	sm.radius = 0.35
	sm.height = 0.7
	mi.mesh = sm
	mi.material_override = _material(Color(0.7, 0.3, 0.15), 0.4)
	sphere.add_child(mi)
	add_child(sphere)


func _build_torch() -> void:
	torch = Node3D.new()
	var stick := MeshInstance3D.new()
	var cm := CylinderMesh.new()
	cm.top_radius = 0.035
	cm.bottom_radius = 0.025
	cm.height = 0.9
	stick.mesh = cm
	stick.material_override = _material(Color(0.25, 0.16, 0.1))
	stick.position = Vector3(0.0, -0.45, 0.0)
	torch.add_child(stick)
	add_child(torch)
	torch_gas = ClassDB.instantiate("FellowGasVolume3D")
	torch_gas.name = "Torch"
	torch_gas.space = 1  # world: the gas stays behind the moving torch
	torch_gas.size = Vector3(3.0, 3.0, 3.0)
	torch_gas.resolution = int(gas_res * 0.75)
	torch_gas.vorticity = 0.3
	torch_gas.turbulence = 4.0
	torch_gas.turbulence_scale = 0.2
	torch_gas.smoke_color = Color(0.45, 0.44, 0.43)
	torch_gas.smoke_density = 5.0
	torch_gas.fire_intensity = 18.0
	torch_gas.fire_light_energy = 0.6
	torch_gas.closed_faces = 0
	torch_gas.prewarm_steps = 0 if args.get("gas_prewarm", "1") == "0" else 60
	torch_gas.simulation_enabled = args.get("gas_sim", "1") != "0"
	torch_gas.render_enabled = args.get("gas_render", "1") != "0"
	_scene_wind(torch_gas)
	var e = ClassDB.instantiate("FellowGasEmitter3D")
	e.shape = 0
	e.radius = 0.11
	e.position = Vector3(0.0, 0.45, 0.0)
	e.fuel = 2.2
	e.temperature = 1000.0
	e.smoke = 0.06
	e.noise = 0.6
	e.noise_scale = 0.12
	torch_gas.add_child(e)
	add_child(torch_gas)


func _torch_pos(t: float) -> Vector3:
	var a := t * 0.6
	return Vector3(-4.5 + 2.2 * cos(a), 1.6, 3.0 + 2.2 * sin(a))


func _sphere_pos(t: float) -> Vector3:
	return Vector3(0.85 * cos(t * 0.8), 2.6, 0.85 * sin(t * 0.8))


func set_time(t: float) -> void:
	if sphere:
		sphere.global_position = _sphere_pos(t)
	if torch:
		var p := _torch_pos(t)
		torch.global_position = p
		# The volume's floor 0.45 m below the torch's head (its emitter there).
		torch_gas.global_position = p - Vector3(0.0, 0.45, 0.0)


func sample_extra() -> Dictionary:
	if campfire == null:
		return {}
	var l: OmniLight3D = campfire.get_fire_light_node()
	return {"gas_steps": campfire.get_steps_simulated(), "fire_power": campfire.get_fire_power(),
		"fire_light_energy": l.light_energy if l and l.visible else 0.0,
		"fire_light_color": [l.light_color.r, l.light_color.g, l.light_color.b] if l else [0, 0, 0]}


func _define_cameras() -> void:
	fixed_cameras = {
		"fire_close": {"pos": Vector3(2.4, 1.2, 3.0), "target": Vector3(0.0, 1.1, 0.0), "fov": 55.0, "time": 4.0, "purpose": "the campfire: flames, smoke, the sphere in the smoke"},
		"fire_overview": {"pos": Vector3(-8.0, 6.0, 13.0), "target": Vector3(5.0, 4.0, -4.0), "fov": 60.0, "time": 4.0, "purpose": "the volumes, the chimney's plume in the wind, shadows"},
		"fire_torch": {"pos": Vector3(-2.2, 2.0, 6.4), "target": Vector3(-4.5, 1.9, 3.0), "fov": 55.0, "time": 4.0, "purpose": "the torch carried in a circle: its flame and smoke trailing (a world-space volume)"},
		"fire_chimney": {"pos": Vector3(4.0, 4.5, 5.0), "target": Vector3(13.5, 10.0, -7.0), "fov": 60.0, "time": 4.0, "purpose": "the plume against the sky, its shadow on the roof"},
	}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(-8.0, 6.0, 13.0), Vector3(5.0, 4.0, -4.0), 60.0],
		[8.0, Vector3(3.5, 2.0, 4.5), Vector3(0.0, 1.2, 0.0), 55.0],
		[14.0, Vector3(-2.5, 1.5, 2.5), Vector3(0.0, 1.3, 0.0), 55.0],
		[22.0, Vector3(4.0, 4.5, 5.0), Vector3(13.5, 10.0, -7.0), 60.0],
		[30.0, Vector3(-8.0, 6.0, 13.0), Vector3(5.0, 4.0, -4.0), 60.0],
	])
	var orbit := []
	for i in 5:
		var a := 0.4 + i * 0.25
		orbit.append([i * 0.25, Vector3(cos(a) * 3.4, 1.3, sin(a) * 3.4), Vector3(0.0, 1.2, 0.0), 55.0])
	temporal_seqs = {
		"fire_orbit": {"start": 4.0, "frames": 60, "purpose": "orbit around the campfire (the flames move, the camera too)", "path": CameraPath.from_array(orbit)},
		"fire_torch_walk": {"start": 4.0, "frames": 90, "purpose": "the torch carried past a still camera: its flame and smoke trail behind (a world-space volume)",
			"path": CameraPath.from_array([[0.0, Vector3(-2.2, 2.0, 6.4), Vector3(-4.5, 1.9, 3.0), 55.0], [1.5, Vector3(-2.2, 2.0, 6.4), Vector3(-4.5, 1.9, 3.0), 55.0]])},
	}


## --weather_wind (FellowBench, decision 0127): the volume blows with the scene's FellowWind3D, its gusts included,
## relaxing towards it at --gas_wind_rate (1/s, 0.5; the chimney keeps its own rate).
func _scene_wind(p_volume) -> void:
	if not args.has("weather_wind"):
		return
	p_volume.wind_from_scene = true
	if p_volume.wind_rate <= 0.0:
		p_volume.wind_rate = float(args.get("gas_wind_rate", "0.5"))
