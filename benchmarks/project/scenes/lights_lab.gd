extends "res://bench/scene_base.gd"
## LIGHTS LAB (built in code, deterministic): a long street at night lit by many positional lights.
##
## The Forward+ light loop under the situation it meets in large scenes: hundreds of omni and spot lights spread in
## depth along the view, so that lights near the camera and lights far down the street overlap on screen. Also covers
## the cases the light clusters must handle exactly: the camera inside light volumes, spot cones pointing at the camera
## and away from it, shadowed and unshadowed lights, decals, reflection probes, volumetric fog (whose froxels read the
## same clusters) and a scaled light. Used as a benchmark and as an image-identity scene (decision 0008).

const STREET_LENGTH := 240.0
const STREET_WIDTH := 16.0
const OMNI_COUNT := 192
const SPOT_COUNT := 48


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "lights"
	_setup_common()
	_setup_environment()
	_build_street()
	_build_lights()
	_build_decals_and_probes()
	_define_cameras()
	set_time(0.0)
	info = {"stats": scene_stats(self), "omni_lights": OMNI_COUNT, "spot_lights": SPOT_COUNT, "notes": "Many positional lights spread in depth (light clusters)."}


func _setup_environment() -> void:
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.01, 0.012, 0.02)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.05, 0.05, 0.07)
	env.reflected_light_source = Environment.REFLECTION_SOURCE_BG
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.ssao_enabled = true
	env.glow_enabled = true
	env.volumetric_fog_enabled = true
	env.volumetric_fog_density = 0.01
	env.volumetric_fog_length = 96.0
	cam_attr.auto_exposure_enabled = false


func _mat(albedo: Color, roughness := 0.6, metallic := 0.0) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = albedo
	m.roughness = roughness
	m.metallic = metallic
	return m


func _box(box_name: String, size: Vector3, pos: Vector3, mat: Material) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.name = box_name
	var bm := BoxMesh.new()
	bm.size = size
	mi.mesh = bm
	mi.position = pos
	mi.material_override = mat
	add_child(mi)
	return mi


func _build_street() -> void:
	var ground := _mat(Color(0.35, 0.35, 0.37), 0.35)
	var wall := _mat(Color(0.6, 0.55, 0.5), 0.8)
	var metal := _mat(Color(0.8, 0.8, 0.85), 0.25, 1.0)
	_box("road", Vector3(STREET_WIDTH, 0.2, STREET_LENGTH), Vector3(0, -0.1, -STREET_LENGTH * 0.5 + 10.0), ground)
	# Facades on both sides, broken into blocks of varying height with gaps (side streets).
	var z := 10.0
	var i := 0
	while z > -STREET_LENGTH + 10.0:
		var depth := 8.0 + float(i % 3) * 3.0
		var height := 6.0 + float((i * 7) % 5) * 2.5
		for side in [-1.0, 1.0]:
			_box("facade_%d_%d" % [i, int(side)], Vector3(4.0, height, depth), Vector3(side * (STREET_WIDTH * 0.5 + 2.0), height * 0.5, z - depth * 0.5), wall)
		z -= depth + (2.0 if i % 4 == 3 else 0.0)
		i += 1
	# Columns and kiosks along the street (occluders and receivers between the lights).
	for k in 40:
		var pz := 6.0 - float(k) * 6.0
		var px := -5.0 if k % 2 == 0 else 5.0
		_box("column_%d" % k, Vector3(0.5, 4.0, 0.5), Vector3(px, 2.0, pz), metal)
		if k % 5 == 2:
			_box("kiosk_%d" % k, Vector3(2.0, 2.5, 2.0), Vector3(-px * 0.4, 1.25, pz - 3.0), wall)


func _build_lights() -> void:
	# Deterministic pseudo-random placement (no RNG state shared with anything else).
	var h := 12345
	for k in OMNI_COUNT:
		h = (h * 1103515245 + 12345) & 0x7fffffff
		var u := float(h % 1000) / 1000.0
		var l := OmniLight3D.new()
		l.name = "omni_%d" % k
		var pz := 8.0 - float(k) / OMNI_COUNT * (STREET_LENGTH - 16.0)
		var px := (u - 0.5) * (STREET_WIDTH - 2.0)
		var py := 0.6 + float(k % 4) * 1.3
		l.position = Vector3(px, py, pz)
		l.omni_range = 3.0 + float(k % 7) * 1.8
		l.light_energy = 1.5 + float(k % 3)
		l.light_color = Color.from_hsv(fmod(u * 3.7, 1.0), 0.45, 1.0)
		l.shadow_enabled = k % 12 == 0
		add_child(l)
	for k in SPOT_COUNT:
		var l := SpotLight3D.new()
		l.name = "spot_%d" % k
		var pz := 4.0 - float(k) / SPOT_COUNT * (STREET_LENGTH - 20.0)
		var side := -1.0 if k % 2 == 0 else 1.0
		l.position = Vector3(side * 6.0, 5.0, pz)
		# Alternate: down onto the road, along the street toward the camera, along the street away from it, wide angle.
		match k % 4:
			0:
				l.rotation_degrees = Vector3(-90.0, 0.0, 0.0)
				l.spot_angle = 35.0
			1:
				l.rotation_degrees = Vector3(-15.0, 0.0, 0.0)
				l.spot_angle = 25.0
			2:
				l.rotation_degrees = Vector3(-15.0, 180.0, 0.0)
				l.spot_angle = 25.0
			3:
				l.rotation_degrees = Vector3(-60.0, 90.0 * side, 0.0)
				l.spot_angle = 75.0
		l.spot_range = 8.0 + float(k % 5) * 3.0
		l.light_energy = 3.0
		l.shadow_enabled = k % 8 == 1
		add_child(l)
	# A scaled light (the clusters scale the proxy by the transform, the shaders attenuate with the unscaled range).
	var scaled := OmniLight3D.new()
	scaled.name = "omni_scaled"
	scaled.position = Vector3(0.0, 1.0, -30.0)
	scaled.scale = Vector3(0.5, 0.5, 0.5)
	scaled.omni_range = 9.0
	scaled.light_energy = 2.0
	add_child(scaled)
	# A light around the first camera positions (the camera inside the light volume).
	var around := OmniLight3D.new()
	around.name = "omni_camera"
	around.position = Vector3(0.0, 2.0, 12.0)
	around.omni_range = 12.0
	around.light_energy = 0.8
	add_child(around)
	# Optional (--area_lights=N, default 0, not in the benchmark): area lights over the street (tests of the paths
	# that depend on area lights being present, e.g. volumetric fog's specialized density pass, decision 0056).
	var area_count := int(args.get("area_lights", "0"))
	for k in area_count:
		var a := AreaLight3D.new()
		a.name = "area_%d" % k
		a.position = Vector3((float(k % 3) - 1.0) * 4.0, 4.5, 6.0 - float(k) * 9.0)
		a.rotation_degrees = Vector3(-90.0 + float(k % 2) * 30.0, 0.0, 0.0)
		a.set_area_size(Vector2(2.0 + float(k % 2), 1.0))
		a.light_energy = 4.0
		a.area_range = 10.0
		a.light_color = Color.from_hsv(fmod(float(k) * 0.37, 1.0), 0.5, 1.0)
		a.shadow_enabled = k % 2 == 0
		add_child(a)


func _build_decals_and_probes() -> void:
	var img := Image.create(64, 64, false, Image.FORMAT_RGBA8)
	for y in 64:
		for x in 64:
			var c := 1.0 if ((x / 8) + (y / 8)) % 2 == 0 else 0.2
			img.set_pixel(x, y, Color(c, c * 0.8, 0.3, 1.0))
	var tex := ImageTexture.create_from_image(img)
	for k in 16:
		var d := Decal.new()
		d.name = "decal_%d" % k
		d.texture_albedo = tex
		d.size = Vector3(3.0, 2.0, 3.0)
		d.position = Vector3(-4.0 + float(k % 3) * 4.0, 0.0, 2.0 - float(k) * 13.0)
		add_child(d)
	for k in 4:
		var p := ReflectionProbe.new()
		p.name = "probe_%d" % k
		p.size = Vector3(STREET_WIDTH, 10.0, 40.0)
		p.position = Vector3(0.0, 4.0, -float(k) * 55.0)
		p.update_mode = ReflectionProbe.UPDATE_ONCE
		add_child(p)


func _define_cameras() -> void:
	fixed_cameras = {
		"lights_street": {"pos": Vector3(0, 1.7, 12), "target": Vector3(0, 1.5, -60), "fov": 60.0, "time": 0.0, "purpose": "down the street: near and far lights overlap on screen; camera inside a light volume"},
		"lights_low": {"pos": Vector3(3, 0.5, -40), "target": Vector3(-1, 0.8, -140), "fov": 70.0, "time": 0.0, "purpose": "grazing view along the road, many light volumes cut by the near plane region"},
		"lights_up": {"pos": Vector3(-5, 1.2, -90), "target": Vector3(6, 6, -100), "fov": 60.0, "time": 0.0, "purpose": "spot cones toward and away from the camera, facades"},
		"lights_far": {"pos": Vector3(0, 12, 20), "target": Vector3(0, 0, -200), "fov": 45.0, "time": 0.0, "purpose": "whole street from above, distant lights and fog"},
	}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(0, 1.7, 12), Vector3(0, 1.5, -60), 60.0],
		[10.0, Vector3(2, 1.7, -60), Vector3(-1, 1.5, -140), 60.0],
		[20.0, Vector3(-2, 2.5, -130), Vector3(1, 1.5, -220), 60.0],
		[30.0, Vector3(0, 12, 20), Vector3(0, 0, -200), 45.0],
	])
	temporal_seqs = {
		"lights_walk": {"start": 2.0, "frames": 60, "purpose": "walk down the street",
			"path": CameraPath.from_array([[0.0, Vector3(0, 1.7, 0), Vector3(0, 1.5, -80), 60.0], [1.0, Vector3(0, 1.7, -6), Vector3(0, 1.5, -86), 60.0]])},
	}


func set_time(_t: float) -> void:
	pass
