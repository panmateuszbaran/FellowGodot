extends "res://bench/scene_base.gd"
## CITY LAB (built in code, deterministic, driven only through set_time()): many separate objects.
##
## The other benchmark scenes are limited by the GPU. This one is limited by the CPU, as open-world and city content
## often is: about 17 000 MeshInstance3D nodes (buildings of 2-4 parts, street lamps, trees, crates, benches) with a
## dozen shared meshes and materials, so the per-object work of culling, shadow-caster culling (four sun cascades),
## render-list building and draw submission dominates; 300 carts move every frame (instance updates). Measures the
## render thread (render_cpu_ms, setup_cpu_ms) as much as the GPU.

const LOTS := 56 # lots per side
const LOT := 11.0 # metres per lot
const STREET := 5.0
const CARTS := 300

var sun: DirectionalLight3D
var carts: Array[MeshInstance3D] = []
var cart_lanes: PackedFloat32Array = PackedFloat32Array()
var cart_speed: PackedFloat32Array = PackedFloat32Array()
var cart_axis: PackedByteArray = PackedByteArray()
var _meshes := {}
var _mats: Array[StandardMaterial3D] = []
var _unique: Array[StandardMaterial3D] = [] # --city_materials=N: building walls get one of N materials (draw-heavy variant)
var _count := 0


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "city"
	_setup_common()
	_setup_environment()
	_make_resources()
	_build_city()
	_build_carts()
	_build_lights()
	if args.get("city_lightmap", "0") == "1":
		_build_lightmap_capture()
	_define_cameras()
	set_time(0.0)
	info = {"stats": scene_stats(self), "nodes": _count, "notes": "CPU-bound city: ~17k separate objects, 300 moving carts, sun with 4 cascades."}


func _setup_environment() -> void:
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.ssao_enabled = true
	env.glow_enabled = true
	cam_attr.auto_exposure_enabled = false


func _mat(c: Color, rough: float, metal := 0.0) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = rough
	m.metallic = metal
	return m


func _make_resources() -> void:
	var box := BoxMesh.new()
	_meshes["box"] = box
	var prism := PrismMesh.new()
	_meshes["prism"] = prism
	var cyl := CylinderMesh.new()
	cyl.radial_segments = 8
	cyl.rings = 1
	_meshes["cyl"] = cyl
	var sph := SphereMesh.new()
	sph.radial_segments = 10
	sph.rings = 6
	_meshes["sphere"] = sph
	for i in 8:
		_mats.append(_mat(Color.from_hsv(0.05 + 0.08 * i, 0.25 + 0.05 * (i % 3), 0.55 + 0.05 * (i % 4)), 0.6 + 0.04 * i))
	_mats.append(_mat(Color(0.35, 0.36, 0.38), 0.4, 0.8)) # 8: metal
	_mats.append(_mat(Color(0.25, 0.45, 0.2), 0.8)) # 9: foliage
	_mats.append(_mat(Color(0.4, 0.3, 0.2), 0.9)) # 10: wood
	_mats.append(_mat(Color(0.5, 0.15, 0.12), 0.7)) # 11: roof
	# Draw-heavy variant (--city_materials=N, N > 0): the walls of each lot use one of N materials, so the lists hold
	# thousands of different materials and few elements can be drawn instanced (draw submission dominates instead of
	# per-object work). Same objects and transforms as the default city.
	var unique := int(args.get("city_materials", "0"))
	for i in unique:
		_unique.append(_mat(Color.from_hsv(fposmod(0.618034 * i, 1.0), 0.2 + 0.3 * float(i % 5) / 4.0, 0.45 + 0.4 * float(i % 7) / 6.0), 0.5 + 0.4 * float(i % 11) / 10.0))


func _add(mesh_key: String, mat: Material, pos: Vector3, size: Vector3, rot_y := 0.0) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = _meshes[mesh_key]
	mi.material_override = mat
	mi.transform = Transform3D(Basis(Vector3.UP, deg_to_rad(rot_y)).scaled(size), pos)
	add_child(mi)
	_count += 1
	return mi


func _build_city() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 20261006
	var half := LOTS * LOT * 0.5
	var ground := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(LOTS * LOT + 40.0, LOTS * LOT + 40.0)
	ground.mesh = pm
	ground.material_override = _mat(Color(0.42, 0.42, 0.4), 0.85)
	add_child(ground)
	for ix in LOTS:
		for iz in LOTS:
			var cx := -half + (ix + 0.5) * LOT
			var cz := -half + (iz + 0.5) * LOT
			var w := LOT - STREET - rng.randf_range(0.0, 1.5)
			var d := LOT - STREET - rng.randf_range(0.0, 1.5)
			var h := rng.randf_range(4.0, 14.0) * (1.6 if (ix + iz) % 9 == 0 else 1.0)
			var m := _mats[rng.randi_range(0, 7)]
			if not _unique.is_empty():
				m = _unique[(ix * LOTS + iz) % _unique.size()]
			_add("box", m, Vector3(cx, h * 0.5, cz), Vector3(w, h, d))
			var parts := rng.randi_range(1, 3)
			if parts >= 1:
				_add("prism", _mats[11], Vector3(cx, h + 1.2, cz), Vector3(w, 2.4, d), 90.0 if rng.randf() < 0.5 else 0.0)
			if parts >= 2:
				var m2 := _mats[rng.randi_range(0, 7)]
				if not _unique.is_empty():
					m2 = _unique[(ix * LOTS + iz + 1) % _unique.size()]
				_add("box", m2, Vector3(cx + w * 0.25, h * 0.3, cz - d * 0.5 - 0.6), Vector3(w * 0.4, h * 0.6, 1.2))
			if parts >= 3:
				_add("cyl", _mats[8], Vector3(cx - w * 0.3, h + 1.6, cz + d * 0.2), Vector3(0.6, 3.2, 0.6))
			# Street furniture on the lot's corner: a lamp post (pole + head), a tree (trunk + crown) or crates.
			var sx := cx + LOT * 0.5 - STREET * 0.3
			var sz := cz + LOT * 0.5 - STREET * 0.3
			match (ix * 7 + iz * 3) % 3:
				0:
					_add("cyl", _mats[8], Vector3(sx, 2.5, sz), Vector3(0.18, 5.0, 0.18))
					_add("sphere", _mats[8], Vector3(sx, 5.1, sz), Vector3(0.6, 0.4, 0.6))
				1:
					_add("cyl", _mats[10], Vector3(sx, 1.2, sz), Vector3(0.3, 2.4, 0.3))
					_add("sphere", _mats[9], Vector3(sx, 3.3, sz), Vector3(2.4, 2.6, 2.4))
				_:
					for k in 2:
						_add("box", _mats[10], Vector3(sx + k * 0.9, 0.4, sz), Vector3(0.8, 0.8, 0.8), 15.0 * k)
			# A bench along the street.
			_add("box", _mats[10], Vector3(cx, 0.25, cz + LOT * 0.5 - STREET * 0.7), Vector3(1.6, 0.5, 0.5))


func _build_carts() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	var half := LOTS * LOT * 0.5
	for i in CARTS:
		var mi := _add("box", _mats[rng.randi_range(0, 7)], Vector3.ZERO, Vector3(1.6, 1.2, 2.6))
		carts.append(mi)
		var lane := -half + LOT * rng.randi_range(1, LOTS - 1) - STREET * 0.5 + rng.randf_range(-1.0, 1.0)
		cart_lanes.append(lane)
		cart_speed.append(rng.randf_range(4.0, 11.0) * (1.0 if rng.randf() < 0.5 else -1.0))
		cart_axis.append(rng.randi_range(0, 1))


func _build_lights() -> void:
	sun = DirectionalLight3D.new()
	sun.name = "sun"
	sun.light_energy = 2.0
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 300.0
	sun.light_angular_distance = 0.5
	apply_sun_overrides(sun)
	add_child(sun)
	sun.rotation_degrees = Vector3(-50.0, -35.0, 0.0)
	var rng := RandomNumberGenerator.new()
	rng.seed = 11
	for i in 48:
		var o := OmniLight3D.new()
		o.position = Vector3(rng.randf_range(-120.0, 120.0), 4.5, rng.randf_range(-120.0, 120.0))
		o.omni_range = 14.0
		o.light_energy = 2.0
		o.light_color = Color(1.0, 0.8, 0.55)
		add_child(o)


func _build_lightmap_capture() -> void:
	# Verification option (--city_lightmap=1): a LightmapGI whose probe data is one large tetrahedron with different SH
	# at its corners, and every object in "dynamic" GI mode, so each instance in it gets its own lightmap capture
	# (the renderer gives out up to 2048 capture slots per frame, in list order). No baked lightmap texture.
	var lm := LightmapGI.new()
	var data := LightmapGIData.new()
	var pts := PackedVector3Array([Vector3(-2000, -100, -2000), Vector3(2000, -100, -2000), Vector3(0, -100, 2000), Vector3(0, 3000, 0)])
	var sh := PackedColorArray()
	for c in 4:
		for j in 9:
			sh.append(Color(0.2 + 0.2 * c, 0.1 + 0.05 * j, 0.6 - 0.1 * c, 1.0) * (1.0 if j == 0 else 0.3))
	# One BSP node (plane 0x + 0y + 0z = -1e9: every point is "over") whose both sides are tetrahedron 0 (leaf -1).
	var plane := PackedFloat32Array([0.0, 1.0, 0.0, -1.0e9]).to_byte_array().to_int32_array()
	var bsp := PackedInt32Array([plane[0], plane[1], plane[2], plane[3], -1, -1])
	data.set("probe_data", {"bounds": AABB(Vector3(-LOTS * LOT * 0.5 - 30, -10, -LOTS * LOT * 0.5 - 30), Vector3(LOTS * LOT + 60, 80, LOTS * LOT + 60)),
		"points": pts, "tetrahedra": PackedInt32Array([0, 1, 2, 3]), "bsp": bsp, "sh": sh, "interior": false, "baked_exposure": 1.0})
	lm.light_data = data
	add_child(lm)
	for n in get_children():
		if n is GeometryInstance3D:
			(n as GeometryInstance3D).gi_mode = GeometryInstance3D.GI_MODE_DYNAMIC


func _define_cameras() -> void:
	fixed_cameras = {
		"city_aerial": {"pos": Vector3(-150, 70, 150), "target": Vector3(0, 0, 0), "fov": 60.0, "time": 2.0, "purpose": "wide aerial view, most of the city in the frustum"},
		"city_street": {"pos": Vector3(3, 2.5, 40), "target": Vector3(3, 3, -60), "fov": 70.0, "time": 2.0, "purpose": "street level, long view down a street"},
		"city_roofs": {"pos": Vector3(40, 30, 20), "target": Vector3(-20, 5, -40), "fov": 60.0, "time": 2.0, "purpose": "over the roofs, shadows"},
	}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(-200, 60, 200), Vector3(0, 0, 0), 60.0],
		[8.0, Vector3(0, 35, 120), Vector3(20, 0, -20), 60.0],
		[16.0, Vector3(120, 20, 0), Vector3(0, 5, -40), 65.0],
		[24.0, Vector3(20, 8, -100), Vector3(-60, 4, -60), 70.0],
		[30.0, Vector3(-120, 40, -150), Vector3(0, 0, 0), 60.0],
	])
	temporal_seqs = {
		"city_pan": {"start": 3.0, "frames": 60, "purpose": "pan over the roofs with moving carts",
			"path": CameraPath.from_array([[0.0, Vector3(-40, 30, 60), Vector3(-20, 0, -20), 60.0], [1.0, Vector3(40, 30, 60), Vector3(20, 0, -20), 60.0]])},
		"city_sky_pan": {"start": 3.0, "frames": 60, "purpose": "a fast pan along the sky from the roofs (the clouds' reprojection)",
			"path": CameraPath.from_array([[0.0, Vector3(0, 40, 0), Vector3(200, 110, 60), 70.0], [1.0, Vector3(0, 40, 0), Vector3(-60, 110, 200), 70.0]])},
	}


func set_time(t: float) -> void:
	var half := LOTS * LOT * 0.5
	var span := LOTS * LOT
	for i in carts.size():
		var along := fposmod(t * cart_speed[i] + float(i) * 37.0, span) - half
		var mi := carts[i]
		if cart_axis[i] == 0:
			mi.position = Vector3(along, 0.6, cart_lanes[i])
		else:
			mi.position = Vector3(cart_lanes[i], 0.6, along)
