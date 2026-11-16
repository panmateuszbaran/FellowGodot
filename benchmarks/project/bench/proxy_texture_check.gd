extends SceneTree
## FellowEngine milestone 127 check (decision 0251): streaming proxies drawn with a baked texture.
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 960x540 -s res://bench/proxy_texture_check.gd
##       -- --out=<dir> [--cells=4]
##
## A village of --cells x --cells cells of 64 m: houses of 1 m quads whose walls, gables and roofs take their parts of
## one atlas texture (white plaster with dark windows, plain plaster, red roof tiles: a trim sheet, the common way of
## texturing modular buildings), on ground with a tiled texture, under a streamer. Seen from 300 m and from 600 m (the
## sun without shadows: the proxies' colours alone): (A) every cell loaded; (B) every cell a proxy coloured per vertex
## (each material's mean colour: the atlas's mean on walls and roofs alike); (C, D, E) proxies with textures of texels
## of 0.25, 0.5 and 1 m. Each: the picture, its difference from A (the share of pixels more than 16 levels off and
## the mean difference, over the pixels where A draws the village), the textures' bytes and the bake's time.
## Checked: every proxy textured; the textured proxies closer to A than the vertex-coloured ones at each distance.
## One JSON line: PROXY_TEXTURE_CHECK {...}; exit code 1 when a check fails.

const CELL := 64.0
const SKY := Color(0.55, 0.7, 0.9)
var args := {}
var out_dir := ""
var result := {}
var n_cells := 4
var root3d: Node3D
var cam: Camera3D
var streamer: Node3D
var atlas_mat: StandardMaterial3D
var ground_mat: StandardMaterial3D
var views := {}


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://proxy_texture_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	n_cells = int(_arg("cells", "4"))
	_run.call_deferred()


func _settle(n: int) -> void:
	for i in n:
		await process_frame


# The atlas: four quarters (a 3 m wall section with its window; red roof tiles; plain plaster; wooden planks), sRGB.
func _atlas() -> ImageTexture:
	var size := 512
	var img := Image.create(size, size, false, Image.FORMAT_RGB8)
	var rng := RandomNumberGenerator.new()
	rng.seed = 251
	var plaster := Color(0.86, 0.83, 0.76)
	for y in size:
		for x in size:
			var q := Vector2i(x / 256, y / 256)
			var u := float(x % 256) / 256.0
			var v := float(y % 256) / 256.0
			var n := rng.randf_range(-0.03, 0.03)
			var c: Color
			if q == Vector2i(0, 0):
				# A window in the middle of the section: a frame, dark glass split by a cross.
				var inside := u > 0.3 and u < 0.7 and v > 0.22 and v < 0.72
				var frame := u > 0.27 and u < 0.73 and v > 0.19 and v < 0.75
				if inside:
					c = Color(0.1, 0.13, 0.2) if absf(u - 0.5) > 0.015 and absf(v - 0.47) > 0.015 else Color(0.9, 0.9, 0.88)
				elif frame:
					c = Color(0.92, 0.92, 0.9)
				else:
					c = plaster
			elif q == Vector2i(1, 0):
				# Tiles: rows with dark gaps, each tile a little different.
				var row := int(v * 8.0)
				var fv := v * 8.0 - row
				var col := int(u * 6.0 + (0.5 if row % 2 == 1 else 0.0))
				var shade := 0.85 + 0.15 * sin(float(col * 31 + row * 17))
				c = Color(0.62, 0.2, 0.12) * shade
				if fv > 0.85:
					c = Color(0.25, 0.08, 0.05)
			elif q == Vector2i(0, 1):
				c = plaster
			else:
				var plank := int(u * 5.0)
				c = Color(0.42, 0.27, 0.15) * (0.85 + 0.1 * (plank % 2))
			img.set_pixel(x, y, Color(clampf(c.r + n, 0, 1), clampf(c.g + n, 0, 1), clampf(c.b + n, 0, 1)))
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)


func _grass() -> ImageTexture:
	var size := 128
	var img := Image.create(size, size, false, Image.FORMAT_RGB8)
	var noise := FastNoiseLite.new()
	noise.seed = 7
	noise.frequency = 0.08
	for y in size:
		for x in size:
			var t := noise.get_noise_2d(x, y) * 0.5 + 0.5
			img.set_pixel(x, y, Color(0.3, 0.45, 0.2).lerp(Color(0.45, 0.4, 0.25), t))
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)


# A quad of 1 m pieces: origin, its two edges, and the atlas rectangle each section of `section` metres takes.
func _quad(st: SurfaceTool, o: Vector3, eu: Vector3, ev: Vector3, rect: Rect2, section: Vector2) -> void:
	var lu := eu.length()
	var lv := ev.length()
	var du := eu / lu
	var dv := ev / lv
	var nu := maxi(1, roundi(lu))
	var nv := maxi(1, roundi(lv))
	var n := du.cross(dv).normalized()
	var inset := rect.grow(-rect.size.x * 0.02)
	for j in nv:
		for i in nu:
			var u0 := lu * i / nu
			var u1 := lu * (i + 1) / nu
			var v0 := lv * j / nv
			var v1 := lv * (j + 1) / nv
			var pts := [Vector2(u0, v0), Vector2(u1, v0), Vector2(u1, v1), Vector2(u0, v1)]
			var uvs := []
			for p in pts:
				# Within its section (the section's corner from the piece's centre).
				var cu := floorf((u0 + u1) * 0.5 / section.x)
				var cv := floorf((v0 + v1) * 0.5 / section.y)
				var fu: float = clampf((p.x - cu * section.x) / section.x, 0.0, 1.0)
				var fv: float = clampf((p.y - cv * section.y) / section.y, 0.0, 1.0)
				uvs.append(inset.position + Vector2(fu, 1.0 - fv) * inset.size)
			for k in [0, 1, 2, 0, 2, 3]:
				st.set_normal(n)
				st.set_uv(uvs[k])
				st.add_vertex(o + du * pts[k].x + dv * pts[k].y)


func _gable(st: SurfaceTool, a: Vector3, b: Vector3, top: Vector3, rect: Rect2) -> void:
	var n := (b - a).cross(top - a).normalized()
	var inset := rect.grow(-rect.size.x * 0.05)
	var uvs := [inset.position + Vector2(0, inset.size.y), inset.end, inset.position + Vector2(inset.size.x * 0.5, 0)]
	var pts := [a, b, top]
	for k in 3:
		st.set_normal(n)
		st.set_uv(uvs[k])
		st.add_vertex(pts[k])


func _house(st: SurfaceTool, center: Vector3, yaw: float, w: float, d: float, h: float) -> void:
	var facade := Rect2(0, 0, 0.5, 0.5)
	var roof := Rect2(0.5, 0, 0.5, 0.5)
	var plain := Rect2(0, 0.5, 0.5, 0.5)
	var b := Basis(Vector3.UP, yaw)
	var x := b * Vector3(w * 0.5, 0, 0)
	var z := b * Vector3(0, 0, d * 0.5)
	var up := Vector3(0, h, 0)
	var sec := Vector2(3, 3)
	# Walls (counter-clockwise seen from outside).
	_quad(st, center - x + z, x * 2.0, up, facade, sec)
	_quad(st, center + x - z, -x * 2.0, up, facade, sec)
	_quad(st, center + x + z, -z * 2.0, up, facade, sec)
	_quad(st, center - x - z, z * 2.0, up, facade, sec)
	# Gables and the roof (a ridge along x, 3 m over the walls, eaves 0.4 m out).
	var top := Vector3(0, h + 3.0, 0)
	_gable(st, center + x + z + up, center + x - z + up, center + x + top, plain)
	_gable(st, center - x - z + up, center - x + z + up, center - x + top, plain)
	var over := b * Vector3(0.4, 0, 0)
	var eave := z * (1.0 + 0.8 / d)
	var drop := Vector3(0, -0.4 * 3.0 / (d * 0.5), 0)
	_quad(st, center - x - over + up + eave + drop, (x + over) * 2.0, top - up - eave - drop, roof, Vector2(2, 2))
	_quad(st, center + x + over + up - eave + drop, -(x + over) * 2.0, top - up + eave - drop, roof, Vector2(2, 2))


func _make_cell(cx: int, cz: int) -> Node3D:
	var rng := RandomNumberGenerator.new()
	rng.seed = 251000 + cx * 97 + cz
	var root := Node3D.new()
	root.name = "cell"
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for i in 3:
		for j in 3:
			var w := 3.0 * rng.randi_range(2, 4)
			var d := 3.0 * rng.randi_range(2, 3)
			var h := 3.0 * rng.randi_range(1, 2)
			var c := Vector3(-21.0 + i * 21.0 + rng.randf_range(-3, 3), 0, -21.0 + j * 21.0 + rng.randf_range(-3, 3))
			_house(st, c, rng.randf() * TAU, w, d, h)
	var houses := MeshInstance3D.new()
	houses.name = "houses"
	houses.mesh = st.commit()
	houses.material_override = atlas_mat
	root.add_child(houses)
	houses.owner = root
	var plane := PlaneMesh.new()
	plane.size = Vector2(CELL, CELL)
	plane.subdivide_width = 7
	plane.subdivide_depth = 7
	var ground := MeshInstance3D.new()
	ground.name = "ground"
	ground.mesh = plane
	ground.material_override = ground_mat
	root.add_child(ground)
	ground.owner = root
	return root


func _shot(name: String) -> Image:
	await RenderingServer.frame_post_draw
	var img := root.get_texture().get_image()
	img.save_png(out_dir.path_join(name + ".png"))
	return img


func _diff(a: Image, b: Image) -> Dictionary:
	var big := 0
	var total := 0.0
	var n := 0
	for y in a.get_height():
		for x in a.get_width():
			var ca := a.get_pixel(x, y)
			var cb := b.get_pixel(x, y)
			if maxf(absf(ca.r - SKY.r), maxf(absf(ca.g - SKY.g), absf(ca.b - SKY.b))) < 3.0 / 255.0 and maxf(absf(cb.r - SKY.r), maxf(absf(cb.g - SKY.g), absf(cb.b - SKY.b))) < 3.0 / 255.0:
				continue # the sky in both
			n += 1
			var d := maxf(absf(ca.r - cb.r), maxf(absf(ca.g - cb.g), absf(ca.b - cb.b)))
			total += d
			if d > 16.0 / 255.0:
				big += 1
	return {"pixels": n, "share_over_16": snappedf(big / maxf(1.0, n), 0.0001), "mean": snappedf(total / maxf(1.0, n), 0.00001)}


func _view(name: String) -> void:
	var w := n_cells * CELL
	var c := Vector3(w * 0.5, 0, w * 0.5)
	var dist: float = views[name]
	var dir := Vector3(-1, 0, -0.6).normalized()
	cam.position = c + dir * dist + Vector3(0, dist * 0.22, 0)
	cam.look_at(c)


func _shots(lane: String, reference: Dictionary) -> Dictionary:
	var out := {}
	for v in views:
		_view(v)
		await _settle(8)
		var img := await _shot("%s_%s" % [lane, v])
		if reference.is_empty():
			out[v] = img
		else:
			out[v] = _diff(reference[v], img)
	return out


func _run() -> void:
	views = {"300m": 300.0, "600m": 600.0}
	root3d = Node3D.new()
	root.add_child(root3d)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = SKY
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.45, 0.5, 0.55)
	root3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-45, -60, 0)
	root3d.add_child(sun)
	cam = Camera3D.new()
	cam.fov = 40.0
	cam.far = 3000.0
	root3d.add_child(cam)
	cam.make_current()

	# The materials saved once (the cells' scenes refer to them).
	var atlas := _atlas()
	ResourceSaver.save(atlas, out_dir.path_join("atlas.res"))
	atlas = load(out_dir.path_join("atlas.res"))
	var grass := _grass()
	ResourceSaver.save(grass, out_dir.path_join("grass.res"))
	grass = load(out_dir.path_join("grass.res"))
	atlas_mat = StandardMaterial3D.new()
	atlas_mat.albedo_texture = atlas
	atlas_mat.roughness = 0.85
	ResourceSaver.save(atlas_mat, out_dir.path_join("atlas_mat.tres"))
	atlas_mat = load(out_dir.path_join("atlas_mat.tres"))
	ground_mat = StandardMaterial3D.new()
	ground_mat.albedo_texture = grass
	ground_mat.uv1_scale = Vector3(16, 16, 1)
	ground_mat.roughness = 0.95
	ResourceSaver.save(ground_mat, out_dir.path_join("ground_mat.tres"))
	ground_mat = load(out_dir.path_join("ground_mat.tres"))

	var level := Node3D.new()
	level.name = "Level"
	root3d.add_child(level)
	streamer = ClassDB.instantiate("FellowWorldStreamer3D")
	streamer.name = "Streamer"
	level.add_child(streamer)
	DirAccess.make_dir_recursive_absolute(out_dir.path_join("cells"))
	var triangles := 0
	for x in n_cells:
		for z in n_cells:
			var content := _make_cell(x, z)
			triangles += (content.get_node("houses").mesh as ArrayMesh).surface_get_array_len(0) / 3
			var ps := PackedScene.new()
			ps.pack(content)
			var path := out_dir.path_join("cells").path_join("cell_%02d_%02d.scn" % [x, z])
			ResourceSaver.save(ps, path)
			content.free()
			var cell: Node3D = ClassDB.instantiate("FellowStreamingCell3D")
			cell.name = "cell_%02d_%02d" % [x, z]
			cell.position = Vector3((x + 0.5) * CELL, 0, (z + 0.5) * CELL)
			cell.set("scene_path", path)
			cell.set("bounds", AABB(Vector3(-CELL * 0.5, -5.0, -CELL * 0.5), Vector3(CELL, 20.0, CELL)))
			streamer.add_child(cell)
	result["house_triangles_per_cell"] = triangles / (n_cells * n_cells)

	# A. Everything loaded.
	streamer.load_distance = 100000.0
	streamer.load_now()
	for i in 600:
		await process_frame
		if streamer.is_settled() and i > 10:
			break
	await _settle(10)
	var a := await _shots("a_loaded", {})

	# B-E. Every cell a proxy (nothing within the load distance), baked again for each.
	streamer.load_distance = 1.0
	cam.position = Vector3(-100000, 0, -100000)
	await _settle(30)
	var lanes := [["b_vertex_colours", 0.0], ["c_texel_0_25", 0.25], ["d_texel_0_5", 0.5], ["e_texel_1", 1.0]]
	for lane in lanes:
		var name: String = lane[0]
		var info: Dictionary = streamer.bake_proxies(out_dir.path_join("proxies_" + name), {"texel_size": lane[1]})
		var row := {
			"texel_size": lane[1],
			"proxies": info.get("proxies", 0),
			"textured": info.get("textured", 0),
			"proxy_triangles_per_cell": snappedf(float(info.get("proxy_triangles", 0)) / maxf(1.0, info.get("proxies", 1)), 0.1),
			"texture_kb_per_proxy": snappedf(float(info.get("texture_bytes", 0)) / 1024.0 / maxf(1.0, info.get("proxies", 1)), 0.1),
			"bake_ms": snappedf(info.get("bake_ms", 0.0), 0.1),
			"texture_ms": snappedf(info.get("texture_ms", 0.0), 0.1),
			"texture_hit_share": snappedf(info.get("texture_hit_share", 0.0), 0.001),
		}
		await _settle(5)
		row["vs_a"] = await _shots(name, a)
		result[name] = row

	# A textured proxy as saved: its file read again.
	var saved = ResourceLoader.load(out_dir.path_join("proxies_d_texel_0_5").path_join("cell_00_00_proxy.res"), "", ResourceLoader.CACHE_MODE_IGNORE)
	var reloaded := {}
	if saved is ArrayMesh and saved.surface_get_material(0) is StandardMaterial3D and saved.surface_get_material(0).albedo_texture:
		var img: Image = saved.surface_get_material(0).albedo_texture.get_image()
		reloaded = {"format": img.get_format(), "width": img.get_width(), "height": img.get_height(), "mipmaps": img.has_mipmaps(), "triangles": saved.surface_get_array_index_len(0) / 3}
	result["reloaded"] = reloaded

	var failures := []
	if reloaded.get("format", -1) != Image.FORMAT_DXT1 or not reloaded.get("mipmaps", false):
		failures.append("the saved proxy's texture (BC1 with mipmaps)")
	for lane in lanes:
		var row: Dictionary = result[lane[0]]
		if row.proxies != n_cells * n_cells:
			failures.append("%s: proxies" % lane[0])
		if lane[1] > 0.0 and row.textured != n_cells * n_cells:
			failures.append("%s: not every proxy textured" % lane[0])
	for v in views:
		var b: Dictionary = result.b_vertex_colours.vs_a[v]
		var d: Dictionary = result.d_texel_0_5.vs_a[v]
		if d.mean >= b.mean:
			failures.append("%s: the textured proxies not closer than the vertex colours" % v)
	result["failures"] = failures
	print("PROXY_TEXTURE_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("proxy_texture_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)
