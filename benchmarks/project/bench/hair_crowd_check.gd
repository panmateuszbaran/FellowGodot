extends SceneTree
## FellowEngine milestone 126 check (decision 0250): many FellowHair3D nodes stepped on the worker threads at once.
##
##   godot --headless --path benchmarks/project --fixed-fps 60 -s res://bench/hair_crowd_check.gd -- --out=<dir> [--heads=24]
##     [--self_shadow=1] (milestone 144: a sun, each head's hair shadowing itself from it, decision 0268)
##
## Heads with hair (160 guides each) in a grid within the simulation distance, each turning its own way; 240 frames.
## Reported: the main thread's time for the whole group a frame (preparing, waiting for the workers, uploading) against
## the steps' time summed over the heads (what one thread would spend), and the processors available. Checked: every
## head stepped every frame; the hair's lengths kept (each head's guides within 1 % of their segments).
## One JSON line: HAIR_CROWD_CHECK {...}; exit code 1 when a check fails.

var args := {}
var out_dir := ""
var heads := []
var hairs := []
var frame := 0
var done := false


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://hair_crowd_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	_build.call_deferred()


func _build() -> void:
	var root3d := Node3D.new()
	get_root().add_child(root3d)
	var cam := Camera3D.new()
	root3d.add_child(cam)
	cam.current = true
	cam.look_at_from_position(Vector3(0, 3, 8), Vector3(0, 1.6, 0))
	var n := int(_arg("heads", "24"))
	# Milestone 144 (decision 0268): --self_shadow=1 shadows each head's hair from a sun by itself (on the workers too).
	var ss := float(_arg("self_shadow", "0"))
	if ss > 0.0:
		var sun := DirectionalLight3D.new()
		sun.rotation_degrees = Vector3(-35, 140, 0)
		root3d.add_child(sun)
	for i in n:
		var head := Node3D.new()
		head.position = Vector3((i % 6) * 0.8 - 2.0, 1.6, -(i / 6) * 0.8)
		root3d.add_child(head)
		var hair := FellowHair3D.new()
		hair.strand_count = 2000
		hair.seed = i
		hair.self_shadow = ss
		head.add_child(hair)
		heads.append(head)
		hairs.append(hair)
	physics_frame.connect(_on_frame)


func _on_frame() -> void:
	if done:
		return
	frame += 1
	for i in heads.size():
		(heads[i] as Node3D).rotation.y = deg_to_rad(40.0) * sin(frame / 30.0 + i)
	if frame == 30:
		FellowHair3D.reset_global_stats()
	if frame == 270:
		done = true
		_finish.call_deferred()


func _finish() -> void:
	var g := FellowHair3D.get_global_stats()
	var frames := maxi(1, g.frames)
	var worst := 0.0
	for hair in hairs:
		var seg: float = hair.length / float(hair.get_point_count() - 1)
		for gi in hair.get_stats().guides:
			var pts: PackedVector3Array = hair.get_guide_points(gi)
			for k in range(1, pts.size()):
				worst = maxf(worst, absf(pts[k].distance_to(pts[k - 1]) - seg) / seg)
	var result := {
		"heads": hairs.size(),
		"frames": g.frames,
		"group_ms_per_frame": snappedf(g.group_usec / 1000.0 / frames, 0.001),
		"steps_ms_per_frame_summed": snappedf(g.simulate_usec / 1000.0 / frames, 0.001),
		"upload_ms_per_frame": snappedf(g.upload_usec / 1000.0 / frames, 0.001),
		"self_shadow_ms_per_frame_summed": snappedf(g.self_shadow_usec / 1000.0 / frames, 0.001),
		"heads_stepped_per_frame": snappedf(float(g.simulated) / frames, 0.01),
		"processors": OS.get_processor_count(),
		"max_stretch": snappedf(worst, 0.0001),
	}
	result["speedup"] = snappedf((result.steps_ms_per_frame_summed + result.self_shadow_ms_per_frame_summed) / maxf(0.001, result.group_ms_per_frame - result.upload_ms_per_frame), 0.01)
	var failures := []
	if result.heads_stepped_per_frame < hairs.size() - 0.01:
		failures.append("not every head stepped")
	if worst > 0.01:
		failures.append("stretch")
	result["failures"] = failures
	print("HAIR_CROWD_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("hair_crowd_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)
