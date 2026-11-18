extends Node
## The rooftops' demo: shots one after another, each putting the character somewhere (or going on from where the last
## left it) with a program of wishes, the camera following it (aimed), tracking it from a place, or on a path of its
## own. Started by `-- --demo` (`--demo_from=<shot>` to start later), or by `autostart`; with Movie Maker
## (`--write-movie`) it is the showcase's video. When the last shot ends the scene goes on as a game.

@export var autostart := false

var shots: Array = []
var shot := -1
var t := 0.0
var _next := 0
var _player: CharacterBody3D
var _rig: Node3D
var _cam: Camera3D
var _cam_pos := Vector3()
var _cam_look := Vector3()
var _target := Vector3()
var _has_target := false
var _quit_at_end := false
var _mark := INF # when the shot's mantle began (its program's "m+" times)


func _ready() -> void:
	var args := OS.get_cmdline_user_args()
	if not (autostart or args.has("--demo")):
		set_physics_process(false)
		set_process(false)
		return
	var from := 0
	for a in args:
		if a.begins_with("--demo_from="):
			from = int(a.get_slice("=", 1))
		if a == "--demo_quit":
			_quit_at_end = true
	_player = get_node("../Player")
	_rig = get_node("../CameraRig")
	_cam = _rig.get_node("Camera3D")
	_player.autopilot = true
	_player.get_node("Traversal").connect("mantle_started", func(_k, _l): _mark = minf(_mark, t))
	_make_shots()
	_start(from)


# ------------------------------------------------------------------ the shots

func _make_shots() -> void:
	var beam_arch := Vector3(1.26, 0.0, -12.42)
	shots = [
		{
			"name": "establish", "duration": 7.0,
			"place": [Vector3(-1.2, 0.0, 30.0), 0.0],
			"cam": {"mode": "path", "keys": [
				[0.0, Vector3(16.0, 24.0, 30.0), Vector3(-2.0, 10.0, -12.0), 55.0],
				[7.0, Vector3(3.0, 6.0, 11.0), Vector3(0.5, 8.0, -12.0), 60.0]]},
			"prog": [],
		},
		{
			"name": "run", "duration": 5.6,
			"place": [Vector3(-1.6, 0.0, 15.5), 0.0],
			"cam": {"mode": "follow", "yaw": 18.0, "pitch": -8.0, "rate": 3.0},
			"prog": [[0.0, {"to": Vector3(1.26, 0.0, -13.0), "sprint": true}]],
		},
		{
			"name": "climb", "duration": 5.6,
			"place": [beam_arch + Vector3(0.0, 13.6, 0.0), 0.0],
			"cam": {"mode": "track", "keys": [[0.0, Vector3(7.0, 11.0, -6.5), 44.0], [5.6, Vector3(6.2, 15.5, -7.2), 42.0]], "look_up": 0.5},
			"prog": [[0.0, {"wish": Vector3(0, 0, -1), "sprint": true}]],
		},
		{
			"name": "belfry", "duration": 30.0, "until_perch": 6.6,
			"place": [beam_arch + Vector3(0.0, 22.4, 0.0), 0.0],
			"cam": {"mode": "follow", "yaw": -25.0, "pitch": 12.0, "rate": 2.0},
			# Times "m+<s>": after the mantle into the belfry's arch began.
			"prog": [[0.0, {"wish": Vector3(0, 0, -1), "sprint": true}],
					["m+0.0", {"wish": Vector3(), "sprint": false}],
					["m+1.6", {"to_viewpoint": true, "walk": true, "aim": [140.0, -20.0, 1.2]}],
					["m+4.2", {"perch": true}]],
		},
		{
			"name": "leap", "duration": 6.0, "until_hay": true,
			"cam": {"mode": "rig"},
			"prog": [[0.0, {"jump": true}]],
		},
		{
			"name": "hay", "duration": 3.6,
			"cam": {"mode": "track", "keys": [[0.0, Vector3(-1.8, 1.3, -4.2), 48.0], [3.6, Vector3(-1.2, 1.4, -4.9), 50.0]], "look_up": 0.5},
			"prog": [],
		},
		{
			"name": "away", "duration": 6.5,
			"cam": {"mode": "path_follow", "keys": [
				[0.0, Vector3(-2.5, 2.2, -3.0), Vector3(1.0, 1.5, -9.0), 55.0],
				[6.5, Vector3(-9.0, 14.0, 10.0), Vector3(2.0, 11.0, -14.0), 60.0]]},
			"prog": [[0.3, {"to": Vector3(-8.0, 0.0, -7.5), "walk": true}]],
		},
	]


func _start(i: int) -> void:
	if i >= shots.size():
		_finish()
		return
	shot = i
	t = 0.0
	_next = 0
	_has_target = false
	_mark = INF
	var s: Dictionary = shots[i]
	if s.has("place"):
		var tr: Node = _player.get_node("Traversal")
		if tr.is_active() or tr.is_climbing() or tr.is_hanging():
			tr.call("release") if (tr.is_climbing() or tr.is_hanging()) else tr.call("cancel")
		_player.global_position = s["place"][0]
		_player.velocity = Vector3()
		_player.facing = deg_to_rad(s["place"][1])
		_player.rotation.y = _player.facing
		_player.mode = 0
		_player.wish = Vector3()
		_player.want_sprint = false
		_player.want_walk = false
	var c: Dictionary = s["cam"]
	match c["mode"]:
		"follow", "rig":
			_rig.set_process(true)
			_rig.release() if c["mode"] == "follow" else null
			if c.has("yaw"):
				_rig.yaw = deg_to_rad(c["yaw"]) + _player.facing
				_rig.pitch = deg_to_rad(c["pitch"])
		_:
			_rig.set_process(false)
	print("demo: shot %d %s" % [i, s["name"]])


func _finish() -> void:
	_player.autopilot = false
	_rig.set_process(true)
	_rig.release()
	set_physics_process(false)
	set_process(false)
	print("demo: done")
	if _quit_at_end:
		get_tree().quit()


func _physics_process(delta: float) -> void:
	if shot < 0:
		return
	t += delta
	var s: Dictionary = shots[shot]
	var prog: Array = s["prog"]
	while _next < prog.size() and _time_of(prog[_next][0]) <= t:
		var c: Dictionary = prog[_next][1]
		if c.has("wish"):
			_player.wish = c["wish"]
			_has_target = false
		if c.has("to"):
			_target = c["to"]
			_has_target = true
		if c.has("to_viewpoint"):
			var vp := get_node_or_null("../Viewpoint") as Node3D
			if vp:
				_target = vp.global_position
				_has_target = true
		if c.has("sprint"):
			_player.want_sprint = c["sprint"]
		if c.has("walk"):
			_player.want_walk = c["walk"]
		if c.has("jump"):
			_player.want_jump = true
		if c.has("perch"):
			_player.want_perch = true
		if c.has("aim"):
			s["cam"]["yaw"] = c["aim"][0]
			s["cam"]["pitch"] = c["aim"][1]
			s["cam"]["rate"] = c["aim"][2]
			s["cam"]["aim_world"] = true
		_next += 1
	if _has_target:
		var d := _target - _player.global_position
		d.y = 0.0
		_player.wish = d.normalized() if d.length() > 0.4 else Vector3()
	if s.get("until_hay", false) and _player.mode == 5:
		_start(shot + 1)
	elif s.has("until_perch") and _player.mode == 3:
		# The swirl round the perch for its time, then the next shot.
		if not s.has("_perched"):
			s["_perched"] = t
		if t - float(s["_perched"]) >= float(s["until_perch"]):
			_start(shot + 1)
	elif t >= float(s["duration"]):
		_start(shot + 1)


func _process(delta: float) -> void:
	if shot < 0 or shot >= shots.size():
		return
	var s: Dictionary = shots[shot]
	var c: Dictionary = s["cam"]
	var chest := _player.get_global_transform_interpolated().origin + Vector3.UP * 1.2
	match c["mode"]:
		"follow":
			if c.has("yaw"):
				var yaw: float = deg_to_rad(c["yaw"]) + (0.0 if c.get("aim_world", false) else _player.facing)
				_rig.aim(yaw, deg_to_rad(c["pitch"]), c["rate"], delta)
		"path", "path_follow":
			var k := _sample(c["keys"], t)
			_cam.global_position = k[0]
			var look: Vector3 = k[1]
			if c["mode"] == "path_follow":
				look = look.lerp(chest, 0.35)
			_cam.global_transform = Transform3D(Basis(), k[0]).looking_at(look)
			_cam.fov = k[2]
		"track":
			var keys: Array = c["keys"]
			var k := _sample([[keys[0][0], keys[0][1], Vector3(), keys[0][2]], [keys[1][0], keys[1][1], Vector3(), keys[1][2]]], t)
			var goal := chest + Vector3.UP * float(c.get("look_up", 0.0))
			_cam_look = goal if t < 0.05 else _cam_look.lerp(goal, 1.0 - exp(-5.0 * delta))
			_cam.global_transform = Transform3D(Basis(), k[0]).looking_at(_cam_look)
			_cam.fov = k[2]


func _time_of(v: Variant) -> float:
	if v is String and (v as String).begins_with("m+"):
		return _mark + float((v as String).substr(2))
	return float(v)


## Position, look-at and field of view along keys [t, pos, look, fov], eased between them.
func _sample(keys: Array, time: float) -> Array:
	if time <= float(keys[0][0]):
		return [keys[0][1], keys[0][2], keys[0][3]]
	for i in keys.size() - 1:
		var a: Array = keys[i]
		var b: Array = keys[i + 1]
		if time <= float(b[0]):
			var u := smoothstep(0.0, 1.0, (time - float(a[0])) / maxf(float(b[0]) - float(a[0]), 1e-4))
			return [(a[1] as Vector3).lerp(b[1], u), (a[2] as Vector3).lerp(b[2], u), lerpf(a[3], b[3], u)]
	var l: Array = keys[keys.size() - 1]
	return [l[1], l[2], l[3]]
