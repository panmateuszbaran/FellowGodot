# FellowEngine — Motion Lab's crowd without the bench (run from a copy of benchmarks/project): N characters wander for
# a while; per step and character the hips' and head's heights, the state (0 idle, 1 moving), the target speed, the
# desired speed, the generations and the get-ups so far go to <out>/crowd.bin ([steps][characters][7]) for the check
# that no one stays on the ground (tests/fmb_crowd_check.py). --get_up=0 and --catch_up=0 turn those off.
#
#   godot --path <bench project copy> --rendering-driver vulkan --script res://fmb_crowd_run.gd -- \
#       --fmb=<model.fmb> [--body=<mannequin.glb>] --out=<dir> [--crowd=8] [--seconds=20] [--get_up=1] [--catch_up=1]
extends SceneTree

var lab
var t := 0.0
var seconds := 20.0
var out_dir := ""
var rec := PackedFloat32Array()
var steps := 0
const FIELDS := ["hips_y", "head_y", "state", "speed", "desired_speed", "generations", "get_ups"]


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
		return
	for c in lab._chars:
		c.get_up = _arg("get_up", "1") != "0"
		c.catch_up = _arg("catch_up", "1") != "0"


func _process(_delta: float) -> bool:
	if t >= seconds:
		var f := FileAccess.open(out_dir + "/crowd.bin", FileAccess.WRITE)
		f.store_buffer(rec.to_byte_array())
		var j := FileAccess.open(out_dir + "/info.json", FileAccess.WRITE)
		j.store_string(JSON.stringify({"steps": steps, "crowd": lab._chars.size(), "fields": FIELDS, "service": lab.sample_extra()}))
		print("done ", steps, " ", lab.sample_extra())
		return true
	t += 1.0 / 60.0
	lab.set_time(t)
	steps += 1
	for c in lab._chars:
		var jp: PackedVector3Array = c.get_joint_positions()
		rec.append(jp[0].y if jp.size() > 0 else 0.0)
		rec.append(jp[6].y if jp.size() > 6 else 0.0)
		var st: Dictionary = c.get_stats()
		rec.append(float(st.get("state", 0)))
		rec.append(float(st.get("speed", 0.0)))
		rec.append(c.desired_velocity.length())
		rec.append(float(st.get("generations", 0)))
		rec.append(float(st.get("get_ups", 0)))
	return false
