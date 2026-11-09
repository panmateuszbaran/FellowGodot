extends CharacterBody3D
## The showcase's character: third-person movement over the terrain (WASD or the arrow keys, Shift to run, Space to
## jump, the mouse or the right stick to look, the wheel to zoom), its clips blended by speed (an AnimationTree: idle,
## walk and run in a BlendSpace1D, scaled to the ground speed, and a falling pose in the air), wading and swimming in the
## lake, and FellowSnowTracker3D under its feet: footprints when there is snow on the ground.

const Humanoid := preload("res://tools/humanoid_builder.gd")

@export var walk_speed := 1.7
@export var run_speed := 4.6
@export var jump_velocity := 4.4
@export var turn_speed := 10.0
@export var acceleration := 9.0
@export var mouse_sensitivity := 0.0025
@export var water_level := 0.0 ## the lake's surface (FellowWater3D's height): deeper than wading depth, the character swims
@export var min_zoom := 1.6
@export var max_zoom := 9.0
@export var start_yaw := 0.0 ## the camera's heading at the start (radians); the character faces the same way

@onready var model: Node3D = $Model
@onready var pivot: Node3D = $CameraPivot
@onready var arm: SpringArm3D = $CameraPivot/SpringArm3D
@onready var camera: Camera3D = $CameraPivot/SpringArm3D/Camera3D
@onready var tree: AnimationTree = $AnimationTree

var _gravity := 9.8
var _blend := 0.0
var _air := 0.0
var _zoom := 4.5
var _swimming := false


static func ensure_actions() -> void:
	var keys := {
		"move_forward": [KEY_W, KEY_UP], "move_back": [KEY_S, KEY_DOWN], "move_left": [KEY_A, KEY_LEFT],
		"move_right": [KEY_D, KEY_RIGHT], "jump": [KEY_SPACE], "run": [KEY_SHIFT],
	}
	for action in keys:
		if InputMap.has_action(action):
			continue
		InputMap.add_action(action)
		for k in keys[action]:
			var e := InputEventKey.new()
			e.physical_keycode = k
			InputMap.action_add_event(action, e)
	var pad := {"move_forward": [JOY_AXIS_LEFT_Y, -1.0], "move_back": [JOY_AXIS_LEFT_Y, 1.0], "move_left": [JOY_AXIS_LEFT_X, -1.0], "move_right": [JOY_AXIS_LEFT_X, 1.0]}
	for action in pad:
		var m := InputEventJoypadMotion.new()
		m.axis = pad[action][0]
		m.axis_value = pad[action][1]
		InputMap.action_add_event(action, m)
	var jb := InputEventJoypadButton.new()
	jb.button_index = JOY_BUTTON_A
	InputMap.action_add_event("jump", jb)
	var rb := InputEventJoypadButton.new()
	rb.button_index = JOY_BUTTON_LEFT_SHOULDER
	InputMap.action_add_event("run", rb)


func _ready() -> void:
	ensure_actions()
	_gravity = float(ProjectSettings.get_setting("physics/3d/default_gravity", 9.8))
	arm.add_excluded_object(get_rid())
	pivot.rotation.y = start_yaw
	model.rotation.y = start_yaw + PI
	_zoom = arm.spring_length
	tree.active = true


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		pivot.rotation.y -= event.relative.x * mouse_sensitivity
		arm.rotation.x = clampf(arm.rotation.x - event.relative.y * mouse_sensitivity, deg_to_rad(-70.0), deg_to_rad(35.0))
	elif event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_zoom = maxf(min_zoom, _zoom * 0.9)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_zoom = minf(max_zoom, _zoom / 0.9)


func _physics_process(delta: float) -> void:
	# The right stick looks around.
	var look := Vector2(Input.get_joy_axis(0, JOY_AXIS_RIGHT_X), Input.get_joy_axis(0, JOY_AXIS_RIGHT_Y))
	if look.length() > 0.15:
		pivot.rotation.y -= look.x * 2.5 * delta
		arm.rotation.x = clampf(arm.rotation.x - look.y * 1.8 * delta, deg_to_rad(-70.0), deg_to_rad(35.0))
	arm.spring_length = lerpf(arm.spring_length, _zoom, 1.0 - exp(-10.0 * delta))

	var input := Input.get_vector("move_left", "move_right", "move_forward", "move_back")
	var view_basis := Basis(Vector3.UP, pivot.rotation.y)
	var dir := view_basis * Vector3(input.x, 0.0, input.y)
	dir.y = 0.0
	var amount := minf(dir.length(), 1.0)
	dir = dir.normalized() if amount > 0.0 else Vector3.ZERO
	var depth := water_level - global_position.y # how deep the feet are under the lake's surface
	_swimming = depth > 1.25
	var target_speed := (run_speed if Input.is_action_pressed("run") else walk_speed) * amount
	if _swimming:
		target_speed = minf(target_speed, 1.6)
	elif depth > 0.4:
		target_speed *= 0.6 # wading
	var horizontal := Vector3(velocity.x, 0.0, velocity.z)
	horizontal = horizontal.move_toward(dir * target_speed, acceleration * delta * maxf(target_speed, walk_speed))
	velocity.x = horizontal.x
	velocity.z = horizontal.z

	if _swimming:
		# Float with the head above the surface.
		velocity.y = lerpf(velocity.y, clampf((depth - 1.3) * 3.0, -2.0, 2.0), 1.0 - exp(-4.0 * delta))
	elif is_on_floor():
		if Input.is_action_just_pressed("jump"):
			velocity.y = jump_velocity
	else:
		velocity.y -= _gravity * delta
	move_and_slide()

	# Face the way it moves.
	if horizontal.length() > 0.2:
		var yaw := atan2(horizontal.x, horizontal.z)
		model.rotation.y = lerp_angle(model.rotation.y, yaw, 1.0 - exp(-turn_speed * delta))

	# The clips: idle 0, walk 1, run 2, each played at the speed that keeps the feet on the ground.
	var speed := horizontal.length()
	var blend_target := 0.0
	if speed > 0.05:
		blend_target = speed / walk_speed if speed <= walk_speed else 1.0 + (speed - walk_speed) / maxf(run_speed - walk_speed, 0.01)
	_blend = lerpf(_blend, clampf(blend_target, 0.0, 2.0), 1.0 - exp(-8.0 * delta))
	var walk_scale := walk_speed / Humanoid.WALK_CLIP_SPEED
	var run_scale := run_speed / Humanoid.RUN_CLIP_SPEED
	var time_scale := walk_scale if _blend <= 1.0 else lerpf(walk_scale, run_scale, _blend - 1.0)
	if _blend < 1.0 and speed > 0.05:
		time_scale *= clampf(speed / walk_speed, 0.6, 1.0)
	var airborne := not is_on_floor() and not _swimming and velocity.y < -1.0
	_air = lerpf(_air, 1.0 if (airborne or (not is_on_floor() and not _swimming and velocity.y > 0.5)) else 0.0, 1.0 - exp(-10.0 * delta))
	tree.set("parameters/locomotion/blend_position", _blend)
	tree.set("parameters/speed/scale", time_scale)
	tree.set("parameters/air/blend_amount", _air)


func place(p_position: Vector3, p_yaw: float) -> void:
	global_position = p_position
	start_yaw = p_yaw
	pivot.rotation.y = p_yaw
	model.rotation.y = p_yaw + PI


func is_swimming() -> bool:
	return _swimming
