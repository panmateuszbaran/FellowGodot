extends SceneTree
## FellowEngine milestone 75 check (decision 0183): FellowTraversal3D hanging from ledges and climbing FellowLadder3Ds.
##
##   godot --path benchmarks/project --headless --fixed-fps 60 -s res://bench/climb_check.gd -- --out=<dir>
##   godot --path benchmarks/project --fixed-fps 60 --resolution 1280x720 -s res://bench/climb_check.gd
##       -- --out=<dir> --pictures=1 [--frames=1] [--grid=1 --shot=640x540]
##
## Lanes, each a CharacterBody3D (a capsule 0.35 m by 1.8 m, a test mannequin posed by a FellowTraversalPose3D) driven
## by a program of the controller's wishes over time: jumping at a 2.5 m ledge with a wall rising behind it (it hangs),
## moving left along it to its end and letting go; jumping at a 2.3 m ledge with hang_first (it hangs, then climbs on
## pushing at the wall); hanging from the rim of a round tower and moving round it; walking up to a ladder against a 4 m
## platform, climbing it and stepping off onto the platform; walking off the platform onto the ladder's top, climbing
## down and stepping off at its foot; a ledge whose edge has a 0.8 m gap (it stops at the gap). For each: the actions
## started and finished, where the body ended, how far it moved along the ledge, the frames its capsule overlapped the
## world, the hands' and feet's distance to their targets. One JSON line: CLIMB_CHECK {...}; exit code 1 when a lane
## did otherwise than expected.
## Milestone 81 (decision 0189): round a block's outside corner, round an inside corner onto a wall meeting the ledge,
## a jump across a gap to the next ledge, a jump up to a beam over the ledge. Milestone 84 (decision 0192): walking
## over a platform's edge into a hang from it (then along it and letting go), and running over it (no hang). Milestone 86
## (decision 0194): from a hang down to a ledge below.

const KIND_NAMES := ["none", "step", "low", "high", "vault", "too_high", "blocked", "hang", "ladder"]
const RUN_SECONDS := 9.0

var args := {}
var out_dir := ""
var result := {}
var root3d: Node3D
var lanes: Array = []
var frame := 0
var pictures := false
var save_frames := false
var grid := false
var shot_size := Vector2i(480, 360)
var strip_times := [0.5, 1.5, 3.0, 4.5, 6.5, 8.5]
var shots := {}
var viewports := {}
var probe_shape: CapsuleShape3D
var controller_script: GDScript
var done := false


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://climb_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	pictures = _arg("pictures", "0") == "1"
	save_frames = _arg("frames", "0") == "1"
	grid = _arg("grid", "0") == "1"
	var ss: PackedStringArray = _arg("shot", "480x360").split("x")
	shot_size = Vector2i(int(ss[0]), int(ss[1]))
	_build.call_deferred()


func _make_scripts() -> void:
	controller_script = GDScript.new()
	controller_script.source_code = """extends CharacterBody3D
# A program of the controller's wishes: [seconds, {intent, speed, jump, call}] in order.
var program := []
var traversal
var t := 0.0
var intent := Vector3()
var speed := 0.0
var next := 0

func _physics_process(delta: float) -> void:
	t += delta
	while next < program.size() and float(program[next][0]) <= t:
		var cmd: Dictionary = program[next][1]
		if cmd.has("intent"):
			intent = cmd.intent
		if cmd.has("speed"):
			speed = cmd.speed
		if cmd.has("jump") and is_on_floor() and not traversal.is_active():
			velocity.y = cmd.jump
		if cmd.has("call"):
			traversal.call(cmd.call)
		next += 1
	traversal.set_intent(intent)
	if traversal.is_active():
		return
	if not is_on_floor():
		velocity.y -= 9.8 * delta
	velocity.x = intent.x * speed
	velocity.z = intent.z * speed
	move_and_slide()
"""
	controller_script.reload()


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.85
	return m


func _box(pos: Vector3, size: Vector3, color := Color(0.72, 0.7, 0.66)) -> StaticBody3D:
	var b := StaticBody3D.new()
	b.position = pos
	var cs := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = size
	cs.shape = shape
	b.add_child(cs)
	var mi := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	mi.mesh = mesh
	mi.material_override = _material(color)
	b.add_child(mi)
	root3d.add_child(b)
	return b


func _cylinder(pos: Vector3, radius: float, height: float, color := Color(0.72, 0.7, 0.66)) -> StaticBody3D:
	var b := StaticBody3D.new()
	b.position = pos
	var cs := CollisionShape3D.new()
	var shape := CylinderShape3D.new()
	shape.radius = radius
	shape.height = height
	cs.shape = shape
	b.add_child(cs)
	var mi := MeshInstance3D.new()
	var mesh := CylinderMesh.new()
	mesh.top_radius = radius
	mesh.bottom_radius = radius
	mesh.height = height
	mesh.radial_segments = 48
	mi.mesh = mesh
	mi.material_override = _material(color)
	b.add_child(mi)
	root3d.add_child(b)
	return b


# A ladder: the FellowLadder3D and a mesh for it (two rails and the rungs).
func _ladder(pos: Vector3, height: float) -> FellowLadder3D:
	var l := FellowLadder3D.new()
	l.height = height
	l.position = pos
	root3d.add_child(l)
	var mat := _material(Color(0.5, 0.36, 0.22))
	for x in [-0.25, 0.25]:
		var rail := MeshInstance3D.new()
		var rm := BoxMesh.new()
		rm.size = Vector3(0.05, height, 0.05)
		rail.mesh = rm
		rail.material_override = mat
		rail.position = Vector3(x, height * 0.5, 0)
		l.add_child(rail)
	var y := l.rung_offset
	while y <= height:
		var rung := MeshInstance3D.new()
		var cm := BoxMesh.new()
		cm.size = Vector3(0.5, 0.035, 0.035)
		rung.mesh = cm
		rung.material_override = mat
		rung.position = Vector3(0, y, 0)
		l.add_child(rung)
		y += l.rung_spacing
	return l


func _bone(sk: Skeleton3D, name: String, parent: String, offset: Vector3) -> void:
	var i := sk.add_bone(name)
	if parent != "":
		sk.set_bone_parent(i, sk.find_bone(parent))
	sk.set_bone_rest(i, Transform3D(Basis(), offset))
	sk.set_bone_pose_position(i, offset)


# A test mannequin (Godot's humanoid names, facing -Z, rest rotations identity): capsules along its bones.
func _mannequin(body: Node3D, color: Color) -> Skeleton3D:
	var sk := Skeleton3D.new()
	sk.name = "Skeleton3D"
	_bone(sk, "Hips", "", Vector3(0, 0.95, 0))
	_bone(sk, "Spine", "Hips", Vector3(0, 0.12, 0))
	_bone(sk, "Chest", "Spine", Vector3(0, 0.2, 0))
	_bone(sk, "Neck", "Chest", Vector3(0, 0.22, 0))
	_bone(sk, "Head", "Neck", Vector3(0, 0.08, 0))
	for side in [["Left", -1.0], ["Right", 1.0]]:
		var s: String = side[0]
		var x: float = side[1]
		_bone(sk, s + "UpperArm", "Chest", Vector3(0.19 * x, 0.18, 0))
		_bone(sk, s + "LowerArm", s + "UpperArm", Vector3(0.04 * x, -0.28, 0.02))
		_bone(sk, s + "Hand", s + "LowerArm", Vector3(0.01 * x, -0.26, -0.02))
		_bone(sk, s + "UpperLeg", "Hips", Vector3(0.1 * x, -0.06, 0))
		_bone(sk, s + "LowerLeg", s + "UpperLeg", Vector3(0, -0.42, 0.01))
		_bone(sk, s + "Foot", s + "LowerLeg", Vector3(0, -0.39, 0.02))
		_bone(sk, s + "Toes", s + "Foot", Vector3(0, -0.06, -0.13))
	body.add_child(sk)
	var mat := _material(color)
	var segments := [["Hips", "Spine", 0.13], ["Spine", "Chest", 0.14], ["Chest", "Neck", 0.15], ["Neck", "Head", 0.05]]
	for s in ["Left", "Right"]:
		segments.append([s + "UpperArm", s + "LowerArm", 0.05])
		segments.append([s + "LowerArm", s + "Hand", 0.045])
		segments.append([s + "UpperLeg", s + "LowerLeg", 0.07])
		segments.append([s + "LowerLeg", s + "Foot", 0.055])
		segments.append([s + "Foot", s + "Toes", 0.045])
	for seg in segments:
		var a := BoneAttachment3D.new()
		a.bone_name = seg[0]
		sk.add_child(a)
		var off: Vector3 = sk.get_bone_rest(sk.find_bone(seg[1])).origin
		var mi := MeshInstance3D.new()
		var cap := CapsuleMesh.new()
		cap.radius = seg[2]
		cap.height = off.length() + 2.0 * seg[2]
		cap.radial_segments = 12
		cap.rings = 4
		mi.mesh = cap
		mi.material_override = mat
		mi.transform = Transform3D(Basis(Quaternion(Vector3.UP, off.normalized())), off * 0.5)
		a.add_child(mi)
	for s in ["Head", "LeftHand", "RightHand"]:
		var a := BoneAttachment3D.new()
		a.bone_name = s
		sk.add_child(a)
		var mi := MeshInstance3D.new()
		var sph := SphereMesh.new()
		sph.radius = 0.11 if s == "Head" else 0.055
		sph.height = sph.radius * 2.0
		mi.mesh = sph
		mi.material_override = mat
		mi.position = Vector3(0, 0.1, 0) if s == "Head" else Vector3(0, -0.05, 0)
		a.add_child(mi)
	return sk


func _lane(name: String, start: Vector3, program: Array) -> Dictionary:
	var body := CharacterBody3D.new()
	body.name = "Climber_" + name
	body.set_script(controller_script)
	body.position = start
	var cs := CollisionShape3D.new()
	var cap := CapsuleShape3D.new()
	cap.radius = 0.35
	cap.height = 1.8
	cs.shape = cap
	cs.position = Vector3(0, 0.9, 0)
	body.add_child(cs)
	var t := FellowTraversal3D.new()
	t.name = "Traversal"
	body.add_child(t)
	var sk := _mannequin(body, Color(0.25, 0.45, 0.8))
	var pose := FellowTraversalPose3D.new()
	pose.blend_time = 0.0
	sk.add_child(pose)
	root3d.add_child(body)
	body.set("traversal", t)
	body.set("program", program)
	var lane := {
		"name": name, "x": start.x, "start": start, "body": body, "traversal": t, "skeleton": sk, "pose": pose,
		"events": [], "penetration_frames": 0, "hang_frames": 0, "ladder_frames": 0, "edge_start": null, "edge_last": null,
		"inward_start": null, "inward_last": null, "hand_error_max": 0.0, "foot_error_max": 0.0, "hand_samples": 0,
		"foot_samples": 0, "wall_gap_max": 0.0,
	}
	t.mantle_started.connect(func(kind: int, _ledge: Dictionary) -> void:
		lane.events.append(["mantle_started", KIND_NAMES[kind], snappedf(frame / 60.0, 0.01)]))
	t.mantle_finished.connect(func(kind: int) -> void:
		lane.events.append(["mantle_finished", KIND_NAMES[kind], snappedf(frame / 60.0, 0.01)]))
	t.hang_started.connect(func(ledge: Dictionary) -> void:
		lane.events.append(["hang_started", snappedf((ledge["edge"] as Vector3).y, 0.1), snappedf(frame / 60.0, 0.01)]))
	t.hang_finished.connect(func(climbed: bool) -> void:
		lane.events.append(["hang_finished", climbed, snappedf(frame / 60.0, 0.01)]))
	t.hang_transferred.connect(func(_ledge: Dictionary, how: int) -> void:
		lane.events.append(["hang_transferred", ["outer", "inner", "along", "up", "drop", "down"][how], snappedf(frame / 60.0, 0.01)]))
	t.ladder_started.connect(func(_ladder: Node3D) -> void:
		lane.events.append(["ladder_started", snappedf(frame / 60.0, 0.01)]))
	t.ladder_finished.connect(func(at_top: bool) -> void:
		lane.events.append(["ladder_finished", at_top, snappedf(frame / 60.0, 0.01)]))
	sk.skeleton_updated.connect(_on_skeleton_updated.bind(lane))
	lanes.append(lane)
	return lane


func _build() -> void:
	_make_scripts()
	root3d = Node3D.new()
	get_root().add_child(root3d)
	probe_shape = CapsuleShape3D.new()
	probe_shape.radius = 0.31
	probe_shape.height = 1.72
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.55, 0.65, 0.78)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.6, 0.62, 0.66)
	e.ambient_light_energy = 0.6
	env.environment = e
	root3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 35, 0)
	sun.shadow_enabled = true
	root3d.add_child(sun)
	_box(Vector3(40, -0.5, -5), Vector3(120, 1, 40), Color(0.36, 0.42, 0.34))

	var walk := Vector3(0, 0, -1)
	var x := 0.0
	# 1. A 2.5 m ledge (3 m long, 0.4 m deep) with a wall rising behind it: no room to climb, it hangs; then left (the
	# body's left facing the wall: -X) to the end, then lets go.
	_box(Vector3(x, 1.25, -2.2), Vector3(3.0, 2.5, 0.4))
	_box(Vector3(x, 3.0, -2.9), Vector3(3.0, 6.0, 1.0), Color(0.6, 0.6, 0.66))
	_lane("hang_shimmy", Vector3(x, 0, 0), [[0.0, {"intent": walk, "speed": 2.0}], [0.55, {"jump": 5.2}], [1.4, {"intent": Vector3()}],
			[2.0, {"intent": Vector3(-1, 0, 0)}], [5.0, {"intent": Vector3(0, -1, 0)}], [5.3, {"intent": Vector3()}]])

	# 2. A 2.8 m ledge with room on top, hang_first: it hangs, then climbs when pushed at the wall again.
	x = 9.0
	_box(Vector3(x, 1.4, -3.0), Vector3(3.0, 2.8, 2.0))
	var l2 := _lane("hang_climb", Vector3(x, 0, 0), [[0.0, {"intent": walk, "speed": 2.0}], [0.55, {"jump": 5.2}], [1.4, {"intent": Vector3()}],
			[2.5, {"intent": walk}], [4.5, {"intent": Vector3()}]])
	(l2.traversal as FellowTraversal3D).hang_first = true

	# 3. A round tower (radius 1.6 m, 2.5 m) with a narrower one rising from it (radius 1.2 m): it hangs from the rim and
	# goes round it.
	x = 18.0
	_cylinder(Vector3(x, 1.25, -3.6), 1.6, 2.5)
	_cylinder(Vector3(x, 3.0, -3.6), 1.2, 6.0, Color(0.6, 0.6, 0.66))
	_lane("hang_round", Vector3(x, 0, 0), [[0.0, {"intent": walk, "speed": 2.0}], [0.55, {"jump": 5.2}], [1.4, {"intent": Vector3()}],
			[2.0, {"intent": Vector3(-1, 0, 0)}], [3.0, {"intent": Vector3(-1, 0, -1).normalized()}], [4.0, {"intent": Vector3(0, 0, -1)}],
			[5.5, {"intent": Vector3()}]])

	# 4. A ladder against a 4 m platform: up it and off onto the top.
	x = 27.0
	_box(Vector3(x, 2.0, -3.5), Vector3(3.0, 4.0, 3.0))
	_ladder(Vector3(x, 0, -1.97), 4.0)
	_lane("ladder_up", Vector3(x, 0, 0), [[0.0, {"intent": walk, "speed": 1.5}], [4.8, {"intent": Vector3()}]])

	# 5. The same: from the platform's top over the edge onto the ladder, down it, off at its foot.
	x = 36.0
	_box(Vector3(x, 2.0, -3.5), Vector3(3.0, 4.0, 3.0))
	_ladder(Vector3(x, 0, -1.97), 4.0)
	_lane("ladder_down", Vector3(x, 4.0, -4.0), [[0.0, {"intent": Vector3(0, 0, 1), "speed": 1.5}], [8.0, {"intent": Vector3()}]])

	# 6. A ledge with a gap: two 1.5 m ledges 0.8 m apart before one wall rising behind them; it hangs from the right one
	# and stops at the gap.
	x = 45.0
	_box(Vector3(x + 0.75, 1.25, -2.2), Vector3(1.5, 2.5, 0.4))
	_box(Vector3(x - 1.55, 1.25, -2.2), Vector3(1.5, 2.5, 0.4))
	_box(Vector3(x - 0.4, 3.0, -2.9), Vector3(4.0, 6.0, 1.0), Color(0.6, 0.6, 0.66))
	_lane("hang_gap", Vector3(x + 0.75, 0, 0), [[0.0, {"intent": walk, "speed": 2.0}], [0.55, {"jump": 5.2}], [1.4, {"intent": Vector3()}],
			[2.0, {"intent": Vector3(-1, 0, 0)}], [6.0, {"intent": Vector3()}]])

	# Milestone 81 (decision 0189): round corners and across to other ledges.
	# 7. A free block 2.8 m high (2 m wide, 1.6 m deep), hang_first: it hangs, goes left to the end and round the corner
	# onto the block's side (the wish held left goes on along it), then stops.
	x = 54.0
	_box(Vector3(x, 1.4, -2.8), Vector3(2.0, 2.8, 1.6))
	var l7 := _lane("hang_corner_out", Vector3(x, 0, 0), [[0.0, {"intent": walk, "speed": 2.0}], [0.55, {"jump": 5.2}], [1.4, {"intent": Vector3()}],
			[2.0, {"intent": Vector3(-1, 0, 0)}], [4.2, {"intent": Vector3()}]])
	(l7.traversal as FellowTraversal3D).hang_first = true

	# 8. A wall 2.5 m high with another meeting it from the front on its left (0.6 m thick, 2 m out), hang_first: it
	# hangs, goes left until the other wall is in the way and round the inside corner onto it, then out along it.
	x = 63.0
	_box(Vector3(x, 1.25, -2.2), Vector3(3.0, 2.5, 0.4))
	_box(Vector3(x - 1.0, 1.25, -1.0), Vector3(0.6, 2.5, 2.0))
	var l8 := _lane("hang_corner_in", Vector3(x, 0, 0), [[0.0, {"intent": walk, "speed": 2.0}], [0.55, {"jump": 5.2}], [1.4, {"intent": Vector3()}],
			[2.0, {"intent": Vector3(-1, 0, 0)}], [3.8, {"intent": Vector3()}]])
	(l8.traversal as FellowTraversal3D).hang_first = true

	# 9. Two 2.5 m ledges (1.5 m long, 0.4 m deep) 0.8 m apart: it hangs from the right one, goes left to the gap, jumps
	# across (hang_jump with the wish left) and goes on to the left one's end.
	x = 72.0
	_box(Vector3(x + 0.25, 1.25, -2.2), Vector3(1.5, 2.5, 0.4))
	_box(Vector3(x - 2.05, 1.25, -2.2), Vector3(1.5, 2.5, 0.4))
	var l9 := _lane("hang_jump_gap", Vector3(x, 0, 0), [[0.0, {"intent": walk, "speed": 2.0}], [0.55, {"jump": 5.2}], [1.4, {"intent": Vector3()}],
			[2.0, {"intent": Vector3(-1, 0, 0)}], [3.2, {"call": "hang_jump"}], [6.5, {"intent": Vector3()}]])
	(l9.traversal as FellowTraversal3D).hang_first = true

	# 10. A 2.5 m ledge (5 m long) with a beam over its left half (its top at 3.6 m, 0.6 m of air between): it hangs from
	# the ledge right of the beam, goes left under it and jumps up to it (hang_jump with the wish up).
	x = 81.0
	_box(Vector3(x, 1.25, -2.2), Vector3(5.0, 2.5, 0.4))
	_box(Vector3(x - 1.25, 3.35, -2.2), Vector3(2.5, 0.5, 0.4))
	var l10 := _lane("hang_jump_up", Vector3(x + 1.2, 0, 0), [[0.0, {"intent": walk, "speed": 2.0}], [0.55, {"jump": 5.2}], [1.4, {"intent": Vector3()}],
			[2.0, {"intent": Vector3(-1, 0, 0)}], [4.0, {"intent": Vector3()}], [4.4, {"intent": Vector3(0, 1, 0), "call": "hang_jump"}],
			[4.5, {"intent": Vector3()}]])
	(l10.traversal as FellowTraversal3D).hang_first = true

	# Milestone 84 (decision 0192): over an edge into a hang.
	# 11. On a 3.5 m platform walking at 1.5 m/s over its edge (drop_to_hang): it hangs from the edge facing back, the
	# wish that walked it there doing nothing; then left along it, then down (it lets go).
	x = 90.0
	_box(Vector3(x, 1.75, -4.0), Vector3(3.0, 3.5, 3.0))
	var l11 := _lane("drop_hang", Vector3(x, 3.5, -4.0), [[0.0, {"intent": Vector3(0, 0, 1), "speed": 1.5}], [3.0, {"intent": Vector3(-1, 0, 0)}],
			[4.0, {"intent": Vector3(0, -1, 0)}], [4.3, {"intent": Vector3()}]])
	(l11.traversal as FellowTraversal3D).drop_to_hang = true
	# 12. The same at 4.5 m/s: over the edge, falling to the ground.
	x = 95.5
	_box(Vector3(x, 1.75, -4.0), Vector3(3.0, 3.5, 3.0))
	var l12 := _lane("drop_run", Vector3(x, 3.5, -4.0), [[0.0, {"intent": Vector3(0, 0, 1), "speed": 4.5}], [1.5, {"intent": Vector3()}]])
	(l12.traversal as FellowTraversal3D).drop_to_hang = true

	# Milestone 86 (decision 0194): down to a ledge below.
	# 13. On a 6 m platform with a ledge 3.6 m high 0.3 m out of its face: over the edge into a hang (drop_to_hang), then
	# hang_jump with the wish down: caught on the ledge below.
	x = -10.0
	_box(Vector3(x, 3.0, -4.0), Vector3(3.0, 6.0, 3.0))
	_box(Vector3(x, 1.8, -2.35), Vector3(3.0, 3.6, 0.3))
	var l13 := _lane("hang_jump_down", Vector3(x, 6.0, -4.0), [[0.0, {"intent": Vector3(0, 0, 1), "speed": 1.5}],
			[2.5, {"intent": Vector3(0, -1, 0), "call": "hang_jump"}], [2.55, {"intent": Vector3()}]])
	(l13.traversal as FellowTraversal3D).drop_to_hang = true

	var cam := Camera3D.new()
	root3d.add_child(cam)
	cam.look_at_from_position(Vector3(22, 9, 16), Vector3(22, 1.5, -2))
	if pictures:
		_make_viewports()
	physics_frame.connect(_on_physics)


func _make_viewports() -> void:
	var grid_box: GridContainer
	if grid:
		grid_box = GridContainer.new()
		grid_box.columns = 3
		grid_box.add_theme_constant_override("h_separation", 0)
		grid_box.add_theme_constant_override("v_separation", 0)
		get_root().add_child(grid_box)
	for lane in lanes:
		var name: String = lane.name
		var vp := SubViewport.new()
		vp.size = shot_size
		vp.world_3d = get_root().world_3d
		vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		var cam := Camera3D.new()
		cam.fov = 55
		vp.add_child(cam)
		var x: float = lane.x
		match name:
			"ladder_up", "ladder_down":
				cam.look_at_from_position(Vector3(x + 5.0, 3.0, 2.5), Vector3(x, 2.0, -2.0))
			"hang_round":
				cam.look_at_from_position(Vector3(x + 1.0, 3.0, 4.0), Vector3(x - 0.8, 1.6, -2.4))
			"hang_corner_out":
				cam.look_at_from_position(Vector3(x - 4.5, 2.6, 2.0), Vector3(x - 0.8, 1.5, -2.4))
			"hang_corner_in":
				cam.look_at_from_position(Vector3(x + 2.0, 2.6, 2.8), Vector3(x - 0.6, 1.5, -1.6))
			"hang_jump_gap":
				cam.look_at_from_position(Vector3(x - 0.8, 2.6, 4.0), Vector3(x - 1.0, 1.6, -1.8))
			"hang_jump_up":
				cam.look_at_from_position(Vector3(x + 2.5, 2.8, 3.2), Vector3(x - 0.4, 2.2, -1.8))
			"hang_jump_down":
				cam.look_at_from_position(Vector3(x + 4.5, 4.5, 3.5), Vector3(x - 0.3, 3.4, -2.6))
			"drop_hang", "drop_run":
				cam.look_at_from_position(Vector3(x + 4.0, 3.6, 3.0), Vector3(x - 0.3, 2.2, -2.8))
			_:
				cam.look_at_from_position(Vector3(x + 2.5, 2.4, 3.2), Vector3(x - 0.4, 1.6, -1.8))
		if grid:
			var cell := Control.new()
			cell.custom_minimum_size = Vector2(shot_size)
			var holder := SubViewportContainer.new()
			holder.size = Vector2(shot_size)
			holder.add_child(vp)
			cell.add_child(holder)
			var label := Label.new()
			label.text = name
			label.add_theme_font_size_override("font_size", 20)
			label.add_theme_color_override("font_outline_color", Color.BLACK)
			label.add_theme_constant_override("outline_size", 6)
			label.position = Vector2(10, 6)
			cell.add_child(label)
			grid_box.add_child(cell)
		else:
			get_root().add_child(vp)
		viewports[name] = vp


func _find(name: String) -> Dictionary:
	for lane in lanes:
		if lane.name == name:
			return lane
	return {}


func _on_skeleton_updated(lane: Dictionary) -> void:
	var t: FellowTraversal3D = lane.traversal
	var sk: Skeleton3D = lane.skeleton
	var targets: Dictionary = t.get_pose_targets()
	if not targets.get("active", false) or not (t.is_hanging() or t.is_on_ladder()):
		return
	var pairs := [["LeftHand", "hand_left", targets.hand_weights.x, "LeftUpperArm", "LeftLowerArm"],
			["RightHand", "hand_right", targets.hand_weights.y, "RightUpperArm", "RightLowerArm"],
			["LeftFoot", "foot_left", targets.foot_weights.x, "LeftUpperLeg", "LeftLowerLeg"],
			["RightFoot", "foot_right", targets.foot_weights.y, "RightUpperLeg", "RightLowerLeg"]]
	for p in pairs:
		if p[2] < 0.999:
			continue
		var end_pos: Vector3 = sk.global_transform * sk.get_bone_global_pose(sk.find_bone(p[0])).origin
		var root_pos: Vector3 = sk.global_transform * sk.get_bone_global_pose(sk.find_bone(p[3])).origin
		var reach := sk.get_bone_rest(sk.find_bone(p[4])).origin.length() + sk.get_bone_rest(sk.find_bone(p[0])).origin.length()
		var target: Vector3 = targets[p[1]]
		if root_pos.distance_to(target) > reach * 0.98:
			continue  # out of reach: the limb points at it, straight
		var err := end_pos.distance_to(target)
		if String(p[0]).ends_with("Hand"):
			lane.hand_error_max = maxf(lane.hand_error_max, err)
			lane.hand_samples += 1
		else:
			lane.foot_error_max = maxf(lane.foot_error_max, err)
			lane.foot_samples += 1


func _on_physics() -> void:
	if done:
		return
	frame += 1
	for lane in lanes:
		var body: CharacterBody3D = lane.body
		var t: FellowTraversal3D = lane.traversal
		var q := PhysicsShapeQueryParameters3D.new()
		q.shape = probe_shape
		q.transform = Transform3D(Basis(), body.global_position + Vector3(0, 0.9, 0))
		q.exclude = [body.get_rid()]
		if not body.get_world_3d().direct_space_state.intersect_shape(q, 1).is_empty():
			lane.penetration_frames += 1
		if t.is_hanging():
			lane.hang_frames += 1
			var tg: Dictionary = t.get_pose_targets()
			var edge: Vector3 = (tg.hand_left + tg.hand_right) * 0.5
			if lane.edge_start == null:
				lane.edge_start = edge
				lane.inward_start = tg.inward
			lane.edge_last = edge
			lane.inward_last = tg.inward
		if t.is_on_ladder():
			lane.ladder_frames += 1
	if pictures:
		_shoot()
	if frame >= int(RUN_SECONDS * 60.0):
		done = true
		_finish.call_deferred()


func _shoot() -> void:
	for name in viewports:
		var vp: SubViewport = viewports[name]
		if save_frames:
			DirAccess.make_dir_recursive_absolute(out_dir.path_join("frames_" + name))
			vp.get_texture().get_image().save_png(out_dir.path_join("frames_%s/%04d.png" % [name, frame]))
		for k in strip_times.size():
			if frame == int(strip_times[k] * 60.0):
				if not shots.has(name):
					shots[name] = {}
				shots[name][k] = vp.get_texture().get_image()


func _names(lane: Dictionary) -> Array:
	var a := []
	for e in lane.events:
		a.append(e[0] + ("" if e.size() < 3 else ":" + str(e[1])))
	return a


func _finish() -> void:
	var failures := []
	var out := {}
	for lane in lanes:
		var body: CharacterBody3D = lane.body
		var feet := body.global_position
		var e := {"events": lane.events, "end": [snappedf(feet.x - lane.x, 0.01), snappedf(feet.y, 0.01), snappedf(feet.z, 0.01)],
			"penetration_frames": lane.penetration_frames, "hang_s": snappedf(lane.hang_frames / 60.0, 0.01),
			"ladder_s": snappedf(lane.ladder_frames / 60.0, 0.01), "hand_error_max_cm": snappedf(lane.hand_error_max * 100.0, 0.1),
			"foot_error_max_cm": snappedf(lane.foot_error_max * 100.0, 0.1), "hand_samples": lane.hand_samples,
			"foot_samples": lane.foot_samples, "on_floor": body.is_on_floor()}
		var moved := 0.0
		var turned := 0.0
		if lane.edge_start != null:
			moved = (lane.edge_last as Vector3 - lane.edge_start as Vector3).length()
			turned = rad_to_deg((lane.inward_start as Vector3).angle_to(lane.inward_last))
			e["moved_along"] = snappedf(moved, 0.01)
			e["turned_deg"] = snappedf(turned, 0.1)
		var names := _names(lane)
		e["sequence"] = names
		var ok: bool = lane.penetration_frames == 0
		match lane.name:
			"hang_shimmy":
				# Hung, moved left (-X) to within the body's width of the end (the wall's end at -1.5), let go, landed.
				ok = ok and names.has("hang_started:2.5") and names.has("hang_finished:false") and moved > 0.8 and feet.x - lane.x < -0.9 and feet.x - lane.x > -1.5 and feet.y < 0.05
			"hang_climb":
				ok = ok and names.has("hang_started:2.8") and names.has("hang_finished:true") and names.has("mantle_finished:high") and absf(feet.y - 2.8) < 0.05
			"hang_round":
				# Round the tower: turned with its rim, the body as far from its axis as hanging puts it.
				var axis := Vector3(lane.x, 0, -3.6)
				var dist := Vector2(feet.x - axis.x, feet.z - axis.z).length()
				e["axis_distance"] = snappedf(dist, 0.01)
				ok = ok and names.has("hang_started:2.5") and turned > 45.0 and absf(dist - (1.6 + 0.35 + 0.08)) < 0.12
			"ladder_up":
				ok = ok and names.has("ladder_started") and names.has("ladder_finished:true") and names.has("mantle_finished:low") and absf(feet.y - 4.0) < 0.05
			"ladder_down":
				ok = ok and names.has("ladder_started") and names.has("ladder_finished:false") and feet.y < 0.05 and feet.z > -1.9
			"hang_gap":
				# Stopped at the gap (the right ledge's end 0.75 m left of the start): not across it.
				var past_end: float = feet.x - (lane.x - 0.75)
				e["from_gap"] = snappedf(past_end, 0.01)
				ok = ok and names.has("hang_started:2.5") and past_end > 0.1 and past_end < 0.6 and moved > 0.2
			"hang_corner_out":
				# Round the outside corner (the block's left side at x - 1): turned a quarter, hanging from the side, behind
				# the front face; one transfer.
				e["from_side"] = snappedf(feet.x - (lane.x - 1.0), 0.01)
				ok = ok and names.has("hang_started:2.8") and names.count("hang_transferred:outer") == 1 and absf(turned - 90.0) < 5.0 and absf(feet.x - (lane.x - 1.0) + 0.43) < 0.05 and feet.z < -2.2 and feet.z > -3.5 and not names.has("hang_finished:false")
			"hang_corner_in":
				# Round the inside corner onto the other wall (its face at x - 0.7): turned a quarter, hanging from it, out along
				# it.
				e["from_side"] = snappedf(feet.x - (lane.x - 0.7), 0.01)
				ok = ok and names.has("hang_started:2.5") and names.count("hang_transferred:inner") == 1 and absf(turned - 90.0) < 5.0 and absf(feet.x - (lane.x - 0.7) - 0.43) < 0.05 and feet.z > -1.4 and not names.has("hang_finished:false")
			"hang_jump_gap":
				# Across the gap (the left ledge from x - 1.3 to x - 2.8) and on to its end.
				ok = ok and names.has("hang_started:2.5") and names.count("hang_transferred:along") == 1 and feet.x - lane.x < -2.3 and feet.x - lane.x > -2.8 and absf(feet.y - (2.5 - 2.05)) < 0.05 and not names.has("hang_finished:false")
			"hang_jump_up":
				# Up to the beam: hanging from 3.6 m.
				ok = ok and names.has("hang_started:2.5") and names.count("hang_transferred:up") == 1 and absf(feet.y - (3.6 - 2.05)) < 0.05 and not names.has("hang_finished:false")
			"drop_hang":
				# Over the edge (z -2.5) into a hang from it (3.5 m), along it, let go, on the ground in front of it.
				ok = ok and names.has("hang_started:3.5") and names.count("hang_transferred:drop") == 1 and names.has("hang_finished:false") and moved > 0.5 and feet.y < 0.05 and feet.z > -2.5 and feet.z < -1.5 and lane.hang_frames > 120
			"hang_jump_down":
				# Over the edge (6 m) into a hang, then down onto the ledge at 3.6 m: hanging there.
				ok = ok and names.has("hang_started:6.0") and names.count("hang_transferred:drop") == 1 and names.count("hang_transferred:down") == 1 and not names.has("hang_finished:false") and absf(feet.y - (3.6 - 2.05)) < 0.05 and absf(feet.z - (-2.2 + 0.43)) < 0.05
			"drop_run":
				# Too fast: over the edge and down, no hang.
				ok = ok and not names.has("hang_started:3.5") and feet.y < 0.05 and feet.z > -2.0
		e["ok"] = ok
		if not ok:
			failures.append(lane.name)
		out[lane.name] = e
	result["lanes"] = out
	result["failures"] = failures
	result["global"] = FellowTraversal3D.get_global_stats()
	result["physics_engine"] = ProjectSettings.get_setting("physics/3d/physics_engine")
	if pictures:
		_save_strip()
	print("CLIMB_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("climb_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)


func _save_strip() -> void:
	var w := shot_size.x
	var h := shot_size.y
	var names := []
	for lane in lanes:
		names.append(lane.name)
	var strip := Image.create(w * strip_times.size(), h * names.size(), false, Image.FORMAT_RGBA8)
	for r in names.size():
		for k in strip_times.size():
			var img: Image = shots.get(names[r], {}).get(k)
			if img == null:
				continue
			img.convert(Image.FORMAT_RGBA8)
			strip.blit_rect(img, Rect2i(0, 0, w, h), Vector2i(k * w, r * h))
	strip.save_png(out_dir.path_join("climb_strip.png"))
