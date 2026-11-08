extends SceneTree
## FellowEngine terrain editing check (decision 0160): FellowTerrain3D's sculpt(), paint() and blits update only what
## they touch, and what they leave is what a terrain built from the edited images makes.
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 640x360 -s res://bench/terrain_edit_check.gd
##       -- [--out=<dir>] [--size=513] [--timing_size=2049] [--foliage=1] [--stroke_seconds=0] [--region_uploads=1]
##
## With a RenderingDevice (lavapipe, a GPU) the textures are compared bit for bit (the heights, the normals and the
## splat maps with all their mips, read back); headless only the CPU side (heights, collision, weights, timing) is.
## Then the cost of an edit on a map of --timing_size: a dab's time, the update at the frame's end; with
## --stroke_seconds, a raise and a paint stroke held that long (a dab a frame) and the frames' times meanwhile.
## --region_uploads=0: rendering/fellow/streaming/terrain_region_uploads off (decision 0168: the textures updated whole).
## One JSON line: TERRAIN_EDIT_CHECK {...}; the exit code is 1 when a check fails.

var args := {}
var out_dir := ""
var size := 513
var timing_size := 2049
var cam: Camera3D
var root3d: Node3D
var a  # the edited terrain
var b  # built from its images
var frame := 0
var step := 0
var wait_until := 0
var result := {"checks": {}, "dab_usec": {}, "flush_ms": {}}
var failed := false
var original := {}
var original_heights: Image
var edit_rect := Rect2i()
var stroke_frames := 0
var stroke_i := 0
var stroke_kind := ""
var stroke_last_usec := 0
var stroke_times := {}


func _stroke_frame() -> bool:
	# One dab a frame along a circle, the camera following; false when the stroke is over.
	var now := Time.get_ticks_usec()
	if stroke_last_usec > 0:
		if not stroke_times.has(stroke_kind):
			stroke_times[stroke_kind] = []
		stroke_times[stroke_kind].append((now - stroke_last_usec) / 1000.0)
	stroke_last_usec = now
	if stroke_i >= stroke_frames:
		var v: Array = stroke_times.get(stroke_kind, [])
		v.sort()
		var s := 0.0
		for x in v:
			s += x
		if not v.is_empty():
			result["stroke_%s_%d" % [stroke_kind, timing_size]] = {"frames": v.size(), "mean_ms": s / v.size(),
					"p99_ms": v[int(v.size() * 0.99)], "max_ms": v[v.size() - 1],
					"flush_ms_last": a.get_stats()["edit_flush_usec"] / 1000.0}
		return false
	var c := Vector3(timing_size * 0.5, 0, timing_size * 0.5)
	var ang := stroke_i * 0.02
	var p := c + Vector3(cos(ang), 0, sin(ang)) * 60.0
	var h: float = a.get_height(p)
	cam.position = p + Vector3(0, 60, 50)
	cam.look_at(Vector3(p.x, h, p.z))
	if stroke_kind == "raise":
		a.sculpt(p, 12.0, 0.2, FellowTerrain3D.SCULPT_RAISE)
	else:
		a.paint(p, 12.0, 1 + stroke_i % 3, 0.3)
	stroke_i += 1
	return true


func _check(name: String, ok: bool, detail = null) -> void:
	result["checks"][name] = {"ok": ok, "detail": detail} if detail != null else {"ok": ok}
	if not ok:
		failed = true
		printerr("TERRAIN_EDIT_CHECK FAIL ", name, " ", detail)


func _layers() -> Array:
	var colors := [Color(0.3, 0.45, 0.2), Color(0.45, 0.42, 0.4), Color(0.9, 0.9, 0.95), Color(0.45, 0.32, 0.2),
			Color(0.2, 0.3, 0.6), Color(0.6, 0.5, 0.2), Color(0.7, 0.2, 0.2)]
	var out := []
	for c in colors:
		var l := FellowTerrainLayer.new()
		l.albedo_color = c
		out.append(l)
	return out


func _make_terrain(heights: Image, splat: Image, splat2: Image) -> Node3D:
	var t = FellowTerrain3D.new()
	t.heightmap = heights
	t.splat_map = splat
	t.splat_map_2 = splat2
	t.layers = _layers()
	t.collision_region_size = 128
	if args.get("foliage", "1") == "1":
		var f := FellowTerrainFoliage.new()
		var box := BoxMesh.new()
		box.size = Vector3(0.2, 0.6, 0.2)
		f.mesh = box
		f.layer = 3
		f.density = 0.5
		f.range = 80.0
		t.foliage = [f]
	return t


func _noise_heights(n: int) -> Image:
	var noise := FastNoiseLite.new()
	noise.seed = 11
	noise.frequency = 0.006
	noise.fractal_octaves = 4
	return FellowTerrain3D.heightmap_from_image(noise.get_image(n, n, false, false, true), 0.0, 40.0)


func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--") and arg.contains("="):
			var kv: PackedStringArray = arg.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	if args.get("region_uploads", "1") == "0":
		ProjectSettings.set_setting("rendering/fellow/streaming/terrain_region_uploads", false)
	out_dir = args.get("out", "user://terrain_edit_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	size = int(args.get("size", "513"))
	timing_size = int(args.get("timing_size", "2049"))
	root3d = Node3D.new()
	root.add_child(root3d)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(1, 0, 0)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.4, 0.42, 0.45)
	root3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	root3d.add_child(sun)
	cam = Camera3D.new()
	cam.far = 4000.0
	root3d.add_child(cam)
	a = _make_terrain(_noise_heights(size), null, null)
	root3d.add_child(a)


func _texture_images(t) -> Dictionary:
	var out := {}
	var m: ShaderMaterial = t.get_material()
	for p in ["heightmap", "normalmap", "splat_0", "splat_1"]:
		var tex: Texture2D = m.get_shader_parameter(p)
		out[p] = tex.get_image() if tex else null
	return out


func _compare_textures(name: String, x: Dictionary, y: Dictionary) -> void:
	if RenderingServer.get_rendering_device() == null:
		return
	for p in x:
		var ix: Image = x[p]
		var iy: Image = y[p]
		var ok: bool = ix != null and iy != null and ix.get_size() == iy.get_size() and ix.get_format() == iy.get_format() \
				and ix.has_mipmaps() == iy.has_mipmaps() and ix.get_data() == iy.get_data()
		var detail := ""
		if not ok and ix != null and iy != null and ix.get_data().size() == iy.get_data().size():
			var dx := ix.get_data()
			var dy := iy.get_data()
			var diff := 0
			var first := -1
			for i in dx.size():
				if dx[i] != dy[i]:
					diff += 1
					if first < 0:
						first = i
			detail = "%d of %d bytes differ, the first at %d" % [diff, dx.size(), first]
		_check("%s_%s" % [name, p], ok, detail if not ok else null)


func _ray_errors(t, rect: Rect2i) -> Dictionary:
	var space: PhysicsDirectSpaceState3D = t.get_world_3d().direct_space_state
	var worst := 0.0
	var sum := 0.0
	var n := 0
	for i in 400:
		var x := rect.position.x + (i % 20 + 0.37) * rect.size.x / 20.0
		var z := rect.position.y + (i / 20 + 0.61) * rect.size.y / 20.0
		var q := PhysicsRayQueryParameters3D.create(Vector3(x, 500, z), Vector3(x, -500, z))
		var hit: Dictionary = space.intersect_ray(q)
		var h: float = t.get_height(Vector3(x, 0, z))
		if hit.is_empty() or is_nan(h):
			continue
		var e := absf(hit.position.y - h)
		worst = maxf(worst, e)
		sum += e
		n += 1
	return {"rays": n, "mean_mm": 1000.0 * sum / maxf(1, n), "max_mm": 1000.0 * worst}


func _dab(kind: String, callable: Callable) -> void:
	var t0 := Time.get_ticks_usec()
	callable.call()
	var us := Time.get_ticks_usec() - t0
	if not result["dab_usec"].has(kind):
		result["dab_usec"][kind] = []
	result["dab_usec"][kind].append(us)


func _process(_delta: float) -> bool:
	frame += 1
	if frame < wait_until:
		return false
	match step:
		0:
			# Over the edits, the painted grass (layer 3) within the foliage's range.
			cam.position = Vector3(280, 75, 330)
			cam.look_at(Vector3(260, 20, 270))
			wait_until = frame + 4
		1:
			original = _texture_images(a)
			original_heights = a.heightmap.duplicate()
			var stats: Dictionary = a.get_stats()
			result["foliage_before"] = stats.get("foliage_instances", 0)
			# 1. Raise at a texel: the height there rises by the strength exactly; lower takes it back.
			var p1 := Vector3(200, 0, 240)
			var h0: float = a.get_height(p1)
			_dab("raise", func(): a.sculpt(p1, 14.0, 3.0, FellowTerrain3D.SCULPT_RAISE))
			var h1: float = a.get_height(p1)
			_check("raise_centre", absf(h1 - h0 - 3.0) < 1e-4, [h0, h1])
			var p2 := Vector3(300, 0, 260)
			for i in 4:
				_dab("lower", func(): a.sculpt(p2 + Vector3(i * 3, 0, 0), 10.0, 1.5, FellowTerrain3D.SCULPT_LOWER))
			var p3 := Vector3(250, 0, 320)
			var rough_before: float = _roughness(a, p3, 8)
			for i in 6:
				_dab("smooth", func(): a.sculpt(p3, 12.0, 1.0, FellowTerrain3D.SCULPT_SMOOTH))
			var rough_after: float = _roughness(a, p3, 8)
			_check("smooth_flattens", rough_after < rough_before, [rough_before, rough_after])
			var p4 := Vector3(180, 0, 300)
			for i in 8:
				_dab("flatten", func(): a.sculpt(p4, 9.0, 1.0, FellowTerrain3D.SCULPT_FLATTEN, 20.0))
			_check("flatten_target", absf(a.get_height(p4) - 20.0) < 0.01, a.get_height(p4))
			# A dab over the map's edge.
			_dab("raise", func(): a.sculpt(Vector3(2, 0, 150), 10.0, 2.0, FellowTerrain3D.SCULPT_RAISE))
			# 2. Paint: the first dab makes the splat map, a layer above 4 the second.
			_dab("paint", func(): a.paint(p1, 16.0, 1, 1.0))
			for i in 5:
				_dab("paint", func(): a.paint(p2 + Vector3(i * 4, 0, 0), 12.0, 3, 0.8))
			_dab("paint", func(): a.paint(p3, 14.0, 6, 1.0))
			_dab("paint", func(): a.paint(p4, 8.0, 0, 1.0))
			var w1: PackedFloat32Array = a.get_layer_weights(p1)
			var w3: PackedFloat32Array = a.get_layer_weights(p2 + Vector3(8, 0, 0))
			var w6: PackedFloat32Array = a.get_layer_weights(p3)
			var w0: PackedFloat32Array = a.get_layer_weights(p4)
			_check("paint_weights", w1[1] > 0.99 and w3[3] > 0.95 and w6[6] > 0.99 and w0[0] > 0.99,
					[w1[1], w3[3], w6[6], w0[0]])
			var sums := []
			for w in [w1, w3, w6, w0]:
				var s := 0.0
				for v in w:
					s += v
				sums.append(s)
			_check("paint_weights_sum", sums.all(func(s): return absf(s - 1.0) < 0.01), sums)
			edit_rect = Rect2i(0, 120, 340, 230)
			wait_until = frame + 3
		2:
			result["flush_ms"]["last_%d" % size] = a.get_stats()["edit_flush_usec"] / 1000.0
			# 3. Collision follows: physics rays against get_height() over the edits.
			var rays := _ray_errors(a, Rect2i(170, 220, 160, 120))
			result["rays"] = rays
			_check("collision", rays["rays"] > 300 and rays["max_mm"] < 1.0, rays)
			# 4. The edited terrain against one built from its images.
			b = _make_terrain(a.heightmap.duplicate(), a.splat_map.duplicate(), a.splat_map_2.duplicate())
			root3d.add_child(b)
			_check("aabb", a.get_aabb().is_equal_approx(b.get_aabb()), [a.get_aabb(), b.get_aabb()])
			wait_until = frame + 40
		3:
			_compare_textures("edited", _texture_images(a), _texture_images(b))
			var sa: Dictionary = a.get_stats()
			result["foliage_after"] = sa.get("foliage_instances", 0)
			var sb: Dictionary = b.get_stats()
			_check("foliage_regrown", int(sa.get("foliage_instances", 0)) > 0 and sa.get("foliage_instances", 0) == sb.get("foliage_instances", -1),
					[sa.get("foliage_instances", 0), sb.get("foliage_instances", 0)])
			if RenderingServer.get_rendering_device() != null:
				b.visible = false
				var img := root.get_viewport().get_texture().get_image()
				img.save_png(out_dir.path_join("edited.png"))
			# 5. Undo by blits: the heights saved before over the edited rectangle, zero weights over the splat maps.
			var t0 := Time.get_ticks_usec()
			a.blit_heightmap_region(original_heights.get_region(edit_rect), edit_rect.position)
			var zero := Image.create(edit_rect.size.x, edit_rect.size.y, false, Image.FORMAT_RGBA8)
			a.blit_splat_region(0, zero, edit_rect.position)
			a.blit_splat_region(1, zero, edit_rect.position)
			result["undo_usec"] = Time.get_ticks_usec() - t0
			b.queue_free()
			b = _make_terrain(a.heightmap.duplicate(), a.splat_map.duplicate(), a.splat_map_2.duplicate())
			b.visible = false
			root3d.add_child(b)
			wait_until = frame + 3
		4:
			var now := _texture_images(a)
			_compare_textures("undone", now, _texture_images(b))
			if RenderingServer.get_rendering_device() != null:
				_check("undone_heights_original", now["heightmap"].get_data() == original["heightmap"].get_data())
				_check("undone_normals_original", now["normalmap"].get_data() == original["normalmap"].get_data())
			_check("undone_cpu_heights", a.heightmap.get_data() == original_heights.get_data())
			b.queue_free()
			# 6. Timing on a larger map (the textures' uploads grow with it).
			a.queue_free()
			a = _make_terrain(_noise_heights(timing_size), null, null)
			a.foliage = []
			root3d.add_child(a)
			wait_until = frame + 3
		5:
			# A frame's dabs, then the update at its end (the textures, the collision shapes): once for all of them.
			# What an edit cost before: the whole terrain built again.
			var t_rebuild := Time.get_ticks_usec()
			a.update_heightmap()
			result["rebuild_ms_%d" % timing_size] = (Time.get_ticks_usec() - t_rebuild) / 1000.0
			var c := Vector3(timing_size * 0.5, 0, timing_size * 0.5)
			_dab("first_raise_%d" % timing_size, func(): a.sculpt(c, 16.0, 1.0, FellowTerrain3D.SCULPT_RAISE))
			_dab("first_paint_%d" % timing_size, func(): a.paint(c, 16.0, 2, 0.5))
			wait_until = frame + 2
		6:
			var c := Vector3(timing_size * 0.5, 0, timing_size * 0.5)
			for i in 10:
				_dab("raise_%d" % timing_size, func(): a.sculpt(c + Vector3(i * 5, 0, 0), 16.0, 1.0, FellowTerrain3D.SCULPT_RAISE))
			wait_until = frame + 2
		7:
			result["flush_ms"]["heights_%d" % timing_size] = a.get_stats()["edit_flush_usec"] / 1000.0
			var c := Vector3(timing_size * 0.5, 0, timing_size * 0.5)
			for i in 10:
				_dab("paint_%d" % timing_size, func(): a.paint(c + Vector3(i * 5, 0, 0), 16.0, 2, 0.5))
			wait_until = frame + 2
		8:
			result["flush_ms"]["splat_%d" % timing_size] = a.get_stats()["edit_flush_usec"] / 1000.0
			# 7. A stroke held for a while (--stroke_seconds): a dab every frame, the camera on the brush, the frames'
			# times (what sculpting and painting this map feels like).
			stroke_frames = int(float(args.get("stroke_seconds", "0")) * 60.0)
			stroke_i = 0
			stroke_kind = "raise"
			step = 99 if stroke_frames > 0 else 100
			return false
		99:
			if _stroke_frame():
				return false
			if stroke_kind == "raise":
				stroke_kind = "paint"
				stroke_i = 0
				stroke_last_usec = 0
				return false
			step = 100
			return false
		100:
			var summary := {}
			for k in result["dab_usec"]:
				var v: Array = result["dab_usec"][k]
				var s := 0.0
				var mx := 0
				for u in v:
					s += u
					mx = maxi(mx, u)
				summary[k] = {"n": v.size(), "mean_ms": s / v.size() / 1000.0, "max_ms": mx / 1000.0}
			result["dab_usec"] = summary
			result["ok"] = not failed
			print("TERRAIN_EDIT_CHECK ", JSON.stringify(result))
			quit(1 if failed else 0)
	step += 1
	return false


func _roughness(t, p: Vector3, r: int) -> float:
	# The mean absolute second difference of the heights around a point.
	var s := 0.0
	var n := 0
	for z in range(-r, r + 1):
		for x in range(-r, r + 1):
			var q := p + Vector3(x, 0, z)
			var h: float = t.get_height(q)
			var l: float = t.get_height(q + Vector3(-1, 0, 0))
			var rr: float = t.get_height(q + Vector3(1, 0, 0))
			s += absf(l + rr - 2.0 * h)
			n += 1
	return s / n
