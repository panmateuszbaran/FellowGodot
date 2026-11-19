extends CharacterBody3D
## The rooftops' character: the mannequin (made in Blender: tools/blender/build_mannequin.py) moved by this controller
## and by FellowTraversal3D — running, sprinting, jumping and rolling on the ground; mantling, vaulting; climbing the
## buildings from hold to hold as in Assassin's Creed (sills, hoods, string courses, corbels and beams: hanging braced
## on the wall, shimmying along a ledge, reaching up or across hand over hand to the next, leaping to one further —
## the traversal moves the body, FellowTraversalPose3D puts the hands and the feet on the stone over the clips);
## perching on the bell tower's beam, the leap of faith and the way out of the hay.
##
## Its wishes come from the player (WASD or a stick, Shift to free-run — at a wall it jumps for the first hold, and
## hanging it climbs on, leaping where a hold is out of reach —, Space to jump, C to drop or roll, E to perch at a
## viewpoint) or from a script setting `wish`, `want_sprint`, `want_jump` and `want_drop` itself (`autopilot`: the
## showcase's demo).

enum Mode { GROUND, AIR, TRAVERSE, PERCH, DIVE, HAY, ROLL }

const ONE_SHOTS := {"jump": 0.34, "land": 0.45, "roll": 0.8, "climb_jump": 0.5, "dive_start": 0.55, "hay_exit": 2.2,
		"hang_reach_l": 0.75, "hang_reach_r": 0.75, "hang_leap_up": 0.45, "hang_leap_l": 0.5, "hang_leap_r": 0.5}
# FellowTraversal3D's Transfer values.
const TRANSFER_JUMP_ALONG := 2
const TRANSFER_JUMP_UP := 3
const TRANSFER_JUMP_DIAGONAL := 6
const TRANSFER_JUMP_BACK := 7
const TRANSFER_JUMP_CORNER := 8
const TRANSFER_REACH_UP := 9
const TRANSFER_REACH_SIDE := 10

@export var walk_speed := 1.6
@export var run_speed := 4.4
@export var sprint_speed := 7.0
@export var jump_velocity := 4.8
@export var gravity := 9.8
@export var turn_speed := 11.0
@export var acceleration := 16.0
@export var deceleration := 22.0
@export var air_control := 2.5
@export var roll_height := 3.5 ## a fall from higher than this, moving, ends in a roll
@export var camera_path := NodePath("../CameraRig")
@export var viewpoint_path := NodePath("") ## a Node3D at the beam's end: perching there, the leap of faith
@export var autopilot := false

@onready var model: Node3D = $Model
@onready var tree: AnimationTree = $AnimationTree
@onready var traversal: Node = $Traversal

var mode := Mode.GROUND
var facing := 0.0
var wish := Vector3() ## the way to go (global, length up to 1); set by the input or the autopilot
var want_sprint := false
var want_walk := false
var want_jump := false
var want_drop := false
var want_perch := false
var state := ""
var _one_shot := 0.0
var _air_top := 0.0
var _camera: Node3D
var _viewpoint: Node3D
var _last_pos := Vector3()
var _climb_speed := 0.0
var _dive_time := 0.0
var _land_kind := ""


func _ready() -> void:
	_ensure_actions()
	_camera = get_node_or_null(camera_path)
	_viewpoint = get_node_or_null(viewpoint_path)
	facing = rotation.y
	tree.active = true
	_set_state("loco")
	_last_pos = global_position
	traversal.connect("climb_jumped", func(_d): _play("climb_jump"))
	traversal.connect("hang_transfer_started", _on_transfer)


## A move from hold to hold: its clip (the hands and feet are put on the stone by the pose's IK over it).
func _on_transfer(_ledge: Dictionary, how: int, _duration: float, lead: int) -> void:
	var side := "l" if lead == 0 else "r"
	match how:
		TRANSFER_REACH_UP, TRANSFER_REACH_SIDE:
			_play("hang_reach_" + side)
		TRANSFER_JUMP_UP:
			_play("hang_leap_up")
		TRANSFER_JUMP_ALONG, TRANSFER_JUMP_DIAGONAL, TRANSFER_JUMP_CORNER, TRANSFER_JUMP_BACK:
			_play("hang_leap_" + side)


static func _ensure_actions() -> void:
	var keys := {
		"move_forward": [KEY_W, KEY_UP], "move_back": [KEY_S, KEY_DOWN], "move_left": [KEY_A, KEY_LEFT],
		"move_right": [KEY_D, KEY_RIGHT], "jump": [KEY_SPACE], "run": [KEY_SHIFT], "walk": [KEY_CTRL], "drop": [KEY_C],
		"interact": [KEY_E],
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
	for pair in [["jump", JOY_BUTTON_A], ["drop", JOY_BUTTON_B], ["interact", JOY_BUTTON_Y], ["run", JOY_BUTTON_RIGHT_SHOULDER]]:
		var b := InputEventJoypadButton.new()
		b.button_index = pair[1]
		InputMap.action_add_event(pair[0], b)


func _read_input() -> void:
	if autopilot:
		return
	var v := Input.get_vector("move_left", "move_right", "move_forward", "move_back")
	var yaw := _camera.get("yaw") as float if _camera else 0.0
	wish = Basis(Vector3.UP, yaw) * Vector3(v.x, 0.0, v.y)
	want_sprint = Input.is_action_pressed("run")
	want_walk = Input.is_action_pressed("walk")
	want_jump = want_jump or Input.is_action_just_pressed("jump")
	want_drop = want_drop or Input.is_action_just_pressed("drop")
	want_perch = want_perch or Input.is_action_just_pressed("interact")


func _physics_process(delta: float) -> void:
	_read_input()
	if _one_shot > 0.0:
		_one_shot -= delta
	var busy: bool = traversal.is_active() or traversal.is_hanging() or traversal.is_climbing() or traversal.is_on_ladder() or traversal.is_transferring()
	var speed_goal := run_speed
	if want_sprint:
		speed_goal = sprint_speed
	elif want_walk:
		speed_goal = walk_speed
	# Climbing is from hold to hold (no free climbing of bare walls).
	traversal.set("free_climb", false)
	traversal.set_intent(wish * speed_goal)
	match mode:
		Mode.DIVE:
			_dive(delta)
		Mode.HAY:
			_hay(delta)
		Mode.PERCH:
			_perch(delta)
		_:
			if busy:
				_traverse(delta)
			else:
				_move(delta, speed_goal)
	want_jump = false
	want_drop = false
	want_perch = false
	var moved := global_position - _last_pos
	_climb_speed = lerpf(_climb_speed, moved.length() / maxf(delta, 1e-4), 1.0 - exp(-10.0 * delta))
	_last_pos = global_position


func _move(delta: float, speed_goal: float) -> void:
	if mode == Mode.TRAVERSE:
		mode = Mode.GROUND if is_on_floor() else Mode.AIR
		facing = rotation.y
		_air_top = global_position.y
	var on_floor := is_on_floor()
	var h := Vector3(velocity.x, 0.0, velocity.z)
	var goal := wish.limit_length(1.0) * speed_goal
	if on_floor and mode != Mode.ROLL:
		var rate := acceleration if goal.length() > h.length() else deceleration
		h = h.move_toward(goal, rate * delta)
	elif mode == Mode.ROLL:
		h = h.move_toward(goal.normalized() * maxf(h.length() * 0.98, 3.0) if goal.length() > 0.1 else h * 0.97, 8.0 * delta)
	else:
		h = h.move_toward(goal.limit_length(maxf(h.length(), 2.0)), air_control * delta)
	velocity.x = h.x
	velocity.z = h.z
	if not on_floor:
		velocity.y -= gravity * delta
	if wish.length() > 0.1 and mode != Mode.ROLL:
		var target := atan2(-wish.x, -wish.z)
		var rate := turn_speed if on_floor else turn_speed * 0.3
		facing = lerp_angle(facing, target, 1.0 - exp(-rate * delta))
	rotation.y = facing
	# A viewpoint: perching on the beam's end.
	if want_perch and on_floor and _at_viewpoint():
		_start_perch()
		return
	# Free-running at a wall: a jump for the first hold (caught in the air by the traversal's grab).
	if want_sprint and on_floor and _one_shot <= 0.0 and mode == Mode.GROUND and _wall_ahead(1.1):
		want_jump = true
	if want_jump and on_floor and _one_shot <= 0.0:
		velocity.y = jump_velocity
		mode = Mode.AIR
		_air_top = global_position.y
		_play("jump")
	if want_drop and on_floor and h.length() > 2.0 and mode == Mode.GROUND:
		mode = Mode.ROLL
		_play("roll")
	move_and_slide()
	on_floor = is_on_floor()
	match mode:
		Mode.GROUND:
			if not on_floor:
				mode = Mode.AIR
				_air_top = global_position.y
			elif _one_shot <= 0.0:
				_set_state("loco")
		Mode.AIR:
			_air_top = maxf(_air_top, global_position.y)
			if on_floor:
				var fall := _air_top - global_position.y
				mode = Mode.GROUND
				if fall > roll_height and h.length() > 2.0:
					mode = Mode.ROLL
					_play("roll")
				elif fall > 1.2:
					_play("land")
				elif _one_shot <= 0.0:
					_set_state("loco")
			elif _one_shot <= 0.0 and (velocity.y < 1.0 or state != "jump"):
				_set_state("fall")
		Mode.ROLL:
			if _one_shot <= 0.0:
				mode = Mode.GROUND
				_set_state("loco")
	var hs := Vector2(velocity.x, velocity.z).length() if on_floor else 0.0
	tree.set("parameters/loco/blend_position", hs)


func _traverse(_delta: float) -> void:
	mode = Mode.TRAVERSE
	facing = rotation.y
	velocity = Vector3()
	if traversal.is_hanging() or traversal.is_transferring():
		# Free-running on: pushing on leaps to a hold out of reach (the traversal reaches to a near one by itself).
		var at_wall := wish.length() > 0.5
		if want_jump or (want_sprint and at_wall and not traversal.is_transferring()):
			traversal.hang_jump()
		elif want_drop:
			traversal.release()
		var along := absf(wish.dot(global_basis.x))
		tree.set("parameters/hang/blend_position", clampf(along * 1.5, 0.0, 1.0))
		if _one_shot <= 0.0:
			_set_state("hang")
	elif traversal.is_climbing():
		if want_jump and _one_shot <= 0.0:
			traversal.hang_jump()
		elif want_drop:
			traversal.release()
		tree.set("parameters/climb/blend_position", clampf(_climb_speed / 0.8, 0.0, 1.0))
		if _one_shot <= 0.0:
			_set_state("climb")
	elif traversal.is_on_ladder():
		tree.set("parameters/climb/blend_position", clampf(_climb_speed / 0.8, 0.0, 1.0))
		_set_state("climb")
	else:
		# A mantle or a vault: FellowTraversalPose3D plays its clip over whatever this plays.
		tree.set("parameters/loco/blend_position", 0.0)
		_set_state("loco")
	_air_top = global_position.y


## A wall in front of the chest within p_distance (the way the body faces and wishes to go).
func _wall_ahead(p_distance: float) -> bool:
	if wish.length() < 0.5:
		return false
	var d := Vector3(wish.x, 0.0, wish.z).normalized()
	var from := global_position + Vector3.UP * 1.3
	var q := PhysicsRayQueryParameters3D.create(from, from + d * (p_distance + 0.35), collision_mask, [get_rid()])
	var hit := get_world_3d().direct_space_state.intersect_ray(q)
	return not hit.is_empty() and absf((hit.normal as Vector3).y) < 0.3 and (hit.normal as Vector3).dot(-d) > 0.7


# ------------------------------------------------------------------ the viewpoint, the leap, the hay

func _at_viewpoint() -> bool:
	if _viewpoint == null:
		return false
	var d := _viewpoint.global_position - global_position
	return Vector2(d.x, d.z).length() < 0.9 and absf(d.y) < 0.6


func _start_perch() -> void:
	mode = Mode.PERCH
	velocity = Vector3()
	# Facing out along the beam, over the square.
	var out := -_viewpoint.global_basis.z if _viewpoint else -global_basis.z
	facing = atan2(-out.x, -out.z)
	var p := _viewpoint.global_position
	global_position = Vector3(p.x, global_position.y, p.z) - out * 0.15
	rotation.y = facing
	_set_state("perch")
	if _camera and _camera.has_method("synchronize"):
		_camera.synchronize(self)


func _perch(_delta: float) -> void:
	if want_jump:
		mode = Mode.DIVE
		_dive_time = 0.0
		_play("dive_start")
		velocity = -global_basis.z * 2.6 + Vector3.UP * 2.8
		if _camera and _camera.has_method("follow_dive"):
			_camera.follow_dive(self)
	elif wish.length() > 0.5 and wish.dot(global_basis.z) > 0.3:
		mode = Mode.GROUND
		_set_state("loco")
		if _camera and _camera.has_method("release"):
			_camera.release()


func _dive(delta: float) -> void:
	_dive_time += delta
	velocity.y -= gravity * delta
	# Steered onto the hay (as the leap of faith is): the horizontal speed that lands the body on it.
	var hay := _nearest_hay()
	var land_y := 1.4
	if hay:
		land_y = hay.global_position.y + 1.4
		var drop := maxf(global_position.y - land_y, 0.0)
		var t_left := (velocity.y + sqrt(maxf(velocity.y * velocity.y + 2.0 * gravity * drop, 0.0))) / gravity
		var to := hay.global_position - global_position
		var want := Vector3(to.x, 0.0, to.z) / maxf(t_left, 0.15)
		var h := Vector3(velocity.x, 0.0, velocity.z).lerp(want, 1.0 - exp(-4.0 * delta))
		velocity.x = h.x
		velocity.z = h.z
	if _one_shot <= 0.0:
		_set_state("dive")
	# Head first, steeper as it falls; turned over onto the back in the last metres before the hay.
	var above := global_position.y - land_y
	var dive_pitch := -deg_to_rad(55.0) * smoothstep(0.3, 1.2, _dive_time)
	var flip := smoothstep(4.5, 1.0, above)
	var a := lerpf(dive_pitch, -PI, flip)
	var r := Basis(Vector3.RIGHT, a)
	var pivot := Vector3(0.0, 0.95, 0.0)
	model.transform = Transform3D(r * Basis(Vector3.UP, PI), pivot - r * pivot)
	# Off the beam: the first moments clear of it (no collision), then into whatever is below.
	if _dive_time < 0.45:
		global_position += velocity * delta
		return
	var col := move_and_collide(velocity * delta)
	if col:
		var body := col.get_collider() as Node
		if body and body.is_in_group("hay"):
			_enter_hay(body as Node3D)
		else:
			model.transform = Transform3D(Basis(Vector3.UP, PI), Vector3())
			mode = Mode.ROLL
			velocity = Vector3(velocity.x, 0.0, velocity.z)
			_play("roll")


func _nearest_hay() -> Node3D:
	var best: Node3D = null
	var best_d := 30.0
	for n in get_tree().get_nodes_in_group("hay"):
		var d := Vector2(n.global_position.x - global_position.x, n.global_position.z - global_position.z).length()
		if d < best_d:
			best_d = d
			best = n
	return best


func _enter_hay(cart: Node3D) -> void:
	mode = Mode.HAY
	velocity = Vector3()
	model.transform = Transform3D(Basis(Vector3.UP, PI), Vector3())
	# The body on the cart's bed, facing out of its long side towards the square.
	var side := cart.global_basis.z
	if side.dot(global_position - cart.global_position) < 0.0:
		side = -side
	facing = atan2(-side.x, -side.z)
	rotation.y = facing
	global_position = Vector3(cart.global_position.x, cart.global_position.y + 0.85, cart.global_position.z)
	_play("hay_exit")
	if _camera and _camera.has_method("release"):
		_camera.release()


func _hay(_delta: float) -> void:
	# The clip's root motion carries the body out of the cart and down beside it.
	var m: Vector3 = tree.get_root_motion_position()
	global_position += global_basis * (Basis(Vector3.UP, PI) * m)
	# Never below the street: the clip ends on the ground, wherever the cart stands.
	var space := get_world_3d().direct_space_state
	var q := PhysicsRayQueryParameters3D.create(global_position + Vector3.UP * 0.35, global_position + Vector3.DOWN * 0.4,
			collision_mask, [get_rid()])
	var hit := space.intersect_ray(q)
	if hit and (hit.normal as Vector3).y > 0.7 and global_position.y < (hit.position as Vector3).y:
		global_position.y = (hit.position as Vector3).y
	if _one_shot <= 0.0:
		mode = Mode.GROUND
		velocity = Vector3()
		_set_state("loco")


# ------------------------------------------------------------------ the animation tree

func _set_state(s: String) -> void:
	if s == state:
		return
	state = s
	tree.set("parameters/state/transition_request", s)


func _play(s: String) -> void:
	state = ""
	_set_state(s)
	_one_shot = ONE_SHOTS.get(s, 0.0)
