extends Node
## The rooftops' demo: shots one after another, each putting the character somewhere (or going on from where the last
## left it) with a program of wishes, the camera following it (aimed), tracking it from a place, or on a path of its
## own. Started by `-- --demo` (`--demo_from=<shot>`, its index or name, to start later; `--demo_only=<name>` for one
## shot), or by `autostart`; with Movie Maker (`--write-movie`) it is the showcase's video. When the last shot ends the
## scene goes on as a game.

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
var _cam_dist := 0.0 # milestone 156: the orbit camera's distance from the chest, drawn in by walls
var _target := Vector3()
var _has_target := false
var _quit_at_end := false
var _mark := INF # when the shot's mantle began (its program's "m+" times)
var _climb: Node3D # the foot of the climb up the bell tower (ClimbStart), facing the wall
var _climbing := false # running to the climb's foot, then up the holds
var _path: Array = [] # points walked to one after another
var _house := [] # milestone 152: [the foot of a house's front, into it] while running at it and climbing it
var _only := "" # milestone 157: the one shot to play (--demo_only=<name>)
var _reference: Node3D # milestone 157: the owner's reference video performed (ReferenceShot), shown in its shot
var _sun_rotation := Vector3()
var _sky_mode := Sky.PROCESS_MODE_INCREMENTAL


func _ready() -> void:
	var args := OS.get_cmdline_user_args()
	if not (autostart or args.has("--demo")):
		set_physics_process(false)
		set_process(false)
		return
	var from := 0
	var from_name := ""
	for a in args:
		if a.begins_with("--demo_from="):
			# A shot's index or (milestone 157: the shots' indices moved) its name.
			var v := a.get_slice("=", 1)
			if v.is_valid_int():
				from = int(v)
			else:
				from_name = v
		if a == "--demo_quit":
			_quit_at_end = true
		if a.begins_with("--demo_only="):
			_only = a.get_slice("=", 1)
	_player = get_node("../Player")
	_rig = get_node("../CameraRig")
	_cam = _rig.get_node("Camera3D")
	_player.autopilot = true
	_player.get_node("Traversal").connect("mantle_started", func(_k, _l): _mark = minf(_mark, t))
	_climb = get_node_or_null("../ClimbStart") as Node3D
	_reference = get_node_or_null("../ReferenceShot") as Node3D
	_make_shots()
	if _only != "":
		from_name = _only
	if from_name != "":
		for i in shots.size():
			if shots[i]["name"] == from_name:
				from = i
	_start(from)


# ------------------------------------------------------------------ the shots

## A point in the climb's frame (x: right, facing the wall; y: up; z: out from the wall) in the world.
func _cl(p_local: Vector3) -> Vector3:
	return _climb.global_transform * p_local if _climb else p_local


func _make_shots() -> void:
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
			# Milestone 157: the owner's reference video performed 1:1 — the mannequin's every pose and its path measured
			# from it, at the replica of its house, filmed by its camera (ReferenceShot: the clip moves both).
			"name": "reference", "duration": 4.4, "reference": true,
			"cam": {"mode": "reference"},
			"prog": [],
		},
		{
			# Across the square to the bell tower, a jump for its first hold, up the rustication.
			"name": "run", "duration": 14.0, "until_y": 4.5,
			"place": [Vector3(2.6, 0.0, 11.0), 0.0],
			"cam": {"mode": "follow", "yaw": 18.0, "pitch": -8.0, "rate": 3.0},
			"prog": [[0.0, {"climb": true, "sprint": true}]],
		},
		{
			# Up the shaft from hold to hold: reaching hand over hand, leaping to the string course.
			"name": "climb", "duration": 20.0, "until_y": 11.5,
			"cam": {"mode": "track", "keys": [[0.0, _cl(Vector3(4.2, 4.0, 5.5)), 46.0], [8.0, _cl(Vector3(3.6, 10.5, 6.5)), 44.0]], "look_up": 0.4},
			"prog": [],
		},
		{
			# Wide: the tower over the square, the climber small on its face.
			"name": "climb_wide", "duration": 20.0, "until_y": 21.0,
			"cam": {"mode": "track", "keys": [[0.0, _cl(Vector3(-9.0, 9.0, 17.0)), 50.0], [10.0, _cl(Vector3(-7.0, 15.0, 15.0)), 46.0]], "look_up": 2.0},
			"prog": [],
		},
		{
			# From above, the square far below; into the belfry's arch.
			"name": "climb_top", "duration": 16.0, "until_mantle": true,
			"cam": {"mode": "track", "keys": [[0.0, _cl(Vector3(3.2, 31.5, 3.6)), 58.0], [7.0, _cl(Vector3(2.6, 30.5, 3.0)), 58.0]], "look_up": -0.6},
			"prog": [],
		},
		{
			"name": "belfry", "duration": 30.0, "until_perch": 6.6, "mantle_at_start": true,
			"cam": {"mode": "follow", "yaw": -25.0, "pitch": 12.0, "rate": 2.0},
			# Times "m+<s>": after the mantle into the belfry's arch began.
			# Into the belfry, across it behind the arches and out along the beam to its end.
			"prog": [["m+0.0", {"climb": false, "wish": Vector3(), "sprint": false}],
					["m+1.3", {"belfry_path": true, "walk": true, "aim": [140.0, -20.0, 1.2]}],
					["m+7.6", {"perch": true}]],
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
			"prog": [[0.3, {"to": Vector3(4.0, 0.0, 1.0), "walk": true}]],
		},
		{
			# Milestone 152: across to CasaCrema and up its front from the street to the roof, from hold to hold (impost band,
			# rustication, sill, pediment, sill, hood), out to the eave's lip and over it. Milestone 156 (the owner's
			# reference video): filmed as it is — behind the runner and to its side as it comes at the wall; close behind
			# and below the climber, looking up the wall; beside the body on the tiles at the roof's height.
			"name": "house", "duration": 8.0, "until_y": 2.0,
			"place": [Vector3(13.1, 0.0, -7.0), 0.0],
			"cam": {"mode": "orbit", "offset": Vector3(-1.7, -0.1, 3.0), "look_up": 0.3, "rate": 4.0, "fov": 50.0},
			"prog": [[0.5, {"house": [Vector3(13.1, 0.0, -12.9), Vector3(0.0, 0.0, -1.0)], "sprint": true}]],
		},
		{
			"name": "house_climb", "duration": 14.0, "until_mantle": true,
			"cam": {"mode": "orbit", "offset": Vector3(1.5, -0.7, 2.5), "look_up": 0.5, "rate": 2.5, "fov": 55.0},
			"prog": [],
		},
		{
			# Over the gutter and up the tiles on hands and feet (milestone 156), then the camera rising over the roofs to
			# the tower.
			"name": "roof", "duration": 7.8, "mantle_at_start": true,
			"cam": {"mode": "orbit", "offset": Vector3(2.3, 0.15, 0.9), "look_up": -0.3, "rate": 2.0, "fov": 52.0},
			"prog": [["m+1.25", {"house": [], "sprint": false, "crawl": true, "wish": Vector3(0.0, 0.0, -1.0)}],
					["m+4.2", {"crawl": false, "wish": Vector3(), "cam": {"mode": "path_follow", "keys": [
						[4.2, Vector3(10.0, 12.5, -7.0), Vector3(13.5, 12.0, -13.0), 55.0],
						[7.8, Vector3(15.5, 16.1, -2.0), Vector3(10.8, 12.5, -14.5), 58.0]]}}]],
		},
	]


## From the right arch (where the climb comes in) into the belfry, across behind the arches and out along the beam to
## the viewpoint at its end.
func _belfry_path() -> Array:
	var vp := get_node_or_null("../Viewpoint") as Node3D
	if vp == null or _climb == null:
		return []
	var inward := -_climb.global_basis.z
	var back := vp.global_basis.z # from the beam's end back into the tower
	var here := _player.global_position
	var in_right := here + inward * 1.5
	var in_left := vp.global_position + back * 3.6
	in_left.y = here.y
	var end := vp.global_position
	end.y = here.y
	return [in_right, in_left, end]


func _start(i: int) -> void:
	if shot >= 0 and shots[shot].get("reference", false):
		_show_reference(false)
	if i >= shots.size() or (_only != "" and shot >= 0):
		_finish()
		return
	shot = i
	t = 0.0
	_next = 0
	_has_target = false
	_mark = INF
	var s: Dictionary = shots[i]
	if s.get("mantle_at_start", false):
		_mark = 0.0 # the mantle that ended the last shot
	if s.has("place"):
		var tr: Node = _player.get_node("Traversal")
		if tr.is_active() or tr.is_climbing() or tr.is_hanging():
			tr.call("release") if (tr.is_climbing() or tr.is_hanging()) else tr.call("cancel")
		_player.global_position = s["place"][0]
		_player.velocity = Vector3()
		_player.facing = deg_to_rad(s["place"][1])
		_player.rotation.y = _player.facing
		_player.reset_physics_interpolation()
		var lock := _player.get_node_or_null("Model/Mannequin/Skeleton3D/FootLock")
		if lock:
			lock.call("reset")
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
	if s.get("reference", false):
		_show_reference(true)
	print("demo: shot %d %s" % [i, s["name"]])


## Milestone 157: the reference's performance shown (its mannequin, its camera current, its clip from the start; the
## player and the square's people hidden: the video has no one else; the sun turned to light the house's front from
## its left, low, as the video's does — the square's golden hour leaves the west side's fronts in shade) or hidden
## again.
func _show_reference(p_on: bool) -> void:
	if _reference == null:
		return
	_reference.visible = p_on
	_player.visible = not p_on
	var crowd := get_node_or_null("../Crowd") as Node3D
	if crowd:
		crowd.visible = not p_on
	var sun := get_node_or_null("../Sun") as DirectionalLight3D
	var we := get_node_or_null("../WorldEnvironment") as WorldEnvironment
	if we and we.environment and we.environment.sky:
		# The sky's light redrawn at once for the turned sun (incrementally, the shot's first 7 frames had the old
		# sky's ambient light and fog: hazy).
		if p_on:
			_sky_mode = we.environment.sky.process_mode
			we.environment.sky.process_mode = Sky.PROCESS_MODE_REALTIME
		else:
			we.environment.sky.process_mode = _sky_mode
	if sun:
		if p_on:
			_sun_rotation = sun.rotation
			# The house's front lit from its left (seen from the square) at 45 degrees to it, 27 degrees up (the shot's frame:
			# the house model's, its front facing +Z; at 63 degrees, along the front, the neighbours' fronts shaded it).
			sun.global_basis = Basis.looking_at(_reference.global_basis * Vector3(0.55, -0.45, -0.7).normalized(), Vector3.UP)
		else:
			sun.rotation = _sun_rotation
	var cam := _reference.get_node("ReferenceCamera") as Camera3D
	var ap := _reference.get_node("AnimationPlayer") as AnimationPlayer
	if p_on:
		ap.play(&"reference")
		ap.seek(0.0, true)
		cam.make_current()
		if shot >= 0 and Engine.get_process_frames() < 30:
			# Milestone 160: the scene just started (--demo_only=reference): its first pose held a second while the
			# sky's and the bounce's light settle at the shot's camera (they had made the clip's first frames hazy).
			ap.pause()
			shots[shot]["duration"] = float(shots[shot]["duration"]) + 1.0
			get_tree().create_timer(1.0).timeout.connect(func() -> void: ap.play(&"reference"))
	else:
		ap.stop()
		_cam.make_current()


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
		if c.has("climb"):
			_climbing = c["climb"]
		if c.has("house"):
			_house = c["house"]
		if c.has("crawl"):
			_player.want_crawl = c["crawl"]
		if c.has("cam"):
			s["cam"] = c["cam"]
		if c.has("belfry_path"):
			_path = _belfry_path()
		if c.has("aim"):
			s["cam"]["yaw"] = c["aim"][0]
			s["cam"]["pitch"] = c["aim"][1]
			s["cam"]["rate"] = c["aim"][2]
			s["cam"]["aim_world"] = true
		_next += 1
	if not _house.is_empty():
		# Milestone 152: to the house's front and on into it; on the wall, straight at it (free-running up its holds).
		var inward: Vector3 = _house[1]
		var tr: Node = _player.get_node("Traversal")
		if tr.is_hanging() or tr.is_transferring() or tr.is_active():
			_player.wish = inward
		elif _player.global_position.y > 3.0:
			_player.wish = Vector3() # up there (on the roof): standing
		else:
			# To a point 1.2 m out from the front, then on into it (milestone 153: once past that point, straight at the
			# wall — not back to the point, which turned the body round in the air, its back to the wall).
			var aim: Vector3 = (_house[0] as Vector3) - inward * 1.2
			var d: Vector3 = aim - _player.global_position
			d.y = 0.0
			_player.wish = d.normalized() if d.length() > 0.3 and -d.dot(inward) < -0.3 else inward
	elif _climbing and _climb:
		# To the climb's foot and on into the wall; on the wall, straight at it (up the holds, no shimmy).
		var inward := -_climb.global_basis.z
		var tr: Node = _player.get_node("Traversal")
		if tr.is_hanging() or tr.is_transferring() or tr.is_active():
			_player.wish = inward
		else:
			var d := _climb.global_position + inward * 1.2 - _player.global_position
			d.y = 0.0
			_player.wish = d.normalized()
	elif not _path.is_empty():
		var d: Vector3 = (_path[0] as Vector3) - _player.global_position
		d.y = 0.0
		# Milestone 155: within 0.2 m of a point before the next (0.35 m left the walk onto the beam 0.2 m off its line,
		# where the edge guard (milestone 154) stopped it).
		if d.length() < 0.2:
			_path.pop_front()
			if _path.is_empty():
				_player.wish = Vector3()
		else:
			_player.wish = d.normalized()
	elif _has_target:
		var d := _target - _player.global_position
		d.y = 0.0
		_player.wish = d.normalized() if d.length() > 0.4 else Vector3()
	if s.get("until_hay", false) and _player.mode == 5:
		_start(shot + 1)
	elif s.has("until_y") and _player.global_position.y >= float(s["until_y"]):
		_start(shot + 1)
	elif s.get("until_mantle", false) and _mark < INF:
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
		"orbit":
			# Milestone 156: by an offset in the character's frame (x its right, y up, z behind it) from its chest, eased.
			var want: Vector3 = chest + Basis(Vector3.UP, _player.facing) * (c["offset"] as Vector3)
			var look: Vector3 = chest + Vector3.UP * float(c.get("look_up", 0.0))
			var snap := t < 0.05
			_cam_pos = want if snap else _cam_pos.lerp(want, 1.0 - exp(-float(c.get("rate", 3.0)) * delta))
			_cam_look = look if snap else _cam_look.lerp(look, 1.0 - exp(-6.0 * delta))
			# Not inside a wall: drawn in along the line from the chest where the world's collision crosses it (at once),
			# back out eased (the run beside a house's front had taken the camera behind its pilaster).
			var line := _cam_pos - chest
			var reach := line.length()
			var q := PhysicsRayQueryParameters3D.create(chest, chest + line.normalized() * (reach + 0.3), _player.collision_mask)
			q.exclude = [_player.get_rid()]
			var hit := _player.get_world_3d().direct_space_state.intersect_ray(q)
			var allowed := reach
			if not hit.is_empty():
				allowed = maxf(1.2, chest.distance_to(hit.position) - 0.3)
			_cam_dist = allowed if (snap or allowed < _cam_dist) else lerpf(_cam_dist, allowed, 1.0 - exp(-4.0 * delta))
			var shown := chest + line.normalized() * minf(_cam_dist, reach)
			_cam.global_transform = Transform3D(Basis(), shown).looking_at(_cam_look)
			_cam.fov = float(c.get("fov", 50.0))
		"chase":
			# Milestone 152: behind and beside the character by an offset, eased.
			var want: Vector3 = chest + (c["offset"] as Vector3) - Vector3.UP * 1.2
			_cam_pos = want if t < 0.05 else _cam_pos.lerp(want, 1.0 - exp(-2.5 * delta))
			_cam.global_transform = Transform3D(Basis(), _cam_pos).looking_at(chest + Vector3.UP * 0.4)
			_cam.fov = 55.0
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
