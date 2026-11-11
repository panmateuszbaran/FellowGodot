extends SceneTree
## FellowEngine milestone 70 check (decision 0178): FellowTerrain3D's virtual texture.
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 640x360 -s res://bench/terrain_vt_check.gd
##       -- --out=<dir> [--size=513] [--texels=64] [--settle=600] [--budget=64] [--atlas=4080]
##
## One terrain (noise; four layers with textures, one of them triplanar, one painted along a path) drawn from its
## layers and from its virtual texture: the same views (near the ground, across the land, down, grazing, from high up),
## the pictures' difference, the pages each view wanted and baked, a frame's time on lavapipe (the fragments' work in
## software: a proxy, not a GPU's time); a brush and a paint stroke with the virtual texture on
## (the pages over them baked again: the picture as the layers draw it after the same edits); the terrain split into
## regions, each with its own pages in the shared atlas; a flight (the frames with pages missing, the pages baked a
## frame, the pages evicted and the pressure bias with a small --atlas). One JSON line: TERRAIN_VT_CHECK {...}.

var args := {}
var out_dir := ""
var result := {}
var size := 513
var settle_frames := 600
var root3d: Node3D
var cam: Camera3D
var terrain


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://terrain_vt_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	size = int(_arg("size", "513"))
	settle_frames = int(_arg("settle", "600"))
	ProjectSettings.set_setting("rendering/fellow/terrain/virtual_texture_pages_per_frame", int(_arg("budget", "64")))
	if args.has("atlas"):
		ProjectSettings.set_setting("rendering/fellow/terrain/virtual_texture_atlas_size", int(args["atlas"]))
	_run.call_deferred()


func _height(x: float, z: float, noise: FastNoiseLite) -> float:
	var n := noise.get_noise_2d(x, z)
	var ridge := 1.0 - absf(noise.get_noise_2d(x * 0.5 + 300.0, z * 0.5 - 200.0))
	return n * 18.0 + ridge * ridge * 30.0 + 0.02 * x


# A tiling texture of noise in a colour, with its mips; and a normal map from the same noise.
func _layer_textures(seed_: int, c0: Color, c1: Color, freq: float) -> Array:
	var n := FastNoiseLite.new()
	n.seed = seed_
	n.frequency = freq
	n.fractal_octaves = 4
	var gray: Image = n.get_seamless_image(256, 256, false, false, 0.1)
	gray.convert(Image.FORMAT_RGBA8)
	var albedo := Image.create(256, 256, false, Image.FORMAT_RGBA8)
	for y in 256:
		for x in 256:
			var v := gray.get_pixel(x, y).r
			var c := c0.lerp(c1, v)
			c.a = v # its height
			albedo.set_pixel(x, y, c)
	albedo.generate_mipmaps()
	var bump: Image = gray.duplicate()
	bump.bump_map_to_normal_map(4.0)
	bump.generate_mipmaps()
	return [ImageTexture.create_from_image(albedo), ImageTexture.create_from_image(bump)]


func _make_terrain():
	var noise := FastNoiseLite.new()
	noise.seed = 70
	noise.frequency = 0.006
	noise.fractal_octaves = 5
	var data := PackedFloat32Array()
	data.resize(size * size)
	for z in size:
		for x in size:
			data[z * size + x] = _height(x, z, noise)
	var splat := Image.create(size, size, false, Image.FORMAT_RGBA8)
	for z in size:
		for x in size:
			var hx := data[z * size + mini(x + 1, size - 1)] - data[z * size + maxi(x - 1, 0)]
			var hz := data[mini(z + 1, size - 1) * size + x] - data[maxi(z - 1, 0) * size + x]
			var slope := sqrt(hx * hx + hz * hz) * 0.5
			var rock := clampf((slope - 0.4) * 4.0, 0.0, 1.0)
			var high := clampf((data[z * size + x] - 38.0) * 0.15, 0.0, 1.0) * (1.0 - rock)
			# A path winding across the map (the fourth layer).
			var path_z := size * 0.5 + sin(x * 0.02) * size * 0.2
			var path := clampf(1.0 - absf(z - path_z) / 3.0, 0.0, 1.0) * (1.0 - rock)
			splat.set_pixel(x, z, Color(rock, high, path, 0.0))
	var t = ClassDB.instantiate("FellowTerrain3D")
	t.name = "Ground"
	var layers := []
	var specs := [
		[1, Color(0.22, 0.36, 0.14), Color(0.42, 0.55, 0.25), 0.05, 4.0, false],
		[2, Color(0.35, 0.33, 0.31), Color(0.62, 0.6, 0.57), 0.04, 6.0, true],
		[3, Color(0.75, 0.72, 0.62), Color(0.92, 0.9, 0.84), 0.08, 3.0, false],
		[4, Color(0.38, 0.28, 0.18), Color(0.55, 0.43, 0.3), 0.1, 2.0, false],
	]
	for sp in specs:
		var l = ClassDB.instantiate("FellowTerrainLayer")
		var tex := _layer_textures(sp[0], sp[1], sp[2], sp[3])
		l.albedo_texture = tex[0]
		l.normal_texture = tex[1]
		l.height_in_albedo_alpha = true
		l.texture_size = sp[4]
		l.triplanar = sp[5]
		layers.append(l)
	t.layers = layers
	t.heightmap = Image.create_from_data(size, size, false, Image.FORMAT_RF, data.to_byte_array())
	t.splat_map = splat
	t.virtual_texture_texels_per_meter = float(_arg("texels", "64"))
	return t


func _world() -> void:
	root3d = Node3D.new()
	root.add_child(root3d)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.55, 0.7, 0.9)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.35, 0.38, 0.42)
	root3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40, 35, 0)
	root3d.add_child(sun)
	cam = Camera3D.new()
	cam.far = 3000.0
	root3d.add_child(cam)
	cam.make_current()


func _settle(n: int) -> void:
	for i in n:
		await process_frame


# Until the terrains' virtual textures miss no page (or the frames run out); the frames it took.
func _settle_vt(terrains: Array) -> int:
	for i in settle_frames:
		await process_frame
		var missing := 0
		for t in terrains:
			missing += int(t.get_virtual_texture_stats().get("missing", 0))
		if missing == 0 and i > 2:
			await _settle(2)
			return i
	return settle_frames


func _ground(x: float, z: float) -> float:
	return terrain.get_height(Vector3(x, 0, z))


# The views (position, target).
func _views() -> Array:
	var m := size * 0.5
	return [
		["near", Vector3(m - 40, _ground(m - 40, m + 30) + 2.0, m + 30), Vector3(m + 20, _ground(m + 20, m + 40) - 1.0, m + 40)],
		["across", Vector3(60, _ground(60, 60) + 25.0, 60), Vector3(m + 80, 20, m + 60)],
		["down", Vector3(m, _ground(m, m) + 60.0, m), Vector3(m, 0, m + 0.01)],
		["grazing", Vector3(m - 120, _ground(m - 120, m) + 4.0, m), Vector3(m + 200, _ground(m + 200, m) + 2.0, m + 10)],
		["high", Vector3(-80, 260, -80), Vector3(m, 0, m)],
	]


func _look(v: Array) -> void:
	cam.position = v[1]
	cam.look_at(v[2], Vector3.UP if absf((v[2] - v[1]).normalized().y) < 0.99 else Vector3.FORWARD)


func _picture(name: String) -> Image:
	await RenderingServer.frame_post_draw
	var img := root.get_texture().get_image()
	img.save_png(out_dir.path_join(name + ".png"))
	return img


func _frame_ms(frames_: int) -> float:
	# A frame's wall time, the median of n (lavapipe draws on the CPU: the fragments' work in software, a proxy of the
	# shading's cost, not a GPU's time).
	var times := []
	var last := Time.get_ticks_usec()
	for i in frames_:
		await process_frame
		var now := Time.get_ticks_usec()
		times.append((now - last) / 1000.0)
		last = now
	times.sort()
	return snappedf(times[times.size() / 2], 0.01)


func _run() -> void:
	_world()
	terrain = _make_terrain()
	root3d.add_child(terrain)
	await _settle(3)
	var vt_ok: bool = false
	var views := _views()
	var direct := {}
	var timing := {}
	# 1. The layers.
	for v in views:
		_look(v)
		await _settle(8) # the terrain's levels of detail swapped in (three drawn frames after a selection)
		direct[v[0]] = await _picture("direct_" + v[0])
	for v in [views[0], views[1]]:
		_look(v)
		await _settle(8)
		timing["direct_" + v[0] + "_ms"] = await _frame_ms(30)

	# 2. The virtual texture.
	terrain.virtual_texture = true
	vt_ok = terrain.is_virtual_texture_active()
	result["active"] = vt_ok
	var per_view := {}
	for v in views:
		_look(v)
		var frames_ := await _settle_vt([terrain])
		await _settle(8)
		var img := await _picture("vt_" + v[0])
		var st: Dictionary = terrain.get_virtual_texture_stats()
		per_view[v[0]] = {
			"diff": _diff(direct[v[0]], img),
			"settle_frames": frames_,
			"wanted": st.get("wanted", 0),
			"resident": st.get("resident", 0),
			"baked_total": st.get("baked_total", 0),
			"pressure_bias": st.get("pressure_bias", 0.0),
		}
	result["views"] = per_view
	for v in [views[0], views[1]]:
		_look(v)
		await _settle_vt([terrain])
		await _settle(8)
		timing["vt_" + v[0] + "_ms"] = await _frame_ms(30)
	result["frame_ms_lavapipe"] = timing
	result["stats"] = terrain.get_virtual_texture_stats()
	# The pages drawn, by mip.
	terrain.debug_view = 2
	_look(views[1])
	await _settle(2)
	await _picture("vt_debug_across")
	terrain.debug_view = 0

	# 3. Edits with the virtual texture on: the pages over them baked again.
	var m := size * 0.5
	var spot := Vector3(m - 10, 0, m + 40)
	var baked_before := int(terrain.get_virtual_texture_stats().get("baked_total", 0))
	terrain.sculpt(spot, 10.0, 6.0, 0) # SCULPT_RAISE
	terrain.paint(spot + Vector3(6, 0, 0), 8.0, 2, 1.0) # the third layer
	_look(views[0])
	await _settle(2)
	await _settle_vt([terrain])
	await _settle(8)
	result["edit_pages_baked_again"] = int(terrain.get_virtual_texture_stats().get("baked_total", 0)) - baked_before
	var vt_edit := await _picture("vt_edited_near")
	terrain.virtual_texture = false
	await _settle(8)
	var direct_edit := await _picture("direct_edited_near")
	result["edited_near"] = _diff(direct_edit, vt_edit)
	result["edit_changed_direct"] = _diff(direct[views[0][0]], direct_edit)

	# 4. Regions: the same terrain split, each region with its own pages in the shared atlas.
	var streamer = ClassDB.instantiate("FellowWorldStreamer3D")
	streamer.name = "Streamer"
	streamer.load_distance = 5000.0
	root3d.add_child(streamer)
	var cells: Array = terrain.split_into_regions(256, out_dir.path_join("regions"), streamer)
	result["regions"] = cells.size()
	terrain.queue_free()
	await _settle(2)
	streamer.load_now()
	await _settle(4)
	var regions := []
	for cell in streamer.get_children():
		if cell.has_method("get_content") and cell.get_content():
			regions.append(cell.get_content())
	var region_views := {}
	for r in regions:
		r.virtual_texture = true
		r.virtual_texture_texels_per_meter = float(_arg("texels", "64"))
	for v in [views[1], views[2]]:
		_look(v)
		await _settle(2)
		for r in regions:
			r.virtual_texture = false
		await _settle(8)
		var d_img := await _picture("regions_direct_" + v[0])
		for r in regions:
			r.virtual_texture = true
		await _settle_vt(regions)
		await _settle(8)
		var v_img := await _picture("regions_vt_" + v[0])
		region_views[v[0]] = _diff(d_img, v_img)
	result["regions_vt_vs_direct"] = region_views
	var rs := []
	for r in regions:
		rs.append(r.get_virtual_texture_stats().get("resident", 0))
	result["regions_resident"] = rs
	result["atlas"] = regions[0].get_virtual_texture_stats() if regions.size() > 0 else {}

	# 5. A flight low across the regions.
	result["flight"] = await _flight(regions)
	print("TERRAIN_VT_CHECK " + JSON.stringify(result))
	quit()


func _flight(regions: Array) -> Dictionary:
	var from := Vector3(30, 45, 30)
	var to := Vector3(size - 30, 45, size - 30)
	var steps := 300
	var missing_frames := 0
	var baked_max := 0
	var baked0 := 0
	for r in regions:
		baked0 += int(r.get_virtual_texture_stats().get("baked_total", 0))
	var prev := baked0
	var evicted0 := int(regions[0].get_virtual_texture_stats().get("evicted", 0)) if regions.size() > 0 else 0
	var pressure_max := 0.0
	var longest := 0.0
	var last := Time.get_ticks_usec()
	for i in steps + 1:
		cam.position = from.lerp(to, float(i) / steps)
		cam.look_at(cam.position + (to - from).normalized() + Vector3(0, -0.3, 0))
		await process_frame
		var now := Time.get_ticks_usec()
		longest = maxf(longest, (now - last) / 1000.0)
		last = now
		var missing := 0
		var baked := 0
		for r in regions:
			var st: Dictionary = r.get_virtual_texture_stats()
			missing += int(st.get("missing", 0))
			baked += int(st.get("baked_total", 0))
		if missing > 0:
			missing_frames += 1
		baked_max = maxi(baked_max, baked - prev)
		prev = baked
		if regions.size() > 0:
			pressure_max = maxf(pressure_max, float(regions[0].get_virtual_texture_stats().get("pressure_bias", 0.0)))
	var evicted := (int(regions[0].get_virtual_texture_stats().get("evicted", 0)) if regions.size() > 0 else 0) - evicted0
	return {"frames": steps + 1, "frames_missing_pages": missing_frames, "pages_baked": prev - baked0, "pages_baked_max_frame": baked_max, "pages_evicted": evicted, "pressure_bias_max": pressure_max, "longest_frame_ms_lavapipe": snappedf(longest, 0.1)}


func _diff(a: Image, b: Image) -> Dictionary:
	var differing := 0
	var big := 0
	var total := 0.0
	var sq := 0.0
	for y in a.get_height():
		for x in a.get_width():
			var ca := a.get_pixel(x, y)
			var cb := b.get_pixel(x, y)
			var d := maxf(absf(ca.r - cb.r), maxf(absf(ca.g - cb.g), absf(ca.b - cb.b)))
			if d > 0.0:
				differing += 1
				total += d
			if d > 8.0 / 255.0:
				big += 1
			sq += ((ca.r - cb.r) * (ca.r - cb.r) + (ca.g - cb.g) * (ca.g - cb.g) + (ca.b - cb.b) * (ca.b - cb.b)) / 3.0
	var n := float(a.get_width() * a.get_height())
	var mse := sq / n
	return {"share_over_8": snappedf(big / n, 0.0001), "mean": snappedf(total / n, 0.00001), "psnr_db": snappedf(10.0 * log(1.0 / maxf(mse, 1e-12)) / log(10.0), 0.01)}
