extends "res://scenes/external_scene.gd"
## BENCHMARK C - Amazon Lumberyard Bistro v5.2 (NVIDIA ORCA, CC BY 4.0).
##   --scene=bistro_exterior : BistroExterior.fbx, day lighting (HDR panorama sky shipped with the asset + sun)
##   --scene=bistro_interior : BistroInterior.fbx, imported analytic lights + dim exterior sky
## Materials are rebuilt from the ORCA texture convention (README.txt of the asset):
##   *_BaseColor (RGB albedo, A opacity) / *_Specular (R occlusion, G roughness, B metalness) / *_Normal (DirectX,
##   flipped at import) / *_Emissive. This is deterministic and identical for stock and FellowEngine.

const EXTERIOR := "res://external/bistro/BistroExterior.fbx"
const INTERIOR := "res://external/bistro/BistroInterior.fbx"
const TEX_DIR := "res://external/bistro/Textures/"
const TEX_GL_DIR := "res://external/bistro/TexturesGL/"  # BC5 normal maps converted to the OpenGL convention
const HDRI := "res://external/bistro/san_giuseppe_bridge_4k.hdr"
const ALPHA_HINTS := ["leaf", "leaves", "foliage", "plant", "ivy", "grass", "flower", "bush", "tree", "hedge", "fern", "vine", "branch", "shrub", "palm", "chain", "fence", "lattice", "net", "string", "flag", "wire", "banner"]

var interior := false
var material_report := {"rebuilt": 0, "alpha_scissor": [], "emissive": [], "missing_textures": 0, "kept": 0}
var _mat_cache := {}
var info_path := {}


func build(p_args: Dictionary) -> void:
	args = p_args
	interior = str(args.get("scene", "")) == "bistro_interior"
	bench_name = "bistro_interior" if interior else "bistro_exterior"
	_setup_common()
	begin_content()
	var root := load_part(INTERIOR if interior else EXTERIOR)
	if load_error != "":
		return
	_fix_materials(root)
	compute_aabb(content_root)
	var pr := probe()
	take_camera()
	_adjust_imported_lights(content_root)
	if interior:
		_setup_interior_lighting()
	else:
		_setup_exterior_lighting()
	_define_cameras(pr)
	info = {"variant": bench_name, "probe": pr, "materials": material_report, "assets": parts_loaded, "light_changes": light_changes, "flythrough_source": info_path}


# ------------------------------------------------------------------ materials

func _tex(base: String, suffix: String) -> Texture2D:
	var dirs := [TEX_GL_DIR, TEX_DIR] if suffix == "_Normal" else [TEX_DIR]
	for d in dirs:
		for ext in [".dds", ".DDS", ".tga", ".TGA"]:
			var p: String = d + base + suffix + ext
			if ResourceLoader.exists(p):
				if suffix == "_Normal" and d == TEX_DIR and ext.to_lower() == ".dds":
					material_report["normals_without_gl_conversion"] = material_report.get("normals_without_gl_conversion", 0) + 1
				return load(p)
	return null


func _base_name(mat: Material) -> String:
	if mat is BaseMaterial3D and mat.albedo_texture:
		var f: String = mat.albedo_texture.resource_path.get_file().get_basename()
		for suf in ["_BaseColor", "_basecolor", "_Diffuse", "_diffuse"]:
			if f.ends_with(suf):
				return f.substr(0, f.length() - suf.length())
		return f
	return ""


func _fix_materials(n: Node) -> void:
	if n is MeshInstance3D and n.mesh:
		var mi := n as MeshInstance3D
		for i in range(mi.mesh.get_surface_count()):
			var m := mi.get_active_material(i)
			var nm := _rebuild(m)
			if nm != m:
				mi.set_surface_override_material(i, nm)
	for c in n.get_children():
		_fix_materials(c)


func _rebuild(m: Material) -> Material:
	if m == null:
		return m
	var key := m.get_instance_id()
	if _mat_cache.has(key):
		return _mat_cache[key]
	var base := _base_name(m)
	if base == "":
		material_report["kept"] += 1
		_mat_cache[key] = m
		return m
	var orm := ORMMaterial3D.new()
	orm.resource_name = m.resource_name
	var albedo := _tex(base, "_BaseColor")
	var spec := _tex(base, "_Specular")
	var normal := _tex(base, "_Normal")
	var emis := _tex(base, "_Emissive")
	if albedo == null:
		albedo = (m as BaseMaterial3D).albedo_texture
	orm.albedo_texture = albedo
	if spec:
		orm.orm_texture = spec
	else:
		material_report["missing_textures"] += 1
		orm.roughness = 0.7
		orm.metallic = 0.0
	if normal:
		orm.normal_enabled = true
		orm.normal_texture = normal
	if emis:
		orm.emission_enabled = true
		orm.emission_texture = emis
		orm.emission = Color(1, 1, 1)
		orm.emission_energy_multiplier = 4.0
		material_report["emissive"].append(base)
	var lname := (base + " " + m.resource_name).to_lower()
	var is_alpha := false
	for h in ALPHA_HINTS:
		if lname.contains(h):
			is_alpha = true
			break
	if m is BaseMaterial3D and (m as BaseMaterial3D).transparency != BaseMaterial3D.TRANSPARENCY_DISABLED:
		is_alpha = true
	if is_alpha:
		orm.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
		orm.alpha_scissor_threshold = 0.5
		orm.alpha_antialiasing_mode = BaseMaterial3D.ALPHA_ANTIALIASING_OFF
		orm.cull_mode = BaseMaterial3D.CULL_DISABLED
		material_report["alpha_scissor"].append(base)
	if lname.contains("glass") or lname.contains("window"):
		orm.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		orm.albedo_color = Color(1, 1, 1, 0.35)
	orm.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
	material_report["rebuilt"] += 1
	_mat_cache[key] = orm
	return orm


# ------------------------------------------------------------------ lighting

## Imported ORCA/Falcor light intensities are in Falcor units. Exterior: the imported sun (energy 150) is
## disabled and replaced by the benchmark sun. Interior: omni energies are scaled by 0.02 and their range is
## bounded to 15 m (imported range is effectively infinite), shadows enabled. Recorded in results.
var light_changes := []


func _adjust_imported_lights(n: Node) -> void:
	for c in n.get_children():
		_adjust_imported_lights(c)
	if n is DirectionalLight3D:
		n.visible = false
		light_changes.append({"light": str(n.name), "action": "disabled imported directional light", "energy": n.light_energy})
	elif n is OmniLight3D or n is SpotLight3D:
		var before: float = n.light_energy
		n.light_energy = before * 0.02
		if n is OmniLight3D:
			n.omni_range = min(n.omni_range, 15.0)
		else:
			n.spot_range = min(n.spot_range, 15.0)
		n.shadow_enabled = true
		light_changes.append({"light": str(n.name), "action": "energy x0.02, range<=15, shadows on", "energy_before": before})

func _hdri_sky(energy: float) -> void:
	var sky := Sky.new()
	if ResourceLoader.exists(HDRI):
		var pano := PanoramaSkyMaterial.new()
		pano.panorama = load(HDRI)
		pano.energy_multiplier = energy
		sky.sky_material = pano
	else:
		sky.sky_material = PhysicalSkyMaterial.new()
	sky.radiance_size = Sky.RADIANCE_SIZE_256
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY


func _setup_exterior_lighting() -> void:
	_hdri_sky(1.0)
	var sun := DirectionalLight3D.new()
	sun.name = "BenchSun"
	sun.rotation_degrees = Vector3(-38.0, 125.0, 0)
	sun.light_energy = 3.0
	sun.light_color = Color(1.0, 0.94, 0.85)
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 120.0
	sun.directional_shadow_blend_splits = true
	sun.light_angular_distance = 0.5
	add_child(sun)
	default_post(true, 1.0)
	env.volumetric_fog_enabled = true
	env.volumetric_fog_density = 0.003
	env.volumetric_fog_anisotropy = 0.5
	env.volumetric_fog_length = 96.0


func _setup_interior_lighting() -> void:
	_hdri_sky(0.5)
	default_post(true, 1.0)
	env.volumetric_fog_enabled = false


# ------------------------------------------------------------------ cameras

func _p(fx: float, fy: float, fz: float) -> Vector3:
	return aabb.position + aabb.size * Vector3(fx, fy, fz)


func _define_cameras(_pr: Dictionary) -> void:
	var ap := find_first(content_root, "AnimationPlayer") as AnimationPlayer
	var cam := find_first(content_root, "Camera3D") as Camera3D
	var anim_ok := ap != null and cam != null and ap.has_animation("Take 001")
	if anim_ok:
		# The embedded ORCA camera path (Falcor "Take 001") is the deterministic flythrough.
		flythrough = path_from_animation(ap, "Take 001", cam, 60, 55.0, float(args.get("path_speed", "1.0")))
		info_path = {"source": "embedded FBX camera animation 'Take 001'", "length_s": flythrough.duration()}
	if interior:
		fixed_cameras = {
			"bistro_int_cam_01": {"pos": Vector3(0.05, 1.45, -1.43), "target": Vector3(10.0, 1.1, -2.6), "fov": 55.0, "time": 0.0, "purpose": "embedded camera start: dining room toward the bar, many props"},
		}
	else:
		fixed_cameras = {
			"bistro_ext_cam_01": {"pos": Vector3(14.61, 4.29, -41.81), "target": Vector3(11.43, 3.60, -32.36), "fov": 55.0, "time": 0.0, "purpose": "embedded camera start: street, facades, vegetation"},
		}
	if not anim_ok:
		var a: Dictionary = fixed_cameras.values()[0]
		flythrough = CameraPath.from_array([[0.0, a["pos"], a["target"], 55.0], [30.0, a["pos"] + Vector3(0, 0, 10), a["target"] + Vector3(0, 0, 10), 55.0]])
	# Additional fixed cameras = poses of the embedded path at fixed fractions (deterministic, documented).
	var fr := {
		"bistro_ext_cam_02": [0.30, "street corner, restaurant facade, string lights, alpha foliage in planters"],
		"bistro_ext_cam_03": [0.55, "small square with the large tree: vegetation, long shadows"],
		"bistro_ext_cam_04": [0.80, "arcade / covered passage: indoor-outdoor transition, distant geometry"],
	} if not interior else {
		"bistro_int_cam_02": [0.35, "bar counter: many small props, bottles, glass"],
		"bistro_int_cam_03": [0.65, "shelves: high-frequency detail, thin geometry"],
		"bistro_int_cam_04": [0.90, "window side: bright exterior seen from the dark interior"],
	}
	for k in fr.keys():
		var t: float = flythrough.duration() * fr[k][0]
		var d: Dictionary = flythrough.sample(t)
		var xf: Transform3D = d["xform"]
		fixed_cameras[k] = {"pos": xf.origin, "target": xf.origin - xf.basis.z * 10.0, "fov": 55.0, "time": 0.0, "purpose": fr[k][1] + " (embedded path at %d%%)" % int(fr[k][0] * 100)}
	# Temporal sequence: one second of the embedded path (real camera motion through detailed geometry).
	var t0: float = flythrough.duration() * (0.55 if not interior else 0.35)
	var keys := []
	for i in range(5):
		var d: Dictionary = flythrough.sample(t0 + i * 0.25)
		var xf: Transform3D = d["xform"]
		keys.append([i * 0.25, xf.origin, xf.origin - xf.basis.z * 10.0, 55.0])
	temporal_seqs = {("bistro_ext_path" if not interior else "bistro_int_path"): {"start": 0.0, "frames": 60, "purpose": "1 s of the embedded camera path (vegetation / props under camera motion)", "path": CameraPath.from_array(keys)}}
