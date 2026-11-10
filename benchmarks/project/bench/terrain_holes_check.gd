extends SceneTree
## FellowEngine milestone 64 check (decision 0172): a FellowTerrain3D's holes, its foliage fading out with distance and
## its trees' impostors baked into resources.
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 640x360 -s res://bench/terrain_holes_check.gd
##       -- --out=<dir> [--part=holes|fade|impostors|all] [--frames=16]
##
## holes: a hole cut with paint_holes() looked at from above over a red sky — the share of the hole's disc where the
##   sky shows through (the ground and its grass gone), red pixels elsewhere (none: no cracks); is_hole() and
##   get_height() at its centre; a ball dropped over the hole falls through it, one beside it rests on the ground.
## fade: grass to 40 m looked at along the ground with fade_distance 0 and 15 (--fades=0,15): the pictures and the grass's
##   share of each band of 8 rows from the horizon down (the far bands thin out as the instances shrink).
## impostors: trees to 400 m with an impostor distance of 50 m: their impostors baked into <out>/impostors, loaded back
##   from the files, and the view with them and without (the triangles drawn).
## One JSON line: TERRAIN_HOLES_CHECK {...}.

var args := {}
var out_dir := ""
var part := "all"
var frames := 16
var result := {}
var root3d: Node3D
var terrain
var cam: Camera3D
var env: WorldEnvironment
var sun: DirectionalLight3D
const N := 257


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://terrain_holes_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	part = _arg("part", "all")
	if _arg("debug_stats", "0") == "1":
		ProjectSettings.set_setting("rendering/fellow/debug/multimesh_stats", true)
	frames = int(_arg("frames", "16"))
	_run.call_deferred()


func _make_world(sky_red: bool) -> void:
	root3d = Node3D.new()
	root.add_child(root3d)
	env = WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(1, 0, 0) if sky_red else Color(0.55, 0.7, 0.9)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.35, 0.38, 0.42)
	root3d.add_child(env)
	sun = DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 35, 0)
	sun.shadow_enabled = true
	root3d.add_child(sun)
	var noise := FastNoiseLite.new()
	noise.seed = 64
	noise.frequency = 0.01
	var h := PackedFloat32Array()
	h.resize(N * N)
	for z in N:
		for x in N:
			h[z * N + x] = noise.get_noise_2d(x, z) * 4.0
	terrain = ClassDB.instantiate("FellowTerrain3D")
	var layer = ClassDB.instantiate("FellowTerrainLayer")
	layer.albedo_color = Color(0.35, 0.5, 0.25)
	terrain.layers = [layer]
	terrain.heightmap = Image.create_from_data(N, N, false, Image.FORMAT_RF, h.to_byte_array())
	root3d.add_child(terrain)
	cam = Camera3D.new()
	cam.far = 1000.0
	root3d.add_child(cam)
	cam.make_current()


func _free_world() -> void:
	root3d.queue_free()
	await process_frame
	await process_frame


func _grass_mesh() -> Mesh:
	var m := BoxMesh.new()
	m.size = Vector3(0.12, 0.7, 0.12)
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.2, 0.75, 0.15)
	m.material = mat
	return m


func _tree_mesh() -> Mesh:
	# A trunk and a crown in one mesh of two surfaces (a baked impostor shows both).
	var trunk := CylinderMesh.new()
	trunk.top_radius = 0.25
	trunk.bottom_radius = 0.35
	trunk.height = 3.0
	trunk.radial_segments = 16
	var crown := SphereMesh.new()
	crown.radius = 2.0
	crown.height = 4.5
	crown.radial_segments = 32
	crown.rings = 16
	var am := ArrayMesh.new()
	var a: Array = trunk.get_mesh_arrays()
	var v: PackedVector3Array = a[Mesh.ARRAY_VERTEX]
	for i in v.size():
		v[i] += Vector3(0, 1.5, 0)
	a[Mesh.ARRAY_VERTEX] = v
	am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, a)
	var b: Array = crown.get_mesh_arrays()
	var w: PackedVector3Array = b[Mesh.ARRAY_VERTEX]
	for i in w.size():
		w[i] += Vector3(0, 4.8, 0)
	b[Mesh.ARRAY_VERTEX] = w
	am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, b)
	var m0 := StandardMaterial3D.new()
	m0.albedo_color = Color(0.4, 0.27, 0.15)
	var m1 := StandardMaterial3D.new()
	m1.albedo_color = Color(0.15, 0.42, 0.12)
	am.surface_set_material(0, m0)
	am.surface_set_material(1, m1)
	am.resource_name = "tree"
	return am


func _settle(n: int) -> void:
	for i in n:
		await process_frame


# Until the terrain's foliage cells have all been made (their worker tasks can lag behind lavapipe's frames): the same
# instance count for 10 frames.
func _wait_foliage() -> void:
	var last := -1
	var same := 0
	for i in 900:
		await process_frame
		var n: int = terrain.get_stats().get("foliage_instances", 0)
		same = same + 1 if n == last and n > 0 else 0
		last = n
		if same >= 10:
			return


func _render_ms(n: int) -> Dictionary:
	var vp := root.get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(vp, true)
	await _settle(4)
	var cpu := 0.0
	var gpu := 0.0
	for i in n:
		await RenderingServer.frame_post_draw
		cpu += RenderingServer.viewport_get_measured_render_time_cpu(vp)
		gpu += RenderingServer.viewport_get_measured_render_time_gpu(vp)
	return {"cpu_ms": snappedf(cpu / n, 0.01), "gpu_ms": snappedf(gpu / n, 0.01)}


func _shot(name: String) -> Image:
	await RenderingServer.frame_post_draw
	var img := root.get_texture().get_image()
	img.save_png(out_dir.path_join(name + ".png"))
	return img


func _run() -> void:
	if part == "holes" or part == "all":
		await _holes()
	if part == "fade" or part == "all":
		await _fade()
	if part == "impostors" or part == "all":
		await _impostors()
	print("TERRAIN_HOLES_CHECK " + JSON.stringify(result))
	quit()


func _holes() -> void:
	_make_world(true)
	var grass = ClassDB.instantiate("FellowTerrainFoliage")
	grass.mesh = _grass_mesh()
	grass.density = 3.0
	grass.range = 60.0
	grass.cast_shadows = false
	terrain.foliage = [grass]
	var centre := Vector3(128, 0, 128)
	var radius := 8.0
	terrain.paint_holes(centre, radius, true)
	# A hole cut and filled again (blit_hole_region, as the editor's undo): none left there.
	terrain.paint_holes(Vector3(60, 0, 60), 4.0, true)
	var empty := Image.create(12, 12, false, Image.FORMAT_L8)
	terrain.blit_hole_region(empty, Vector2i(54, 54))
	cam.position = Vector3(128, 45, 128)
	cam.rotation_degrees = Vector3(-90, 0, 0)
	cam.fov = 40.0
	await _wait_foliage()
	await _settle(frames)
	var img := await _shot("holes_top")
	# The hole's disc on screen: the camera looks straight down from 45 m, the ground about 0 m high.
	var w := img.get_width()
	var hgt := img.get_height()
	var half_h := tan(deg_to_rad(cam.fov * 0.5)) * 45.0 # metres from the centre to the picture's top edge
	var px_per_m := (hgt * 0.5) / half_h
	var r_px := (radius - 1.5) * px_per_m # inside the disc, away from its rim (the triangles touching it)
	var in_disc := 0
	var red_in_disc := 0
	var red_outside := 0
	for y in hgt:
		for x in w:
			var c := img.get_pixel(x, y)
			var red := c.r > 0.9 and c.g < 0.1 and c.b < 0.1
			var d := Vector2(x - w * 0.5, y - hgt * 0.5).length()
			if d < r_px:
				in_disc += 1
				if red:
					red_in_disc += 1
			elif d > (radius + 1.5) * px_per_m and red:
				red_outside += 1
	var stats: Dictionary = terrain.get_stats()
	result["holes"] = {
		"disc_pixels": in_disc,
		"sky_share_in_disc": float(red_in_disc) / maxf(1.0, in_disc),
		"red_pixels_outside": red_outside,
		"is_hole_centre": terrain.is_hole(centre),
		"is_hole_filled": terrain.is_hole(Vector3(60, 0, 60)),
		"height_centre_nan": is_nan(terrain.get_height(centre)),
		"height_beside": terrain.get_height(centre + Vector3(radius + 3.0, 0, 0)),
		"hole_vertices": stats.get("hole_vertices", -1),
		"hole_patches": stats.get("hole_patches", -1),
		"patches": stats.get("patches", -1),
		"foliage_instances": stats.get("foliage_instances", -1),
	}
	# Two balls: over the hole, and beside it.
	var balls := []
	for p in [centre, centre + Vector3(radius + 6.0, 0, 0)]:
		var body := RigidBody3D.new()
		var shape := CollisionShape3D.new()
		shape.shape = SphereShape3D.new()
		shape.shape.radius = 0.5
		body.add_child(shape)
		root3d.add_child(body)
		body.global_position = p + Vector3(0, 6, 0)
		balls.append(body)
	for i in 180:
		await physics_frame
	var ground_beside: float = terrain.get_height(balls[1].global_position)
	result["holes"]["ball_over_hole_y"] = balls[0].global_position.y
	result["holes"]["ball_beside_y"] = balls[1].global_position.y
	result["holes"]["ball_beside_over_ground"] = balls[1].global_position.y - ground_beside
	result["holes"]["physics_engine"] = ProjectSettings.get_setting("physics/3d/physics_engine")
	# From the side: the hole's edge and the ground below it (the red sky through it).
	cam.position = centre + Vector3(0, 9, 22)
	cam.rotation_degrees = Vector3(-24, 0, 0)
	cam.fov = 60.0
	await _settle(8)
	await _shot("holes_side")
	await _free_world()


func _fade() -> void:
	_make_world(false)
	var grass = ClassDB.instantiate("FellowTerrainFoliage")
	grass.mesh = _grass_mesh()
	grass.density = 2.0
	grass.range = 40.0
	grass.cast_shadows = false
	terrain.foliage = [grass]
	var p := Vector3(128, 0, 200)
	cam.position = p + Vector3(0, terrain.get_height(p) + 1.7, 0)
	cam.rotation_degrees = Vector3(-6, 0, 0)
	cam.fov = 60.0
	result["fade"] = {}
	var i := 0
	for fade_text in _arg("fades", "0,15").split(","):
		var fade := float(fade_text)
		grass.fade_distance = fade
		await _wait_foliage()
		await _settle(frames)
		var img := await _shot("fade_%d_%d" % [i, int(fade)])
		# The grass's share of each band of 8 rows from the horizon down (TAA's jitter moves single pixels, not this);
		# rows of a 360-row picture, scaled to the picture's height.
		var bands := []
		var k := img.get_height() / 360.0
		var band := maxi(1, int(round(8 * k)))
		for b in 11:
			var y0 := int(round((144 + 8 * b) * k))
			var grass_px := 0
			for y in range(y0, y0 + band):
				for x in img.get_width():
					var c := img.get_pixel(x, y)
					if c.g > c.r * 1.8 and c.g > 0.43:
						grass_px += 1
			bands.append(snappedf(float(grass_px) / (band * img.get_width()), 0.01))
		result["fade"]["fade_%d_%d" % [i, int(fade)]] = {
			"grass_share_by_band": bands,
			"foliage_instances": terrain.get_stats().get("foliage_instances", -1),
		}
		i += 1
	await _free_world()


func _impostors() -> void:
	_make_world(false)
	var trees = ClassDB.instantiate("FellowTerrainFoliage")
	trees.mesh = _tree_mesh()
	trees.density = 0.004
	trees.range = 400.0
	trees.impostor_distance = 50.0
	trees.fade_distance = float(_arg("tree_fade", "30"))
	terrain.foliage = [trees]
	terrain.name = "Check"
	var p := Vector3(128, 0, 250)
	cam.position = p + Vector3(0, terrain.get_height(p) + 12.0, 0)
	cam.rotation_degrees = Vector3(-8, 0, 0)
	cam.fov = 60.0
	await _settle(4)
	var dir := out_dir.path_join("impostors")
	var t0 := Time.get_ticks_msec()
	var queued: int = terrain.bake_foliage_impostors(dir, 8, 128)
	var baked: Array = []
	if queued > 0:
		baked = await terrain.foliage_impostors_baked
	var info := {"queued": queued, "bake_ms": Time.get_ticks_msec() - t0, "paths": baked}
	if baked.size() > 0:
		var f := FileAccess.open(baked[0], FileAccess.READ)
		info["file_bytes"] = f.get_length() if f else -1
		var loaded = ResourceLoader.load(baked[0], "", ResourceLoader.CACHE_MODE_IGNORE)
		info["loaded_class"] = loaded.get_class() if loaded else "null"
		if loaded and loaded is Mesh and loaded.get_surface_count() > 0:
			var mat = loaded.surface_get_material(0)
			info["material"] = mat.get_class() if mat else "null"
			if mat is ShaderMaterial:
				var tex = mat.get_shader_parameter("albedo_atlas")
				info["atlas"] = tex.get_class() if tex else "null"
				info["atlas_size"] = tex.get_size() if tex else Vector2()
			trees.impostor = loaded # the file's, as a scene saved with it would have
	result["impostors"] = info
	for with_impostor in [true, false]:
		trees.impostor_distance = 50.0 if with_impostor else 0.0
		var name := "impostors_on" if with_impostor else "impostors_off"
		result["impostors"][name] = {}
		await _wait_foliage()
		for shadows in [true, false]:
			sun.shadow_enabled = shadows
			await _settle(frames)
			if shadows:
				await _shot(name)
			# On lavapipe the GPU's time is the CPU's rasterizing: a relative measure of the work drawn.
			result["impostors"][name]["shadows" if shadows else "no_shadows"] = await _render_ms(20)
		if _arg("debug_stats", "0") == "1":
			var s0: Dictionary = RenderingServer.fellow_get_stats()
			await _settle(10)
			var s1: Dictionary = RenderingServer.fellow_get_stats()
			var mm := {}
			for key in s1:
				if String(key).begins_with("geometry_multimesh"):
					mm[key] = s1[key] - s0.get(key, 0)
			result["impostors"][name]["multimesh_10_frames"] = mm
		result["impostors"][name]["impostor_instances"] = terrain.get_stats().get("foliage_impostor_instances", -1)
		result["impostors"][name]["foliage_instances"] = terrain.get_stats().get("foliage_instances", -1)
		sun.shadow_enabled = true
	# The far trees through a long lens: their meshes against their impostors (the mean difference of the pictures).
	cam.fov = 15.0
	sun.shadow_enabled = false
	var far := {}
	for with_impostor in [false, true]:
		trees.impostor_distance = 50.0 if with_impostor else 0.0
		await _wait_foliage()
		await _settle(frames)
		far[with_impostor] = await _shot("far_impostor" if with_impostor else "far_mesh")
	var diff := 0.0
	var n := 0
	for y in far[false].get_height():
		for x in far[false].get_width():
			var a: Color = far[false].get_pixel(x, y)
			var b: Color = far[true].get_pixel(x, y)
			var d := absf(a.r - b.r) + absf(a.g - b.g) + absf(a.b - b.b)
			if d > 0.0:
				diff += d / 3.0
				n += 1
	result["impostors"]["far_mean_abs_diff"] = snappedf(diff / float(far[false].get_width() * far[false].get_height()), 0.0001)
	result["impostors"]["far_pixels_differing"] = n
	await _free_world()
