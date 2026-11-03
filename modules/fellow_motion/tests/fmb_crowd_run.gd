# FellowEngine — Motion Lab's crowd without the bench (run from a copy of benchmarks/project): N characters wander for
# a while; their hips and head heights per step go to <out>/crowd.bin ([steps][characters][2]) for the check that no
# one sinks to the ground (tests/fmb_crowd_check.py).
#
#   godot --path <bench project copy> --rendering-driver vulkan --script res://fmb_crowd_run.gd -- \
#       --fmb=<model.fmb> [--body=<mannequin.glb>] --out=<dir> [--crowd=8] [--seconds=20]
extends SceneTree

var lab
var t := 0.0
var seconds := 20.0
var out_dir := ""
var rec := PackedFloat32Array()
var steps := 0


func _arg(name: String, def: String) -> String:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--" + name + "="):
			return a.substr(name.length() + 3)
	return def


func _initialize() -> void:
	out_dir = _arg("out", "/tmp/fmb_crowd")
	seconds = float(_arg("seconds", "20"))
	DirAccess.make_dir_recursive_absolute(out_dir)
	lab = load("res://scenes/motion_lab.gd").new()
	get_root().add_child(lab)
	lab.build({"motion_model": _arg("fmb", ""), "motion_body": _arg("body", ""), "motion_crowd": _arg("crowd", "8"), "motion_mode": "flush"})
	if lab.load_error != "":
		push_error(lab.load_error)
		quit(1)


func _process(_delta: float) -> bool:
	if t >= seconds:
		var f := FileAccess.open(out_dir + "/crowd.bin", FileAccess.WRITE)
		f.store_buffer(rec.to_byte_array())
		var j := FileAccess.open(out_dir + "/info.json", FileAccess.WRITE)
		j.store_string(JSON.stringify({"steps": steps, "crowd": lab._chars.size(), "service": lab.sample_extra()}))
		print("done ", steps, " ", lab.sample_extra())
		return true
	t += 1.0 / 60.0
	lab.set_time(t)
	steps += 1
	for c in lab._chars:
		var jp: PackedVector3Array = c.get_joint_positions()
		rec.append(jp[0].y if jp.size() > 0 else 0.0)
		rec.append(jp[6].y if jp.size() > 6 else 0.0)
	return false
