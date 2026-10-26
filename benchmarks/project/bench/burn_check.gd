extends SceneTree
## FellowEngine burning check (decision 0120): one FellowBurnable3D, its GPU state written to files for
## modules/fellow_fluid/tests/burn_compare.py (the NumPy reference, burn_reference.py).
##
##   godot --path benchmarks/project --fixed-fps 60 -s res://bench/burn_check.gd -- --out=<dir> --case=<case> --steps=<n>
##
## Cases:
##   solo  a log (a cylinder lying along x) alone, ignited in its middle (no gas: the surface's air at ambient);
##   ash   a box on its side (the ash falls along its local x), ignited all through, burning fast;
##   gas   the log above a campfire in a gas volume, `--warm` coupled steps, then one step compared on its own
##         (the gas's temperature read back between).
## Writes <out>/initial.bin (the voxels before the steps), state.bin (after), emission.bin, for `gas` before.bin,
## gas.bin (the gas's scalars the last step read) and inject.bin; info.json (the grid, the step's parameters).

var args := {}
var burn
var target: MeshInstance3D
var vol
var steps := 60
var warm := 60
var stage := 0
var out_dir := ""
var case_name := "solo"
var info := {}
var reads: Array = []  # [what, file, node]
var read_i := 0
var wait := 0


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = args.get("out", "user://burn_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	steps = int(args.get("steps", "60"))
	warm = int(args.get("warm", "60"))
	case_name = args.get("case", "solo")
	if not ClassDB.class_exists("FellowBurnable3D"):
		push_error("FellowBurnable3D not available")
		quit(1)
		return
	var cam := Camera3D.new()
	cam.position = Vector3(0, 1.0, 3.0)
	root.add_child(cam)
	target = MeshInstance3D.new()
	var parent: Node = root
	if case_name == "ash":
		var m := BoxMesh.new()
		m.size = Vector3(0.3, 0.2, 0.25)
		target.mesh = m
		target.rotation = Vector3(0, 0, PI * 0.5)  # its local x points down
		target.position = Vector3(0, 0.5, 0)
	else:
		var m := CylinderMesh.new()
		m.top_radius = 0.12
		m.bottom_radius = 0.12
		m.height = 0.8
		m.radial_segments = 24
		m.rings = 1
		target.mesh = m
		target.rotation = Vector3(0, 0, PI * 0.5)  # lying along x
		target.position = Vector3(0, 0.45, 0)
	if case_name == "gas":
		vol = ClassDB.instantiate("FellowGasVolume3D")
		vol.size = Vector3(1.6, 2.4, 1.6)
		vol.resolution = 48
		vol.high_precision = true
		vol.physics_obstacles = false
		vol.fire_light = false
		vol.render_enabled = false
		var e = ClassDB.instantiate("FellowGasEmitter3D")
		e.shape = 0
		e.radius = 0.18
		e.noise = 0.0
		e.fuel = 3.0
		e.temperature = 900.0
		e.smoke = 0.2
		e.position = Vector3(0, 0.15, 0)
		vol.add_child(e)
		root.add_child(vol)
		parent = vol
	if case_name == "ash":
		# In a static body: its box shape is disabled once the box has burned away (collapse_fraction).
		var body := StaticBody3D.new()
		var cs := CollisionShape3D.new()
		cs.name = "Shape"
		var sh := BoxShape3D.new()
		sh.size = Vector3(0.3, 0.2, 0.25)
		cs.shape = sh
		body.add_child(cs)
		parent.add_child(body)
		parent = body
	parent.add_child(target)
	burn = ClassDB.instantiate("FellowBurnable3D")
	burn.resolution = int(args.get("res", "24"))
	burn.simulation_enabled = false
	if case_name == "ash":
		burn.burn_speed = 60.0
		burn.pyrolysis_rate = 0.5
		burn.glow_rate = 0.3
	target.add_child(burn)


func _save(what_file: String, data: PackedByteArray) -> void:
	var f := FileAccess.open(out_dir.path_join(what_file), FileAccess.WRITE)
	f.store_buffer(data)
	f.close()


# Reads the queued (what, file, node) one after another; true when all are written.
func _reads_done() -> bool:
	while read_i < reads.size():
		var r: Array = reads[read_i]
		var node = r[2]
		if wait == 0:
			node.request_debug_data(r[0])
			wait = 1
			return false
		if not node.is_debug_data_ready():
			return false
		_save(r[1], node.get_debug_data())
		wait = 0
		read_i += 1
	return true


func _queue_reads(list: Array) -> void:
	reads = list
	read_i = 0
	wait = 0


func _physics_process(_delta: float) -> bool:
	match stage:
		0:  # wait for the voxels
			if burn.get_voxel_grid_size() == Vector3i.ZERO:
				return false
			_queue_reads([[0, "initial.bin", burn]])
			stage = 1
		1:
			if not _reads_done():
				return false
			if case_name == "gas":
				burn.simulation_enabled = true  # warm up coupled
				stage = 10
				return false
			var c: Vector3 = target.global_position
			if case_name == "ash":
				burn.ignite(c, 0.4, 1400.0)
			else:
				burn.ignite(c, 0.1, 1100.0)
			burn.simulation_enabled = true
			stage = 2
		2:  # the first step's parameters (the ignition)
			info["first"] = _step_info()
			stage = 3
		3:
			if burn.get_steps_simulated() >= steps:
				burn.simulation_enabled = false
				info["last"] = _step_info()
				stage = 4
				wait = -2  # two frames: the steps run
		4:
			if wait < 0:
				wait += 1
				return false
			wait = 0
			_queue_reads([[0, "state.bin", burn], [1, "emission.bin", burn]])
			stage = 5
		5:
			if not _reads_done():
				return false
			_finish()
			return true
		10:
			if burn.get_steps_simulated() >= warm:
				burn.simulation_enabled = false
				vol.simulation_enabled = false
				stage = 11
				wait = -2
		11:
			if wait < 0:
				wait += 1
				return false
			wait = 0
			_queue_reads([[0, "before.bin", burn], [1, "before_emission.bin", burn]])
			stage = 12
		12:
			if not _reads_done():
				return false
			burn.simulation_enabled = true  # one coupled step
			vol.simulation_enabled = true
			stage = 13
		13:
			burn.simulation_enabled = false
			vol.simulation_enabled = false
			info["last"] = _step_info()
			info["first"] = info["last"]
			stage = 14
			wait = -2
		14:
			if wait < 0:
				wait += 1
				return false
			wait = 0
			_queue_reads([[0, "state.bin", burn], [1, "emission.bin", burn], [2, "inject.bin", burn], [0, "gas.bin", vol]])
			stage = 15
		15:
			if not _reads_done():
				return false
			_finish()
			return true
	return false


func _step_info() -> Dictionary:
	var d: Dictionary = burn.get_last_step_info()
	var o := {}
	for k in d:
		var v = d[k]
		if v is Transform3D:
			var t: Transform3D = v
			o[k] = [t.basis.x.x, t.basis.y.x, t.basis.z.x, t.origin.x, t.basis.x.y, t.basis.y.y, t.basis.z.y, t.origin.y, t.basis.x.z, t.basis.y.z, t.basis.z.z, t.origin.z]
		elif v is Vector3i or v is Vector3 or v is Vector4:
			o[k] = [v.x, v.y, v.z] if not (v is Vector4) else [v.x, v.y, v.z, v.w]
		elif v is PackedFloat32Array:
			o[k] = Array(v)
		else:
			o[k] = v
	return o


func _finish() -> void:
	var g: Vector3i = burn.get_voxel_grid_size()
	info["grid"] = [g.x, g.y, g.z]
	info["case"] = case_name
	info["steps"] = burn.get_steps_simulated()
	info["voxel_local"] = burn.get_voxel_size()
	info["triangles"] = burn.get_display_triangle_count()
	info["wood"] = burn.get_wood_fraction()
	info["char"] = burn.get_char_fraction()
	info["ash"] = burn.get_ash_fraction()
	info["t_max"] = burn.get_max_temperature()
	info["collapsed"] = burn.is_collapsed()
	if case_name == "ash":
		info["shape_disabled"] = (target.get_parent().get_node("Shape") as CollisionShape3D).disabled
	if vol:
		var gs: Vector3i = vol.get_grid_size()
		info["gas_grid"] = [gs.x, gs.y, gs.z]
		info["gas_high_precision"] = vol.high_precision
	var j := FileAccess.open(out_dir.path_join("info.json"), FileAccess.WRITE)
	j.store_string(JSON.stringify(info))
	j.close()
	print("BURN_CHECK_DONE ", JSON.stringify(info).substr(0, 400))
