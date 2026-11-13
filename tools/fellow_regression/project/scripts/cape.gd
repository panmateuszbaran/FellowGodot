extends Node3D
## FellowEngine's image regression suite: cloth pinned to bones (milestone 76). A body of capsules on a Skeleton3D
## (hips, spine, chest, neck) walking an arc while its chest twists; a cape pinned along its top edge to the chest bone,
## colliding with the capsules on the hips and the chest; simulated from the scene's start at the capture's fixed frame
## rate: the same folds every run.

var _walker: Node3D
var _skeleton: Skeleton3D
var _frame := 0


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.85
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	return m


func _ready() -> void:
	var ground := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(30, 30)
	ground.mesh = pm
	ground.material_override = _material(Color(0.38, 0.42, 0.36))
	add_child(ground)
	_walker = Node3D.new()
	add_child(_walker)
	_skeleton = Skeleton3D.new()
	_walker.add_child(_skeleton)
	for b in [["Hips", "", Vector3(0, 0.95, 0)], ["Spine", "Hips", Vector3(0, 0.15, 0)], ["Chest", "Spine", Vector3(0, 0.25, 0)], ["Neck", "Chest", Vector3(0, 0.25, 0)]]:
		var i := _skeleton.add_bone(b[0])
		if b[1] != "":
			_skeleton.set_bone_parent(i, _skeleton.find_bone(b[1]))
		_skeleton.set_bone_rest(i, Transform3D(Basis(), b[2]))
		_skeleton.set_bone_pose_position(i, b[2])
	var body := _material(Color(0.62, 0.62, 0.65))
	var shapes := []
	for b in [["Hips", 0.2, 0.55], ["Chest", 0.21, 0.6], ["Neck", 0.11, 0.3]]:
		var at := BoneAttachment3D.new()
		at.bone_name = b[0]
		_skeleton.add_child(at)
		var cs := CollisionShape3D.new()
		var cap := CapsuleShape3D.new()
		cap.radius = b[1]
		cap.height = b[2]
		cs.shape = cap
		cs.position = Vector3(0, 0.05, 0)
		at.add_child(cs)
		var mi := MeshInstance3D.new()
		var cm := CapsuleMesh.new()
		cm.radius = b[1]
		cm.height = b[2]
		mi.mesh = cm
		mi.material_override = body
		mi.position = cs.position
		at.add_child(mi)
		shapes.append(cs)
	var cape := FellowCloth3D.new()
	cape.size = Vector2(0.8, 1.1)
	cape.resolution = 16
	cape.pin_mode = FellowCloth3D.PIN_TOP_EDGE
	cape.material = _material(Color(0.6, 0.1, 0.12))
	cape.position = Vector3(-0.4, 1.55, 0.24)
	cape.collide_with_world = false
	cape.lod_distance = 0.0
	_walker.add_child(cape)
	cape.skeleton_path = cape.get_path_to(_skeleton)
	cape.pin_bones = PackedStringArray(["Chest"])
	var paths := []
	for cs in shapes:
		paths.append(cape.get_path_to(cs))
	cape.colliders = paths
	($Camera as Camera3D).look_at_from_position(to_global(Vector3(1.4, 1.9, 1.2)), to_global(Vector3(-0.3, 1.1, -1.2)))


func _physics_process(_delta: float) -> void:
	_frame += 1
	var t := _frame / 60.0
	# An arc of 3 m radius at 1.8 m/s, facing the way it goes; the chest twisting 30 degrees each way.
	var a := t * 1.8 / 3.0
	_walker.position = Vector3(cos(a) * 3.0 - 3.0, 0, -sin(a) * 3.0)
	_walker.look_at(_walker.global_position + Vector3(-sin(a), 0, -cos(a)), Vector3.UP)
	_skeleton.set_bone_pose_rotation(_skeleton.find_bone("Chest"), Quaternion(Vector3.UP, deg_to_rad(30.0) * sin(t * TAU * 1.5)))
