extends SceneTree
## Motion matching's mirrored clips and tags (milestone 56), without rendering. Builds Matching Lab's database with
## --matching_mirror=1 (the clips and their mirror images, tagged by CMU subject), then:
##  * mirroring: every mirrored frame's features against its original's reflected (the feet swapped, x negated): the
##    largest difference in metres and metres per second;
##  * tags: one character walking and turning for --seconds (30) with tags_required (--require, e.g. s035) or
##    tags_excluded (--exclude, e.g. mirrored): the clips it played, and how many frames of clips without the tags.
## Prints one JSON line.
##   godot --headless --path benchmarks/project -s res://bench/matching_tags_check.gd -- [--require=s035] [--exclude=mirrored]

const STEP := 1.0 / 60.0


func _initialize() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)


func _run() -> void:
	var args := {}
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv := a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	var lab = load("res://scenes/matching_lab.gd").new()
	get_root().add_child(lab)
	var t0 := Time.get_ticks_usec()
	lab.build({"matching_crowd": "0", "matching_mirror": "1", "matching_rebuild": args.get("rebuild", "0")})
	if lab.load_error != "":
		print(JSON.stringify({"error": lab.load_error}))
		quit(1)
		return
	var db = lab.database
	var d: Dictionary = db.get_data()
	var names: PackedStringArray = d["range_names"]
	var starts: PackedInt32Array = d["range_starts"]
	var stops: PackedInt32Array = d["range_stops"]
	var feats: PackedFloat32Array = d["features"]
	var off: PackedFloat32Array = d["feature_offset"]
	var scl: PackedFloat32Array = d["feature_scale"]
	const N := 27
	var x := func(f: int, i: int) -> float: return feats[f * N + i] * scl[i] + off[i]
	# Mirrored ranges follow the originals in the same order.
	var originals := names.size() / 2
	var worst_pos := 0.0
	var worst_vel := 0.0
	var worst_traj := 0.0
	var compared := 0
	for r in originals:
		var m := r + originals
		if not names[m].begins_with(names[r]):
			print(JSON.stringify({"error": "range %d is %s, not %s mirrored" % [m, names[m], names[r]]}))
			quit(1)
			return
		for k in stops[r] - starts[r] - 60:
			var f := starts[r] + k
			var g := starts[m] + k
			# Feet: left (0-2) <-> right (3-5) positions, (6-8) <-> (9-11) velocities; x negated.
			for c in 3:
				var sgn := -1.0 if c == 0 else 1.0
				worst_pos = maxf(worst_pos, absf(x.call(g, c) - sgn * x.call(f, 3 + c)))
				worst_pos = maxf(worst_pos, absf(x.call(g, 3 + c) - sgn * x.call(f, c)))
				worst_vel = maxf(worst_vel, absf(x.call(g, 6 + c) - sgn * x.call(f, 9 + c)))
				worst_vel = maxf(worst_vel, absf(x.call(g, 9 + c) - sgn * x.call(f, 6 + c)))
				worst_vel = maxf(worst_vel, absf(x.call(g, 12 + c) - sgn * x.call(f, 12 + c)))
			# Trajectory positions (15-20) and directions (21-26): (x, z) pairs, x negated.
			for t in 6:
				var i := 15 + t * 2
				worst_traj = maxf(worst_traj, absf(x.call(g, i) + x.call(f, i)))
				worst_traj = maxf(worst_traj, absf(x.call(g, i + 1) - x.call(f, i + 1)))
			compared += 1
	var out := {"database": db.get_info(), "build_ms": (Time.get_ticks_usec() - t0) / 1000.0, "tags": Array(db.get_data()["tag_names"]),
		"mirror": {"frames_compared": compared, "feet_position_max_m": snappedf(worst_pos, 0.0001), "velocity_max_m_s": snappedf(worst_vel, 0.0001), "trajectory_max": snappedf(worst_traj, 0.0001)}}
	# Tags: one character walking with turns.
	var ch := Node3D.new()
	get_root().add_child(ch)
	var skel: Skeleton3D = ClassDB.class_call_static("FellowMotionDatabase", "create_skeleton", lab.bvh_skeleton)
	ch.add_child(skel)
	var mm = ClassDB.instantiate("FellowMotionMatcher3D")
	mm.database = db
	mm.auto_update = false
	if args.has("require"):
		mm.tags_required = str(args["require"]).split("+", false)
	if args.has("exclude"):
		mm.tags_excluded = str(args["exclude"]).split("+", false)
	skel.add_child(mm)
	mm.teleport(Vector3.ZERO, Vector3(0, 0, 1))
	var req: PackedStringArray = mm.tags_required
	var exc: PackedStringArray = mm.tags_excluded
	var clips := {}
	var outside := 0
	var frames := int(float(args.get("seconds", "30")) / STEP)
	for f in frames:
		var t := f * STEP
		var ang := sin(t * 0.4) * 2.0
		var sp := 1.4 if fmod(t, 10.0) < 7.0 else (3.0 if fmod(t, 10.0) < 9.0 else 0.0)
		mm.desired_velocity = Vector3(sin(ang), 0, cos(ang)) * sp
		mm.advance(STEP)
		var clip: String = mm.get_current_clip()
		clips[clip] = clips.get(clip, 0) + 1
		var subject := "s" + clip.get_slice("_", 0).pad_zeros(3)
		var mirrored := clip.ends_with("(mirrored)")
		var ok := true
		for t2 in req:
			ok = ok and ((t2 == "mirrored" and mirrored) or t2 == subject)
		for t2 in exc:
			ok = ok and not ((t2 == "mirrored" and mirrored) or t2 == subject)
		if not ok and f > 0:
			outside += 1
	var mirrored_frames := 0
	for c in clips:
		if str(c).ends_with("(mirrored)"):
			mirrored_frames += clips[c]
	out["tags_run"] = {"required": Array(req), "excluded": Array(exc), "frames": frames, "frames_outside_tags": outside,
		"clips": clips.size(), "mirrored_share": snappedf(float(mirrored_frames) / frames, 0.001), "stats": {"trajectory_error": mm.get_stats()["trajectory_error"], "pop_p99_mm": mm.get_stats()["pop_p99_mm"]}}
	print(JSON.stringify(out))
	quit(0)
