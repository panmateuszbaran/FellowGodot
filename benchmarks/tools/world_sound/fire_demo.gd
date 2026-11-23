extends SceneTree
## FellowEngine's fire sound at a campfire (milestone 179, decision 0321): Burn Lab's campfire at dusk — kindling (a gas
## emitter) under two crossed logs and a branch, a crate beside it lit at a corner at 3 s, a splash of fuel thrown on
## at 11 s (a flare); the camera circling the fire. The flames' roar from the gas volume (its simulated fire), the
## wood's crackle and the embers from each burning object, all synthesised as the scene runs. Record it with Movie
## Maker (frames and sound):
##
##   VK_ICD_FILENAMES=.../lvp_icd.json xvfb-run -a godot --path benchmarks/tools/world_sound --rendering-driver vulkan \
##       --resolution 960x540 --fixed-fps 30 --write-movie <dir>/m.png -s fire_demo.gd [-- --seconds=20]
##
## The sound statistics printed at the end (and every second with --probe: the fire's power, the objects'
## pyrolysis and cracks); --timing: the viewport's GPU and CPU render times from 2 s ("TIMING ..."); --silent: every
## sound off (for the timing without it); --gas_res=N (default 64), --burn_speed=S (default 10).

var _seconds := 20.0
var _timing := false
var _silent := false
var _probe := false
var _gpu_ms := []
var _cpu_ms := []
var _frame := 0
var _cam: Camera3D
var _gas: Node3D
var _kindling: Node3D
var _splash: Node3D
var _burnables := {}
var _crate: Node3D
var _gas_res := 64
var _burn_speed := 10.0


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--seconds="):
			_seconds = float(a.get_slice("=", 1))
		elif a.begins_with("--gas_res="):
			_gas_res = int(a.get_slice("=", 1))
		elif a.begins_with("--burn_speed="):
			_burn_speed = float(a.get_slice("=", 1))
		elif a == "--timing":
			_timing = true
		elif a == "--silent":
			_silent = true
		elif a == "--probe":
			_probe = true
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var sm := ProceduralSkyMaterial.new()
	sm.sky_top_color = Color(0.03, 0.04, 0.08)
	sm.sky_horizon_color = Color(0.18, 0.12, 0.1)
	sm.ground_horizon_color = Color(0.05, 0.04, 0.04)
	sm.ground_bottom_color = Color(0.02, 0.02, 0.02)
	sky.sky_material = sm
	e.sky = sky
	e.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	e.ambient_light_energy = 0.5
	e.tonemap_mode = Environment.TONE_MAPPER_AGX
	e.glow_enabled = true
	env.environment = e
	root.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.light_energy = 0.06
	sun.light_color = Color(0.6, 0.7, 1.0)
	sun.rotation_degrees = Vector3(-20.0, 120.0, 0.0)
	root.add_child(sun)
	_build_ground()
	_build_fire()
	_build_wood()
	if _silent:
		_gas.set("sound_enabled", false)
		for b in _burnables.values():
			b.set("sound_enabled", false)
	_cam = Camera3D.new()
	_cam.fov = 55.0
	root.add_child(_cam)
	_place_camera(0.0)


func _material(p_color: Color, p_roughness: float = 0.85) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = p_color
	m.roughness = p_roughness
	return m


func _build_ground() -> void:
	var mi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(60, 60)
	mi.mesh = pm
	mi.material_override = _material(Color(0.3, 0.27, 0.23), 0.95)
	root.add_child(mi)
	var ground := StaticBody3D.new()
	var gs := CollisionShape3D.new()
	gs.shape = WorldBoundaryShape3D.new()
	ground.add_child(gs)
	root.add_child(ground)
	var stone := _material(Color(0.42, 0.41, 0.4), 0.8)
	for i in 12:
		var a := TAU * i / 12.0
		var s := MeshInstance3D.new()
		var sp := SphereMesh.new()
		sp.radius = 0.12
		sp.height = 0.18
		s.mesh = sp
		s.material_override = stone
		s.position = Vector3(cos(a) * 0.62, 0.05, sin(a) * 0.62)
		root.add_child(s)


func _build_fire() -> void:
	_gas = ClassDB.instantiate("FellowGasVolume3D")
	_gas.set("size", Vector3(3.2, 3.6, 3.2))
	_gas.set("resolution", _gas_res)
	_gas.set("turbulence", 5.0)
	_gas.set("turbulence_scale", 0.25)
	_gas.set("vorticity", 0.35)
	_gas.set("smoke_color", Color(0.5, 0.49, 0.48))
	_gas.set("smoke_density", 5.0)
	_gas.set("fire_intensity", 18.0)
	_gas.set("physics_obstacles", false)
	_gas.set("closed_faces", 55)
	_gas.set("flame_temperature_limit", 1500.0)
	root.add_child(_gas)
	_kindling = ClassDB.instantiate("FellowGasEmitter3D")
	_kindling.set("shape", 2)
	_kindling.set("radius", 0.22)
	_kindling.set("height", 0.12)
	_kindling.position = Vector3(0, 0.08, 0)
	_kindling.set("fuel", 2.5)
	_kindling.set("temperature", 900.0)
	_kindling.set("smoke", 0.08)
	_kindling.set("noise", 0.75)
	_kindling.set("noise_scale", 0.15)
	_gas.add_child(_kindling)
	# Fuel thrown on (at 11 s, for a third of a second).
	_splash = ClassDB.instantiate("FellowGasEmitter3D")
	_splash.set("radius", 0.3)
	_splash.position = Vector3(0.05, 0.3, 0.0)
	_splash.set("fuel", 0.0)
	_splash.set("temperature", 0.0)
	_splash.set("smoke", 0.0)
	_gas.add_child(_splash)


func _burnable(p_mesh: MeshInstance3D, p_res: int, p_name: String) -> Node3D:
	var b: Node3D = ClassDB.instantiate("FellowBurnable3D")
	b.set("resolution", p_res)
	b.set("burn_speed", _burn_speed)
	p_mesh.add_child(b)
	_burnables[p_name] = b
	return b


func _log(p_radius: float, p_length: float, p_xform: Transform3D, p_color: Color) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var cm := CylinderMesh.new()
	cm.top_radius = p_radius
	cm.bottom_radius = p_radius * 1.06
	cm.height = p_length
	cm.radial_segments = 20
	cm.rings = 2
	mi.mesh = cm
	mi.material_override = _material(p_color, 0.9)
	mi.transform = p_xform
	root.add_child(mi)
	return mi


func _build_wood() -> void:
	var bark := Color(0.26, 0.17, 0.1)
	_burnable(_log(0.085, 0.95, Transform3D(Basis(Vector3.UP, 0.25) * Basis(Vector3.FORWARD, PI * 0.5), Vector3(0.0, 0.17, 0.02)), bark), 32, "log_a")
	_burnable(_log(0.075, 0.85, Transform3D(Basis(Vector3.UP, 0.25 + PI * 0.5) * Basis(Vector3.FORWARD, PI * 0.5), Vector3(0.02, 0.33, 0.0)), bark.lightened(0.08)), 32, "log_b")
	var br := _burnable(_log(0.028, 0.8, Transform3D(Basis(Vector3.UP, -0.6) * Basis(Vector3.FORWARD, PI * 0.5), Vector3(-0.12, 0.05, 0.22)), bark.darkened(0.1)), 32, "branch")
	br.set("density", 420.0)
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var w := 0.5
	var hgt := 0.36
	var t := 0.022
	_add_box(st, Vector3(0.0, t * 0.5, 0.0), Vector3(w - 2.0 * t, t, w - 2.0 * t))
	_add_box(st, Vector3(-(w - t) * 0.5, hgt * 0.5, 0.0), Vector3(t, hgt, w))
	_add_box(st, Vector3((w - t) * 0.5, hgt * 0.5, 0.0), Vector3(t, hgt, w))
	_add_box(st, Vector3(0.0, hgt * 0.5, -(w - t) * 0.5), Vector3(w - 2.0 * t, hgt, t))
	_add_box(st, Vector3(0.0, hgt * 0.5, (w - t) * 0.5), Vector3(w - 2.0 * t, hgt, t))
	var cmi := MeshInstance3D.new()
	cmi.mesh = st.commit()
	cmi.material_override = _material(Color(0.66, 0.48, 0.3), 0.75)
	cmi.position = Vector3(0.95, 0.0, -0.25)
	cmi.rotation = Vector3(0.0, 0.35, 0.0)
	root.add_child(cmi)
	_crate = _burnable(cmi, 40, "crate")
	_crate.set("density", 450.0)


func _add_box(st: SurfaceTool, c: Vector3, s: Vector3) -> void:
	var bm := BoxMesh.new()
	bm.size = s
	var a: Array = bm.get_mesh_arrays()
	var verts: PackedVector3Array = a[Mesh.ARRAY_VERTEX]
	var norms: PackedVector3Array = a[Mesh.ARRAY_NORMAL]
	var uvs: PackedVector2Array = a[Mesh.ARRAY_TEX_UV]
	var idx: PackedInt32Array = a[Mesh.ARRAY_INDEX]
	for i in idx:
		st.set_normal(norms[i])
		st.set_uv(uvs[i])
		st.add_vertex(verts[i] + c)


# Circling the fire at 2.2 m, a quarter turn in the demo, a little lower as it goes.
func _place_camera(p_t: float) -> void:
	var a := -0.4 + 0.08 * p_t
	var at := Vector3(sin(a) * 2.2, 1.0 - 0.01 * p_t, cos(a) * 2.2)
	_cam.look_at_from_position(at, Vector3(0.25, 0.3, -0.05))


# The volatiles an object releases, kg a second of the game's time.
func _pyrolysis(p_b: Node3D) -> float:
	var mi: MeshInstance3D = p_b.get_parent()
	var h: float = p_b.call("get_voxel_size") * pow(absf(mi.global_transform.basis.determinant()), 1.0 / 3.0)
	return p_b.call("get_fuel_release") * h * h * h * _burn_speed


func _process(_d: float) -> bool:
	var t := float(_frame) / 30.0
	_place_camera(t)
	# The kindling full for 8 s, out by 14 s; the crate lit at 3 s; the fuel thrown on at 11 s.
	var k := clampf(1.0 - (t - 8.0) / 6.0, 0.0, 1.0)
	_kindling.set("fuel", 2.5 * k)
	_kindling.set("temperature", 900.0 * k)
	_kindling.set("smoke", 0.08 * k)
	if _frame == 90:
		_crate.call("ignite", _crate.get_parent().to_global(Vector3(-0.24, 0.04, 0.24)), 0.1, 1000.0)
	var splash := t >= 11.0 and t < 11.35
	_splash.set("fuel", 14.0 if splash else 0.0)
	_splash.set("temperature", 1100.0 if splash else 0.0)
	_frame += 1
	if _probe and _frame % 30 == 0:
		var line := "PROBE t %.1f fire_power %.3f sound %s" % [t, _gas.call("get_fire_power"), _gas.call("get_sound_stats")]
		for n in _burnables:
			var b: Node3D = _burnables[n]
			line += " | %s pyrolysis %.3f g/s burning %d wood %.3f char %.3f T %.0f %s" % [n, 1000.0 * _pyrolysis(b), b.call("get_burning_voxels"),
					b.call("get_wood_fraction"), b.call("get_char_fraction"), b.call("get_max_temperature"), b.call("get_sound_stats")]
		print(line)
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
		print("flames: ", _gas.call("get_sound_stats"))
		for n in _burnables:
			print(n, ": ", _burnables[n].call("get_sound_stats"))
		return true
	return false
