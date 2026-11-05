extends SceneTree
## FellowWorldStreamer3D.split_into_cells() (milestone 50): a level of boxes split into cells, then streamed back.
## Prints one JSON line: the cells made, the nodes saved, the nodes left in the level (the light, the ground wider than
## the cells), and after load_now() how far every box is from where it was (0 expected).
##   godot --headless --path benchmarks/project -s res://bench/split_check.gd -- [--boxes=200] [--cell=64]

func _initialize() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)


func _run() -> void:
	var args := {}
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv := a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	var count := int(args.get("boxes", "200"))
	var cell := float(args.get("cell", "64"))
	var dir := "user://split_check/cells"
	var level := Node3D.new()
	level.name = "Level"
	get_root().add_child(level)
	var light := DirectionalLight3D.new()
	light.name = "Sun"
	level.add_child(light)
	var ground := MeshInstance3D.new()
	ground.name = "Ground"
	var plane := PlaneMesh.new()
	plane.size = Vector2(512, 512)
	ground.mesh = plane
	level.add_child(ground)
	var rng := RandomNumberGenerator.new()
	rng.seed = 50
	var box := BoxMesh.new()
	var expected := {}
	for i in count:
		var m := MeshInstance3D.new()
		m.name = "Box%d" % i
		m.mesh = box
		m.position = Vector3(rng.randf_range(-128, 128), rng.randf_range(0, 4), rng.randf_range(-128, 128))
		m.rotation.y = rng.randf_range(-PI, PI)
		level.add_child(m)
		expected[m.name] = m.global_transform
	var streamer = ClassDB.instantiate("FellowWorldStreamer3D")
	streamer.name = "Streamer"
	streamer.set("use_camera", false)
	get_root().add_child(streamer)
	var t0 := Time.get_ticks_usec()
	var cells: Array = streamer.split_into_cells(level, cell, dir, {})
	var split_ms := (Time.get_ticks_usec() - t0) / 1000.0
	var left := []
	for c in level.get_children():
		left.append(String(c.name))
	streamer.prefetch(Vector3.ZERO, 1000.0)
	var loaded: int = streamer.load_now()
	var found := 0
	var worst := 0.0
	for c in cells:
		var content = c.get_content()
		if content == null:
			continue
		for n in content.get_children():
			if expected.has(n.name):
				found += 1
				var a: Transform3D = expected[n.name]
				var b: Transform3D = n.global_transform
				worst = maxf(worst, a.origin.distance_to(b.origin))
				worst = maxf(worst, (a.basis.x - b.basis.x).length())
	var files := DirAccess.get_files_at(dir)
	print("SPLIT_CHECK ", JSON.stringify({"boxes": count, "cell": cell, "cells": cells.size(), "files": files.size(),
		"split_ms": split_ms, "left_in_level": left, "loaded": loaded, "boxes_back": found, "worst_offset": worst}))
	quit(0 if found == count and left.size() == 2 else 1)
