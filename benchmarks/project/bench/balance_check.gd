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
## Prints one JSON line. Run on Jolt Physics (benchmarks/project/override.cfg: [physics] 3d/physics_engine="Jolt Physics"):
##   godot --headless --fixed-fps 60 --path benchmarks/project -s res://bench/balance_check.gd -- \
##       [--pushes=20,40,60,80,100,130,160,200] [--fall=200]
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
	while t < seconds:
		await physics_frame
		t += STEP
		var s: int = pa.get_state()
		gap = maxf(gap, _joint_gap())
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
	var st: Dictionary = pa.get_balance_stats()
	if events.has("fell"):
		r["fall"] = {"landed": FACES[int(st["landed_face"])], "lying_after_s": snappedf(float(st["fall_seconds"]), 0.01)}
		if events.has("got_up"):
			r["get_up"] = {"clip": st["get_up_clip"], "start_fit_m": snappedf(float(st["get_up_match_error_m"]), 0.001),
				"blend_speed_max_m_s": snappedf(float(st["get_up_blend_speed_max_m_s"]), 0.01),
				"seconds": snappedf(float(st["get_up_seconds"]), 0.01),
				"hips_height_m": snappedf(_hips_body().y, 0.001),
				"tracking_after_mm": snappedf(1000.0 * err_sum / maxf(err_n, 1), 0.1)}
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
	get_root().add_child(_floor())
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
	skel.add_child(pa)
	pa.motion_matcher_path = pa.get_path_to(mm)
	for sig in ["staggered", "fell", "landed", "got_up"]:
		pa.connect(sig, func(_a = null, _b = null): events.append(sig))
	for i in 3:
		await physics_frame
	if not pa.is_built():
		pa.build()
	await _reset(Vector3.ZERO, Vector3(0, 0, 1))
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
