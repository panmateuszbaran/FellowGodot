extends Node3D
## The showcase's character when MotionBricks' files are in showcase/external (README.md): NVIDIA's SOMA mannequin moved
## by FellowMotionCharacter3D — its whole-body motion generated on the GPU by the MotionBricks network from where the
## player wants to go, no animation clips. This script turns the keys into the character's desired velocity (WASD or
## the left stick; Shift runs), plays the motion a little faster than the network's (time_scale: brisker walking and
## running, the feet still on the ground), keeps it on the terrain (FellowMotionCharacter3D.set_ground_height(); the
## network's motion is on a plane), steers it along the houses' walls and the lake's deep water (it has no physics
## body), follows it with a camera on a spring arm, and puts FellowSnowTracker3D under its feet once its body has loaded.
## The network has no jumps or rolls: those (Space, C) are drawn over its motion here — the body raised on a parabola
## with its legs drawn up, or tucked into a ball and turned once round its right axis while it runs on.

@export var model_path := "res://external/soma_f16.fmb"
@export var body_path := "res://external/soma_mannequin.glb"
@export var walk_speed := 1.5 ## the network's speeds: played time_scale times faster
@export var run_speed := 3.3
@export var time_scale := 1.3 ## the motion played this much faster while moving (1 standing)
@export var jump_velocity := 4.0
@export var gravity := 11.0
@export var roll_time := 0.95 ## seconds for a roll
@export var water_level := 0.0 ## the lake's surface: the character keeps out of water deeper than its knees
@export var start_yaw := 0.0 ## the camera's heading at the start (radians)
@export var terrain_path := NodePath("../Terrain")
@export var mouse_sensitivity := 0.0025
@export var min_zoom := 1.6
@export var max_zoom := 9.0
@export var obstacle_mask := 2 ## the physics layers it steers round (the village's buildings)
@export var camera_clearance := 0.9 ## the camera kept this high over the terrain, out of the grass

@onready var character: Node3D = $Character
@onready var pivot: Node3D = $CameraPivot
@onready var arm: SpringArm3D = $CameraPivot/SpringArm3D
@onready var camera: Camera3D = $CameraPivot/SpringArm3D/Camera3D

var _terrain: Node
var _ground := 0.0
var _zoom := 4.5
var _feet_done := false
var _probe := CapsuleShape3D.new()
var _scale := 1.0
# A jump: the height over the ground and its speed; the legs drawn up (0..1).
var _jump_h := 0.0
var _jump_v := 0.0
var _airborne := false
var _tuck_air := 0.0
# A roll: its time (< 0: none) and its direction.
var _roll_t := -1.0
var _roll_dir := Vector3.FORWARD
var _skeleton: Skeleton3D
var _bones := {}
var autopilot := Vector3.ZERO ## tests: a world direction to walk in without input


func _ready() -> void:
	preload("res://scripts/player.gd").ensure_actions()
	_terrain = get_node_or_null(terrain_path)
	_probe.radius = 0.32
	_probe.height = 1.5
	character.set("model_path", model_path)
	character.set("body_path", body_path)
	character.set("max_speed", run_speed)
	# Advanced from here: faster than real time, and before the jump's and the roll's poses are drawn over it.
	character.set("auto_process", false)
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
	pos.y = _ground
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
	if autopilot != Vector3.ZERO:
		dir = autopilot
	var amount := minf(dir.length(), 1.0)
	var speed := (run_speed if Input.is_action_pressed("run") else walk_speed) * amount
	var v := dir.normalized() * speed if amount > 0.0 else Vector3.ZERO
	var loaded: bool = character.call("is_ready")
	# A jump or a roll from the ground.
	if loaded and not _airborne and _roll_t < 0.0:
		if Input.is_action_just_pressed("jump"):
			_airborne = true
			_jump_v = jump_velocity
		elif Input.is_action_just_pressed("roll"):
			_roll_t = 0.0
			_roll_dir = dir.normalized() if amount > 0.1 else _facing()
	if _roll_t >= 0.0:
		# Rolling: on along its direction at a run (the network runs, the roll is drawn over it).
		v = _roll_dir * maxf(speed, walk_speed * 1.6)
	character.set("desired_velocity", _steer(v))
	# The ground under it (the network's motion is on a plane), raised by a jump.
	var h := _height(character.global_position)
	_ground = lerpf(_ground, h, 1.0 - exp(-14.0 * delta))
	if _airborne:
		_jump_v -= gravity * delta
		_jump_h += _jump_v * delta
		if _jump_h <= 0.0 and _jump_v < 0.0:
			_jump_h = 0.0
			_jump_v = 0.0
			_airborne = false
	character.call("set_ground_height", _ground + _jump_h)
	# The network's motion, faster while it moves.
	var target_scale := lerpf(1.0, time_scale, clampf(v.length() / walk_speed, 0.0, 1.0))
	_scale = lerpf(_scale, target_scale, 1.0 - exp(-6.0 * delta))
	character.call("advance", delta * _scale)
	# The jump's and the roll's poses over it.
	# The legs drawn up on the way up and over the top, down again for the landing.
	var up := _airborne and (_jump_v > 0.0 or _jump_h > 0.3)
	_tuck_air = lerpf(_tuck_air, 1.0 if up else 0.0, 1.0 - exp(-(12.0 if up else 16.0) * delta))
	if _roll_t >= 0.0:
		_roll_t += delta
		if _roll_t >= roll_time:
			_roll_t = -1.0
	if loaded and (_tuck_air > 0.01 or _roll_t >= 0.0):
		_overlay()
	# The camera follows the hips' ground point (half of a jump's height).
	var target := Vector3(character.global_position.x, _ground + _jump_h * 0.5 + 1.55, character.global_position.z)
	pivot.global_position = pivot.global_position.lerp(target, 1.0 - exp(-12.0 * delta))
	# Looking up swings the camera down to the ground: it slides over it instead, out of the grass.
	var cp := camera.global_position
	var floor_y := _height(cp) + camera_clearance
	if cp.y < floor_y:
		camera.global_position = Vector3(cp.x, floor_y, cp.z)
	if not _feet_done and loaded:
		_feet_done = _attach_feet()


## The way the body faces (the network's heading).
func _facing() -> Vector3:
	var f := character.global_basis.z
	f.y = 0.0
	return f.normalized() if f.length() > 1e-3 else Vector3.FORWARD


func _bone(p_name: String) -> int:
	if not _bones.has(p_name):
		_bones[p_name] = _skeleton.find_bone(p_name) if _skeleton else -1
	return _bones[p_name]


## Turns a bone by p_angle about an axis (skeleton space) through its head, its children with it; p_rest (0..1) first
## takes it that far towards its rest pose (the legs: the stride undone).
func _turn(p_name: String, p_axis: Vector3, p_angle: float, p_rest := 0.0) -> void:
	var b := _bone(p_name)
	if b < 0:
		return
	var q: Quaternion = _skeleton.get_bone_pose_rotation(b)
	if p_rest > 0.0:
		q = q.slerp(_skeleton.get_bone_rest(b).basis.get_rotation_quaternion(), p_rest)
	if absf(p_angle) > 1e-4:
		var parent := _skeleton.get_bone_parent(b)
		var pb := _skeleton.get_bone_global_pose(parent).basis.orthonormalized() if parent >= 0 else Basis()
		q = Quaternion((pb.inverse() * p_axis).normalized(), p_angle) * q
	_skeleton.set_bone_pose_rotation(b, q)


## The jump's legs drawn up and the roll's ball and turn, over the network's pose. Rotations about the body's right
## axis: a positive angle tips what points up forwards (the spine curling) and what points down backwards (a knee
## bending); a negative one brings a thigh or an arm forwards.
func _overlay() -> void:
	if _skeleton == null:
		_skeleton = _find_skeleton(character)
		if _skeleton == null:
			return
	var sk_basis := _skeleton.global_basis
	var to_sk := sk_basis.inverse()
	var fwd := _facing()
	var a_body := (to_sk * Vector3.UP.cross(fwd)).normalized()
	# How far into a roll's ball (0..1) and its turn; a jump draws the legs up and the arms forward, the back nearly
	# straight.
	var ball := 0.0
	var phi := 0.0
	if _roll_t >= 0.0:
		var rt := _roll_t / roll_time
		ball = smoothstep(0.0, 0.22, rt) * (1.0 - smoothstep(0.72, 1.0, rt))
		phi = TAU * smoothstep(0.1, 0.86, rt)
	var air := _tuck_air
	var spine := 0.7 * ball + 0.1 * air
	_turn("Spine1", a_body, spine * 0.5)
	_turn("Spine2", a_body, spine * 0.7)
	_turn("Chest", a_body, spine * 0.6)
	_turn("Neck1", a_body, 0.35 * ball)
	_turn("Head", a_body, 0.25 * ball)
	var still := maxf(ball, air * 0.7) # the stride undone
	for side in ["Left", "Right"]:
		_turn(side + "Leg", a_body, -2.0 * ball - 1.1 * air, still)
		_turn(side + "Shin", a_body, 2.2 * ball + 1.5 * air, still)
		_turn(side + "Foot", a_body, -0.3 * ball - 0.2 * air, still)
		_turn(side + "Arm", a_body, -1.1 * ball - 0.7 * air)
		_turn(side + "ForeArm", a_body, -0.6 * ball - 0.5 * air)
	# The roll: the whole body lowered into a ball (its centre the joints' average, its lowest point on the ground) and
	# turned once about that centre.
	if _roll_t < 0.0:
		return
	var hips := _bone("Hips")
	if hips < 0:
		return
	var t := _roll_t / roll_time
	var g := _skeleton.get_bone_global_pose(hips)
	var to_world := _skeleton.global_transform
	var hw := to_world * g.origin
	var rel := Vector3.ZERO
	var pts := PackedVector3Array()
	for j in ["Hips", "Head", "LeftShin", "RightShin", "LeftFoot", "RightFoot", "LeftHand", "RightHand"]:
		var b := _bone(j)
		if b >= 0:
			var p := to_world.basis * (_skeleton.get_bone_global_pose(b).origin - g.origin)
			pts.append(p)
			rel += p
	rel /= maxf(pts.size(), 1)
	var radius := 0.3
	for p in pts:
		radius = maxf(radius, p.distance_to(rel))
	var ground_y := _ground + _jump_h
	var drop := (hw.y + rel.y - (ground_y + radius * 0.85)) * smoothstep(0.0, 0.25, t) * (1.0 - smoothstep(0.75, 1.0, t))
	var centre := hw + rel - Vector3(0, drop, 0)
	var lowered := hw - Vector3(0, drop, 0)
	var r := Basis(Vector3.UP.cross(_roll_dir).normalized(), phi)
	var new_world := centre + r * (lowered - centre)
	var new_basis := (to_sk * r * sk_basis) * g.basis
	var ng := Transform3D(new_basis, to_world.affine_inverse() * new_world)
	var parent := _skeleton.get_bone_parent(hips)
	var local := (_skeleton.get_bone_global_pose(parent).affine_inverse() * ng) if parent >= 0 else ng
	_skeleton.set_bone_pose_position(hips, local.origin)
	_skeleton.set_bone_pose_rotation(hips, local.basis.get_rotation_quaternion())


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
