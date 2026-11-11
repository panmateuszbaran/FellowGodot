extends Node3D
## FellowEngine's image regression suite: FellowTraversal3D and FellowTraversalPose3D (milestone 72). Three bodies, each
## a capsule with a test mannequin (a Skeleton3D posed by a FellowTraversalPose3D), in front of a 1.2 m ledge, a 2 m ledge
## and a thin wall; each mantle is stopped partway (its traversal disabled from processing at a set progress, its pose
## held), so the captured frame does not depend on how many frames the capture runs: the hands on the edge, the hips
## lowered, the feet on the wall's face or tucked over the wall.

# [height, depth, the physics frame its mantle starts, the progress it is held at]
const LANES := [[1.2, 2.0, 4, 0.4], [2.0, 2.0, 4, 0.33], [1.0, 0.25, 4, 0.42]]

var _bodies: Array[CharacterBody3D] = []
var _frame := 0


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.85
	return m


func _box(pos: Vector3, size: Vector3, color: Color) -> void:
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
	add_child(b)


func _bone(sk: Skeleton3D, name: String, parent: String, offset: Vector3) -> void:
	var i := sk.add_bone(name)
	if parent != "":
		sk.set_bone_parent(i, sk.find_bone(parent))
	sk.set_bone_rest(i, Transform3D(Basis(), offset))
	sk.set_bone_pose_position(i, offset)


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
	var head := BoneAttachment3D.new()
	head.bone_name = "Head"
	sk.add_child(head)
	var hm := MeshInstance3D.new()
	var sph := SphereMesh.new()
	sph.radius = 0.11
	sph.height = 0.22
	hm.mesh = sph
	hm.material_override = mat
	hm.position = Vector3(0, 0.1, 0)
	head.add_child(hm)
	return sk


func _ready() -> void:
	_box(Vector3(0, -0.5, -2), Vector3(14, 1, 12), Color(0.38, 0.42, 0.36))
	for i in LANES.size():
		var x := (float(i) - 1.0) * 2.6
		var h: float = LANES[i][0]
		var d: float = LANES[i][1]
		_box(Vector3(x, h * 0.5, -0.5 - d * 0.5), Vector3(1.6, h, d), Color(0.72, 0.7, 0.66))
		var body := CharacterBody3D.new()
		body.position = Vector3(x, 0, 0)
		var cs := CollisionShape3D.new()
		var cap := CapsuleShape3D.new()
		cap.radius = 0.35
		cap.height = 1.8
		cs.shape = cap
		cs.position = Vector3(0, 0.9, 0)
		body.add_child(cs)
		var t := FellowTraversal3D.new()
		t.name = "Traversal"
		t.auto_mantle = false
		body.add_child(t)
		var sk := _mannequin(body, Color(0.25, 0.45, 0.8))
		sk.add_child(FellowTraversalPose3D.new())
		add_child(body)
		_bodies.append(body)
	($Camera as Camera3D).look_at_from_position(Vector3(5.2, 2.6, 2.4), Vector3(0.2, 1.1, -0.9))


func _physics_process(_delta: float) -> void:
	_frame += 1
	for i in _bodies.size():
		var body := _bodies[i]
		var t: FellowTraversal3D = body.get_node("Traversal")
		if t.is_active():
			if t.get_progress() >= LANES[i][3]:
				t.process_mode = Node.PROCESS_MODE_DISABLED
			continue
		body.velocity = Vector3(0, -1, 0)
		body.move_and_slide()
		if _frame == LANES[i][2]:
			t.try_mantle(Vector3(0, 0, -1))
