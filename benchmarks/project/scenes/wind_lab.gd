extends "res://bench/scene_base.gd"
## BENCHMARK - Wind Lab (milestone 26): a meadow at the edge of a forest in the wind (FellowEngine FellowWind3D: the
## grass, the bushes and the trees' trunks, branches and leaves moved in the vertex shader by the wind and its gusts,
## which roll over the meadow in waves). Stock Godot has no wind: --wind=off is the scene still, as stock draws it.
##
## A 240 m square of gentle hills: a meadow of grass clumps (blades as tapered strips) in the middle, a forest of
## broadleaf trees (a tapering trunk, branches, leaf cards with an alpha-scissor texture of leaves) around it, bushes
## along the edge, a few rocks; sun with shadows, sky light.
## Options:
##   --wind=on|off|shader   on: a FellowWind3D and the materials' Wind group (default); off: no wind; shader: the way
##                          projects sway vegetation without FellowEngine (a ShaderMaterial driven by TIME, no gusts)
##   --wind_speed=<m/s> (8), --wind_gustiness=<0..1> (0.6), --wind_gust_size=<m> (18), --wind_grass_bend=<x> (1.0),
##   --wind_grass_sheen=<x> (1.0), --wind_grass=<clumps per m²> (4.5)
## Extra per-frame metrics: none.

const SIZE := 240.0
const GRID := 2.0
const MEADOW := 55.0 # the meadow's radius
const GRASS_PER_M2 := 4.5
const TREES := 700
const BUSHES := 500

var mode := "on"
var wind  # FellowWind3D
var sun: DirectionalLight3D
var _rng := RandomNumberGenerator.new()
var _counts := {}

# --wind=shader: how vegetation sways without FellowEngine (TIME, a phase per plant; no gusts, no motion of the
# branches against the trunk).
const SWAY_SHADER := """
shader_type spatial;
render_mode cull_disabled;
uniform vec3 albedo : source_color;
uniform sampler2D albedo_tex : source_color, filter_linear_mipmap;
uniform bool use_tex = false;
uniform float rough = 0.85;
uniform float height = 1.0;
uniform float strength = 0.1;
void vertex() {
	vec3 o = MODEL_MATRIX[3].xyz;
	float k = clamp(VERTEX.y / height, 0.0, 1.0);
	k *= k;
	float t = TIME * 1.3 + o.x * 0.11 + o.z * 0.07;
	VERTEX.x += sin(t) * strength * height * k;
	VERTEX.z += sin(t * 0.77 + 1.3) * strength * height * 0.6 * k;
}
void fragment() {
	vec4 c = use_tex ? texture(albedo_tex, UV) : vec4(1.0);
	ALBEDO = albedo * c.rgb;
	ROUGHNESS = rough;
	if (use_tex) {
		ALPHA = c.a;
		ALPHA_SCISSOR_THRESHOLD = 0.5;
	}
}
"""


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "wind"
	mode = str(args.get("wind", "on"))
	if mode == "on" and not ClassDB.class_exists("FellowWind3D"):
		load_error = "unsupported: FellowWind3D not available (stock Godot has no wind)"
		return
	_rng.seed = 2610
	_setup_common()
	camera.far = 800.0
	_setup_environment()
	_build_terrain()
	_build_vegetation()
	if mode == "on":
		wind = ClassDB.instantiate("FellowWind3D")
		wind.name = "Wind"
		wind.speed = float(args.get("wind_speed", "8"))
		wind.gustiness = float(args.get("wind_gustiness", "0.6"))
		wind.gust_size = float(args.get("wind_gust_size", "18"))
		# Blowing towards +Z and a little +X (the node's forward axis): across the meadow camera's view.
		wind.basis = Basis.looking_at(Vector3(0.35, 0.0, 1.0).normalized(), Vector3.UP)
		wind.time_auto = false # driven by set_time(): the same pose in every run
		add_child(wind)
		# --wind_leaves=<amount> (decision 0127): leaves falling from the forest's edge and blown over the meadow, GPU
		# particles with the Wind group (they drift with the wind and its gusts).
		if int(args.get("wind_leaves", "0")) > 0:
			_build_leaves(int(args["wind_leaves"]))
	_define_cameras()
	set_time(0.0)
	info = {"mode": mode, "counts": _counts, "stats": scene_stats(self)}


func _setup_environment() -> void:
	var sky := Sky.new()
	var sm := ProceduralSkyMaterial.new()
	sm.sky_top_color = Color(0.32, 0.46, 0.68)
	sm.sky_horizon_color = Color(0.66, 0.70, 0.74)
	sky.sky_material = sm
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.ssao_enabled = true
	env.glow_enabled = true
	env.fog_enabled = true
	env.fog_density = 0.002
	env.fog_light_color = Color(0.7, 0.75, 0.82)
	env.fog_sky_affect = 0.2
	cam_attr.auto_exposure_enabled = false
	sun = DirectionalLight3D.new()
	sun.name = "sun"
	sun.light_energy = 1.6
	sun.light_color = Color(1.0, 0.96, 0.88)
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 120.0
	sun.light_angular_distance = 0.5
	add_child(sun)
	sun.rotation_degrees = Vector3(-38.0, -145.0, 0.0)
	apply_sun_overrides(sun)


static func height(x: float, z: float) -> float:
	return 2.5 * sin(x * 0.017 + 0.3) * cos(z * 0.014 - 0.5) + 0.8 * sin(x * 0.051 + 1.1) * sin(z * 0.043 + 0.2)


static func normal_at(x: float, z: float) -> Vector3:
	var e := 0.5
	var dx := (height(x + e, z) - height(x - e, z)) / (2.0 * e)
	var dz := (height(x, z + e) - height(x, z - e)) / (2.0 * e)
	return Vector3(-dx, 1.0, -dz).normalized()


func _build_terrain() -> void:
	var n := int(SIZE / GRID)
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var cols := PackedColorArray()
	var idx := PackedInt32Array()
	verts.resize((n + 1) * (n + 1))
	norms.resize((n + 1) * (n + 1))
	cols.resize((n + 1) * (n + 1))
	var half := SIZE * 0.5
	for j in n + 1:
		for i in n + 1:
			var x := -half + i * GRID
			var z := -half + j * GRID
			verts[j * (n + 1) + i] = Vector3(x, height(x, z), z)
			norms[j * (n + 1) + i] = normal_at(x, z)
			# The meadow's floor green under the grass, the forest's brown with leaf litter.
			var m := clampf((MEADOW + 4.0 - Vector2(x, z).length()) / 8.0, 0.0, 1.0)
			cols[j * (n + 1) + i] = Color(0.26, 0.22, 0.13).lerp(Color(0.17, 0.26, 0.08), m)
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
	arr[Mesh.ARRAY_COLOR] = cols
	arr[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	var m := StandardMaterial3D.new()
	m.vertex_color_use_as_albedo = true
	m.vertex_color_is_srgb = true
	m.roughness = 0.95
	mesh.surface_set_material(0, m)
	var mi := MeshInstance3D.new()
	mi.name = "terrain"
	mi.mesh = mesh
	add_child(mi)


## A texture of a cluster of leaves (alpha: the leaves), for the trees' and bushes' leaf cards.
func _leaf_texture() -> ImageTexture:
	var n := 128
	var img := Image.create_empty(n, n, true, Image.FORMAT_RGBA8)
	img.fill(Color(0.2, 0.35, 0.12, 0.0))
	var rng := RandomNumberGenerator.new()
	rng.seed = 77
	for l in 26:
		var c := Vector2(rng.randf_range(0.15, 0.85), rng.randf_range(0.15, 0.85)) * n
		var ang := rng.randf() * TAU
		var ax := Vector2(cos(ang), sin(ang))
		var len := rng.randf_range(0.1, 0.17) * n
		var wid := len * 0.42
		var shade := rng.randf_range(0.75, 1.15)
		var x0 := int(maxf(c.x - len, 0))
		var x1 := int(minf(c.x + len, n - 1))
		var y0 := int(maxf(c.y - len, 0))
		var y1 := int(minf(c.y + len, n - 1))
		for y in range(y0, y1 + 1):
			for x in range(x0, x1 + 1):
				var d := Vector2(x + 0.5, y + 0.5) - c
				var u := d.dot(ax) / len
				var v := d.dot(Vector2(-ax.y, ax.x)) / wid
				if u * u + v * v < 1.0:
					var vein := 1.0 - 0.15 * exp(-v * v * 60.0)
					img.set_pixel(x, y, Color(0.20 * shade * vein, 0.36 * shade * vein, 0.11 * shade, 1.0))
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)


func _mat(c: Color, rough: float, two_sided: bool, tex: Texture2D, wind_h: float, wind_bend: float, wind_flutter: float, wind_sheen := 0.0) -> Material:
	if mode == "shader":
		var sh := Shader.new()
		sh.code = SWAY_SHADER
		var sm := ShaderMaterial.new()
		sm.shader = sh
		sm.set_shader_parameter("albedo", Color(c.r, c.g, c.b))
		sm.set_shader_parameter("rough", rough)
		sm.set_shader_parameter("height", wind_h)
		sm.set_shader_parameter("strength", minf(wind_bend * 0.25, 0.15))
		if tex:
			sm.set_shader_parameter("albedo_tex", tex)
			sm.set_shader_parameter("use_tex", true)
		return sm
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = rough
	if two_sided:
		m.cull_mode = BaseMaterial3D.CULL_DISABLED
	if tex:
		m.albedo_texture = tex
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
		m.alpha_scissor_threshold = 0.5
		m.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
	if mode == "on":
		m.set("wind", true)
		m.set("wind_height", wind_h)
		m.set("wind_bend", wind_bend)
		m.set("wind_flutter", wind_flutter)
		m.set("wind_sheen", wind_sheen)
	return m


func _grass_mesh() -> ArrayMesh:
	# Sixteen blades around the clump's centre, each a strip of 3 segments, tapering, leaning out.
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var idx := PackedInt32Array()
	var rng := RandomNumberGenerator.new()
	rng.seed = 5
	for b in 16:
		var ang := TAU * float(b) / 16.0 + rng.randf_range(-0.3, 0.3)
		var dir := Vector3(cos(ang), 0.0, sin(ang))
		var side := Vector3(-dir.z, 0.0, dir.x)
		var root := dir * rng.randf_range(0.02, 0.16)
		var h := rng.randf_range(0.3, 0.55)
		var lean := rng.randf_range(0.1, 0.25)
		var w := 0.03
		var base := verts.size()
		for s in 4:
			var f := float(s) / 3.0
			var p := root + dir * (lean * h * f * f) + Vector3(0, h * f, 0)
			var ww := w * (1.0 - f * 0.85)
			verts.append(p - side * ww)
			verts.append(p + side * ww)
			var nrm := (dir * 0.35 + Vector3.UP).normalized()
			norms.append(nrm)
			norms.append(nrm)
		for s in 3:
			var a := base + s * 2
			idx.append_array(PackedInt32Array([a, a + 1, a + 2, a + 1, a + 3, a + 2]))
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = verts
	arr[Mesh.ARRAY_NORMAL] = norms
	arr[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	# --wind_grass_bend=<x> (1.0), --wind_grass_sheen=<x> (1.0)
	mesh.surface_set_material(0, _mat(Color(0.30, 0.45, 0.15), 0.8, true, null, 0.5, float(args.get("wind_grass_bend", "1.0")), 0.0, float(args.get("wind_grass_sheen", "1.0"))))
	return mesh


## Leaf cards: two crossed quads at p, facing random directions, size s.
func _leaf_cards(st: SurfaceTool, p: Vector3, s: float, rng: RandomNumberGenerator) -> void:
	for q in 2:
		var ang := rng.randf() * TAU + q * PI * 0.5
		var tilt := rng.randf_range(-0.5, 0.5)
		var ax := Vector3(cos(ang), 0.0, sin(ang))
		var up := Vector3(0, cos(tilt), 0) + ax.cross(Vector3.UP) * sin(tilt)
		var n := ax.cross(up).normalized()
		var c := [p - ax * s - up * s, p + ax * s - up * s, p + ax * s + up * s, p - ax * s + up * s]
		var uv := [Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0)]
		for t in [[0, 1, 2], [0, 2, 3]]:
			for i in t:
				st.set_normal((n + Vector3.UP * 0.6).normalized())
				st.set_uv(uv[i])
				st.add_vertex(c[i])


func _tree_mesh(p_leaf_tex: Texture2D) -> ArrayMesh:
	var rng := RandomNumberGenerator.new()
	rng.seed = 11
	var mesh := ArrayMesh.new()
	var height_t := 9.0
	# Bark: a tapering trunk and six branches (cylinders), the branches' tips listed for the leaves.
	var bark := SurfaceTool.new()
	bark.begin(Mesh.PRIMITIVE_TRIANGLES)
	var tips := []
	var trunk := CylinderMesh.new()
	trunk.top_radius = 0.12
	trunk.bottom_radius = 0.32
	trunk.height = 7.0
	trunk.radial_segments = 8
	trunk.rings = 3
	bark.append_from(trunk, 0, Transform3D(Basis(), Vector3(0, 3.5, 0)))
	for b in 7:
		var y := 3.0 + b * 0.6
		var ang := b * 2.4 + rng.randf_range(-0.3, 0.3)
		var dir := Vector3(cos(ang), rng.randf_range(0.6, 1.0), sin(ang)).normalized()
		var len := rng.randf_range(1.8, 2.8) * (1.0 - b * 0.06)
		var br := CylinderMesh.new()
		br.top_radius = 0.03
		br.bottom_radius = 0.08
		br.height = len
		br.radial_segments = 5
		br.rings = 1
		var basis := Basis(Vector3.UP.cross(dir).normalized(), Vector3.UP.angle_to(dir))
		var start := Vector3(0, y, 0)
		bark.append_from(br, 0, Transform3D(basis, start + dir * len * 0.5))
		tips.append([start, dir, len])
	bark.set_material(_mat(Color(0.33, 0.26, 0.19), 0.9, false, null, height_t, 0.08, 0.0))
	bark.generate_normals()
	bark.commit(mesh)
	# Leaves: cards along the outer half of each branch and around the top.
	var leaves := SurfaceTool.new()
	leaves.begin(Mesh.PRIMITIVE_TRIANGLES)
	for t in tips:
		var start: Vector3 = t[0]
		var dir: Vector3 = t[1]
		var len: float = t[2]
		for k in 5:
			var f := 0.45 + 0.55 * float(k) / 4.0
			var p: Vector3 = start + dir * len * f + Vector3(rng.randf_range(-0.4, 0.4), rng.randf_range(-0.2, 0.4), rng.randf_range(-0.4, 0.4))
			_leaf_cards(leaves, p, rng.randf_range(0.55, 0.8), rng)
	for k in 8:
		var a := rng.randf() * TAU
		var r := rng.randf_range(0.0, 1.4)
		_leaf_cards(leaves, Vector3(cos(a) * r, rng.randf_range(7.0, 8.8), sin(a) * r), rng.randf_range(0.6, 0.9), rng)
	leaves.set_material(_mat(Color(1, 1, 1), 0.75, true, p_leaf_tex, height_t, 0.08, 1.0))
	leaves.commit(mesh)
	return mesh


func _bush_mesh(p_leaf_tex: Texture2D) -> ArrayMesh:
	var rng := RandomNumberGenerator.new()
	rng.seed = 23
	var mesh := ArrayMesh.new()
	var leaves := SurfaceTool.new()
	leaves.begin(Mesh.PRIMITIVE_TRIANGLES)
	for k in 14:
		var a := rng.randf() * TAU
		var r := rng.randf_range(0.0, 0.7)
		_leaf_cards(leaves, Vector3(cos(a) * r, rng.randf_range(0.35, 1.1), sin(a) * r), rng.randf_range(0.35, 0.5), rng)
	leaves.set_material(_mat(Color(0.95, 1.0, 0.9), 0.8, true, p_leaf_tex, 1.3, 0.3, 0.8))
	leaves.commit(mesh)
	return mesh


func _place(x: float, z: float, yaw: float, s: float, sink := 0.0) -> Transform3D:
	return Transform3D(Basis(Vector3.UP, yaw).scaled(Vector3(s, s, s)), Vector3(x, height(x, z) - sink, z))


func _build_vegetation() -> void:
	var leaf_tex := _leaf_texture()
	var half := SIZE * 0.5
	var grass: Array = []
	# --wind_grass=<clumps per m²> (4.5): fewer for lavapipe's timing (its vertices are slow).
	var step := 1.0 / sqrt(float(args.get("wind_grass", str(GRASS_PER_M2))))
	var gx := -MEADOW - 10.0
	while gx < MEADOW + 10.0:
		var gz := -MEADOW - 10.0
		while gz < MEADOW + 10.0:
			var x := gx + _rng.randf_range(0.0, step)
			var z := gz + _rng.randf_range(0.0, step)
			if Vector2(x, z).length() < MEADOW + 6.0 * sin(atan2(z, x) * 3.0):
				grass.append(_place(x, z, _rng.randf() * TAU, _rng.randf_range(0.8, 1.3)))
			gz += step
		gx += step
	# Two trees where the edge camera looks; none within 3 m of a camera.
	var trees: Array = [_place(61.0, 11.0, 0.4, 1.1, 0.1), _place(66.5, 18.0, 2.1, 1.0, 0.1)]
	var cams := [Vector2(-45, -10), Vector2(52, 8), Vector2(80, -50)]
	while trees.size() < TREES:
		var x := _rng.randf_range(-half + 4.0, half - 4.0)
		var z := _rng.randf_range(-half + 4.0, half - 4.0)
		var r := Vector2(x, z).length()
		var clear := true
		for c in cams:
			if (c as Vector2).distance_to(Vector2(x, z)) < 3.0:
				clear = false
		if clear and (r > MEADOW + 8.0 or _rng.randf() < 0.01):
			trees.append(_place(x, z, _rng.randf() * TAU, _rng.randf_range(0.8, 1.25), 0.1))
	var bushes: Array = []
	while bushes.size() < BUSHES:
		var a := _rng.randf() * TAU
		var r := MEADOW + _rng.randf_range(-6.0, 20.0)
		var x := cos(a) * r
		var z := sin(a) * r
		bushes.append(_place(x, z, _rng.randf() * TAU, _rng.randf_range(0.8, 1.5), 0.05))
	_counts = {"grass": grass.size(), "trees": trees.size(), "bushes": bushes.size()}
	_add_multimesh("grass", _grass_mesh(), grass)
	_add_multimesh("trees", _tree_mesh(leaf_tex), trees)
	_add_multimesh("bushes", _bush_mesh(leaf_tex), bushes)


func _add_multimesh(p_name: String, p_mesh: Mesh, p_items: Array) -> void:
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.mesh = p_mesh
	mm.instance_count = p_items.size()
	for i in p_items.size():
		mm.set_instance_transform(i, p_items[i])
	var mmi := MultiMeshInstance3D.new()
	mmi.name = p_name
	mmi.multimesh = mm
	# The sway stays within the plant's bend: the margin bounds it for culling (decision 0036).
	mmi.extra_cull_margin = 2.0
	add_child(mmi)


func set_time(t: float) -> void:
	if wind:
		wind.time = t


func _eye(x: float, z: float, up := 1.7) -> Vector3:
	return Vector3(x, height(x, z) + up, z)


func _define_cameras() -> void:
	fixed_cameras = {
		"wind_meadow": {"pos": _eye(-45, -10), "target": _eye(30, 15, 0.5), "fov": 65.0, "time": 2.0, "purpose": "eye level across the meadow to the forest edge: gusts rolling over the grass"},
		"wind_edge": {"pos": _eye(52, 8, 1.6), "target": _eye(64, 12, 4.5), "fov": 65.0, "time": 2.0, "purpose": "at the forest edge, a tree close: trunk, branches, leaves fluttering"},
		"wind_forest": {"pos": _eye(80, -50, 1.7), "target": _eye(95, -40, 5.0), "fov": 70.0, "time": 2.0, "purpose": "inside the forest: canopy, trunks, dappled shadows moving"},
		"wind_aerial": {"pos": Vector3(-110, 45, -90), "target": Vector3(0, 0, 0), "fov": 55.0, "time": 2.0, "purpose": "over the meadow and the forest: gusts as waves over the grass"},
	}
	flythrough = CameraPath.from_array([
		[0.0, _eye(-60, -30), _eye(0, 0, 1.0), 65.0],
		[8.0, _eye(-20, -5), _eye(40, 10, 1.5), 65.0],
		[16.0, _eye(30, 5), _eye(80, 20, 3.0), 68.0],
		[24.0, _eye(70, -20, 2.0), _eye(100, -40, 4.0), 70.0],
		[30.0, Vector3(40, 30, -60), Vector3(0, 0, 0), 60.0],
	])
	temporal_seqs = {
		"wind_still": {"start": 2.0, "frames": 90, "purpose": "a still camera over the meadow: the gusts' waves, the trees swaying (motion vectors, TAA)",
			"path": CameraPath.from_array([[0.0, _eye(-45, -10), _eye(30, 15, 0.5), 65.0], [1.0, _eye(-45, -10), _eye(30, 15, 0.5), 65.0]])},
		"wind_over": {"start": 2.0, "frames": 90, "purpose": "a still camera above the meadow: the gusts' waves over the grass",
			"path": CameraPath.from_array([[0.0, Vector3(-60, 22, -40), Vector3(-10, 0, 0), 55.0], [1.0, Vector3(-60, 22, -40), Vector3(-10, 0, 0), 55.0]])},
		"wind_tree": {"start": 2.0, "frames": 90, "purpose": "a still camera on a tree at the forest edge: branches and leaves",
			"path": CameraPath.from_array([[0.0, _eye(52, 8, 1.6), _eye(64, 12, 4.5), 65.0], [1.0, _eye(52, 8, 1.6), _eye(64, 12, 4.5), 65.0]])},
	}


## A single leaf (alpha: its shape, a midrib), for the falling leaves.
func _single_leaf_texture() -> ImageTexture:
	var n := 32
	var img := Image.create_empty(n, n, false, Image.FORMAT_RGBA8)
	for y in n:
		for x in n:
			var u := (float(x) + 0.5) / n * 2.0 - 1.0
			var v := (float(y) + 0.5) / n * 2.0 - 1.0
			var w := 0.55 * (1.0 - v * v) # half-width along the leaf
			var inside := absf(u) < w and absf(v) < 0.95
			var shade := 0.85 + 0.15 * (1.0 - absf(u) / maxf(w, 0.01))
			if absf(u) < 0.04:
				shade *= 0.8
			img.set_pixel(x, y, Color(shade, shade, shade, 1.0 if inside else 0.0))
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)


func _build_leaves(p_amount: int) -> void:
	var p := GPUParticles3D.new()
	p.name = "Leaves"
	p.amount = p_amount
	p.lifetime = 10.0
	p.preprocess = 10.0
	p.fixed_fps = 60
	p.use_fixed_seed = true
	p.seed = 2627
	p.visibility_aabb = AABB(Vector3(-140, -10, -140), Vector3(280, 40, 280))
	var pm := ParticleProcessMaterial.new()
	pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
	pm.emission_box_extents = Vector3(45.0, 2.5, 35.0)
	pm.direction = Vector3(0, -1, 0)
	pm.spread = 30.0
	pm.initial_velocity_min = 0.2
	pm.initial_velocity_max = 0.6
	pm.gravity = Vector3(0, -0.9, 0)
	pm.damping_min = 0.3
	pm.damping_max = 0.6
	pm.angular_velocity_min = -220.0
	pm.angular_velocity_max = 220.0
	pm.angle_min = -180.0
	pm.angle_max = 180.0
	pm.scale_min = 0.7
	pm.scale_max = 1.3
	pm.hue_variation_min = -0.06
	pm.hue_variation_max = 0.04
	pm.set("wind_enabled", true)
	pm.set("wind_influence", float(args.get("wind_leaves_influence", "0.8")))
	p.process_material = pm
	p.position = Vector3(5.0, 9.0, -5.0)
	var q := QuadMesh.new()
	q.size = Vector2(0.09, 0.13)
	var m := StandardMaterial3D.new()
	m.albedo_texture = _single_leaf_texture()
	m.albedo_color = Color(0.62, 0.55, 0.18)
	m.vertex_color_use_as_albedo = true
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	m.alpha_scissor_threshold = 0.5
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	m.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	m.roughness = 0.8
	q.material = m
	p.draw_pass_1 = q
	add_child(p)
