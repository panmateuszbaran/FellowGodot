extends "res://bench/scene_base.gd"
## BENCHMARK D - Fluid Lab: runtime GPU shallow-water simulation (FellowEngine module fellow_fluid).
## Stock Godot has no GPU fluid system: on stock builds this scene reports "unsupported" and exits.
##
## 64 m x 64 m river basin: inflow at -X (1.25 m deep, 1.2 m/s), open outflow at +X, sloped banks, two islands,
## five solid bridge piers, a submerged weir, a moving half-submerged sphere (Jolt AnimatableBody3D, one-way
## coupled: position + velocity uploaded each frame) and a deterministic sequence of impulse drops.
## Options: --fluid_res=256|512|1024 (simulation grid), --water_visible=0 (simulate but do not draw the water,
## used to isolate the water rendering cost), --fluid_sim=0 (draw but do not simulate).
## Extra per-frame metrics: fluid_sim_gpu_ms, fluid_surface_gpu_ms (RenderingDevice timestamps inside the module).

const DOMAIN := 64.0
const LEVEL := 0.0
const PIERS := [Vector2(-6.0, -12.0), Vector2(-6.0, -4.0), Vector2(-6.0, 4.0), Vector2(-6.0, 12.0), Vector2(14.0, 2.0)]
const PIER_R := 1.2

var water  # FellowShallowWater3D (dynamic: the class does not exist on stock builds)
var sphere_body: AnimatableBody3D
var sphere_mesh: MeshInstance3D
var drops_rng := RandomNumberGenerator.new()
var last_sphere_pos := Vector3.ZERO
var next_drop_t := 0.5
var sim_res := 512


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "fluid"
	if not ClassDB.class_exists("FellowShallowWater3D"):
		load_error = "unsupported: FellowShallowWater3D not available (stock Godot has no GPU fluid module)"
		return
	sim_res = int(args.get("fluid_res", "512"))
	_setup_common()
	_setup_environment()
	var terrain := _make_terrain(sim_res)  # generated at the simulation resolution (no resampling of obstacles)
	_build_terrain_mesh(terrain, 256)
	_build_piers()

	water = ClassDB.instantiate("FellowShallowWater3D")
	water.name = "Water"
	water.resolution = sim_res
	water.domain_size = Vector2(DOMAIN, DOMAIN)
	water.substeps = 2
	water.time_step = 1.0 / 120.0
	water.initial_water_level = LEVEL
	water.terrain_image = terrain
	water.inflow_depth = LEVEL + 0.08  # inflow surface elevation (m): slightly above the resting level
	water.inflow_velocity = Vector2(1.2, 0.0)
	water.inflow_cells = maxi(2, sim_res / 128)
	water.render_grid = mini(sim_res, 512)
	water.simulation_enabled = args.get("fluid_sim", "1") != "0"
	add_child(water)
	water.visible = args.get("water_visible", "1") != "0"

	# Moving collider handled by Jolt (kinematic); only its position/velocity reach the GPU.
	sphere_body = AnimatableBody3D.new()
	sphere_body.sync_to_physics = false
	var cs := CollisionShape3D.new()
	var sh := SphereShape3D.new()
	sh.radius = 1.5
	cs.shape = sh
	sphere_body.add_child(cs)
	sphere_mesh = MeshInstance3D.new()
	var sm := SphereMesh.new()
	sm.radius = 1.5
	sm.height = 3.0
	sphere_mesh.mesh = sm
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.8, 0.25, 0.15)
	mat.roughness = 0.4
	sphere_mesh.material_override = mat
	sphere_body.add_child(sphere_mesh)
	add_child(sphere_body)
	drops_rng.seed = 1234
	_define_cameras()
	set_time(0.0)
	info = {
		"simulation_resolution": sim_res,
		"domain_m": DOMAIN,
		"cell_size_m": DOMAIN / sim_res,
		"substeps_per_frame": 2,
		"time_step_s": 1.0 / 120.0,
		"simulation_rate_hz_at_60fps": 120,
		"simulation_vram_bytes": water.get_simulation_vram_bytes(),
		"render_grid": water.render_grid,
		"water_visible": water.visible,
		"simulation_enabled": water.simulation_enabled,
		"physics_engine": ProjectSettings.get_setting("physics/3d/physics_engine"),
	}


func _setup_environment() -> void:
	var sky := Sky.new()
	var ps := PhysicalSkyMaterial.new()
	sky.sky_material = ps
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.glow_enabled = true
	env.ssao_enabled = true
	env.ssr_enabled = false
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-35, -140, 0)
	sun.light_energy = 2.5
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 120.0
	add_child(sun)


## Bed height in metres (FORMAT_RF): channel -1.25 m, banks rising to +1.5 m, islands, weir, solid piers (50 m).
func _make_terrain(n: int) -> Image:
	var img := Image.create_empty(n, n, false, Image.FORMAT_RF)
	for j in range(n):
		var zf := (j + 0.5) / n
		var z := (zf - 0.5) * DOMAIN
		for i in range(n):
			var xf := (i + 0.5) / n
			var x := (xf - 0.5) * DOMAIN
			var bank := smoothstep(0.30, 0.47, absf(zf - 0.5))
			var b := -1.25 + 2.75 * bank
			b += 1.4 * exp(-((x - 4.0) * (x - 4.0) + (z + 8.0) * (z + 8.0)) / 10.0)
			b += 1.1 * exp(-((x - 20.0) * (x - 20.0) + (z - 9.0) * (z - 9.0)) / 6.0)
			if x > 6.0 and x < 7.0 and z > -10.0 and z < 14.0:
				b = maxf(b, -0.45)
			for p in PIERS:
				if Vector2(x, z).distance_to(p) < PIER_R:
					b = 50.0
			img.set_pixel(i, j, Color(b, 0, 0))
	return img


func _build_terrain_mesh(img: Image, n: int) -> void:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var w := img.get_width()
	for j in range(n + 1):
		for i in range(n + 1):
			var u := float(i) / n
			var v := float(j) / n
			var b := img.get_pixel(mini(int(u * w), w - 1), mini(int(v * w), w - 1)).r
			if b > 20.0:
				b = -1.25
			st.set_uv(Vector2(u, v))
			st.add_vertex(Vector3((u - 0.5) * DOMAIN, b, (v - 0.5) * DOMAIN))
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
	mi.mesh = st.commit()
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.46, 0.40, 0.30)
	mat.roughness = 0.9
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mi.material_override = mat
	add_child(mi)


func _build_piers() -> void:
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.55, 0.55, 0.52)
	mat.roughness = 0.8
	for p in PIERS:
		var mi := MeshInstance3D.new()
		var c := CylinderMesh.new()
		c.top_radius = PIER_R
		c.bottom_radius = PIER_R
		c.height = 6.0
		mi.mesh = c
		mi.material_override = mat
		mi.position = Vector3(p.x, 1.5, p.y)
		add_child(mi)


func _sphere_pos(t: float) -> Vector3:
	return Vector3(8.0 * cos(t * 0.35) + 2.0, 0.3, 9.0 * sin(t * 0.35 * 2.0) * 0.8)


func set_time(t: float) -> void:
	if water == null:
		return
	var p := _sphere_pos(t)
	var vel := (_sphere_pos(t + 0.01) - _sphere_pos(t - 0.01)) / 0.02
	sphere_body.global_position = p
	water.set_collider(0, p, 1.5, vel)
	# Deterministic drops: one impulse every 0.5 s of scene time. Large jumps in time (fixed cameras) only
	# replay the drops of the last second, so the schedule stays deterministic without a burst of impulses.
	if t - next_drop_t > 1.0:
		next_drop_t = floor((t - 1.0) / 0.5) * 0.5 + 0.5
	while t >= next_drop_t:
		var dp := Vector3(drops_rng.randf_range(-20.0, 26.0), 0.0, drops_rng.randf_range(-12.0, 12.0))
		water.add_impulse(dp, 0.9, 0.35)
		next_drop_t += 0.5
	if t < next_drop_t - 1.0:
		next_drop_t = t + 0.5  # time moved backwards (camera pre-roll / new pass): restart the schedule


func sample_extra() -> Dictionary:
	if water == null:
		return {}
	return {"fluid_sim_gpu_ms": water.get_simulation_gpu_time_ms(), "fluid_surface_gpu_ms": water.get_surface_gpu_time_ms()}


func _define_cameras() -> void:
	fixed_cameras = {
		"fluid_cam_01": {"pos": Vector3(-30.0, 22.0, 34.0), "target": Vector3(4.0, -1.0, 0.0), "fov": 55.0, "time": 12.0, "purpose": "overview: flow, piers, islands, moving collider"},
		"fluid_cam_02": {"pos": Vector3(-1.0, 3.5, 9.0), "target": Vector3(-6.0, 0.0, 2.0), "fov": 55.0, "time": 12.0, "purpose": "pier wakes and foam, refraction over the weir"},
		"fluid_cam_03": {"pos": Vector3(24.0, 1.2, -14.0), "target": Vector3(0.0, 0.0, 6.0), "fov": 60.0, "time": 12.0, "purpose": "grazing view: reflections, normals, distant waves"},
	}
	var keys := []
	for i in range(13):
		var a := TAU * i / 12.0
		keys.append([i * 2.5, Vector3(cos(a) * 34.0, 16.0 + 4.0 * sin(a * 2.0), sin(a) * 34.0), Vector3(2.0, -1.0, 0.0), 55.0])
	flythrough = CameraPath.from_array(keys)
	temporal_seqs = {
		"fluid_close": {"start": 12.0, "frames": 60, "purpose": "close-up of pier wakes under slow camera motion",
			"path": CameraPath.from_array([[0.0, Vector3(-1.0, 3.5, 9.0), Vector3(-6.0, 0.0, 2.0), 55.0], [1.0, Vector3(1.0, 3.5, 8.0), Vector3(-6.0, 0.0, 2.0), 55.0]])},
	}
