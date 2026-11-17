extends SceneTree
## FellowEngine milestone 140 check (decision 0265): animation/update_lod — Crowd Lab's characters (52 bones, a rotation
## track per bone, each its own AnimationPlayer and Skeleton3D) playing as a game plays them (the idle process, not
## Crowd Lab's manual seeks), on a field --area metres square with a camera at its edge looking across it: how many
## evaluate each frame, by distance band, and the main thread's time of the scene's process (headless: the dummy
## renderer; the cloud's timings vary, compare interleaved runs). Every evaluation is checked to put its clip at the time
## it has without the setting.
##
##   godot --headless --path benchmarks/project --fixed-fps 60 -s res://bench/animation_lod_check.gd
##       -- [--crowd=256] [--area=120] [--frames=600] [--set=update_lod=true,...] [--blend=0]
##
## --set takes rendering/fellow/animation/* settings (without the prefix). One JSON line: ANIMATION_LOD_CHECK {...}.

var args := {}
var crowd
var frame := 0
var frames := 600
var stats0 := {}
var cam: Camera3D
var players: Array = []
var last_position: Array = [] # each player's clip position last frame: moved = evaluated
var evaluations := 0
var most_in_a_frame := 0 # the evaluations of the busiest frame (the crowd's spread over the intervals)
var evaluations_by_band := [0, 0, 0, 0, 0] # within 20, 40, 80, 160 m, beyond (or out of view)
var characters_by_band := [0, 0, 0, 0, 0]
var time_errors := 0
var max_time_error := 0.0
var dt := 1.0 / 60.0
var start_position: Array = []
var loop_ms := 0.0
var t_loop := 0


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	for kv in str(args.get("set", "")).split(",", false):
		var p: PackedStringArray = kv.split("=", true, 1)
		if p.size() == 2:
			var v: Variant = p[1]
			if p[1] == "true" or p[1] == "false":
				v = p[1] == "true"
			elif p[1].is_valid_int():
				v = int(p[1])
			elif p[1].is_valid_float():
				v = float(p[1])
			ProjectSettings.set_setting("rendering/fellow/animation/" + p[0], v)
	frames = int(args.get("frames", "600"))
	var area := float(args.get("area", "120"))
	crowd = load("res://scenes/crowd_lab.gd").new()
	crowd.name = "Crowd"
	root.add_child(crowd)
	crowd.args = {}
	crowd.crowd = int(args.get("crowd", "256"))
	crowd.blend = false
	crowd._area = area
	crowd._define_bones()
	crowd._build_mesh()
	crowd._build_clips()
	crowd._build_crowd()
	# Spread over the field (a grid area metres wide), each playing its clip from its phase in the idle process.
	var n: int = crowd._chars.size()
	var side := int(ceil(sqrt(float(n))))
	for i in n:
		var ch: Node3D = crowd._chars[i]
		ch.position = Vector3((float(i % side) + 0.5) / side * area - area * 0.5, 0.0, (float(i / side) + 0.5) / side * area)
		var ap: AnimationPlayer = crowd._players[i]
		ap.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_IDLE
		ap.seek(crowd._phase[i], true)
		players.append(ap)
		start_position.append(ap.current_animation_position)
		last_position.append(-1.0)
	# The camera at the field's near edge, eye height, looking across it (75 degrees): some characters out of view.
	cam = Camera3D.new()
	cam.fov = 75.0
	root.add_child(cam)
	cam.transform = Transform3D(Basis(), Vector3(0.0, 1.7, -2.0)).looking_at(Vector3(0.0, 1.2, area * 0.5))
	cam.make_current()


func _band(ch: Node3D) -> int:
	var d := ch.global_position.distance_to(cam.global_position)
	if not cam.is_position_in_frustum(ch.global_position):
		return 4
	return 0 if d <= 20.0 else (1 if d <= 40.0 else (2 if d <= 80.0 else (3 if d <= 160.0 else 4)))


func _process(_delta: float) -> bool:
	frame += 1
	if frame == 20:
		stats0 = RenderingServer.call("fellow_get_stats") if RenderingServer.has_method("fellow_get_stats") else {}
		t_loop = Time.get_ticks_usec()
	# The previous frame's evaluations (the mixers ran in its process step).
	var this_frame := 0
	for i in players.size():
		var ap: AnimationPlayer = players[i]
		var pos := ap.current_animation_position
		if frame > 20:
			var band := _band(crowd._chars[i])
			characters_by_band[band] += 1
			if pos != last_position[i]:
				evaluations += 1
				this_frame += 1
				evaluations_by_band[band] += 1
				# Its clip where it is without the setting: its phase and (frame - 1) steps (this frame's step is not
				# processed yet).
				var length: float = ap.get_animation(ap.current_animation).length
				var expected := fposmod(start_position[i] + (frame - 1) * dt, length)
				var err := absf(ap.current_animation_position - expected)
				err = minf(err, length - err)
				max_time_error = maxf(max_time_error, err)
				if err > 1e-4:
					time_errors += 1
		last_position[i] = pos
	most_in_a_frame = maxi(most_in_a_frame, this_frame)
	if frame >= frames:
		loop_ms = (Time.get_ticks_usec() - t_loop) / 1000.0 / maxf(1, frames - 20)
		var scene_ms := -1.0
		if stats0.has("main_frames"):
			var s1: Dictionary = RenderingServer.call("fellow_get_stats")
			scene_ms = (s1["main_process_usec"] - stats0["main_process_usec"]) / 1000.0 / maxf(1, s1["main_frames"] - stats0["main_frames"])
		var share := []
		for b in 5:
			share.append(snappedf(evaluations_by_band[b] / maxf(1, characters_by_band[b]), 0.001))
		var counted := frames - 20
		print("ANIMATION_LOD_CHECK ", JSON.stringify({"crowd": players.size(), "frames": frames, "set": args.get("set", ""),
				"scene_ms": snappedf(scene_ms, 0.001), "frame_ms": snappedf(loop_ms, 0.001),
				"evaluations_per_frame": snappedf(evaluations / float(counted), 0.01), "most_in_a_frame": most_in_a_frame,
				"characters_per_band": characters_by_band.map(func(c): return c / counted),
				"evaluated_share_per_band": share, "time_errors": time_errors, "max_time_error": max_time_error}))
		quit(0)
	return false
