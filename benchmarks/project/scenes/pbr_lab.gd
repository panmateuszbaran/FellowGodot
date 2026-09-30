extends "res://bench/scene_base.gd"
## BENCHMARK A - PBR Validation Lab (built entirely in code, deterministic).
##
## Contents: dielectric roughness sweep, metallic roughness sweep, metallic sweep, normal-mapped panel,
## high-frequency normal-mapped glossy metal sphere (specular aliasing), HDR emissive bars, transparent
## glass panes + sphere, alpha-scissor grass cards (MultiMesh) and chain-link fence, a closed room with
## a dark interior (indirect-only region), a comb of progressively thinner cylinders, subpixel wires,
## a high-frequency checkerboard strip seen at grazing angles, a moving box and a rotating fan.
##
## Lighting variants (--lighting=):
##   art (default) procedural sky + sun + omni + spot  -> used for performance and stock-vs-fellow captures
##   ref           uniform HDR panorama sky + sun only  -> matched 1:1 by benchmarks/reference/pbr_lab_cycles.py
## --export-ref=<dir> writes pbr_lab_scene.json + all procedural textures for the Cycles reference build.
##
## All material values below are LINEAR; Godot's albedo/emission colours are sRGB-encoded, so they are
## converted with linear_to_srgb() when assigned.

const SKY_REF_RADIANCE := Color(0.60, 0.70, 0.85)  # linear radiance of the uniform reference sky
const SUN_ENERGY := 2.5  # Godot units (irradiance = PI * energy); Cycles sun strength = PI * 2.5
const SUN_COLOR := Color(1.0, 0.96, 0.9)  # linear
const SUN_ELEVATION := 40.0
const SUN_AZIMUTH := -35.0
const REF_TIME := 1.25  # scene time of every fixed camera (moving box parked at x = +3.2)

var lighting := "art"
var tex_cache := {}  # name -> {"img": Image, "tex": ImageTexture}
var desc_objects := []
var desc_lights := []
var moving_box: MeshInstance3D
var fan: Node3D
var export_dir := ""


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "pbr"
	lighting = args.get("lighting", "art")
	export_dir = args.get("export-ref", "")
	_setup_common()
	_setup_environment()
	_build_textures()
	_build_geometry()
	_build_lights()
	_define_cameras()
	set_time(0.0)
	info = {
		"lighting": lighting,
		"stats": scene_stats(self),
		"notes": "Procedural, seeded (deterministic). Diffuse model Lambert on all lab materials to match Cycles Principled v2.",
	}
	if export_dir != "":
		set_time(REF_TIME)
		_export_reference(export_dir)
		set_time(0.0)


# ------------------------------------------------------------------ environment

func _setup_environment() -> void:
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	if lighting == "ref":
		var img := Image.create_empty(64, 32, false, Image.FORMAT_RGBAF)
		img.fill(SKY_REF_RADIANCE)
		var pano := PanoramaSkyMaterial.new()
		pano.panorama = ImageTexture.create_from_image(img)
		pano.energy_multiplier = 1.0
		sky.sky_material = pano
		sky.radiance_size = Sky.RADIANCE_SIZE_256
	else:
		var proc := ProceduralSkyMaterial.new()
		proc.sky_top_color = Color(0.32, 0.46, 0.72)
		proc.sky_horizon_color = Color(0.70, 0.75, 0.82)
		proc.ground_horizon_color = Color(0.62, 0.63, 0.65)
		proc.ground_bottom_color = Color(0.25, 0.23, 0.21)
		proc.sun_angle_max = 30.0
		sky.sky_material = proc
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_sky_contribution = 1.0
	env.ambient_light_energy = 1.0
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.tonemap_exposure = 1.0
	env.glow_enabled = true
	env.ssao_enabled = true
	env.ssao_radius = 1.0
	env.ssao_intensity = 2.0
	env.ssr_enabled = false
	env.ssil_enabled = false
	env.sdfgi_enabled = false
	env.fog_enabled = false
	env.volumetric_fog_enabled = false
	cam_attr.exposure_multiplier = 1.0
	cam_attr.auto_exposure_enabled = false


# ------------------------------------------------------------------ procedural textures

func _add_tex(tex_name: String, img: Image, mip := true) -> ImageTexture:
	if mip:
		img.generate_mipmaps()
	var t := ImageTexture.create_from_image(img)
	tex_cache[tex_name] = {"img": img, "tex": t}
	return t


func _build_textures() -> void:
	# 1) Tile + noise height -> normal map (panel).
	var n := FastNoiseLite.new()
	n.seed = 1337
	n.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	n.frequency = 0.02
	n.fractal_octaves = 4
	var size := 1024
	var h := n.get_image(size, size, false, false, true)  # normalized L8 noise
	h.convert(Image.FORMAT_RF)
	var tile := 128
	for y in range(size):
		for x in range(size):
			var gx := x % tile
			var gy := y % tile
			var grout := 1.0 if (gx < 6 or gy < 6) else 0.0
			var v := h.get_pixel(x, y).r * 0.35 + (1.0 - grout) * 0.65
			h.set_pixel(x, y, Color(v, v, v))
	h.convert(Image.FORMAT_RGBA8)
	h.bump_map_to_normal_map(6.0)
	_add_tex("panel_normal", h)

	# 2) High-frequency ridges -> normal map (specular aliasing sphere).
	var r := Image.create_empty(1024, 512, false, Image.FORMAT_RF)
	for y in range(512):
		for x in range(1024):
			var v := 0.5 + 0.5 * sin(x * TAU * 64.0 / 1024.0) * 0.6 + 0.2 * sin((x + y) * TAU * 23.0 / 1024.0)
			r.set_pixel(x, y, Color(v, v, v))
	r.convert(Image.FORMAT_RGBA8)
	r.bump_map_to_normal_map(3.0)
	_add_tex("ridges_normal", r)

	# 3) Checkerboard albedo (8x8 checks per tile), sRGB-encoded.
	var c := Image.create_empty(512, 512, false, Image.FORMAT_RGB8)
	var dark := Color(0.04, 0.04, 0.04).linear_to_srgb()
	var light := Color(0.80, 0.80, 0.80).linear_to_srgb()
	for y in range(512):
		for x in range(512):
			c.set_pixel(x, y, light if ((x / 64 + y / 64) % 2 == 0) else dark)
	_add_tex("checker_albedo", c)

	# 4) Grass blades with alpha (alpha-scissor foliage), sRGB albedo.
	var g := Image.create_empty(256, 512, false, Image.FORMAT_RGBA8)
	g.fill(Color(0.1, 0.2, 0.05, 0.0))
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	for b in range(9):
		var base_x := rng.randf_range(20.0, 236.0)
		var lean := rng.randf_range(-60.0, 60.0)
		var height := rng.randf_range(300.0, 505.0)
		var width := rng.randf_range(10.0, 18.0)
		var col := Color(rng.randf_range(0.04, 0.10), rng.randf_range(0.20, 0.35), rng.randf_range(0.02, 0.06)).linear_to_srgb()
		for y in range(int(height)):
			var u := y / height
			var cx := base_x + lean * u * u
			var w := width * (1.0 - u)
			for x in range(int(cx - w), int(cx + w) + 1):
				if x >= 0 and x < 256:
					g.set_pixel(x, 511 - y, Color(col.r, col.g, col.b, 1.0))
	_add_tex("grass_albedo", g)

	# 5) Chain-link fence: diamond wire grid, alpha 1 on wires.
	var f := Image.create_empty(256, 256, false, Image.FORMAT_RGBA8)
	for y in range(256):
		for x in range(256):
			var d1 := absi((x + y) % 128 - 64)
			var d2 := absi((x - y + 256) % 128 - 64)
			var wire := d1 > 58 or d2 > 58
			var cc := Color(0.55, 0.56, 0.58).linear_to_srgb()
			f.set_pixel(x, y, Color(cc.r, cc.g, cc.b, 1.0 if wire else 0.0))
	_add_tex("fence_albedo", f)


# ------------------------------------------------------------------ materials & meshes

func _mat(def: Dictionary) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	var alb: Color = def.get("albedo", Color(0.5, 0.5, 0.5))
	var srgb := alb.linear_to_srgb()
	srgb.a = def.get("alpha_value", 1.0)
	m.albedo_color = srgb
	m.metallic = def.get("metallic", 0.0)
	m.roughness = def.get("roughness", 0.5)
	m.metallic_specular = 0.5
	m.diffuse_mode = BaseMaterial3D.DIFFUSE_LAMBERT
	if def.has("albedo_tex"):
		m.albedo_texture = tex_cache[def["albedo_tex"]]["tex"]
	if def.has("normal_tex"):
		m.normal_enabled = true
		m.normal_texture = tex_cache[def["normal_tex"]]["tex"]
		m.normal_scale = def.get("normal_scale", 1.0)
	if def.has("uv_scale"):
		var s: Vector2 = def["uv_scale"]
		m.uv1_scale = Vector3(s.x, s.y, 1.0)
	if def.has("emission"):
		m.emission_enabled = true
		m.emission = (def["emission"] as Color).linear_to_srgb()
		m.emission_energy_multiplier = def.get("emission_energy", 1.0)
	match def.get("alpha", "opaque"):
		"blend":
			m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		"scissor":
			m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
			m.alpha_scissor_threshold = def.get("alpha_scissor", 0.5)
		"hash":
			m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_HASH
	if def.get("double_sided", false):
		m.cull_mode = BaseMaterial3D.CULL_DISABLED
	m.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
	return m


func _mesh_from_def(md: Dictionary) -> Mesh:
	match md["type"]:
		"sphere":
			var s := SphereMesh.new()
			s.radius = md["radius"]
			s.height = md["radius"] * 2.0
			s.radial_segments = md.get("segments", 64)
			s.rings = md.get("rings", 32)
			return s
		"box":
			var b := BoxMesh.new()
			b.size = md["size"]
			return b
		"plane":
			var p := PlaneMesh.new()
			p.size = md["size"]
			return p
		"cylinder":
			var c := CylinderMesh.new()
			c.top_radius = md["radius"]
			c.bottom_radius = md["radius"]
			c.height = md["height"]
			c.radial_segments = md.get("segments", 16)
			c.rings = 1
			return c
		"quad":
			var q := QuadMesh.new()
			q.size = md["size"]
			return q
	return null


func _add(obj_name: String, md: Dictionary, xf: Transform3D, def: Dictionary, parent: Node3D = null) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.name = obj_name
	mi.mesh = _mesh_from_def(md)
	mi.material_override = _mat(def)
	mi.transform = xf
	(parent if parent else self).add_child(mi)
	desc_objects.append({"name": obj_name, "mesh": _json_mesh(md), "material": _json_mat(def), "parent": parent.name if parent else ""})
	return mi


func _json_mesh(md: Dictionary) -> Dictionary:
	var o := {}
	for k in md.keys():
		var v = md[k]
		if v is Vector2 or v is Vector3:
			o[k] = [v.x, v.y] if v is Vector2 else [v.x, v.y, v.z]
		else:
			o[k] = v
	return o


func _json_mat(def: Dictionary) -> Dictionary:
	var o := {}
	for k in def.keys():
		var v = def[k]
		if v is Color:
			o[k] = [v.r, v.g, v.b]
		elif v is Vector2:
			o[k] = [v.x, v.y]
		else:
			o[k] = v
	return o


func _xf(pos: Vector3, rot_deg := Vector3.ZERO, scl := Vector3.ONE) -> Transform3D:
	var b := Basis.from_euler(rot_deg * (PI / 180.0)).scaled(scl)
	return Transform3D(b, pos)


func _build_geometry() -> void:
	# Ground + checker strip (grazing-angle texture aliasing).
	_add("ground", {"type": "plane", "size": Vector2(80, 80)}, _xf(Vector3.ZERO), {"albedo": Color(0.18, 0.18, 0.18), "roughness": 0.85})
	_add("checker_strip", {"type": "plane", "size": Vector2(4, 32)}, _xf(Vector3(8.0, 0.002, -12.0)), {"albedo": Color(1, 1, 1), "roughness": 0.6, "albedo_tex": "checker_albedo", "uv_scale": Vector2(1, 8)})

	# Sphere rows.
	var xs := [-3.6, -2.4, -1.2, 0.0, 1.2, 2.4, 3.6]
	for i in range(7):
		var rough := i / 6.0
		_add("dielectric_r%d" % i, {"type": "sphere", "radius": 0.4}, _xf(Vector3(xs[i], 0.4, 0.0)), {"albedo": Color(0.75, 0.12, 0.10), "roughness": rough})
		_add("gold_r%d" % i, {"type": "sphere", "radius": 0.4}, _xf(Vector3(xs[i], 0.4, -1.3)), {"albedo": Color(1.0, 0.766, 0.336), "metallic": 1.0, "roughness": rough})
		_add("copper_m%d" % i, {"type": "sphere", "radius": 0.4}, _xf(Vector3(xs[i], 0.4, -2.6)), {"albedo": Color(0.95, 0.64, 0.54), "metallic": i / 6.0, "roughness": 0.25})

	# Normal-mapped panel and high-frequency glossy sphere.
	_add("normal_panel", {"type": "box", "size": Vector3(3.5, 1.8, 0.08)}, _xf(Vector3(1.5, 0.9, -4.5)), {"albedo": Color(0.45, 0.42, 0.38), "roughness": 0.35, "normal_tex": "panel_normal", "normal_scale": 1.0, "uv_scale": Vector2(2, 1)})
	_add("ridged_metal_sphere", {"type": "sphere", "radius": 0.6}, _xf(Vector3(5.4, 0.6, -2.6)), {"albedo": Color(0.95, 0.93, 0.88), "metallic": 1.0, "roughness": 0.1, "normal_tex": "ridges_normal", "normal_scale": 1.0, "uv_scale": Vector2(4, 2)})

	# HDR emissive bars (bloom test): energy 2 / 8 / 32.
	var e_cols := [Color(1.0, 0.85, 0.6), Color(0.3, 0.9, 1.0), Color(1.0, 0.3, 0.8)]
	var e_en := [2.0, 8.0, 32.0]
	for i in range(3):
		_add("emissive_%d" % i, {"type": "box", "size": Vector3(0.08, 1.2, 0.08)}, _xf(Vector3(-5.6, 0.6, -0.5 + i * 0.7)), {"albedo": Color(0.02, 0.02, 0.02), "roughness": 0.5, "emission": e_cols[i], "emission_energy": e_en[i]})

	# Transparent glass panes + sphere.
	var g_cols := [Color(0.3, 0.5, 0.9), Color(0.3, 0.9, 0.5), Color(0.9, 0.9, 0.9)]
	var g_alpha := [0.25, 0.5, 0.75]
	for i in range(3):
		_add("glass_%d" % i, {"type": "box", "size": Vector3(1.0, 1.4, 0.02)}, _xf(Vector3(-3.4 + i * 1.2, 0.7, 1.8)), {"albedo": g_cols[i], "roughness": 0.05, "alpha": "blend", "alpha_value": g_alpha[i]})
	_add("glass_sphere", {"type": "sphere", "radius": 0.5}, _xf(Vector3(0.4, 0.5, 1.8)), {"albedo": Color(0.9, 0.9, 0.95), "roughness": 0.05, "alpha": "blend", "alpha_value": 0.35})

	# Comb of progressively thinner cylinders (fine geometric detail).
	var radii := [0.02, 0.016, 0.012, 0.009, 0.007, 0.005, 0.004, 0.003, 0.0025, 0.002, 0.0015, 0.001]
	for i in range(radii.size()):
		_add("comb_%02d" % i, {"type": "cylinder", "radius": radii[i], "height": 1.4, "segments": 8}, _xf(Vector3(0.9 + i * 0.1, 0.7, 1.15)), {"albedo": Color(0.8, 0.8, 0.8), "roughness": 0.4, "metallic": 1.0})

	# Chain-link fence (alpha-scissor, high-frequency).
	_add("fence", {"type": "quad", "size": Vector2(4.0, 1.5)}, _xf(Vector3(4.5, 0.75, 0.3)), {"albedo": Color(1, 1, 1), "roughness": 0.4, "metallic": 0.8, "albedo_tex": "fence_albedo", "uv_scale": Vector2(26, 10), "alpha": "scissor", "alpha_scissor": 0.5, "double_sided": true})

	# Subpixel wires between two poles 16 m apart, ~17 m from the overview camera.
	_add("pole_l", {"type": "cylinder", "radius": 0.08, "height": 4.2, "segments": 16}, _xf(Vector3(-8.0, 2.1, -10.0)), {"albedo": Color(0.3, 0.25, 0.2), "roughness": 0.7})
	_add("pole_r", {"type": "cylinder", "radius": 0.08, "height": 4.2, "segments": 16}, _xf(Vector3(8.0, 2.1, -10.0)), {"albedo": Color(0.3, 0.25, 0.2), "roughness": 0.7})
	for i in range(5):
		_add("wire_%d" % i, {"type": "cylinder", "radius": 0.004, "height": 16.0, "segments": 6}, _xf(Vector3(0.0, 3.0 + i * 0.2, -10.0), Vector3(0, 0, 90)), {"albedo": Color(0.05, 0.05, 0.05), "roughness": 0.5})

	# Closed room with an opening facing +Z (dark indirect-only interior).
	var wall := {"albedo": Color(0.6, 0.6, 0.6), "roughness": 0.9}
	_add("room_back", {"type": "box", "size": Vector3(3.2, 2.5, 0.1)}, _xf(Vector3(-5.5, 1.25, -7.0)), wall)
	_add("room_left", {"type": "box", "size": Vector3(0.1, 2.5, 2.6)}, _xf(Vector3(-7.05, 1.25, -5.75)), wall)
	_add("room_right", {"type": "box", "size": Vector3(0.1, 2.5, 2.6)}, _xf(Vector3(-3.95, 1.25, -5.75)), wall)
	_add("room_roof", {"type": "box", "size": Vector3(3.2, 0.1, 2.7)}, _xf(Vector3(-5.5, 2.55, -5.75)), wall)
	_add("room_lintel", {"type": "box", "size": Vector3(3.2, 1.0, 0.1)}, _xf(Vector3(-5.5, 2.0, -4.5)), wall)
	_add("room_sphere", {"type": "sphere", "radius": 0.35}, _xf(Vector3(-5.5, 0.35, -6.2)), {"albedo": Color(0.8, 0.8, 0.8), "roughness": 0.5})

	# Grass patch: MultiMesh of crossed alpha-scissor cards (deterministic placement).
	_build_grass()

	# Moving box (ghosting / disocclusion) and rotating fan (thin fast motion).
	moving_box = _add("moving_box", {"type": "box", "size": Vector3(0.5, 0.5, 0.5)}, _xf(Vector3(0, 0.9, 0.9)), {"albedo": Color(0.85, 0.85, 0.85), "roughness": 0.3})
	fan = Node3D.new()
	fan.name = "fan"
	fan.position = Vector3(-1.8, 1.9, -4.3)
	add_child(fan)
	for i in range(4):
		_add("fan_blade_%d" % i, {"type": "box", "size": Vector3(1.6, 0.05, 0.02)}, _xf(Vector3.ZERO, Vector3(0, 0, i * 45.0)), {"albedo": Color(0.1, 0.1, 0.1), "roughness": 0.4}, fan)


func _build_grass() -> void:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var w := 0.25
	var hgt := 0.5
	for a in [0.0, PI / 2.0]:
		var d := Vector3(cos(a), 0, sin(a)) * w
		var nrm := Vector3(-sin(a), 0, cos(a))
		var v := [Vector3(-d.x, 0, -d.z), Vector3(d.x, 0, d.z), Vector3(d.x, hgt, d.z), Vector3(-d.x, hgt, -d.z)]
		var uv := [Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0)]
		for idx in [0, 1, 2, 0, 2, 3]:
			st.set_normal(nrm)
			st.set_uv(uv[idx])
			st.add_vertex(v[idx])
	var card := st.commit()
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.mesh = card
	mm.instance_count = 600
	var rng := RandomNumberGenerator.new()
	rng.seed = 42
	var xforms := []
	for i in range(600):
		var p := Vector3(rng.randf_range(2.6, 6.4), 0.0, rng.randf_range(0.6, 4.2))
		var s := rng.randf_range(0.7, 1.2)
		var xf := Transform3D(Basis(Vector3.UP, rng.randf_range(0.0, TAU)).scaled(Vector3(s, s, s)), p)
		mm.set_instance_transform(i, xf)
		xforms.append([xf.basis.x.x, xf.basis.y.x, xf.basis.z.x, xf.origin.x, xf.basis.x.y, xf.basis.y.y, xf.basis.z.y, xf.origin.y, xf.basis.x.z, xf.basis.y.z, xf.basis.z.z, xf.origin.z])
	var mmi := MultiMeshInstance3D.new()
	mmi.name = "grass"
	mmi.multimesh = mm
	var def := {"albedo": Color(1, 1, 1), "roughness": 0.7, "albedo_tex": "grass_albedo", "alpha": "scissor", "alpha_scissor": 0.5, "double_sided": true}
	mmi.material_override = _mat(def)
	add_child(mmi)
	desc_objects.append({"name": "grass", "mesh": {"type": "grass_cards", "width": w * 2.0, "height": hgt, "instances": xforms}, "material": _json_mat(def), "parent": ""})


# ------------------------------------------------------------------ lights

func _build_lights() -> void:
	var sun := DirectionalLight3D.new()
	sun.name = "sun"
	sun.light_color = SUN_COLOR.linear_to_srgb()
	sun.light_energy = SUN_ENERGY
	sun.rotation_degrees = Vector3(-SUN_ELEVATION, SUN_AZIMUTH, 0)
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 60.0
	sun.directional_shadow_blend_splits = true
	sun.light_angular_distance = 0.5
	apply_sun_overrides(sun)
	add_child(sun)
	var sdir := -sun.global_transform.basis.z
	desc_lights.append({"type": "sun", "color": [SUN_COLOR.r, SUN_COLOR.g, SUN_COLOR.b], "godot_energy": SUN_ENERGY, "irradiance": PI * SUN_ENERGY, "direction": [sdir.x, sdir.y, sdir.z], "angular_distance_deg": 0.5})
	if lighting != "ref":
		var omni := OmniLight3D.new()
		omni.name = "omni_warm"
		omni.position = Vector3(2.0, 1.2, -0.6)
		omni.light_color = Color(1.0, 0.8, 0.6)
		omni.light_energy = 4.0
		omni.omni_range = 6.0
		omni.shadow_enabled = true
		add_child(omni)
		var spot := SpotLight3D.new()
		spot.name = "spot_cool"
		spot.light_color = Color(0.7, 0.85, 1.0)
		spot.light_energy = 12.0
		spot.spot_range = 8.0
		spot.spot_angle = 30.0
		spot.shadow_enabled = true
		add_child(spot)
		spot.look_at_from_position(Vector3(-2.5, 3.0, 1.0), Vector3(-2.5, 0.0, -1.5), Vector3.UP)


# ------------------------------------------------------------------ cameras & animation

func _define_cameras() -> void:
	fixed_cameras = {
		"pbr_overview": {"pos": Vector3(0, 2.6, 7.5), "target": Vector3(0, 0.6, -1.5), "fov": 55.0, "time": 1.25, "purpose": "whole lab, subpixel wires, tonemapping range"},
		"pbr_spheres": {"pos": Vector3(0, 1.4, 3.4), "target": Vector3(0, 0.4, -1.3), "fov": 50.0, "time": 1.25, "purpose": "roughness/metallic sweeps, specular response"},
		"pbr_grazing": {"pos": Vector3(8.0, 0.6, 6.0), "target": Vector3(8.0, 0.0, -30.0), "fov": 60.0, "time": 1.25, "purpose": "high-frequency texture at grazing angles, anisotropic filtering, moire"},
		"pbr_dark_room": {"pos": Vector3(-4.8, 1.2, -1.2), "target": Vector3(-5.5, 0.8, -6.2), "fov": 55.0, "time": 1.25, "purpose": "dark indirect-only interior next to bright exterior"},
		"pbr_thin": {"pos": Vector3(1.5, 1.2, 3.6), "target": Vector3(1.4, 1.0, 0.8), "fov": 45.0, "time": 1.25, "purpose": "comb of thin cylinders, fence, alpha foliage"},
		"pbr_emissive": {"pos": Vector3(-3.2, 1.1, 2.8), "target": Vector3(-5.6, 0.6, 0.2), "fov": 50.0, "time": 1.25, "purpose": "HDR emissive bars, bloom spread"},
		"pbr_specular": {"pos": Vector3(4.3, 1.0, -0.8), "target": Vector3(5.4, 0.6, -2.6), "fov": 40.0, "time": 1.25, "purpose": "high-frequency normal map on glossy metal (specular aliasing)"},
	}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(0, 2.6, 7.5), Vector3(0, 0.6, -1.5), 55.0],
		[4.0, Vector3(5.5, 1.4, 5.0), Vector3(3.5, 0.4, 0.5), 55.0],
		[8.0, Vector3(7.5, 0.6, 3.0), Vector3(8.0, 0.0, -25.0), 60.0],
		[12.0, Vector3(4.3, 1.0, -0.6), Vector3(5.4, 0.6, -2.6), 45.0],
		[16.0, Vector3(0.0, 1.3, 3.2), Vector3(0.0, 0.4, -1.5), 50.0],
		[20.0, Vector3(-3.0, 1.1, 2.5), Vector3(-5.6, 0.6, 0.0), 50.0],
		[24.0, Vector3(-4.8, 1.2, -1.2), Vector3(-5.5, 0.8, -6.2), 55.0],
		[28.0, Vector3(-1.0, 3.0, 6.0), Vector3(0.0, 1.0, -3.0), 55.0],
		[30.0, Vector3(0, 2.6, 7.5), Vector3(0, 0.6, -1.5), 55.0],
	])
	# Temporal sequences (60 frames = 1 s each at 60 Hz virtual time).
	temporal_seqs = {
		"moving_object": {"start": 2.0, "frames": 60, "purpose": "static camera, box sweeping across spheres: ghosting / disocclusion trails",
			"path": CameraPath.from_array([[0.0, Vector3(0, 1.3, 4.2), Vector3(0, 0.7, -1.0), 50.0], [1.0, Vector3(0, 1.3, 4.2), Vector3(0, 0.7, -1.0), 50.0]])},
		"camera_pan": {"start": 0.0, "frames": 60, "purpose": "45 deg/s yaw pan over spheres, comb and fence: blur and history rejection under camera motion",
			"path": CameraPath.from_array([[0.0, Vector3(0, 1.5, 4.5), Vector3(-3.0, 0.6, 0.0), 55.0], [1.0, Vector3(0, 1.5, 4.5), Vector3(3.2, 0.6, 0.4), 55.0]])},
		"foliage_strafe": {"start": 0.0, "frames": 60, "purpose": "lateral strafe past alpha-scissor grass and fence: alpha shimmer, disocclusion",
			"path": CameraPath.from_array([[0.0, Vector3(2.0, 0.8, 5.5), Vector3(2.0, 0.3, 1.0), 50.0], [1.0, Vector3(6.5, 0.8, 5.5), Vector3(6.5, 0.3, 1.0), 50.0]])},
		"specular_orbit": {"start": 0.0, "frames": 60, "purpose": "slow orbit around ridged glossy sphere: specular shimmer",
			"path": CameraPath.from_array([[0.0, Vector3(3.8, 1.0, -1.2), Vector3(5.4, 0.6, -2.6), 40.0], [0.5, Vector3(4.5, 1.0, -0.8), Vector3(5.4, 0.6, -2.6), 40.0], [1.0, Vector3(5.5, 1.0, -0.6), Vector3(5.4, 0.6, -2.6), 40.0]])},
		"thin_dolly": {"start": 0.0, "frames": 60, "purpose": "slow dolly toward the comb and the subpixel wires: thin-geometry stability",
			"path": CameraPath.from_array([[0.0, Vector3(1.5, 1.4, 6.0), Vector3(1.0, 1.8, -10.0), 45.0], [1.0, Vector3(1.5, 1.4, 4.0), Vector3(1.0, 1.8, -10.0), 45.0]])},
	}


func set_time(t: float) -> void:
	if moving_box:
		moving_box.position = Vector3(3.2 * sin(TAU * t / 5.0), 0.9, 0.9)
		moving_box.rotation = Vector3(0, t * 0.8, 0)
	if fan:
		fan.rotation = Vector3(0, 0, t * PI * 0.5)


# ------------------------------------------------------------------ reference export (Cycles)

func _flat3(a) -> Array:
	var o := []
	if a == null:
		return o
	for v in a:
		o.append(snappedf(v.x, 0.000001)); o.append(snappedf(v.y, 0.000001)); o.append(snappedf(v.z, 0.000001))
	return o


func _flat2(a) -> Array:
	var o := []
	if a == null:
		return o
	for v in a:
		o.append(snappedf(v.x, 0.000001)); o.append(snappedf(v.y, 0.000001))
	return o


func _mesh_def_from_json(j: Dictionary) -> Dictionary:
	var md := j.duplicate()
	if md.has("size"):
		var sz: Array = md["size"]
		md["size"] = Vector2(sz[0], sz[1]) if sz.size() == 2 else Vector3(sz[0], sz[1], sz[2])
	return md


func _export_reference(dir: String) -> void:
	DirAccess.make_dir_recursive_absolute(dir)
	var tex_files := {}
	for k in tex_cache.keys():
		var img: Image = tex_cache[k]["img"].duplicate()
		img.clear_mipmaps()
		var p := dir.path_join(k + ".png")
		img.save_png(p)
		tex_files[k] = k + ".png"
	var cams := {}
	for k in fixed_cameras.keys():
		var c: Dictionary = fixed_cameras[k]
		cams[k] = {"pos": [c["pos"].x, c["pos"].y, c["pos"].z], "target": [c["target"].x, c["target"].y, c["target"].z], "fov_vertical_deg": c["fov"]}
	# World transforms of every exported object (evaluated at t = REF_TIME, the fixed-camera time).
	for o in desc_objects:
		var node: Node3D = null
		if o["name"] == "grass":
			node = get_node("grass")
		elif o["parent"] != "":
			node = get_node(NodePath(o["parent"] + "/" + o["name"]))
		else:
			node = get_node(NodePath(o["name"]))
		var xf := node.global_transform
		o["world"] = [xf.basis.x.x, xf.basis.y.x, xf.basis.z.x, xf.origin.x, xf.basis.x.y, xf.basis.y.y, xf.basis.z.y, xf.origin.y, xf.basis.x.z, xf.basis.y.z, xf.basis.z.z, xf.origin.z]
	# Exact triangle data of every unique mesh (so Cycles renders identical geometry / UVs / normals).
	var meshes := {}
	for o in desc_objects:
		var key := JSON.stringify(o["mesh"]) if o["mesh"]["type"] != "grass_cards" else "grass_card"
		o["mesh_key"] = key.md5_text()
		if meshes.has(o["mesh_key"]):
			continue
		var m: Mesh
		if o["name"] == "grass":
			m = (get_node("grass") as MultiMeshInstance3D).multimesh.mesh
		else:
			m = _mesh_from_def(_mesh_def_from_json(o["mesh"]))
		var arr := m.surface_get_arrays(0)
		meshes[o["mesh_key"]] = {
			"positions": _flat3(arr[Mesh.ARRAY_VERTEX]),
			"normals": _flat3(arr[Mesh.ARRAY_NORMAL]),
			"uvs": _flat2(arr[Mesh.ARRAY_TEX_UV]),
			"indices": Array(arr[Mesh.ARRAY_INDEX]) if arr[Mesh.ARRAY_INDEX] else [],
		}
	var mf := FileAccess.open(dir.path_join("pbr_lab_meshes.json"), FileAccess.WRITE)
	mf.store_string(JSON.stringify(meshes))
	mf.close()
	var d := {
		"schema": 1,
		"meshes_file": "pbr_lab_meshes.json",
		"winding": "Godot front faces are clockwise when viewed from the front (flip for Blender)",
		"ref_time": REF_TIME,
		"units": "meters, Y-up (Godot). Blender importer converts to Z-up.",
		"colors": "linear",
		"sky_radiance": [SKY_REF_RADIANCE.r, SKY_REF_RADIANCE.g, SKY_REF_RADIANCE.b],
		"lights": desc_lights,
		"objects": desc_objects,
		"textures": tex_files,
		"cameras": cams,
		"notes": "Godot directional light energy E -> irradiance PI*E. Emission radiance = color*energy. Diffuse model Lambert.",
	}
	var f := FileAccess.open(dir.path_join("pbr_lab_scene.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(d, "  "))
	f.close()
