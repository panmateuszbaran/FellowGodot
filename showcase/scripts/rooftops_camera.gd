extends Node3D
## The rooftops' camera: third person over the shoulder, its pivot following the character on critically damped
## springs (quicker across than up and down, so that jumps and climbs do not jolt it), looking a little ahead of the
## way it runs, drawn back and widened as it sprints, closer and tipped up while it climbs, kept out of the walls by a
## sphere cast (in fast, out slowly), turning behind the character by itself when the mouse leaves it alone; and two
## shots of its own — the swirl round a viewpoint ("synchronizing") and the follow of a leap of faith.

@export var target_path := NodePath("../Player")
@export var height := 1.55
@export var distance := 4.0
@export var shoulder := 0.42
@export var mouse_sensitivity := 0.0025
@export var follow_time := 0.09 ## the pivot's lag across (s)
@export var follow_time_up := 0.22 ## and up and down
@export var auto_yaw := true

var yaw := 0.0
var pitch := -0.22
var _pivot := Vector3()
var _pivot_vel := Vector3()
var _dist := 4.0
var _dist_goal := 4.0
var _fov := 62.0
var _idle_mouse := 10.0
var _shot := "" ## "", "sync", "dive"
var _shot_time := 0.0
var _shot_center := Vector3()
var _shot_yaw := 0.0
var _target: Node3D
var _lead := Vector3()
var _last_target := Vector3()
var _shake := 0.0

@onready var cam: Camera3D = $Camera3D


func _ready() -> void:
	_target = get_node_or_null(target_path)
	if _target:
		_pivot = _target.global_position + Vector3.UP * height
		_last_target = _target.global_position
		yaw = _target.rotation.y
	_dist = distance
	top_level = true


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		yaw -= event.relative.x * mouse_sensitivity
		pitch = clampf(pitch - event.relative.y * mouse_sensitivity, -1.2, 0.75)
		_idle_mouse = 0.0


## Steered by a script (the demo): the yaw and pitch it should go to, eased.
func aim(p_yaw: float, p_pitch: float, p_rate: float, delta: float) -> void:
	yaw = lerp_angle(yaw, p_yaw, 1.0 - exp(-p_rate * delta))
	pitch = lerpf(pitch, p_pitch, 1.0 - exp(-p_rate * delta))
	_idle_mouse = 0.0


func synchronize(p_target: Node3D) -> void:
	_shot = "sync"
	_shot_time = 0.0
	_shot_center = p_target.global_position
	# Round the side of the character away from the tower: from its left, past its front, to its right.
	var out := -p_target.global_basis.z
	_shot_yaw = atan2(out.x, out.z)


func follow_dive(_p_target: Node3D) -> void:
	_shot = "dive"
	_shot_time = 0.0


func release() -> void:
	_shot = ""


func shake(p_amount: float) -> void:
	_shake = maxf(_shake, p_amount)


static func _damp(cur: float, goal: float, vel: Array, time: float, dt: float) -> float:
	# A critically damped spring (Game Programming Gems 4, 1.10).
	var omega := 2.0 / maxf(time, 1e-4)
	var x := omega * dt
	var e := 1.0 / (1.0 + x + 0.48 * x * x + 0.235 * x * x * x)
	var change := cur - goal
	var temp: float = (vel[0] + omega * change) * dt
	vel[0] = (vel[0] - omega * temp) * e
	return goal + (change + temp) * e


func _process(delta: float) -> void:
	if _target == null:
		return
	var tpos: Vector3 = _target.get_global_transform_interpolated().origin if _target.is_inside_tree() else _target.global_position
	var tvel := (tpos - _last_target) / maxf(delta, 1e-4)
	_last_target = tpos
	var hvel := Vector3(tvel.x, 0.0, tvel.z)
	var speed := hvel.length()
	var climbing := false
	var trav := _target.get_node_or_null("Traversal")
	if trav:
		climbing = trav.is_climbing() or trav.is_hanging() or trav.is_active()
	_idle_mouse += delta
	_shot_time += delta
	# The pivot: over the character, a little ahead of where it runs.
	_lead = _lead.lerp(hvel.limit_length(7.0) * 0.12, 1.0 - exp(-2.5 * delta))
	var goal := tpos + Vector3.UP * height + _lead
	var vx := [_pivot_vel.x]
	var vy := [_pivot_vel.y]
	var vz := [_pivot_vel.z]
	_pivot.x = _damp(_pivot.x, goal.x, vx, follow_time, delta)
	_pivot.y = _damp(_pivot.y, goal.y, vy, follow_time_up if not climbing else follow_time_up * 1.6, delta)
	_pivot.z = _damp(_pivot.z, goal.z, vz, follow_time, delta)
	_pivot_vel = Vector3(vx[0], vy[0], vz[0])
	# Behind the character by itself when the mouse has been still a while.
	if auto_yaw and _shot == "" and _idle_mouse > 1.5 and speed > 1.0 and not climbing:
		var behind := atan2(-hvel.x, -hvel.z)
		yaw = lerp_angle(yaw, behind, 1.0 - exp(-0.9 * delta * clampf(speed / 4.0, 0.3, 1.5)))
	var sprint := clampf((speed - 4.6) / 2.4, 0.0, 1.0)
	_dist_goal = distance + 1.3 * sprint - (0.5 if climbing else 0.0)
	var fov_goal := 62.0 + 9.0 * sprint
	var p := pitch
	if climbing and _idle_mouse > 1.0:
		p = lerpf(pitch, 0.18, 0.5)
	var cam_pos: Vector3
	var look_at_pos := _pivot
	match _shot:
		"sync":
			# Round the character on the side away from the tower, rising: the whole square and the city below.
			var u := smoothstep(0.0, 1.0, clampf(_shot_time / 6.5, 0.0, 1.0))
			var a := _shot_yaw + deg_to_rad(lerpf(-105.0, 75.0, u))
			var r := lerpf(4.2, 7.5, u)
			var h := lerpf(0.4, 3.2, u)
			cam_pos = _shot_center + Vector3(sin(a) * r, h, cos(a) * r)
			look_at_pos = _shot_center + Vector3.UP * lerpf(1.0, 0.2, u)
			fov_goal = lerpf(52.0, 64.0, u)
			cam.global_transform = Transform3D(Basis(), cam_pos).looking_at(look_at_pos)
			_fov = lerpf(_fov, fov_goal, 1.0 - exp(-3.0 * delta))
			cam.fov = _fov
			return
		"dive":
			# Over the character's shoulder, looking down past it to the hay.
			var back := Basis(Vector3.UP, _target.rotation.y) * Vector3(0.0, 0.0, 1.0)
			cam_pos = tpos + back * 3.2 + Vector3.UP * 3.0
			look_at_pos = tpos + Vector3.DOWN * 3.5 - back * 2.0
			var cur := cam.global_position
			cam.global_position = cur.lerp(cam_pos, 1.0 - exp(-6.0 * delta)) if _shot_time > 0.05 else cam_pos
			cam.global_transform = Transform3D(Basis(), cam.global_position).looking_at(look_at_pos)
			_fov = lerpf(_fov, 74.0, 1.0 - exp(-2.0 * delta))
			cam.fov = _fov
			return
	var basis := Basis.from_euler(Vector3(p, yaw, 0.0))
	var right := basis.x
	var pivot := _pivot + right * shoulder * (1.0 - 0.5 * sprint)
	# Out of the walls: a sphere cast from the pivot back to the camera's place.
	var want := pivot + basis.z * _dist_goal
	var space := get_world_3d().direct_space_state
	var shape := SphereShape3D.new()
	shape.radius = 0.22
	var q := PhysicsShapeQueryParameters3D.new()
	q.shape = shape
	q.transform = Transform3D(Basis(), pivot)
	q.motion = want - pivot
	q.collision_mask = 1
	q.exclude = [_target.get_rid()] if _target is CollisionObject3D else []
	var hit := space.cast_motion(q)
	var free := _dist_goal * hit[0]
	if free < _dist:
		_dist = lerpf(_dist, free, 1.0 - exp(-25.0 * delta))
	else:
		_dist = lerpf(_dist, free, 1.0 - exp(-2.2 * delta))
	cam_pos = pivot + basis.z * maxf(_dist, 0.4)
	var shake_off := Vector3()
	if _shake > 0.0:
		_shake = maxf(0.0, _shake - delta * 2.5)
		var tt := Time.get_ticks_msec() * 0.001
		shake_off = Vector3(sin(tt * 37.0), sin(tt * 45.0 + 1.3), 0.0) * _shake * 0.05
	cam.global_transform = Transform3D(basis, cam_pos + basis * shake_off)
	_fov = lerpf(_fov, fov_goal, 1.0 - exp(-3.0 * delta))
	cam.fov = _fov
