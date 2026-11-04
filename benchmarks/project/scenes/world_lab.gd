extends "res://bench/scene_base.gd"
## WORLD LAB (milestone 46; built in code, deterministic): a 768 m square world in 12 x 12 cells of 64 m, each a saved
## PackedScene (a ground tile, 10 buildings as one mesh of ~1.5 m quads, 40 crates in a MultiMesh; materials from
## Streaming Lab's textures): about 3.6 M triangles and 144 different meshes in all.
##
##   --world_streaming=1   the cells under a FellowWorldStreamer3D (FellowEngine), loaded and freed around the camera;
##                         0 (default, and on stock Godot): every cell instantiated at load, as a level is today.
##   --world_distance=<m>  the streamer's load distance (320; the camera's far plane is 300, so what the camera can see
##                         is loaded); --world_cells=<n> cells per side (12).
##
## The cells are generated into res://external/world/ the first time (or with --world_generate=1): they need the
## Streaming Lab's textures (benchmarks/scripts/make_streaming_textures.py).

const CELL := 64.0
const TEX_DIR := "res://external/streaming"
const DIR := "res://external/world"

var n_cells := 12
var sun: DirectionalLight3D
var streamer: Node3D = null
var _mats: Array[StandardMaterial3D] = []
var _generated := 0


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "world"
	n_cells = int(args.get("world_cells", "12"))
	_setup_common()
	camera.far = 300.0
	_setup_environment()
	var t0 := Time.get_ticks_usec()
	if not _ensure_cells():
		return
	var gen_ms := (Time.get_ticks_usec() - t0) / 1000.0
	var t1 := Time.get_ticks_usec()
	var use_streaming := str(args.get("world_streaming", "0")) == "1" and ClassDB.class_exists("FellowWorldStreamer3D")
	if use_streaming:
		streamer = ClassDB.instantiate("FellowWorldStreamer3D")
		streamer.name = "streamer"
		streamer.set("load_distance", float(args.get("world_distance", "320")))
		streamer.set("unload_margin", float(args.get("world_margin", "32")))
		streamer.set("max_loading", int(args.get("world_max_loading", "2")))
		streamer.set("max_activations_per_frame", int(args.get("world_activations", "1")))
		add_child(streamer)
	for x in n_cells:
		for z in n_cells:
			var path := _cell_path(x, z)
			var origin := _cell_origin(x, z)
			if use_streaming:
				var cell: Node3D = ClassDB.instantiate("FellowStreamingCell3D")
				cell.name = "cell_%02d_%02d" % [x, z]
				cell.position = origin
				cell.set("scene_path", path)
				cell.set("bounds", AABB(Vector3(-CELL * 0.5, -1.0, -CELL * 0.5), Vector3(CELL, 40.0, CELL)))
				streamer.add_child(cell)
			else:
				var ps: PackedScene = load(path)
				var n: Node3D = ps.instantiate()
				n.position = origin
				add_child(n)
	var place_ms := (Time.get_ticks_usec() - t1) / 1000.0
	_build_lights()
	_define_cameras()
	set_time(0.0)
	info = {"cells": n_cells * n_cells, "cell_size_m": CELL, "streaming": use_streaming, "generated_cells": _generated,
		"generate_ms": gen_ms, "place_ms": place_ms, "stats": scene_stats(self),
		"notes": "%d x %d cells of %d m, each a saved PackedScene (ground, 10 buildings as one mesh, 40 crates)." % [n_cells, n_cells, int(CELL)]}


func _cell_path(x: int, z: int) -> String:
	return DIR.path_join("cell_%02d_%02d.scn" % [x, z])


func _cell_origin(x: int, z: int) -> Vector3:
	var half := n_cells * CELL * 0.5
	return Vector3(-half + (x + 0.5) * CELL, 0.0, -half + (z + 0.5) * CELL)


func _setup_environment() -> void:
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	cam_attr.auto_exposure_enabled = false


func _ensure_cells() -> bool:
	var regenerate := str(args.get("world_generate", "0")) == "1"
	DirAccess.make_dir_recursive_absolute(DIR)
	var missing := []
	for x in n_cells:
		for z in n_cells:
			if regenerate or not FileAccess.file_exists(_cell_path(x, z)):
				missing.append(Vector2i(x, z))
	if missing.is_empty():
		return true
	for i in 48:
		var a: Texture2D = load(TEX_DIR.path_join("m%02d_albedo.ctex" % i))
		var nm: Texture2D = load(TEX_DIR.path_join("m%02d_normal.ctex" % i))
		if a == null or nm == null:
			load_error = "World Lab: Streaming Lab's textures missing in %s (benchmarks/scripts/make_streaming_textures.py)" % TEX_DIR
			return false
		var m := StandardMaterial3D.new()
		m.albedo_texture = a
		m.normal_enabled = true
		m.normal_texture = nm
		m.roughness = 0.75
		m.uv1_scale = Vector3(0.25, 0.25, 1.0) # the meshes' UVs are in metres: one repeat every 4 m
		_mats.append(m)
	for c in missing:
		var root := _make_cell(c.x, c.y)
		var ps := PackedScene.new()
		ps.pack(root)
		var err := ResourceSaver.save(ps, _cell_path(c.x, c.y))
		root.free()
		if err != OK:
			load_error = "World Lab: could not save %s (%d)" % [_cell_path(c.x, c.y), err]
			return false
		_generated += 1
	print("world lab: generated %d cells" % _generated)
	return true


func _make_cell(x: int, z: int) -> Node3D:
	var rng := RandomNumberGenerator.new()
	rng.seed = 46000 + x * 131 + z
	var root := Node3D.new()
	root.name = "cell"
	# The ground tile.
	var ground := MeshInstance3D.new()
	ground.name = "ground"
	var pm := PlaneMesh.new()
	pm.size = Vector2(CELL, CELL)
	ground.mesh = pm
	var gm: StandardMaterial3D = _mats[(x * 7 + z * 3) % _mats.size()].duplicate()
	gm.uv1_scale = Vector3(CELL / 4.0, CELL / 4.0, 1.0)
	ground.material_override = gm
	root.add_child(ground)
	ground.owner = root
	# Ten buildings in one mesh: walls of ~1.5 m quads (UVs in metres), one material per cell.
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var uvs := PackedVector2Array()
	for b in 10:
		var w := rng.randf_range(8.0, 16.0)
		var d := rng.randf_range(8.0, 16.0)
		var h := rng.randf_range(10.0, 32.0)
		var cx := rng.randf_range(-CELL * 0.5 + w, CELL * 0.5 - w)
		var cz := rng.randf_range(-CELL * 0.5 + d, CELL * 0.5 - d)
		_box_quads(verts, normals, uvs, Vector3(cx, h * 0.5, cz), Vector3(w, h, d), 1.5)
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	var am := ArrayMesh.new()
	am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var buildings := MeshInstance3D.new()
	buildings.name = "buildings"
	buildings.mesh = am
	buildings.material_override = _mats[(x * 5 + z * 11 + 1) % _mats.size()]
	root.add_child(buildings)
	buildings.owner = root
	# Forty crates.
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	var crate := BoxMesh.new()
	crate.size = Vector3(1, 1, 1)
	mm.mesh = crate
	mm.instance_count = 40
	for i in 40:
		mm.set_instance_transform(i, Transform3D(Basis(Vector3.UP, rng.randf() * TAU), Vector3(rng.randf_range(-30, 30), 0.5, rng.randf_range(-30, 30))))
	var crates := MultiMeshInstance3D.new()
	crates.name = "crates"
	crates.multimesh = mm
	crates.material_override = _mats[(x + z * 13 + 2) % _mats.size()]
	root.add_child(crates)
	crates.owner = root
	return root


func _box_quads(verts: PackedVector3Array, normals: PackedVector3Array, uvs: PackedVector2Array, center: Vector3, size: Vector3, step: float) -> void:
	var h := size * 0.5
	# Four walls and the roof: each face a grid of quads; UVs are the face's coordinates in metres.
	var faces := [
		[Vector3(0, 0, 1), Vector3(1, 0, 0), Vector3(0, 1, 0), size.x, size.y, h.z],
		[Vector3(0, 0, -1), Vector3(-1, 0, 0), Vector3(0, 1, 0), size.x, size.y, h.z],
		[Vector3(1, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0), size.z, size.y, h.x],
		[Vector3(-1, 0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0), size.z, size.y, h.x],
		[Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, -1), size.x, size.z, h.y],
	]
	for f in faces:
		var n: Vector3 = f[0]
		var u: Vector3 = f[1]
		var v: Vector3 = f[2]
		var fw: float = f[3]
		var fh: float = f[4]
		var nu := maxi(1, int(ceil(fw / step)))
		var nv := maxi(1, int(ceil(fh / step)))
		var o: Vector3 = center + n * float(f[5]) - u * fw * 0.5 - v * fh * 0.5
		for j in nv:
			for i in nu:
				var u0 := fw * i / nu
				var u1 := fw * (i + 1) / nu
				var v0 := fh * j / nv
				var v1 := fh * (j + 1) / nv
				var p00 := o + u * u0 + v * v0
				var p10 := o + u * u1 + v * v0
				var p01 := o + u * u0 + v * v1
				var p11 := o + u * u1 + v * v1
				verts.append_array([p00, p11, p10, p00, p01, p11])
				for k in 6:
					normals.append(n)
				uvs.append_array([Vector2(u0, -v0), Vector2(u1, -v1), Vector2(u1, -v0), Vector2(u0, -v0), Vector2(u0, -v1), Vector2(u1, -v1)])


func _build_lights() -> void:
	sun = DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 35, 0)
	sun.light_energy = 1.2
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 250.0
	add_child(sun)
	apply_sun_overrides(sun)


func _define_cameras() -> void:
	var half := n_cells * CELL * 0.5
	fixed_cameras = {
		"street": {"pos": Vector3(-half + 40, 2.0, -half + 40), "target": Vector3(0, 4, 0), "fov": 70.0, "purpose": "street level near a corner, looking across the world"},
		"centre": {"pos": Vector3(10, 30, 10), "target": Vector3(-60, 0, -60), "fov": 65.0, "purpose": "above the centre"},
		"far_corner": {"pos": Vector3(half - 30, 3.0, half - 30), "target": Vector3(half - 120, 6, half - 140), "fov": 70.0, "purpose": "the opposite corner (other cells)"},
	}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(-half + 30, 25, -half + 30), Vector3(0, 0, 0), 65.0],
		[10.0, Vector3(-half * 0.2, 25, -half * 0.1), Vector3(half, 0, half * 0.3), 65.0],
		[20.0, Vector3(half * 0.4, 25, half * 0.3), Vector3(half, 0, half), 65.0],
		[30.0, Vector3(half - 30, 25, half - 30), Vector3(0, 0, 0), 65.0],
	])
	temporal_seqs = {}


func set_time(_t: float) -> void:
	pass
