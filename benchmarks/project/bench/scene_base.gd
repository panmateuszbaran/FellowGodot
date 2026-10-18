extends Node3D
## Base class for FellowBench scenes (not a class_name: the project must run without an editor-built class cache).
##
## A scene builds its content in build(), owns one Camera3D, one WorldEnvironment and a
## CameraAttributesPractical, and exposes:
##   fixed_cameras  : Dictionary  name -> {"pos": Vector3, "target": Vector3, "fov": float, "time": float, "purpose": String}
##   flythrough     : camera_path.gd instance (deterministic performance sequence)
##   temporal_seqs  : Dictionary  name -> {"path": camera_path, "start": float, "frames": int, "purpose": String}
## Animated objects must be driven ONLY through set_time(t) (virtual seconds), never by wall-clock.

const CameraPath = preload("res://bench/camera_path.gd")

var bench_name := "base"
var camera: Camera3D
var world_env: WorldEnvironment
var env: Environment
var cam_attr: CameraAttributesPractical
var fixed_cameras := {}
var flythrough
var temporal_seqs := {}
var info := {}  # free-form description exported to results (asset versions, light setup, ...)
var args := {}
var load_error := ""


func _setup_common(p_env: Environment = null) -> void:
	camera = Camera3D.new()
	camera.name = "BenchCamera"
	camera.near = 0.05
	camera.far = 4000.0
	add_child(camera)
	camera.make_current()
	world_env = WorldEnvironment.new()
	env = p_env if p_env else Environment.new()
	world_env.environment = env
	cam_attr = CameraAttributesPractical.new()
	cam_attr.auto_exposure_enabled = false
	world_env.camera_attributes = cam_attr
	add_child(world_env)


func build(p_args: Dictionary) -> void:
	args = p_args


func set_time(_t: float) -> void:
	pass


func set_camera_pose(pos: Vector3, target: Vector3, fov: float) -> void:
	var s := CameraPath.new()
	var d := s._make(pos, target, fov)
	camera.global_transform = d["xform"]
	camera.fov = fov


func apply_path_sample(d: Dictionary) -> void:
	camera.global_transform = d["xform"]
	camera.fov = d["fov"]


## Analysis-only overrides of the benchmark sun, identical for every build and recorded in the results:
##   --sun_blend=0|1 (cascade blending), --sun_shadow=0|1, --sun_angular=<deg> (0 = PCF instead of PCSS).
var sun_overrides := {}


func apply_sun_overrides(sun: DirectionalLight3D) -> void:
	if args.has("sun_blend"):
		sun.directional_shadow_blend_splits = str(args["sun_blend"]) == "1"
		sun_overrides["blend_splits"] = sun.directional_shadow_blend_splits
	if args.has("sun_shadow"):
		sun.shadow_enabled = str(args["sun_shadow"]) == "1"
		sun_overrides["shadow_enabled"] = sun.shadow_enabled
	if args.has("sun_angular"):
		sun.light_angular_distance = float(args["sun_angular"])
		sun_overrides["angular_distance"] = sun.light_angular_distance


func describe() -> Dictionary:
	var cams := {}
	for k in fixed_cameras.keys():
		var c: Dictionary = fixed_cameras[k]
		cams[k] = {"pos": [c["pos"].x, c["pos"].y, c["pos"].z], "target": [c["target"].x, c["target"].y, c["target"].z], "fov": c.get("fov", 60.0), "time": c.get("time", 0.0), "purpose": c.get("purpose", "")}
	var seqs := {}
	for k in temporal_seqs.keys():
		seqs[k] = {"start": temporal_seqs[k]["start"], "frames": temporal_seqs[k]["frames"], "purpose": temporal_seqs[k].get("purpose", ""), "path": temporal_seqs[k]["path"].to_json()}
	return {
		"name": bench_name,
		"info": info,
		"fixed_cameras": cams,
		"flythrough": flythrough.to_json() if flythrough else [],
		"flythrough_duration_s": flythrough.duration() if flythrough else 0.0,
		"temporal_sequences": seqs,
		"sun_overrides": sun_overrides,
	}


## Utility: count scene statistics (meshes, surfaces, triangles, materials, lights).
func scene_stats(root: Node) -> Dictionary:
	# "tri_cache": the triangles of each mesh surface, counted once per mesh: a PrimitiveMesh's arrays are read back from
	# the GPU (a flush and wait each), which for City Lab's 17 000 instances was most of its build time (m12_f174).
	var st := {"mesh_instances": 0, "surfaces": 0, "triangles": 0, "materials": {}, "lights": 0, "multimesh_instances": 0, "tri_cache": {}}
	_stat_rec(root, st)
	st["unique_materials"] = st["materials"].size()
	st.erase("materials")
	st.erase("tri_cache")
	return st


func _stat_rec(n: Node, st: Dictionary) -> void:
	if n is MeshInstance3D and n.mesh:
		st["mesh_instances"] += 1
		var m: Mesh = n.mesh
		for i in range(m.get_surface_count()):
			st["surfaces"] += 1
			if m is ArrayMesh:
				var il: int = m.surface_get_array_index_len(i)
				st["triangles"] += (il if il > 0 else m.surface_get_array_len(i)) / 3
			else:
				var key := "%d:%d" % [m.get_instance_id(), i]
				if not st["tri_cache"].has(key):
					var arr := m.surface_get_arrays(i)
					var idx = arr[Mesh.ARRAY_INDEX]
					var t := 0
					if idx:
						t = idx.size() / 3
					elif arr[Mesh.ARRAY_VERTEX]:
						t = arr[Mesh.ARRAY_VERTEX].size() / 3
					st["tri_cache"][key] = t
				st["triangles"] += st["tri_cache"][key]
			var mat = n.get_active_material(i)
			if mat:
				st["materials"][mat.get_instance_id()] = true
	elif n is MultiMeshInstance3D and n.multimesh:
		st["multimesh_instances"] += n.multimesh.instance_count
	elif n is Light3D:
		st["lights"] += 1
	for c in n.get_children():
		_stat_rec(c, st)
