extends SceneTree
## FellowEngine's foot placement course (milestone 174, decision 0316): a CMU mocap walk (or run) made on flat ground,
## its root motion driving a CharacterBody3D (capsule, steps up to 0.35 m) along lanes side by side — flat, ramps up
## 10°, 20° and 30°, down 20°, stairs up and down (0.16 m by 0.30 m), a 20° cross slope, rubble. Each lane has two
## characters: "off" (the animation as it is) and "on" (a FellowFootPlacement3D on its skeleton).
##
## (--variants=off,on,lock,lock_on adds a FellowFootLock3D before the placement.)
##
## Measured from the pose drawn, once a frame, against the ground under each foot (rays): over the frames the
## animation has a foot down (its sole within --contact of the skeleton's origin) the sole's lowest point against the
## ground under it (sunk into it, floating over it); a foot in the air sunk more than 2 cm into the ground (a toe
## stubbing a step); legs near straight; the legs' angular acceleration (95th percentile); the hips' drop; the CPU time.
##
##   godot --headless --fixed-fps 60 --path benchmarks/tools/foot_placement -s placement.gd -- \
##       --data=<cmu dir> --out=<file.json> [--gait=walk|run] [--clip=35_08] [--seconds=10] [--lanes=flat,up20,...]
##       [--variants=off,on] [--set=probe_up:0.4,...] [--trace] [--scan]

const CMU_SCALE := 0.056444
const LEGS := [["LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase"], ["RightUpLeg", "RightLeg", "RightFoot", "RightToeBase"]]
const LANE_WIDTH := 8.0
const SETTLE := 1.0 # s before measuring
const AFTER_RESTART := 0.5 # s not measured after the clip starts again
const PLANTED := 0.4 # m/s: a sole slower than this across the ground (and low) is down

var _data := ""
var _out := ""
var _gait := "walk"
var _seconds := 10.0
var _contact := 0.04
var _lane_names: PackedStringArray = ["flat", "up10", "up20", "up30", "down20", "stairs_up", "stairs_down", "cross20", "rubble"]
var _scan := false
var _trace := false
var _want_clip := ""
var _params := {} # FellowFootPlacement3D properties from --set=name:value,...
var _variants: PackedStringArray = ["off", "on"]
var _clip := ""
var _anim: Animation
var _lib := AnimationLibrary.new()
var _bvh: Dictionary
var _chars: Array = []
var _frame := 0
var _frames := 0
var _since_restart := 0.0
var _rng := RandomNumberGenerator.new()


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		var v := a.get_slice("=", 1)
		if a.begins_with("--data="):
			_data = v
		elif a.begins_with("--out="):
			_out = v
		elif a.begins_with("--gait="):
			_gait = v
		elif a.begins_with("--seconds="):
			_seconds = float(v)
		elif a.begins_with("--contact="):
			_contact = float(v)
		elif a.begins_with("--lanes="):
			_lane_names = v.split(",")
		elif a.begins_with("--variants="):
			_variants = v.split(",")
		elif a.begins_with("--clip="):
			_want_clip = v
		elif a == "--trace":
			_trace = true
		elif a == "--scan":
			_scan = true
		elif a.begins_with("--set="):
			for kv in v.split(","):
				_params[kv.get_slice(":", 0)] = float(kv.get_slice(":", 1))
	_frames = int(round(_seconds * 60.0))
	if _want_clip == "" and not _scan:
		_want_clip = "35_08" if _gait == "walk" else "35_18" # 1.20 m/s, 3.8 s; 3.14 m/s, 1.45 s (both straight)
	_rng.seed = 11
	if not _pick_clip():
		quit(1)
		return
	if _scan:
		quit(0)
		return
	for i in _lane_names.size():
		var x := float(i) * LANE_WIDTH * 2.0
		_build_lane(_lane_names[i], x)
		for vi in _variants.size():
			var dx := (float(vi) - 0.5 * float(_variants.size() - 1)) * 1.8
			_chars.append(_make_character(_lane_names[i], _variants[vi], Vector3(x + dx, _start_height(_lane_names[i]) + 0.05, 0.5)))
	print("placement: clip %s (%s, %.2f s), %d lanes, %d characters, %.1f s" % [_clip, _gait, _anim.length, _lane_names.size(),
			_chars.size(), _seconds])


# ------------------------------------------------------------------ the clip

## The longest of CMU's clips (subjects 16, 35, 69) that goes straight at the gait's speed.
func _pick_clip() -> bool:
	var best := -1.0
	var lo := 0.9 if _gait == "walk" else 2.4
	var hi := 1.8 if _gait == "walk" else 4.2
	for f in _bvh_files():
		var b: Dictionary = ClassDB.class_call_static("FellowMotionDatabase", "import_bvh", f, CMU_SCALE, true)
		if not b.has("animation"):
			continue
		var anim: Animation = b["animation"]
		var pt := anim.find_track(NodePath("Skeleton3D:Hips"), Animation.TYPE_POSITION_3D)
		var rt := anim.find_track(NodePath("Skeleton3D:Hips"), Animation.TYPE_ROTATION_3D)
		if pt < 0 or rt < 0 or anim.length < 1.2:
			continue
		var n := anim.track_get_key_count(pt)
		var p0: Vector3 = anim.track_get_key_value(pt, 0)
		var p1: Vector3 = anim.track_get_key_value(pt, n - 1)
		var flat := Vector2(p1.x - p0.x, p1.z - p0.z)
		var speed := flat.length() / anim.length
		var dev := 0.0
		for k in n:
			var p: Vector3 = anim.track_get_key_value(pt, k)
			var rel := Vector2(p.x - p0.x, p.z - p0.z)
			dev = maxf(dev, absf(rel.cross(flat.normalized())) if flat.length() > 0.01 else 0.0)
		var q0: Quaternion = anim.track_get_key_value(rt, 0)
		var q1: Quaternion = anim.track_get_key_value(rt, n - 1)
		var f0 := q0 * Vector3(0, 0, 1)
		var f1 := q1 * Vector3(0, 0, 1)
		var turn := rad_to_deg(absf(angle_difference(atan2(f0.x, f0.z), atan2(f1.x, f1.z))))
		if _scan:
			print("%s  %.2f s  %.2f m/s  turn %.0f°  off the line %.2f m" % [f.get_file(), anim.length, speed, turn, dev])
		var wanted := f.get_file().get_basename() == _want_clip
		if wanted or (_want_clip == "" and speed >= lo and speed <= hi and turn < 15.0 and dev < 0.25 and anim.length > best):
			best = INF if wanted else anim.length
			_clip = f.get_file().get_basename()
			_anim = anim
			_bvh = b
	if _clip == "":
		printerr("placement: no straight %s clip in %s" % [_gait, _data])
		return false
	_split_root(_anim)
	_lib.add_animation(_clip, _anim)
	return true


## A game rig's root bone from the mocap's hips (as the transitions course): Root on the ground under the hips, turned
## with their heading.
func _split_root(p_anim: Animation) -> void:
	var pt := p_anim.find_track(NodePath("Skeleton3D:Hips"), Animation.TYPE_POSITION_3D)
	var rt := p_anim.find_track(NodePath("Skeleton3D:Hips"), Animation.TYPE_ROTATION_3D)
	var root_pt := p_anim.add_track(Animation.TYPE_POSITION_3D)
	p_anim.track_set_path(root_pt, NodePath("Skeleton3D:Root"))
	var root_rt := p_anim.add_track(Animation.TYPE_ROTATION_3D)
	p_anim.track_set_path(root_rt, NodePath("Skeleton3D:Root"))
	for k in p_anim.track_get_key_count(pt):
		var t := p_anim.track_get_key_time(pt, k)
		var p: Vector3 = p_anim.track_get_key_value(pt, k)
		var q: Quaternion = p_anim.track_get_key_value(rt, k)
		var fwd := q * Vector3(0, 0, 1)
		var yaw := Quaternion(Vector3.UP, atan2(fwd.x, fwd.z))
		var ground := Vector3(p.x, 0.0, p.z)
		p_anim.position_track_insert_key(root_pt, t, ground)
		p_anim.rotation_track_insert_key(root_rt, t, yaw)
		p_anim.track_set_key_value(pt, k, yaw.inverse() * (p - ground))
		p_anim.track_set_key_value(rt, k, yaw.inverse() * q)


func _bvh_files() -> Array[String]:
	var files: Array[String] = []
	for sub in ["016", "035", "069"]:
		var d := _data.path_join(sub)
		if not DirAccess.dir_exists_absolute(d):
			continue
		var names := Array(DirAccess.get_files_at(d))
		names.sort()
		for f in names:
			if f.ends_with(".bvh"):
				files.append(d.path_join(f))
	return files


# ------------------------------------------------------------------ the course

func _start_height(p_lane: String) -> float:
	match p_lane:
		"down20":
			return 14.0 * tan(deg_to_rad(20.0))
		"stairs_down":
			return 12.0 * 0.16
	return 0.0


func _box(p_size: Vector3, p_xform: Transform3D) -> void:
	var body := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = p_size
	cs.shape = shape
	body.add_child(cs)
	body.transform = p_xform
	root.add_child(body)


## A slab whose top runs from p_from along p_dir (a unit vector in the lane's plane) for p_length.
func _slab(p_x: float, p_from: Vector3, p_angle: float, p_length: float) -> Vector3:
	var basis := Basis(Vector3.RIGHT, -deg_to_rad(p_angle))
	var dir := basis * Vector3(0, 0, 1)
	var n := basis * Vector3(0, 1, 0)
	var centre := Vector3(p_x, 0, 0) + p_from + dir * (p_length * 0.5) - n * 0.25
	_box(Vector3(LANE_WIDTH, 0.5, p_length), Transform3D(basis, centre))
	return p_from + dir * p_length


func _build_lane(p_lane: String, p_x: float) -> void:
	var start := Vector3(0, _start_height(p_lane), -2.0)
	match p_lane:
		"flat":
			_slab(p_x, start, 0.0, 40.0)
		"up10", "up20", "up30":
			var a := float(p_lane.substr(2))
			_slab(p_x, _slab(p_x, start, 0.0, 4.5), a, 30.0)
		"down20":
			_slab(p_x, _slab(p_x, start, 0.0, 4.5), -20.0, 30.0)
		"stairs_up", "stairs_down":
			var up := p_lane == "stairs_up"
			var top := 12.0 * 0.16
			_slab(p_x, Vector3(0, 0.0 if up else top, -2.0), 0.0, 4.5)
			for i in 12:
				var h := 0.16 * float(i + 1) if up else top - 0.16 * float(i + 1)
				_box(Vector3(LANE_WIDTH, h, 0.30), Transform3D(Basis(), Vector3(p_x, h * 0.5, 2.5 + 0.30 * float(i) + 0.15)))
			_slab(p_x, Vector3(0, top if up else 0.0, 2.5 + 0.30 * 12.0), 0.0, 30.0)
		"cross20":
			var basis := Basis(Vector3.BACK, deg_to_rad(20.0)) # the left side (+x) higher
			_box(Vector3(LANE_WIDTH * 1.6, 0.5, 40.0), Transform3D(basis, Vector3(p_x, 0, 18.0) - basis * Vector3(0, 0.25, 0)))
		"rubble":
			_slab(p_x, start, 0.0, 40.0)
			for i in 260:
				var s := Vector3(_rng.randf_range(0.15, 0.5), _rng.randf_range(0.03, 0.12), _rng.randf_range(0.15, 0.5))
				var p := Vector3(p_x + _rng.randf_range(-LANE_WIDTH * 0.5, LANE_WIDTH * 0.5), s.y * 0.5, _rng.randf_range(1.5, 18.0))
				_box(s, Transform3D(Basis(Vector3.UP, _rng.randf_range(0.0, PI)), p))


# ------------------------------------------------------------------ the characters

func _make_character(p_lane: String, p_variant: String, p_at: Vector3) -> Dictionary:
	var body := CharacterBody3D.new()
	body.name = "%s_%s" % [p_lane, p_variant]
	body.collision_layer = 2
	body.collision_mask = 1
	body.floor_max_angle = deg_to_rad(50.0)
	body.floor_snap_length = 0.4
	body.set("floor_step_height", 0.35) # milestone 169
	var cs := CollisionShape3D.new()
	var cap := CapsuleShape3D.new()
	cap.radius = 0.3
	cap.height = 1.8
	cs.shape = cap
	cs.position = Vector3(0, 0.9, 0)
	body.add_child(cs)
	root.add_child(body)
	body.global_position = p_at
	var skel: Skeleton3D = ClassDB.class_call_static("FellowMotionDatabase", "create_skeleton", _bvh)
	skel.name = "Skeleton3D"
	var root_bone := skel.add_bone("Root")
	skel.set_bone_parent(skel.find_bone("Hips"), root_bone)
	body.add_child(skel)
	var player := AnimationPlayer.new()
	body.add_child(player)
	player.root_node = NodePath("..")
	player.add_animation_library("", _lib)
	player.root_motion_track = NodePath("Skeleton3D:Root")
	player.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
	player.play(_clip)
	var lock: Node = null
	if p_variant.begins_with("lock"):
		# FellowFootLock3D first (decision 0295): the feet held across the ground where the animation puts them down.
		lock = ClassDB.instantiate("FellowFootLock3D")
		lock.set("left_foot_bone", "LeftFoot")
		lock.set("right_foot_bone", "RightFoot")
		lock.set("left_toe_bone", "LeftToeBase")
		lock.set("right_toe_bone", "RightToeBase")
		lock.set("contact_height", 0.02) # CMU's toes pass within 5 cm of the ground in the swing
		skel.add_child(lock)
	var place: Node = null
	if p_variant.begins_with("on") or p_variant == "lock_on":
		place = ClassDB.instantiate("FellowFootPlacement3D")
		place.set("left_foot_bone", "LeftFoot")
		place.set("right_foot_bone", "RightFoot")
		place.set("left_toe_bone", "LeftToeBase")
		place.set("right_toe_bone", "RightToeBase")
		place.set("collision_mask", 1)
		for k in _params:
			place.set(k, _params[k])
		skel.add_child(place)
	var legs := []
	var sole := []
	var rest_ground := 0.0
	for side in LEGS:
		for b in [side[2], side[3]]:
			rest_ground = minf(rest_ground, skel.get_bone_global_rest(skel.find_bone(b)).origin.y)
	for side in LEGS:
		var ids := []
		for b in side:
			ids.append(skel.find_bone(b))
		legs.append(ids)
		# The sole's heel and ball points as FellowFootPlacement3D finds them (in the foot bone's space).
		var foot_rest := skel.get_bone_global_rest(ids[2])
		var ankle := foot_rest.origin
		var toe := skel.get_bone_global_rest(ids[3]).origin
		var fwd := Vector3(toe.x - ankle.x, 0, toe.z - ankle.z).normalized()
		var heel := Vector3(ankle.x, rest_ground, ankle.z) - fwd * 0.05
		sole.append([foot_rest.affine_inverse() * heel, foot_rest.affine_inverse() * Vector3(toe.x, rest_ground, toe.z)])
	var c := {"lane": p_lane, "variant": p_variant, "body": body, "skeleton": skel, "player": player, "place": place,
			"legs": legs, "sole": sole, "snap": {}, "rot_prev": [], "omega_prev": [], "stance_err": [], "swing_sunk": 0,
			"swing_frames": 0, "straight": 0, "feet_prev": [], "leg_frames": 0, "accel": [], "drop": [], "speed": [], "vy": 0.0}
	skel.skeleton_updated.connect(_on_skeleton_updated.bind(c))
	return c


func _on_skeleton_updated(c: Dictionary) -> void:
	# The pose as drawn (modifiers applied), kept for the frame's measurement.
	var skel: Skeleton3D = c.skeleton
	var xf := skel.global_transform
	var feet := []
	var ext := []
	for s in 2:
		var ids: Array = c.legs[s]
		var foot := xf * skel.get_bone_global_pose(ids[2])
		feet.append([foot * (c.sole[s][0] as Vector3), foot * (c.sole[s][1] as Vector3)])
		var h := skel.get_bone_global_pose(ids[0]).origin
		var k := skel.get_bone_global_pose(ids[1]).origin
		var a := skel.get_bone_global_pose(ids[2]).origin
		ext.append(h.distance_to(a) / (h.distance_to(k) + k.distance_to(a)))
	var rots := []
	for side in c.legs:
		for i in 3:
			rots.append(skel.get_bone_pose_rotation(side[i]))
	c.snap = {"feet": feet, "ext": ext, "rots": rots, "origin": xf.origin}


func _ground_y(p: Vector3) -> float:
	var q := PhysicsRayQueryParameters3D.create(p + Vector3(0, 1.0, 0), p - Vector3(0, 3.0, 0), 1)
	var hit := root.get_world_3d().direct_space_state.intersect_ray(q)
	return (hit.position as Vector3).y if not hit.is_empty() else -INF


func _process(p_delta: float) -> bool:
	var dt := 1.0 / 60.0
	if _frame > 0:
		_score()
	if _frame >= _frames:
		_finish()
		return true
	_since_restart += dt
	for c in _chars:
		var player: AnimationPlayer = c.player
		if player.current_animation_position + dt >= _anim.length:
			player.play(_clip, 0.3)
			player.seek(0.0, false)
	if _chars.size() > 0 and (_chars[0].player as AnimationPlayer).current_animation_position < 0.05:
		_since_restart = 0.0
	for c in _chars:
		var player: AnimationPlayer = c.player
		player.advance(dt)
		var body: CharacterBody3D = c.body
		var rm := player.get_root_motion_position()
		var v := body.global_basis * (rm / dt)
		c.vy = 0.0 if body.is_on_floor() else (c.vy as float) - 9.8 * dt
		body.velocity = Vector3(v.x, c.vy, v.z)
		body.move_and_slide()
	_frame += 1
	return false


func _score() -> void:
	var measuring := float(_frame) / 60.0 > SETTLE and _since_restart > AFTER_RESTART
	# Which feet the animation has down, and how high its soles are over its ground: the "off" character of each lane (the
	# pose as animated, over the skeleton's origin).
	var down_of := {}
	var clear_of := {}
	for c in _chars:
		if c.variant == "off" and not (c.snap as Dictionary).is_empty():
			var flags := []
			var clear := []
			for s in 2:
				var pts: Array = c.snap.feet[s]
				var o: Vector3 = c.snap.origin
				var h := minf((pts[0] as Vector3).y, (pts[1] as Vector3).y) - o.y
				# Down: low and still (its sole slower than PLANTED across the ground; low alone, a foot coming down was
				# counted while it still swept over a step's edge).
				var mid := ((pts[0] as Vector3) + (pts[1] as Vector3)) * 0.5
				var speed := INF
				if (c.feet_prev as Array).size() == 2:
					var prev: Vector3 = c.feet_prev[s]
					speed = Vector2(mid.x - prev.x, mid.z - prev.z).length() * 60.0
				flags.append(h < _contact and speed < PLANTED)
				clear.append(h)
			down_of[c.lane] = flags
			clear_of[c.lane] = clear
			var mids := []
			for s in 2:
				mids.append(((c.snap.feet[s][0] as Vector3) + (c.snap.feet[s][1] as Vector3)) * 0.5)
			c.feet_prev = mids
	for c in _chars:
		var snap: Dictionary = c.snap
		if snap.is_empty():
			continue
		# The legs' angular acceleration (deg/s²) from the frame's pose.
		var rots: Array = snap.rots
		var omega := []
		var accel := 0.0
		if (c.rot_prev as Array).size() == rots.size():
			for i in rots.size():
				var dq: Quaternion = (c.rot_prev[i] as Quaternion).inverse() * (rots[i] as Quaternion)
				var w := rad_to_deg(dq.get_angle()) * 60.0
				omega.append(dq.get_axis() * w if dq.get_angle() > 1e-6 else Vector3())
				if (c.omega_prev as Array).size() == rots.size():
					accel = maxf(accel, ((omega[i] as Vector3) - (c.omega_prev[i] as Vector3)).length() * 60.0)
		c.omega_prev = omega
		c.rot_prev = rots
		if not measuring or not down_of.has(c.lane):
			continue
		c.accel.append(accel)
		if c.place:
			c.drop.append(-float(c.place.call("get_hips_drop")))
		for s in 2:
			var pts: Array = snap.feet[s]
			var err := INF
			for p in pts:
				var g := _ground_y(p)
				if g > -INF:
					err = minf(err, (p as Vector3).y - g)
			if err == INF:
				continue
			if _trace and c.place:
				print("F %d %s foot %d down %s err %.3f raw %.3f clear %.3f off %.3f drop %.3f ext %.3f origin %.3f heel %s g %.3f ball %s g %.3f" % [_frame, c.lane, s,
						str(down_of[c.lane][s]), err - clear_of[c.lane][s], err, clear_of[c.lane][s], c.place.call("get_foot_offset", s),
						c.place.call("get_hips_drop"), snap.ext[s], (snap.origin as Vector3).y, str(pts[0]), _ground_y(pts[0]), str(pts[1]), _ground_y(pts[1])])
			if down_of[c.lane][s]:
				c.stance_err.append(err - clear_of[c.lane][s]) # against the animation's own clearance over its ground
			else:
				c.swing_frames += 1
				if err < -0.02: # in the ground
					c.swing_sunk += 1
			c.leg_frames += 1
			if (snap.ext[s] as float) > 0.995:
				c.straight += 1


func _pct(p_values: Array, p_q: float) -> float:
	if p_values.is_empty():
		return 0.0
	var v := p_values.duplicate()
	v.sort()
	return v[clampi(int(round(p_q * (v.size() - 1))), 0, v.size() - 1)]


func _finish() -> void:
	var rows := {}
	print("%-12s %-8s %9s %9s %9s %9s %8s %8s %9s %7s %7s" % ["lane", "variant", "|err| p50", "sunk p95", "float p95", "worst",
			"stub %", "straight", "legs p95", "drop", "µs"])
	for c in _chars:
		var errs: Array = c.stance_err
		var absd := []
		var sunk := []
		var flo := []
		var worst := 0.0
		for e in errs:
			absd.append(absf(e))
			sunk.append(maxf(0.0, -e))
			flo.append(maxf(0.0, e))
			worst = maxf(worst, absf(e))
		var row := {"stance_frames": errs.size(), "err_p50_cm": _pct(absd, 0.5) * 100.0, "sunk_p95_cm": _pct(sunk, 0.95) * 100.0,
				"float_p95_cm": _pct(flo, 0.95) * 100.0, "worst_cm": worst * 100.0,
				"swing_stub_share": float(c.swing_sunk) / maxf(1.0, float(c.swing_frames)),
				"straight_share": float(c.straight) / maxf(1.0, float(c.leg_frames)), "legs_accel_p95": _pct(c.accel, 0.95),
				"hips_drop_mean_cm": (_mean(c.drop) * 100.0) if not (c.drop as Array).is_empty() else 0.0,
				"hips_drop_max_cm": (_pct(c.drop, 1.0) * 100.0) if not (c.drop as Array).is_empty() else 0.0,
				"usec_per_frame": float(c.place.call("get_stats").usec_per_frame) if c.place else 0.0,
				"z_reached": (c.body as Node3D).global_position.z}
		rows["%s/%s" % [c.lane, c.variant]] = row
		print("%-12s %-8s %8.1fcm %8.1fcm %8.1fcm %8.1fcm %7.1f%% %7.1f%% %9.0f %6.1fcm %7.1f" % [c.lane, c.variant, row.err_p50_cm,
				row.sunk_p95_cm, row.float_p95_cm, row.worst_cm, row.swing_stub_share * 100.0, row.straight_share * 100.0,
				row.legs_accel_p95, row.hips_drop_mean_cm, row.usec_per_frame])
	if _out != "":
		var f := FileAccess.open(_out, FileAccess.WRITE)
		f.store_string(JSON.stringify({"clip": _clip, "gait": _gait, "seconds": _seconds, "contact": _contact, "params": _params,
				"rows": rows}, "\t"))
		f.close()


func _mean(p_values: Array) -> float:
	var s := 0.0
	for v in p_values:
		s += v
	return s / maxf(1.0, float(p_values.size()))
