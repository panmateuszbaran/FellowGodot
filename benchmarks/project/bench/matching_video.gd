extends SceneTree
## Matching Lab filmed (milestone 164): one character followed by a camera at its side, the simulation at 60 Hz and a
## frame saved every second step (30 fps), for looking at the motion itself: its feet, turns, starts and stops.
##   godot --path benchmarks/project --rendering-driver vulkan --resolution 960x540 -s res://bench/matching_video.gd -- \
##       --out=<dir> --seconds=12 --follow=0|walker [--matching_crowd=6 and the lab's other args]

const STEP := 1.0 / 60.0


func _initialize() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)


func _run() -> void:
	var args := {"matching_crowd": "6"}
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv := a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	var out := str(args.get("out", "/tmp/matching_video"))
	DirAccess.make_dir_recursive_absolute(out)
	var lab = load("res://scenes/matching_lab.gd").new()
	get_root().add_child(lab)
	lab.build(args)
	if lab.load_error != "":
		printerr(lab.load_error)
		quit(1)
		return
	# --follow=<index>, or --follow=walker: the first character that walks (milestone 165: the strafing walk).
	var follow := 0
	if str(args.get("follow", "0")) == "walker":
		for i in lab._matchers.size():
			if float(lab._rng_state[i]["speed"]) < 2.0 and float(lab._rng_state[i]["pause_bias"]) < 0.5:
				follow = i
				break
	else:
		follow = int(args.get("follow", "0"))
	var mm = lab._matchers[follow]
	var cam := Camera3D.new()
	cam.fov = 40.0
	lab.add_child(cam)
	cam.current = true
	var seconds := float(args.get("seconds", "12"))
	var warm := float(args.get("warm", "2"))
	var t := 0.0
	# Warm up without filming.
	while t < warm:
		t += STEP
		lab.set_time(t)
	var side := Vector3(3.2, 0.0, 2.4)
	var look: Vector3 = mm.get_character_position()
	var frames := int(seconds * 30.0)
	for i in frames:
		t += STEP * 2.0
		lab.set_time(t)
		var p: Vector3 = mm.get_character_position()
		look = look.lerp(p, 0.15)
		cam.global_position = look + side + Vector3(0, 1.25, 0)
		cam.look_at(look + Vector3(0, 0.85, 0), Vector3.UP)
		await process_frame
		await RenderingServer.frame_post_draw
		get_root().get_texture().get_image().save_png("%s/f%04d.png" % [out, i])
	print("matching_video: %d frames" % frames)
	quit(0)
