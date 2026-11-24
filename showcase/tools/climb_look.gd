extends SceneTree
## Milestone 196: the character's climbing filmed (the owner: the climbing and free-running animations must be the best
## they can be). The demo plays from a shot (`-- --demo --demo_from=house`: the run at CasaCrema, the climb up its front
## from hold to hold, over the eave and up the tiles), the frame rate fixed (pass --fixed-fps 60); every --every-th
## frame is saved (<dir>/f%04d.png) from a camera of its own: "side" beside the body along the wall (the limbs against
## the stone), "back" behind and below it, or "demo" (the demo's own cameras). Each frame's state goes to
## <dir>/frames.json (the player's state, the traversal's, the body's place, the hands' and feet's distances to their
## targets, the elbows' and knees' bend directions) for climb_metrics.py.
##
##   godot --path showcase --resolution 960x540 --fixed-fps 60 -s res://tools/climb_look.gd -- --demo --demo_from=house \
##       --out=<dir> [--seconds=30] [--skip=0] [--every=2] [--cam=side] [--lite=1] [--mocap_all=1] [--images=0]
##   godot ... -s res://tools/climb_look.gd -- --program=hang --out=<dir> [--seconds=18] ...
##   godot ... -s res://tools/climb_look.gd -- --program=course --out=<dir> [--seconds=10] ...
##
## --images=0 (headless): frames.json alone. --jpg=1: the frames as JPEG (f%04d.jpg). --program=hang (no demo): at CasaCrema's front up to its first hold, hanging
## still for 4 s, along the hold each way, still again, then on up from hold to hold.
##
## --lite=1 keeps the square's houses and CasaCrema (no city beyond, no country, no people: lavapipe's whole rooftops
## outgrows 6 GB); --mocap_all=1 plays every clip of the owner's Mixamo library (rooftops_player.gd's MOCAP_KEEP_OWN too).

const KEEP := ["PalazzoOcra", "Campanile", "CasaCrema", "PalazzoSiena", "CasaBianca", "CasaOcraBassa", "CasaRosa",
		"CasaRiferimento", "Fountain", "Near4CasaCrema"]
const LIMBS := [["LeftUpperArm", "LeftLowerArm", "LeftHand"], ["RightUpperArm", "RightLowerArm", "RightHand"],
		["LeftUpperLeg", "LeftLowerLeg", "LeftFoot"], ["RightUpperLeg", "RightLowerLeg", "RightFoot"]]

var _out := "user://climb_look"
var _seconds := 30.0
var _skip := 0
var _every := 2
var _cam_mode := "side"
var _lite := false
var _images := true
var _jpg := false
var _dist := 3.0 # the camera's distance from the body
var _program := "" # "hang": at CasaCrema's front, up to its first hold, hanging still, along it each way, on up
var _player: CharacterBody3D
var _skel: Skeleton3D
var _cam: Camera3D
var _cam_pos := Vector3()
var _look := Vector3()
var _frames: Array = []
var _posed := {} # the bones as drawn (after the skeleton's modifiers: read when it says it is updated)
const BONES := ["Hips", "LeftUpperArm", "LeftLowerArm", "LeftHand", "RightUpperArm", "RightLowerArm", "RightHand", "LeftUpperLeg",
		"LeftLowerLeg", "LeftFoot", "RightUpperLeg", "RightLowerLeg", "RightFoot", "Head"]


func _init() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			_out = a.get_slice("=", 1)
		elif a.begins_with("--seconds="):
			_seconds = float(a.get_slice("=", 1))
		elif a.begins_with("--skip="):
			_skip = int(a.get_slice("=", 1))
		elif a.begins_with("--every="):
			_every = maxi(1, int(a.get_slice("=", 1)))
		elif a.begins_with("--cam="):
			_cam_mode = a.get_slice("=", 1)
		elif a == "--lite=1":
			_lite = true
		elif a == "--images=0":
			_images = false
		elif a == "--jpg=1":
			_jpg = true
		elif a.begins_with("--dist="):
			_dist = float(a.get_slice("=", 1))
		elif a.begins_with("--program="):
			_program = a.get_slice("=", 1)
	DirAccess.make_dir_recursive_absolute(_out)
	var sc: Node = (load("res://rooftops/rooftops.tscn") as PackedScene).instantiate()
	if _lite:
		for n in ["Skyline", "CityHouses", "Landscape", "Trees_cypress", "Trees_olive", "Trees_pine", "Trees_broadleaf",
				"Villas", "Trees_villa_cypress", "Birds", "Crowd", "PipelineWarmer"]:
			var c := sc.get_node_or_null(n)
			if c:
				sc.remove_child(c)
				c.free()
		var town := sc.get_node("Town")
		for c in town.get_children():
			if not (c.name in KEEP):
				town.remove_child(c)
				c.free()
	if _lite:
		# (the light simplified for lavapipe: no SDFGI, SSIL or volumetric fog — the motion is what is looked at)
		for n in sc.find_children("*", "WorldEnvironment", true, false):
			var env := (n as WorldEnvironment).environment
			env.sdfgi_enabled = false
			env.ssil_enabled = false
			env.volumetric_fog_enabled = false
			env.ambient_light_energy = 1.6
	root.add_child(sc)
	_player = sc.get_node("Player")
	_skel = _player.get_node("Model/Mannequin/Skeleton3D")
	_skel.skeleton_updated.connect(_on_posed)
	await process_frame
	if _cam_mode != "demo":
		_cam = Camera3D.new()
		_cam.fov = 50.0
		sc.add_child(_cam)
	if _program == "hang":
		_player.set("autopilot", true)
		_player.global_position = Vector3(13.1, 0.0, -9.6)
		_player.set("facing", 0.0)
		_player.rotation.y = 0.0
	elif _program == "course":
		_course(sc)
	var total := _skip + int(_seconds * 60.0)
	var vp := root.get_viewport_rid()
	for i in total:
		# (only the frames saved are drawn)
		var draw := _images and i >= _skip and (i - _skip) % _every == 0
		RenderingServer.viewport_set_update_mode(vp, RenderingServer.VIEWPORT_UPDATE_ALWAYS if draw else RenderingServer.VIEWPORT_UPDATE_DISABLED)
		if _program == "hang":
			_drive(i / 60.0)
		elif _program == "course":
			_drive_course(i / 60.0)
		if _cam:
			_place_camera(i == 0)
			_cam.current = true
		await process_frame
		_record(i)
		if draw:
			await RenderingServer.frame_post_draw
			var shot := root.get_texture().get_image()
			if _jpg:
				shot.save_jpg("%s/f%04d.jpg" % [_out, (i - _skip) / _every], 0.9)
			else:
				shot.save_png("%s/f%04d.png" % [_out, (i - _skip) / _every])
	var f := FileAccess.open(_out.path_join("frames.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify({"every": _every, "skip": _skip, "frames": _frames}))
	f.close()
	print("climb_look: %d frames" % total)
	quit()


## The "course" program (milestone 196): on the square, along +x — a low wall to vault (0.95 m high, 0.35 m thick) at
## a sprint, then a block 3 m high and 2 m deep: a jump at it, its edge caught, up onto its top from the hang.
const COURSE_Z := 8.0
const COURSE_X := -2.0

func _box(p_parent: Node, p_name: String, p_size: Vector3, p_at: Vector3) -> void:
	var body := StaticBody3D.new()
	body.name = p_name
	p_parent.add_child(body)
	body.global_position = p_at
	var cs := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = p_size
	cs.shape = shape
	body.add_child(cs)
	var mi := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = p_size
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.72, 0.64, 0.52)
	mat.roughness = 0.9
	mesh.material = mat
	mi.mesh = mesh
	body.add_child(mi)


func _course(p_scene: Node) -> void:
	# (the square's props out of the way)
	var town := p_scene.get_node("Town")
	for c in town.get_children():
		var n := String(c.name)
		if n.begins_with("Bollard") or n.begins_with("Barrel") or n.begins_with("Crate") or n.begins_with("Stall") or n == "HayCart":
			c.queue_free()
	_box(p_scene, "CourseVault", Vector3(0.35, 0.95, 3.0), Vector3(COURSE_X + 5.0, 0.475, COURSE_Z))
	_box(p_scene, "CourseBlock", Vector3(2.0, 3.0, 3.0), Vector3(COURSE_X + 11.0, 1.5, COURSE_Z))
	_player.get("traversal").set("hang_first", true) # (the block's edge caught and hung from, then climbed onto)
	_player.set("autopilot", true)
	_player.global_position = Vector3(COURSE_X, 0.0, COURSE_Z)
	_player.set("facing", -PI / 2.0) # (facing +x: the model looks down its −Z)
	_player.rotation.y = -PI / 2.0


var _course_hung := -1.0

func _drive_course(t: float) -> void:
	var trav: Node = _player.get("traversal")
	var go := Vector3(1.0, 0.0, 0.0)
	if trav.is_hanging() and _course_hung < 0.0:
		_course_hung = t
	if _course_hung < 0.0:
		_player.set("wish", go if t > 0.3 else Vector3())
		_player.set("want_sprint", t > 0.3)
	elif t < _course_hung + 1.0:
		_player.set("wish", Vector3())
		_player.set("want_sprint", false)
	else:
		# up onto the top from the hang, then still on it
		var on_top: bool = _player.global_position.y > 2.0 and not trav.is_active()
		_player.set("wish", Vector3() if on_top else go)
		_player.set("want_walk", true)


## The "hang" program: a run at CasaCrema's front (the jump at the wall catches its first hold), then hanging still
## (HANG_STILL s), along the hold to the left and back to the right, still again, then up the front from hold to hold.
const HANG_STILL := 4.0

func _drive(t: float) -> void:
	var trav: Node = _player.get("traversal")
	var into := Vector3(0.0, 0.0, -1.0)
	_player.set("want_sprint", false)
	if t < 2.2 and not trav.is_hanging():
		_player.set("wish", into)
		_player.set("want_sprint", t > 0.2)
		return
	if not trav.is_hanging() and not trav.is_transferring() and t < 2.2:
		return
	var u := t - 2.2
	var w := Vector3()
	if u < HANG_STILL:
		w = Vector3()
	elif u < HANG_STILL + 1.6:
		w = Vector3(-1.0, 0.0, 0.0) # the body's left facing the wall is the world's -x here
	elif u < HANG_STILL + 3.2:
		w = Vector3(1.0, 0.0, 0.0)
	elif u < HANG_STILL * 2.0 + 3.2:
		w = Vector3()
	else:
		w = into
	_player.set("wish", w)


func _on_posed() -> void:
	var xf := _skel.get_global_transform_interpolated()
	for n in BONES:
		_posed[n] = xf * _skel.get_bone_global_pose(_skel.find_bone(n)).origin


func _bone(n: String) -> Vector3:
	if _posed.has(n):
		return _posed[n]
	return _skel.global_transform * _skel.get_bone_global_pose(_skel.find_bone(n)).origin


## Beside the body along the wall it climbs (the traversal's inward), or behind and below it; eased (no shake of its own).
func _place_camera(p_first: bool) -> void:
	var trav: Node = _player.get("traversal")
	var hips := _bone("Hips")
	var inward := -_player.global_basis.z
	var hints: Dictionary = trav.get_pose_targets()
	if hints.get("active", false) and hints.has("inward"):
		inward = hints["inward"]
	inward.y = 0.0
	inward = inward.normalized() if inward.length() > 0.1 else Vector3(0, 0, -1)
	var side := Vector3.UP.cross(inward).normalized()
	var want: Vector3
	if _cam_mode == "back":
		want = hips - inward * _dist + Vector3.UP * -0.4 + side * 0.6
	else:
		want = hips - inward * (_dist * 0.55) + side * _dist + Vector3.UP * 0.3
	var rate := 1.0 if p_first else 0.12
	_cam_pos = _cam_pos.lerp(want, rate)
	_look = _look.lerp(hips + Vector3.UP * 0.1, 1.0 if p_first else 0.2)
	_cam.global_position = _cam_pos
	_cam.look_at(_look, Vector3.UP)


func _record(p_frame: int) -> void:
	var trav: Node = _player.get("traversal")
	var hints: Dictionary = trav.get_pose_targets()
	var r := {"i": p_frame, "state": String(_player.get("state")), "hang": trav.is_hanging(), "move": trav.is_transferring(),
			"climb": trav.is_climbing(), "mantle": trav.is_active(), "body": _v(_player.global_position),
			"hips": _v(_bone("Hips"))}
	# Each limb: its joints, and the plane it bends in (the normal of upper, joint, end) — a flip of that normal from one
	# frame to the next is an elbow or knee turning over.
	var limbs := []
	for l in LIMBS:
		var a := _bone(l[0])
		var b := _bone(l[1])
		var c := _bone(l[2])
		limbs.append({"a": _v(a), "b": _v(b), "c": _v(c), "n": _v((b - a).cross(c - b).normalized())})
	r["limbs"] = limbs
	if hints.get("active", false):
		for k in ["hand_left", "hand_right", "foot_left", "foot_right"]:
			r[k] = _v(hints[k])
		r["hand_weights"] = [hints["hand_weights"].x, hints["hand_weights"].y]
		r["foot_weights"] = [hints["foot_weights"].x, hints["foot_weights"].y]
		r["hand_grips"] = [hints["hand_grips"].x, hints["hand_grips"].y]
	_frames.append(r)


func _v(p: Vector3) -> Array:
	return [snappedf(p.x, 0.0001), snappedf(p.y, 0.0001), snappedf(p.z, 0.0001)]
