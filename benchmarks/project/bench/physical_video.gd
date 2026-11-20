extends SceneTree
## Physical animation filmed (milestone 166): one CMU character walking with FellowMotionMatcher3D and
## FellowPhysicalAnimation3D, pushed at the chest and the head from alternating sides, then let go (a ragdoll); a camera
## at its side, 30 fps. Run on Jolt Physics (Godot Physics' joints jitter in a ragdoll).
##   godot --path benchmarks/project --rendering-driver vulkan --resolution 960x540 --fixed-fps 60 \
##       -s res://bench/physical_video.gd -- --out=<dir> [--impulse=40]

const STEP := 1.0 / 60.0


func _initialize() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)


func _run() -> void:
	var args := {}
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv := a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	var out := str(args.get("out", "/tmp/physical_video"))
	var impulse := float(args.get("impulse", "40"))
	DirAccess.make_dir_recursive_absolute(out)
	var lab = load("res://scenes/matching_lab.gd").new()
	get_root().add_child(lab)
	lab.build({"matching_crowd": "1"})
	# Matching Lab's own character stays (its body mesh); its matcher gets the physical animation.
	var mm = lab._matchers[0]
	var skel: Skeleton3D = mm.get_parent()
	skel.modifier_callback_mode_process = Skeleton3D.MODIFIER_CALLBACK_MODE_PROCESS_PHYSICS
	mm.auto_update = true
	var pa = ClassDB.instantiate("FellowPhysicalAnimation3D")
	skel.add_child(pa)
	var floor := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var b := BoxShape3D.new()
	b.size = Vector3(400, 1, 400)
	cs.shape = b
	cs.position = Vector3(0, -0.5, 0)
	floor.add_child(cs)
	lab.add_child(floor)
	lab.set_process(false)
	lab.set_physics_process(false)
	var cam := Camera3D.new()
	cam.fov = 40.0
	lab.add_child(cam)
	cam.current = true
	var frames := int(float(args.get("seconds", "10")) * 30.0)
	var look: Vector3 = mm.get_character_position()
	var side := Vector3(3.0, 0.0, 1.6)
	var hits := [[60, "Spine1", Vector3(1, 0, 0)], [105, "Head", Vector3(-1, 0, 0)], [150, "Spine1", Vector3(-1, 0, 0.3)], [195, "Head", Vector3(0, 0, -1)]]
	mm.desired_velocity = Vector3(0, 0, 1.3)
	for i in frames:
		for h in hits:
			if h[0] == i:
				var node: Node3D = skel.get_node_or_null("FellowPhysicalBones/Body_" + h[1])
				if node:
					pa.apply_hit(node.global_position, h[2].normalized() * (impulse if h[1] == "Spine1" else impulse * 0.5))
		if i == 240:
			pa.strength = 0.0
			mm.desired_velocity = Vector3.ZERO
		for k in 2:
			await physics_frame
		var p: Vector3 = mm.get_character_position() if i < 240 else skel.get_node("FellowPhysicalBones").get_child(0).global_position * Vector3(1, 0, 1)
		look = look.lerp(p, 0.15)
		cam.global_position = look + side + Vector3(0, 1.2, 0)
		cam.look_at(look + Vector3(0, 0.8, 0), Vector3.UP)
		await process_frame
		await RenderingServer.frame_post_draw
		get_root().get_texture().get_image().save_png("%s/f%04d.png" % [out, i])
	print("physical_video: %d frames" % frames)
	quit(0)
