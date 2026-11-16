extends SceneTree
## FellowEngine milestone 125 check (decision 0249): FellowHair3D's scalp from a mesh and its level of detail.
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 960x540 -s res://bench/hair_lod_check.gd -- --out=<dir>
##
## * The scalp from a mesh: an egg-shaped head (an ellipsoid of 0.085 x 0.11 x 0.1 m) whose vertices are painted red
##   above the ears and off the face; the roots must lie on its surface (the ellipsoid's equation within the mesh's
##   faceting), on its painted part only (the faces' normals: none on the face, none under the ears).
## * Level of detail: the camera at 1, 4, 8, 16, 32 and 60 m: the strands drawn (all within lod_distance, then by the
##   square of the distance; through a 75-degree view), their widths' scale, the simulation stopped beyond
##   simulation_distance; the GPU's time
##   (lavapipe: relative only) with and without the level of detail; pictures at 8 m with and without it.
## One JSON line: HAIR_LOD_CHECK {...}; exit code 1 when a check fails.

var args := {}
var out_dir := ""
var result := {}
var root3d: Node3D
var head: Node3D
var hair: FellowHair3D
var cam: Camera3D
var frame := 0
var done := false
var steps := []
var step := -1
var step_frame := 0
var lod_rows := []
var gpu_ms := []
const RX := 0.085
const RY := 0.11
const RZ := 0.1


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://hair_lod_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	_build.call_deferred()


# An ellipsoid's mesh, its vertices painted red where hair grows: above the ears (normal's y over -0.25) and off the
# face (the normal not within 50 degrees of -Z).
func _head_mesh() -> ArrayMesh:
	var sphere := SphereMesh.new()
	sphere.radius = 1.0
	sphere.height = 2.0
	sphere.radial_segments = 48
	sphere.rings = 24
	var arrays := sphere.get_mesh_arrays()
	var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
	var colors := PackedColorArray()
	for i in verts.size():
		var v := verts[i]
		var n := normals[i]
		verts[i] = Vector3(v.x * RX, v.y * RY, v.z * RZ)
		normals[i] = Vector3(n.x / RX, n.y / RY, n.z / RZ).normalized()
		var painted := normals[i].y > -0.25 and normals[i].dot(Vector3(0, 0, -1)) < cos(deg_to_rad(50.0))
		colors.push_back(Color(1, 0, 0) if painted else Color(0, 0, 0))
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_COLOR] = colors
	arrays[Mesh.ARRAY_TANGENT] = null
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


func _build() -> void:
	root3d = Node3D.new()
	get_root().add_child(root3d)
	get_root().use_taa = true
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.42, 0.47, 0.55)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.55, 0.58, 0.65)
	e.ambient_light_energy = 0.5
	e.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.environment = e
	root3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-35, 150, 0)
	sun.light_energy = 2.0
	sun.shadow_enabled = true
	root3d.add_child(sun)
	head = Node3D.new()
	head.position = Vector3(0, 1.6, 0)
	root3d.add_child(head)
	var mesh := _head_mesh()
	var skull := MeshInstance3D.new()
	skull.mesh = mesh
	var mat := ShaderMaterial.new()
	mat.shader = Shader.new()
	mat.shader.code = "shader_type spatial;\nvoid fragment() { ALBEDO = mix(vec3(0.78, 0.6, 0.5), vec3(0.06, 0.04, 0.03), COLOR.r); ROUGHNESS = 0.6; }"
	skull.set_surface_override_material(0, mat)
	head.add_child(skull)
	hair = FellowHair3D.new()
	hair.scalp_mesh = mesh
	hair.scalp_radius = 0.095 # the head's collider: a sphere inside the egg
	hair.length = 0.3
	hair.strand_count = 10000
	head.add_child(hair)
	cam = Camera3D.new()
	cam.fov = 35
	root3d.add_child(cam)
	cam.current = true
	RenderingServer.viewport_set_measure_render_time(get_root().get_viewport_rid(), true)
	for d in [1.0, 4.0, 8.0, 16.0, 32.0, 60.0]:
		steps.append({"distance": d, "lod": true})
	steps.append({"distance": 8.0, "lod": false})
	steps.append({"distance": 16.0, "lod": false})
	physics_frame.connect(_on_frame)


func _roots_check() -> Dictionary:
	# The guides' roots (their first points, the styled shape where the head is) on the egg, on its painted part.
	var worst := 0.0
	var on_face := 0
	var under_ears := 0
	var inv := head.global_transform.affine_inverse()
	for gi in hair.get_stats().guides:
		var r: Vector3 = inv * hair.get_guide_rest_points(gi)[0]
		var f := sqrt((r.x / RX) ** 2 + (r.y / RY) ** 2 + (r.z / RZ) ** 2)
		worst = maxf(worst, absf(f - 1.0))
		var n := Vector3(r.x / (RX * RX), r.y / (RY * RY), r.z / (RZ * RZ)).normalized()
		if n.dot(Vector3(0, 0, -1)) > cos(deg_to_rad(46.0)):
			on_face += 1
		if n.y < -0.3:
			under_ears += 1
	return {"guides": hair.get_stats().guides, "worst_off_surface": snappedf(worst, 0.0001), "on_face": on_face, "under_ears": under_ears}


func _on_frame() -> void:
	if done:
		return
	frame += 1
	if frame == 5:
		result["roots"] = _roots_check()
		cam.look_at_from_position(Vector3(0.35, 1.75, -0.9), Vector3(0, 1.6, 0))
	if frame == 40:
		get_root().get_texture().get_image().save_png(out_dir.path_join("hair_mesh_scalp.png"))
		step = 0
		step_frame = frame
		_apply_step()
	if step >= 0 and frame > step_frame + 10:
		# 10 frames at a distance; the last 5 frames' GPU time.
		pass
	if step >= 0 and frame >= step_frame + 5 and frame < step_frame + 10:
		gpu_ms.append(RenderingServer.viewport_get_measured_render_time_gpu(get_root().get_viewport_rid()))
	if step >= 0 and frame == step_frame + 10:
		var st := hair.get_stats()
		var s: Dictionary = steps[step]
		gpu_ms.sort()
		lod_rows.append({"distance": s.distance, "camera_distance": snappedf(cam.global_position.distance_to(hair.global_position), 0.001), "lod": s.lod, "visible_strands": st.visible_strands, "width_scale": snappedf(st.width_scale, 0.01),
			"simulated": st.simulated, "gpu_ms": snappedf(gpu_ms[gpu_ms.size() / 2], 0.01)})
		gpu_ms.clear()
		if s.distance == 8.0:
			get_root().get_texture().get_image().save_png(out_dir.path_join("hair_8m_%s.png" % ("lod" if s.lod else "all")))
		step += 1
		step_frame = frame
		if step >= steps.size():
			done = true
			_finish.call_deferred()
		else:
			_apply_step()


func _apply_step() -> void:
	var s: Dictionary = steps[step]
	hair.lod_distance = 4.0 if s.lod else 0.0
	# The camera in front of the face at the distance, Godot's default 75-degree view (the level of detail's reference).
	cam.fov = 75.0
	cam.look_at_from_position(Vector3(0.2 * s.distance, 1.6 + 0.05 * s.distance, -s.distance), Vector3(0, 1.55, 0))


func _finish() -> void:
	result["lod"] = lod_rows
	var failures := []
	var roots: Dictionary = result.get("roots", {})
	if roots.get("worst_off_surface", 1.0) > 0.03 or roots.get("on_face", 1) > 0 or roots.get("under_ears", 1) > 0 or roots.get("guides", 0) < 100:
		failures.append("roots")
	for row in lod_rows:
		var total := hair.strand_count
		var d: float = row.camera_distance
		var expected := total if (not row.lod or d <= 4.0) else maxi(1, roundi(total * maxf(0.05, 16.0 / (d * d))))
		if absi(row.visible_strands - expected) > 2: # (the distance rounded to a millimetre here)
			failures.append("strands at %s m" % row.distance)
		if row.simulated != (d <= 40.0):
			failures.append("simulation at %s m" % row.distance)
	result["failures"] = failures
	print("HAIR_LOD_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("hair_lod_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)
