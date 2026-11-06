extends SceneTree
## Motion matching's ground adaptation (milestone 56), measured without rendering: Matching Lab's CMU database, one
## character walking (or jogging) straight over flat ground, a slope, stairs up and down and uneven ground, with
## FellowMotionMatcher3D.ground_adaptation off, on with only the root following the ground (ground_foot_placement
## off: what a game without foot placement gets), and on. The same inputs every run, so the animation is the same frame
## for frame on every ground (the search does not see the ground's height): each foot's height over the ground under
## (the nearer to its own of the heel's and the toe's: a foot rests on the ground with one or the other) is compared
## with the same frame's on flat ground, in the frames the foot is down there (its ankle lower than 13 cm).
## Prints one JSON line.
##   godot --headless --path benchmarks/project -s res://bench/ground_check.gd -- [--speed=1.3] [--seconds=10]

const STEP := 1.0 / 60.0
var lab
var speed := 1.3
var seconds := 10.0


func _initialize() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)


func _ground(kind: String) -> Node3D:
	var root := Node3D.new()
	root.name = "Ground_" + kind
	var body := StaticBody3D.new()
	root.add_child(body)
	var add_box := func(size: Vector3, xform: Transform3D) -> void:
		var cs := CollisionShape3D.new()
		var b := BoxShape3D.new()
		b.size = size
		cs.shape = b
		cs.transform = xform
		body.add_child(cs)
	match kind:
		"flat":
			add_box.call(Vector3(40, 1, 80), Transform3D(Basis(), Vector3(0, -0.5, 10)))
		"slope":
			# 8 degrees up along +z from z = 2, a plateau after.
			add_box.call(Vector3(40, 1, 80), Transform3D(Basis(), Vector3(0, -0.5, -38.0)))
			var a := deg_to_rad(8.0)
			var l := 8.0
			add_box.call(Vector3(40, 1, l), Transform3D(Basis(Vector3.RIGHT, -a), Vector3(0, -0.5 * cos(a), 2.0 + l * 0.5 * cos(a)) + Vector3(0, l * 0.5 * sin(a), 0) + Vector3(0, 0, 0.5 * sin(a))))
			add_box.call(Vector3(40, 1, 40), Transform3D(Basis(), Vector3(0, l * sin(a) - 0.5, 2.0 + l * cos(a) + 20.0)))
		"stairs":
			# Eight steps of 15 cm up (60 cm deep) from z = 2, a landing, eight down.
			add_box.call(Vector3(40, 1, 80), Transform3D(Basis(), Vector3(0, -0.5, 0)))
			for i in 8:
				var h := 0.15 * (i + 1)
				add_box.call(Vector3(40, h, 0.6), Transform3D(Basis(), Vector3(0, h * 0.5, 2.0 + i * 0.6 + 0.3)))
			add_box.call(Vector3(40, 1.2, 2.4), Transform3D(Basis(), Vector3(0, 0.6, 2.0 + 4.8 + 1.2)))
			for i in 8:
				var h := 0.15 * (8 - i)
				add_box.call(Vector3(40, h, 0.6), Transform3D(Basis(), Vector3(0, h * 0.5, 2.0 + 4.8 + 2.4 + i * 0.6 + 0.3)))
		"uneven":
			# Blocks of random heights (0 to 12 cm), 50 cm square.
			var rng := RandomNumberGenerator.new()
			rng.seed = 56
			add_box.call(Vector3(40, 1, 80), Transform3D(Basis(), Vector3(0, -0.5, 0)))
			for z in 40:
				for x in 8:
					var h := rng.randf_range(0.0, 0.12)
					if h > 0.01:
						add_box.call(Vector3(0.5, h, 0.5), Transform3D(Basis(), Vector3(-2.0 + x * 0.5 + 0.25, h * 0.5, z * 0.5 + 0.25)))
	return root


func _ground_y(space: PhysicsDirectSpaceState3D, p: Vector3) -> float:
	var q := PhysicsRayQueryParameters3D.create(Vector3(p.x, p.y + 1.5, p.z), Vector3(p.x, p.y - 3.0, p.z))
	var hit := space.intersect_ray(q)
	return hit["position"].y if hit else -100.0


func _walk(kind: String, mode: String) -> Dictionary:
	var g := _ground(kind)
	get_root().add_child(g)
	for i in 3:
		await physics_frame
	var ch := Node3D.new()
	get_root().add_child(ch)
	var skel: Skeleton3D = ClassDB.class_call_static("FellowMotionDatabase", "create_skeleton", lab.bvh_skeleton)
	ch.add_child(skel)
	var mm = ClassDB.instantiate("FellowMotionMatcher3D")
	mm.database = lab.database
	mm.auto_update = false
	mm.ground_adaptation = mode != "off"
	mm.ground_foot_placement = mode == "on"
	skel.add_child(mm)
	mm.teleport(Vector3(0, 0, 0), Vector3(0, 0, 1))
	mm.desired_velocity = Vector3(0, 0, speed)
	var space: PhysicsDirectSpaceState3D = ch.get_world_3d().direct_space_state
	var heights := []
	var hips := []
	var roots := []
	var frames := int(seconds / STEP)
	for f in frames:
		mm.advance(STEP)
		var feet: PackedVector3Array = mm.get_foot_positions()
		var st0: Dictionary = mm.get_stats()
		var toes: PackedVector3Array = st0["toe_positions"]
		var h := []
		for s in 2:
			h.append([feet[s].y - _ground_y(space, feet[s]), toes[s].y - _ground_y(space, toes[s])])
		heights.append(h)
		hips.append(float(st0["hips_drop_mm"]))
		roots.append(mm.get_character_position())
	var st: Dictionary = mm.get_stats()
	ch.queue_free()
	g.queue_free()
	await process_frame
	return {"heights": heights, "hips_drop_mm": hips, "roots": roots, "slide_mm_s": float(st["foot_slide_speed"]) * 1000.0, "hips_drop_min_mm": st["hips_drop_min_mm"]}


func _run() -> void:
	var args := {}
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv := a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	speed = float(args.get("speed", "1.3"))
	seconds = float(args.get("seconds", "10"))
	lab = load("res://scenes/matching_lab.gd").new()
	get_root().add_child(lab)
	lab.build({"matching_crowd": "0"})
	if lab.load_error != "":
		print(JSON.stringify({"error": lab.load_error}))
		quit(1)
		return
	lab.visible = false
	var ref: Dictionary = await _walk("flat", "off")
	var out := {"speed": speed, "seconds": seconds, "grounds": {}}
	for kind in ["flat", "slope", "stairs", "uneven"]:
		for mode in ["off", "root", "on"]:
			var r: Dictionary = await _walk(kind, mode)
			var errs := PackedFloat32Array()
			var below := 0
			var n := 0
			for f in r["heights"].size():
				for s in 2:
					var href: Array = ref["heights"][f][s]
					if href[0] > 0.13:
						continue # the foot is up in the reference: not compared
					var hh: Array = r["heights"][f][s]
					# The heel's or the toe's clearance, whichever is nearer to the reference's: the foot resting on it.
					var eh: float = hh[0] - href[0]
					var et: float = hh[1] - href[1]
					var e: float = eh if absf(eh) < absf(et) else et
					if eh < -0.03 or et < -0.03:
						below += 1 # part of the foot in the ground
					errs.append(absf(e))
					n += 1
			errs.sort()
			var mean := 0.0
			for e in errs:
				mean += e
			var key := "%s_%s" % [kind, mode]
			var hd: Array = r["hips_drop_mm"]
			out["grounds"][key] = {"compared": n, "error_mean_mm": snappedf(1000.0 * mean / maxf(n, 1), 0.1),
				"error_p95_mm": snappedf(1000.0 * errs[int(errs.size() * 0.95)] if errs.size() else 0.0, 0.1),
				"error_max_mm": snappedf(1000.0 * errs[errs.size() - 1] if errs.size() else 0.0, 0.1),
				"into_ground_3cm_share": snappedf(float(below) / maxf(n, 1), 0.0001),
				"hips_drop_min_mm": snappedf(r["hips_drop_min_mm"], 0.1), "slide_mm_s": snappedf(r["slide_mm_s"], 0.1),
				"root_y_end": snappedf(r["roots"][r["roots"].size() - 1].y, 0.001)}
	print(JSON.stringify(out))
	quit(0)
