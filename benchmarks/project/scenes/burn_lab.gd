extends "res://bench/scene_base.gd"
## BURN LAB (built in code; deterministic at a fixed frame rate): FellowEngine's burning objects (decision 0120) in a
## gas volume (0117, 0118). Stock Godot has neither: on stock builds this scene reports "unsupported".
##
##   * a campfire's kindling (a gas emitter) burning down over the first seconds under two crossed logs and a thin
##     branch: the logs catch, their own volatiles feed the flames, they char, glow in their cracks, recede and end as
##     ash; the branch burns through where the fire is;
##   * a wooden crate (five planks, one mesh) beside the fire, lit at one corner after 3 s (FellowBurnable3D.ignite).
## Options: --gas_res=N (the volume's cells along its longest side, default 64), --burn_res=N (the voxels along each
## object's longest side, default 32), --burn_speed=S (burning time per second, default 10), --kindling_s=S (how long
## the kindling burns, default 12), --night=1, --burn_draw=0 (simulated, drawn with the original meshes: the drawing's
## cost), --flame_limit=K (the gas's, default 1500).
## Captures: burn_a ... burn_f are the same view one after another (with --settle=300: every 5 s), burn_top the ash.
## Extra per-frame metrics: the logs' and the crate's wood, char and ash fractions, their burning voxels.

var sun: DirectionalLight3D
var gas
var kindling
var log_a
var log_b
var branch
var crate
var ticks := 0
var night := false
var burn_speed := 10.0
var burn_res := 32
var kindling_s := 12.0


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "burn"
	if not ClassDB.class_exists("FellowBurnable3D") or not ClassDB.class_exists("FellowGasVolume3D"):
		load_error = "unsupported: FellowBurnable3D not available (stock Godot has no burning objects)"
		return
	night = args.get("night", "0") == "1"
	burn_speed = float(args.get("burn_speed", "10"))
	burn_res = int(args.get("burn_res", "32"))
	kindling_s = float(args.get("kindling_s", "12"))
	_setup_common()
	_setup_environment()
	_build_ground()
	_build_fire()
	_build_wood()
	_define_cameras()
	info = {"stats": scene_stats(self), "night": night, "burn_speed": burn_speed, "burn_res": burn_res,
		"gas_grid": [gas.get_grid_size().x, gas.get_grid_size().y, gas.get_grid_size().z], "gas_cell_m": gas.get_cell_size(),
		"notes": "Burning objects coupled with a gas volume: two logs and a branch over a kindling fire, a crate lit at a corner."}


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
		sm.sky_top_color = Color(0.16, 0.22, 0.38)
		sm.sky_horizon_color = Color(0.62, 0.45, 0.32)
		sm.ground_horizon_color = Color(0.25, 0.2, 0.16)
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
	sun.light_energy = 0.04 if night else 0.9
	sun.light_color = Color(0.6, 0.7, 1.0) if night else Color(1.0, 0.72, 0.5)
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 30.0
	apply_sun_overrides(sun)
	add_child(sun)
	sun.rotation_degrees = Vector3(-14.0, 120.0, 0.0)


func _material(p_color: Color, p_roughness: float = 0.85) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = p_color
	m.roughness = p_roughness
	return m


func _build_ground() -> void:
	var mi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(200, 200)
	mi.mesh = pm
	mi.material_override = _material(Color(0.3, 0.27, 0.23), 0.95)
	add_child(mi)
	var stone := _material(Color(0.42, 0.41, 0.4), 0.8)
	for i in 12:
		var a := TAU * i / 12.0
		var s := MeshInstance3D.new()
		var sm := SphereMesh.new()
		sm.radius = 0.12
		sm.height = 0.18
		s.mesh = sm
		s.material_override = stone
		s.position = Vector3(cos(a) * 0.62, 0.05, sin(a) * 0.62)
		add_child(s)


func _build_fire() -> void:
	gas = ClassDB.instantiate("FellowGasVolume3D")
	gas.name = "Fire"
	gas.size = Vector3(3.2, 3.6, 3.2)
	gas.resolution = int(args.get("gas_res", "64"))
	gas.turbulence = 5.0
	gas.turbulence_scale = 0.25
	gas.vorticity = 0.35
	gas.smoke_color = Color(0.5, 0.49, 0.48)
	gas.smoke_density = 5.0
	gas.fire_intensity = 18.0
	gas.physics_obstacles = false
	gas.flame_temperature_limit = float(args.get("flame_limit", "1500"))
	if args.has("expansion"):
		gas.expansion = float(args["expansion"])
	add_child(gas)
	kindling = ClassDB.instantiate("FellowGasEmitter3D")
	kindling.shape = 2  # cylinder
	kindling.radius = 0.22
	kindling.height = 0.12
	kindling.position = Vector3(0, 0.08, 0)
	kindling.fuel = 2.5
	kindling.temperature = 900.0
	kindling.smoke = 0.08
	kindling.noise = 0.75
	kindling.noise_scale = 0.15
	gas.add_child(kindling)


func _burnable(p_mesh: MeshInstance3D, p_res: int) -> Node:
	var b = ClassDB.instantiate("FellowBurnable3D")
	b.resolution = p_res
	b.burn_speed = burn_speed
	b.draw_burning = args.get("burn_draw", "1") != "0"
	b.simulation_enabled = args.get("burn_sim", "1") != "0"
	b.flame_strength = float(args.get("flame_strength", "1"))
	if args.has("surface_heating"):
		b.surface_heating = float(args["surface_heating"])
	if args.has("attach"):
		b.flame_attachment = float(args["attach"])
	if args.has("feedback"):
		b.flame_feedback = float(args["feedback"])
	p_mesh.add_child(b)
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
	add_child(mi)
	return mi


func _build_wood() -> void:
	var bark := Color(0.26, 0.17, 0.1)
	# Two logs crossed over the kindling, a thin branch across its edge.
	var la := _log(0.085, 0.95, Transform3D(Basis(Vector3.UP, 0.25) * Basis(Vector3.FORWARD, PI * 0.5), Vector3(0.0, 0.17, 0.02)), bark)
	log_a = _burnable(la, burn_res)
	var lb := _log(0.075, 0.85, Transform3D(Basis(Vector3.UP, 0.25 + PI * 0.5) * Basis(Vector3.FORWARD, PI * 0.5), Vector3(0.02, 0.33, 0.0)), bark.lightened(0.08))
	log_b = _burnable(lb, burn_res)
	var br := _log(0.028, 0.8, Transform3D(Basis(Vector3.UP, -0.6) * Basis(Vector3.FORWARD, PI * 0.5), Vector3(-0.12, 0.05, 0.22)), bark.darkened(0.1))
	branch = _burnable(br, burn_res)
	branch.density = 420.0
	# A crate of five planks (one mesh: the planks do not overlap) beside the fire.
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var w := 0.5
	var hgt := 0.36
	var t := 0.022
	_add_box(st, Vector3(0.0, t * 0.5, 0.0), Vector3(w - 2.0 * t, t, w - 2.0 * t))  # the bottom, inside the walls
	_add_box(st, Vector3(-(w - t) * 0.5, hgt * 0.5, 0.0), Vector3(t, hgt, w))
	_add_box(st, Vector3((w - t) * 0.5, hgt * 0.5, 0.0), Vector3(t, hgt, w))
	_add_box(st, Vector3(0.0, hgt * 0.5, -(w - t) * 0.5), Vector3(w - 2.0 * t, hgt, t))
	_add_box(st, Vector3(0.0, hgt * 0.5, (w - t) * 0.5), Vector3(w - 2.0 * t, hgt, t))
	var cmi := MeshInstance3D.new()
	cmi.mesh = st.commit()
	cmi.material_override = _material(Color(0.66, 0.48, 0.3), 0.75)
	cmi.position = Vector3(0.95, 0.0, -0.25)
	cmi.rotation = Vector3(0.0, 0.35, 0.0)
	add_child(cmi)
	crate = _burnable(cmi, 40)
	crate.density = 450.0


# An axis-aligned box (BoxMesh's triangles, moved to c).
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


func _physics_process(_delta: float) -> void:
	if gas == null:
		return
	ticks += 1
	var t := ticks / 60.0
	# The kindling burns down: full for 60 % of its time, then out.
	var k := clampf(1.0 - (t - kindling_s * 0.6) / (kindling_s * 0.4), 0.0, 1.0)
	kindling.fuel = 2.5 * k
	kindling.temperature = 900.0 * k
	kindling.smoke = 0.08 * k
	if ticks == 180 and crate:
		# A corner of the crate, the side towards the fire, lit as by a torch.
		crate.ignite(crate.get_parent().to_global(Vector3(-0.24, 0.3, 0.24)), 0.1, 1000.0)


func sample_extra() -> Dictionary:
	if gas == null:
		return {}
	var d := {"gas_steps": gas.get_steps_simulated(), "fire_power": gas.get_fire_power()}
	for pair in [["log_a", log_a], ["log_b", log_b], ["branch", branch], ["crate", crate]]:
		var b = pair[1]
		d[pair[0]] = [snappedf(b.get_wood_fraction(), 0.0001), snappedf(b.get_char_fraction(), 0.0001), snappedf(b.get_ash_fraction(), 0.0001), b.get_burning_voxels(), snappedf(b.get_max_temperature(), 1.0)]
	return d


func _define_cameras() -> void:
	var close := {"pos": Vector3(1.3, 0.85, 1.75), "target": Vector3(0.2, 0.22, -0.05), "fov": 50.0, "time": 0.0}
	for n in ["a", "b", "c", "d", "e", "f"]:
		var c := close.duplicate()
		c["purpose"] = "the fire one capture after another (with --settle=300: every 5 s): the logs catch, char, glow, recede, end as ash"
		fixed_cameras["burn_" + n] = c
	var macro := {"pos": Vector3(0.75, 0.5, 0.75), "target": Vector3(0.0, 0.2, 0.0), "fov": 45.0, "time": 0.0}
	for n in ["a", "b", "c", "d"]:
		var c := macro.duplicate()
		c["purpose"] = "the logs close, one capture after another: char, cracks glowing, receding, ash"
		fixed_cameras["burn_macro_" + n] = c
	fixed_cameras["burn_top"] = {"pos": Vector3(0.5, 1.9, 1.1), "target": Vector3(0.3, 0.0, -0.05), "fov": 50.0, "time": 0.0, "purpose": "the remains from above: ash heaps, charred ends"}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(1.3, 0.85, 1.75), Vector3(0.2, 0.22, -0.05), 50.0],
		[10.0, Vector3(-1.6, 1.0, 1.2), Vector3(0.1, 0.3, 0.0), 50.0],
		[20.0, Vector3(-0.8, 1.2, -1.8), Vector3(0.3, 0.3, -0.1), 50.0],
		[30.0, Vector3(1.3, 0.85, 1.75), Vector3(0.2, 0.22, -0.05), 50.0],
	])
	var orbit := []
	for i in 5:
		var a := 0.6 + i * 0.25
		orbit.append([i * 0.25, Vector3(cos(a) * 2.2, 0.9, sin(a) * 2.2), Vector3(0.2, 0.25, 0.0), 50.0])
	temporal_seqs = {
		"burn_orbit": {"start": 0.0, "frames": 60, "purpose": "orbit around the fire (the flames move, the camera too)", "path": CameraPath.from_array(orbit)},
	}
