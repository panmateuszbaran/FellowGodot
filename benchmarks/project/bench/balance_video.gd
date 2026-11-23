extends SceneTree
## Balance, falls and getting up filmed (milestone 183, decision 0325): Matching Lab's character with
## FellowMotionMatcher3D and FellowPhysicalAnimation3D (balance on; the CMU database with the getting-up clips of
## subjects 139 and 140), standing and walking, pushed at the chest: a small push taken back where it stands, a bigger one
## staggers it back, a hard one from the front topples it onto its back and it gets up, one from the side while it
## walks staggers it sideways, a hard one from behind puts it on its face and it gets up. A camera at its side, 30 fps.
## Run on Jolt Physics (benchmarks/project/override.cfg: [physics] 3d/physics_engine="Jolt Physics"):
##   godot --path benchmarks/project --rendering-driver vulkan --resolution 960x540 --fixed-fps 60 \
##       -s res://bench/balance_video.gd -- --out=<dir> [--seconds=32]
## Milestone 187: --program=staggers films only staggers, standing, from the front, the side and behind, the camera
## low at the feet (13 s); --skate_guard=0 films milestone 186's feet (FellowMotionMatcher3D.foot_skate_guard off).

const PUSHES := [
	[2.0, Vector3(0, 0, -60)], # taken back where it stands
	[4.5, Vector3(0, 0, -110)], # a stagger back
	[7.5, Vector3(0, 0, -230)], # a fall onto its back
	[19.5, Vector3(85, 0, 0)], # from the side, walking
	[23.5, Vector3(0, 0, 230)], # a fall onto its face
]
const STAGGERS := [
	[1.5, Vector3(0, 0, -60)], # from the front
	[4.5, Vector3(0, 0, -80)],
	[7.5, Vector3(-60, 0, 0)], # from the side
	[10.5, Vector3(0, 0, 80)], # from behind
]


func _initialize() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)


func _run() -> void:
	var args := {}
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv := a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	var out := str(args.get("out", "/tmp/balance_video"))
	DirAccess.make_dir_recursive_absolute(out)
	var lab = load("res://scenes/matching_lab.gd").new()
	get_root().add_child(lab)
	lab.build({"matching_crowd": "1", "matching_subjects": "016,035,069,139,140", "matching_db": "res://external/matching/cmu_getup.res"})
	var mm = lab._matchers[0]
	var skel: Skeleton3D = mm.get_parent()
	skel.modifier_callback_mode_process = Skeleton3D.MODIFIER_CALLBACK_MODE_PROCESS_PHYSICS
	mm.auto_update = true
	mm.teleport(Vector3.ZERO, Vector3(0, 0, 1))
	mm.desired_velocity = Vector3.ZERO
	var staggers := str(args.get("program", "")) == "staggers"
	if args.has("skate_guard"):
		mm.foot_skate_guard = str(args["skate_guard"]) == "1"
	var pa = ClassDB.instantiate("FellowPhysicalAnimation3D")
	pa.balance = true
	skel.add_child(pa)
	pa.motion_matcher_path = pa.get_path_to(mm)
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
	var log := []
	for sig in ["staggered", "fell", "landed", "got_up"]:
		pa.connect(sig, func(_a = null, _b = null): log.append([sig, pa.get_balance_stats()]))
	var cam := Camera3D.new()
	cam.fov = 45.0
	lab.add_child(cam)
	cam.current = true
	var frames := int(float(args.get("seconds", "13" if staggers else "32")) * 30.0)
	var look := Vector3.ZERO
	var side := Vector3(2.4, 0.0, 1.4) if staggers else Vector3(3.6, 0.0, 1.2)
	for i in frames:
		var t := i / 30.0
		for p in (STAGGERS if staggers else PUSHES):
			if absf(p[0] - t) < 0.5 / 30.0:
				var chest: Node3D = skel.get_node_or_null("FellowPhysicalBones/Body_Spine1")
				if chest:
					pa.apply_hit(chest.global_position, p[1])
					print("balance_video: %.1f s push %s → %s" % [t, p[1], ["standing", "staggering", "falling", "down", "getting up"][pa.get_state()]])
		if not staggers and absf(t - 17.0) < 0.5 / 30.0:
			mm.desired_velocity = Vector3(0, 0, 1.2)
		if absf(t - 21.5) < 0.5 / 30.0:
			mm.desired_velocity = Vector3.ZERO
		for k in 2:
			await physics_frame
		var hips: Node3D = skel.get_node("FellowPhysicalBones").get_child(0)
		var p3: Vector3 = hips.global_position * Vector3(1, 0, 1)
		look = p3 if i == 0 else look.lerp(p3, 0.08)
		cam.global_position = look + side + Vector3(0, 0.8 if staggers else 1.1, 0)
		cam.look_at(look + Vector3(0, 0.45 if staggers else 0.7, 0), Vector3.UP)
		await process_frame
		await RenderingServer.frame_post_draw
		get_root().get_texture().get_image().save_png("%s/f%04d.png" % [out, i])
	for e in log:
		var st: Dictionary = e[1]
		print("balance_video: %s state %d face %d clip %s fit %.3f m" % [e[0], st["state"], st["landed_face"], st["get_up_clip"], st["get_up_match_error_m"]])
	print("balance_video: %d frames" % frames)
	quit(0)
