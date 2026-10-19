extends "res://scenes/external_scene.gd"
## BENCHMARK B - Intel "New Sponza" (Intel Graphics Research Samples, CC BY 4.0).
## Parts: main (always) + curtains + ivy + trees (--parts=main,curtains,ivy,trees).
## Lighting: physical sky + shadowed sun through the open roof, SSAO + SSIL + SSR, subtle volumetric fog,
## AgX tonemapping with auto exposure (bright atrium vs dark galleries).

const PARTS := {
	"main": "res://external/sponza/main/NewSponza_Main_glTF_003.gltf",
	"curtains": "res://external/sponza/curtains/NewSponza_Curtains_glTF.gltf",
	"ivy": "res://external/sponza/ivy/NewSponza_IvyGrowth_glTF.gltf",
	"trees": "res://external/sponza/trees/NewSponza_CypressTree_glTF.gltf",
}


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "sponza"
	_setup_common()
	begin_content()
	# --parts=a,b or a+b (the runner's --builds list is comma-separated)
	var parts: PackedStringArray = str(args.get("parts", "main,curtains,ivy,trees")).replace("+", ",").split(",", false)
	for p in parts:
		if PARTS.has(p):
			load_part(PARTS[p])
			if load_error != "":
				return
	compute_aabb(content_root)
	var pr := probe()
	var removed := []
	remove_dead_lights(content_root, removed)
	take_camera()
	add_sky_and_sun(62.0, -28.0, 3.0, 60.0)
	default_post(true, 1.0)
	env.volumetric_fog_enabled = true
	env.volumetric_fog_density = 0.006
	env.volumetric_fog_anisotropy = 0.6
	env.volumetric_fog_length = 64.0
	_define_cameras()
	info = {"parts": Array(parts), "probe": pr, "assets": parts_loaded, "removed_zero_energy_lights": removed,
		"camera_source": "sponza_cam_01..06 are the artist cameras PhysCamera001..006 shipped in the glTF"}


func _p(fx: float, fy: float, fz: float) -> Vector3:
	# Point from normalised coordinates inside the scene AABB (0..1 per axis).
	return aabb.position + aabb.size * Vector3(fx, fy, fz)


func _cam_from_import(cam_name: String, purpose: String) -> Dictionary:
	var c := content_root.find_child(cam_name, true, false) as Camera3D
	if c == null:
		return {}
	var xf := c.global_transform
	return {"pos": xf.origin, "target": xf.origin - xf.basis.z * 10.0, "fov": c.fov, "time": 0.0, "purpose": purpose}


func _define_cameras() -> void:
	var defs := {
		"sponza_cam_01": ["PhysCamera001", "ground level, long axis: bright atrium vs dark arcades, distant geometry"],
		"sponza_cam_02": ["PhysCamera002", "upper floor, looking back along the atrium: curtains, ivy, draw distance"],
		"sponza_cam_03": ["PhysCamera003", "columns with fine carved detail, high-frequency textures (narrow FOV)"],
		"sponza_cam_04": ["PhysCamera004", "upper gallery, shadowed corridor, cloth / alpha geometry"],
		"sponza_cam_05": ["PhysCamera005", "low camera looking up: bright sky opening, reflective floor, specular"],
		"sponza_cam_06": ["PhysCamera006", "top-down view into the courtyard from the roof level"],
	}
	fixed_cameras = {}
	for k in defs.keys():
		var d := _cam_from_import(defs[k][0], defs[k][1])
		if not d.is_empty():
			fixed_cameras[k] = d
	flythrough = CameraPath.from_array([
		[0.0, Vector3(-9.0, 1.7, 0.0), Vector3(10.0, 2.5, 0.0), 60.0],
		[6.0, Vector3(-2.0, 1.7, 1.5), Vector3(5.0, 3.0, -3.0), 60.0],
		[12.0, Vector3(5.0, 1.7, -1.5), Vector3(12.0, 2.0, 2.0), 60.0],
		[18.0, Vector3(10.0, 2.0, 0.0), Vector3(0.0, 6.0, 0.0), 60.0],
		[24.0, Vector3(6.0, 5.0, 0.0), Vector3(-8.0, 6.0, 3.0), 60.0],
		[30.0, Vector3(-2.0, 8.0, 0.0), Vector3(-9.0, 7.0, 5.0), 60.0],
		[36.0, Vector3(-6.0, 4.0, 0.0), Vector3(8.0, 1.0, 0.0), 60.0],
		[42.0, Vector3(-9.0, 1.7, 0.0), Vector3(10.0, 2.5, 0.0), 60.0],
	])
	temporal_seqs = {
		"sponza_pan": {"start": 0.0, "frames": 60, "purpose": "45 deg/s pan across columns, curtains and ivy",
			"path": CameraPath.from_array([[0.0, Vector3(-4.0, 1.7, 0.0), Vector3(6.0, 2.5, -8.0), 60.0], [1.0, Vector3(-4.0, 1.7, 0.0), Vector3(6.0, 2.5, 8.0), 60.0]])},
	}
