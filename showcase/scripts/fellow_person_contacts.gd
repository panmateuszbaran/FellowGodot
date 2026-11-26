extends SkeletonModifier3D
## Milestone 233 (FellowPerson): the player's hands and feet put where the mannequin's are. A modifier on the mannequin's
## skeleton right after the person's RetargetModifier3D (so in the same update, after the pose was handed over: a
## modifier on the person's own skeleton runs before the hand-over and is overwritten by it): for each limb a two-bone IK
## on the person's skeleton — its upper bone and its middle one turned so that the limb's end reaches the mannequin's,
## the elbow or knee in the plane of the mannequin's; the hand or foot keeps its turn in the world. A limb shorter than
## the reach is stretched up to MAX_STRETCH (the mannequin's arms are long: hanging from a ledge the person's hands fell
## 3-7 cm short).

const MAX_STRETCH := 1.1

var person: Skeleton3D
var limbs: Array = [] # [upper, middle, end] of the person, [middle, end] of the mannequin
var last_error := 0.0 # (tools) the largest distance left between a limb's end and its target in the last update, m
var last_short := 0.0 # (tools) of it, how far the farthest target was out of the limb's reach, m


func setup(p_person: Skeleton3D, p_mannequin: Skeleton3D) -> void:
	person = p_person
	for l in [["LeftUpperArm", "LeftLowerArm", "LeftHand"], ["RightUpperArm", "RightLowerArm", "RightHand"],
			["LeftUpperLeg", "LeftLowerLeg", "LeftFoot"], ["RightUpperLeg", "RightLowerLeg", "RightFoot"]]:
		var ids := [person.find_bone(l[0]), person.find_bone(l[1]), person.find_bone(l[2]), p_mannequin.find_bone(l[1]), p_mannequin.find_bone(l[2])]
		if not ids.has(-1):
			limbs.append(ids)


func _process_modification_with_delta(_delta: float) -> void:
	var skel := get_skeleton()
	if skel == null or person == null:
		return
	# The mannequin's model space into the person's.
	var to_person := person.global_transform.affine_inverse() * skel.global_transform
	var scale := person.global_transform.basis.get_scale().x
	last_error = 0.0
	last_short = 0.0
	for l in limbs:
		var upper: int = l[0]
		var middle: int = l[1]
		var end: int = l[2]
		var target := to_person * skel.get_bone_global_pose(l[4]).origin
		var pole := to_person * skel.get_bone_global_pose(l[3]).origin
		var gu := person.get_bone_global_pose(upper)
		var gm := person.get_bone_global_pose(middle)
		var ge := person.get_bone_global_pose(end)
		var a := gu.origin.distance_to(gm.origin)
		var b := gm.origin.distance_to(ge.origin)
		var to_t := target - gu.origin
		if to_t.length_squared() < 1e-8 or a < 1e-4 or b < 1e-4:
			continue
		# Stretched (both bones alike) when the target is beyond the limb's length.
		var k := clampf(to_t.length() / (a + b), 1.0, MAX_STRETCH)
		if k > 1.0:
			person.set_bone_pose_position(middle, person.get_bone_pose_position(middle) * k)
			person.set_bone_pose_position(end, person.get_bone_pose_position(end) * k)
			var gm0 := gm.origin
			gm.origin = gu.origin + (gm0 - gu.origin) * k
			ge.origin = gm.origin + (ge.origin - gm0) * k
			a *= k
			b *= k
		var c := clampf(to_t.length(), absf(a - b) + 1e-4, a + b - 1e-4)
		var d := to_t.normalized()
		var u := (pole - gu.origin) - d * (pole - gu.origin).dot(d)
		if u.length_squared() < 1e-8:
			u = (gm.origin - gu.origin) - d * (gm.origin - gu.origin).dot(d)
			if u.length_squared() < 1e-8:
				continue
		u = u.normalized()
		var cos_a := clampf((a * a + c * c - b * b) / (2.0 * a * c), -1.0, 1.0)
		var elbow := gu.origin + a * (cos_a * d + sqrt(1.0 - cos_a * cos_a) * u)
		var reach := gu.origin + d * c
		# The upper bone turned to the new elbow, the middle one to the end.
		var q1 := Quaternion((gm.origin - gu.origin).normalized(), (elbow - gu.origin).normalized())
		var new_u := Basis(q1) * gu.basis
		var end_after := gu.origin + Basis(q1) * (ge.origin - gu.origin)
		var q2 := Quaternion((end_after - elbow).normalized(), (reach - elbow).normalized())
		var new_m := Basis(q2) * (Basis(q1) * gm.basis)
		var parent := person.get_bone_parent(upper)
		var gp := person.get_bone_global_pose(parent).basis if parent >= 0 else Basis()
		person.set_bone_pose_rotation(upper, (gp.inverse() * new_u).get_rotation_quaternion())
		person.set_bone_pose_rotation(middle, (new_u.inverse() * new_m).get_rotation_quaternion())
		person.set_bone_pose_rotation(end, (new_m.inverse() * ge.basis).get_rotation_quaternion())
		last_error = maxf(last_error, person.get_bone_global_pose(end).origin.distance_to(target) * scale)
		last_short = maxf(last_short, (to_t.length() - c) * scale)
