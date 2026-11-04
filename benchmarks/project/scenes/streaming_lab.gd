extends "res://bench/scene_base.gd"
## STREAMING LAB (built in code, deterministic; milestone 45): a long street of buildings whose 48 materials each have a
## 2048 x 2048 albedo (BC1) and normal map (BC5) with full mip chains, as Godot imports VRAM-compressed 3D textures:
## 48 x 8 MB = 384 MB of textures, most of them far from any one camera. Texture streaming
## (rendering/fellow/streaming/textures) keeps only the mips the cameras can see; without it every level is resident.
##
## The textures are generated, never committed: benchmarks/scripts/make_streaming_textures.py writes them into
## res://external/streaming/ (--streaming_dir=<res path> to use another folder).

const BLOCKS := 32 # buildings per side
const SPACING := 18.0 # metres between buildings along the street
const STREET_HALF := 9.0 # from the street's axis to the facades

var sun: DirectionalLight3D
var _albedo: Array[Texture2D] = []
var _normal: Array[Texture2D] = []
var _mats: Array[StandardMaterial3D] = []
var _boxes := {}
var _count := 0
var _missing := 0


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "streaming"
	_setup_common()
	_setup_environment()
	var t0 := Time.get_ticks_usec()
	_load_textures()
	var load_ms := (Time.get_ticks_usec() - t0) / 1000.0
	if _missing > 0:
		load_error = "Streaming Lab: %d textures missing in %s (run benchmarks/scripts/make_streaming_textures.py)" % [_missing, _dir()]
		return
	_build_street()
	_build_lights()
	_define_cameras()
	set_time(0.0)
	info = {"stats": scene_stats(self), "nodes": _count, "textures": _albedo.size() + _normal.size(), "texture_load_ms": load_ms,
		"notes": "A street of %d buildings with %d materials, each a 2048^2 BC1 albedo and BC5 normal map (full mip chains)." % [BLOCKS * 2, _mats.size()]}


func _dir() -> String:
	return str(args.get("streaming_dir", "res://external/streaming"))


func _setup_environment() -> void:
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	cam_attr.auto_exposure_enabled = false


func _load_textures() -> void:
	var n := int(args.get("streaming_materials", "48"))
	for i in n:
		var a: Texture2D = load(_dir().path_join("m%02d_albedo.ctex" % i))
		var nm: Texture2D = load(_dir().path_join("m%02d_normal.ctex" % i))
		if a == null or nm == null:
			_missing += 1
			continue
		_albedo.append(a)
		_normal.append(nm)
		var m := StandardMaterial3D.new()
		m.albedo_texture = a
		m.normal_enabled = true
		m.normal_texture = nm
		m.roughness = 0.75
		# BoxMesh puts each face in a third of U and half of V; four repeats of the texture across a face.
		m.uv1_scale = Vector3(4.0, 4.0, 1.0)
		_mats.append(m)


func _box(size: Vector3) -> BoxMesh:
	var key := "%.1f_%.1f_%.1f" % [size.x, size.y, size.z]
	if not _boxes.has(key):
		var b := BoxMesh.new()
		b.size = size
		_boxes[key] = b
	return _boxes[key]


func _add(mesh: Mesh, mat: Material, pos: Vector3) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = mat
	mi.position = pos
	add_child(mi)
	_count += 1
	return mi


func _build_street() -> void:
	var length := BLOCKS * SPACING
	# The street: one material tiled every 4 m.
	var ground := PlaneMesh.new()
	ground.size = Vector2(STREET_HALF * 2.0 + 40.0, length + 60.0)
	var gm: StandardMaterial3D = _mats[0].duplicate()
	gm.uv1_scale = Vector3(ground.size.x / 4.0, ground.size.y / 4.0, 1.0)
	_add(ground, gm, Vector3(0, 0, -length * 0.5))
	# Buildings on both sides, each with the next material (so a material is used at two distances along the street).
	var rng := RandomNumberGenerator.new()
	rng.seed = 4545
	var k := 1
	for side in [-1, 1]:
		for i in BLOCKS:
			var h := float(rng.randi_range(3, 7)) * 4.0
			var w := float(rng.randi_range(3, 4)) * 4.0
			var d := 12.0
			var mat := _mats[k % _mats.size()]
			k += 1
			_add(_box(Vector3(d, h, w)), mat, Vector3(side * (STREET_HALF + d * 0.5), h * 0.5, -i * SPACING - SPACING * 0.5))
	# Crates along the pavements: small objects with dense texels close to the camera.
	var crate := _box(Vector3(1.0, 1.0, 1.0))
	for i in BLOCKS:
		var mat := _mats[(i * 7 + 3) % _mats.size()]
		_add(crate, mat, Vector3((-1 if i % 2 == 0 else 1) * (STREET_HALF - 1.5), 0.5, -i * SPACING - 4.0))
	# --streaming_props=<n>: n more small boxes scattered over the roofs (the evaluation's cost per instance).
	var props := int(args.get("streaming_props", "0"))
	var small := _box(Vector3(0.5, 0.5, 0.5))
	for i in props:
		var side := -1 if i % 2 == 0 else 1
		var x := side * (STREET_HALF + 1.0 + rng.randf() * 10.0)
		var z := -rng.randf() * length
		_add(small, _mats[i % _mats.size()], Vector3(x, 30.0 + rng.randf() * 4.0, z))


func _build_lights() -> void:
	sun = DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 35, 0)
	sun.light_energy = 1.2
	sun.shadow_enabled = true
	add_child(sun)
	apply_sun_overrides(sun)


func _define_cameras() -> void:
	var length := BLOCKS * SPACING
	fixed_cameras = {
		"street_start": {"pos": Vector3(0, 1.7, 5), "target": Vector3(0, 3, -120), "fov": 70.0, "purpose": "street level at the start, looking down the street (near facades at full resolution, far ones coarse)"},
		"street_mid": {"pos": Vector3(-3, 1.7, -length * 0.5), "target": Vector3(-STREET_HALF, 3.0, -length * 0.5 - 14.0), "fov": 70.0, "purpose": "mid-street, a facade 7 m away"},
		"facade_close": {"pos": Vector3(STREET_HALF - 1.2, 1.7, -length * 0.25), "target": Vector3(STREET_HALF, 1.6, -length * 0.25 - 1.0), "fov": 60.0, "purpose": "1.2 m from a wall: its texels at screen resolution"},
		"aerial": {"pos": Vector3(60, 90, 40), "target": Vector3(0, 0, -length * 0.5), "fov": 60.0, "purpose": "above the street: every building far"},
	}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(0, 1.7, 5), Vector3(0, 3, -120), 70.0],
		[10.0, Vector3(2, 1.7, -length * 0.33), Vector3(-4, 3, -length * 0.33 - 100), 70.0],
		[20.0, Vector3(-2, 1.7, -length * 0.66), Vector3(6, 3, -length * 0.66 - 100), 70.0],
		[26.0, Vector3(0, 30, -length * 0.9), Vector3(0, 0, -length * 0.4), 65.0],
		[30.0, Vector3(40, 60, 20), Vector3(0, 0, -length * 0.5), 60.0],
	])
	temporal_seqs = {
		"street_walk": {"start": 0.0, "frames": 120, "purpose": "walking down the street (new facades come close)",
			"path": CameraPath.from_array([[0.0, Vector3(0, 1.7, -40), Vector3(0, 3, -160), 70.0], [2.0, Vector3(0, 1.7, -64), Vector3(0, 3, -184), 70.0]])},
	}


func set_time(_t: float) -> void:
	pass
