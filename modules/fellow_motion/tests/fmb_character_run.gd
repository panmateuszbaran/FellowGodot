# FellowEngine — drive one FellowMotionCharacter3D through a velocity schedule (flush mode: deterministic, one frame
# of latency), record its joints every step and screenshots, for tests/fmb_character_check.py.
#
#   godot --path <project> --rendering-driver vulkan --script fmb_character_run.gd -- --fmb=<model.fmb> \
#       [--body=<mannequin.glb>] --out=<dir> [--seconds=12] [--shots=4]
extends SceneTree

var ch
var cam: Camera3D
var t := 0.0
var step := 0
var seconds := 12.0
var out_dir := ""
var fmb := ""
var shots := 4
var rec := PackedFloat32Array()
var info := []
var started := false
var shot_pending := -1


func _arg(name: String, def: String) -> String:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--" + name + "="):
			return a.substr(name.length() + 3)
	return def


# The schedule: stand, walk ahead, turn left walking, jog, run a curve, stop.
func _velocity(time: float) -> Vector3:
	if time < 1.0:
		return Vector3.ZERO
	if time < 4.0:
		return Vector3(0, 0, 1.3)
	if time < 6.0:
		return Vector3(1.3, 0, 0)
	if time < 8.0:
		return Vector3(2.8, 0, 0)
	if time < 10.0:
		var a := (time - 8.0) * 0.8
		return Vector3(cos(a), 0, -sin(a)) * 4.0
	return Vector3.ZERO


func _initialize() -> void:
	fmb = _arg("fmb", "")
	out_dir = _arg("out", "/tmp/fmb_char")
	seconds = float(_arg("seconds", "12"))
	shots = int(_arg("shots", "4"))
	DirAccess.make_dir_recursive_absolute(out_dir)
	var root := Node3D.new()
	get_root().add_child(root)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.55, 0.62, 0.7)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.5, 0.5, 0.55)
	root.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation = Vector3(deg_to_rad(-50), deg_to_rad(30), 0)
	sun.shadow_enabled = true
	root.add_child(sun)
	var ground := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(40, 40)
	ground.mesh = pm
	var gm := StandardMaterial3D.new()
	gm.albedo_color = Color(0.4, 0.4, 0.38)
	ground.material_override = gm
	root.add_child(ground)
	ch = ClassDB.instantiate("FellowMotionCharacter3D")
	ch.model_path = fmb
	ch.body_path = _arg("body", "")
	ch.inference_mode = 1
	ch.auto_process = false
	ch.max_batch = 4
	root.add_child(ch)
	cam = Camera3D.new()
	root.add_child(cam)
	cam.current = true


func _process(_delta: float) -> bool:
	var dt := 1.0 / 60.0
	if shot_pending >= 0:
		get_root().get_texture().get_image().save_png(out_dir + "/shot_%04d.png" % shot_pending)
		shot_pending = -1
	if t >= seconds:
		var f := FileAccess.open(out_dir + "/joints.bin", FileAccess.WRITE)
		f.store_buffer(rec.to_byte_array())
		var j := FileAccess.open(out_dir + "/info.json", FileAccess.WRITE)
		j.store_string(JSON.stringify({"steps": step, "stats": ch.get_stats(), "service": ClassDB.class_call_static("FellowMotionCharacter3D", "get_service_stats", fmb), "frames": info}))
		print("done ", step, " steps ", ch.get_stats())
		return true
	ch.desired_velocity = _velocity(t)
	ch.advance(dt)
	ClassDB.class_call_static("FellowMotionCharacter3D", "flush_requests", fmb)
	t += dt
	step += 1
	var jp: PackedVector3Array = ch.get_joint_positions()
	for p in jp:
		rec.append(p.x)
		rec.append(p.y)
		rec.append(p.z)
	info.append([snappedf(t, 0.001), ch.get_state(), snappedf(ch.get_actual_velocity().length(), 0.001)])
	# Follow camera, side-on.
	var hp: Vector3 = ch.global_position
	cam.look_at_from_position(hp + Vector3(3.2, 1.4, 1.2), hp + Vector3(0, 0.9, 0))
	if shots > 0 and step % int(seconds * 60.0 / shots) == 0:
		shot_pending = step
	return false
