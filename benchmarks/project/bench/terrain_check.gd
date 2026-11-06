extends SceneTree
## FellowEngine terrain check (decision 0158): a FellowTerrain3D built from noise, looked at and measured.
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 640x360 -s res://bench/terrain_check.gd
##       -- --out=<dir> [--size=1025] [--spacing=1] [--patch=16] [--frames=12] [--debug_view=1]
##
## For each camera: a PNG, and the share of the ground's pixels where the sky (pure red) shows through (cracks between
## levels of detail; the camera looks down, so every pixel below the horizon should be ground). Then physics rays cast
## down on a grid against get_height() (collision against what the queries say), and the terrain's statistics. One JSON
## line: TERRAIN_CHECK {...}.

var args := {}
var out_dir := ""
var terrain
var cam: Camera3D
var views := []
var view_i := 0
var frame := 0
var frames := 12
var result := {"views": {}}
var size := 1025
var spacing := 1.0


func _height(x: float, z: float, noise: FastNoiseLite) -> float:
	var n := noise.get_noise_2d(x, z)
	var ridge := 1.0 - absf(noise.get_noise_2d(x * 0.5 + 300.0, z * 0.5 - 200.0))
	return n * 25.0 + ridge * ridge * 40.0 + 0.02 * x


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = args.get("out", "user://terrain_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	size = int(args.get("size", "1025"))
	spacing = float(args.get("spacing", "1"))
	frames = int(args.get("frames", "12"))

	var root3d := Node3D.new()
	root.add_child(root3d)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(1, 0, 0)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.35, 0.38, 0.42)
	root3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40, 35, 0)
	sun.shadow_enabled = true
	root3d.add_child(sun)

	var noise := FastNoiseLite.new()
	noise.seed = 7
	noise.frequency = 0.004
	noise.fractal_octaves = 5
	var t0 := Time.get_ticks_usec()
	var data := PackedFloat32Array()
	data.resize(size * size)
	for z in size:
		for x in size:
			data[z * size + x] = _height(x * spacing, z * spacing, noise)
	var img := Image.create_from_data(size, size, false, Image.FORMAT_RF, data.to_byte_array())
	# Splat: layer 1 (rock) on slopes, layer 2 (snow) high up, layer 3 (dirt) in patches.
	var splat := Image.create(size, size, false, Image.FORMAT_RGBA8)
	for z in size:
		for x in size:
			var hx := data[z * size + mini(x + 1, size - 1)] - data[z * size + maxi(x - 1, 0)]
			var hz := data[mini(z + 1, size - 1) * size + x] - data[maxi(z - 1, 0) * size + x]
			var slope := sqrt(hx * hx + hz * hz) / (2.0 * spacing)
			var h := data[z * size + x]
			var rock := clampf((slope - 0.45) * 4.0, 0.0, 1.0)
			var snow := clampf((h - 55.0) * 0.1, 0.0, 1.0) * (1.0 - rock)
			var dirt := clampf(noise.get_noise_2d(x * 3.0, z * 3.0) * 3.0, 0.0, 1.0) * (1.0 - rock - snow)
			splat.set_pixel(x, z, Color(rock, snow, dirt, 0.0))
	result["build_data_ms"] = (Time.get_ticks_usec() - t0) / 1000.0

	terrain = ClassDB.instantiate("FellowTerrain3D")
	terrain.vertex_spacing = spacing
	terrain.patch_size = int(args.get("patch", "16"))
	terrain.debug_view = int(args.get("debug_view", "0"))
	var colors := [Color(0.25, 0.42, 0.16), Color(0.45, 0.43, 0.4), Color(0.92, 0.94, 0.97), Color(0.42, 0.3, 0.18)]
	var layers := []
	for c in colors:
		var l = ClassDB.instantiate("FellowTerrainLayer")
		l.albedo_color = c
		l.roughness = 0.85
		layers.append(l)
	layers[1].triplanar = true
	terrain.layers = layers
	t0 = Time.get_ticks_usec()
	terrain.heightmap = img
	terrain.splat_map = splat
	result["set_heightmap_ms"] = (Time.get_ticks_usec() - t0) / 1000.0
	t0 = Time.get_ticks_usec()
	root3d.add_child(terrain)
	result["enter_tree_ms"] = (Time.get_ticks_usec() - t0) / 1000.0

	cam = Camera3D.new()
	cam.far = 4000.0
	root3d.add_child(cam)
	cam.make_current()
	var e := (size - 1) * spacing
	# Each view: position, a point it looks at.
	views = [
		["low", Vector3(e * 0.3, 0.0, e * 0.3), Vector3(e * 0.6, 0.0, e * 0.55), 3.0],
		["mid", Vector3(e * 0.2, 0.0, e * 0.25), Vector3(e * 0.55, 0.0, e * 0.6), 40.0],
		["high", Vector3(e * 0.1, 0.0, e * 0.1), Vector3(e * 0.5, 0.0, e * 0.5), 220.0],
		["down", Vector3(e * 0.5, 0.0, e * 0.45), Vector3(e * 0.5, 0.0, e * 0.5), 600.0],
	]


func _place(i: int) -> void:
	var v: Array = views[i]
	var p: Vector3 = v[1]
	var h: float = terrain.get_height(p)
	p.y = h + float(v[3])
	var target: Vector3 = v[2]
	target.y = terrain.get_height(target)
	cam.global_position = p
	cam.look_at(target, Vector3.UP if absf((target - p).normalized().y) < 0.99 else Vector3.FORWARD)


func _cracks(img: Image) -> Dictionary:
	# A crack is the sky (pure red) seen through the ground in a line at most two pixels wide: a red pixel with ground
	# within two pixels on both sides, horizontally or vertically, and mostly ground around it (at most 17 of the 49
	# pixels of its 7 x 7 neighbourhood red). The sky beyond the map or the horizon is not.
	var w := img.get_width()
	var h := img.get_height()
	var red := PackedByteArray()
	red.resize(w * h)
	var ground := 0
	for y in h:
		for x in w:
			var c := img.get_pixel(x, y)
			var r := c.r > 0.98 and c.g < 0.02 and c.b < 0.02
			red[y * w + x] = 1 if r else 0
			if not r:
				ground += 1
	var cracks := 0
	for y in range(2, h - 2):
		for x in range(2, w - 2):
			if red[y * w + x] == 0:
				continue
			var lft := red[y * w + x - 1] == 0 or red[y * w + x - 2] == 0
			var rgt := red[y * w + x + 1] == 0 or red[y * w + x + 2] == 0
			var up := red[(y - 1) * w + x] == 0 or red[(y - 2) * w + x] == 0
			var dn := red[(y + 1) * w + x] == 0 or red[(y + 2) * w + x] == 0
			if not ((lft and rgt) or (up and dn)):
				continue
			# Thin: a crack's neighbourhood is mostly ground (a silhouette's staircase is half sky).
			var n := 0
			for dy in range(-3, 4):
				for dx in range(-3, 4):
					var yy := clampi(y + dy, 0, h - 1)
					var xx := clampi(x + dx, 0, w - 1)
					n += red[yy * w + xx]
			if n <= 17:
				cracks += 1
	return {"crack_pixels": cracks, "ground_pixels": ground}


func _rays() -> Dictionary:
	var space := root.get_world_3d().direct_space_state
	var e := (size - 1) * spacing
	var n := 0
	var miss := 0
	var max_err := 0.0
	var sum_err := 0.0
	for i in 40:
		for j in 40:
			var x := (i + 0.37) / 40.0 * e
			var z := (j + 0.61) / 40.0 * e
			var q := PhysicsRayQueryParameters3D.create(Vector3(x, 500.0, z), Vector3(x, -500.0, z))
			var hit := space.intersect_ray(q)
			var hq: float = terrain.get_height(Vector3(x, 0, z))
			if hit.is_empty():
				miss += 1
				continue
			var err := absf(hit.position.y - hq)
			max_err = maxf(max_err, err)
			sum_err += err
			n += 1
	return {"rays": n, "misses": miss, "mean_error_m": sum_err / maxf(1.0, float(n)), "max_error_m": max_err}


func _process(_delta: float) -> bool:
	if frame == 0 and view_i == 0:
		_place(0)
	frame += 1
	if frame < frames:
		return false
	var v: Array = views[view_i]
	var img := root.get_texture().get_image()
	img.save_png(out_dir.path_join("terrain_%s.png" % v[0]))
	var r := _cracks(img)
	r["stats"] = terrain.get_stats()
	result["views"][v[0]] = r
	view_i += 1
	frame = 0
	if view_i < views.size():
		_place(view_i)
		return false
	result["physics"] = _rays()
	result["stats"] = terrain.get_stats()
	print("TERRAIN_CHECK ", JSON.stringify(result))
	return true
