# FellowEngine — the CPU's share of Motion Lab's characters (run from a copy of benchmarks/project): N characters in
# flush mode stepped at 60 Hz; per step the time of the steering, of the characters' advance() calls (sampling,
# crossfading, accepting results, requesting) and of the flush (the model's batch, GPU included) go to
# <out>/cpu.json with the service's statistics.
#
#   godot --path <bench project copy> --rendering-driver vulkan --script fmb_cpu_profile.gd -- \
#       --fmb=<model.fmb> [--body=<mannequin.glb>] --out=<dir> [--crowd=16] [--seconds=10]
extends SceneTree

var lab
var t := 0.0
var seconds := 10.0
var out_dir := ""
var steps := 0
var t_steer := PackedFloat64Array()
var t_advance := PackedFloat64Array()
var t_flush := PackedFloat64Array()
var gens := PackedInt32Array()


func _arg(name: String, def: String) -> String:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--" + name + "="):
			return a.substr(name.length() + 3)
	return def


func _initialize() -> void:
	out_dir = _arg("out", "/tmp/fmb_cpu")
	seconds = float(_arg("seconds", "10"))
	DirAccess.make_dir_recursive_absolute(out_dir)
	lab = load("res://scenes/motion_lab.gd").new()
	get_root().add_child(lab)
	lab.build({"motion_model": _arg("fmb", ""), "motion_body": _arg("body", ""), "motion_crowd": _arg("crowd", "16"), "motion_mode": "flush"})
	if lab.load_error != "":
		push_error(lab.load_error)
		quit(1)


func _gen_total() -> int:
	var g := 0
	for c in lab._chars:
		g += int(c.get_stats().get("generations", 0))
	return g


func _process(_delta: float) -> bool:
	if t >= seconds:
		var adv := Array(t_advance)
		adv.sort()
		var res := {"steps": steps, "crowd": lab._chars.size(), "service": lab.sample_extra(),
			"steer_ms": t_steer, "advance_ms": t_advance, "flush_ms": t_flush, "generations": gens,
			"advance_median_ms": adv[adv.size() / 2], "advance_p90_ms": adv[int(adv.size() * 0.9)]}
		var f := FileAccess.open(out_dir + "/cpu.json", FileAccess.WRITE)
		f.store_string(JSON.stringify(res))
		print("done ", steps, " advance median ", res["advance_median_ms"], " ms, p90 ", res["advance_p90_ms"], " ms ", lab.sample_extra())
		return true
	var dt := 1.0 / 60.0
	t += dt
	var a := Time.get_ticks_usec()
	for i in lab._chars.size():
		lab._steer(i, dt)
	var b := Time.get_ticks_usec()
	for ch in lab._chars:
		ch.advance(dt)
	var c := Time.get_ticks_usec()
	ClassDB.class_call_static("FellowMotionCharacter3D", "flush_requests", lab.model_path)
	var d := Time.get_ticks_usec()
	t_steer.append((b - a) / 1000.0)
	t_advance.append((c - b) / 1000.0)
	t_flush.append((d - c) / 1000.0)
	gens.append(_gen_total())
	steps += 1
	return false
