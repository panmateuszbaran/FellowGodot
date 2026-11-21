extends "res://bench/scene_base.gd"
## GI LAB (built in code, static; milestone 167): the cases diffuse global illumination is judged by.
##
## * A room (8 x 4 x 6 m inside, walls 30 cm) lit only through a window and a door on its sunny side: the sun's patch on
##   the floor lights the white ceiling and back wall, a red wall and a green wall tint what faces them, the far
##   corners are dark.
## * A sealed box (4 x 3 x 4 m, walls and roof 10 cm, no opening) beside it: inside, any light is a leak.
## * Outside, a yellow block and a blue column by a white wall: colour bleeding in the open, and the sky's occlusion
##   under an overhang.
## Physical sky and a shadowed sun from the window's side; AgX, a fixed exposure; no SSAO, SSIL, SSR or SDFGI unless
## --set turns them on (env.sdfgi_enabled=true ...).

var sun: DirectionalLight3D
var _mats := {}


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "gi"
	_setup_common()
	var sky := Sky.new()
	var sky_mat := PhysicalSkyMaterial.new()
	sky_mat.turbidity = 8.0
	sky_mat.mie_coefficient = 0.004
	sky.sky_material = sky_mat
	sky.radiance_size = Sky.RADIANCE_SIZE_256
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.tonemap_exposure = float(args.get("gi_exposure", "1.0"))
	env.glow_enabled = false
	env.ssao_enabled = false
	env.ssil_enabled = false
	env.ssr_enabled = false
	env.sdfgi_enabled = false
	sun = DirectionalLight3D.new()
	sun.name = "BenchSun"
	# From +z (the window's side), 35 degrees up, a little from +x.
	sun.rotation_degrees = Vector3(-35.0, 20.0, 0.0)
	sun.light_energy = 2.5
	sun.light_color = Color(1.0, 0.95, 0.88)
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 60.0
	sun.light_angular_distance = 0.5
	apply_sun_overrides(sun)
	add_child(sun)

	_mat("ground", Color(0.45, 0.45, 0.43))
	_mat("white", Color(0.8, 0.8, 0.78))
	_mat("red", Color(0.7, 0.08, 0.06))
	_mat("green", Color(0.08, 0.55, 0.12))
	_mat("yellow", Color(0.85, 0.7, 0.1))
	_mat("blue", Color(0.08, 0.15, 0.7))

	_box("ground", Vector3(-40, -0.2, -40), Vector3(40, 0.0, 40))
	# The room: inside x -4..4, y 0..4, z -3..3; walls 0.3 thick.
	_box("white", Vector3(-4.3, 4.0, -3.3), Vector3(4.3, 4.3, 3.3)) # ceiling
	_box("white", Vector3(-4.3, 0.0, -3.3), Vector3(4.3, 4.0, -3.0)) # back wall
	_box("red", Vector3(-4.3, 0.0, -3.0), Vector3(-4.0, 4.0, 3.0)) # left wall
	_box("green", Vector3(4.0, 0.0, -3.0), Vector3(4.3, 4.0, 3.0)) # right wall
	# The front wall (z 3..3.3) with a window (x -2.5..0.5, y 1..3) and a door (x 2..3, y 0..2.2).
	_box("white", Vector3(-4.3, 0.0, 3.0), Vector3(-2.5, 4.0, 3.3))
	_box("white", Vector3(-2.5, 0.0, 3.0), Vector3(0.5, 1.0, 3.3))
	_box("white", Vector3(-2.5, 3.0, 3.0), Vector3(0.5, 4.0, 3.3))
	_box("white", Vector3(0.5, 0.0, 3.0), Vector3(2.0, 4.0, 3.3))
	_box("white", Vector3(2.0, 2.2, 3.0), Vector3(3.0, 4.0, 3.3))
	_box("white", Vector3(3.0, 0.0, 3.0), Vector3(4.3, 4.0, 3.3))
	# A table in the room: a dark area under it.
	_box("white", Vector3(-1.2, 0.75, -1.8), Vector3(1.2, 0.8, -0.6))
	for x in [-1.1, 1.05]:
		for z in [-1.75, -0.7]:
			_box("white", Vector3(x, 0.0, z), Vector3(x + 0.05, 0.75, z + 0.05))

	# The sealed box: inside x 8.1..11.9, y 0..2.9, z -1.9..1.9; walls and roof 0.1 thick.
	_box("white", Vector3(8.0, 2.9, -2.0), Vector3(12.0, 3.0, 2.0))
	_box("white", Vector3(8.0, 0.0, -2.0), Vector3(12.0, 2.9, -1.9))
	_box("white", Vector3(8.0, 0.0, 1.9), Vector3(12.0, 2.9, 2.0))
	_box("white", Vector3(8.0, 0.0, -1.9), Vector3(8.1, 2.9, 1.9))
	_box("white", Vector3(11.9, 0.0, -1.9), Vector3(12.0, 2.9, 1.9))

	# Outside: a white wall with an overhang, a yellow block and a blue column in front of it.
	_box("white", Vector3(-12.0, 0.0, -2.0), Vector3(-7.0, 3.5, -1.7))
	_box("white", Vector3(-12.0, 3.5, -2.0), Vector3(-7.0, 3.7, 0.5)) # the overhang
	_box("yellow", Vector3(-11.0, 0.0, 1.0), Vector3(-9.8, 1.2, 2.2))
	_box("blue", Vector3(-8.4, 0.0, 0.2), Vector3(-7.9, 3.0, 0.7))

	fixed_cameras = {
		"gi_room": {"pos": Vector3(3.3, 1.7, 2.5), "target": Vector3(-2.5, 1.0, -1.6), "fov": 70.0, "time": 0.0,
			"purpose": "the room from the door's corner: the sun's patch, the red wall, the ceiling and back wall lit by the floor"},
		"gi_room_dark": {"pos": Vector3(-3.3, 1.5, 2.6), "target": Vector3(3.5, 1.0, -2.6), "fov": 70.0, "time": 0.0,
			"purpose": "the room towards its dark corner: the green wall, under the table"},
		"gi_sealed": {"pos": Vector3(10.0, 1.4, -1.5), "target": Vector3(10.0, 1.2, 1.9), "fov": 75.0, "time": 0.0,
			"purpose": "inside the sealed box, facing its sunlit wall: any light is a leak"},
		"gi_outside": {"pos": Vector3(-3.0, 2.2, 7.5), "target": Vector3(-9.0, 1.2, 0.0), "fov": 60.0, "time": 0.0,
			"purpose": "outside: yellow and blue bleeding onto the white wall and the ground, the overhang's shade"},
		"gi_overview": {"pos": Vector3(6.0, 7.0, 14.0), "target": Vector3(0.0, 1.5, 0.0), "fov": 60.0, "time": 0.0,
			"purpose": "the lab from above the window's side"},
	}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(3.3, 1.7, 2.5), Vector3(-2.5, 1.0, -1.6), 70.0],
		[6.0, Vector3(0.0, 1.7, 1.0), Vector3(-3.0, 1.5, -2.5), 70.0],
		[12.0, Vector3(-3.3, 1.5, 2.6), Vector3(3.5, 1.0, -2.6), 70.0],
		[18.0, Vector3(2.5, 1.6, 6.0), Vector3(0.0, 1.5, 0.0), 70.0],
		[24.0, Vector3(-3.0, 2.2, 7.5), Vector3(-9.0, 1.2, 0.0), 60.0],
		[30.0, Vector3(3.3, 1.7, 2.5), Vector3(-2.5, 1.0, -1.6), 70.0],
	])
	temporal_seqs = {
		"gi_room_pan": {"start": 0.0, "frames": 90, "purpose": "a slow pan across the room: indirect light on surfaces coming into view",
			"path": CameraPath.from_array([[0.0, Vector3(3.3, 1.7, 2.5), Vector3(-2.5, 1.0, -1.6), 70.0], [1.5, Vector3(3.0, 1.7, 2.2), Vector3(-3.2, 1.3, 1.8), 70.0]])},
	}
	info = {"sun": {"elevation_deg": 35.0, "azimuth_deg": 20.0, "energy": sun.light_energy}}


func _mat(p_name: String, p_color: Color) -> void:
	var m := StandardMaterial3D.new()
	m.albedo_color = p_color
	m.roughness = 0.9
	_mats[p_name] = m


func _box(p_mat: String, p_from: Vector3, p_to: Vector3) -> void:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = p_to - p_from
	mi.mesh = bm
	mi.material_override = _mats[p_mat]
	mi.position = (p_from + p_to) * 0.5
	add_child(mi)
