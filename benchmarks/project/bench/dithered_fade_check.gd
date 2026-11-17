extends SceneTree
## FellowEngine milestone 132 check (decision 0256): dithered fades (rendering/fellow/geometry/dithered_fade).
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 960x540 -s res://bench/dithered_fade_check.gd
##       -- --out=<dir> [--objects=4000] [--frames=60] [--taa=1]
##
## Two levels of detail of a ball (a sphere of 64 x 32 faces near, of 10 x 5 far, the far one a little darker), handed
## over at 40 m with a margin of 6 m, each with its own fade (visibility_range_fade_mode SELF), or the far one the near
## one's visibility parent (DEPENDENCIES). Lanes: a switch (no fade), the fade blended (as stock), the fade dithered.
## (1) A hand-over: a row of 15 balls seen from 40 m, the hand-over's distance moved from 50 m to 30 m over 150 frames
## (TAA on with --taa=1; the picture changes only by the levels): the balls' mean brightness from frame to frame (192 x
## 108, their inside) — a switch is one step, a fade a ramp — and, in the middle
## (the middle of the hand-over), the pixels where the sky shows through the balls (holes) against the switch's
## picture. (2) The cost: --objects balls on a shell 40 m around the camera, all mid-fade — the GPU's and the
## renderer's times (medians of --frames), the draws, and the picture. (3) Nothing fading (the camera nearer than any
## hand-over): the picture with the setting on is the picture with it off, exactly. (4) FellowHLOD3D's fade: its
## merged boxes dithered over their proxy, no hole.
## One JSON line: DITHERED_FADE_CHECK {...}; exit code 1 when a check fails.

const SWITCH := 40.0
const MARGIN := 6.0
const SKY := Color(0.55, 0.7, 0.9)

var args := {}
var out_dir := ""
var result := {}
var world: Node3D
var cam: Camera3D
var near_mesh: Mesh
var far_mesh: Mesh
var near_mat: StandardMaterial3D
var far_mat: StandardMaterial3D


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	ProjectSettings.set_setting("rendering/fellow/autotune/enabled", false)
	out_dir = _arg("out", "user://dithered_fade_check").replace("\\", "/")
	DirAccess.make_dir_recursive_absolute(out_dir)
	_run.call_deferred()


func _setup() -> void:
	world = Node3D.new()
	root.add_child(world)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = SKY
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.5, 0.55, 0.6)
	world.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	world.add_child(sun)
	cam = Camera3D.new()
	cam.far = 500.0
	cam.fov = 40.0
	world.add_child(cam)
	cam.make_current()
	root.use_taa = _arg("taa", "1") == "1"
	var hi := SphereMesh.new()
	hi.radial_segments = 64
	hi.rings = 32
	near_mesh = hi
	var lo := SphereMesh.new()
	lo.radial_segments = 10
	lo.rings = 5
	far_mesh = lo
	near_mat = StandardMaterial3D.new()
	near_mat.albedo_color = Color(0.8, 0.35, 0.2)
	far_mat = StandardMaterial3D.new()
	far_mat.albedo_color = Color(0.5, 0.22, 0.13) # (darker: a hand-over shows in the balls' mean colour)


# A ball's two levels: lane "switch" (no fade), "self" (each fades itself), "deps" (the far one the near one's
# visibility parent, the near one fading with it).
func _ball(parent: Node3D, pos: Vector3, lane: String) -> void:
	var near := MeshInstance3D.new()
	near.mesh = near_mesh
	near.material_override = near_mat
	var far := MeshInstance3D.new()
	far.mesh = far_mesh
	far.material_override = far_mat
	parent.add_child(far)
	parent.add_child(near)
	near.position = pos
	far.position = pos
	if lane == "deps":
		far.visibility_range_begin = SWITCH
		far.visibility_range_begin_margin = MARGIN
		far.visibility_range_fade_mode = GeometryInstance3D.VISIBILITY_RANGE_FADE_DEPENDENCIES
		near.visibility_parent = near.get_path_to(far)
	else:
		near.visibility_range_end = SWITCH
		far.visibility_range_begin = SWITCH
		if lane == "self":
			near.visibility_range_end_margin = MARGIN
			far.visibility_range_begin_margin = MARGIN
			near.visibility_range_fade_mode = GeometryInstance3D.VISIBILITY_RANGE_FADE_SELF
			far.visibility_range_fade_mode = GeometryInstance3D.VISIBILITY_RANGE_FADE_SELF


func _shot() -> Image:
	await RenderingServer.frame_post_draw
	return root.get_texture().get_image()


func _small(img: Image) -> PackedByteArray:
	var s := img.duplicate() as Image
	s.convert(Image.FORMAT_RGB8)
	s.resize(192, 108, Image.INTERPOLATE_BILINEAR)
	return s.get_data()


# The pixels (of a 192 x 108 picture) well inside the balls: not sky, nor next to it.
func _ball_mask(a: PackedByteArray) -> PackedByteArray:
	var sky := [int(SKY.r * 255.0), int(SKY.g * 255.0), int(SKY.b * 255.0)]
	var is_sky := PackedByteArray()
	is_sky.resize(a.size() / 3)
	for i in is_sky.size():
		is_sky[i] = 1 if absi(a[i * 3] - sky[0]) + absi(a[i * 3 + 1] - sky[1]) + absi(a[i * 3 + 2] - sky[2]) <= 12 else 0
	var mask := PackedByteArray()
	mask.resize(is_sky.size())
	for y in range(1, 107):
		for x in range(1, 191):
			var i := y * 192 + x
			mask[i] = 1 if (is_sky[i] + is_sky[i - 1] + is_sky[i + 1] + is_sky[i - 192] + is_sky[i + 192]) == 0 else 0
	return mask


# The mean brightness (0-255) of the masked pixels.
func _masked_mean(a: PackedByteArray, mask: PackedByteArray) -> float:
	var total := 0
	var count := 0
	for i in mask.size():
		if mask[i]:
			total += a[i * 3] + a[i * 3 + 1] + a[i * 3 + 2]
			count += 1
	return total / maxf(count * 3.0, 1.0)


# Pixels that are sky in a but not in b (holes), and pixels more than 16 levels apart.
func _compare(a: Image, b: Image) -> Dictionary:
	var holes := 0
	var off := 0
	var sky := 0
	for y in a.get_height():
		for x in a.get_width():
			var ca := a.get_pixel(x, y)
			var cb := b.get_pixel(x, y)
			var a_sky := absf(ca.r - SKY.r) + absf(ca.g - SKY.g) + absf(ca.b - SKY.b) < 0.02
			var b_sky := absf(cb.r - SKY.r) + absf(cb.g - SKY.g) + absf(cb.b - SKY.b) < 0.02
			if not b_sky:
				sky += 1
				if a_sky:
					holes += 1
			if maxf(absf(ca.r - cb.r), maxf(absf(ca.g - cb.g), absf(ca.b - cb.b))) > 16.0 / 255.0:
				off += 1
	var n := float(a.get_width() * a.get_height())
	return {"holes_share_of_balls": snappedf(holes / maxf(sky, 1.0), 0.0001), "share_over_16": snappedf(off / n, 0.0001)}


func _exact(a: Image, b: Image) -> bool:
	return a.get_data() == b.get_data()


func _flight(lane: String, dither: bool) -> Dictionary:
	ProjectSettings.set_setting("rendering/fellow/geometry/dithered_fade", dither)
	var row := Node3D.new()
	world.add_child(row)
	for i in 15:
		_ball(row, Vector3((i - 7) * 2.6, 0, 0), lane)
	# The camera still at 40 m from the middle ball, the hand-over's distance moved from 50 m to 30 m over the frames:
	# the picture changes only by the levels (as a camera backing away from 30 m to 50 m would see them, without the
	# balls growing smaller).
	cam.position = Vector3(0, 2.0, -SWITCH)
	cam.look_at(Vector3.ZERO)
	var n := 150
	var means := []
	var mask := PackedByteArray()
	var mid: Image
	for f in n + 30:
		var sw := lerpf(50.0, 30.0, clampf((f - 30) / float(n), 0.0, 1.0))
		for c in row.get_children():
			var gi := c as GeometryInstance3D
			if gi.visibility_range_end > 0.0:
				gi.visibility_range_end = sw
			if gi.visibility_range_begin > 0.0:
				gi.visibility_range_begin = sw
		var img := await _shot()
		if f < 30:
			continue
		var small := _small(img)
		if mask.is_empty():
			mask = _ball_mask(small)
		means.append(_masked_mean(small, mask))
		if f == 30 + n / 2:
			mid = img
			img.save_png(out_dir.path_join("flight_%s_%s.png" % [lane, "dither" if dither else "blend"]))
	row.queue_free()
	await process_frame
	# The balls' brightness from frame to frame: a switch is one step (the far level darker), a fade a ramp.
	var steps := []
	for i in range(1, means.size()):
		steps.append(absf(means[i] - means[i - 1]))
	var sorted := steps.duplicate()
	sorted.sort()
	var changing := 0
	for st in steps:
		if st > 0.1:
			changing += 1
	return {"brightness_near": snappedf(means[0], 0.1), "brightness_far": snappedf(means[-1], 0.1), "largest_step": snappedf(sorted[-1], 0.01),
		"median_step": snappedf(sorted[sorted.size() / 2], 0.001), "frames_changing_over_0_1": changing, "mid": mid}


func _cost(lane: String, dither: bool) -> Dictionary:
	ProjectSettings.set_setting("rendering/fellow/geometry/dithered_fade", dither)
	var shell := Node3D.new()
	world.add_child(shell)
	cam.position = Vector3.ZERO
	cam.rotation = Vector3.ZERO
	var count := int(_arg("objects", "4000"))
	var rng := RandomNumberGenerator.new()
	rng.seed = 256
	for i in count:
		# Within the view (40 degrees), at the hand-over's distance (its middle: half faded).
		var dir := Vector3(rng.randf_range(-0.33, 0.33), rng.randf_range(-0.18, 0.18), -1.0).normalized()
		_ball(shell, dir * SWITCH, lane)
	for i in 30:
		await process_frame
	var vp := root.get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(vp, true)
	var frames := int(_arg("frames", "60"))
	var gpu := []
	var cpu := []
	for i in frames:
		await process_frame
		gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(vp))
		cpu.append(RenderingServer.viewport_get_measured_render_time_cpu(vp) + RenderingServer.get_frame_setup_time_cpu())
	gpu.sort()
	cpu.sort()
	var draws := Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME)
	var img := await _shot()
	img.save_png(out_dir.path_join("cost_%s_%s.png" % [lane, "dither" if dither else "blend"]))
	shell.queue_free()
	await process_frame
	await process_frame
	return {"gpu_ms": snappedf(gpu[frames / 2], 0.01), "render_cpu_ms": snappedf(cpu[frames / 2], 0.01), "draws": draws, "img": img}


# Pixels that are a ball's (more red than blue) in b and not in a, over b's ball pixels; pixels that differ at all.
func _ball_holes(a: Image, b: Image) -> Dictionary:
	var holes := 0
	var balls := 0
	var differing := 0
	for y in a.get_height():
		for x in a.get_width():
			var ca := a.get_pixel(x, y)
			var cb := b.get_pixel(x, y)
			if cb.r > cb.b + 0.05:
				balls += 1
				if not (ca.r > ca.b + 0.05):
					holes += 1
			if ca != cb:
				differing += 1
	return {"holes": snappedf(holes / maxf(balls, 1.0), 0.0001), "share_differing": snappedf(differing / float(a.get_width() * a.get_height()), 0.0001)}


func _ball_mesh(sphere: SphereMesh, mat: Material) -> ArrayMesh:
	var m := ArrayMesh.new()
	m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, sphere.get_mesh_arrays())
	m.surface_set_material(0, mat)
	return m


# (5) A flat terrain (blue) with balls as foliage, their "impostor" the far ball (a mesh: any mesh can stand in), from
# 40 m with a band of 6 m on each side, without TAA: against the impostors alone (their silhouettes inside the
# meshes'), no pixel of a ball lost; against the switch, a fade.
func _foliage() -> Dictionary:
	root.use_taa = false
	var size := 257
	var heights := PackedFloat32Array()
	heights.resize(size * size)
	var t = ClassDB.instantiate("FellowTerrain3D")
	var layer = ClassDB.instantiate("FellowTerrainLayer")
	layer.albedo_color = Color(0.1, 0.12, 0.8)
	t.layers = [layer]
	t.heightmap = Image.create_from_data(size, size, false, Image.FORMAT_RF, heights.to_byte_array())
	var f = ClassDB.instantiate("FellowTerrainFoliage")
	f.mesh = _ball_mesh(near_mesh, near_mat)
	f.impostor = _ball_mesh(far_mesh, far_mat)
	f.density = 0.03
	f.range = 150.0
	f.cast_shadows = false
	f.sink = -0.5
	f.scale_min = 1.0
	f.scale_max = 1.0
	t.foliage = [f]
	world.add_child(t)
	cam.position = Vector3(128, 1.6, 40)
	cam.rotation_degrees = Vector3(-2, 180, 0)
	var pics := {}
	var counts := {}
	for lane in [["impostors", 1.0, 0.0], ["fade", SWITCH, MARGIN], ["switch", SWITCH, 0.0], ["meshes", 1000.0, 0.0]]:
		f.impostor_distance = lane[1]
		f.impostor_fade = lane[2]
		var last := -1
		var same := 0
		for i in 900:
			await process_frame
			var n: int = t.get_stats().get("foliage_instances", 0)
			same = same + 1 if n == last and n > 0 else 0
			last = n
			if same >= 10:
				break
		for i in 5:
			await process_frame
		pics[lane[0]] = await _shot()
		pics[lane[0]].save_png(out_dir.path_join("foliage_%s.png" % lane[0]))
		counts[lane[0]] = {"instances": t.get_stats().get("foliage_instances", 0), "impostor_instances": t.get_stats().get("foliage_impostor_instances", 0)}
	t.queue_free()
	await process_frame
	return {"counts": counts, "fade_vs_impostors": _ball_holes(pics["fade"], pics["impostors"]), "fade_vs_switch": _ball_holes(pics["fade"], pics["switch"]),
		"switch_vs_impostors": _ball_holes(pics["switch"], pics["impostors"])}


func _run() -> void:
	_setup()
	await process_frame
	var failures := []

	# (3) Nothing fading: the setting changes nothing.
	var row := Node3D.new()
	world.add_child(row)
	for i in 15:
		_ball(row, Vector3((i - 7) * 2.6, 0, 0), "self")
	cam.position = Vector3(0, 2.0, -20.0)
	cam.look_at(Vector3.ZERO)
	var exact := {}
	var pics := []
	var taa := root.use_taa
	root.use_taa = false # (TAA's jitter differs from frame to frame: the pictures compared without it)
	for dither in [false, true]:
		ProjectSettings.set_setting("rendering/fellow/geometry/dithered_fade", dither)
		for i in 30:
			await process_frame
		pics.append(await _shot())
	exact["nothing_fading_same_picture"] = _exact(pics[0], pics[1])
	# The two levels crossing over (each ball half faded or so, at 40-44 m), without TAA: against the far level alone
	# (its silhouette is inside the near one's) — holes where neither level keeps a pixel — blended and dithered.
	row.queue_free()
	await process_frame
	var ref_row := Node3D.new()
	world.add_child(ref_row)
	for i in 15:
		var far := MeshInstance3D.new()
		far.mesh = far_mesh
		far.material_override = far_mat
		far.position = Vector3((i - 7) * 2.6, 0, 0)
		ref_row.add_child(far)
	cam.position = Vector3(0, 0.0, -SWITCH)
	cam.look_at(Vector3.ZERO)
	for i in 5:
		await process_frame
	var far_only := await _shot()
	ref_row.queue_free()
	row = Node3D.new()
	world.add_child(row)
	for i in 15:
		_ball(row, Vector3((i - 7) * 2.6, 0, 0), "self")
	var cross := {}
	for dither in [false, true]:
		ProjectSettings.set_setting("rendering/fellow/geometry/dithered_fade", dither)
		for i in 5:
			await process_frame
		var at := await _shot()
		at.save_png(out_dir.path_join("crossover_%s.png" % ("dither" if dither else "blend")))
		cross["dither" if dither else "blend"] = _compare(at, far_only)
	ProjectSettings.set_setting("rendering/fellow/geometry/dithered_fade", false)
	result["crossover"] = cross
	row.queue_free()
	await process_frame
	root.use_taa = taa
	result["exact"] = exact
	if not exact["nothing_fading_same_picture"]:
		failures.append("the setting changed a picture where nothing fades")
	if cross["dither"]["holes_share_of_balls"] > 0.001:
		failures.append("holes where the two levels cross over, dithered")

	# (4) FellowHLOD3D with fade: 16 boxes merged into one proxy (or, decision 0257, instanced: a MultiMesh of them whose
	# instances fade in on the GPU while the boxes, nodes, fade out), the camera still 40 m away, the node's distance
	# set past the camera (the boxes), at it (the middle of the fade) and short of it (the proxy alone). Dithered, the
	# middle has the boxes' pixels and the proxy's in the others: the same picture (the proxy is the same surface), both
	# drawn, no hole.
	root.use_taa = false
	ProjectSettings.set_setting("rendering/fellow/geometry/dithered_fade", true)
	var box := BoxMesh.new()
	box.size = Vector3(1.6, 1.6, 1.6)
	box.subdivide_width = 8
	box.subdivide_height = 8
	box.subdivide_depth = 8
	result["hlod_fade"] = {}
	for instancing in [false, true]:
		var hlod = ClassDB.instantiate("FellowHLOD3D")
		hlod.set("build_on_ready", false)
		hlod.set("instancing", instancing)
		hlod.set("instance_threshold", 4)
		hlod.set("cell_size", 100.0)
		hlod.set("margin", MARGIN)
		hlod.set("fade", true)
		world.add_child(hlod)
		for i in 16:
			var b := MeshInstance3D.new()
			b.mesh = box
			b.material_override = near_mat
			b.position = Vector3((i % 8 - 3.5) * 2.4, (i / 8) * 2.4, 0)
			hlod.add_child(b)
		hlod.call("build")
		cam.position = Vector3(0, 2.0, -SWITCH)
		cam.look_at(Vector3(0, 1.2, 0))
		var hlod_pics := {}
		var hlod_tris := {}
		var kind := "instanced" if instancing else "merged"
		for lane in [["members", 100.0], ["middle", SWITCH], ["proxy", 10.0]]:
			hlod.set("distance", lane[1])
			for i in 5:
				await process_frame
			hlod_pics[lane[0]] = await _shot()
			hlod_tris[lane[0]] = Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME)
			hlod_pics[lane[0]].save_png(out_dir.path_join("hlod_%s_%s.png" % [kind, lane[0]]))
		var info: Dictionary = hlod.call("get_build_info")
		result.hlod_fade[kind] = {"merged_proxies": info.get("merged_proxies", 0), "instanced_proxies": info.get("instanced_proxies", 0), "triangles": hlod_tris,
			"middle_vs_members": _compare(hlod_pics["middle"], hlod_pics["members"])}
		# (A MultiMesh's draws are not counted in the frame's primitives when culled on the GPU: the middle then at least
		# the members'.)
		var both: bool = hlod_tris["middle"] >= hlod_tris["members"] + (0 if instancing else hlod_tris["proxy"])
		if not both or result.hlod_fade[kind].middle_vs_members.holes_share_of_balls > 0.001 or result.hlod_fade[kind].middle_vs_members.share_over_16 > 0.001:
			failures.append("HLOD fade (%s): not both drawn in the middle, or holes" % kind)
		hlod.queue_free()
		await process_frame
	ProjectSettings.set_setting("rendering/fellow/geometry/dithered_fade", false)
	root.use_taa = taa
	await process_frame

	# (5) Decision 0257: a terrain's foliage handing over to its impostor per instance on the GPU.
	result["foliage"] = await _foliage()
	if result.foliage.fade_vs_impostors.holes > 0.001 or result.foliage.fade_vs_switch.share_differing == 0.0:
		failures.append("foliage: holes where the meshes hand over to the impostors, or no fade")
	root.use_taa = taa

	# (1) The flights.
	var flights := {}
	var reference: Image
	for lane in [["switch", false], ["self", false], ["self", true], ["deps", false], ["deps", true]]:
		var key: String = lane[0] + ("_dither" if lane[1] else ("" if lane[0] == "switch" else "_blend"))
		var r: Dictionary = await _flight(lane[0], lane[1])
		var mid: Image = r["mid"]
		r.erase("mid")
		if lane[0] == "switch":
			reference = mid
		else:
			r["middle_vs_switch"] = _compare(mid, reference)
		flights[key] = r
	result["flight"] = flights
	for key in ["self_dither", "deps_dither"]:
		if flights[key]["largest_step"] > 0.25 * flights["switch"]["largest_step"]:
			failures.append("%s: the hand-over is a step, not a ramp" % key)

	# (2) The cost, all mid-fade.
	var cost := {}
	for lane in [["switch", false], ["self", false], ["self", true], ["deps", false], ["deps", true]]:
		var key: String = lane[0] + ("_dither" if lane[1] else ("" if lane[0] == "switch" else "_blend"))
		var r: Dictionary = await _cost(lane[0], lane[1])
		r.erase("img")
		cost[key] = r
	result["cost"] = cost
	result["failures"] = failures
	print("DITHERED_FADE_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("dithered_fade_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)
