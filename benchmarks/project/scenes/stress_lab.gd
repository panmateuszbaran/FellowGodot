extends "res://bench/scene_base.gd"
## ROBUSTNESS SCENE - Stress Lab (built in code, deterministic, driven only through set_time()).
##
## Not a performance benchmark: it puts every situation the default-on FellowEngine features must handle exactly
## like stock into one frame, so that stock and FellowEngine (with its exact features on) can be compared image by
## image:
##   * large static surfaces (mesh clusters: floor 80 k, height field 180 k triangles)
##   * a large alpha-blended surface (order-preserving clusters) and a large surface whose shader moves vertices
##     (clusters must not be used)
##   * a procedural mesh whose positions are rewritten every frame with mesh_surface_update_vertex_region()
##     (clusters must be dropped)
##   * a large mesh moving through the scene (clusters on a moving instance, dynamic shadow caster)
##   * a MultiMesh of moving boxes (dynamic casters, instanced draws)
##   * a sun that rotates for part of the timeline (shadow cache must not be used while it moves)
##   * omni and spot shadows, a reflection probe updated every frame, and a second viewport (SubViewport with its
##     own camera, shown on a screen in the scene: a second set of shadow caches and render buffers)

var floor_mesh: MeshInstance3D
var deform_mesh: ArrayMesh
var deform_n := 160
var deform_base := PackedVector3Array()
var mover: MeshInstance3D
var boxes: MultiMeshInstance3D
var sun: DirectionalLight3D
var sub_camera: Camera3D


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "stress"
	_setup_common()
	_setup_environment()
	_build_static()
	_build_dynamic()
	_build_lights()
	_build_second_view()
	_define_cameras()
	set_time(0.0)
	info = {"stats": scene_stats(self), "notes": "Robustness scene for default-on FellowEngine features; not a performance benchmark."}


func _setup_environment() -> void:
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var proc := ProceduralSkyMaterial.new()
	sky.sky_material = proc
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.ssao_enabled = true
	env.glow_enabled = true
	cam_attr.auto_exposure_enabled = false


func _mat(albedo: Color, roughness := 0.6, metallic := 0.0) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = albedo
	m.roughness = roughness
	m.metallic = metallic
	return m


func _grid_arrays(n: int, size: float, height: Callable) -> Array:
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	verts.resize(n * n)
	normals.resize(n * n)
	for y in n:
		for x in n:
			var px := (float(x) / (n - 1) - 0.5) * size
			var pz := (float(y) / (n - 1) - 0.5) * size
			verts[y * n + x] = Vector3(px, height.call(px, pz), pz)
			normals[y * n + x] = Vector3.UP
	var idx := PackedInt32Array()
	idx.resize((n - 1) * (n - 1) * 6)
	var k := 0
	for y in n - 1:
		for x in n - 1:
			var a := y * n + x
			idx[k] = a; idx[k + 1] = a + 1; idx[k + 2] = a + n
			idx[k + 3] = a + 1; idx[k + 4] = a + n + 1; idx[k + 5] = a + n
			k += 6
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = verts
	arr[Mesh.ARRAY_NORMAL] = normals
	arr[Mesh.ARRAY_INDEX] = idx
	return arr


func _add_mesh(mesh_name: String, mesh: Mesh, xf: Transform3D, mat: Material) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.name = mesh_name
	mi.mesh = mesh
	mi.transform = xf
	if mat:
		mi.material_override = mat
	add_child(mi)
	return mi


func _build_static() -> void:
	# Floor: 201 x 201 vertices = 80 k triangles (clustered).
	var floor_am := ArrayMesh.new()
	floor_am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, _grid_arrays(201, 60.0, func(_x, _z): return 0.0))
	floor_mesh = _add_mesh("floor", floor_am, Transform3D.IDENTITY, _mat(Color(0.55, 0.55, 0.5), 0.8))
	# Height field: 301 x 301 vertices = 180 k triangles (clustered), hills casting shadows on each other.
	var hf := ArrayMesh.new()
	hf.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, _grid_arrays(301, 24.0, func(x, z): return 1.5 + 1.2 * sin(x * 0.6) * cos(z * 0.45)))
	_add_mesh("heightfield", hf, Transform3D(Basis.IDENTITY, Vector3(-14, 0, -14)), _mat(Color(0.35, 0.5, 0.3), 0.9))
	# Large alpha-blended surface (order-preserving clusters): a 45 k-triangle wavy curtain.
	var curtain := ArrayMesh.new()
	curtain.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, _grid_arrays(151, 8.0, func(x, z): return 0.25 * sin(x * 3.0) + 0.15 * cos(z * 5.0)))
	var glass := _mat(Color(0.6, 0.8, 1.0, 0.45), 0.1)
	glass.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	glass.cull_mode = BaseMaterial3D.CULL_DISABLED
	_add_mesh("blended_curtain", curtain, Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3(6, 4, -6)), glass)
	# Large surface whose shader moves vertices (clusters must not be used for it).
	var wavy := ArrayMesh.new()
	wavy.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, _grid_arrays(151, 8.0, func(_x, _z): return 0.0))
	var sm := ShaderMaterial.new()
	var sh := Shader.new()
	sh.code = """shader_type spatial;
uniform float phase = 0.0;
void vertex() {
	VERTEX.y += 0.6 * sin(VERTEX.x * 1.3 + phase) * cos(VERTEX.z * 0.9 + phase);
}
void fragment() {
	ALBEDO = vec3(0.7, 0.45, 0.3);
	ROUGHNESS = 0.5;
}
"""
	sm.shader = sh
	_add_mesh("vertex_shader_sheet", wavy, Transform3D(Basis.IDENTITY, Vector3(-6, 0.8, 8)), sm)


func _build_dynamic() -> void:
	# Procedural mesh rewritten every frame through mesh_surface_update_vertex_region (45 k triangles: clustered
	# at creation, clusters dropped at the first update).
	deform_mesh = ArrayMesh.new()
	var arr := _grid_arrays(deform_n, 6.0, func(_x, _z): return 0.0)
	deform_base = arr[Mesh.ARRAY_VERTEX]
	deform_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	_add_mesh("deformed_cpu", deform_mesh, Transform3D(Basis.IDENTITY, Vector3(6, 0.6, 7)), _mat(Color(0.8, 0.3, 0.3), 0.4))
	# Large moving mesh (65 k triangles).
	var sphere := SphereMesh.new()
	sphere.radial_segments = 256
	sphere.rings = 128
	sphere.radius = 1.6
	sphere.height = 3.2
	mover = _add_mesh("moving_sphere", sphere, Transform3D.IDENTITY, _mat(Color(0.9, 0.85, 0.7), 0.3, 0.6))
	# MultiMesh of moving boxes.
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.mesh = BoxMesh.new()
	mm.instance_count = 400
	boxes = MultiMeshInstance3D.new()
	boxes.name = "boxes"
	boxes.multimesh = mm
	boxes.material_override = _mat(Color(0.3, 0.4, 0.8), 0.5)
	add_child(boxes)


func _build_lights() -> void:
	sun = DirectionalLight3D.new()
	sun.name = "sun"
	sun.light_energy = 2.0
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 80.0
	sun.directional_shadow_blend_splits = true
	sun.light_angular_distance = 0.5
	apply_sun_overrides(sun)
	add_child(sun)
	var omni := OmniLight3D.new()
	omni.position = Vector3(2, 2, 2)
	omni.omni_range = 10.0
	omni.light_energy = 3.0
	omni.light_color = Color(1.0, 0.75, 0.5)
	omni.shadow_enabled = true
	add_child(omni)
	var spot := SpotLight3D.new()
	spot.spot_range = 14.0
	spot.spot_angle = 35.0
	spot.light_energy = 8.0
	spot.shadow_enabled = true
	add_child(spot)
	spot.look_at_from_position(Vector3(-4, 6, 4), Vector3(-2, 0, 0), Vector3.UP)
	var probe := ReflectionProbe.new()
	probe.update_mode = ReflectionProbe.UPDATE_ALWAYS
	probe.size = Vector3(12, 6, 12)
	probe.position = Vector3(2, 3, 2)
	add_child(probe)


func _build_second_view() -> void:
	var sv := SubViewport.new()
	sv.name = "second_view"
	sv.size = Vector2i(512, 288)
	sv.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(sv)
	sub_camera = Camera3D.new()
	sub_camera.near = 0.05
	sub_camera.far = 400.0
	sv.add_child(sub_camera)
	sub_camera.look_at_from_position(Vector3(-12, 7, 12), Vector3(0, 0, 0), Vector3.UP)
	var screen := MeshInstance3D.new()
	var q := QuadMesh.new()
	q.size = Vector2(4.0, 2.25)
	screen.mesh = q
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.albedo_texture = sv.get_texture()
	screen.material_override = m
	screen.position = Vector3(0, 2.2, -8)
	add_child(screen)


func _define_cameras() -> void:
	fixed_cameras = {
		"stress_overview": {"pos": Vector3(0, 9, 18), "target": Vector3(0, 0, 0), "fov": 60.0, "time": 1.0, "purpose": "everything, sun still"},
		"stress_low": {"pos": Vector3(3, 1.5, 10), "target": Vector3(0, 1, -4), "fov": 60.0, "time": 1.0, "purpose": "floor/height field clusters at grazing view, camera inside large AABBs"},
		"stress_sun_moving": {"pos": Vector3(0, 9, 18), "target": Vector3(0, 0, 0), "fov": 60.0, "time": 3.0, "purpose": "sun rotating (cache must step aside)"},
		"stress_screen": {"pos": Vector3(0, 2.2, -4.5), "target": Vector3(0, 2.2, -8), "fov": 60.0, "time": 1.0, "purpose": "second viewport on the screen"},
	}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(0, 9, 18), Vector3(0, 0, 0), 60.0],
		[5.0, Vector3(14, 4, 6), Vector3(0, 1, -2), 60.0],
		[10.0, Vector3(3, 1.5, 10), Vector3(0, 1, -4), 60.0],
		[15.0, Vector3(-10, 6, -10), Vector3(0, 0, 0), 60.0],
		[20.0, Vector3(0, 9, 18), Vector3(0, 0, 0), 60.0],
	])
	temporal_seqs = {
		"stress_pan": {"start": 0.5, "frames": 60, "purpose": "pan across the dynamic objects, sun still",
			"path": CameraPath.from_array([[0.0, Vector3(0, 5, 14), Vector3(-6, 0.5, 0), 60.0], [1.0, Vector3(0, 5, 14), Vector3(6, 0.5, 0), 60.0]])},
		"stress_sun": {"start": 2.2, "frames": 60, "purpose": "static camera while the sun rotates",
			"path": CameraPath.from_array([[0.0, Vector3(0, 9, 18), Vector3(0, 0, 0), 60.0], [1.0, Vector3(0, 9, 18), Vector3(0, 0, 0), 60.0]])},
	}


func set_time(t: float) -> void:
	# Sun: still until t = 2, rotates from t = 2 to 4, still afterwards.
	var u := clampf((t - 2.0) / 2.0, 0.0, 1.0)
	if sun:
		sun.rotation_degrees = Vector3(-40.0 - 15.0 * u, -35.0 + 50.0 * u, 0.0)
	if mover:
		mover.position = Vector3(8.0 * sin(t * 0.7), 2.2 + 0.5 * sin(t * 1.3), 8.0 * cos(t * 0.7) - 2.0)
	if boxes:
		var mm := boxes.multimesh
		for i in mm.instance_count:
			var a := float(i) * 0.37 + t * 0.5
			var r := 4.0 + float(i % 20) * 0.6
			var p := Vector3(r * cos(a), 0.5 + 0.4 * sin(t * 2.0 + float(i)), r * sin(a))
			mm.set_instance_transform(i, Transform3D(Basis(Vector3.UP, a), p).scaled_local(Vector3(0.4, 0.4, 0.4)))
	if deform_mesh:
		var data := PackedByteArray()
		data.resize(deform_base.size() * 12)
		for i in deform_base.size():
			var v := deform_base[i]
			data.encode_float(i * 12, v.x)
			data.encode_float(i * 12 + 4, 0.4 * sin(v.x * 2.0 + t * 3.0) * cos(v.z * 2.0))
			data.encode_float(i * 12 + 8, v.z)
		RenderingServer.mesh_surface_update_vertex_region(deform_mesh.get_rid(), 0, 0, data)
	var sheet := get_node_or_null("vertex_shader_sheet") as MeshInstance3D
	if sheet:
		(sheet.material_override as ShaderMaterial).set_shader_parameter("phase", t * 2.0)
