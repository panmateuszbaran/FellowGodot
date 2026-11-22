extends SceneTree
## FellowEngine's animation transitions (milestone 171, decision 0313): CMU mocap clips (benchmarks/scripts/
## get_cmu_locomotion.sh) switched every --every seconds to a random clip at a random time, the same sequence played by
## characters side by side:
##
## * ref: the switch at once, nothing hiding it (the target's own motion: which feet are planted);
## * xfade: AnimationPlayer's cross-fade over --blend seconds (both animations evaluated meanwhile);
## * dead: the switch at once, a FellowInertializer3D over --blend seconds (dead blending: only the new one evaluated);
## * auto: the same, its blends started by the AnimationPlayer's animation_started signal (animation_mixer_path).
##
## Over each transition's window (the switch and --blend seconds after it): the feet's sliding (their horizontal speed
## in the world, root motion applied, over the frames the reference has them planted), the largest bone turn in a frame
## (pops), and the animation's CPU time a frame (AnimationPlayer.advance(), and the modifier's). Prints and writes JSON.
##
##   godot --headless --fixed-fps 60 --path benchmarks/tools/animation_transitions -s transitions.gd -- \
##       --data=<cmu dir> --out=<file.json> [--blend=0.25] [--halflife=0.05] [--every=1.0] [--switches=120] [--seed=7]

const CMU_SCALE := 0.056444
const FEET := ["LeftToeBase", "RightToeBase"]
const PLANTED := 0.15 # m/s: the reference's foot slower than this is planted

var _data := ""
var _out := ""
var _blend := 0.25
var _halflife := 0.05
var _every := 1.0
var _switches := 120
var _seed := 7
var _clips: Array[String] = []
var _chars := {} # name -> {root, skeleton, player, inertializer, xform, feet_prev, ...}
var _schedule: Array = [] # [clip, start time]
var _frame := 0
var _frames_per_switch := 60
var _stats := {}


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		var v := a.get_slice("=", 1)
		if a.begins_with("--data="):
			_data = v
		elif a.begins_with("--out="):
			_out = v
		elif a.begins_with("--blend="):
			_blend = float(v)
		elif a.begins_with("--halflife="):
			_halflife = float(v)
		elif a.begins_with("--every="):
			_every = float(v)
		elif a.begins_with("--switches="):
			_switches = int(v)
		elif a.begins_with("--seed="):
			_seed = int(v)
	_frames_per_switch = int(round(_every * 60.0))
	var files := _bvh_files()
	if files.is_empty():
		printerr("transitions: no BVH files in ", _data)
		quit(1)
		return
	var lib := AnimationLibrary.new()
	var bvh0: Dictionary = {}
	for f in files:
		var b: Dictionary = ClassDB.class_call_static("FellowMotionDatabase", "import_bvh", f, CMU_SCALE, true)
		if not b.has("animation"):
			continue
		if bvh0.is_empty():
			bvh0 = b
		var anim: Animation = b["animation"]
		if anim.length < _every + _blend + 0.5:
			continue
		var clip_name := f.get_file().get_basename()
		lib.add_animation(clip_name, anim)
		_clips.append(clip_name)
	var rng := RandomNumberGenerator.new()
	rng.seed = _seed
	for i in _switches:
		var clip: String = _clips[rng.randi() % _clips.size()]
		var anim := lib.get_animation(clip)
		_schedule.append([clip, rng.randf_range(0.0, anim.length - _every - _blend - 0.2)])
	for n in ["ref", "xfade", "dead", "auto"]:
		_chars[n] = _make_character(n, bvh0, lib)
	print("transitions: %d clips, %d switches every %.2f s, blend %.2f s, halflife %.3f s" % [_clips.size(), _switches,
			_every, _blend, _halflife])


func _bvh_files() -> Array[String]:
	var files: Array[String] = []
	for sub in ["016", "035", "069"]:
		var d := _data.path_join(sub)
		if not DirAccess.dir_exists_absolute(d):
			continue
		var names := Array(DirAccess.get_files_at(d))
		names.sort()
		for f in names:
			if not f.ends_with(".bvh"):
				continue
			var n := int(f.get_basename().split("_")[1])
			if sub == "016" and (n <= 7 or n == 9 or n == 10):
				continue # subject 16's jumps are not locomotion (as Matching Lab)
			files.append(d.path_join(f))
	return files


func _make_character(p_name: String, p_bvh: Dictionary, p_lib: AnimationLibrary) -> Dictionary:
	var node := Node3D.new()
	node.name = p_name
	root.add_child(node)
	var skel: Skeleton3D = ClassDB.class_call_static("FellowMotionDatabase", "create_skeleton", p_bvh)
	skel.name = "Skeleton3D"
	node.add_child(skel)
	var player := AnimationPlayer.new()
	node.add_child(player)
	player.root_node = NodePath("..")
	player.add_animation_library("", p_lib)
	player.root_motion_track = NodePath("Skeleton3D:Hips")
	player.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
	var inert: Node = null
	if p_name == "dead" or p_name == "auto":
		inert = ClassDB.instantiate("FellowInertializer3D")
		inert.set("blend_time", _blend)
		inert.set("extrapolation_halflife", _halflife)
		skel.add_child(inert)
		if p_name == "auto":
			# Blends started by the player's animation_started signal, not by hand: the same as "dead".
			inert.set("animation_mixer_path", inert.get_path_to(player))
	var feet := []
	for f in FEET:
		feet.append(skel.find_bone(f))
	var bones := skel.get_bone_count()
	skel.skeleton_updated.connect(_on_skeleton_updated.bind(p_name))
	return {"node": node, "skeleton": skel, "player": player, "inert": inert, "xform": Transform3D(), "feet": feet,
			"feet_prev": [], "rot_prev": [], "bones": bones, "usec": 0, "slide": 0.0, "slide_frames": 0,
			"slide_all": 0.0, "slide_all_frames": 0, "max_turn": [], "speeds_last": [], "turn_last": 0.0,
			"omega_prev": [], "accel_last": 0.0, "max_accel": [],
			"window_last": false, "switch_last": false}


func _process(p_delta: float) -> bool:
	var dt := 1.0 / 60.0
	# The previous frame's poses (read as each skeleton finished updating, its modifiers applied): scored against the
	# reference's planted feet.
	if _frame > 0:
		_score()
	var k := _frame % _frames_per_switch
	var switch := _frame / _frames_per_switch
	if switch >= _switches:
		_finish()
		return true
	if k == 0:
		var clip: String = _schedule[switch][0]
		var t0: float = _schedule[switch][1]
		for n in _chars:
			var c: Dictionary = _chars[n]
			var player: AnimationPlayer = c.player
			player.play(clip, _blend if (n == "xfade" and switch > 0) else 0.0)
			player.seek(t0, false)
			if n == "dead" and switch > 0:
				c.inert.call("blend")
	_window = switch > 0 and k <= int(round(_blend * 60.0)) + 1
	_switch_frame = switch > 0 and k == 0
	for n in _chars:
		var c: Dictionary = _chars[n]
		var player: AnimationPlayer = c.player
		var t := Time.get_ticks_usec()
		player.advance(dt)
		c.usec += Time.get_ticks_usec() - t
		var xf: Transform3D = c.xform
		c.xform = xf * Transform3D(Basis(player.get_root_motion_rotation()), player.get_root_motion_position())
	_frame += 1
	return false


var _window := false
var _switch_frame := false


func _on_skeleton_updated(p_name: String) -> void:
	var c: Dictionary = _chars[p_name]
	var skel: Skeleton3D = c.skeleton
	var xf: Transform3D = c.xform
	var feet_now := []
	for b in c.feet:
		feet_now.append(xf * skel.get_bone_global_pose(b).origin)
	var speeds := []
	if not c.feet_prev.is_empty():
		for i in feet_now.size():
			var d: Vector3 = feet_now[i] - c.feet_prev[i]
			speeds.append(Vector2(d.x, d.z).length() * 60.0)
	c.feet_prev = feet_now
	var rots := []
	var omegas := []
	var turn := 0.0
	var accel := 0.0
	for b in c.bones:
		var q := skel.get_bone_pose_rotation(b)
		rots.append(q)
		if not c.rot_prev.is_empty():
			var qp: Quaternion = c.rot_prev[b]
			turn = maxf(turn, rad_to_deg(qp.angle_to(q)))
			# The bone's angular velocity (rotation vector a second) and its change from the last frame's.
			var dq := (q * qp.inverse()).normalized()
			if dq.w < 0.0:
				dq = -dq
			var w := dq.get_axis() * dq.get_angle() * 60.0 if dq.get_angle() > 1e-6 else Vector3()
			omegas.append(w)
			if not c.omega_prev.is_empty():
				accel = maxf(accel, rad_to_deg(((c.omega_prev[b] as Vector3) - w).length() * 60.0))
	c.rot_prev = rots
	c.omega_prev = omegas
	c.accel_last = accel
	c.speeds_last = speeds
	c.turn_last = turn
	c.window_last = _window
	c.switch_last = _switch_frame


func _score() -> void:
	var ref: Dictionary = _chars["ref"]
	var planted := []
	for s in ref.speeds_last:
		planted.append(s < PLANTED)
	for n in _chars:
		var c: Dictionary = _chars[n]
		var speeds: Array = c.speeds_last
		if speeds.is_empty() or speeds.size() != planted.size():
			continue
		if c.window_last:
			(c.max_turn as Array).append(c.turn_last)
			(c.max_accel as Array).append(c.accel_last)
		if c.switch_last:
			continue # the reference's own switch frame: its feet jump, it says nothing of planting
		for i in speeds.size():
			if planted[i]:
				c.slide_all += speeds[i]
				c.slide_all_frames += 1
				if c.window_last:
					c.slide += speeds[i]
					c.slide_frames += 1


func _finish() -> void:
	var rows := {}
	for n in _chars:
		var c: Dictionary = _chars[n]
		var turns: Array = c.max_turn
		turns.sort()
		var accels: Array = c.max_accel
		accels.sort()
		var row := {
			"slide_in_transitions": snappedf(c.slide / maxf(c.slide_frames, 1), 0.0001),
			"slide_frames": c.slide_frames,
			"slide_everywhere": snappedf(c.slide_all / maxf(c.slide_all_frames, 1), 0.0001),
			"turn_p50": snappedf(turns[turns.size() / 2], 0.01) if not turns.is_empty() else 0.0,
			"turn_p95": snappedf(turns[int(turns.size() * 0.95)], 0.01) if not turns.is_empty() else 0.0,
			"turn_max": snappedf(turns[-1], 0.01) if not turns.is_empty() else 0.0,
			"accel_p50": snappedf(accels[accels.size() / 2], 1.0) if not accels.is_empty() else 0.0,
			"accel_p95": snappedf(accels[int(accels.size() * 0.95)], 1.0) if not accels.is_empty() else 0.0,
			"usec_per_frame": snappedf(float(c.usec) / maxf(_frame, 1), 0.01),
		}
		if c.inert:
			row["modifier"] = c.inert.call("get_stats")
		rows[n] = row
		print("%-6s slide in transitions %.3f m/s (%d foot-frames), everywhere %.3f m/s; bone turn in a frame p50 %.2f p95 %.2f max %.2f deg; angular acceleration p50 %.0f p95 %.0f deg/s2; %.1f us a frame%s" % [
				n, row.slide_in_transitions, row.slide_frames, row.slide_everywhere, row.turn_p50, row.turn_p95,
				row.turn_max, row.accel_p50, row.accel_p95, row.usec_per_frame,
				(" + modifier %.1f us" % float(row.modifier.usec_per_frame)) if row.has("modifier") else ""])
	if _out != "":
		var f := FileAccess.open(_out, FileAccess.WRITE)
		f.store_string(JSON.stringify({"blend": _blend, "halflife": _halflife, "every": _every, "switches": _switches,
				"clips": _clips.size(), "rows": rows}, " "))
