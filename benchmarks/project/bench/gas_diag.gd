extends SceneTree
## FellowEngine gas diagnostics: Fire Lab's chimney volume alone (its house and chimney as static bodies), run for a
## number of steps, its scalars and velocity dumped (raw texels) with a summary (NaN / infinity counts, totals).
##
##   godot --path benchmarks/project --fixed-fps 60 -s res://bench/gas_diag.gd -- --out=<dir> --steps=240
##         [--variant=base|nowind|closed|sor|hp|noemitvel|noobst] [--res=80]

var args := {}
var vol
var steps := 240
var stage := 0
var what := 0
var out_dir := ""
const NAMES := ["scalars", "velocity", "pressure", "residual", "solid"]


func _box(p_size: Vector3, p_pos: Vector3) -> void:
	var body := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var sh := BoxShape3D.new()
	sh.size = p_size
	cs.shape = sh
	body.add_child(cs)
	body.position = p_pos
	root.add_child(body)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = args.get("out", "user://gas_diag")
	DirAccess.make_dir_recursive_absolute(out_dir)
	steps = int(args.get("steps", "240"))
	var variant: String = args.get("variant", "base")
	var cam := Camera3D.new()
	root.add_child(cam)
	if variant != "noobst":
		_box(Vector3(6.0, 5.0, 6.0), Vector3(10.0, 2.5, -7.0))
		_box(Vector3(6.6, 0.3, 6.6), Vector3(10.0, 5.15, -7.0))
		_box(Vector3(0.8, 2.2, 0.8), Vector3(11.5, 6.4, -8.0))
	vol = ClassDB.instantiate("FellowGasVolume3D")
	vol.position = Vector3(11.5, 7.5, -8.0)
	vol.size = Vector3(6.0, 9.0, 6.0)
	vol.resolution = int(args.get("res", "80"))
	vol.closed_faces = 63 if variant == "closed" else 0
	vol.wind = Vector3(0, 0, 0) if variant == "nowind" else Vector3(1.4, 0.0, 0.5)
	vol.wind_rate = 0.3
	vol.vorticity = 0.25
	vol.smoke_dissipation = 0.06
	vol.cooling = 0.6
	vol.fire_light = false
	vol.render_enabled = false
	vol.high_precision = variant == "hp"
	if variant == "sor":
		vol.pressure_solver = 0
	var e = ClassDB.instantiate("FellowGasEmitter3D")
	e.shape = 0
	e.radius = 0.32
	e.position = Vector3(0, 0.35, 0)
	e.fuel = 0.0
	e.temperature = 160.0
	e.smoke = 1.6
	e.noise = 0.4
	e.noise_scale = 0.4
	e.velocity = Vector3(0, 1.2, 0)
	e.velocity_strength = 0.0 if variant == "noemitvel" else 0.3
	vol.add_child(e)
	root.add_child(vol)


func _physics_process(_delta: float) -> bool:
	if stage == 0:
		if vol.get_steps_simulated() >= steps:
			vol.simulation_enabled = false
			stage = 1
			what = 0
			vol.request_debug_data(what)
		return false
	if not vol.is_debug_data_ready():
		return false
	var data: PackedByteArray = vol.get_debug_data()
	var f := FileAccess.open(out_dir.path_join(NAMES[what] + ".bin"), FileAccess.WRITE)
	f.store_buffer(data)
	f.close()
	what += 1
	if what < NAMES.size():
		vol.request_debug_data(what)
		return false
	var info := {"grid": [vol.get_grid_size().x, vol.get_grid_size().y, vol.get_grid_size().z], "dx": vol.get_cell_size(),
		"steps": vol.get_steps_simulated(), "high_precision": vol.high_precision, "variant": args.get("variant", "base")}
	var j := FileAccess.open(out_dir.path_join("info.json"), FileAccess.WRITE)
	j.store_string(JSON.stringify(info))
	j.close()
	print("GAS_DIAG_DONE ", JSON.stringify(info))
	return true
