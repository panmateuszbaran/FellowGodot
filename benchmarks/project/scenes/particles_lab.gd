extends "res://bench/scene_base.gd"
## PARTICLES LAB (built in code; deterministic at a fixed frame rate): many GPU particle emitters.
##
## No other benchmark scene has GPU particles. Games scatter many small emitters (torches, smoke, sparks, dust), and
## each costs the renderer work every frame whatever its size: its frame parameters uploaded, its process shader
## dispatched, its particles copied (and sorted for blending) into the buffer its draw reads, then drawn in the depth,
## colour and shadow passes. EMITTERS emitters (--emitters=N, default 400) in a grid over a ground with low walls, a
## third of each kind:
##   * fire: 48 additive billboards rising, 0.9 s lifetime;
##   * smoke: 96 alpha-blended billboards sorted by view depth, 3 s lifetime, growing;
##   * sparks: 160 small stretched billboards falling under gravity, 1.4 s lifetime.
## Every emitter has a fixed seed, a fixed process rate of 60 steps per second and 2 s of preprocess, so its particles
## depend only on the frames simulated: with the bench's --fixed-fps the same frames on every run. The simulation runs on
## the engine's clock (the particles are not driven by set_time(), which only moves nothing here); the camera paths are.

const EMITTERS := 400
const SPACING := 3.0

var sun: DirectionalLight3D
var emitters := 400
var _area := 60.0


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "particles"
	emitters = int(args.get("emitters", str(EMITTERS)))
	_area = maxf(20.0, sqrt(float(emitters)) * SPACING)
	_setup_common()
	_setup_environment()
	_build_ground()
	_build_emitters()
	_build_lights()
	_define_cameras()
	info = {"stats": scene_stats(self), "emitters": emitters,
		"notes": "GPU particles: fire (48, additive), smoke (96, alpha, sorted by depth), sparks (160) emitters in a grid; fixed seeds and process rate."}


func _setup_environment() -> void:
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var sm := ProceduralSkyMaterial.new()
	sm.sky_top_color = Color(0.12, 0.14, 0.2)
	sm.sky_horizon_color = Color(0.3, 0.28, 0.3)
	sm.ground_horizon_color = Color(0.25, 0.22, 0.2)
	sky.sky_material = sm
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.ssao_enabled = true
	env.glow_enabled = true
	cam_attr.auto_exposure_enabled = false


func _build_ground() -> void:
	var plane := PlaneMesh.new()
	plane.size = Vector2(_area * 1.6, _area * 1.6)
	var mi := MeshInstance3D.new()
	mi.mesh = plane
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(0.32, 0.3, 0.28)
	m.roughness = 0.85
	mi.material_override = m
	add_child(mi)
	var wall := BoxMesh.new()
	wall.size = Vector3(SPACING * 0.9, 0.8, 0.25)
	var wm := StandardMaterial3D.new()
	wm.albedo_color = Color(0.45, 0.42, 0.38)
	wm.roughness = 0.9
	var side := int(ceil(sqrt(float(emitters))))
	for i in side * side / 2:
		var w := MeshInstance3D.new()
		w.mesh = wall
		w.material_override = wm
		var gx := float((i * 2) % side) - float(side - 1) * 0.5
		var gz := float((i * 2) / side) - float(side - 1) * 0.5
		w.position = Vector3(gx * SPACING, 0.4, gz * SPACING + SPACING * 0.5)
		add_child(w)


func _quad_material(p_color: Color, p_additive: bool, p_stretch: bool) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED if p_additive else BaseMaterial3D.SHADING_MODE_PER_PIXEL
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.blend_mode = BaseMaterial3D.BLEND_MODE_ADD if p_additive else BaseMaterial3D.BLEND_MODE_MIX
	m.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	m.billboard_keep_scale = true
	m.vertex_color_use_as_albedo = true
	m.albedo_color = p_color
	m.albedo_texture = _soft_dot()
	if p_stretch:
		m.billboard_mode = BaseMaterial3D.BILLBOARD_FIXED_Y
	return m


var _dot: ImageTexture


func _soft_dot() -> ImageTexture:
	if _dot:
		return _dot
	var img := Image.create(32, 32, false, Image.FORMAT_RGBA8)
	for y in 32:
		for x in 32:
			var d := Vector2(x - 15.5, y - 15.5).length() / 15.5
			var a := clampf(1.0 - d, 0.0, 1.0)
			img.set_pixel(x, y, Color(1, 1, 1, a * a))
	_dot = ImageTexture.create_from_image(img)
	return _dot


func _make_emitter(kind: int, p_seed: int) -> GPUParticles3D:
	var p := GPUParticles3D.new()
	var pm := ParticleProcessMaterial.new()
	# --weather_wind (FellowBench, decision 0127): the particles drift with the scene's FellowWind3D.
	if args.has("weather_wind"):
		pm.set("wind_enabled", true)
		pm.set("wind_influence", float(args.get("particles_wind_influence", "1.5")))
	var quad := QuadMesh.new()
	match kind:
		0: # fire
			p.amount = 48
			p.lifetime = 0.9
			pm.direction = Vector3(0, 1, 0)
			pm.spread = 12.0
			pm.initial_velocity_min = 0.8
			pm.initial_velocity_max = 1.4
			pm.gravity = Vector3(0, 0.6, 0)
			pm.scale_min = 0.6
			pm.scale_max = 1.0
			pm.color = Color(1.0, 0.55, 0.2)
			quad.size = Vector2(0.4, 0.4)
			quad.material = _quad_material(Color(1.0, 0.6, 0.25, 0.8), true, false)
		1: # smoke
			p.amount = 96
			p.lifetime = 3.0
			p.draw_order = GPUParticles3D.DRAW_ORDER_VIEW_DEPTH
			pm.direction = Vector3(0, 1, 0)
			pm.spread = 20.0
			pm.initial_velocity_min = 0.4
			pm.initial_velocity_max = 0.7
			pm.gravity = Vector3(0.15, 0.25, 0)
			pm.scale_min = 0.8
			pm.scale_max = 1.6
			var curve := CurveTexture.new()
			var c := Curve.new()
			c.add_point(Vector2(0, 0.4))
			c.add_point(Vector2(1, 1.0))
			curve.curve = c
			pm.scale_curve = curve
			pm.color = Color(0.35, 0.34, 0.33, 0.5)
			quad.size = Vector2(0.9, 0.9)
			quad.material = _quad_material(Color(0.5, 0.5, 0.5, 0.45), false, false)
		_: # sparks
			p.amount = 160
			p.lifetime = 1.4
			pm.direction = Vector3(0, 1, 0)
			pm.spread = 35.0
			pm.initial_velocity_min = 2.0
			pm.initial_velocity_max = 3.5
			pm.gravity = Vector3(0, -6.0, 0)
			pm.scale_min = 0.5
			pm.scale_max = 1.0
			pm.color = Color(1.0, 0.8, 0.4)
			quad.size = Vector2(0.06, 0.18)
			quad.material = _quad_material(Color(1.0, 0.85, 0.5, 1.0), true, true)
	pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	pm.emission_sphere_radius = 0.15
	p.process_material = pm
	p.draw_pass_1 = quad
	p.fixed_fps = 60
	p.interpolate = false
	p.use_fixed_seed = true
	p.seed = p_seed
	p.preprocess = 2.0
	p.visibility_aabb = AABB(Vector3(-3, -1, -3), Vector3(6, 7, 6))
	if args.has("weather_wind"):  # downwind, as far as the wind (its gusts 1.6 times the mean) carries them in their life
		var wa := deg_to_rad(float(args.get("weather_wind_dir", "40")))
		var drift: Vector3 = p.transform.basis.inverse() * (Vector3(cos(wa), 0.0, sin(wa)) * float(args["weather_wind"]) * 1.6 * p.lifetime)
		var box: AABB = p.visibility_aabb
		p.visibility_aabb = box.merge(AABB(box.position + drift, box.size)).grow(1.0)
	return p


func _build_emitters() -> void:
	var side := int(ceil(sqrt(float(emitters))))
	for i in emitters:
		var p := _make_emitter(i % 3, 1000 + i)
		var gx := float(i % side) - float(side - 1) * 0.5
		var gz := float(i / side) - float(side - 1) * 0.5
		p.position = Vector3(gx * SPACING, 0.2, gz * SPACING)
		add_child(p)


func _build_lights() -> void:
	sun = DirectionalLight3D.new()
	sun.name = "sun"
	sun.light_energy = 0.8
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 80.0
	sun.light_angular_distance = 0.5
	apply_sun_overrides(sun)
	add_child(sun)
	sun.rotation_degrees = Vector3(-40.0, 30.0, 0.0)


func _define_cameras() -> void:
	var h := _area * 0.5
	fixed_cameras = {
		"particles_aerial": {"pos": Vector3(-h, h * 0.9, h), "target": Vector3(0, 0, 0), "fov": 60.0, "time": 2.0, "purpose": "every emitter in view"},
		"particles_street": {"pos": Vector3(1.5, 1.7, h * 0.6), "target": Vector3(0, 1.0, -h), "fov": 65.0, "time": 2.0, "purpose": "eye level along a row: near smoke fills the screen, layers of blending"},
		"particles_close": {"pos": Vector3(1.2, 1.4, 2.2), "target": Vector3(0, 1.2, 0), "fov": 55.0, "time": 2.0, "purpose": "a few emitters close up"},
	}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(-h, h * 0.9, h), Vector3(0, 0, 0), 60.0],
		[8.0, Vector3(h * 0.5, h * 0.4, h * 0.6), Vector3(0, 0.5, 0), 60.0],
		[16.0, Vector3(1.5, 1.7, h * 0.6), Vector3(0, 1.0, -h), 65.0],
		[22.0, Vector3(1.2, 1.4, 2.2), Vector3(0, 1.2, 0), 55.0],
		[30.0, Vector3(h, h * 0.6, -h), Vector3(0, 0, 0), 60.0],
	])
	temporal_seqs = {
		"particles_pan": {"start": 3.0, "frames": 60, "purpose": "pan along a row at eye level",
			"path": CameraPath.from_array([[0.0, Vector3(-5, 1.7, 7), Vector3(-2, 1.2, -6), 60.0], [1.0, Vector3(5, 1.7, 7), Vector3(2, 1.2, -6), 60.0]])},
	}


func set_time(_t: float) -> void:
	pass
