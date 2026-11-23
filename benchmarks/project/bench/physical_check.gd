extends SceneTree
## Physical animation (milestone 166), measured without rendering: Matching Lab's CMU database, one character walking
## with FellowMotionMatcher3D and FellowPhysicalAnimation3D over flat ground, the physics at 60 Hz:
##  * tracking: with nothing pushing, how far the bodies are from where the animation has their bones (mean, largest);
##  * hits: a push at the chest from the side, then one at the head from the front: how far the struck body is knocked,
##    how long until every body is back within 3 cm, how far the hips go (the character stays on its feet);
##  * ragdoll: strength 0: the body falls; how fast it moves, how low the hips come to rest, how far into the floor any
##    body goes, whether it is still at the end;
##  * the drive's time a step, and the run's physics frame time with and without the bodies.
## Prints one JSON line. Run with a fixed step: godot --headless --fixed-fps 60 --path benchmarks/project \
##   -s res://bench/physical_check.gd -- [--speed=1.3] [--impulse=40] [--gain=0.5]

const STEP := 1.0 / 60.0
var lab
var args := {}


func _initialize() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)


func _floor() -> StaticBody3D:
	var body := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var b := BoxShape3D.new()
	b.size = Vector3(200, 1, 200)
	cs.shape = b
	cs.position = Vector3(0, -0.5, 0)
	body.add_child(cs)
	return body


func _character(physical: bool) -> Dictionary:
	var ch := Node3D.new()
	get_root().add_child(ch)
	var skel: Skeleton3D = ClassDB.class_call_static("FellowMotionDatabase", "create_skeleton", lab.bvh_skeleton)
	skel.modifier_callback_mode_process = Skeleton3D.MODIFIER_CALLBACK_MODE_PROCESS_PHYSICS
	ch.add_child(skel)
	var mm = ClassDB.instantiate("FellowMotionMatcher3D")
	mm.database = lab.database
	skel.add_child(mm)
	mm.teleport(Vector3(0, 0, 0), Vector3(0, 0, 1))
	var pa = null
	if physical:
		pa = ClassDB.instantiate("FellowPhysicalAnimation3D")
		if args.has("gain"):
			pa.position_gain = float(args["gain"])
			pa.rotation_gain = float(args["gain"])
		skel.add_child(pa)
	return {"ch": ch, "skel": skel, "mm": mm, "pa": pa}


func _body_index(pa, bone: String) -> int:
	var names: PackedStringArray = pa.get_body_bones()
	return names.find(bone)


func _run() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv := a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	var speed := float(args.get("speed", "1.3"))
	var impulse := float(args.get("impulse", "40"))
	lab = load("res://scenes/matching_lab.gd").new()
	get_root().add_child(lab)
	lab.build({"matching_crowd": "0"})
	if lab.load_error != "":
		print(JSON.stringify({"error": lab.load_error}))
		quit(1)
		return
	lab.visible = false
	var floor := _floor()
	get_root().add_child(floor)
	var out := {"engine": str(ProjectSettings.get_setting("physics/3d/physics_engine"))}

	# The physics frame's time without the bodies, for the cost.
	var plain := _character(false)
	plain["mm"].desired_velocity = Vector3(0, 0, speed)
	var t0 := Time.get_ticks_usec()
	for i in 240:
		await physics_frame
	out["frame_usec_without"] = snappedf((Time.get_ticks_usec() - t0) / 240.0, 0.1)
	plain["ch"].queue_free()
	await physics_frame

	var c := _character(true)
	var mm = c["mm"]
	var pa = c["pa"]
	mm.desired_velocity = Vector3(0, 0, speed)
	for i in 3:
		await physics_frame
	if not pa.is_built():
		pa.build()
	out["bodies"] = pa.get_body_count()
	out["body_bones"] = pa.get_body_bones()
	# Tracking: a second to settle, then four measured.
	for i in 60:
		await physics_frame
	pa.reset_stats()
	t0 = Time.get_ticks_usec()
	var worst_track := {}
	for i in 240:
		await physics_frame
		if args.has("debug"):
			var e: PackedFloat32Array = pa.get_body_errors()
			for k in e.size():
				if e[k] > 0.05:
					worst_track[out["body_bones"][k]] = maxf(worst_track.get(out["body_bones"][k], 0.0), e[k])
					if not worst_track.has("first"):
						worst_track["first"] = [i, out["body_bones"][k], e[k]]
	out["frame_usec_with"] = snappedf((Time.get_ticks_usec() - t0) / 240.0, 0.1)
	if args.has("debug"):
		out["worst_track"] = worst_track
	var st: Dictionary = pa.get_stats()
	out["tracking"] = {"error_mean_mm": snappedf(1000.0 * float(st["tracking_error_mean_m"]), 0.1),
		"error_max_mm": snappedf(1000.0 * float(st["tracking_error_max_m"]), 0.1),
		"angle_mean_deg": snappedf(rad_to_deg(float(st["tracking_angle_mean_rad"])), 0.01),
		"drive_usec_per_step": snappedf(float(st["drive_usec_per_step"]), 0.1)}

	# Hits.
	var hits := []
	var hips_i := 0
	for spec in [["Spine1", Vector3(impulse, 0, 0)], ["Head", Vector3(0, 0, -impulse * 0.5)]]:
		var bi := _body_index(pa, spec[0])
		if bi < 0:
			hits.append({"bone": spec[0], "error": "no body"})
			continue
		var node: Node3D = pa.get_node("../FellowPhysicalBones/Body_" + spec[0])
		pa.apply_hit(node.global_position, spec[1])
		var peak := 0.0
		var hips_peak := 0.0
		var back := -1.0
		for f in 180:
			await physics_frame
			var e: PackedFloat32Array = pa.get_body_errors()
			peak = maxf(peak, e[bi])
			hips_peak = maxf(hips_peak, e[hips_i])
			var worst := 0.0
			for v in e:
				worst = maxf(worst, v)
			if back < 0.0 and f > 3 and worst < 0.03:
				back = (f + 1) * STEP
		hits.append({"bone": spec[0], "impulse_n_s": spec[1].length(), "struck_peak_mm": snappedf(peak * 1000.0, 0.1),
			"hips_peak_mm": snappedf(hips_peak * 1000.0, 0.1), "recovered_s": snappedf(back, 0.01)})
	out["hits"] = hits

	# Ragdoll.
	pa.strength = 0.0
	mm.desired_velocity = Vector3.ZERO
	var vmax := 0.0
	var lowest := 10.0
	var last_speed := 0.0
	var bodies: Array = pa.get_node("../FellowPhysicalBones").get_children()
	for f in 240:
		await physics_frame
		var sp := 0.0
		for b in bodies:
			if b is PhysicalBone3D:
				sp = maxf(sp, b.linear_velocity.length())
				for s in b.get_children():
					if s is CollisionShape3D and s.shape is BoxShape3D:
						# (Milestone 185: a foot's sole, a box — its lowest corner.)
						var e: Vector3 = s.shape.size * 0.5
						var t: Transform3D = s.global_transform
						lowest = minf(lowest, t.origin.y - absf(t.basis.x.y) * e.x - absf(t.basis.y.y) * e.y - absf(t.basis.z.y) * e.z)
					elif s is CollisionShape3D:
						lowest = minf(lowest, b.global_position.y - s.shape.radius)
		vmax = maxf(vmax, sp)
		last_speed = sp
	var hips_node: Node3D = bodies[0]
	out["ragdoll"] = {"speed_max_m_s": snappedf(vmax, 0.01), "speed_end_m_s": snappedf(last_speed, 0.001),
		"hips_rest_height_m": snappedf(hips_node.global_position.y, 0.001), "into_floor_mm": snappedf(-minf(lowest, 0.0) * 1000.0, 0.1)}
	print(JSON.stringify(out))
	quit(0)
