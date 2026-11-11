extends SceneTree
## FellowEngine milestone 72 check (decision 0180): FellowTraversal3D and FellowTraversalPose3D.
##
##   godot --path benchmarks/project --headless --fixed-fps 60 -s res://bench/traversal_check.gd -- --out=<dir>
##   godot --path benchmarks/project --fixed-fps 60 --resolution 640x360 -s res://bench/traversal_check.gd
##       -- --out=<dir> --pictures=1 [--frames=1] [--shot=400x300] [--debug_draw=1]
##
## A course of lanes, each a CharacterBody3D (a capsule 0.35 m by 1.8 m, a test mannequin on a Skeleton3D posed by a
## FellowTraversalPose3D) running at an obstacle: a step, ledges of 0.9, 1.25 and 2.0 m, a wall too high, a ledge under
## a ceiling, a low ceiling over the runner, thin walls vaulted fast and slow, a thin wall with a wall behind it, walls
## at 30 and 60 degrees, a cylinder, a ramp, a sloped top, a moving platform, a jump to a ledge too high from the ground,
## and a ledge mantled with a clip (root motion, warped). For each: what the traversal decided, where the body ended,
## the frames its capsule overlapped the world, the hands' and feet's distance to their targets, the mantle's time. The
## checks' cost (find_ledge, a frame's automatic check). With --pictures=1, six lanes filmed from the side at six moments
## of their mantles (traversal_strip.png); --frames=1 saves every frame of them (for a video); --grid=1 shows them in
## the window, three by two (for --write-movie, the window 3 x 2 shots). One JSON line: TRAVERSAL_CHECK {...}; exit code 1
## when a lane decided otherwise than expected.
##
##   godot --path benchmarks/project --fixed-fps 60 -s res://bench/traversal_check.gd -- --crowd=200 [--seconds=20]
##
## The traversal's cost among many characters: N runners up and down lanes of a 0.9 m box, a thin wall, a 1.25 m ledge
## and a 1.8 m ledge, mantling and vaulting all the way (no meshes on their skeletons, each posed by a
## FellowTraversalPose3D); the traversals' time a frame (their checks, the mantles, the poses: get_global_stats()) and the
## frame's (headless: the CPU's whole frame, physics and scripts). One JSON line: TRAVERSAL_CROWD {...}.

const KIND_NAMES := ["none", "step", "low", "high", "vault", "too_high", "blocked"]
const RUN_SECONDS := 4.0

var args := {}
var out_dir := ""
var result := {}
var root3d: Node3D
var lanes: Array = []
var frame := 0
var pictures := false
var save_frames := false
var shot_size := Vector2i(400, 300)
var debug_draw := true
var strip_lanes := ["low_125", "high_20", "vault_fast", "air_grab", "clip_low", "angled_30"]
var strip_phases := [0.0, 0.2, 0.4, 0.6, 0.8, 1.0]
var shots := {}
var viewports := {}
var probe_shape: CapsuleShape3D
var controller_script: GDScript
var platform_script: GDScript
var done := false
var crowd := 0
var crowd_seconds := 20.0
var crowd_frames: PackedFloat64Array = []
var crowd_last_usec := 0
var crowd_start_frame := 0
var crowd_stats0 := {}
var grid := false
var grid_box: GridContainer


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://traversal_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	pictures = _arg("pictures", "0") == "1"
	save_frames = _arg("frames", "0") == "1"
	var ss: PackedStringArray = _arg("shot", "400x300").split("x")
	shot_size = Vector2i(int(ss[0]), int(ss[1]))
	debug_draw = _arg("debug_draw", "1") == "1"
	grid = _arg("grid", "0") == "1"
	crowd = int(_arg("crowd", "0"))
	crowd_seconds = float(_arg("seconds", "20"))
	result["physics_engine"] = ProjectSettings.get_setting("physics/3d/physics_engine")
	_build.call_deferred()


func _make_scripts() -> void:
	controller_script = GDScript.new()
	controller_script.source_code = """extends CharacterBody3D
var speed := 3.0
var dir := Vector3(0, 0, -1)
var jump_at := -1.0
var jump_speed := 5.5
var travelled := 0.0
var traversal
var running := true
var turn_z := 0.0

func _physics_process(delta: float) -> void:
	if traversal.is_active():
		return
	if turn_z > 0.0 and ((global_position.z < -turn_z and dir.z < 0.0) or (global_position.z > turn_z and dir.z > 0.0)):
		dir = -dir
	if not is_on_floor():
		velocity.y -= 9.8 * delta
	var v := dir * speed if running else Vector3()
	velocity.x = v.x
	velocity.z = v.z
	traversal.set_intent(v)
	if jump_at >= 0.0 and travelled >= jump_at and is_on_floor():
		velocity.y = jump_speed
		jump_at = -1.0
	var before := global_position
	move_and_slide()
	travelled += (global_position - before).length()
"""
	controller_script.reload()
	platform_script = GDScript.new()
	platform_script.source_code = """extends AnimatableBody3D
var velocity := Vector3(0.8, 0, 0)

func _physics_process(delta: float) -> void:
	global_position += velocity * delta
"""
	platform_script.reload()


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.85
	return m


func _box(parent: Node3D, pos: Vector3, size: Vector3, yaw_deg := 0.0, pitch_deg := 0.0, color := Color(0.72, 0.7, 0.66)) -> StaticBody3D:
	var b := StaticBody3D.new()
	b.position = pos
	b.rotation_degrees = Vector3(pitch_deg, yaw_deg, 0)
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
	parent.add_child(b)
	return b


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
	if crowd > 0 and not pictures:
		return sk
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


# A clip with root motion on the hips (up 1 m, then forward 0.8 m), the spine bending and the arms raised.
func _clip() -> Animation:
	var anim := Animation.new()
	anim.length = 1.0
	var hips := anim.add_track(Animation.TYPE_POSITION_3D)
	anim.track_set_path(hips, NodePath("Skeleton3D:Hips"))
	anim.position_track_insert_key(hips, 0.0, Vector3(0, 0.95, 0))
	anim.position_track_insert_key(hips, 0.3, Vector3(0, 1.45, -0.05))
	anim.position_track_insert_key(hips, 0.55, Vector3(0, 1.95, -0.15))
	anim.position_track_insert_key(hips, 1.0, Vector3(0, 1.95, -0.8))
	var spine := anim.add_track(Animation.TYPE_ROTATION_3D)
	anim.track_set_path(spine, NodePath("Skeleton3D:Spine"))
	anim.rotation_track_insert_key(spine, 0.0, Quaternion())
	anim.rotation_track_insert_key(spine, 0.5, Quaternion(Vector3(1, 0, 0), deg_to_rad(-35.0)))
	anim.rotation_track_insert_key(spine, 1.0, Quaternion())
	for s in ["Left", "Right"]:
		var arm := anim.add_track(Animation.TYPE_ROTATION_3D)
		anim.track_set_path(arm, NodePath("Skeleton3D:" + s + "UpperArm"))
		anim.rotation_track_insert_key(arm, 0.0, Quaternion())
		anim.rotation_track_insert_key(arm, 0.35, Quaternion(Vector3(1, 0, 0), deg_to_rad(150.0)))
		anim.rotation_track_insert_key(arm, 0.8, Quaternion(Vector3(1, 0, 0), deg_to_rad(40.0)))
		anim.rotation_track_insert_key(arm, 1.0, Quaternion())
	return anim


func _lane(name: String, index: int, speed: float, expect: String) -> Dictionary:
	var x := float(index) * 8.0
	var body := CharacterBody3D.new()
	body.name = "Runner_" + name
	body.set_script(controller_script)
	body.position = Vector3(x, 0.0, 0)
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
	body.set("speed", speed)
	body.set("traversal", t)
	t.debug_draw = pictures and debug_draw
	var lane := {
		"name": name, "x": x, "body": body, "traversal": t, "skeleton": sk, "pose": pose, "expect": expect,
		"started": "", "start_frame": -1, "finish_frame": -1, "seen": {}, "penetration_frames": 0,
		"hand_error_max": 0.0, "foot_error_max": 0.0, "hand_samples": 0, "foot_samples": 0, "reason": "",
		"top": NAN, "progress_last": 0.0, "kind_ledge": {},
	}
	t.mantle_started.connect(func(kind: int, ledge: Dictionary) -> void:
		lane["started"] = KIND_NAMES[kind]
		lane["start_frame"] = frame
		lane["kind_ledge"] = {"height": snappedf(ledge["height"], 0.001), "depth": snappedf(ledge["depth"], 0.001)})
	t.mantle_finished.connect(func(_kind: int) -> void:
		lane["finish_frame"] = frame
		body.set("running", false))
	sk.skeleton_updated.connect(_on_skeleton_updated.bind(lane))
	lanes.append(lane)
	return lane


func _build() -> void:
	_make_scripts()
	root3d = Node3D.new()
	get_root().add_child(root3d)
	if crowd > 0:
		_build_crowd()
		physics_frame.connect(_on_physics)
		return
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
	_box(root3d, Vector3(75, -0.5, -10), Vector3(180, 1, 40), 0, 0, Color(0.36, 0.42, 0.34))

	var i := 0
	var l: Dictionary
	# The obstacles' faces 2.5 m ahead of the runners (z = -2.5).
	l = _lane("step", i, 3.0, "none")
	_box(root3d, Vector3(l.x, 0.15, -3.5), Vector3(2, 0.3, 2))
	i += 1
	l = _lane("low_09", i, 3.0, "low")
	l.top = 0.9
	_box(root3d, Vector3(l.x, 0.45, -3.5), Vector3(2, 0.9, 2))
	i += 1
	l = _lane("low_125", i, 3.0, "low")
	l.top = 1.25
	_box(root3d, Vector3(l.x, 0.625, -3.5), Vector3(2, 1.25, 2))
	i += 1
	l = _lane("high_20", i, 3.0, "high")
	l.top = 2.0
	_box(root3d, Vector3(l.x, 1.0, -3.5), Vector3(2, 2.0, 2))
	i += 1
	l = _lane("too_high", i, 3.0, "too_high")
	_box(root3d, Vector3(l.x, 1.3, -3.5), Vector3(2, 2.6, 2))
	i += 1
	l = _lane("ceiling_on_top", i, 3.0, "blocked")
	_box(root3d, Vector3(l.x, 0.5, -4.0), Vector3(2, 1.0, 3))
	_box(root3d, Vector3(l.x, 1.0 + 1.3 + 0.15, -4.2), Vector3(2, 0.3, 3))
	i += 1
	l = _lane("ceiling_over_runner", i, 3.0, "blocked")
	_box(root3d, Vector3(l.x, 0.5, -3.5), Vector3(2, 1.0, 2))
	_box(root3d, Vector3(l.x, 2.0 + 0.15, -1.5), Vector3(2, 0.3, 2.0))
	i += 1
	l = _lane("vault_fast", i, 4.5, "vault")
	l.top = 0.0
	_box(root3d, Vector3(l.x, 0.5, -2.625), Vector3(2, 1.0, 0.25))
	i += 1
	l = _lane("vault_slow", i, 1.5, "vault")
	l.top = 0.0
	_box(root3d, Vector3(l.x, 0.5, -2.625), Vector3(2, 1.0, 0.25))
	i += 1
	l = _lane("fence_wall_behind", i, 4.5, "blocked")
	_box(root3d, Vector3(l.x, 0.5, -2.625), Vector3(2, 1.0, 0.25))
	_box(root3d, Vector3(l.x, 1.25, -3.3), Vector3(2, 2.5, 0.3))
	i += 1
	l = _lane("angled_30", i, 3.0, "low")
	l.top = 1.0
	# A face 2.5 m ahead turned 30 degrees.
	var b := _box(root3d, Vector3(), Vector3(4, 1.0, 4), 30.0)
	b.position = Vector3(l.x, 0.5, -2.5) + Basis(Vector3.UP, deg_to_rad(30.0)) * Vector3(0, 0, -2.0)
	i += 1
	l = _lane("angled_60", i, 3.0, "none")
	# The face long on the side the runner slides to (its corner out of reach before the run ends).
	b = _box(root3d, Vector3(), Vector3(14, 1.0, 4), 60.0)
	b.position = Vector3(l.x, 0.5, -2.5) + Basis(Vector3.UP, deg_to_rad(60.0)) * Vector3(5.0, 0, -2.0)
	i += 1
	l = _lane("cylinder", i, 3.0, "low")
	l.top = 1.1
	var cyl := StaticBody3D.new()
	cyl.position = Vector3(l.x, 0.55, -3.4)
	var ccs := CollisionShape3D.new()
	var cshape := CylinderShape3D.new()
	cshape.radius = 0.9
	cshape.height = 1.1
	ccs.shape = cshape
	cyl.add_child(ccs)
	var cmi := MeshInstance3D.new()
	var cmesh := CylinderMesh.new()
	cmesh.top_radius = 0.9
	cmesh.bottom_radius = 0.9
	cmesh.height = 1.1
	cmi.mesh = cmesh
	cmi.material_override = _material(Color(0.72, 0.7, 0.66))
	cyl.add_child(cmi)
	root3d.add_child(cyl)
	i += 1
	l = _lane("ramp", i, 3.0, "none")
	_box(root3d, Vector3(l.x, 0.6, -5.5), Vector3(2, 0.2, 6), 0, 12.0)
	i += 1
	l = _lane("sloped_top", i, 3.0, "blocked")
	var wedge := StaticBody3D.new()
	wedge.position = Vector3(l.x, 0, -2.5)
	var wcs := CollisionShape3D.new()
	var wshape := ConvexPolygonShape3D.new()
	var rise := 0.8 * tan(deg_to_rad(50.0))
	var pts := PackedVector3Array()
	for xx in [-1.0, 1.0]:
		pts.append(Vector3(xx, 0, 0))
		pts.append(Vector3(xx, 1.0, 0))
		pts.append(Vector3(xx, 1.0 + rise, -0.8))
		pts.append(Vector3(xx, 0, -0.8))
	wshape.points = pts
	wcs.shape = wshape
	wedge.add_child(wcs)
	var wmi := MeshInstance3D.new()
	wmi.mesh = wshape.get_debug_mesh()
	wedge.add_child(wmi)
	root3d.add_child(wedge)
	i += 1
	l = _lane("moving_platform", i, 3.0, "low")
	l.top = 1.0
	var plat := AnimatableBody3D.new()
	plat.position = Vector3(l.x - 1.0, 0.5, -3.5)
	plat.sync_to_physics = true
	var pcs := CollisionShape3D.new()
	var pshape := BoxShape3D.new()
	pshape.size = Vector3(2.5, 1.0, 2.0)
	pcs.shape = pshape
	plat.add_child(pcs)
	var pmi := MeshInstance3D.new()
	var pmesh := BoxMesh.new()
	pmesh.size = pshape.size
	pmi.mesh = pmesh
	pmi.material_override = _material(Color(0.8, 0.6, 0.35))
	plat.add_child(pmi)
	plat.set_script(platform_script)
	root3d.add_child(plat)
	l["platform"] = plat
	i += 1
	l = _lane("air_grab", i, 3.0, "high")
	l.top = 2.9
	l.body.set("jump_at", 1.0)
	_box(root3d, Vector3(l.x, 1.45, -3.5), Vector3(2, 2.9, 2))
	i += 1
	l = _lane("clip_low", i, 3.0, "low")
	l.top = 1.2
	l.traversal.low_clip = _clip()
	l.traversal.clip_root_bone = &"Hips"
	_box(root3d, Vector3(l.x, 0.6, -3.5), Vector3(2, 1.2, 2))
	i += 1
	l = _lane("disabled", i, 3.0, "none")
	l.traversal.enabled = false
	_box(root3d, Vector3(l.x, 0.45, -3.5), Vector3(2, 0.9, 2))

	if pictures:
		_make_viewports()
	else:
		var cam := Camera3D.new()
		root3d.add_child(cam)
	physics_frame.connect(_on_physics)


func _make_viewports() -> void:
	for name in strip_lanes:
		var lane: Dictionary = _find(name)
		var vp := SubViewport.new()
		vp.size = shot_size
		vp.world_3d = get_root().world_3d
		vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		var cam := Camera3D.new()
		cam.fov = 50
		vp.add_child(cam)
		if grid:
			if grid_box == null:
				grid_box = GridContainer.new()
				grid_box.columns = 3
				grid_box.add_theme_constant_override("h_separation", 0)
				grid_box.add_theme_constant_override("v_separation", 0)
				get_root().add_child(grid_box)
			# A cell: the view, its lane's name over it.
			var cell := Control.new()
			cell.custom_minimum_size = Vector2(shot_size)
			var holder := SubViewportContainer.new()
			holder.size = Vector2(shot_size)
			holder.add_child(vp)
			cell.add_child(holder)
			var label := Label.new()
			label.text = String(name)
			label.add_theme_font_size_override("font_size", 22)
			label.add_theme_color_override("font_outline_color", Color.BLACK)
			label.add_theme_constant_override("outline_size", 6)
			label.position = Vector2(10, 6)
			cell.add_child(label)
			grid_box.add_child(cell)
		else:
			get_root().add_child(vp)
		var top: float = 1.0 if is_nan(lane.top) else maxf(lane.top, 1.0)
		cam.look_at_from_position(Vector3(lane.x + 4.6, top * 0.55 + 1.1, -1.4), Vector3(lane.x, top * 0.55 + 0.45, -2.8))
		viewports[name] = {"vp": vp, "cam": cam}
	var main_cam := Camera3D.new()
	root3d.add_child(main_cam)
	main_cam.look_at_from_position(Vector3(72, 22, 16), Vector3(72, 0, -3))


func _find(name: String) -> Dictionary:
	for lane in lanes:
		if lane.name == name:
			return lane
	return {}


func _check_lane(lane: Dictionary) -> void:
	var body: CharacterBody3D = lane.body
	var t: FellowTraversal3D = lane.traversal
	var lc: Dictionary = t.get_last_check()
	var k: String = KIND_NAMES[lc["kind"]]
	if k != "none":
		lane.seen[k] = true
		if lc["reason"] != "":
			lane.reason = lc["reason"]
	# The capsule against the world (a little smaller than the body's: touching is not overlapping).
	var q := PhysicsShapeQueryParameters3D.new()
	q.shape = probe_shape
	q.transform = Transform3D(Basis(), body.global_position + Vector3(0, 0.9, 0))
	q.exclude = [body.get_rid()]
	var hits := body.get_world_3d().direct_space_state.intersect_shape(q, 1)
	if not hits.is_empty():
		lane.penetration_frames += 1
	if t.is_active():
		lane.progress_last = t.get_progress()


# The pose the modifiers made this frame (between the modifiers and the bones' restore): the hands and feet against the
# targets they were posed to, the clip's bones halfway through.
func _on_skeleton_updated(lane: Dictionary) -> void:
	var t: FellowTraversal3D = lane.traversal
	var sk: Skeleton3D = lane.skeleton
	var targets: Dictionary = t.get_pose_targets()
	if not targets.get("active", false):
		return
	lane.hips_drop_max = maxf(lane.get("hips_drop_max", 0.0), lane.pose.get_hips_drop())
	if targets.get("clip") != null:
		if absf(t.get_progress() - 0.5) < 0.02 and not lane.has("clip_mid"):
			lane["clip_mid"] = {
				"spine_angle_deg": snappedf(rad_to_deg(sk.get_bone_pose_rotation(sk.find_bone("Spine")).get_angle()), 0.1),
				"hips_offset": sk.get_bone_pose_position(sk.find_bone("Hips")) - Vector3(0, 0.95, 0),
			}
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
		if String(p[0]).ends_with("Foot"):
			var follow: float = targets.foot_follow.x if p[0] == "LeftFoot" else targets.foot_follow.y
			target -= Vector3.UP * lane.pose.get_hips_drop() * follow
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
	if crowd > 0:
		_crowd_tick()
		return
	for lane in lanes:
		_check_lane(lane)
	if pictures:
		_maybe_shoot()
	if frame >= int(RUN_SECONDS * 60.0):
		done = true
		_finish.call_deferred()


func _maybe_shoot() -> void:
	for name in strip_lanes:
		var lane: Dictionary = _find(name)
		var t: FellowTraversal3D = lane.traversal
		var phase := -1.0
		if t.is_active():
			phase = t.get_progress()
		elif lane.finish_frame >= 0 and frame == lane.finish_frame + 1:
			phase = 1.0
		elif lane.start_frame < 0:
			phase = -1.0
		if not shots.has(name):
			shots[name] = {}
		var vp: SubViewport = viewports[name].vp
		if save_frames and (lane.finish_frame < 0 or frame <= lane.finish_frame + 30):
			var img := vp.get_texture().get_image()
			DirAccess.make_dir_recursive_absolute(out_dir.path_join("frames_" + name))
			img.save_png(out_dir.path_join("frames_%s/%04d.png" % [name, frame]))
		if phase < 0.0:
			continue
		for k in strip_phases.size():
			if not shots[name].has(k) and phase >= strip_phases[k] - 0.001:
				shots[name][k] = vp.get_texture().get_image()


func _finish() -> void:
	var failures := []
	var out_lanes := {}
	for lane in lanes:
		var body: CharacterBody3D = lane.body
		var t: FellowTraversal3D = lane.traversal
		var got: String = lane.started if lane.started != "" else ("none" if lane.seen.is_empty() else ",".join(lane.seen.keys()))
		var ok := false
		match lane.expect:
			"low", "high", "vault":
				ok = lane.started == lane.expect and lane.finish_frame > 0
			"none":
				ok = lane.started == "" and not lane.seen.has("low") and not lane.seen.has("high") and not lane.seen.has("vault")
			_:
				ok = lane.started == "" and lane.seen.has(lane.expect)
		var feet := body.global_position
		var entry := {
			"expect": lane.expect, "got": got, "ok": ok, "reason": lane.reason,
			"feet": [snappedf(feet.x - lane.x, 0.001), snappedf(feet.y, 0.001), snappedf(feet.z, 0.001)],
			"penetration_frames": lane.penetration_frames,
		}
		if lane.start_frame >= 0:
			entry["mantle_frames"] = lane.finish_frame - lane.start_frame
			entry["ledge"] = lane.kind_ledge
			entry["hand_error_max_cm"] = snappedf(lane.hand_error_max * 100.0, 0.01)
			entry["foot_error_max_cm"] = snappedf(lane.foot_error_max * 100.0, 0.01)
			entry["ik_samples"] = [lane.hand_samples, lane.foot_samples]
			entry["hips_drop_max"] = snappedf(lane.get("hips_drop_max", 0.0), 0.001)
			entry["yaw_deg"] = snappedf(rad_to_deg(body.global_rotation.y), 0.1)
		if not is_nan(lane.top) and lane.start_frame >= 0:
			entry["on_top"] = absf(feet.y - lane.top) < 0.08
			if not entry["on_top"]:
				ok = false
		if lane.has("platform"):
			var plat: Node3D = lane.platform
			var local := plat.global_transform.affine_inverse() * feet
			entry["on_platform"] = absf(local.x) < 1.25 and absf(local.z) < 1.0
			ok = ok and entry["on_platform"]
		if lane.has("clip_mid"):
			entry["clip_mid"] = {"spine_angle_deg": lane.clip_mid.spine_angle_deg, "hips_offset": [snappedf(lane.clip_mid.hips_offset.x, 0.001), snappedf(lane.clip_mid.hips_offset.y, 0.001), snappedf(lane.clip_mid.hips_offset.z, 0.001)]}
		if lane.penetration_frames > 0 and lane.start_frame >= 0:
			ok = false
		entry["ok"] = ok
		if not ok:
			failures.append(lane.name)
		out_lanes[lane.name] = entry
	result["lanes"] = out_lanes
	result["failures"] = failures
	_measure_cost()
	if pictures:
		_save_strip()
		var windows := {}
		for name in strip_lanes:
			var lane: Dictionary = _find(name)
			windows[name] = [lane.start_frame, lane.finish_frame]
		result["frame_windows"] = windows
	print("TRAVERSAL_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("traversal_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)


# find_ledge's cost: a runner put back in front of a ledge, the check repeated.
func _measure_cost() -> void:
	var lane: Dictionary = _find("too_high")
	var body: CharacterBody3D = lane.body
	var t: FellowTraversal3D = lane.traversal
	var cases := {"ledge_low": "low_125", "thin_wall": "vault_fast", "nothing": "ramp", "too_high": "too_high"}
	var costs := {}
	for key in cases:
		var src: Dictionary = _find(cases[key])
		var b2: CharacterBody3D = src.body
		var t2: FellowTraversal3D = src.traversal
		b2.global_position = Vector3(src.x, 0.02, -1.9)
		b2.velocity = Vector3(0, 0, -3)
		var n := 2000
		var t0 := Time.get_ticks_usec()
		for k in n:
			t2.find_ledge(Vector3(0, 0, -1))
		costs[key] = snappedf(float(Time.get_ticks_usec() - t0) / n, 0.01)
		costs[key + "_kind"] = KIND_NAMES[t2.get_last_check()["kind"]]
	result["find_ledge_usec"] = costs


func _save_strip() -> void:
	var w := shot_size.x
	var h := shot_size.y
	var strip := Image.create(w * strip_phases.size(), h * strip_lanes.size(), false, Image.FORMAT_RGBA8)
	strip.fill(Color(0.1, 0.1, 0.1))
	for r in strip_lanes.size():
		var name: String = strip_lanes[r]
		for c in strip_phases.size():
			var img: Image = shots.get(name, {}).get(c)
			if img == null:
				continue
			img.convert(Image.FORMAT_RGBA8)
			strip.blit_rect(img, Rect2i(0, 0, w, h), Vector2i(c * w, r * h))
	strip.save_png(out_dir.path_join("traversal_strip.png"))
	var shot := get_root().get_texture().get_image()
	shot.save_png(out_dir.path_join("traversal_course.png"))


# ------------------------------------------------------------------ the crowd (--crowd=N)

func _build_crowd() -> void:
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 35, 0)
	root3d.add_child(sun)
	var width := float(crowd) * 2.5
	_box(root3d, Vector3(width * 0.5, -0.5, 0), Vector3(width + 10.0, 1, 34), 0, 0, Color(0.36, 0.42, 0.34))
	for i in crowd:
		var x := float(i) * 2.5
		var body := CharacterBody3D.new()
		body.set_script(controller_script)
		# Between the obstacles, half the runners going one way.
		body.position = Vector3(x, 0.0, [10.5, 4.7, 0.2, -6.1, -11.4][i % 5])
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
		sk.add_child(FellowTraversalPose3D.new())
		root3d.add_child(body)
		body.set("speed", 3.5)
		body.set("traversal", t)
		body.set("turn_z", 13.0)
		body.set("dir", Vector3(0, 0, -1) if i % 2 == 0 else Vector3(0, 0, 1))
		# The lane: a 0.9 m box, a thin wall, a 1.25 m ledge, a 1.8 m ledge.
		_box(root3d, Vector3(x, 0.45, 7.5), Vector3(1.6, 0.9, 1.5))
		_box(root3d, Vector3(x, 0.5, 2.5), Vector3(1.6, 1.0, 0.25))
		_box(root3d, Vector3(x, 0.625, -3.0), Vector3(1.6, 1.25, 2.0))
		_box(root3d, Vector3(x, 0.9, -9.0), Vector3(1.6, 1.8, 1.5))
	var cam := Camera3D.new()
	root3d.add_child(cam)
	cam.look_at_from_position(Vector3(-6, 8, 18), Vector3(minf(width, 30.0) * 0.5, 0, 0))
	crowd_start_frame = 60


func _crowd_tick() -> void:
	var now := Time.get_ticks_usec()
	if frame == crowd_start_frame:
		FellowTraversal3D.reset_global_stats()
		crowd_last_usec = now
		return
	if frame < crowd_start_frame:
		return
	crowd_frames.append(float(now - crowd_last_usec) / 1000.0)
	crowd_last_usec = now
	if frame >= crowd_start_frame + int(crowd_seconds * 60.0):
		done = true
		var n := float(frame - crowd_start_frame)
		var st := FellowTraversal3D.get_global_stats()
		var r := {"runners": crowd, "frames": int(n), "physics_engine": ProjectSettings.get_setting("physics/3d/physics_engine"),
			"stats": st,
			"traversal_ms_per_frame": snappedf(float(st.check_usec + st.step_usec + st.pose_usec) / n / 1000.0, 0.001),
			"checks_ms_per_frame": snappedf(float(st.check_usec) / n / 1000.0, 0.001),
			"steps_ms_per_frame": snappedf(float(st.step_usec) / n / 1000.0, 0.001),
			"poses_ms_per_frame": snappedf(float(st.pose_usec) / n / 1000.0, 0.001),
			"check_usec_mean": snappedf(float(st.check_usec) / maxf(1.0, float(st.checks)), 0.01),
			"checks_per_frame": snappedf(float(st.checks) / n, 0.01),
			"mantles_per_second": snappedf(float(st.mantles_low + st.mantles_high + st.vaults) / (n / 60.0), 0.01)}
		var fr := crowd_frames.duplicate()
		fr.sort()
		r["frame_ms_mean"] = snappedf(_mean(crowd_frames), 0.001)
		r["frame_ms_p99"] = snappedf(fr[int(fr.size() * 0.99)], 0.001)
		print("TRAVERSAL_CROWD " + JSON.stringify(r))
		var f := FileAccess.open(out_dir.path_join("traversal_crowd.json"), FileAccess.WRITE)
		f.store_string(JSON.stringify(r, "  "))
		f.close()
		quit(0)


func _mean(a: PackedFloat64Array) -> float:
	var s := 0.0
	for v in a:
		s += v
	return s / maxf(1.0, float(a.size()))
