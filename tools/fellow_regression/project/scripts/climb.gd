extends Node3D
## FellowEngine's image regression suite: hanging and ladders (milestone 75). Two bodies, each a capsule with a test
## mannequin (a Skeleton3D posed by a FellowTraversalPose3D): one hanging from a 2.5 m ledge with a wall rising behind it
## (no room to climb), moving left along it; one on a FellowLadder3D against a 4 m platform, climbing. Each traversal is
## stopped at a set physics frame (disabled from processing, its pose held), so the captured frame does not depend on
## how many frames the capture runs: the hands on the edge and the feet on the wall, the hands and feet on the rungs.

var _hanger: CharacterBody3D
var _climber: CharacterBody3D
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


func _body(pos: Vector3, color: Color) -> CharacterBody3D:
	var body := CharacterBody3D.new()
	body.position = pos
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
	t.ladders = false
	body.add_child(t)
	var sk := _mannequin(body, color)
	sk.add_child(FellowTraversalPose3D.new())
	add_child(body)
	return body


func _ready() -> void:
	_box(Vector3(0, -0.5, -2), Vector3(14, 1, 12), Color(0.38, 0.42, 0.36))
	# The ledge (0.4 m deep) and the wall behind it.
	_box(Vector3(-1.6, 1.25, -2.2), Vector3(2.4, 2.5, 0.4), Color(0.72, 0.7, 0.66))
	_box(Vector3(-1.6, 3.0, -2.9), Vector3(2.4, 6.0, 1.0), Color(0.6, 0.6, 0.66))
	# The platform and its ladder.
	_box(Vector3(1.8, 2.0, -3.5), Vector3(2.4, 4.0, 3.0), Color(0.72, 0.7, 0.66))
	var ladder := FellowLadder3D.new()
	ladder.height = 4.0
	ladder.position = Vector3(1.8, 0, -1.97)
	add_child(ladder)
	var wood := _material(Color(0.5, 0.36, 0.22))
	for x in [-0.25, 0.25]:
		var rail := MeshInstance3D.new()
		var rm := BoxMesh.new()
		rm.size = Vector3(0.05, 4.0, 0.05)
		rail.mesh = rm
		rail.material_override = wood
		rail.position = Vector3(x, 2.0, 0)
		ladder.add_child(rail)
	var y := ladder.rung_offset
	while y <= 4.0:
		var rung := MeshInstance3D.new()
		var cm := BoxMesh.new()
		cm.size = Vector3(0.5, 0.035, 0.035)
		rung.mesh = cm
		rung.material_override = wood
		rung.position = Vector3(0, y, 0)
		ladder.add_child(rung)
		y += ladder.rung_spacing
	# The hanger where hanging puts it (the hands 2.05 m over the feet at the edge, 0.43 m off the face), the climber in
	# front of the ladder 1 m up it.
	_hanger = _body(Vector3(-1.1, 0.45, -1.57), Color(0.25, 0.45, 0.8))
	_climber = _body(Vector3(1.8, 1.0, -1.4), Color(0.8, 0.45, 0.25))
	($Camera as Camera3D).look_at_from_position(Vector3(4.6, 2.8, 3.6), Vector3(0.0, 1.6, -1.8))


func _physics_process(_delta: float) -> void:
	_frame += 1
	var th: FellowTraversal3D = _hanger.get_node("Traversal")
	var tc: FellowTraversal3D = _climber.get_node("Traversal")
	if _frame == 3:
		th.try_hang(Vector3(0, 0, -1))
		th.set_intent(Vector3(-1, 0, 0))
		tc.set_intent(Vector3(0, 0, -1))
		tc.try_ladder()
	if _frame == 34:
		th.process_mode = Node.PROCESS_MODE_DISABLED
		tc.process_mode = Node.PROCESS_MODE_DISABLED
