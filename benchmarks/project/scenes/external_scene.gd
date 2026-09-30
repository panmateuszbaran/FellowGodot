extends "res://bench/scene_base.gd"
## Shared helpers for scenes built from external (downloaded, imported) assets.

var content_root: Node3D
var aabb := AABB()
var parts_loaded := []


func load_part(path: String) -> Node3D:
	if not ResourceLoader.exists(path):
		load_error = "missing asset %s - run benchmarks/scripts/fetch_*.py and import_assets.py" % path
		return null
	var ps = load(path)
	if ps == null or not (ps is PackedScene):
		load_error = "could not load %s" % path
		return null
	var inst: Node3D = ps.instantiate()
	content_root.add_child(inst)
	parts_loaded.append(path)
	return inst


func begin_content() -> void:
	content_root = Node3D.new()
	content_root.name = "Content"
	add_child(content_root)


func compute_aabb(n: Node) -> void:
	aabb = AABB()
	var first := [true]
	_aabb_rec(n, first)


func _aabb_rec(n: Node, first: Array) -> void:
	if n is VisualInstance3D and not (n is Light3D):
		var vi := n as VisualInstance3D
		var bb := vi.global_transform * vi.get_aabb()
		if first[0]:
			aabb = bb
			first[0] = false
		else:
			aabb = aabb.merge(bb)
	for c in n.get_children():
		_aabb_rec(c, first)


## Collects imported lights / cameras / animation players (for the probe listing and to disable them).
func collect(n: Node, out: Dictionary) -> void:
	if n is Light3D:
		out["lights"].append({"name": str(n.name), "type": n.get_class(), "energy": n.light_energy, "pos": _v(n.global_position), "range": n.get("omni_range") if n is OmniLight3D else (n.get("spot_range") if n is SpotLight3D else 0.0), "shadow": n.shadow_enabled})
	elif n is Camera3D and n != camera:
		out["cameras"].append({"name": str(n.name), "pos": _v(n.global_position), "fwd": _v(-n.global_transform.basis.z), "fov": n.fov})
	elif n is AnimationPlayer:
		out["animation_players"].append({"name": str(n.name), "animations": Array(n.get_animation_list())})
	for c in n.get_children():
		collect(c, out)


func _v(v: Vector3) -> Array:
	return [snappedf(v.x, 0.001), snappedf(v.y, 0.001), snappedf(v.z, 0.001)]


func probe() -> Dictionary:
	var out := {"lights": [], "cameras": [], "animation_players": []}
	collect(content_root, out)
	out["aabb"] = {"position": _v(aabb.position), "size": _v(aabb.size)}
	out["stats"] = scene_stats(content_root)
	return out


## Standard outdoor lighting rig: physical sky + sun (shadowed). Returns the sun.
func add_sky_and_sun(elevation_deg: float, azimuth_deg: float, sun_energy: float, max_shadow_distance: float) -> DirectionalLight3D:
	var sky := Sky.new()
	var mat := PhysicalSkyMaterial.new()
	mat.energy_multiplier = 1.0
	mat.turbidity = 8.0
	mat.mie_coefficient = 0.004
	sky.sky_material = mat
	sky.radiance_size = Sky.RADIANCE_SIZE_256
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	var sun := DirectionalLight3D.new()
	sun.name = "BenchSun"
	sun.rotation_degrees = Vector3(-elevation_deg, azimuth_deg, 0)
	sun.light_energy = sun_energy
	sun.light_color = Color(1.0, 0.95, 0.88)
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = max_shadow_distance
	sun.directional_shadow_blend_splits = true
	sun.light_angular_distance = 0.5
	apply_sun_overrides(sun)
	add_child(sun)
	return sun


func default_post(auto_exposure: bool, exposure: float) -> void:
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.tonemap_exposure = exposure
	env.glow_enabled = true
	env.ssao_enabled = true
	env.ssao_radius = 1.0
	env.ssao_intensity = 2.0
	env.ssil_enabled = true
	env.ssr_enabled = true
	env.ssr_max_steps = 64
	env.sdfgi_enabled = false
	cam_attr.auto_exposure_enabled = auto_exposure
	cam_attr.auto_exposure_scale = 0.4
	cam_attr.auto_exposure_speed = 1.0
	cam_attr.auto_exposure_min_sensitivity = 50.0
	cam_attr.auto_exposure_max_sensitivity = 800.0


## Removes imported lights whose energy is 0 (export artefacts: they still cost clustered-light work).
func remove_dead_lights(n: Node, removed: Array) -> void:
	for c in n.get_children():
		remove_dead_lights(c, removed)
	if n is Light3D and is_zero_approx(n.light_energy):
		removed.append(str(n.name))
		n.get_parent().remove_child(n)
		n.queue_free()


## Makes the bench camera current and disables imported cameras.
func take_camera() -> void:
	_disable_cams(content_root)
	camera.make_current()


func _disable_cams(n: Node) -> void:
	if n is Camera3D:
		n.current = false
	for c in n.get_children():
		_disable_cams(c)


func find_first(n: Node, cls: String) -> Node:
	if n.is_class(cls):
		return n
	for c in n.get_children():
		var r := find_first(c, cls)
		if r:
			return r
	return null


## Samples an imported camera animation into a deterministic camera path (then stops the player).
func path_from_animation(ap: AnimationPlayer, anim_name: String, cam: Camera3D, keys: int, fov: float, speed: float = 1.0):
	var anim := ap.get_animation(anim_name)
	var length := anim.length
	var arr := []
	ap.play(anim_name)
	for i in range(keys + 1):
		var t := length * i / keys
		ap.seek(t, true)
		var xf := cam.global_transform
		arr.append([t / speed, xf.origin, xf.origin - xf.basis.z * 10.0, fov])
	ap.stop()
	ap.seek(0.0, true)
	return CameraPath.from_array(arr)
