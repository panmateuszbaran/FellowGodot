extends "res://bench/scene_base.gd"
## ROBUSTNESS SCENE - Blend Lab (built in code, deterministic, driven only through set_time()).
##
## Not a performance benchmark: alpha-blended (mix) surfaces whose alpha is exactly 0 over large regions, as foliage
## cards, decals-as-quads, fences and particles drawn with blending have: a texture with alpha-0 holes (linear and
## nearest filtering, mipmaps), an albedo colour with alpha 0, a shader that writes ALPHA = 0 over half its area,
## vertex-colour alpha, and materials that must keep stock behaviour (depth_draw_always, alpha antialiasing,
## premultiplied/add blending). Lit by a shadowed sun, an omni and a spot light, with fog, SSAO and TAA.

var sun: DirectionalLight3D
var cards: Array[MeshInstance3D] = []


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "blend"
	_setup_common()
	_setup_environment()
	_build_ground()
	_build_cards()
	_build_lights()
	_define_cameras()
	set_time(0.0)
	info = {"stats": scene_stats(self), "notes": "Robustness scene for blended alpha-zero fragments; not a performance benchmark."}


func _setup_environment() -> void:
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.ssao_enabled = true
	env.fog_enabled = true
	env.fog_density = 0.01
	env.volumetric_fog_enabled = true
	env.volumetric_fog_density = 0.01
	cam_attr.auto_exposure_enabled = false


func _hole_texture(n: int, nearest_pattern: bool) -> ImageTexture:
	# RGBA8: coloured leaves (alpha 1 inside, a soft rim, exactly 0 elsewhere).
	var img := Image.create(n, n, false, Image.FORMAT_RGBA8)
	for y in n:
		for x in n:
			var u := (float(x) + 0.5) / n
			var v := (float(y) + 0.5) / n
			var a := 0.0
			for k in 5:
				var cx := 0.2 + 0.15 * k
				var cy := 0.25 + 0.5 * fposmod(float(k) * 0.618, 1.0)
				var d := Vector2((u - cx) * 1.6, v - cy).length()
				a = maxf(a, clampf((0.16 - d) * 25.0, 0.0, 1.0))
			if nearest_pattern:
				a = 1.0 if ((x / 8 + y / 8) % 2 == 0 and a > 0.0) else (0.5 if a > 0.5 else 0.0)
			img.set_pixel(x, y, Color(0.2 + 0.6 * u, 0.5 + 0.3 * v, 0.15, a))
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)


func _build_ground() -> void:
	var mi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(60, 60)
	pm.subdivide_width = 8
	pm.subdivide_depth = 8
	mi.mesh = pm
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(0.45, 0.42, 0.38)
	m.roughness = 0.8
	mi.material_override = m
	add_child(mi)
	# A few opaque blocks behind the cards (shadows through holes, depth under the cards).
	for i in 6:
		var b := MeshInstance3D.new()
		var bm := BoxMesh.new()
		bm.size = Vector3(1.5, 2.0 + i * 0.4, 1.5)
		b.mesh = bm
		var mm := StandardMaterial3D.new()
		mm.albedo_color = Color.from_hsv(float(i) / 6.0, 0.5, 0.8)
		mm.roughness = 0.3 + 0.1 * i
		mm.metallic = 0.5 if i % 2 == 0 else 0.0
		b.material_override = mm
		b.position = Vector3(-7.5 + 3.0 * i, bm.size.y * 0.5, -6.0)
		add_child(b)


func _card(pos: Vector3, rot_y: float, size: Vector2, mat: Material, double_sided_mesh := false) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var q := QuadMesh.new()
	q.size = size
	mi.mesh = q
	mi.material_override = mat
	mi.position = pos
	mi.rotation_degrees = Vector3(0, rot_y, 0)
	add_child(mi)
	cards.append(mi)
	return mi


func _build_cards() -> void:
	var tex_lin := _hole_texture(256, false)
	var tex_near := _hole_texture(128, true)

	# 1. foliage cards: texture alpha, linear + mipmaps, two-sided, lit, receive shadows.
	var leaf := StandardMaterial3D.new()
	leaf.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	leaf.albedo_texture = tex_lin
	leaf.cull_mode = BaseMaterial3D.CULL_DISABLED
	leaf.roughness = 0.7
	# 2. nearest filtering, normal-mapped look via rim + specular.
	var fence := StandardMaterial3D.new()
	fence.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	fence.albedo_texture = tex_near
	fence.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST_WITH_MIPMAPS
	fence.metallic = 0.4
	fence.roughness = 0.3
	# 3. albedo colour alpha exactly 0 (an invisible sheet).
	var ghost := StandardMaterial3D.new()
	ghost.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	ghost.albedo_color = Color(1, 0, 0, 0)
	# 4. shader: ALPHA = 0 on half the area, fractional elsewhere; emission and fog.
	var sh := Shader.new()
	sh.code = """
shader_type spatial;
render_mode blend_mix, cull_disabled;
void fragment() {
	float s = sin(UV.x * 31.0) * sin(UV.y * 17.0);
	ALBEDO = vec3(0.3, 0.6, 0.9) + 0.2 * vec3(UV, 0.0);
	EMISSION = vec3(0.05, 0.1, 0.2) * step(0.5, UV.x);
	ROUGHNESS = 0.4;
	ALPHA = s > 0.0 ? s : 0.0;
}
"""
	var shm := ShaderMaterial.new()
	shm.shader = sh
	# 5. vertex colour alpha (0 at two corners).
	var vc := StandardMaterial3D.new()
	vc.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	vc.vertex_color_use_as_albedo = true
	# Materials that must behave like stock (no define): depth_draw_always, alpha to coverage, premultiplied, add.
	var always := leaf.duplicate()
	always.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_ALWAYS
	var a2c := leaf.duplicate()
	a2c.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	a2c.alpha_antialiasing_mode = BaseMaterial3D.ALPHA_ANTIALIASING_ALPHA_TO_COVERAGE
	var premul := leaf.duplicate()
	premul.blend_mode = BaseMaterial3D.BLEND_MODE_PREMULT_ALPHA
	var add := fence.duplicate()
	add.blend_mode = BaseMaterial3D.BLEND_MODE_ADD

	var mats := [leaf, fence, ghost, shm, leaf, fence, always, a2c, premul, add]
	for row in 3:
		for i in 10:
			var x := -9.0 + 2.0 * i + 0.3 * row
			var z := -3.0 + 2.5 * row
			_card(Vector3(x, 1.4, z), -20.0 + 15.0 * row + 4.0 * i, Vector2(2.2, 2.2), mats[(i + row * 3) % mats.size()])
	# Vertex-colour quad (ArrayMesh with per-vertex alpha).
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var corners := [Vector3(-2, 0, 0), Vector3(2, 0, 0), Vector3(2, 3, 0), Vector3(-2, 3, 0)]
	var alphas := [0.0, 1.0, 0.0, 0.0]
	for idx in [0, 1, 2, 0, 2, 3]:
		st.set_color(Color(0.9, 0.7, 0.3, alphas[idx]))
		st.set_normal(Vector3(0, 0, 1))
		st.add_vertex(corners[idx])
	var vmi := MeshInstance3D.new()
	vmi.mesh = st.commit()
	vmi.material_override = vc
	vmi.position = Vector3(4, 0.1, 4)
	add_child(vmi)
	# Overlapping layers (blending order matters).
	for k in 4:
		_card(Vector3(-4 + 0.4 * k, 1.2 + 0.1 * k, 4.5 + 0.3 * k), 10.0 * k, Vector2(2.5, 2.5), leaf if k % 2 == 0 else shm)


func _build_lights() -> void:
	sun = DirectionalLight3D.new()
	sun.name = "sun"
	sun.light_energy = 2.0
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 60.0
	sun.light_angular_distance = 0.5
	apply_sun_overrides(sun)
	add_child(sun)
	var omni := OmniLight3D.new()
	omni.position = Vector3(1, 2.5, 2)
	omni.omni_range = 9.0
	omni.light_energy = 3.0
	omni.light_color = Color(1.0, 0.7, 0.5)
	omni.shadow_enabled = true
	add_child(omni)
	var spot := SpotLight3D.new()
	spot.spot_range = 14.0
	spot.spot_angle = 30.0
	spot.light_energy = 8.0
	spot.shadow_enabled = true
	add_child(spot)
	spot.look_at_from_position(Vector3(-5, 6, 6), Vector3(-3, 1, 0), Vector3.UP)


func _define_cameras() -> void:
	fixed_cameras = {
		"blend_front": {"pos": Vector3(0, 2, 11), "target": Vector3(0, 1.3, 0), "fov": 60.0, "time": 1.0, "purpose": "all card rows"},
		"blend_close": {"pos": Vector3(-3, 1.5, 7.5), "target": Vector3(-4, 1.3, 4.5), "fov": 50.0, "time": 1.0, "purpose": "overlapping layers close up"},
		"blend_side": {"pos": Vector3(12, 3, 3), "target": Vector3(0, 1.2, 0), "fov": 60.0, "time": 1.0, "purpose": "grazing angles, fog"},
		"blend_top": {"pos": Vector3(0, 12, 6), "target": Vector3(0, 0, 0), "fov": 60.0, "time": 1.0, "purpose": "shadows through holes"},
	}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(0, 2, 11), Vector3(0, 1.3, 0), 60.0],
		[5.0, Vector3(12, 3, 3), Vector3(0, 1.2, 0), 60.0],
		[10.0, Vector3(0, 2, 11), Vector3(0, 1.3, 0), 60.0],
	])
	temporal_seqs = {
		"blend_pan": {"start": 0.5, "frames": 60, "purpose": "pan along the cards",
			"path": CameraPath.from_array([[0.0, Vector3(-6, 2, 9), Vector3(-6, 1.3, 0), 60.0], [1.0, Vector3(6, 2, 9), Vector3(6, 1.3, 0), 60.0]])},
	}


func set_time(t: float) -> void:
	if sun:
		sun.rotation_degrees = Vector3(-45.0 - 5.0 * t, -30.0 + 3.0 * t, 0.0)
