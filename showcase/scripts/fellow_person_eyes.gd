extends SkeletonModifier3D
## Milestone 235 (FellowPerson): a person's eyes turned where its face looks. The people's rig has LeftEye and RightEye
## bones under the head (make_characters.py: at the eyeballs' centres, each eyeball wholly weighted to its own); the
## mannequin has none, so RetargetModifier3D leaves them alone. The gaze is the face's (FellowFaceAnimator: its target,
## clamped to the eyes' range, eased, with saccades; the eyelids' look shapes from the same angles): this modifier, on
## the mannequin's skeleton after the person's RetargetModifier3D (in the same update, the head's pose handed over),
## tells the face how far the head has turned towards the target (`head_turn`: the face's angles are measured from the
## body, which does not move with the head's bone), so its angles are the eyes' in the head, and turns each eye by them
## from its rest (in the head), the eyes turned in towards a near target.

const EYE_SPACING := 0.064 # m (an adult's eyes' centres)

var person: Skeleton3D
var face: Node # FellowFaceAnimator
var body_xf := Transform3D() # the face's mesh in the person's skeleton's space
var head := -1
var eyes: Array[int] = [] # left, right
var head_rest := Basis() # the head's global rest
var eye_rest: Array[Basis] = [] # the eyes' global rests


func setup(p_person: Skeleton3D, p_face: Node) -> bool:
	person = p_person
	face = p_face
	head = person.find_bone("Head")
	for n in ["LeftEye", "RightEye"]:
		var b := person.find_bone(n)
		if b < 0 or head < 0 or person.get_bone_parent(b) != head:
			eyes.clear()
			return false
		eyes.append(b)
	head_rest = person.get_bone_global_rest(head).basis
	for e in eyes:
		eye_rest.append(person.get_bone_global_rest(e).basis)
	# (the face's eye_center in its mesh's space: the mesh's place under the skeleton, without the tree)
	var mesh := face.get_node_or_null(face.get("mesh_path")) as Node3D
	var n: Node = mesh
	while n != null and n != person:
		if n is Node3D:
			body_xf = (n as Node3D).transform * body_xf
		n = n.get_parent()
	var mid := (person.get_bone_global_rest(eyes[0]).origin + person.get_bone_global_rest(eyes[1]).origin) * 0.5
	face.set("eye_center", body_xf.affine_inverse() * mid)
	face.set("eye_spacing", EYE_SPACING)
	return true


static func _angles(p_d: Vector3) -> Vector2:
	return Vector2(rad_to_deg(atan2(p_d.x, p_d.z)), rad_to_deg(atan2(p_d.y, Vector2(p_d.x, p_d.z).length())))


func _process_modification_with_delta(_delta: float) -> void:
	if person == null or eyes.is_empty() or face == null or not face.can_process():
		return
	var gh := person.get_bone_global_pose(head)
	var turn := gh.basis.orthonormalized() * head_rest.orthonormalized().inverse() # the head's turn from its rest
	var tp: NodePath = face.get("look_target_path")
	var target := face.get_node_or_null(tp) as Node3D if not tp.is_empty() else null
	var vergence := 0.0
	if target and target.is_inside_tree() and person.is_inside_tree():
		var at := person.global_transform.affine_inverse() * target.global_position
		var mid := (person.get_bone_global_pose(eyes[0]).origin + person.get_bone_global_pose(eyes[1]).origin) * 0.5
		var d := at - mid
		if d.length() > 1e-3:
			# The face's angles (from its mesh, at its eye_center) less the head's: the eyes' in the head.
			var from_body := _angles(body_xf.basis.inverse() * (at - body_xf * (face.get("eye_center") as Vector3)))
			var from_head := _angles(turn.inverse() * d)
			face.call("set_head_turn", from_body - from_head)
			vergence = rad_to_deg(atan2(EYE_SPACING * 0.5, d.length()))
	else:
		face.call("set_head_turn", Vector2())
	var g: Vector2 = face.call("get_gaze_angles")
	var range_h := float(face.get("gaze_range_horizontal"))
	for i in 2:
		# (the left eye turned in — to the character's right, -yaw — by the vergence, the right one to its left)
		var yaw := clampf(g.x - vergence if i == 0 else g.x + vergence, -range_h, range_h)
		var rot := Basis(Vector3.UP, deg_to_rad(yaw)) * Basis(Vector3.RIGHT, deg_to_rad(-g.y))
		var want := turn * rot * eye_rest[i]
		person.set_bone_pose_rotation(eyes[i], (gh.basis.orthonormalized().inverse() * want).get_rotation_quaternion())
