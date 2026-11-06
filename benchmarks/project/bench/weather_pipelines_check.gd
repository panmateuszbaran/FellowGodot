extends SceneTree
## FellowEngine weather pipelines check (milestone 57): the pipelines compiled in the frames after a FellowRain3D's
## precipitation changes (rain -> snow -> rain), by kind. A node that compiles pipelines mid-game makes a hitch; the
## rain node's are compiled when it is set up, and none should compile when it starts or stops snowing.
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 320x180 -s res://bench/weather_pipelines_check.gd
##       [-- --start=rain|snow --frames=40]
##
## Prints one JSON line: the pipelines by kind during the set-up frames and after each change.

var args := {}
var rain
var frame := 0
var frames := 40
var phase := 0
var phases := []
var before := {}
var result := {}


func _counts() -> Dictionary:
	return {
		"canvas": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_CANVAS),
		"mesh": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_MESH),
		"surface": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_SURFACE),
		"draw": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_DRAW),
		"specialization": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_SPECIALIZATION),
	}


func _diff(a: Dictionary, b: Dictionary) -> Dictionary:
	var d := {}
	for k in b:
		if b[k] - a.get(k, 0) != 0:
			d[k] = b[k] - a.get(k, 0)
	return d


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	frames = int(args.get("frames", "40"))
	var snow_first: bool = args.get("start", "rain") == "snow"
	phases = ["setup", "snow" if not snow_first else "rain", "rain" if not snow_first else "snow"]

	var root3d := Node3D.new()
	root.add_child(root3d)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.4, 0.45, 0.5)
	root3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	sun.shadow_enabled = true
	root3d.add_child(sun)
	var floor_mesh := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(40, 40)
	floor_mesh.mesh = plane
	root3d.add_child(floor_mesh)
	var cam := Camera3D.new()
	cam.position = Vector3(0, 1.7, 6)
	root3d.add_child(cam)
	cam.make_current()
	rain = ClassDB.instantiate("FellowRain3D")
	rain.rain = 0.8
	rain.precipitation = 1 if snow_first else 0
	root3d.add_child(rain)
	before = _counts()


func _process(_delta: float) -> bool:
	frame += 1
	if frame < frames:
		return false
	var now := _counts()
	result[phases[phase]] = _diff(before, now)
	phase += 1
	if phase >= phases.size():
		print("WEATHER_PIPELINES ", JSON.stringify(result))
		return true
	rain.precipitation = 1 if phases[phase] == "snow" else 0
	before = _counts()
	frame = 0
	return false
