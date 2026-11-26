extends Node3D
## Milestone 210 (the owner: "the hay pile on the cart looks very unrealistic and off-putting"): the cart's hay made at
## run time, in place of its model's smooth dome (that surface hidden) — loose hay heaped over the bed:
##  * a heap: a loaf over the bed higher than its sides, lumpy with forkfuls, spilling over the side boards and the
##    ends and hanging down outside them, shaded as straw (hay.gdshader: streaks every way, gold to grey-green, deep
##    between them);
##  * straws: STRAWS thin curved blades (a MultiMesh, each its own length, width, colour and curl) over the heap — most
##    lying along it every way, some standing out of it, a fringe hanging over the boards — and a few on the ground
##    under it;
##  * `burst(at)`: a body landing in it throws up straws (the player's leap of faith);
##  * the straws stir in the scene's wind (milestone 215), those standing out or hanging over the boards most.
## Its parent is the cart (the model's bed 0.81 m up, its sides to 1.17 m at ±0.72 m across, its ends at ±1.27 m
## along); everything is seeded, the same each time.

@export var straws := 26000
@export var ground_straws := 500
@export var seed := 11

const BED := 0.81
const SIDE_TOP := 1.17
const HALF_X := 1.27 # the end boards
const HALF_Z := 0.72 # the side boards
const SPILL := 0.24 # m out over the boards
const TOP := 2.02 # the heap's crown

var _rng := RandomNumberGenerator.new()
var _clumps: Array = []
var _particles: GPUParticles3D


func _ready() -> void:
	_rng.seed = seed
	_hide_model_hay()
	for i in 14:
		_clumps.append([Vector2(_rng.randf_range(-1.0, 1.0), _rng.randf_range(-0.6, 0.6)), _rng.randf_range(0.05, 0.16), _rng.randf_range(0.18, 0.4)])
	var heap := MeshInstance3D.new()
	heap.name = "Heap"
	heap.mesh = _heap_mesh()
	# (the heap's own straw shading, darker: it is the shade between the straws over it)
	var hm := (load("res://rooftops/materials/hay.tres") as ShaderMaterial).duplicate() as ShaderMaterial
	hm.set_shader_parameter("color", Color(0.56, 0.44, 0.22))
	heap.material_override = hm
	add_child(heap)
	var mm := MultiMeshInstance3D.new()
	mm.name = "Straws"
	mm.multimesh = _straws()
	mm.material_override = _straw_material()
	mm.visibility_range_end = 70.0
	mm.visibility_range_end_margin = 10.0
	mm.visibility_range_fade_mode = GeometryInstance3D.VISIBILITY_RANGE_FADE_SELF
	add_child(mm)
	_particles = _burst_particles()
	add_child(_particles)
	add_to_group(&"hay_fx")


## The model's hay (the surface with hay.tres) drawn no more: this heap is in its place.
func _hide_model_hay() -> void:
	var mi := get_parent().get_node_or_null("Mesh") as MeshInstance3D
	if mi == null or mi.mesh == null:
		return
	for i in mi.mesh.get_surface_count():
		var m := mi.mesh.surface_get_material(i)
		if m and m.resource_path.ends_with("hay.tres"):
			var hidden := StandardMaterial3D.new()
			hidden.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			hidden.albedo_color = Color(0, 0, 0, 0)
			hidden.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			mi.set_surface_override_material(i, hidden)


## The heap's height at (x, z) (the cart's frame): a loaf higher than the sides, its forkfuls, a fine roughness; over
## the boards it spills out and hangs down outside them.
func height(x: float, z: float) -> float:
	var ax := absf(x) / (HALF_X + SPILL)
	var az := absf(z) / (HALF_Z + SPILL)
	if ax >= 1.0 or az >= 1.0:
		return SIDE_TOP - 0.2
	var loaf := pow(1.0 - pow(ax, 2.6), 0.55) * pow(1.0 - pow(az, 2.3), 0.6)
	var h := BED + (TOP - BED) * loaf
	for c in _clumps:
		var d := Vector2(x, z) - (c[0] as Vector2) * Vector2(HALF_X, HALF_Z)
		h += float(c[1]) * exp(-d.length_squared() / (float(c[2]) * float(c[2]))) * loaf
	h += 0.035 * (sin(x * 7.3 + z * 2.1) * sin(z * 6.1 - x * 1.7) + 0.5 * sin(x * 13.0 + 1.3) * sin(z * 11.0 + 0.7)) * loaf
	# (outside the boards: hanging down their outside, no lower than 0.2 m under their top)
	var out := maxf((absf(z) - HALF_Z) / SPILL, (absf(x) - HALF_X) / SPILL)
	if out > 0.0:
		h = lerpf(h, SIDE_TOP - 0.2, smoothstep(0.0, 1.0, out))
	return h


func _heap_mesh() -> ArrayMesh:
	var nx := 72
	var nz := 46
	var x0 := -(HALF_X + SPILL)
	var z0 := -(HALF_Z + SPILL)
	var sx := 2.0 * (HALF_X + SPILL) / nx
	var sz := 2.0 * (HALF_Z + SPILL) / nz
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var pts := []
	for j in nz + 1:
		for i in nx + 1:
			var x := x0 + i * sx
			var z := z0 + j * sz
			pts.append(Vector3(x, height(x, z), z))
	for j in nz:
		for i in nx:
			var a: Vector3 = pts[j * (nx + 1) + i]
			var b: Vector3 = pts[j * (nx + 1) + i + 1]
			var c: Vector3 = pts[(j + 1) * (nx + 1) + i]
			var d: Vector3 = pts[(j + 1) * (nx + 1) + i + 1]
			for v in [a, c, b, b, c, d]:
				st.set_normal(_normal(v.x, v.z))
				st.add_vertex(v)
	return st.commit()


func _normal(x: float, z: float) -> Vector3:
	var e := 0.02
	return Vector3(height(x - e, z) - height(x + e, z), 2.0 * e, height(x, z - e) - height(x, z + e)).normalized()


## One straw: a thin blade 1 m long along +Y (scaled by each instance), its width along X, curving towards +Z.
func _straw_mesh() -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var n := 4
	for k in n:
		var t0 := float(k) / n
		var t1 := float(k + 1) / n
		var p0 := Vector3(0.0, t0, 0.18 * t0 * t0)
		var p1 := Vector3(0.0, t1, 0.18 * t1 * t1)
		var w0 := 0.5 * (1.0 - 0.6 * t0)
		var w1 := 0.5 * (1.0 - 0.6 * t1)
		var quad := [p0 + Vector3(-w0, 0, 0), p0 + Vector3(w0, 0, 0), p1 + Vector3(-w1, 0, 0), p1 + Vector3(w1, 0, 0)]
		var uvs := [Vector2(0, t0), Vector2(1, t0), Vector2(0, t1), Vector2(1, t1)]
		for q in [0, 2, 1, 1, 2, 3]:
			st.set_normal(Vector3(0.0, -0.36 * (t0 + t1) * 0.5, 1.0).normalized())
			st.set_uv(uvs[q])
			st.add_vertex(quad[q])
	return st.commit()


func _straws() -> MultiMesh:
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_custom_data = true
	mm.mesh = _straw_mesh()
	mm.instance_count = straws + ground_straws
	var lx := HALF_X + SPILL
	var lz := HALF_Z + SPILL
	for i in straws:
		var x := _rng.randf_range(-lx, lx)
		var z := _rng.randf_range(-lz, lz)
		var y := height(x, z)
		var n := _normal(x, z)
		var edge := maxf((absf(z) - HALF_Z) / SPILL, (absf(x) - HALF_X) / SPILL)
		var dir: Vector3
		if edge > -0.15 and _rng.randf() < 0.45:
			# The fringe over the boards: hanging down their outside, out and down, every which way.
			var outw := Vector3(signf(x) if (absf(x) - HALF_X) > (absf(z) - HALF_Z) else 0.0, 0.0, signf(z) if (absf(z) - HALF_Z) >= (absf(x) - HALF_X) else 0.0)
			dir = (outw * _rng.randf_range(0.3, 1.1) + Vector3.DOWN * _rng.randf_range(0.5, 1.0) + Vector3(_rng.randf_range(-0.7, 0.7), 0.0, _rng.randf_range(-0.7, 0.7))).normalized()
		elif _rng.randf() < 0.22:
			# Standing out of the heap.
			dir = (n * _rng.randf_range(0.6, 1.0) + Vector3(_rng.randf_range(-0.7, 0.7), 0.0, _rng.randf_range(-0.7, 0.7))).normalized()
		else:
			# Lying along it, every way.
			var t := Vector3(_rng.randf_range(-1.0, 1.0), 0.0, _rng.randf_range(-1.0, 1.0))
			dir = (t - n * t.dot(n)).normalized()
			if dir.length() < 0.5:
				dir = n.cross(Vector3.RIGHT).normalized()
			dir = (dir + n * _rng.randf_range(-0.05, 0.25)).normalized()
		var length := _rng.randf_range(0.12, 0.34)
		var width := _rng.randf_range(0.0035, 0.007)
		var start := Vector3(x, y - _rng.randf_range(0.0, 0.03), z) - dir * length * _rng.randf_range(0.0, 0.3)
		mm.set_instance_transform(i, _blade(start, dir, length, width))
		mm.set_instance_custom_data(i, _straw_colour())
	# A few fallen round the cart.
	for i in ground_straws:
		var a := _rng.randf() * TAU
		var r := sqrt(_rng.randf()) * 1.2
		var x := cos(a) * (lx + r * 0.6) * 0.95
		var z := sin(a) * (lz + r)
		var dir := Vector3(_rng.randf_range(-1.0, 1.0), _rng.randf_range(0.0, 0.08), _rng.randf_range(-1.0, 1.0)).normalized()
		var length := _rng.randf_range(0.1, 0.28)
		mm.set_instance_transform(straws + i, _blade(Vector3(x, 0.006, z), dir, length, _rng.randf_range(0.003, 0.006)))
		mm.set_instance_custom_data(straws + i, _straw_colour())
	return mm


## A straw from p_start along p_dir, its width across, its curl about it at random.
func _blade(p_start: Vector3, p_dir: Vector3, p_length: float, p_width: float) -> Transform3D:
	var y := p_dir.normalized()
	var side := y.cross(Vector3.UP if absf(y.y) < 0.95 else Vector3.RIGHT).normalized()
	side = side.rotated(y, _rng.randf() * TAU)
	var zz := side.cross(y).normalized()
	return Transform3D(Basis(side * p_width, y * p_length, zz * p_length), p_start)


## Each straw's colour (gold, pale, a few grey-green or brown) and how much lighter its tip is.
func _straw_colour() -> Color:
	var r := _rng.randf()
	var c := Color(0.68, 0.5, 0.17)
	if r < 0.3:
		c = Color(0.76, 0.6, 0.25)
	elif r < 0.48:
		c = Color(0.55, 0.39, 0.13)
	elif r < 0.58:
		c = Color(0.48, 0.46, 0.2)
	elif r < 0.65:
		c = Color(0.42, 0.29, 0.12)
	var v := _rng.randf_range(0.85, 1.12)
	return Color(c.r * v, c.g * v, c.b * v, _rng.randf_range(0.0, 1.0))


func _straw_material() -> ShaderMaterial:
	var sh := Shader.new()
	sh.code = """shader_type spatial;
render_mode cull_disabled, diffuse_burley, specular_schlick_ggx, fellow_wind;
varying vec4 straw;
varying float along;
void vertex() {
	straw = INSTANCE_CUSTOM;
	along = UV.y;
	// Milestone 215: the scene's wind (FellowEngine's, decision 0125, render_mode fellow_wind: a FellowWind3D's gusts)
	// stirs the straws — those standing out of the heap or hanging over its boards (upright, up or down) most, those
	// lying in it hardly (held by the others). Each straw a stem of its own length (the blade is 1 m along +Y, scaled),
	// swaying from its foot.
	float upright = abs(normalize(MODEL_MATRIX[1].xyz).y);
	WIND_HEIGHT = 1.0;
	WIND_BEND = mix(0.04, 0.6, upright * upright);
	WIND_FLUTTER = 0.4;
}
void fragment() {
	// A straw's hollow round stem: lighter down its middle, darker at its edges and its foot (in the heap's shade), a
	// lighter, drier tip for some.
	float across = abs(UV.x - 0.5) * 2.0;
	vec3 col = straw.rgb * (0.78 + 0.32 * (1.0 - across * across));
	col *= mix(0.55, 1.0, smoothstep(0.0, 0.35, along));
	col = mix(col, col * 1.1 + vec3(0.02), smoothstep(0.6, 1.0, along) * straw.a);
	ALBEDO = col;
	ROUGHNESS = 0.72;
	SPECULAR = 0.35;
	BACKLIGHT = col * 0.3;
}
"""
	var m := ShaderMaterial.new()
	m.shader = sh
	return m


## Straws thrown up where a body lands in the hay.
func _burst_particles() -> GPUParticles3D:
	var p := GPUParticles3D.new()
	p.name = "Burst"
	p.amount = 520
	p.one_shot = true
	p.emitting = false
	p.explosiveness = 0.92
	p.lifetime = 2.2
	p.local_coords = false
	var pm := ParticleProcessMaterial.new()
	pm.direction = Vector3(0, 1, 0)
	pm.spread = 75.0
	pm.initial_velocity_min = 2.2
	pm.initial_velocity_max = 5.6
	pm.gravity = Vector3(0, -4.0, 0)
	pm.damping_min = 1.2
	pm.damping_max = 2.4
	pm.angular_velocity_min = -360.0
	pm.angular_velocity_max = 360.0
	pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	pm.emission_sphere_radius = 0.6
	pm.scale_min = 0.8
	pm.scale_max = 1.6
	pm.color = Color(0.82, 0.68, 0.36)
	# (each straw along its flight, turning over as it rises and falls, fluttering: not a stick standing in the air)
	pm.particle_flag_align_y = true
	pm.turbulence_enabled = true
	pm.turbulence_noise_strength = 1.4
	pm.turbulence_influence_min = 0.08
	pm.turbulence_influence_max = 0.2
	p.process_material = pm
	var blade := QuadMesh.new()
	blade.size = Vector2(0.01, 0.26)
	var bm := StandardMaterial3D.new()
	bm.albedo_color = Color(0.82, 0.68, 0.36)
	bm.cull_mode = BaseMaterial3D.CULL_DISABLED
	bm.vertex_color_use_as_albedo = true
	bm.roughness = 0.8
	blade.material = bm
	p.draw_pass_1 = blade
	p.visibility_aabb = AABB(Vector3(-4, -1, -4), Vector3(8, 6, 8))
	return p


## A body landing at p_at (world): straws thrown up from the heap there.
func burst(p_at: Vector3) -> void:
	if _particles == null:
		return
	# (from the heap's top over where it landed)
	var local := to_local(p_at)
	_particles.global_position = to_global(Vector3(local.x, height(local.x, local.z) + 0.1, local.z))
	_particles.restart()
