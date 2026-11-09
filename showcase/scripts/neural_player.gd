extends Node3D
## The showcase's character when MotionBricks' files are in showcase/external (README.md): NVIDIA's SOMA mannequin moved
## by FellowMotionCharacter3D — its whole-body motion generated on the GPU by the MotionBricks network from where the
## player wants to go, no animation clips. This script turns the keys into the character's desired velocity (WASD or
## the left stick; Shift runs), keeps it on the terrain (FellowMotionCharacter3D.set_ground_height(); the network's
## motion is on a plane), steers it along the houses' walls and the lake's deep water (it has no physics body), follows
## it with a camera on a spring arm, and puts FellowSnowTracker3D under its feet once its body has loaded.

@export var model_path := "res://external/soma_f16.fmb"
@export var body_path := "res://external/soma_mannequin.glb"
@export var walk_speed := 1.4
@export var run_speed := 3.2
@export var water_level := 0.0 ## the lake's surface: the character keeps out of water deeper than its knees
@export var start_yaw := 0.0 ## the camera's heading at the start (radians)
@export var terrain_path := NodePath("../Terrain")
@export var mouse_sensitivity := 0.0025
@export var min_zoom := 1.6
@export var max_zoom := 9.0
@export var obstacle_mask := 2 ## the physics layers it steers round (the village's buildings)

@onready var character: Node3D = $Character
@onready var pivot: Node3D = $CameraPivot
@onready var arm: SpringArm3D = $CameraPivot/SpringArm3D

var _terrain: Node
var _ground := 0.0
var _zoom := 4.5
var _feet_done := false
var _probe := CapsuleShape3D.new()


func _ready() -> void:
	preload("res://scripts/player.gd").ensure_actions()
	_terrain = get_node_or_null(terrain_path)
	_probe.radius = 0.32
	_probe.height = 1.5
	character.set("model_path", model_path)
	character.set("body_path", body_path)
	character.set("max_speed", run_speed)
	character.rotation.y = start_yaw + PI
	pivot.top_level = true
	pivot.global_position = character.global_position + Vector3(0, 1.55, 0)
	pivot.rotation.y = start_yaw
	_zoom = arm.spring_length
	_ground = global_position.y


func place(p_position: Vector3, p_yaw: float) -> void:
	global_position = p_position
	character.global_position = p_position
	character.rotation.y = p_yaw + PI
	start_yaw = p_yaw
	pivot.global_position = p_position + Vector3(0, 1.55, 0)
	pivot.rotation.y = p_yaw
	_ground = p_position.y


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		pivot.rotation.y -= event.relative.x * mouse_sensitivity
		arm.rotation.x = clampf(arm.rotation.x - event.relative.y * mouse_sensitivity, deg_to_rad(-70.0), deg_to_rad(35.0))
	elif event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_zoom = maxf(min_zoom, _zoom * 0.9)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_zoom = minf(max_zoom, _zoom / 0.9)


func _height(p: Vector3) -> float:
	if _terrain:
		var h: float = _terrain.call("get_height", p)
		if not is_nan(h):
			return h
	return _ground


## Its desired velocity without what would take it into a wall or deep water.
func _steer(v: Vector3) -> Vector3:
	if v.length() < 0.01:
		return v
	var pos := character.global_position
	# Walls: a capsule round the body (0.15 to 1.65 m) swept 0.6 s ahead against the buildings' layer.
	var params := PhysicsShapeQueryParameters3D.new()
	params.shape = _probe
	params.collision_mask = obstacle_mask
	params.transform = Transform3D(Basis(), pos + Vector3(0, 0.9, 0))
	params.motion = v * 0.6 + v.normalized() * 0.2
	var space := get_world_3d().direct_space_state
	var cast := space.cast_motion(params)
	if cast.size() == 2 and cast[1] < 1.0:
		params.transform.origin += params.motion * cast[1]
		params.motion = Vector3.ZERO
		var info := space.get_rest_info(params)
		if info.has("normal"):
			var n: Vector3 = info["normal"]
			n.y = 0.0
			if n.length() > 0.01:
				n = n.normalized()
				if v.dot(n) < 0.0:
					v -= n * v.dot(n)
	# Deep water: no further downhill into it.
	var ahead := pos + v.normalized() * 1.2
	if _height(ahead) < water_level - 0.6:
		var e := 0.5
		var g := Vector3(_height(ahead + Vector3(e, 0, 0)) - _height(ahead - Vector3(e, 0, 0)), 0.0, _height(ahead + Vector3(0, 0, e)) - _height(ahead - Vector3(0, 0, e)))
		if g.length() > 1e-4:
			var down := -g.normalized() # into the water
			if v.dot(down) > 0.0:
				v -= down * v.dot(down)
		else:
			v = Vector3.ZERO
	return v


func _process(delta: float) -> void:
	var look := Vector2(Input.get_joy_axis(0, JOY_AXIS_RIGHT_X), Input.get_joy_axis(0, JOY_AXIS_RIGHT_Y))
	if look.length() > 0.15:
		pivot.rotation.y -= look.x * 2.5 * delta
		arm.rotation.x = clampf(arm.rotation.x - look.y * 1.8 * delta, deg_to_rad(-70.0), deg_to_rad(35.0))
	arm.spring_length = lerpf(arm.spring_length, _zoom, 1.0 - exp(-10.0 * delta))
	var input := Input.get_vector("move_left", "move_right", "move_forward", "move_back")
	var dir := Basis(Vector3.UP, pivot.rotation.y) * Vector3(input.x, 0.0, input.y)
	dir.y = 0.0
	var amount := minf(dir.length(), 1.0)
	var speed := (run_speed if Input.is_action_pressed("run") else walk_speed) * amount
	var v := dir.normalized() * speed if amount > 0.0 else Vector3.ZERO
	character.set("desired_velocity", _steer(v))
	# The ground under it (the network's motion is on a plane).
	var h := _height(character.global_position)
	_ground = lerpf(_ground, h, 1.0 - exp(-14.0 * delta))
	character.call("set_ground_height", _ground)
	# The camera follows the hips' ground point.
	var target := character.global_position + Vector3(0, 1.55, 0)
	pivot.global_position = pivot.global_position.lerp(target, 1.0 - exp(-12.0 * delta))
	if not _feet_done and character.call("is_ready"):
		_feet_done = _attach_feet()


func _attach_feet() -> bool:
	var sk := _find_skeleton(character)
	if sk == null:
		return false
	for side in ["Left", "Right"]:
		if sk.find_bone(side + "Foot") < 0:
			continue
		var ba := BoneAttachment3D.new()
		ba.name = side + "FootTrack"
		sk.add_child(ba)
		ba.bone_name = side + "Foot"
		var tr: Node3D = ClassDB.instantiate("FellowSnowTracker3D")
		tr.set("radius", 0.07)
		tr.set("contact_height", 0.12)
		ba.add_child(tr)
	return true


func _find_skeleton(p_node: Node) -> Skeleton3D:
	for i in p_node.get_child_count(true):
		var c := p_node.get_child(i, true)
		if c is Skeleton3D:
			return c
		var s := _find_skeleton(c)
		if s:
			return s
	return null


func is_using_motionbricks() -> bool:
	return true


func get_status() -> String:
	var err: String = character.call("get_error")
	if err != "":
		return "MotionBricks: " + err
	return "MotionBricks" if character.call("is_ready") else "MotionBricks (loading)"
