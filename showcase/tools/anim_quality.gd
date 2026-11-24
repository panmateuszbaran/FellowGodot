extends SceneTree
## The showcase's animation measured (docs/showcase_animation_quality.md): the rooftops' player under a fixed program,
## its bones read after the modifiers every frame. Writes <out>/<program>.json for tools/anim_quality.py, which holds
## the gates. Headless and deterministic:
##   godot --headless --path showcase --fixed-fps 120 -s res://tools/anim_quality.gd -- --program=gaits --out=<dir>
## Programs: gaits (walk, run, sprint, the changes between them, a stop, a turn at a run), climb (the demo's run at
## CasaCrema, the climb up its front and over its eave: add -- --demo --demo_from=house).

const ANKLE_REST := 0.08 # the ankle's height standing (m)
const TOE_REST := 0.02
const ON_GROUND := 0.025 # a foot's lowest point this close to the ground: planted
const BEARING := 0.01 # a heel or a ball this close to the ground bears weight (its slide is the foot's skate)
const LIFTED := 0.06 # and this far above it since: a new step
const BINS := 50
## Milestone 156: the whole body's smoothness — each joint's turn against its parent (deg/s) and the hips' acceleration.
const SMOOTH_BONES := ["Spine", "Chest", "Neck", "Head", "LeftUpperArm", "LeftLowerArm", "LeftHand", "RightUpperArm",
		"RightLowerArm", "RightHand", "LeftUpperLeg", "LeftLowerLeg", "RightUpperLeg", "RightLowerLeg"]
## Milestone 195: the joints that must stay out of the buildings (the head, the elbows, the knees, the ankles and the
## balls of the feet — not the hands, which wrap over a ledge's edge), each looked at from the hips: a surface between
## them is a limb through a wall.
const INSIDE_BONES := ["Head", "LeftLowerArm", "RightLowerArm", "LeftLowerLeg", "RightLowerLeg", "LeftFoot", "RightFoot",
		"LeftToes", "RightToes"]

var program := "gaits"
var out_dir := "user://anim_quality"
var player: CharacterBody3D
var sk: Skeleton3D
var traversal: Node
var frame := 0
var seg := ""
var b := {}
var ankle_rest_angle: Array[float] = [0.0, 0.0]
var heel_local: Array[Vector3] = [Vector3(), Vector3()] # the heel's contact in the foot bone's space
var ball_local: Array[Vector3] = [Vector3(), Vector3()] # the sole under the ball, in the foot bone's space
# Per segment: frames, the joints' samples, their speeds; per foot: contacts, toe-offs, the stride's samples.
var segs := {}
var last := {}
var foot_state := [{}, {}]
var last_rot := {}

# [from s, segment, wish (yaw degrees, -90 = +x), walk, sprint]; a null yaw stands.
var gaits := [[0.0, "idle", null, false, false], [0.7, "walk_in", -90.0, true, false], [1.7, "walk", -90.0, true, false],
		[5.0, "run_in", -90.0, false, false], [5.8, "run", -90.0, false, false], [9.0, "sprint_in", -90.0, false, true],
		[9.7, "sprint", -90.0, false, true], [12.0, "stop", null, false, false], [13.2, "idle2", null, false, false],
		[14.0, "run2_in", -90.0, false, false], [15.0, "run2", -90.0, false, false], [16.0, "turn", 0.0, false, false],
		[17.4, "end", null, false, false]]


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--program="):
			program = a.get_slice("=", 1)
		elif a.begins_with("--out="):
			out_dir = a.get_slice("=", 1)
	var scene := (load("res://rooftops/rooftops.tscn") as PackedScene).instantiate()
	root.add_child(scene)
	player = scene.get_node("Player")
	traversal = player.get_node("Traversal")
	sk = player.get_node("Model/Mannequin/Skeleton3D")
	for n in ["Hips", "Spine", "Chest", "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "LeftToes", "RightUpperLeg", "RightLowerLeg",
			"RightFoot", "RightToes"] + SMOOTH_BONES:
		b[n] = sk.find_bone(n)
	# The ankle's angle standing (between the shin and the foot) is its zero.
	for f in 2:
		var s := "Left" if f == 0 else "Right"
		var knee := sk.get_bone_global_rest(b[s + "LowerLeg"]).origin
		var ankle := sk.get_bone_global_rest(b[s + "Foot"]).origin
		var toe := sk.get_bone_global_rest(b[s + "Toes"]).origin
		ankle_rest_angle[f] = rad_to_deg((ankle - knee).angle_to(toe - ankle))
		heel_local[f] = sk.get_bone_global_rest(b[s + "Foot"]).affine_inverse() * Vector3(ankle.x, 0.0, ankle.z - 0.05)
		ball_local[f] = sk.get_bone_global_rest(b[s + "Foot"]).affine_inverse() * Vector3(toe.x, 0.0, toe.z)
	if program == "gaits":
		player.autopilot = true
		player.position = Vector3(-25.0, 0.0, 37.0)
		player.facing = deg_to_rad(-90.0)
		player.rotation.y = player.facing
	sk.skeleton_updated.connect(_upd)


## A bone where it is drawn (the body between its physics ticks: at 120 Hz over 60 Hz ticks, read at the ticks a
## planted foot would seem to slide back and forth by the body's speed).
func _p(n: String) -> Vector3:
	return sk.get_global_transform_interpolated() * sk.get_bone_global_pose(b[n]).origin


func _ground_under(p: Vector3) -> float:
	var q := PhysicsRayQueryParameters3D.create(p + Vector3.UP * 0.6, p + Vector3.DOWN * 1.5, 1)
	var hit := player.get_world_3d().direct_space_state.intersect_ray(q)
	return (hit.position as Vector3).y if not hit.is_empty() else -INF


## The joints of one leg: the knee's flexion (negative: bent backwards), the hip's (the thigh from the trunk's line,
## forwards positive), the ankle's plantarflexion (from standing; the foot pointed down positive).
func _leg(f: int, fwd: Vector3, right: Vector3, trunk_up: Vector3) -> Dictionary:
	var s := "Left" if f == 0 else "Right"
	var hip := _p(s + "UpperLeg")
	var knee := _p(s + "LowerLeg")
	var ankle := _p(s + "Foot")
	var toe := _p(s + "Toes")
	var thigh := knee - hip
	var shin := ankle - knee
	var bend := rad_to_deg(thigh.angle_to(shin))
	var line := ankle - hip
	var off := thigh - line * (thigh.dot(line) / maxf(line.length_squared(), 1e-6))
	var knee_flex := bend if off.dot(fwd) >= -0.002 else -bend
	# The thigh in the body's side plane, from the trunk's line down.
	var t := thigh - right * thigh.dot(right)
	var down := -(trunk_up - right * trunk_up.dot(right)).normalized()
	var hip_flex := rad_to_deg(down.signed_angle_to(t.normalized(), right))
	var ankle_pf: float = ankle_rest_angle[f] - rad_to_deg(shin.angle_to(toe - ankle))
	var g := minf(ankle.y - ANKLE_REST, toe.y - TOE_REST) - _ground_under(Vector3(ankle.x, ankle.y, ankle.z))
	var foot := sk.get_global_transform_interpolated() * sk.get_bone_global_pose(b[s + "Foot"])
	var heel := foot * heel_local[f]
	var ball := foot * ball_local[f]
	return {"knee": knee_flex, "hip": hip_flex, "ankle": ankle_pf, "g": g, "ankle_pos": ankle, "toe_pos": toe, "heel_pos": heel,
			"ball_pos": ball, "g_heel": heel.y - _ground_under(heel), "g_ball": ball.y - _ground_under(ball)}


func _upd() -> void:
	var s := _segment()
	if s == "" or s == "end":
		return
	var fwd := -player.global_basis.z
	var right := player.global_basis.x
	var trunk_up := (_p("Chest") - _p("Hips")).normalized()
	var legs := [_leg(0, fwd, right, trunk_up), _leg(1, fwd, right, trunk_up)]
	var d: Dictionary = segs.get_or_add(s, {"frames": 0, "knee": [], "hip": [], "ankle": [], "g_min": INF, "skates": [],
			"speed": {"knee": 0.0, "hip": 0.0, "ankle": 0.0}, "contacts": [[], []], "toe_offs": [[], []], "curves": [[], []],
			"hips_y": [], "body_v": [], "ang": [], "hips_p": []})
	d.frames += 1
	# Milestone 195: limbs through the buildings — a surface (not the player's) between the hips and a joint.
	var space := player.get_world_3d().direct_space_state
	var hips := _p("Hips")
	var through := ""
	for n in INSIDE_BONES:
		var j := _p(n)
		var q := PhysicsRayQueryParameters3D.create(hips, j, 1, [player.get_rid()])
		var hit := space.intersect_ray(q)
		if not hit.is_empty() and hips.distance_to(hit.position) < hips.distance_to(j) - 0.04:
			through = n
			var depth: float = (hit.position as Vector3).distance_to(j) * 100.0
			d["inside_depth_cm"] = maxf(float(d.get("inside_depth_cm", 0.0)), depth)
			break
	if through != "":
		d["inside"] = int(d.get("inside", 0)) + 1
		var ib: Dictionary = d.get_or_add("inside_bones", {})
		ib[through] = int(ib.get(through, 0)) + 1
	var dt := 1.0 / 120.0
	# The fastest joint this frame (its turn against its parent's), and where the hips are drawn.
	var ang := 0.0
	var ang_bone := ""
	for n in SMOOTH_BONES:
		var bi: int = b[n]
		var parent := sk.get_bone_parent(bi)
		var q := (sk.get_bone_global_pose(parent).basis.inverse() * sk.get_bone_global_pose(bi).basis).get_rotation_quaternion()
		if last_rot.has(n) and frame > 4: # (the first frames: from the rest pose to the first animated one)
			var dq: Quaternion = (last_rot[n] as Quaternion).inverse() * q
			var w := rad_to_deg(2.0 * acos(clampf(absf(dq.w), 0.0, 1.0))) / dt
			if w > ang:
				ang = w
				ang_bone = n
		last_rot[n] = q
	d.ang.append(ang)
	# The worst moments (the fastest joints, a frame each), to find what pops.
	var worst: Array = d.get_or_add("worst", [])
	worst.append([ang, ang_bone, frame, player.get("state"), String(player.get("state"))])
	worst.sort_custom(func(x, y): return x[0] > y[0])
	if worst.size() > 6:
		worst.resize(6)
	d.hips_p.append([frame, _p("Hips")])
	d.hips_y.append(_p("Hips").y - player.global_position.y)
	d.body_v.append(Vector2(player.velocity.x, player.velocity.z).length())
	for f in 2:
		var L: Dictionary = legs[f]
		d.knee.append(L.knee)
		d.hip.append(L.hip)
		d.ankle.append(L.ankle)
		d.g_min = minf(d.g_min, L.g)
		var fs: Dictionary = foot_state[f]
		if fs.has("knee"):
			for j in ["knee", "hip", "ankle"]:
				d.speed[j] = maxf(d.speed[j], absf(L[j] - fs[j]) / dt)
			# The skate: while its heel or its ball bears weight, how far the foot's contact moves (the slower of the two
			# that touch), summed over the stance.
			var moved := INF
			if L.g_heel < BEARING:
				var hp: Vector3 = L.heel_pos
				var ph: Vector3 = fs.heel_pos
				moved = minf(moved, Vector2(hp.x - ph.x, hp.z - ph.z).length())
			if L.g_ball < BEARING:
				var bp: Vector3 = L.ball_pos
				var pb: Vector3 = fs.ball_pos
				moved = minf(moved, Vector2(bp.x - pb.x, bp.z - pb.z).length())
			if moved < INF:
				fs["skate"] = float(fs.get("skate", 0.0)) + moved
				fs["bearing"] = int(fs.get("bearing", 0)) + 1
			elif int(fs.get("bearing", 0)) > 0:
				if int(fs.bearing) >= 6:
					d.skates.append(float(fs.skate) * 100.0)
				fs["skate"] = 0.0
				fs["bearing"] = 0
		# Steps: a contact after the foot was lifted; a toe-off when it leaves the ground.
		var lifted: bool = fs.get("lifted", false)
		var planted: bool = fs.get("planted", false)
		if L.g > LIFTED:
			lifted = true
		if L.g < ON_GROUND and not planted:
			planted = true
			if lifted:
				d.contacts[f].append(d.frames)
				lifted = false
		elif L.g >= ON_GROUND and planted:
			planted = false
			d.toe_offs[f].append(d.frames)
		d.curves[f].append([L.knee, L.hip, L.ankle])
		fs.merge({"knee": L.knee, "hip": L.hip, "ankle": L.ankle, "ankle_pos": L.ankle_pos, "toe_pos": L.toe_pos, "heel_pos": L.heel_pos,
				"ball_pos": L.ball_pos,
				"lifted": lifted, "planted": planted}, true)


func _segment() -> String:
	if program == "gaits":
		return seg
	if traversal.is_hanging():
		return "hold_to_hold" if traversal.is_transferring() else "hanging"
	if traversal.is_active():
		return "mantle"
	if player.global_position.y > 8.0:
		return "roof"
	return "approach"


func _process(_d: float) -> bool:
	frame += 1
	var t := frame / 120.0
	if program == "gaits":
		var step: Array = gaits[0]
		for p in gaits:
			if t >= p[0]:
				step = p
		seg = step[1]
		if step[2] == null:
			player.wish = Vector3()
		else:
			var yaw := deg_to_rad(float(step[2]))
			player.wish = Vector3(-sin(yaw), 0.0, -cos(yaw))
		player.want_walk = step[3]
		player.want_sprint = step[4]
		if seg != "end":
			return false
	elif t < 14.0:
		return false
	_write()
	return true


## Per segment: the joints' ranges, the share of the knees within 5° of straight, the slide, the feet's lowest point;
## per foot the strides (contact to contact) resampled to BINS bins, and where in them each toe-off fell.
func _write() -> void:
	var outd := {"program": program, "segments": {}}
	for k in segs:
		var d: Dictionary = segs[k]
		var r := {"frames": d.frames, "g_min_cm": d.g_min * 100.0, "speed_deg_s": d.speed,
				"inside_share": float(d.get("inside", 0)) / maxf(1.0, d.frames), "inside_bones": d.get("inside_bones", {}),
				"inside_depth_cm": d.get("inside_depth_cm", 0.0)}
		for j in ["knee", "hip", "ankle"]:
			var a: Array = d[j].duplicate()
			a.sort()
			r[j] = {"min": a[0], "p05": a[int(a.size() * 0.05)], "median": a[a.size() / 2], "p95": a[int(a.size() * 0.95)], "max": a[-1]}
		var straight := 0
		var hyper := 0
		for x in d.knee:
			straight += 1 if absf(x) < 5.0 else 0
			hyper += 1 if x < -1.0 else 0
		r["knee_straight_share"] = float(straight) / d.knee.size()
		r["knee_hyper_share"] = float(hyper) / d.knee.size()
		# The skate per stance (cm): the median and the worst.
		var sk_: Array = d.skates.duplicate()
		sk_.sort()
		r["skate_cm"] = sk_[sk_.size() / 2] if sk_.size() > 0 else 0.0
		r["skate_max_cm"] = sk_[-1] if sk_.size() > 0 else 0.0
		r["stances"] = sk_.size()
		var hy: Array = d.hips_y
		var hs := 0.0
		for x in hy:
			hs += x
		r["hips_height"] = hs / maxf(1.0, hy.size())
		var bv := 0.0
		for x in d.body_v:
			bv += x
		r["body_speed"] = bv / maxf(1.0, d.body_v.size())
		# Strides.
		var strides := []
		var toe_off := []
		var period := []
		for f in 2:
			var c: Array = d.contacts[f]
			for i in range(c.size() - 1):
				var a0: int = c[i]
				var a1: int = c[i + 1]
				var n := a1 - a0
				if n < 8:
					continue
				var curve := []
				for bi in BINS:
					var fi := a0 - 1 + int(float(bi) / BINS * n)
					curve.append(d.curves[f][fi])
				strides.append(curve)
				period.append(n / 120.0)
				for to in d.toe_offs[f]:
					if to > a0 and to <= a1:
						toe_off.append(float(to - a0) / n)
						break
		r["strides"] = strides
		r["toe_off"] = toe_off
		r["period_s"] = period
		# Left and right: the right foot's contact within the left's stride.
		var lr := []
		var cl: Array = d.contacts[0]
		var cr: Array = d.contacts[1]
		for i in range(cl.size() - 1):
			for x in cr:
				if x > cl[i] and x <= cl[i + 1]:
					lr.append(float(x - cl[i]) / (cl[i + 1] - cl[i]))
		r["right_phase"] = lr
		# Smoothness: the fastest joint per frame and the hips' acceleration (second differences, m/s²).
		r["worst"] = d.get("worst", [])
		var an: Array = d.ang.duplicate()
		an.sort()
		if an.size() > 0:
			r["joint_deg_s"] = {"p50": an[an.size() / 2], "p95": an[int(an.size() * 0.95)], "p99": an[int(an.size() * 0.99)], "max": an[-1]}
		var acc := []
		var hp: Array = d.hips_p
		for i in range(1, hp.size() - 1):
			# Only over three frames in a row (a segment comes back after others: its samples are not continuous).
			if int(hp[i + 1][0]) - int(hp[i - 1][0]) != 2:
				continue
			var p0: Vector3 = hp[i - 1][1]
			var p1: Vector3 = hp[i][1]
			var p2: Vector3 = hp[i + 1][1]
			acc.append((p2 - 2.0 * p1 + p0).length() * 120.0 * 120.0)
		acc.sort()
		if acc.size() > 0:
			r["hips_acc"] = {"p50": acc[acc.size() / 2], "p95": acc[int(acc.size() * 0.95)], "p99": acc[int(acc.size() * 0.99)], "max": acc[-1]}
		outd.segments[k] = r
	DirAccess.make_dir_recursive_absolute(out_dir)
	var fa := FileAccess.open(out_dir.path_join(program + ".json"), FileAccess.WRITE)
	fa.store_string(JSON.stringify(outd))
	fa.close()
	print("anim_quality: %s -> %s" % [program, out_dir.path_join(program + ".json")])
