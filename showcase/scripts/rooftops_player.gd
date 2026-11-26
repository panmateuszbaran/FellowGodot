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
# Mixamo's climb up from a hang ("Braced Hang To Crouch") is retargeted with its root motion but not played: over
# CasaCrema's eave (a lip 0.5 m out, a roof's slope) it put the head, an arm and a shin through it in half the mantle's
# frames (the mannequin's own: none), and onto a plain top it ends crouched (the stand-up a blend). Its vault is played.
# (milestone 206: and the jump at a wall — Mixamo's "Run To Start Climbing" reaches with one arm, its chest turned 37
# degrees from the stride: caught on the hold by both hands, the body rolled 25-34 degrees)
const MOCAP_KEEP_OWN := ["fall", "climb_move", "hang_climb", "jump_wall"]
# Milestone 196: between holds a hand off its hold is reached for by this share of the IK (FellowTraversalPose3D's
# hand_flight_weight): Mixamo's arm carries it, the IK puts it on the hold as it takes it.
const MOCAP_HAND_FLIGHT := 0.3
# Milestone 196: Mixamo's shimmy goes this fast along the ledge (m/s, at the mannequin's scale); played at the body's pace.
const SHIMMY_CLIP_SPEED := 0.45
const MOCAP_SHIMMY_SPEED := 0.6
## Milestone 200 (the owner: "the push up still looks unnatural"): a leap up flies under gravity (the traversal's
## leap_gravity: a dip, a push from rest to the speed that carries the body to the dead point, the flight, the catch at
## the dead point and the settle into the hang), Mixamo's hop played over its push, flight and settle.
const LEAP_GRAVITY := 9.8
## Milestone 201 (the owner: "why does the character jump very high, reach with one hand and fall onto the edge instead
## of jumping straight to it?"): a jump at a wall aimed at the hold above (FellowTraversal3D.aim_jump_at_hold) — as fast
## as brings the hands onto it at the jump's top (no faster than jump_velocity × AIM_MAX_SPEED: a hold higher than that
## caught on the way down as before), both hands reaching for it as the body rises, caught at the top; "jump_wall" (Mixamo's
## "Run To Start Climbing") played from its push off the ground (JUMP_WALL_PUSH s into it) at the takeoff to its hand on
## its hold (JUMP_WALL_SET) at the top, its limbs the catch's ("hang_catch" held at its first frame).
const AIM_JUMP := true
const AIM_MAX_SPEED := 1.05
const JUMP_WALL_PUSH := 0.13
# Milestone 203 (the owner: "why does the character jump face first into the wall"): free-running at a wall the run is
# braked (no harder than WALL_BRAKE_DECEL, m/s²) to WALL_RUN_SPEED at the takeoff (WALL_JUMP_AT from the wall); in the air
# the feet meet the stone and the legs take the rest of the speed (no harder than WALL_ABSORB) until the body is
# WALL_HANG_GAP from it — the sprint had flown on at 7 m/s and stopped against the stone in a frame (43 g), the face 6 cm
# from it. `--wall_brake=0` (tools): as before.
const WALL_RUN_SPEED := 4.0
const WALL_BRAKE_DECEL := 6.0
const WALL_BRAKE_RANGE := 5.0
const WALL_JUMP_AT := 0.95
const WALL_ABSORB := 22.0
const WALL_HANG_GAP := 0.5
const JUMP_WALL_SET := 0.55
# FellowTraversal3D's Transfer values.
const TRANSFER_JUMP_ALONG := 2
const TRANSFER_JUMP_UP := 3
const TRANSFER_JUMP_DIAGONAL := 6
const TRANSFER_JUMP_BACK := 7
const TRANSFER_JUMP_CORNER := 8
const TRANSFER_REACH_UP := 9
const TRANSFER_REACH_SIDE := 10
const TRANSFER_STEP_UP := 11 # milestone 203

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
var _warp: Array = [] # milestone 200: a leap's clip time warped onto its phases: [clip, length, duration, elapsed, phases]
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
## Milestone 198 (the owner: "the animations' blending is sometimes sudden and violent"): the states switch at once and
## FellowInertializer3D (before the IK's modifiers) blends from the pose the character had, carried on by its own
## velocity, to the new one's — not two animations' poses mixed over 0.2 s.
const INERTIA_TIME := 0.3
const INERTIA_JUMP_WALL := 0.45
var _inertia: Node = null
## Milestone 198 (the owner: "the character does not grip the edge with its hand, but somewhere in the air; the hands
## sometimes go through the edge"): a hand on a ledge held as a climber's (FellowTraversalPose3D.ledge_grip) — the palm
## against the face under the edge, the fingers over it on the top, the wrist out of the stone.
const LEDGE_GRIP := 1.0


const PLAYER_PERSON := "res://people/luca.glb" # (milestone 233)


func _ready() -> void:
	_ensure_actions()
	_camera = get_node_or_null(camera_path)
	_viewpoint = get_node_or_null(viewpoint_path)
	facing = rotation.y
	_use_mocap()
	_inertialize()
	_fall_tree()
	_limit_joints()
	var pose := get_node_or_null("Model/Mannequin/Skeleton3D/TraversalPose")
	if pose:
		var grip := LEDGE_GRIP
		for arg in OS.get_cmdline_user_args():
			if arg.begins_with("--ledge_grip="): # (tools)
				grip = float(arg.get_slice("=", 1))
			elif arg.begins_with("--max_joint_speed=") or arg.begins_with("--body_sway=") or arg.begins_with("--hang_fit="): # (tools)
				pose.set(arg.get_slice("=", 0).trim_prefix("--"), float(arg.get_slice("=", 1)))
		pose.set("ledge_grip", grip)
	# (tools, milestone 203: `--trav_set=name:value,...`, `--pose_set=...` set the traversal's or the pose's properties)
	for arg in OS.get_cmdline_user_args():
		for pre in ["--trav_set=", "--pose_set="]:
			if arg.begins_with(pre):
				var node: Node = traversal if pre == "--trav_set=" else pose
				for pair in arg.trim_prefix(pre).split(","):
					if node and pair.contains(":"):
						node.set(pair.get_slice(":", 0), float(pair.get_slice(":", 1)))
	tree.active = true
	_set_state("loco")
	_last_pos = global_position
	traversal.connect("climb_jumped", func(_d): _play("climb_jump"))
	traversal.connect("hang_transfer_started", _on_transfer)
	# Milestone 233: the player a person (FellowPerson over the mannequin, at the mannequin's height so that the hands
	# and feet land where the traversal puts them); --player_person=0: the mannequin.
	if not "--player_person=0" in OS.get_cmdline_user_args() and ResourceLoader.exists(PLAYER_PERSON):
		FellowPerson.dress(get_node("Model") as Node3D, load(PLAYER_PERSON) as PackedScene, null, true)


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
	var keep: Array = [] if "--mocap_all=1" in OS.get_cmdline_user_args() else MOCAP_KEEP_OWN.duplicate()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--mocap_keep="): # (tools, milestone 206: these clips the mannequin's own too)
			keep.append_array(arg.get_slice("=", 1).split(","))
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
	# Milestone 198 (the owner: "the jump up is not anatomical — it does not put energy into the leap, it just flies up"):
	# the traversal's leaps up and along timed as Mixamo's hops — the body where their hips went (the push, over the top,
	# the drop into the catch), the hands letting go and catching when theirs did.
	var clips_info: Dictionary = info.get("clips", {}) if info is Dictionary else {}
	for leap in [[0, "hang_leap_up"], [1, "hang_leap_l"]]:
		var l: Dictionary = clips_info.get(leap[1], {}).get("leap", {})
		if l.is_empty() or String(leap[1]) in keep or "--no_leap_timing=1" in OS.get_cmdline_user_args():
			continue
		var h: Array = l["hands"]
		traversal.call("set_leap_timing", leap[0], float(l["time"]), float(l.get("reach", 1.0)), PackedFloat32Array(l["progress"]),
				PackedFloat32Array(l["lift"]), Vector4(h[0], h[1], h[2], h[3]))
		if leap[0] == 0:
			var g := LEAP_GRAVITY
			for arg in OS.get_cmdline_user_args():
				if arg.begins_with("--leap_gravity="): # (tools; 0: milestone 198's leap)
					g = float(arg.get_slice("=", 1))
			traversal.set("leap_gravity", g)
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
	# Milestone 201: the jump at a wall's clip played from a point and at a rate (its hand on its hold at the aimed jump's
	# top).
	if bt.has_node(&"jump_wall") and not bt.has_node(&"ts_jump_wall"):
		for i in tr.get_input_count():
			if tr.get_input_name(i) == "jump_wall":
				bt.disconnect_node(&"state", i)
				bt.add_node(&"ts_jump_wall", AnimationNodeTimeScale.new(), Vector2(200, 2900))
				bt.add_node(&"seek_jump_wall", AnimationNodeTimeSeek.new(), Vector2(100, 2900))
				bt.connect_node(&"seek_jump_wall", 0, &"jump_wall")
				bt.connect_node(&"ts_jump_wall", 0, &"seek_jump_wall")
				bt.connect_node(&"state", i, &"ts_jump_wall")
				# (its limbs, at an aimed jump, the catch's first frame's — both arms up on the hold, the legs bent under:
				# the clip's arms are one up at a climbing wall's hold and one down at another, whose turn the arms' IK kept
				# as it lifted the hand to the hold, the forearm and the wrist twisting half round as the catch's clip came
				# in; its foot up the wall at the hips' height swung the knee across the body and back as the catch's
				# braced legs came in)
				if mocap.has_animation(&"hang_catch"):
					bt.disconnect_node(&"state", i)
					var catch := AnimationNodeAnimation.new()
					catch.animation = &"hang_catch"
					bt.add_node(&"jump_wall_catch", catch, Vector2(0, 3100))
					bt.add_node(&"hold_jump_wall_catch", AnimationNodeTimeScale.new(), Vector2(100, 3100))
					bt.connect_node(&"hold_jump_wall_catch", 0, &"jump_wall_catch")
					var arms := AnimationNodeBlend2.new()
					arms.filter_enabled = true
					var skel := get_node_or_null("Model/Mannequin/Skeleton3D") as Skeleton3D
					if skel:
						for b in skel.get_bone_count():
							var n := skel.get_bone_name(b)
							if n.begins_with("Left") or n.begins_with("Right"):
								arms.set_filter_path(NodePath("Mannequin/Skeleton3D:" + n), true)
					bt.add_node(&"jump_wall_arms", arms, Vector2(300, 2900))
					bt.connect_node(&"jump_wall_arms", 0, &"ts_jump_wall")
					bt.connect_node(&"jump_wall_arms", 1, &"hold_jump_wall_catch")
					bt.connect_node(&"state", i, &"jump_wall_arms")
	tree.tree_root = bt
	if bt.has_node(&"hold_jump_wall_catch"):
		tree.set("parameters/hold_jump_wall_catch/scale", 0.0)
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
		if _inertia:
			_inertia.call("blend", 0.2) # (the shimmy's other side: its clip swapped at once)


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
		TRANSFER_STEP_UP:
			# Milestone 203: standing up onto the hold — Mixamo's climb up a wall (a hand up, the other side's foot up) under
			# the traversal's hands and feet.
			clip = "hang_reach_" + side
		TRANSFER_JUMP_ALONG, TRANSFER_JUMP_DIAGONAL, TRANSFER_JUMP_CORNER, TRANSFER_JUMP_BACK:
			clip = "hang_leap_" + side
	if clip.is_empty():
		return
	# Milestone 153: the clip played over the move's time (its wind-up included).
	var length: float = _one_shots[clip]
	tree.set("parameters/ts_%s/scale" % clip, length / maxf(duration, 0.05))
	# Milestone 200: a leap in flight under gravity — the clip's push played over its dip and push, its flight over the
	# flight, the rest over the settle.
	_warp = []
	var phases: PackedFloat32Array = traversal.call("get_leap_phases") if how == TRANSFER_JUMP_UP else PackedFloat32Array()
	if phases.size() == 4:
		_warp = [clip, length, maxf(duration, 0.05), 0.0, phases]
		_warp_clip(0.0)
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


## Milestone 200: the leap's clip played at the rate of the phase the move is in (see _on_transfer).
func _warp_clip(delta: float) -> void:
	_warp[3] += delta
	var u: float = _warp[3] / _warp[2]
	var ph: PackedFloat32Array = _warp[4]
	var f := [0.0, ph[0], ph[2], 1.0]
	var m := [0.0, ph[1], ph[3], 1.0]
	var i := 0 if u < m[1] else (1 if u < m[2] else 2)
	tree.set("parameters/ts_%s/scale" % _warp[0], _warp[1] * (f[i + 1] - f[i]) / maxf(_warp[2] * (m[i + 1] - m[i]), 1e-3))
	if u >= 1.0:
		_warp = []


func _physics_process(delta: float) -> void:
	_read_input()
	if _one_shot > 0.0:
		_one_shot -= delta
	if not _warp.is_empty():
		_warp_clip(delta)
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
	# (milestone 205: the first step off a climb onto a top — not yet on its floor — at the ground's rates: at the air's,
	# the walk up the roof had started at full speed in a frame)
	var off_traverse := mode == Mode.TRAVERSE
	if mode == Mode.TRAVERSE:
		mode = Mode.GROUND if is_on_floor() else Mode.AIR
		facing = rotation.y
		_air_top = global_position.y
	var on_floor := is_on_floor() or (off_traverse and velocity.y <= 0.5)
	var h := Vector3(velocity.x, 0.0, velocity.z)
	var goal := wish.limit_length(1.0) * speed_goal
	var brake := AIM_JUMP and not "--wall_brake=0" in OS.get_cmdline_user_args()
	if brake and on_floor and mode == Mode.GROUND and want_sprint:
		# Milestone 203: a run at a wall braked for its jump.
		var d := _wall_distance(WALL_BRAKE_RANGE)
		if d < WALL_BRAKE_RANGE:
			goal = goal.limit_length(sqrt(WALL_RUN_SPEED * WALL_RUN_SPEED + 2.0 * WALL_BRAKE_DECEL * maxf(0.0, d - WALL_JUMP_AT)))
	if on_floor and mode != Mode.ROLL:
		var rate := acceleration if goal.length() > h.length() else deceleration
		# Milestone 205: a walk on a roof's slope starts and stops carefully (WALK_ACCEL: at the run's 16 and 22 m/s², the
		# body had rocked back at the ridge).
		if want_walk and h.length() <= walk_speed + 0.1 and goal.length() <= walk_speed + 0.1 and get_floor_normal().angle_to(Vector3.UP) > deg_to_rad(10.0):
			rate = minf(rate, WALK_ACCEL)
		# Milestone 191: turning back at speed brakes hard first (a plant and a turn, not a slow arc backwards).
		if h.length() > 2.5 and goal.length() > 0.5 and h.normalized().dot(goal.normalized()) < -0.3:
			rate = deceleration * 1.6
		h = h.move_toward(goal, rate * delta)
	elif mode == Mode.ROLL:
		h = h.move_toward(goal.normalized() * maxf(h.length() * 0.98, 3.0) if goal.length() > 0.1 else h * 0.97, 8.0 * delta)
	else:
		h = h.move_toward(goal.limit_length(maxf(h.length(), 2.0)), air_control * delta)
		if brake and state == "jump_wall":
			# Milestone 203: the feet on the stone, the legs taking the speed into it — at rest where the body hangs from the
			# hold by the time it is caught (as fast as stops it there, slowing evenly), never harder than WALL_ABSORB.
			var gap := _wall_distance(2.0)
			if gap < 2.0:
				h = h.limit_length(sqrt(2.0 * WALL_ABSORB * maxf(0.0, gap - WALL_HANG_GAP)))
			var aim: Dictionary = traversal.call("get_aim_state") if traversal.has_method("get_aim_state") else {}
			if not aim.is_empty():
				var inward: Vector3 = aim["inward"]
				var left := ((aim["hang_feet"] as Vector3) - global_position).dot(inward)
				var t_left := maxf(float(aim["catch_time"]) - float(aim["time"]), delta)
				h = h.limit_length(2.0 * maxf(0.0, left) / t_left)
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
	if want_sprint and on_floor and _one_shot <= 0.0 and mode == Mode.GROUND and _wall_ahead(_wall_jump_at()):
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
		var at_wall := want_sprint and _wall_ahead(maxf(1.1, _wall_jump_at()))
		var aimed := 0.0
		if at_wall and AIM_JUMP and not "--aim_jump=0" in OS.get_cmdline_user_args() and traversal.has_method("aim_jump_at_hold"):
			aimed = traversal.call("aim_jump_at_hold", Vector3(wish.x, 0.0, wish.z), gravity, jump_velocity * AIM_MAX_SPEED)
			if aimed > 0.0:
				velocity.y = aimed
		_play("jump_wall" if at_wall else "jump")
		if at_wall and _mocap:
			# (the clip's reach at the top; at its own rate where nothing was aimed at)
			var from := JUMP_WALL_PUSH if aimed > 0.0 else 0.0
			var rate := clampf((JUMP_WALL_SET - from) / (aimed / gravity), 0.5, 2.5) if aimed > 0.0 else 1.0
			tree.set("parameters/ts_jump_wall/scale", rate)
			tree.set("parameters/seek_jump_wall/seek_request", from)
			tree.set("parameters/jump_wall_arms/blend_amount", 1.0 if aimed > 0.0 else 0.0)
	if want_drop and on_floor and h.length() > 2.0 and mode == Mode.GROUND and not over_edge:
		mode = Mode.ROLL
		_play("roll")
	var before := velocity
	move_and_slide()
	_bump_people(before)
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
			if state == "fall":
				_fall_mix(delta)
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
## Milestone 205 (the owner: "walking on the roof needs improving"): only steeper than 28 degrees — the rooftops' tiles
## (21) are walked up, leaning into the slope (`ROOF_LEAN`), the feet placed on them.
const CRAWL_SLOPE := 28.0
const WALK_ACCEL := 4.0 # m/s²
const ROOF_LEAN := 0.6 # of the slope's angle, going straight up it
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
	# Milestone 207 (the owner: "after standing up on the roof the body does a strange jump"): a jump pressed while
	# hanging or climbing is the traversal's — kept in the buffer (counted down only on the ground), it had jumped the
	# body as soon as the climb onto the roof let go of it, 1.7 s later.
	_jump_buffer = 0.0
	if traversal.is_hanging() or traversal.is_transferring():
		# Free-running on: pushing on leaps to a hold out of reach (the traversal reaches to a near one by itself).
		var at_wall := wish.length() > 0.5
		if want_jump or (want_sprint and at_wall and not traversal.is_transferring()):
			traversal.hang_jump()
		elif want_drop:
			traversal.release()
		var side := wish.dot(global_basis.x)
		# (milestone 207: as the traversal's hang_side_dead_zone — pushing up, a little to the side is no shimmy)
		var dz := float(traversal.get("hang_side_dead_zone")) if "hang_side_dead_zone" in traversal else 0.0
		if dz > 0.0 and wish.dot(-global_basis.z) > 0.0 and absf(side) < dz * wish.length():
			side = 0.0
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
	var q := PhysicsRayQueryParameters3D.create(p + Vector3.UP * 0.5, p + Vector3.DOWN * EDGE_DROP, RAY_MASK, [get_rid()])
	var hit := get_world_3d().direct_space_state.intersect_ray(q)
	return not hit.is_empty() and (hit.normal as Vector3).y >= 0.5


## A wall in front of the chest within p_distance (the way the body faces and wishes to go).
## Milestone 203: the jump at a wall taken this far from it (the ray's reach less its 0.35 m): close enough for the legs to
## take the run's speed by the jump's top (1.1 m as before with `--wall_brake=0`).
func _wall_jump_at() -> float:
	return WALL_JUMP_AT - 0.35 if AIM_JUMP and not "--wall_brake=0" in OS.get_cmdline_user_args() else 1.1


## Milestone 203: how far the wall ahead (the way the body is asked to go) is from the body's middle (p_range when none).
func _wall_distance(p_range: float) -> float:
	var d := Vector3(wish.x, 0.0, wish.z)
	if d.length() < 0.5:
		d = -global_basis.z
	d = d.normalized()
	var from := global_position + Vector3.UP * 1.3
	var q := PhysicsRayQueryParameters3D.create(from, from + d * p_range, RAY_MASK, [get_rid()])
	var hit := get_world_3d().direct_space_state.intersect_ray(q)
	if hit.is_empty() or absf((hit.normal as Vector3).y) >= 0.3 or (hit.normal as Vector3).dot(-d) <= 0.7:
		return p_range
	return from.distance_to(hit.position)


func _wall_ahead(p_distance: float) -> bool:
	if wish.length() < 0.5:
		return false
	var d := Vector3(wish.x, 0.0, wish.z).normalized()
	var from := global_position + Vector3.UP * 1.3
	var q := PhysicsRayQueryParameters3D.create(from, from + d * (p_distance + 0.35), RAY_MASK, [get_rid()])
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
	# (milestone 210: straws thrown up where the body lands)
	get_tree().call_group(&"hay_fx", &"burst", global_position + Vector3.UP * 0.9)
	if _camera and _camera.has_method("release"):
		_camera.release()


func _hay(_delta: float) -> void:
	# The clip's root motion carries the body out of the cart and down beside it.
	var m: Vector3 = tree.get_root_motion_position()
	global_position += global_basis * (Basis(Vector3.UP, PI) * m)
	# Never below the street: the clip ends on the ground, wherever the cart stands.
	var space := get_world_3d().direct_space_state
	var q := PhysicsRayQueryParameters3D.create(global_position + Vector3.UP * 0.35, global_position + Vector3.DOWN * 0.4,
			RAY_MASK, [get_rid()])
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
	# (milestone 205: up a roof, into its slope — the more the straighter up it the body goes)
	if is_on_floor() and v > 0.2:
		var n := get_floor_normal()
		var uphill := Vector3(-n.x, 0.0, -n.z)
		if n.angle_to(Vector3.UP) > deg_to_rad(10.0): # (a roof's; the square's paving and its kerbs, none)
			var along := Vector3(velocity.x, 0.0, velocity.z).normalized().dot(uphill.normalized())
			goal.y += clampf(n.angle_to(Vector3.UP) * ROOF_LEAN * along, -0.2, 0.3)
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
	if _no_look: # (tools, milestone 205: `--head_look=0`)
		_head_look.influence = 0.0
		return
	var head := get_global_transform_interpolated().origin + Vector3.UP * 1.6
	var ahead := Vector3(-sin(facing), 0.0, -cos(facing))
	var goal := ahead * 4.0 + Vector3.DOWN * 0.6
	var weight := 0.0
	var hints: Dictionary = traversal.get_pose_targets()
	if hints.get("active", false) and float(hints.get("look_weight", 0.0)) > 0.01:
		# (milestone 205: from the head itself — hanging, the capsule's 1.6 m is not where it is — and no steeper than
		# LOOK_UP_MAX; the target placed back from the estimate's point, where the eased offset is added)
		var eyes := _head_at(head)
		var way := _look_way((hints["look"] as Vector3) - eyes, hints.get("inward", ahead))
		# (at least 3 m out along the way: a hold just over the head swung it 37 degrees in two frames as the body moved
		# under it)
		goal = way.normalized() * maxf(way.length(), 3.0) + (eyes - head)
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


## Milestone 205 (the owner: "the first jump bends the head in a strange way"): a look no steeper than LOOK_UP_MAX over
## the horizon, its way across the wall's (into it) as it steepens — a hold above the head says little about which way
## across: looking straight up at the next hold, the head had swung 40-60 degrees from side to side through a leap and
## tipped back to the neck's limit.
const LOOK_UP_MAX := 0.87 # radians (50 degrees)
var _no_look := "--head_look=0" in OS.get_cmdline_user_args()

func _head_at(p_default: Vector3) -> Vector3:
	var skel := _head_look.get_parent() as Skeleton3D if _head_look else null
	if skel == null:
		return p_default
	if _head_bone < 0:
		_head_bone = skel.find_bone("Head")
	return skel.global_transform * skel.get_bone_global_pose(_head_bone).origin if _head_bone >= 0 else p_default

var _head_bone := -1


func _look_way(g: Vector3, inward: Vector3) -> Vector3:
	var l := g.length()
	var flat := Vector3(g.x, 0.0, g.z)
	var into := Vector3(inward.x, 0.0, inward.z)
	if l < 0.01 or into.length() < 0.01:
		return g
	into = into.normalized()
	var way := flat.normalized() if flat.length() > 0.01 else into
	if way.dot(into) > -0.95:
		way = way.slerp(into, smoothstep(0.5, 0.9, clampf(g.y / l, 0.0, 1.0))).normalized()
	var up := minf(atan2(g.y, flat.length()), LOOK_UP_MAX)
	return (way * cos(up) + Vector3.UP * sin(up)) * l


func _set_state(s: String) -> void:
	if s == state:
		return
	state = s
	if s == "fall":
		_fall_time = 0.0
		_flail = 0.0
		tree.set("parameters/fall_mix/blend_amount", 0.0)
	tree.set("parameters/state/transition_request", s)
	if _inertia:
		# (milestone 206: out of the sprint into the jump at a wall over longer — the arms' swing up from the stride's,
		# the mannequin's own clip: at 0.3 s an upper arm turned at 2,240 °/s)
		_inertia.call("blend", INERTIA_JUMP_WALL if s == "jump_wall" else INERTIA_TIME)


## Milestone 208 (the owner: "more interaction with the NPCs — running into them, knocking someone over, not passing
## through them"): the square's people are capsules on LAYER_PEOPLE, which the body meets (its rays — the edge, the wall
## ahead, the ground — see the world alone: RAY_MASK). A person run into is told how fast (rooftops_crowd.gd's bump():
## a shove, a stagger, knocked down); the body loses speed to one staggered (BUMP_STAGGER_KEEP of it) or knocked down
## (BUMP_KNOCK_KEEP), going on through where they stood.
const LAYER_PEOPLE := 16
const RAY_MASK := 1
const BUMP_STAGGER_KEEP := 0.45
const BUMP_KNOCK_KEEP := 0.7
var _crowd: Node

func _bump_people(p_before: Vector3) -> void:
	for i in get_slide_collision_count():
		var c := get_slide_collision(i)
		var other := c.get_collider()
		if other == null or not (other is CollisionObject3D) or ((other as CollisionObject3D).collision_layer & LAYER_PEOPLE) == 0:
			continue
		if _crowd == null:
			_crowd = get_tree().get_first_node_in_group(&"crowd")
		if _crowd == null:
			return
		var r := int(_crowd.call("bump", other, p_before, c.get_normal()))
		if r == 2:
			velocity.x = p_before.x * BUMP_STAGGER_KEEP
			velocity.z = p_before.z * BUMP_STAGGER_KEEP
		elif r == 3:
			velocity.x = p_before.x * BUMP_KNOCK_KEEP
			velocity.z = p_before.z * BUMP_KNOCK_KEEP
		if r >= 2:
			return


## Milestone 207 (the owner: "the fall has a broken animation" — the body had fallen upright and stiff, the legs nearly
## straight, one arm out behind): a fall is the clip "fall" (in the air ready to land: the knees up, the arms out for
## balance, the eyes down) blended into "fall_flail" (the arms wheeling over the shoulders, the legs pedalling) as it
## lasts — from FALL_FLAIL_FROM to FALL_FLAIL_FULL seconds — and back to the first as the ground comes up (under
## FALL_READY seconds away at the speed it falls): fall_mix, a Blend2 in place of the state's clip.
const FALL_FLAIL_FROM := 0.45
const FALL_FLAIL_FULL := 1.1
const FALL_READY := 0.3
var _fall_time := 0.0
var _flail := 0.0

func _fall_tree() -> void:
	var ap := tree.get_node(tree.anim_player) as AnimationPlayer
	if ap == null or not ap.has_animation(&"fall_flail"):
		return
	var bt := (tree.tree_root as AnimationNodeBlendTree).duplicate(true) as AnimationNodeBlendTree
	var tr := bt.get_node(&"state") as AnimationNodeTransition
	if bt.has_node(&"fall_mix") or not bt.has_node(&"fall"):
		return
	for i in tr.get_input_count():
		if tr.get_input_name(i) == "fall":
			bt.disconnect_node(&"state", i)
			var flail := AnimationNodeAnimation.new()
			flail.animation = &"fall_flail"
			bt.add_node(&"fall_flail", flail, Vector2(0, 3300))
			bt.add_node(&"fall_mix", AnimationNodeBlend2.new(), Vector2(200, 3300))
			bt.connect_node(&"fall_mix", 0, &"fall")
			bt.connect_node(&"fall_mix", 1, &"fall_flail")
			bt.connect_node(&"state", i, &"fall_mix")
	tree.tree_root = bt
	tree.set("parameters/fall_mix/blend_amount", 0.0)


func _fall_mix(delta: float) -> void:
	_fall_time += delta
	var want := clampf((_fall_time - FALL_FLAIL_FROM) / (FALL_FLAIL_FULL - FALL_FLAIL_FROM), 0.0, 1.0)
	if want > 0.0 and velocity.y < 0.0:
		var reach := -velocity.y * FALL_READY + 0.2
		var q := PhysicsRayQueryParameters3D.create(global_position + Vector3.UP * 0.1, global_position + Vector3.DOWN * reach, RAY_MASK, [get_rid()])
		if not get_world_3d().direct_space_state.intersect_ray(q).is_empty():
			want = 0.0
	_flail = move_toward(_flail, want, delta / 0.25)
	tree.set("parameters/fall_mix/blend_amount", smoothstep(0.0, 1.0, _flail))


## Milestone 202 (the owner: "assess the movements' anatomical correctness"): the joints within a person's range of
## motion after everything else on the skeleton (FellowJointLimits3D, last among its modifiers) — the elbows and knees
## bent in their hinges' plane, the ankles, the wrists and the neck within a person's range. (tools: `--joint_limits=0`,
## `--hinge_alignment=0`, `--limit_ankles=0`, `--limit_wrists=0`, `--limit_neck=0`)
const JOINT_LIMITS := true

func _limit_joints() -> void:
	var skel := get_node_or_null("Model/Mannequin/Skeleton3D") as Skeleton3D
	var args := OS.get_cmdline_user_args()
	if skel == null or not JOINT_LIMITS or not ClassDB.class_exists(&"FellowJointLimits3D") or "--joint_limits=0" in args:
		return
	var limits: Node = ClassDB.instantiate(&"FellowJointLimits3D")
	limits.name = "JointLimits"
	# Milestone 203: a hinge turned into its bend however far round (a leg stepped up onto a hold at the hips' height had
	# its knee bent backwards past the 120 degrees milestone 202 turned).
	limits.set("hinge_max_turn", 175.0)
	# Milestone 204: the head tipped back to 66 degrees at most (the measure's limit 70: eased in at 720 degrees a second, a
	# look up to a hold 2.5 m over the hands had passed it by 1-3 degrees).
	limits.set("neck_flexion_range", Vector2(-66.0, 56.0))
	# Milestone 205 (the owner: "the first jump bends the head in a strange way"): the head tipped to the side at most 15
	# degrees off the chest (a person's 45 is a stretch: caught on the wall with the chest rolled 35 degrees, the head
	# held upright had bent the neck 31 degrees sideways).
	limits.set("neck_bend_limit", 15.0)
	for arg in args:
		for p in ["hinge_alignment", "limit_ankles", "limit_wrists", "limit_neck"]:
			if arg.begins_with("--%s=" % p): # (tools)
				limits.set(p, arg.get_slice("=", 1) != "0")
		if arg.begins_with("--max_correction_speed="): # (tools)
			limits.set("max_correction_speed", float(arg.get_slice("=", 1)))
		if arg.begins_with("--hinge_max_turn="): # (tools, milestone 203)
			limits.set("hinge_max_turn", float(arg.get_slice("=", 1)))
	skel.add_child(limits)


## Milestone 198: the player's own copy of the tree (where the mocap's has not made one), its states switched at once,
## an inertializer first among the skeleton's modifiers.
func _inertialize() -> void:
	var skel := get_node_or_null("Model/Mannequin/Skeleton3D") as Skeleton3D
	if skel == null or not ClassDB.class_exists(&"FellowInertializer3D") or "--no_inertia=1" in OS.get_cmdline_user_args():
		return
	if not _mocap:
		tree.tree_root = (tree.tree_root as AnimationNodeBlendTree).duplicate(true)
	var tr := (tree.tree_root as AnimationNodeBlendTree).get_node(&"state") as AnimationNodeTransition
	tr.xfade_time = 0.0
	_inertia = ClassDB.instantiate(&"FellowInertializer3D")
	_inertia.name = "Inertia"
	_inertia.set("blend_time", INERTIA_TIME)
	skel.add_child(_inertia)
	skel.move_child(_inertia, 0)


func _play(s: String) -> void:
	_shot_name = s
	state = ""
	_set_state(s)
	_one_shot = _one_shots.get(s, 0.0)
