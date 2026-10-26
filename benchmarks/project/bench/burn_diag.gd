extends SceneTree
## Burn Lab's burning over time (decision 0120): builds scenes/burn_lab.gd and prints its objects' fractions every
## second of simulated time (one line per second: wood, char, ash, burning voxels, max temperature per object; the
## fire's power).
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 160x90 -s res://bench/burn_diag.gd -- --seconds=40
##         [the Burn Lab options: --gas_res=48 --burn_speed=10 --kindling_s=12 ...]

var args := {}
var scene
var seconds := 40
var ticks := 0
var dump_at := -1
var dump_list: Array = []
var dump_i := 0
var dump_wait := false
var out_dir := ""


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	seconds = int(args.get("seconds", "40"))
	dump_at = int(args.get("dump_at", "-1"))
	out_dir = args.get("out", "/tmp/burn_diag")
	scene = load("res://scenes/burn_lab.gd").new()
	root.add_child(scene)
	scene.build(args)
	if scene.load_error != "":
		push_error(scene.load_error)
		quit(1)
		return
	scene.set_camera_pose(Vector3(1.3, 0.85, 1.75), Vector3(0.2, 0.22, -0.05), 50.0)


# --dump_at=S: at S seconds the gas's scalars and each object's state, emission and injection written to --out
# (the simulation paused while they are read).
func _dump() -> bool:
	while dump_i < dump_list.size():
		var e: Array = dump_list[dump_i]
		if not dump_wait:
			e[0].request_debug_data(e[1])
			dump_wait = true
			return false
		if not e[0].is_debug_data_ready():
			return false
		var f := FileAccess.open(out_dir.path_join(e[2]), FileAccess.WRITE)
		f.store_buffer(e[0].get_debug_data())
		f.close()
		dump_wait = false
		dump_i += 1
	var info := {"gas_grid": [scene.gas.get_grid_size().x, scene.gas.get_grid_size().y, scene.gas.get_grid_size().z], "gas_high_precision": scene.gas.high_precision}
	for k in ["log_a", "log_b", "branch", "crate"]:
		var b = scene.get(k)
		var d: Dictionary = b.get_last_step_info()
		if d.is_empty():
			continue
		var t: Transform3D = d["voxel_to_gas"]
		info[k] = {"grid": [b.get_voxel_grid_size().x, b.get_voxel_grid_size().y, b.get_voxel_grid_size().z], "voxel_size": d["voxel_size"],
			"voxel_to_gas": [t.basis.x.x, t.basis.y.x, t.basis.z.x, t.origin.x, t.basis.x.y, t.basis.y.y, t.basis.z.y, t.origin.y, t.basis.x.z, t.basis.y.z, t.basis.z.z, t.origin.z],
			"region_origin": [d["region_origin"].x, d["region_origin"].y, d["region_origin"].z], "region_size": [d["region_size"].x, d["region_size"].y, d["region_size"].z]}
	var j := FileAccess.open(out_dir.path_join("info.json"), FileAccess.WRITE)
	j.store_string(JSON.stringify(info))
	j.close()
	scene.gas.simulation_enabled = true
	for k in ["log_a", "log_b", "branch", "crate"]:
		scene.get(k).simulation_enabled = true
	dump_list = []
	return true


func _physics_process(_delta: float) -> bool:
	if not dump_list.is_empty():
		_dump()
		return false
	if dump_at >= 0 and ticks == dump_at * 60:
		DirAccess.make_dir_recursive_absolute(out_dir)
		scene.gas.simulation_enabled = false
		dump_list = [[scene.gas, 0, "gas.bin"], [scene.gas, 1, "velocity.bin"], [scene.gas, 4, "solid.bin"]]
		for k in ["log_a", "log_b", "branch", "crate"]:
			var b = scene.get(k)
			b.simulation_enabled = false
			if b.get_steps_simulated() > 0:
				dump_list += [[b, 0, k + "_state.bin"], [b, 1, k + "_emission.bin"], [b, 2, k + "_inject.bin"]]
		dump_i = 0
		dump_at = -1
		return false
	ticks += 1
	if ticks % 60 == 0:
		var d: Dictionary = scene.sample_extra()
		var line := "t %3d s  fire %7.2f" % [ticks / 60, d.get("fire_power", 0.0)]
		for k in ["log_a", "log_b", "branch", "crate"]:
			var v: Array = d[k]
			line += "  | %s w %.3f c %.3f a %.3f b %5d T %5.0f" % [k, v[0], v[1], v[2], v[3], v[4]]
		print(line)
	if ticks >= seconds * 60:
		print("BURN_DIAG_DONE")
		return true
	return false
