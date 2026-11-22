extends SceneTree
## FellowEngine's waves on the CPU (milestone 175, decision 0317) against the GPU's: FellowWater3D's displacement
## texture read back after a few frames, compared cascade by cascade with the CPU's grid at the texels it has, for a
## lake's waves and a sea's. Needs a RenderingDevice (lavapipe: VK_ICD_FILENAMES=.../lvp_icd.json xvfb-run -a ...).
##
##   godot --path benchmarks/tools/buoyancy -s gpu_match.gd --rendering-driver vulkan --resolution 320x180

var _water: Node3D
var _frame := 0
var _case := 0
const CASES := [[6.0, 3.0, 1.0], [12.0, 60.0, 1.0], [12.0, 60.0, 0.0]] # wind m/s, fetch km, choppiness


func _initialize() -> void:
	var cam := Camera3D.new()
	root.add_child(cam)
	cam.position = Vector3(0, 10, 20)
	_setup()


func _setup() -> void:
	if _water:
		_water.queue_free()
	_water = ClassDB.instantiate("FellowWater3D")
	_water.set("wind_speed", CASES[_case][0])
	_water.set("fetch", CASES[_case][1])
	_water.set("choppiness", CASES[_case][2])
	_water.set("time_auto", false)
	_water.set("time", 37.25)
	root.add_child(_water)
	_frame = 0


func _process(_d: float) -> bool:
	_frame += 1
	if _frame < 8:
		return false
	var hs: float = _water.call("get_significant_wave_height")
	_water.call("get_surface_height", Vector3()) # the CPU's waves built
	var st: Dictionary = _water.call("get_cpu_wave_stats")
	print("  the CPU's grid %d, %.0f µs an update" % [st.grid, st.usec_per_update])
	print("case wind %.0f m/s fetch %.0f km choppiness %.1f: Hs %.2f m, CPU share of the variance %.3f" % [CASES[_case][0],
			CASES[_case][1], CASES[_case][2], hs, float(_water.call("get_cpu_wave_stats").variance_share)])
	for c in 2: # the CPU's cascades: those of 10 m and more
		var d: Dictionary = _water.call("debug_compare_cpu_waves", c)
		print("  cascade %d: height rms %.4f m, CPU error rms %.4f (max %.4f); dx rms %.4f, error rms %.4f" % [c,
				d.get("height_rms", 0.0), d.get("height_error_rms", 0.0), d.get("height_error_max", 0.0), d.get("dx_rms", 0.0),
				d.get("dx_error_rms", 0.0)])
	_case += 1
	if _case >= CASES.size():
		return true
	_setup()
	return false
