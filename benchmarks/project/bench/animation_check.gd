extends SceneTree
## FellowEngine skeletal animation check (decisions 0076, 0163): Crowd Lab's characters (52 bones, a rotation track per
## bone, each its own AnimationPlayer and Skeleton3D) stepped by a time step that is not a multiple of the clips' key
## spacing; every 25 frames a hash of every character's bones — pose rotation, global pose, global pose without
## overrides — and the main thread's time. Runs with FellowEngine's animation settings off and on must give the same
## hashes; scene_ms (the scene's process step a frame, from FellowEngine's stats) compares their cost (headless: the
## dummy renderer, the main thread's work only; the cloud's timings vary between runs: compare CPU time with
## `perf stat -e task-clock` or instructions with callgrind there, decision 0163).
##
##   godot --headless --path benchmarks/project -s res://bench/animation_check.gd -- [--crowd=128] [--frames=400]
##       [--set=<setting>=<value>,...] [--blend=0] [--hash=1]
##
## --set takes rendering/fellow/animation/* settings (without the prefix), e.g. --set=lazy_simulator_poses=false.
## One JSON line: ANIMATION_CHECK {"hashes": [...], "scene_ms": ..., "process_ms": ..., "set_time_ms": ...}
## (process_ms: Performance.TIME_PROCESS, each second's largest frame step, averaged).

var args := {}
var crowd
var frame := 0
var frames := 400
var step := 0.0131 # s: not a multiple of the 20 key/s spacing
var t := 0.0
var hashes := []
var process_ms := []
var set_time_ms := []
var stats0 := {}


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
			ProjectSettings.set_setting("rendering/fellow/animation/" + p[0], v)
	frames = int(args.get("frames", "400"))
	crowd = load("res://scenes/crowd_lab.gd").new()
	crowd.name = "Crowd"
	root.add_child(crowd)
	crowd.args = {}
	crowd.crowd = int(args.get("crowd", "128"))
	crowd.blend = args.get("blend", "0") == "1"
	crowd._area = 30.0
	crowd._define_bones()
	crowd._build_mesh()
	crowd._build_clips()
	crowd._build_crowd()


func _hash() -> String:
	var ctx := HashingContext.new()
	ctx.start(HashingContext.HASH_SHA256)
	for ch in crowd._chars:
		var sk: Skeleton3D = ch.get_node("Skeleton3D")
		var buf := PackedFloat32Array()
		for b in sk.get_bone_count():
			var q := sk.get_bone_pose_rotation(b)
			var g := sk.get_bone_global_pose(b)
			var n := sk.get_bone_global_pose_no_override(b)
			buf.append_array([q.x, q.y, q.z, q.w])
			for xf in [g, n]:
				buf.append_array([xf.basis.x.x, xf.basis.x.y, xf.basis.x.z, xf.basis.y.x, xf.basis.y.y, xf.basis.y.z,
						xf.basis.z.x, xf.basis.z.y, xf.basis.z.z, xf.origin.x, xf.origin.y, xf.origin.z])
		ctx.update(buf.to_byte_array())
	return ctx.finish().hex_encode()


func _process(_delta: float) -> bool:
	frame += 1
	if frame == 20:
		stats0 = RenderingServer.call("fellow_get_stats") if RenderingServer.has_method("fellow_get_stats") else {}
	if frame > 20:
		process_ms.append(Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0)
	if args.get("hash", "1") == "1" and frame % 25 == 0:
		hashes.append([frame, _hash().substr(0, 16)])
	if frame >= frames:
		process_ms.sort()
		set_time_ms.sort()
		var mean := 0.0
		for v in process_ms:
			mean += v
		mean /= maxf(1, process_ms.size())
		var st := 0.0
		for v in set_time_ms:
			st += v
		st /= maxf(1, set_time_ms.size())
		var scene_ms := -1.0
		if stats0.has("main_frames"):
			var s1: Dictionary = RenderingServer.call("fellow_get_stats")
			scene_ms = (s1["main_process_usec"] - stats0["main_process_usec"]) / 1000.0 / maxf(1, s1["main_frames"] - stats0["main_frames"])
		print("ANIMATION_CHECK ", JSON.stringify({"hashes": hashes, "scene_ms": scene_ms, "process_ms": mean,
				"process_ms_median": process_ms[process_ms.size() / 2], "set_time_ms": st,
				"crowd": crowd.crowd, "frames": frames, "set": args.get("set", "")}))
		quit(0)
		return false
	t += step
	var t0 := Time.get_ticks_usec()
	crowd.set_time(t)
	if frame > 20:
		set_time_ms.append((Time.get_ticks_usec() - t0) / 1000.0)
	return false
