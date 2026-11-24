extends SceneTree
## Milestone 195: the player's camera measured over a scripted moment (headless, fixed frame rate): the owner saw it
## jump as the character stepped up onto the fountain's rim. The player (autopilot) runs at the fountain from
## --from (x,z) for --seconds; every frame the camera's place and view are taken, and what changes faster than the
## frames around it (the second difference: a jerk) is reported — the largest in metres and degrees, and when. Through
## the step up (the traversal's mantle): the body's fastest across the ground, the hips' fastest change of speed, and the
## share of its frames with a limb through a surface (anim_quality.gd's test). --trace=1 prints every frame.
##
##   godot --headless --path showcase --fixed-fps 60 -s res://tools/camera_look.gd -- [--from=-3.5,6] [--seconds=4]
##       [--walk=1] [--trace=1]

var _player: CharacterBody3D
var _cam: Camera3D
var _from := Vector2(-3.5, 6.0)
var _seconds := 4.0
var _walk := false
var _trace := false
var _frame := 0
var _pos: Array[Vector3] = []
var _fwd: Array[Vector3] = []
var _states: Array[String] = []
var _scene: Node
var _skel: Skeleton3D
var _hips: Array[Vector3] = [] # the hips drawn, through the mantle
var _mantle_v := 0.0 # the body's fastest across the ground through the mantle (m/s)
var _mantle_frames := 0
var _inside := 0 # frames of the mantle with a limb through a surface (anim_quality.gd's test)
var _last_body := Vector3()
var _inside_bones := {}
const INSIDE_BONES := ["Head", "LeftLowerArm", "RightLowerArm", "LeftLowerLeg", "RightLowerLeg", "LeftFoot", "RightFoot",
		"LeftToes", "RightToes"]
var _to := Vector3()


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--from="):
			var p := a.get_slice("=", 1).split(",")
			_from = Vector2(float(p[0]), float(p[1]))
		elif a.begins_with("--seconds="):
			_seconds = float(a.get_slice("=", 1))
		elif a == "--walk=1":
			_walk = true
		elif a == "--trace=1":
			_trace = true
	_scene = (load("res://rooftops/rooftops.tscn") as PackedScene).instantiate()
	root.add_child(_scene)


func _setup() -> void: # the first frame: the scene in the tree
	_player = _scene.get_node("Player")
	_player.set("autopilot", true)
	_player.global_position = Vector3(_from.x, 0.0, _from.y)
	var fountain := (_scene.get_node("Town/Fountain") as Node3D).global_position
	_to = Vector3(fountain.x - _from.x, 0.0, fountain.z - _from.y).normalized()
	_player.set("facing", atan2(-_to.x, -_to.z))
	_player.rotation.y = _player.get("facing")
	_player.set("want_walk", _walk)
	_skel = _player.get_node("Model/Mannequin/Skeleton3D")


func _bone(n: String) -> Vector3:
	return _skel.global_transform * _skel.get_bone_global_pose(_skel.find_bone(n)).origin


func _mantle_frame() -> void:
	var body := _player.global_position
	if _player.get("traversal").is_active():
		_mantle_frames += 1
		_mantle_v = maxf(_mantle_v, Vector2(body.x - _last_body.x, body.z - _last_body.z).length() * 60.0)
		var hips := _bone("Hips")
		_hips.append(hips)
		var space := _player.get_world_3d().direct_space_state
		for n in INSIDE_BONES:
			var j := _bone(n)
			var hit := space.intersect_ray(PhysicsRayQueryParameters3D.create(hips, j, 1, [_player.get_rid()]))
			if not hit.is_empty() and hips.distance_to(hit.position) < hips.distance_to(j) - 0.04:
				_inside += 1
				_inside_bones[n] = int(_inside_bones.get(n, 0)) + 1
				break
	_last_body = body


func _process(_d: float) -> bool:
	_frame += 1
	if _frame == 1:
		_setup()
	if _frame < 30:
		_player.global_position = Vector3(_from.x, 0.0, _from.y) # settle where it starts
		_player.set("wish", Vector3())
		return false
	_player.set("wish", _to)
	_cam = root.get_viewport().get_camera_3d()
	_pos.append(_cam.global_position)
	_fwd.append(-_cam.global_basis.z)
	_states.append(String(_player.get("state")))
	_mantle_frame()
	if _trace:
		print("  %.3f s %s %s act=%s player %s cam %s" % [(_frame - 30) / 60.0, _player.get("state"), _player.get("mode"),
				_player.get("traversal").is_active(), _player.global_position,
				_cam.global_position])
	if _frame < 30 + int(_seconds * 60.0):
		return false
	var worst_m := 0.0
	var worst_m_at := 0
	var worst_deg := 0.0
	var worst_deg_at := 0
	for i in range(1, _pos.size() - 1):
		var jm := (_pos[i + 1] - 2.0 * _pos[i] + _pos[i - 1]).length()
		if jm > worst_m:
			worst_m = jm
			worst_m_at = i
		var a0 := rad_to_deg(_fwd[i - 1].angle_to(_fwd[i]))
		var a1 := rad_to_deg(_fwd[i].angle_to(_fwd[i + 1]))
		var jd := absf(a1 - a0)
		if jd > worst_deg:
			worst_deg = jd
			worst_deg_at = i
	var acc := 0.0
	for i in range(1, _hips.size() - 1):
		acc = maxf(acc, (_hips[i + 1] - 2.0 * _hips[i] + _hips[i - 1]).length() * 3600.0)
	print("camera_look: the mantle %d frames, the body at most %.2f m/s across, the hips' fastest change %.0f m/s², a limb through a surface %.0f%%" % [
			_mantle_frames, _mantle_v, acc, 100.0 * _inside / maxf(1.0, _mantle_frames)], " ", _inside_bones)
	print("camera_look: jerk %.4f m at %.2f s (%s), %.3f deg at %.2f s (%s); the character up %.2f m" % [worst_m,
			worst_m_at / 60.0, _states[worst_m_at], worst_deg, worst_deg_at / 60.0, _states[worst_deg_at], _player.global_position.y])
	return true
