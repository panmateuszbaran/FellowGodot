extends SceneTree
## FellowEngine gas solver check (decision 0117): runs one FellowGasVolume3D for a number of physics ticks and writes
## its GPU state to files, for modules/fellow_fluid/tests/gas_compare.py (the NumPy reference, gas_reference.py).
##
##   godot --path benchmarks/project --fixed-fps 60 -s res://bench/gas_check.gd -- --out=<dir> --steps=<n>
##         [--res=48] [--high_precision=1] [--maccormack=1] [--iterations=20] [--case=campfire|closed|wind|shift|sphere]
##         [--multigrid=1] [--cycles=2] [--sparse=0] [--margin=2]
##
## Writes <out>/scalars.bin, velocity.bin, pressure.bin, residual.bin, solid.bin (raw texels, x fastest) and
## <out>/info.json (the grid, the parameters).

var args := {}
var vol
var steps := 60
var stage := 0
var what := 0
var out_dir := ""
var frames := 0
var moved := false
const NAMES := ["scalars", "velocity", "pressure", "residual", "solid"]


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = args.get("out", "user://gas_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	steps = int(args.get("steps", "60"))
	if not ClassDB.class_exists("FellowGasVolume3D"):
		push_error("FellowGasVolume3D not available")
		quit(1)
		return
	var cam := Camera3D.new()
	root.add_child(cam)
	vol = ClassDB.instantiate("FellowGasVolume3D")
	var res := int(args.get("res", "48"))
	# 32 x 48 x 32 cells of 0.1 m at res 48.
	vol.size = Vector3(res * 2.0 / 3.0, res, res * 2.0 / 3.0) * 0.1
	vol.resolution = res
	vol.high_precision = args.get("high_precision", "1") == "1"
	vol.maccormack = args.get("maccormack", "1") == "1"
	vol.pressure_iterations = int(args.get("iterations", "20"))
	vol.pressure_solver = 1 if args.get("multigrid", "1") == "1" else 0
	vol.multigrid_cycles = int(args.get("cycles", "2"))
	vol.sparse = args.get("sparse", "0") == "1"
	vol.sparse_margin = int(args.get("margin", "2"))
	vol.physics_obstacles = false
	vol.fire_light = false
	vol.render_enabled = false
	vol.vorticity = float(args.get("vorticity", "0.3"))
	var case_name: String = args.get("case", "campfire")
	var e = ClassDB.instantiate("FellowGasEmitter3D")
	e.shape = 0
	e.radius = res * 2.0 / 3.0 * 0.12 * 0.1
	e.noise = 0.0
	e.fuel = 3.0
	e.temperature = 900.0
	e.smoke = 0.2
	e.position = Vector3(0, 0.25, 0)
	if case_name == "closed":
		vol.closed_faces = 63
		e.fuel = 0.0
		e.temperature = 400.0
		e.smoke = 1.0
	elif case_name == "wind":
		vol.wind = Vector3(1.0, 0.0, 0.0)
	elif case_name == "shift":
		vol.space = 1  # world: the box follows the node by whole cells (moved after 20 steps)
	elif case_name == "sphere":
		# A static sphere obstacle (r 0.5 m) above the emitter, found through the physics server.
		vol.physics_obstacles = true
		var body := StaticBody3D.new()
		var cs := CollisionShape3D.new()
		var sh := SphereShape3D.new()
		sh.radius = 0.5
		cs.shape = sh
		body.add_child(cs)
		body.position = Vector3(0.0, 2.4, 0.0)
		root.add_child(body)
	vol.add_child(e)
	root.add_child(vol)


func _physics_process(_delta: float) -> bool:
	frames += 1
	if stage == 0:
		if args.get("case", "") == "shift" and vol.get_steps_simulated() == 20 and not moved:
			vol.position += Vector3(0.2, 0.0, -0.1)
			moved = true
		if vol.get_steps_simulated() >= steps:
			vol.simulation_enabled = false
			stage = 1
			what = 0
			vol.request_debug_data(what)
		return false
	if stage == 1:
		if not vol.is_debug_data_ready():
			return false
		var f := FileAccess.open(out_dir.path_join(NAMES[what] + ".bin"), FileAccess.WRITE)
		f.store_buffer(vol.get_debug_data())
		f.close()
		what += 1
		if what < NAMES.size():
			vol.request_debug_data(what)
			return false
		var info := {"grid": [vol.get_grid_size().x, vol.get_grid_size().y, vol.get_grid_size().z], "dx": vol.get_cell_size(),
			"steps": vol.get_steps_simulated(), "high_precision": vol.high_precision, "maccormack": vol.maccormack,
			"iterations": vol.pressure_iterations, "multigrid": vol.pressure_solver == 1, "cycles": vol.multigrid_cycles, "sparse": vol.sparse, "margin": vol.sparse_margin,
			"case": args.get("case", "campfire"), "frames": frames}
		var j := FileAccess.open(out_dir.path_join("info.json"), FileAccess.WRITE)
		j.store_string(JSON.stringify(info))
		j.close()
		print("GAS_CHECK_DONE ", JSON.stringify(info))
		return true
	return false
