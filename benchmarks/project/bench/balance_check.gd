extends SceneTree
## Balance, falls and getting up (milestone 183, decision 0325), measured without rendering: Matching Lab's CMU
## database with the getting-up clips (subjects 139 and 140, tagged "get_up"), one character with
## FellowMotionMatcher3D and FellowPhysicalAnimation3D (balance on), standing on flat ground, the physics at 60 Hz:
##  * pushes at the chest from the front, growing: the capture point's offset, what follows (taken back where it
##    stands, a stagger, a fall), how far the character goes, how long until it stands again;
##  * falls pushed from the front, behind and either side: the face it lands on, the getting-up clip chosen and how
##    well its first pose fits the body (the mean distance of the bones), the bodies' fastest while blending in, how long
##    getting up takes, the hips' height standing again, and two seconds after: the bodies' distance from the animation;
##  * a push from the side while walking.
## Milestone 184 (decision 0326), each trial: the feet's slip (how far a foot moves over the ground while within 3 cm of
## its standing height: each stance's farthest from where it began, summed, until the body lies down; and in the two
## seconds after getting up), and a fall's timing — when the hips' body reaches 0.3 m and how fast it comes down then,
## how far it slides on, and when the fall went limp (its hips' height and lean then, whether at the knees).
## Milestone 185 (decision 0327): while the body is limp, the knees' and elbows' flexion at its least and most (negative:
## bent backwards) and the trunk's bend at its most (the lower back's bone against the neck's).
## Milestone 186 (decision 0328): a fall's step first — when it fell after the push, how far the character had gone
## then and how many times a foot had left the ground (`--fall_step=0`: milestone 185's fall where it stands).
## And a foot's body caught 8 cm under a thin floor (a 3 cm slab, as a roof's), standing: its distance from its place
## after 0.25, 0.5 and 1 s.
## Prints one JSON line (with --trace=1 also a line a frame: the feet, the hips, the motion matcher's feet and clip, the
## fastest body; --trace_feet=1 adds its feet's locks). Run on Jolt Physics (benchmarks/project/override.cfg:
## [physics] 3d/physics_engine="Jolt Physics"):
##   godot --headless --fixed-fps 60 --path benchmarks/project -s res://bench/balance_check.gd -- \
##       [--pushes=20,40,60,80,100,130,160,200] [--fall=200] [--trace=1] [--anatomical=0] [--grounding=0] [--fall_step=0] [--stuck_rescue=0]
## The clips: benchmarks/scripts/get_cmu_locomotion.sh (subjects 16, 35, 69, 139, 140).

const STEP := 1.0 / 60.0
const STATES := ["standing", "staggering", "falling", "down", "getting_up"]
const FACES := ["up", "down", "side"]
var lab
var args := {}
var ch: Node3D
var skel: Skeleton3D
var mm
var pa
var events := []
var joints := [] # [body, parent body, the child bone's place in the parent bone's space (rest)]
var foot_y0 := [] # the feet's bodies' heights standing
var ground: StaticBody3D


func _initialize() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)


func _floor() -> StaticBody3D:
	var body := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var b := BoxShape3D.new()
	b.size = Vector3(400, 1, 400)
	cs.shape = b
	cs.position = Vector3(0, -0.5, 0)
	body.add_child(cs)
	return body


func _joint_gap() -> float:
	# The ragdoll's integrity: each body's bone where its parent body's bone has it (their rest offset), the largest gap.
	var worst := 0.0
	for j in joints:
		var child: Transform3D = j[0].global_transform * j[0].body_offset.affine_inverse()
		var parent: Transform3D = j[1].global_transform * j[1].body_offset.affine_inverse()
		worst = maxf(worst, (child.origin - parent * j[2]).length())
	return worst


func _hips_body() -> Vector3:
	return (pa.get_node("../FellowPhysicalBones").get_child(0) as Node3D).global_position


func _chest() -> Vector3:
	var node: Node3D = pa.get_node("../FellowPhysicalBones/Body_Spine1")
	return node.global_position


func _reset(position: Vector3, facing: Vector3) -> void:
	mm.desired_velocity = Vector3.ZERO
	mm.teleport(position, facing)
	for i in 4:
		await physics_frame
	pa.reset_bodies()
	for i in 90:
		await physics_frame


func _trial(impulse: Vector3, at: Vector3, seconds: float) -> Dictionary:
	# One push, followed until the character has stood again for a second and a half (or the time is up).
	var start: Vector3 = mm.get_character_position()
	events.clear()
	pa.apply_hit(at, impulse)
	var st0: Dictionary = pa.get_balance_stats()
	var r := {"impulse_n_s": snappedf(impulse.length(), 0.1), "capture_offset_m": snappedf(float(st0["capture_offset_m"]), 0.001)}
	var states := []
	var last := -1
	var travel := 0.0
	var standing_since := -1.0
	var t := 0.0
	var got_up_at := -1.0
	var err_sum := 0.0
	var err_n := 0
	var gap := 0.0
	# Milestone 184: the feet's slip and the fall's timing (see the top).
	var feet: Array = [pa.get_node("../FellowPhysicalBones/Body_LeftFoot"), pa.get_node("../FellowPhysicalBones/Body_RightFoot")]
	var anchor: Array = [null, null]
	var stance_slip := [0.0, 0.0]
	var slip := 0.0
	var slip_max := 0.0
	var up_slip := 0.0 # the same in the two seconds after getting up (the motion matcher taking over)
	var impact_t := -1.0
	var impact_v := 0.0
	var impact_at := Vector3()
	var after_slide := 0.0
	var hips_last := _hips_body()
	# Milestone 185: the limp body's joints — the knees' and elbows' flexion at its least and most (negative: bent
	# backwards), and the trunk's bend (its lower back's bone against the neck's) at its most.
	var bones: PackedStringArray = pa.get_body_bones()
	var knee_lo := 1e9
	var knee_hi := -1e9
	var elbow_lo := 1e9
	var elbow_hi := -1e9
	var trunk_bend := 0.0
	# Milestone 186: before the fall, the feet's lifts and how far it went.
	var fell_at := -1.0
	var fell_travel := 0.0
	var lifts := 0
	var lifted := [false, false]
	while t < seconds:
		await physics_frame
		t += STEP
		var s: int = pa.get_state()
		gap = maxf(gap, _joint_gap())
		if pa.get_stats()["limp"]:
			var ang: PackedFloat32Array = pa.get_joint_angles()
			for k in ang.size():
				var bn: String = bones[k]
				if bn in ["LeftLeg", "RightLeg"]:
					knee_lo = minf(knee_lo, ang[k])
					knee_hi = maxf(knee_hi, ang[k])
				elif bn in ["LeftForeArm", "RightForeArm"]:
					elbow_lo = minf(elbow_lo, ang[k])
					elbow_hi = maxf(elbow_hi, ang[k])
			var lb: Node3D = pa.get_node_or_null("../FellowPhysicalBones/Body_LowerBack")
			var nk: Node3D = pa.get_node_or_null("../FellowPhysicalBones/Body_Neck1")
			if lb and nk:
				trunk_bend = maxf(trunk_bend, rad_to_deg((-lb.global_transform.basis.z).angle_to(-nk.global_transform.basis.z)))
		if fell_at < 0.0 and events.has("fell"):
			fell_at = t
			fell_travel = travel
		for k in 2:
			var fp: Vector3 = feet[k].global_position
			if fell_at < 0.0:
				var up: bool = fp.y > foot_y0[k] + 0.03
				if up and not lifted[k]:
					lifts += 1
				lifted[k] = up
			var after_up: bool = got_up_at >= 0.0 and t - got_up_at < 2.0
			var on: bool = fp.y < foot_y0[k] + 0.03 and ((s < 3 and t < 4.0 and got_up_at < 0.0) or after_up)
			if on and anchor[k] == null:
				anchor[k] = fp
				stance_slip[k] = 0.0
			if on:
				stance_slip[k] = maxf(stance_slip[k], Vector2(fp.x - anchor[k].x, fp.z - anchor[k].z).length())
			if not on and anchor[k] != null:
				if got_up_at >= 0.0:
					up_slip += stance_slip[k]
				else:
					slip += stance_slip[k]
					slip_max = maxf(slip_max, stance_slip[k])
				anchor[k] = null
		var hp := _hips_body()
		if impact_t < 0.0 and hp.y < 0.3:
			impact_t = t
			impact_v = (hips_last.y - hp.y) / STEP
			impact_at = hp
		if impact_t >= 0.0 and t - impact_t < 2.0:
			after_slide = maxf(after_slide, Vector2(hp.x - impact_at.x, hp.z - impact_at.z).length())
		hips_last = hp
		if args.has("trace"):
			var mf: PackedVector3Array = mm.get_foot_positions()
			var fast := ""
			var fast_v := 0.0
			for b in pa.get_node("../FellowPhysicalBones").get_children():
				if b is PhysicalBone3D and b.linear_velocity.length() > fast_v:
					fast_v = b.linear_velocity.length()
					fast = b.name
			print("tracev %.3f %d %s %.2f" % [t, s, fast, fast_v])
			var lf: Vector3 = feet[0].global_position
			var rf: Vector3 = feet[1].global_position
			var line := "trace %.3f %d L %.3f %.3f %.3f R %.3f %.3f %.3f H %.3f %.3f %.3f" % [t, s, lf.x, lf.y, lf.z, rf.x, rf.y, rf.z, hp.x, hp.y, hp.z]
			line += " ML %.3f %.3f %.3f MR %.3f %.3f %.3f" % [mf[0].x, mf[0].y, mf[0].z, mf[1].x, mf[1].y, mf[1].z]
			line += " %s %d" % [mm.get_current_clip(), mm.get_current_frame()]
			if args.has("trace_feet"):
				var ms: Dictionary = mm.get_stats()
				line += " lk %s %s" % [ms["left_locked"], ms["right_locked"]]
			print(line)
		if s != last:
			states.append([STATES[s], snappedf(t, 0.01)])
			last = s
		var p: Vector3 = mm.get_character_position() if s < 2 else _hips_body()
		travel = maxf(travel, Vector2(p.x - start.x, p.z - start.z).length())
		if got_up_at < 0.0 and events.has("got_up"):
			got_up_at = t
		if got_up_at >= 0.0 and t - got_up_at > 0.5 and t - got_up_at < 2.5:
			var e: PackedFloat32Array = pa.get_body_errors()
			for v in e:
				err_sum += v
			err_n += e.size()
		if s == 0:
			if standing_since < 0.0:
				standing_since = t
			if t - standing_since > (2.5 if got_up_at >= 0.0 else 1.5):
				break
		else:
			standing_since = -1.0
	r["states"] = states
	r["travel_m"] = snappedf(travel, 0.01)
	r["joint_gap_max_mm"] = snappedf(gap * 1000.0, 0.1)
	if knee_hi > -1e8:
		r["limp_joints_deg"] = {"knees": [snappedf(knee_lo, 1), snappedf(knee_hi, 1)], "elbows": [snappedf(elbow_lo, 1), snappedf(elbow_hi, 1)], "trunk_bend": snappedf(trunk_bend, 1)}
	for k in 2:
		if anchor[k] != null:
			if got_up_at >= 0.0:
				up_slip += stance_slip[k]
			else:
				slip += stance_slip[k]
				slip_max = maxf(slip_max, stance_slip[k])
	r["feet_slip_cm"] = snappedf(slip * 100.0, 0.1)
	r["feet_slip_max_cm"] = snappedf(slip_max * 100.0, 0.1)
	if impact_t >= 0.0:
		r["hips_down_s"] = snappedf(impact_t, 0.01)
		r["hips_down_speed_m_s"] = snappedf(impact_v, 0.01)
		r["hips_slide_after_m"] = snappedf(after_slide, 0.01)
	var st: Dictionary = pa.get_balance_stats()
	if events.has("fell"):
		r["fall"] = {"after_s": snappedf(fell_at, 0.01), "stepped_m": snappedf(fell_travel, 0.01), "feet_lifted": lifts,
			"landed": FACES[int(st["landed_face"])], "lying_after_s": snappedf(float(st["fall_seconds"]), 0.01),
			"limp_after_s": snappedf(float(st["fall_limp_seconds"]), 0.01), "limp_hips_m": snappedf(float(st["fall_limp_hips_m"]), 0.01),
			"limp_lean_deg": snappedf(float(st["fall_limp_lean_deg"]), 0.1), "kneeling": st["fall_kneeling"]}
		if events.has("got_up"):
			r["get_up"] = {"clip": st["get_up_clip"], "start_fit_m": snappedf(float(st["get_up_match_error_m"]), 0.001),
				"blend_speed_max_m_s": snappedf(float(st["get_up_blend_speed_max_m_s"]), 0.01),
				"seconds": snappedf(float(st["get_up_seconds"]), 0.01),
				"hips_height_m": snappedf(_hips_body().y, 0.001),
				"tracking_after_mm": snappedf(1000.0 * err_sum / maxf(err_n, 1), 0.1),
				"feet_slip_after_cm": snappedf(up_slip * 100.0, 0.1)}
	r["events"] = events.duplicate()
	return r


func _run() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv := a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	lab = load("res://scenes/matching_lab.gd").new()
	get_root().add_child(lab)
	lab.build({"matching_crowd": "0", "matching_subjects": "016,035,069,139,140", "matching_db": "res://external/matching/cmu_getup.res"})
	if lab.load_error != "":
		print(JSON.stringify({"error": lab.load_error}))
		quit(1)
		return
	lab.visible = false
	ground = _floor()
	get_root().add_child(ground)
	var out := {"engine": str(ProjectSettings.get_setting("physics/3d/physics_engine"))}

	ch = Node3D.new()
	get_root().add_child(ch)
	skel = ClassDB.class_call_static("FellowMotionDatabase", "create_skeleton", lab.bvh_skeleton)
	skel.modifier_callback_mode_process = Skeleton3D.MODIFIER_CALLBACK_MODE_PROCESS_PHYSICS
	ch.add_child(skel)
	mm = ClassDB.instantiate("FellowMotionMatcher3D")
	mm.database = lab.database
	skel.add_child(mm)
	mm.teleport(Vector3.ZERO, Vector3(0, 0, 1))
	pa = ClassDB.instantiate("FellowPhysicalAnimation3D")
	pa.balance = true
	pa.anatomical_limits = str(args.get("anatomical", "1")) == "1" # milestone 185: --anatomical=0 for milestone 184's ragdoll
	mm.foot_grounding = str(args.get("grounding", "1")) == "1"
	if args.has("stuck_rescue"):
		pa.stuck_rescue = str(args["stuck_rescue"]) == "1" # milestone 186: 0 for milestone 185's (a body 0.75 m away put back)
	if args.has("fall_step"):
		pa.fall_step_time = float(args["fall_step"]) # milestone 186: 0 for milestone 185's fall where it stands
	skel.add_child(pa)
	pa.motion_matcher_path = pa.get_path_to(mm)
	for sig in ["staggered", "fell", "landed", "got_up"]:
		pa.connect(sig, func(_a = null, _b = null): events.append(sig))
	for i in 3:
		await physics_frame
	if not pa.is_built():
		pa.build()
	await _reset(Vector3.ZERO, Vector3(0, 0, 1))
	for f in ["Body_LeftFoot", "Body_RightFoot"]:
		foot_y0.append((pa.get_node("../FellowPhysicalBones/" + f) as Node3D).global_position.y)
	var sim: Node = pa.get_node("../FellowPhysicalBones")
	var by_bone := {}
	for b in sim.get_children():
		if b is PhysicalBone3D:
			by_bone[skel.find_bone(b.bone_name)] = b
	for bi in by_bone:
		var pbi: int = skel.get_bone_parent(bi)
		if by_bone.has(pbi): # (only a body's own parent bone: a bone between them is posed, not at rest)
			joints.append([by_bone[bi], by_bone[pbi], (skel.get_bone_global_rest(pbi).affine_inverse() * skel.get_bone_global_rest(bi)).origin])

	# Pushes from the front at the chest, growing.
	var pushes := []
	for v in str(args.get("pushes", "20,40,60,80,100,130,160,200")).split(","):
		await _reset(Vector3.ZERO, Vector3(0, 0, 1))
		pushes.append(await _trial(Vector3(0, 0, -float(v)), _chest(), 16.0))
	out["pushes_front"] = pushes

	# Falls from every side.
	var j := float(args.get("fall", "200"))
	var falls := {}
	for side in [["front", Vector3(0, 0, -j)], ["behind", Vector3(0, 0, j)], ["left", Vector3(-j, 0, 0)], ["right", Vector3(j, 0, 0)]]:
		await _reset(Vector3.ZERO, Vector3(0, 0, 1))
		falls[side[0]] = await _trial(side[1], _chest(), 16.0)
	out["falls"] = falls

	# Milestone 186: a foot's body caught under a thin floor (a 3 cm slab where the ground was), standing.
	var slab := StaticBody3D.new()
	var slab_shape := CollisionShape3D.new()
	slab_shape.shape = BoxShape3D.new()
	slab_shape.shape.size = Vector3(400, 0.03, 400)
	slab_shape.position = Vector3(0, -0.015, 0)
	slab.add_child(slab_shape)
	get_root().add_child(slab)
	ground.position.y = -2.0
	await _reset(Vector3.ZERO, Vector3(0, 0, 1))
	var foot_body: PhysicalBone3D = pa.get_node("../FellowPhysicalBones/Body_LeftFoot")
	var fi: int = Array(pa.get_body_bones()).find("LeftFoot")
	var ft := foot_body.global_transform
	ft.origin.y -= 0.08
	PhysicsServer3D.body_set_state(foot_body.get_rid(), PhysicsServer3D.BODY_STATE_TRANSFORM, ft)
	var stuck := {}
	var r0: int = pa.get_stats()["stuck_rescues"]
	for i in 60:
		await physics_frame
		if i + 1 in [15, 30, 60]:
			stuck["after_%.2f_s_mm" % ((i + 1) / 60.0)] = snappedf(1000.0 * pa.get_body_errors()[fi], 0.1)
	stuck["put_back"] = int(pa.get_stats()["stuck_rescues"]) - r0
	out["foot_under_floor"] = stuck
	slab.queue_free()
	ground.position.y = 0.0

	# A push from the side while walking.
	await _reset(Vector3.ZERO, Vector3(0, 0, 1))
	mm.desired_velocity = Vector3(0, 0, 1.3)
	for i in 90:
		await physics_frame
	out["walking_side_push"] = await _trial(Vector3(70, 0, 0), _chest(), 6.0)
	out["stats"] = pa.get_balance_stats()
	out["get_up_clips"] = out["stats"]["get_up_clips"]
	out["stats"].erase("get_up_clips")
	out["tags_excluded"] = mm.tags_excluded
	out["drive_usec_per_step"] = snappedf(float(pa.get_stats()["drive_usec_per_step"]), 0.1)
	print(JSON.stringify(out))
	quit(0)
