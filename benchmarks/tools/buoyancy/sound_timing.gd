extends SceneTree
## FellowEngine's water sound (milestone 177, decision 0319): when a splash is heard against when it happens. Three
## boxes dropped into still water one second apart (the first starts the voices, the others find them playing), with
## Movie Maker writing the sound; each strike's time printed ("strike <n> frame <f> tick <t>") for water_sound.py
## --timing to find its onset in the sound.
##
##   VK_ICD_FILENAMES=.../lvp_icd.json xvfb-run -a godot --path benchmarks/tools/buoyancy --rendering-driver vulkan \
##       --resolution 64x64 --fixed-fps 30 --write-movie <dir>/t.png -s sound_timing.gd
##   python benchmarks/tools/buoyancy/water_sound.py --timing <dir>/t.wav <log>

var _frame := 0
var _boxes := []


func _initialize() -> void:
	var water: Node3D = ClassDB.instantiate("FellowWater3D")
	water.set("wave_scale", 0.0)
	root.add_child(water)
	var cam := Camera3D.new()
	root.add_child(cam)
	cam.look_at_from_position(Vector3(0, 3, -6), Vector3(0, 0, 0))
	for k in 3:
		var body := RigidBody3D.new()
		var cs := CollisionShape3D.new()
		var s := BoxShape3D.new()
		s.size = Vector3(0.4, 0.4, 0.4)
		cs.shape = s
		body.add_child(cs)
		body.mass = 400.0 * 0.064
		var buoy: Node3D = ClassDB.instantiate("FellowBuoyancy3D")
		buoy.name = "Buoyancy"
		body.add_child(buoy)
		buoy.connect("water_entered", _on_entered.bind(k))
		body.position = Vector3(-1.5 + 1.5 * k, 1.5, 0)
		body.freeze = true
		root.add_child(body)
		_boxes.append(body)


func _on_entered(p_speed: float, p_k: int) -> void:
	print("strike %d frame %d tick %d speed %.2f" % [p_k, _frame, Engine.get_physics_frames(), p_speed])


func _physics_process(_d: float) -> bool:
	var t := float(_frame) / 30.0
	for k in 3:
		if _boxes[k].freeze and t >= 0.5 + float(k):
			_boxes[k].freeze = false
	return false


func _process(_d: float) -> bool:
	_frame += 1
	return _frame > 30 * 4
