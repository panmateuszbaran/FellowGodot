extends SceneTree
## Matching Lab's measurements without rendering (milestone 48): builds scenes/matching_lab.gd with the given args,
## runs its crowd for --seconds (60) of simulated time at 60 Hz and prints one JSON line: the matchers' global
## statistics (updates, searches and their microseconds, transitions, LOD), foot sliding while in contact, the
## distance between the character and its simulation, popping (the largest bone rotation a frame) and the main
## thread's time a step.
##   godot --headless --path benchmarks/project -s res://bench/matching_eval.gd -- --matching_crowd=16 [lab args]

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
	lab.build(args)
	var build_ms := (Time.get_ticks_usec() - t0) / 1000.0
	if lab.load_error != "":
		print(JSON.stringify({"error": lab.load_error}))
		quit(1)
		return
	ClassDB.class_call_static("FellowMotionMatcher3D", "reset_global_stats")
	var seconds := float(args.get("seconds", "60"))
	var frames := int(seconds / STEP)
	var warm := int(float(args.get("warm", "2")) / STEP)
	var step_usec := PackedFloat64Array()
	for f in range(warm + frames):
		if f == warm:
			ClassDB.class_call_static("FellowMotionMatcher3D", "reset_global_stats")
			for mm in lab._matchers:
				mm.reset_stats()
			lab.matcher_wall_usec = 0
			lab.matcher_steps = 0
		var s0 := Time.get_ticks_usec()
		lab.set_time((f + 1) * STEP)
		if f >= warm:
			step_usec.append(Time.get_ticks_usec() - s0)
		if args.get("render", "0") == "1":
			await process_frame
	var out: Dictionary = lab.sample_extra()
	var hist := PackedInt64Array()
	hist.resize(400)
	var pop_max := 0.0
	var transitions := 0
	for mm in lab._matchers:
		var st: Dictionary = mm.get_stats()
		var h: PackedInt32Array = st["pop_histogram"]
		for i in h.size():
			hist[i] += h[i]
		if float(st["pop_max_mm"]) > pop_max:
			pop_max = float(st["pop_max_mm"])
			out["pop_max_where"] = [st["pop_max_bone"], st["pop_max_after_transition"]]
		transitions += int(st["transitions"])
	var total_frames := 0
	for v in hist:
		total_frames += v
	for q in [0.5, 0.99, 0.999]:
		var target := int(ceil(q * total_frames))
		var acc := 0
		for i in hist.size():
			acc += hist[i]
			if acc >= target:
				out["pop_p%s_mm" % str(q).replace("0.", "")] = (i + 0.5) * 0.5
				break
	out["pop_max_mm"] = pop_max
	out["transitions_per_character_second"] = transitions / (seconds * lab._matchers.size())
	var sorted := step_usec.duplicate()
	sorted.sort()
	var total := 0.0
	for v in step_usec:
		total += v
	out["step_usec_mean"] = total / maxf(step_usec.size(), 1)
	out["step_usec_p99"] = sorted[int(sorted.size() * 0.99)] if sorted.size() > 0 else 0.0
	out["characters"] = lab._matchers.size()
	out["seconds"] = seconds
	out["build_ms"] = build_ms
	out["database"] = lab.database.get_info()
	out["database"].erase("roles")
	out["args"] = args
	print("MATCHING_EVAL " + JSON.stringify(out))
	quit()
