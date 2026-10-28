extends "res://bench/scene_base.gd"
## FOLIAGE LAB (built in code, deterministic, driven only through set_time()): open-world vegetation.
##
## Vegetation in open-world content is drawn with instancing: a MultiMesh per grass, bush, rock and tree type, either
## one large MultiMeshInstance3D per type or one per terrain chunk. Godot culls a MultiMeshInstance3D as a whole (its
## AABB against the camera frustum and each shadow cascade), so every instance of a visible MultiMesh is drawn in every
## pass that sees any part of it. The scene has both layouts (--foliage_layout=chunks, the default: grass in 16 m
## chunks that fade out with visibility ranges like a careful project; single: one MultiMesh per type over the whole
## map) on a 512 m terrain:
##   * terrain: a 2 m height-field grid (131 k triangles, clustered by FellowEngine's mesh clusters)
##   * grass: clumps of 8 blades (24 triangles), ~150 k instances inside the meadow
##   * bushes (~6 k, 160 triangles) and rocks (~4 k, 80 triangles)
##   * trees: trunk + three crown lobes (~420 triangles), ~6 k instances, mostly in a forest ring around the meadow
##   * sun with four PCSS cascades (200 m), sky light, SSAO, glow
## No vertex animation by default. --foliage_wind=1: grass, bushes and tree crowns sway in the wind (a vertex shader
## driven by TIME, the way vegetation shaders usually are), each MultiMeshInstance3D with an extra cull margin that
## bounds the displacement (decision 0036).

const SIZE := 512.0
const GRID := 2.0
const MEADOW := 150.0 # grass radius around the centre
const CHUNK := 16.0
const GRASS_PER_M2 := 2.2
const TREES := 6000
const BUSHES := 6000
const ROCKS := 4000

var sun: DirectionalLight3D
var layout := "chunks"
var wind := false
var _counts := {}
var _mm_parent: Node3D = null # where the vegetation goes: a FellowHLOD3D with --foliage_impostors=<distance>
var hlod: Node3D = null

# Wind sway: VERTEX moves by up to strength * 1.25^2 * sqrt(1 + 0.6^2) = 1.82 * strength (mesh space) above y0;
# the node's extra cull margin bounds it in the node's space (instance scales up to 1.25 / 1.3 / 1.4).
const WIND_SHADER := """
shader_type spatial;
render_mode cull_%s;
uniform vec3 albedo : source_color;
uniform float rough = 0.85;
uniform float strength = 0.1;
uniform float y0 = 0.0;
uniform float y1 = 1.0;
void vertex() {
	vec3 o = MODEL_MATRIX[3].xyz;
	float k = clamp((VERTEX.y - y0) / (y1 - y0), 0.0, 1.25);
	k *= k;
	float t = TIME * 1.3 + o.x * 0.11 + o.z * 0.07;
	VERTEX.x += sin(t) * strength * k;
	VERTEX.z += sin(t * 0.77 + 1.3) * strength * 0.6 * k;
}
void fragment() {
	ALBEDO = albedo;
	ROUGHNESS = rough;
}
"""
const WIND := {
	# name: [strength, y0, y1, margin]
	"grass": [0.06, 0.0, 0.6, 0.2],
	"bushes": [0.04, 0.0, 1.0, 0.15],
	"trees": [0.15, 3.0, 8.0, 0.5],
}


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "foliage"
	layout = str(args.get("foliage_layout", "chunks"))
	wind = str(args.get("foliage_wind", "0")) == "1"
	_setup_common()
	_setup_environment()
	_build_terrain()
	# --foliage_impostors=<distance> (FellowEngine, decision 0122): the vegetation under a FellowHLOD3D whose impostors
	# (baked at start) draw the far trees (meshes of at least --foliage_impostor_tris triangles, default 256: the trees);
	# --foliage_impostor_frames=<n> (8), --foliage_impostor_res=<px> (128).
	_mm_parent = self
	var imp_distance := float(args.get("foliage_impostors", "0"))
	if imp_distance > 0.0 and ClassDB.class_exists("FellowHLOD3D"):
		hlod = ClassDB.instantiate("FellowHLOD3D")
		hlod.name = "hlod"
		hlod.set("build_on_ready", false)
		hlod.set("impostor_distance", imp_distance)
		hlod.set("impostor_frames", int(args.get("foliage_impostor_frames", "8")))
		hlod.set("impostor_resolution", int(args.get("foliage_impostor_res", "128")))
		hlod.set("impostor_min_triangles", int(args.get("foliage_impostor_tris", "256")))
		add_child(hlod)
		_mm_parent = hlod
	_build_vegetation()
	_mm_parent = self
	_build_lights()
	_define_cameras()
	if hlod:
		hlod.call("build")
		while hlod.call("is_baking"):
			await get_tree().process_frame
	set_time(0.0)
	info = {"stats": scene_stats(self), "layout": layout, "counts": _counts, "lods": str(args.get("foliage_lods", "0")) == "1",
		"wind": wind, "notes": "Open-world vegetation with MultiMesh instancing (%s layout); sun with 4 cascades." % layout}
	if hlod:
		info["hlod"] = hlod.call("get_build_info")
		print("foliage hlod: ", info["hlod"])


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


static func height(x: float, z: float) -> float:
	return 4.0 * sin(x * 0.011 + 0.4) * cos(z * 0.009 - 0.7) + 1.6 * sin(x * 0.037 + 1.3) * sin(z * 0.031 + 0.2) + 0.5 * sin(x * 0.11 - z * 0.07)


static func normal_at(x: float, z: float) -> Vector3:
	var e := 0.5
	var dx := (height(x + e, z) - height(x - e, z)) / (2.0 * e)
	var dz := (height(x, z + e) - height(x, z - e)) / (2.0 * e)
	return Vector3(-dx, 1.0, -dz).normalized()


func _mat(c: Color, rough: float, two_sided := false, use_color := false) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = rough
	if two_sided:
		m.cull_mode = BaseMaterial3D.CULL_DISABLED
	if use_color:
		m.vertex_color_use_as_albedo = true
	return m


func _build_terrain() -> void:
	var n := int(SIZE / GRID)
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var idx := PackedInt32Array()
	verts.resize((n + 1) * (n + 1))
	norms.resize((n + 1) * (n + 1))
	var half := SIZE * 0.5
	for j in n + 1:
		for i in n + 1:
			var x := -half + i * GRID
			var z := -half + j * GRID
			verts[j * (n + 1) + i] = Vector3(x, height(x, z), z)
			norms[j * (n + 1) + i] = normal_at(x, z)
	idx.resize(n * n * 6)
	var k := 0
	for j in n:
		for i in n:
			var a := j * (n + 1) + i
			idx[k] = a
			idx[k + 1] = a + 1
			idx[k + 2] = a + n + 1
			idx[k + 3] = a + 1
			idx[k + 4] = a + n + 2
			idx[k + 5] = a + n + 1
			k += 6
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = verts
	arr[Mesh.ARRAY_NORMAL] = norms
	arr[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	mesh.surface_set_material(0, _mat(Color(0.32, 0.38, 0.2), 0.95))
	var mi := MeshInstance3D.new()
	mi.name = "terrain"
	mi.mesh = mesh
	add_child(mi)


func _grass_mesh() -> ArrayMesh:
	# Eight tapered blades around the clump's centre, 3 triangles each, normals tilted up (soft look).
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var idx := PackedInt32Array()
	for b in 8:
		var ang := TAU * float(b) / 8.0 + 0.35 * float(b % 3)
		var dir := Vector3(cos(ang), 0.0, sin(ang))
		var side := Vector3(-dir.z, 0.0, dir.x)
		var root := dir * (0.05 + 0.03 * float(b % 2))
		var h := 0.42 + 0.08 * float(b % 4)
		var lean := dir * (0.12 + 0.04 * float(b % 3))
		var w := 0.035
		var base := verts.size()
		verts.append_array(PackedVector3Array([root - side * w, root + side * w,
			root + lean * 0.45 + Vector3(0, h * 0.55, 0) - side * w * 0.7, root + lean * 0.45 + Vector3(0, h * 0.55, 0) + side * w * 0.7,
			root + lean + Vector3(0, h, 0)]))
		var nrm := (dir * 0.3 + Vector3.UP).normalized()
		for v in 5:
			norms.append(nrm)
		idx.append_array(PackedInt32Array([base, base + 1, base + 2, base + 1, base + 3, base + 2, base + 2, base + 3, base + 4]))
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = verts
	arr[Mesh.ARRAY_NORMAL] = norms
	arr[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	mesh.surface_set_material(0, _mat(Color(0.3, 0.42, 0.16), 0.85, true))
	return mesh


func _tree_mesh() -> ArrayMesh:
	var mesh := ArrayMesh.new()
	var trunk := CylinderMesh.new()
	trunk.top_radius = 0.18
	trunk.bottom_radius = 0.3
	trunk.height = 5.0
	trunk.radial_segments = 8
	trunk.rings = 1
	var st := SurfaceTool.new()
	st.append_from(trunk, 0, Transform3D(Basis(), Vector3(0, 2.5, 0)))
	st.set_material(_mat(Color(0.35, 0.25, 0.17), 0.9))
	st.commit(mesh)
	var crown := SphereMesh.new()
	crown.radius = 1.0
	crown.height = 2.0
	crown.radial_segments = 10
	crown.rings = 6
	var st2 := SurfaceTool.new()
	st2.append_from(crown, 0, Transform3D(Basis().scaled(Vector3(2.2, 1.8, 2.2)), Vector3(0, 5.2, 0)))
	st2.append_from(crown, 0, Transform3D(Basis().scaled(Vector3(1.7, 1.5, 1.7)), Vector3(0.5, 6.6, -0.3)))
	st2.append_from(crown, 0, Transform3D(Basis().scaled(Vector3(1.2, 1.2, 1.2)), Vector3(-0.3, 7.7, 0.2)))
	st2.set_material(_mat(Color(0.16, 0.3, 0.12), 0.85))
	st2.commit(mesh)
	return mesh


func _bush_mesh() -> ArrayMesh:
	var mesh := ArrayMesh.new()
	var s := SphereMesh.new()
	s.radius = 0.6
	s.height = 1.0
	s.radial_segments = 10
	s.rings = 5
	var st := SurfaceTool.new()
	st.append_from(s, 0, Transform3D(Basis(), Vector3(0, 0.4, 0)))
	st.append_from(s, 0, Transform3D(Basis().scaled(Vector3(0.8, 0.8, 0.8)), Vector3(0.45, 0.35, 0.2)))
	st.set_material(_mat(Color(0.2, 0.33, 0.14), 0.9))
	st.commit(mesh)
	return mesh


func _rock_mesh() -> ArrayMesh:
	var mesh := ArrayMesh.new()
	var s := SphereMesh.new()
	s.radius = 0.5
	s.height = 0.7
	s.radial_segments = 8
	s.rings = 5
	var st := SurfaceTool.new()
	st.append_from(s, 0, Transform3D(Basis().scaled(Vector3(1.3, 0.8, 1.0)), Vector3(0, 0.15, 0)))
	st.set_material(_mat(Color(0.45, 0.44, 0.42), 0.8))
	st.commit(mesh)
	return mesh


func _place(x: float, z: float, yaw: float, s: float, sink := 0.0) -> Transform3D:
	var n := normal_at(x, z)
	var b := Basis(Vector3.UP, yaw).scaled(Vector3(s, s, s))
	# Lean slightly with the slope.
	var tilt := Vector3.UP.cross(n)
	if tilt.length() > 1e-4:
		b = Basis(tilt.normalized(), Vector3.UP.angle_to(n) * 0.5) * b
	return Transform3D(b, Vector3(x, height(x, z) - sink, z))


## --foliage_wind=1: the mesh's surfaces get the wind shader with their StandardMaterial3D's colour, roughness and culling.
func _with_wind(p_mesh: ArrayMesh, p_type: String) -> ArrayMesh:
	if not wind or not WIND.has(p_type):
		return p_mesh
	var w: Array = WIND[p_type]
	for i in p_mesh.get_surface_count():
		var sm := p_mesh.surface_get_material(i) as StandardMaterial3D
		var sh := Shader.new()
		sh.code = WIND_SHADER % ("disabled" if sm.cull_mode == BaseMaterial3D.CULL_DISABLED else "back")
		var m := ShaderMaterial.new()
		m.shader = sh
		m.set_shader_parameter("albedo", Color(sm.albedo_color.r, sm.albedo_color.g, sm.albedo_color.b))
		m.set_shader_parameter("rough", sm.roughness)
		# --foliage_wind_strength=<scale> (default 1): stronger sway, e.g. to show what too small a margin loses.
		m.set_shader_parameter("strength", w[0] * float(args.get("foliage_wind_strength", "1")))
		m.set_shader_parameter("y0", w[1])
		m.set_shader_parameter("y1", w[2])
		p_mesh.surface_set_material(i, m)
	return p_mesh


## Instances (transform, colour) per type, then grouped into MultiMeshes by layout.
## --foliage_lods=1: the vegetation meshes get LOD levels the way the importer makes them for imported scenes
## (ImporterMesh.generate_lods, meshoptimizer), so that Godot's mesh LOD (1-pixel error rule) can coarsen them.
func _with_lods(p_mesh: ArrayMesh) -> ArrayMesh:
	if str(args.get("foliage_lods", "0")) != "1":
		return p_mesh
	var im := ImporterMesh.new()
	for i in p_mesh.get_surface_count():
		im.add_surface(p_mesh.surface_get_primitive_type(i), p_mesh.surface_get_arrays(i), [], {}, p_mesh.surface_get_material(i), "s%d" % i)
	im.generate_lods(25.0, 60.0, [])
	return im.get_mesh()


func _build_vegetation() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 2026
	var half := SIZE * 0.5
	# Grass: jittered grid inside the meadow.
	var grass: Array = []
	var step := 1.0 / sqrt(GRASS_PER_M2)
	var gx := -MEADOW
	while gx < MEADOW:
		var gz := -MEADOW
		while gz < MEADOW:
			var x := gx + rng.randf_range(0.0, step)
			var z := gz + rng.randf_range(0.0, step)
			if Vector2(x, z).length() < MEADOW - 4.0 * absf(sin(atan2(z, x) * 5.0)):
				grass.append([_place(x, z, rng.randf() * TAU, rng.randf_range(0.8, 1.25)), Color(1, 1, 1)])
			gz += step
		gx += step
	# Trees: a forest ring around the meadow plus scattered ones.
	var trees: Array = []
	while trees.size() < TREES:
		var r := sqrt(rng.randf()) * (half - 6.0)
		var a := rng.randf() * TAU
		var x := cos(a) * r
		var z := sin(a) * r
		var ring := r > MEADOW + 10.0
		if ring or rng.randf() < 0.04:
			trees.append([_place(x, z, rng.randf() * TAU, rng.randf_range(0.8, 1.3), 0.2), Color(1, 1, 1)])
	var bushes: Array = []
	while bushes.size() < BUSHES:
		var x := rng.randf_range(-half + 4.0, half - 4.0)
		var z := rng.randf_range(-half + 4.0, half - 4.0)
		bushes.append([_place(x, z, rng.randf() * TAU, rng.randf_range(0.7, 1.4), 0.1), Color(1, 1, 1)])
	var rocks: Array = []
	while rocks.size() < ROCKS:
		var x := rng.randf_range(-half + 4.0, half - 4.0)
		var z := rng.randf_range(-half + 4.0, half - 4.0)
		rocks.append([_place(x, z, rng.randf() * TAU, rng.randf_range(0.5, 2.0), 0.15), Color(1, 1, 1)])
	_counts = {"grass": grass.size(), "trees": trees.size(), "bushes": bushes.size(), "rocks": rocks.size()}
	var grass_mesh := _with_wind(_with_lods(_grass_mesh()), "grass")
	var tree_mesh := _with_wind(_with_lods(_tree_mesh()), "trees")
	var bush_mesh := _with_wind(_with_lods(_bush_mesh()), "bushes")
	var rock_mesh := _with_lods(_rock_mesh())
	if layout == "single":
		_add_multimesh("grass", grass_mesh, grass, false, 0.0)
		_add_multimesh("trees", tree_mesh, trees, false, 0.0)
		_add_multimesh("bushes", bush_mesh, bushes, false, 0.0)
		_add_multimesh("rocks", rock_mesh, rocks, false, 0.0)
	else:
		# Grass in 16 m chunks with a visibility range (fades out by distance, as projects usually do); trees, bushes and
		# rocks in 64 m blocks.
		_add_chunked("grass", grass_mesh, grass, CHUNK, false, 90.0)
		_add_chunked("trees", tree_mesh, trees, 64.0, false, 0.0)
		_add_chunked("bushes", bush_mesh, bushes, 64.0, false, 0.0)
		_add_chunked("rocks", rock_mesh, rocks, 64.0, false, 0.0)


func _add_chunked(p_name: String, p_mesh: Mesh, p_items: Array, p_chunk: float, p_colors: bool, p_range: float) -> void:
	var cells := {}
	for it in p_items:
		var o: Vector3 = (it[0] as Transform3D).origin
		var key := Vector2i(int(floor(o.x / p_chunk)), int(floor(o.z / p_chunk)))
		if not cells.has(key):
			cells[key] = []
		cells[key].append(it)
	var keys := cells.keys()
	keys.sort()
	for key in keys:
		_add_multimesh("%s_%d_%d" % [p_name, key.x, key.y], p_mesh, cells[key], p_colors, p_range)


func _add_multimesh(p_name: String, p_mesh: Mesh, p_items: Array, p_colors: bool, p_range: float) -> void:
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_colors = p_colors
	mm.mesh = p_mesh
	mm.instance_count = p_items.size()
	var stride := 12 + (4 if p_colors else 0)
	var buf := PackedFloat32Array()
	buf.resize(p_items.size() * stride)
	var k := 0
	for it in p_items:
		var t: Transform3D = it[0]
		buf[k + 0] = t.basis.x.x
		buf[k + 1] = t.basis.y.x
		buf[k + 2] = t.basis.z.x
		buf[k + 3] = t.origin.x
		buf[k + 4] = t.basis.x.y
		buf[k + 5] = t.basis.y.y
		buf[k + 6] = t.basis.z.y
		buf[k + 7] = t.origin.y
		buf[k + 8] = t.basis.x.z
		buf[k + 9] = t.basis.y.z
		buf[k + 10] = t.basis.z.z
		buf[k + 11] = t.origin.z
		if p_colors:
			var c: Color = it[1]
			buf[k + 12] = c.r
			buf[k + 13] = c.g
			buf[k + 14] = c.b
			buf[k + 15] = c.a
		k += stride
	mm.buffer = buf
	var mmi := MultiMeshInstance3D.new()
	mmi.name = p_name
	mmi.multimesh = mm
	var type := p_name.get_slice("_", 0)
	if wind and WIND.has(type):
		# --foliage_wind_margin=<scale> (default 1): the margins scaled, e.g. to show that culling with too small a margin
		# loses swaying instances at the edges of the view.
		mmi.extra_cull_margin = WIND[type][3] * float(args.get("foliage_wind_margin", "1"))
	if p_range > 0.0:
		mmi.visibility_range_end = p_range
		mmi.visibility_range_end_margin = 10.0
		mmi.visibility_range_fade_mode = GeometryInstance3D.VISIBILITY_RANGE_FADE_DISABLED
	_mm_parent.add_child(mmi)


func _build_lights() -> void:
	sun = DirectionalLight3D.new()
	sun.name = "sun"
	sun.light_energy = 2.0
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 200.0
	sun.light_angular_distance = 0.5
	apply_sun_overrides(sun)
	add_child(sun)
	sun.rotation_degrees = Vector3(-42.0, -30.0, 0.0)


func _eye(x: float, z: float, up := 1.7) -> Vector3:
	return Vector3(x, height(x, z) + up, z)


func _define_cameras() -> void:
	fixed_cameras = {
		"foliage_meadow": {"pos": _eye(-60, 40), "target": _eye(20, -10, 1.0), "fov": 70.0, "time": 1.0, "purpose": "eye level in the meadow: dense grass to the forest edge"},
		"foliage_forest": {"pos": _eye(175, 30), "target": _eye(220, 10, 2.0), "fov": 70.0, "time": 1.0, "purpose": "inside the forest ring: trees, bushes, cascaded shadows"},
		"foliage_aerial": {"pos": Vector3(-200, 90, 200), "target": Vector3(0, 0, 0), "fov": 60.0, "time": 1.0, "purpose": "aerial view over the whole map"},
	}
	flythrough = CameraPath.from_array([
		[0.0, _eye(-120, 90), _eye(-60, 40, 1.2), 70.0],
		[8.0, _eye(-50, 30), _eye(10, -20, 1.2), 70.0],
		[16.0, _eye(30, -10), _eye(120, -60, 1.5), 72.0],
		[24.0, _eye(140, -40, 2.5), _eye(200, -20, 3.0), 72.0],
		[30.0, Vector3(150, 40, 60), Vector3(0, 0, 0), 60.0],
	])
	temporal_seqs = {
		"foliage_walk": {"start": 1.0, "frames": 60, "purpose": "walk through the meadow, turning",
			"path": CameraPath.from_array([[0.0, _eye(-40, 20), _eye(10, -10, 1.0), 70.0], [1.0, _eye(-30, 14), _eye(30, 10, 1.0), 70.0]])},
	}
