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
		"hang_reach_l": 0.75, "hang_reach_r": 0.75, "hang_leap_up": 0.6, "hang_leap_l": 0.65, "hang_leap_r": 0.65,
		"hang_across_l": 0.8, "hang_across_r": 0.8, "jump_wall": 0.9}
# Milestone 192: the one-shots' lengths (their mocap clips' where those are played); the gaits' speeds (m/s) and cycles (s)
# (the blend points of the locomotion's gait).
var _one_shots := ONE_SHOTS.duplicate()
var _gaits: Array = [[1.6, 0.95], [3.0, 0.8], [4.4, 0.7], [7.0, 0.62]]
var _mocap := false
const MOCAP_DIR := "res://rooftops/mocap"
const MOCAP_GAITS := ["gait_walk", "gait_jog", "gait_run", "gait_sprint"]
# Milestone 195 (the owner, playing): the mannequin's own clips kept for these — Mixamo's "Falling Idle" is a skydiver's,
# face down (the character fell on its face after every jump and turned upright on landing). Milestone 196: the hangs
# and the moves from hold to hold Mixamo's again (milestone 195 had kept the mannequin's: Mixamo's hang had bobbed — a
# loop cut from a catch —, its moves fought the IK); the climb up a free wall (ladders too) the mannequin's.
# Mixamo's climb up from a hang ("Braced Hang To Crouch") and vault are retargeted with their root motion but not played
# yet: over CasaCrema's eave (a lip 0.5 m out, a roof's slope) the climb-up put the head, an arm and a shin through it in
# half the mantle's frames (the mannequin's own: none) — it climbs a plain ledge.
const MOCAP_KEEP_OWN := ["fall", "climb_move", "hang_climb", "vault"]
# Milestone 196: between holds a hand off its hold is reached for by this share of the IK (FellowTraversalPose3D's
# hand_flight_weight): Mixamo's arm carries it, the IK puts it on the hold as it takes it.
const MOCAP_HAND_FLIGHT := 0.3
# Milestone 196: Mixamo's shimmy goes this fast along the ledge (m/s, at the mannequin's scale); played at the body's pace.
const SHIMMY_CLIP_SPEED := 0.45
const MOCAP_SHIMMY_SPEED := 0.6
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
@export var crawl_speed := 0.65 ## up a roof steeper than CRAWL_SLOPE, slowly (walking, or `want_crawl`): on hands and feet
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
var want_crawl := false ## the demo: up a roof on hands and feet whatever the speed asked
var state := ""
var _one_shot := 0.0
var _air_top := 0.0
var _camera: Node3D
var _viewpoint: Node3D
var _last_pos := Vector3()
var _climb_speed := 0.0
var _dive_time := 0.0
var _land_kind := ""
var _crawling := false
var _edge_push := 0.0 ## milestone 191: how long the body has been pushing at an edge the guard holds it at
## Milestone 191 (the owner: the control "not satisfying"): a jump pressed a moment before landing is kept until then
## (JUMP_BUFFER), and one pressed a moment after walking off an edge still jumps (COYOTE_TIME).
const JUMP_BUFFER := 0.15
const COYOTE_TIME := 0.12
const FALL_GRAVITY := 1.45
var _jump_buffer := 0.0
var _since_floor := 0.0
var _walked_off := false
var _shot_name := ""


func _ready() -> void:
	_ensure_actions()
	_camera = get_node_or_null(camera_path)
	_viewpoint = get_node_or_null(viewpoint_path)
	facing = rotation.y
	_use_mocap()
	tree.active = true
	_set_state("loco")
	_last_pos = global_position
	traversal.connect("climb_jumped", func(_d): _play("climb_jump"))
	traversal.connect("hang_transfer_started", _on_transfer)


## Milestone 192: Mixamo's motion-captured clips (tools/build_mocap_clips.gd's library, not in the repository: Mixamo's
## licence lets a game use them, not hand them out) in place of the mannequin's own of the same names where it exists —
## the gaits' blend points at the speeds their strides were taken at, their cycles their own.
func _use_mocap() -> void:
	var path := MOCAP_DIR.path_join("mocap_clips.res")
	if not ResourceLoader.exists(path):
		return
	var mocap := load(path) as AnimationLibrary
	var ap := tree.get_node(tree.anim_player) as AnimationPlayer
	if mocap == null or ap == null:
		return
	# The player's own copy of the library (the crowd's NPCs share the mannequin's).
	var lib := ap.get_animation_library(&"").duplicate() as AnimationLibrary
	# (tools: `-- --mocap_all=1` plays every clip of the library, these too)
	var keep: Array = [] if "--mocap_all=1" in OS.get_cmdline_user_args() else MOCAP_KEEP_OWN
	for a in mocap.get_animation_list():
		if String(a) in keep:
			continue
		if lib.has_animation(a):
			lib.remove_animation(a)
		lib.add_animation(a, mocap.get_animation(a))
	ap.remove_animation_library(&"")
	ap.add_animation_library(&"", lib)
	_mocap = true
	# Milestone 194: a move from hold to hold plays its clip over the move's time, from the clip's own length.
	for a in mocap.get_animation_list():
		if String(a).begins_with("hang_") and _one_shots.has(String(a)) and not String(a) in keep:
			_one_shots[String(a)] = mocap.get_animation(a).length
	_mocap_tree(mocap, keep)
	var pose := get_node_or_null("Model/Mannequin/Skeleton3D/TraversalPose")
	if pose:
		var flight := MOCAP_HAND_FLIGHT
		for arg in OS.get_cmdline_user_args():
			if arg.begins_with("--hand_flight="): # (tools)
				flight = float(arg.get_slice("=", 1))
		pose.set("hand_flight_weight", flight)
		pose.set("hand_slide", 1.0) # (a shimmy's hands step as Mixamo's do, on the edge)
	# (the shimmy along a ledge near the pace Mixamo's was captured at: 1 m/s had played it at twice its rate)
	traversal.set("shimmy_speed", MOCAP_SHIMMY_SPEED)
	# Milestone 196: the traversal's own clips with root motion — up onto a ledge from a hang, over an obstacle — Mixamo's
	# where the library has them (their root's travel the body's path, scaled onto the ledge).
	for slot in [["hang_clip", &"hang_climb"], ["vault_clip", &"vault"]]:
		if mocap.has_animation(slot[1]) and not String(slot[1]) in keep:
			traversal.set(slot[0], mocap.get_animation(slot[1]))
	var info = JSON.parse_string(FileAccess.get_file_as_string(MOCAP_DIR.path_join("clips.json")))
	var gaits: Dictionary = info.get("gaits", {}) if info is Dictionary else {}
	var table: Array = []
	for g in MOCAP_GAITS:
		if gaits.has(g):
			table.append([float(gaits[g]["speed"]), float(gaits[g]["period"]), g])
	table.sort_custom(func(x, y): return x[0] < y[0])
	if table.size() == MOCAP_GAITS.size() and table[0][0] > 0.5:
		var gait := ((tree.tree_root as AnimationNodeBlendTree).get_node(&"loco") as AnimationNodeBlendTree).get_node(&"gait") as AnimationNodeBlendSpace1D
		gait.max_space = maxf(gait.max_space, table[table.size() - 1][0] + 1.0)
		_gaits = []
		for row in table:
			for i in gait.get_blend_point_count():
				if (gait.get_blend_point_node(i) as AnimationNodeAnimation).animation == StringName(row[2]):
					gait.set_blend_point_position(i, row[0])
			_gaits.append([row[0], row[1]])
	print("rooftops_player: mocap clips ", mocap.get_animation_list().size(), " gaits ", _gaits)


## Milestone 196: the player's own copy of the animation tree, with the mocap's catch of a hold (a state of its own) and
## the hang's blend played at a rate (ts_hang: the shimmy at the body's pace along the ledge).
func _mocap_tree(mocap: AnimationLibrary, keep: Array) -> void:
	var bt := (tree.tree_root as AnimationNodeBlendTree).duplicate(true) as AnimationNodeBlendTree
	var tr := bt.get_node(&"state") as AnimationNodeTransition
	if mocap.has_animation(&"hang_catch") and not "hang_catch" in keep and not bt.has_node(&"hang_catch"):
		tr.add_input("hang_catch")
		var i := tr.get_input_count() - 1
		tr.set_input_reset(i, true)
		var a := AnimationNodeAnimation.new()
		a.animation = &"hang_catch"
		bt.add_node(&"hang_catch", a, Vector2(0, 3000))
		bt.connect_node(&"state", i, &"hang_catch")
		_one_shots["hang_catch"] = mocap.get_animation(&"hang_catch").length
	if not bt.has_node(&"ts_hang"):
		for i in tr.get_input_count():
			if tr.get_input_name(i) == "hang":
				bt.disconnect_node(&"state", i)
				bt.add_node(&"ts_hang", AnimationNodeTimeScale.new(), Vector2(200, 200))
				bt.connect_node(&"ts_hang", 0, &"hang")
				bt.connect_node(&"state", i, &"ts_hang")
	tree.tree_root = bt
	traversal.connect("hang_started", _on_hang_started)


## Milestone 196: a hold caught from the air (a jump at a wall, a leap): the mocap's catch, the body's weight taken.
func _on_hang_started(_ledge: Dictionary) -> void:
	if _one_shots.has("hang_catch") and mode == Mode.AIR:
		_play("hang_catch")


## Milestone 192: a state's clip swapped for a variant of it where the library has one (the mocap's jump from a run, its
## hard landing), its own otherwise.
func _variant(p_node: StringName, p_clip: StringName) -> void:
	if not _mocap:
		return
	var n := (tree.tree_root as AnimationNodeBlendTree).get_node(p_node) as AnimationNodeAnimation
	var ap := tree.get_node(tree.anim_player) as AnimationPlayer
	var want := p_clip if ap.has_animation(p_clip) else p_node
	if n and n.animation != want:
		n.animation = want


## Milestone 194: a blend space's point's clip swapped for a variant of it where the library has one.
func _variant_point(p_space: StringName, p_point: int, p_clip: StringName) -> void:
	var space := (tree.tree_root as AnimationNodeBlendTree).get_node(p_space) as AnimationNodeBlendSpace1D
	var ap := tree.get_node(tree.anim_player) as AnimationPlayer
	if space == null or p_point >= space.get_blend_point_count() or not ap.has_animation(p_clip):
		return
	var n := space.get_blend_point_node(p_point) as AnimationNodeAnimation
	if n and n.animation != p_clip:
		n.animation = p_clip


## A move from hold to hold: its clip (the hands and feet are put on the stone by the pose's IK over it).
func _on_transfer(_ledge: Dictionary, how: int, duration: float, lead: int) -> void:
	var side := "l" if lead == 0 else "r"
	var clip := ""
	match how:
		TRANSFER_REACH_UP:
			clip = "hang_reach_" + side
		TRANSFER_REACH_SIDE:
			clip = "hang_across_" + side
		TRANSFER_JUMP_UP:
			clip = "hang_leap_up"
		TRANSFER_JUMP_ALONG, TRANSFER_JUMP_DIAGONAL, TRANSFER_JUMP_CORNER, TRANSFER_JUMP_BACK:
			clip = "hang_leap_" + side
	if clip.is_empty():
		return
	# Milestone 153: the clip played over the move's time (its wind-up included).
	var length: float = _one_shots[clip]
	tree.set("parameters/ts_%s/scale" % clip, length / maxf(duration, 0.05))
	_play(clip)
	_one_shot = duration


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
	if Input.is_action_just_pressed("jump"):
		_jump_buffer = JUMP_BUFFER
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
	_crawling = mode == Mode.GROUND and _crawl_wanted()
	if _crawling:
		speed_goal = crawl_speed
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
	if mode != Mode.DIVE and mode != Mode.HAY and mode != Mode.PERCH:
		_lean_model(Vector2(velocity.x, velocity.z).length() if mode == Mode.GROUND else 0.0, delta)
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
		# Milestone 191: turning back at speed brakes hard first (a plant and a turn, not a slow arc backwards).
		if h.length() > 2.5 and goal.length() > 0.5 and h.normalized().dot(goal.normalized()) < -0.3:
			rate = deceleration * 1.6
		h = h.move_toward(goal, rate * delta)
	elif mode == Mode.ROLL:
		h = h.move_toward(goal.normalized() * maxf(h.length() * 0.98, 3.0) if goal.length() > 0.1 else h * 0.97, 8.0 * delta)
	else:
		h = h.move_toward(goal.limit_length(maxf(h.length(), 2.0)), air_control * delta)
	# Milestone 154: walking or running (not free-running), the body does not go over a drop — the part of its speed
	# that would carry it over an edge is taken away and it slides along the edge (Assassin's Creed's low profile).
	# Milestone 191 (the owner: off the fountain only with a jump): a drop under EDGE_DROP is walked off; at a deeper one
	# the body goes over after pushing at it for EDGE_COMMIT, or at once with the drop key.
	var over_edge := false
	if on_floor and mode == Mode.GROUND and not want_sprint and h.length() > 0.05:
		var held := _edge_guard(h)
		if held.length() < h.length() * 0.5 and goal.length() > speed_goal * 0.5:
			_edge_push += delta
		else:
			_edge_push = 0.0
		if want_drop or _edge_push >= EDGE_COMMIT:
			over_edge = held.length() < h.length() * 0.5
		else:
			h = held
	else:
		_edge_push = 0.0
	velocity.x = h.x
	velocity.z = h.z
	if not on_floor:
		# Milestone 191: falling (past a jump's top, or off an edge) pulls harder than rising — a jump's arc snappier,
		# a fall less floaty (as games do); not a jump at a wall, which climbs to its hold.
		var g := gravity * (FALL_GRAVITY if velocity.y < 0.0 and state != "jump_wall" else 1.0)
		velocity.y -= g * delta
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
	_jump_buffer = maxf(_jump_buffer - delta, 0.0)
	_since_floor = 0.0 if on_floor else _since_floor + delta
	var jump_asked := want_jump or _jump_buffer > 0.0
	var can_jump := on_floor or (mode == Mode.AIR and _walked_off and _since_floor < COYOTE_TIME and velocity.y <= 0.5)
	# A landing's settle can be cut short by the next jump (not a roll).
	var settled: bool = _one_shot <= 0.0 or (_shot_name == "land" and _one_shot < float(_one_shots["land"]) - 0.1)
	if jump_asked and can_jump and settled:
		_jump_buffer = 0.0
		_walked_off = false
		velocity.y = jump_velocity
		mode = Mode.AIR
		_air_top = global_position.y
		# Milestone 153: at a wall, the arms reaching up for the hold. Milestone 192: from a run, the mocap's running jump.
		_variant(&"jump", &"jump_run" if Vector2(velocity.x, velocity.z).length() > 3.0 else &"jump")
		_play("jump_wall" if want_sprint and _wall_ahead(1.1) else "jump")
	if want_drop and on_floor and h.length() > 2.0 and mode == Mode.GROUND and not over_edge:
		mode = Mode.ROLL
		_play("roll")
	move_and_slide()
	on_floor = is_on_floor()
	match mode:
		Mode.GROUND:
			if not on_floor:
				mode = Mode.AIR
				_air_top = global_position.y
				_walked_off = velocity.y <= 0.5 # off an edge, not a jump
			elif _one_shot <= 0.0:
				_set_state("crawl" if _crawling else "loco")
		Mode.AIR:
			_air_top = maxf(_air_top, global_position.y)
			if on_floor:
				var fall := _air_top - global_position.y
				mode = Mode.GROUND
				if fall > roll_height and h.length() > 2.0:
					mode = Mode.ROLL
					_play("roll")
				elif fall > 1.2:
					_variant(&"land", &"land_hard" if fall > 3.0 else &"land")
					_play("land")
					if _mocap and fall > 3.0:
						_one_shot = maxf(_one_shot, 0.9) # the mocap's hard landing: down on a hand, up again
				elif _one_shot <= 0.0:
					_set_state("loco")
			elif _one_shot <= 0.0 and (velocity.y < 1.0 or state != "jump") and not (state == "jump_wall" and velocity.y > -3.0):
				_set_state("fall")
		Mode.ROLL:
			if _one_shot <= 0.0:
				mode = Mode.GROUND
				_set_state("loco")
	var hs := Vector2(velocity.x, velocity.z).length() if on_floor else 0.0
	_loco(hs, delta)
	tree.set("parameters/ts_crawl/scale", clampf(hs / CRAWL_CLIP_SPEED, 0.0, 2.0))


## Milestone 156 (the owner's reference video: over the gutter and up the tiles on hands and feet): on a roof steeper
## than CRAWL_SLOPE, going up it (within 40 degrees of straight up) slowly — walking, or the demo's `want_crawl` — the
## body scrambles on hands and feet (the clip "crawl", made for a roof of 21 degrees, played at the rate of the speed).
const CRAWL_SLOPE := 15.0
const CRAWL_CLIP_SPEED := 0.55 # m/s, mannequin_anims.py's CRAWL_V

func _crawl_wanted() -> bool:
	if not is_on_floor() or not (want_crawl or want_walk) or wish.length() < 0.3:
		return false
	var n := get_floor_normal()
	if n.angle_to(Vector3.UP) < deg_to_rad(CRAWL_SLOPE):
		return false
	var uphill := Vector3(-n.x, 0.0, -n.z).normalized()
	return wish.normalized().dot(uphill) > cos(deg_to_rad(40.0))


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
		var side := wish.dot(global_basis.x)
		var along := absf(side)
		tree.set("parameters/hang/blend_position", clampf(along * 1.5, 0.0, 1.0))
		# Milestone 194: the mocap's shimmy has a side (the model faces the body's −Z: its left is the body's −X).
		if _mocap and along > 0.1:
			_variant_point(&"hang", 1, &"hang_shimmy_l" if side < 0.0 else &"hang_shimmy_r")
		if _mocap:
			# Milestone 196: the shimmy at the body's pace along the ledge (the hang at rest at its own).
			var pace := clampf(_climb_speed / SHIMMY_CLIP_SPEED, 0.4, 2.5) if along > 0.1 else 1.0
			tree.set("parameters/ts_hang/scale", lerpf(1.0, pace, clampf(along * 1.5, 0.0, 1.0)))
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
		_loco(0.0)
		_set_state("loco")
	_air_top = global_position.y


## Milestone 154: walking or running (not sprinting), the body does not step off an edge. Where the ground ends within
## EDGE_LOOK the way h goes (a drop of more than EDGE_DROP), the edge's way out is found (the ways round that half of the
## body with no ground) and h loses its part along it: the body stops at the edge, or goes along it if h was slanting.
## Only when the way h goes has no ground: a beam or a ridge is walked along, whatever lies either side of it.
const EDGE_LOOK := 0.42
const EDGE_DROP := 1.3 # milestone 191: 0.7 had held the body on the fountain's rim (0.75 m)
const EDGE_COMMIT := 0.35 # s

func _edge_guard(h: Vector3) -> Vector3:
	var dir := Vector3(h.x, 0.0, h.z).normalized()
	if _ground_at(global_position + dir * EDGE_LOOK):
		return h
	var out := dir
	for i in 12:
		var d := Vector3(sin(TAU * i / 12.0), 0.0, cos(TAU * i / 12.0))
		if d.dot(dir) > 0.01 and not _ground_at(global_position + d * EDGE_LOOK):
			out += d
	out = out.normalized()
	var slide := h - out * maxf(h.dot(out), 0.0)
	if slide.length() > 0.05 and _ground_at(global_position + slide.normalized() * EDGE_LOOK):
		return slide
	return Vector3()


func _ground_at(p: Vector3) -> bool:
	var q := PhysicsRayQueryParameters3D.create(p + Vector3.UP * 0.5, p + Vector3.DOWN * EDGE_DROP, collision_mask, [get_rid()])
	var hit := get_world_3d().direct_space_state.intersect_ray(q)
	return not hit.is_empty() and (hit.normal as Vector3).y >= 0.5


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
	reset_physics_interpolation()
	# The way out of the hay is the clip's root motion, read at the physics ticks: the tree advanced with them meanwhile.
	tree.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_PHYSICS
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
		tree.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_IDLE
		_set_state("loco")


# ------------------------------------------------------------------ the animation tree

## Milestone 154: the gaits' cycle (s) at a speed (m/s): walk 0.95 s at 1.6, jog 0.8 at 3.0 (milestone 155), run 0.7 at
## 4.4, sprint 0.62 at 7.0 (mannequin_anims.py's GAITS); slower than a walk, the walk's stride over a longer cycle.
## Milestone 192: from `_gaits` (the mocap's own speeds and cycles when it is played); faster than the fastest gait, its
## stride over a shorter cycle.
func _gait_cycle(v: float) -> float:
	var first: Array = _gaits[0]
	if v <= first[0]:
		return first[1] * first[0] / maxf(v, 0.25)
	for i in range(1, _gaits.size()):
		var a: Array = _gaits[i - 1]
		var b: Array = _gaits[i]
		if v <= b[0]:
			return lerpf(a[1], b[1], (v - a[0]) / (b[0] - a[0]))
	var last: Array = _gaits[_gaits.size() - 1]
	return last[1] if not _mocap else last[1] * last[0] / v


## The locomotion's blend for a ground speed: the gait (walk, run, sprint) at the cycle's rate, idle blended in below
## 0.6 m/s. Milestone 155: both eased (0.08 s and 0.15 s; with no delta, set at once) — a stop from a sprint took the
## pose to idle's within a tick (the joints 3.7 times their fastest in any gait).
var _loco_v := 0.0
var _loco_move := 0.0

func _loco(v: float, delta: float = 0.0) -> void:
	var moving := clampf(v / 0.6, 0.0, 1.0)
	_loco_v = lerpf(_loco_v, v, 1.0 - exp(-delta / 0.08)) if delta > 0.0 else v
	_loco_move = lerpf(_loco_move, moving, 1.0 - exp(-delta / 0.15)) if delta > 0.0 else moving
	tree.set("parameters/loco/gait/blend_position", clampf(_loco_v, _gaits[0][0], _gaits[_gaits.size() - 1][0]))
	tree.set("parameters/loco/rate/scale", 1.0 / _gait_cycle(_loco_v))
	tree.set("parameters/loco/move/blend_amount", _loco_move)


## Milestone 154: the model (not the capsule) tipped about its feet: into a turn by the turn's rate times the speed
## (up to 14 degrees), forwards as the speed grows and back as it falls (up to 8); eased.
var _lean := Vector2()
var _last_facing := 0.0
var _last_speed := 0.0

func _lean_model(v: float, delta: float) -> void:
	if delta <= 0.0 or model == null:
		return
	var turn := wrapf(facing - _last_facing, -PI, PI) / delta
	var accel := (v - _last_speed) / delta
	_last_facing = facing
	_last_speed = v
	var goal := Vector2(clampf(-turn * v * 0.018, -0.25, 0.25), clampf(accel * 0.012, -0.14, 0.14))
	if mode != Mode.GROUND or _one_shot > 0.0:
		goal = Vector2()
	_lean = _lean.lerp(goal, 1.0 - exp(-8.0 * delta))
	model.transform.basis = Basis(Vector3.UP, PI) * Basis(Vector3(0, 0, 1), _lean.x) * Basis(Vector3(1, 0, 0), _lean.y)


# ------------------------------------------------------------------ where the eyes go

## Milestone 154: the head turned to where the body is going (a LookAtModifier3D on the head bone, after the climbing's
## pose): on a wall, the traversal's look (the next hold, the way it shimmies, over the top); on the ground, the way the
## controller steers — ahead of the body through a turn (Assassin's Creed's head leading the turn). The way and the
## weight eased; drawn in the idle frames.
var _look_off := Vector3(0.0, -0.6, -4.0)
var _look_weight := 0.0
@onready var _look_target: Node3D = get_node_or_null("LookTarget")
@onready var _head_look: SkeletonModifier3D = get_node_or_null("Model/Mannequin/Skeleton3D/HeadLook")
## Milestone 155 (decision 0295): the feet held where they come down, on the ground (off it, and while the traversal
## poses the body, nothing to hold); eased in and out over 0.1 s.
@onready var _foot_lock: SkeletonModifier3D = get_node_or_null("Model/Mannequin/Skeleton3D/FootLock")
## Milestone 174 (decision 0316): the feet on the ground under them (the roofs' slopes, steps), with the lock's weight.
@onready var _foot_place: SkeletonModifier3D = get_node_or_null("Model/Mannequin/Skeleton3D/FootPlacement")

func _process(delta: float) -> void:
	var down: bool = mode == Mode.GROUND and is_on_floor() and not traversal.is_active() and not traversal.is_hanging() and not _crawling
	if _foot_lock:
		_foot_lock.influence = move_toward(_foot_lock.influence, 1.0 if down else 0.0, delta / 0.1)
	if _foot_place:
		_foot_place.influence = move_toward(_foot_place.influence, 1.0 if down else 0.0, delta / 0.1)
	if _look_target == null or _head_look == null:
		return
	var head := get_global_transform_interpolated().origin + Vector3.UP * 1.6
	var ahead := Vector3(-sin(facing), 0.0, -cos(facing))
	var goal := ahead * 4.0 + Vector3.DOWN * 0.6
	var weight := 0.0
	var hints: Dictionary = traversal.get_pose_targets()
	if hints.get("active", false) and float(hints.get("look_weight", 0.0)) > 0.01:
		goal = (hints["look"] as Vector3) - head
		weight = float(hints["look_weight"])
	elif mode == Mode.GROUND and _one_shot <= 0.0:
		var w := Vector3(wish.x, 0.0, wish.z)
		if w.length() > 0.2:
			goal = w.normalized() * 4.0 + Vector3.DOWN * 0.6
		weight = 0.85 if w.length() > 0.2 else 0.5
	if weight > 0.0:
		_look_off = _look_off.lerp(goal, 1.0 - exp(-9.0 * delta))
	_look_weight = lerpf(_look_weight, weight, 1.0 - exp(-6.0 * delta))
	_look_target.global_position = head + _look_off
	_head_look.influence = _look_weight


func _set_state(s: String) -> void:
	if s == state:
		return
	state = s
	tree.set("parameters/state/transition_request", s)


func _play(s: String) -> void:
	_shot_name = s
	state = ""
	_set_state(s)
	_one_shot = _one_shots.get(s, 0.0)
