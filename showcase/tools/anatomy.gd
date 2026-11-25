extends RefCounted
## Milestone 202: the showcase character's joints against a person's range of motion — read from the posed skeleton (the
## bones as drawn, after every modifier), each joint's angles in anatomical terms, and the limbs through the body.
## Used by tools/anim_quality.gd (each segment's share of frames outside the range, its worst moments); the limits are
## an adult's active range of motion (AAOS, with a few degrees' slack), the measures:
##   spine (the upper chest against the hips) and neck (the head against the upper chest): flexion forwards (+), the
##     bend to the left (+), the turn to the left (+) — from the rest pose's;
##   shoulder: how far the upper arm goes behind the chest's plane and across its middle;
##   elbow, knee: the hinge's bend (+: flexion — the forearm forwards, the shin back — from the bone's own frame, so the
##     upper arm's or thigh's roll turns it as a person's does; −: hyperextension) and its bend to the side (a hinge has
##     none);
##   forearm: the palm's turn about the forearm with the elbow bent (0: full supination — the palm towards the shoulder,
##     90: neutral, 180: full pronation; the palm turned outwards past supination is out of a person's range);
##   wrist: extension (+) and flexion (−), radial (+) and ulnar (−) deviation;
##   hip: flexion (+) and extension (−), abduction (+) and adduction (−);
##   ankle: plantarflexion (+) and dorsiflexion (−), from the rest pose's;
##   the body's own volume: an elbow or a wrist inside the trunk, the knees or the ankles through each other.

const LIMITS := {
	"spine_flex": [-40.0, 95.0], "spine_bend": [-45.0, 45.0], "spine_turn": [-55.0, 55.0],
	"neck_flex": [-70.0, 60.0], "neck_bend": [-45.0, 45.0], "neck_turn": [-85.0, 85.0],
	"shoulder_behind": [0.0, 65.0], "shoulder_across": [0.0, 50.0],
	"elbow_flex": [-10.0, 150.0], "elbow_side": [0.0, 25.0],
	"forearm_turn": [-20.0, 200.0],
	"wrist_ext": [-85.0, 75.0], "wrist_dev": [-40.0, 25.0],
	"hip_flex": [-30.0, 140.0], "hip_abd": [-30.0, 50.0],
	"knee_flex": [-10.0, 155.0], "knee_side": [0.0, 20.0],
	"ankle_pf": [-30.0, 55.0],
	"arm_in_trunk": [0.08, 99.0], "knees_apart": [0.07, 99.0], "ankles_apart": [0.06, 99.0],
}
const SIDED := ["shoulder_behind", "shoulder_across", "elbow_flex", "elbow_side", "forearm_turn", "wrist_ext", "wrist_dev",
		"hip_flex", "hip_abd", "knee_flex", "knee_side", "ankle_pf"]

var sk: Skeleton3D
var b := {}
var ok := false
var rest_spine := Vector3()
var rest_neck := Vector3()
var flex_dir := {} # per upper arm / thigh: the hinge's bend in its own frame
var palm_local := {} # per hand: the palm's normal in its own frame
var hand_rest := {} # per hand: its rest rotation against the forearm's
var hand_frame := {} # per hand, in its own frame: [its axis (to the middle finger's knuckle), its thumb's side, its back]
var hip_rest := {}
var ankle_rest := {}


func _init(p_skeleton: Skeleton3D) -> void:
	sk = p_skeleton
	for n in ["Hips", "UpperChest", "Neck", "Head"]:
		b[n] = sk.find_bone(n)
	for s in ["Left", "Right"]:
		for n in ["UpperArm", "LowerArm", "Hand", "IndexProximal", "MiddleProximal", "LittleProximal", "UpperLeg", "LowerLeg", "Foot", "Toes"]:
			b[s + n] = sk.find_bone(s + n)
	ok = true
	for k in b:
		if b[k] < 0:
			ok = false
	if not ok:
		return
	var P := _rest_basis("Hips")
	var T := _rest_basis("UpperChest")
	var H := _rest_basis("Head")
	rest_spine = _angles(P.inverse() * T)
	rest_neck = _angles(T.inverse() * H)
	for s in ["Left", "Right"]:
		# (the rest pose a T: the elbow bends forwards, the knee back; the palms face down)
		flex_dir[s + "UpperArm"] = _flex_local(_rest_basis(s + "UpperArm"), Vector3(0, 0, 1))
		flex_dir[s + "UpperLeg"] = _flex_local(_rest_basis(s + "UpperLeg"), Vector3(0, 0, -1))
		palm_local[s] = _rest_basis(s + "Hand").inverse() * Vector3(0, -1, 0)
		hand_rest[s] = _rest_basis(s + "LowerArm").inverse() * _rest_basis(s + "Hand")
		var hb := _rest_basis(s + "Hand")
		var ax := (_rest_pos(s + "MiddleProximal") - _rest_pos(s + "Hand")).normalized()
		var th := _rest_pos(s + "IndexProximal") - _rest_pos(s + "LittleProximal")
		th = (th - ax * th.dot(ax)).normalized()
		var back := th.cross(ax).normalized() * (1.0 if s == "Left" else -1.0)
		hand_frame[s] = [hb.inverse() * ax, hb.inverse() * th, hb.inverse() * back]
		var t: Vector3 = P.inverse() * _rest_basis(s + "UpperLeg").y
		hip_rest[s] = Vector2(rad_to_deg(atan2(t.z, -t.y)), rad_to_deg(asin(clampf(t.x * (1.0 if s == "Left" else -1.0), -1.0, 1.0))))
		ankle_rest[s] = _ankle_angle(true, s)


func _rest_basis(n: String) -> Basis:
	return sk.get_bone_global_rest(b[n]).basis.orthonormalized()


func _basis(n: String) -> Basis:
	return sk.get_bone_global_pose(b[n]).basis.orthonormalized()


func _pos(n: String) -> Vector3:
	return sk.get_bone_global_pose(b[n]).origin


func _rest_pos(n: String) -> Vector3:
	return sk.get_bone_global_rest(b[n]).origin


## A joint's bend direction (p_world in the rest pose) in its upper bone's own frame, square to the bone.
func _flex_local(p_rest: Basis, p_world: Vector3) -> Vector3:
	var l := p_rest.inverse() * p_world
	l.y = 0.0
	return l.normalized()


## The flexion forwards, the bend to the left and the turn to the left (degrees) of a rotation against its parent's
## (the bend: the up axis's lean out of the middle plane, whatever the flexion; the turn: the forward axis's about up).
func _angles(q: Basis) -> Vector3:
	var y := q * Vector3(0, 1, 0)
	var z := q * Vector3(0, 0, 1)
	return Vector3(rad_to_deg(atan2(y.z, y.y)), rad_to_deg(asin(clampf(y.x, -1.0, 1.0))), rad_to_deg(atan2(z.x, z.z)))


func _ankle_angle(p_rest: bool, s: String) -> float:
	var knee := _rest_pos(s + "LowerLeg") if p_rest else _pos(s + "LowerLeg")
	var ankle := _rest_pos(s + "Foot") if p_rest else _pos(s + "Foot")
	var toe := _rest_pos(s + "Toes") if p_rest else _pos(s + "Toes")
	return rad_to_deg((ankle - knee).angle_to(toe - ankle))


## The hinge's bend: [flexion (signed), its bend to the side] in degrees.
func _hinge(p_upper: String, p_lower: String, p_end: String) -> Vector2:
	var U := _basis(p_upper)
	var f := (_pos(p_end) - _pos(p_lower)).normalized()
	var fl := U.inverse() * f
	var angle := rad_to_deg(acos(clampf(fl.y, -1.0, 1.0)))
	var bend := Vector3(fl.x, 0.0, fl.z)
	if bend.length() < 1e-5:
		return Vector2(0.0, 0.0)
	var fd: Vector3 = flex_dir[p_upper]
	var sd := Vector3(0, 1, 0).cross(fd)
	var az := atan2(bend.dot(sd), bend.dot(fd))
	return Vector2(angle * cos(az), absf(angle * sin(az)))


func _seg_dist(p: Vector3, a: Vector3, c: Vector3) -> float:
	var ab := c - a
	var t := clampf((p - a).dot(ab) / maxf(ab.length_squared(), 1e-8), 0.0, 1.0)
	return p.distance_to(a + ab * t)


## This frame's measures {name: value} (sided ones as "<name>_l", "<name>_r"; the forearm's only with the elbow bent).
func measure() -> Dictionary:
	var m := {}
	if not ok:
		return m
	var P := _basis("Hips")
	var T := _basis("UpperChest")
	var H := _basis("Head")
	var sp := _angles(P.inverse() * T) - rest_spine
	m["spine_flex"] = sp.x
	m["spine_bend"] = sp.y
	m["spine_turn"] = wrapf(sp.z, -180.0, 180.0)
	var nk := _angles(T.inverse() * H) - rest_neck
	m["neck_flex"] = nk.x
	m["neck_bend"] = nk.y
	m["neck_turn"] = wrapf(nk.z, -180.0, 180.0)
	var trunk_a := _pos("Hips")
	var trunk_b := _pos("UpperChest")
	var arm_in := 99.0
	for s in ["Left", "Right"]:
		var x := "_l" if s == "Left" else "_r"
		var lat := 1.0 if s == "Left" else -1.0
		# The shoulder: the upper arm in the chest's frame.
		var d: Vector3 = T.inverse() * (_pos(s + "LowerArm") - _pos(s + "UpperArm")).normalized()
		m["shoulder_behind" + x] = rad_to_deg(asin(clampf(-d.z, 0.0, 1.0)))
		# (across the middle: in front of the chest as far as the limit, beside or behind it half as far; overhead, the hands
		# together on a hold, not limited)
		m["shoulder_across" + x] = 0.0 if d.y > 0.5 else rad_to_deg(asin(clampf(-d.x * lat, 0.0, 1.0))) * (1.0 if d.z > 0.3 else 2.0)
		var el := _hinge(s + "UpperArm", s + "LowerArm", s + "Hand")
		m["elbow_flex" + x] = el.x
		m["elbow_side" + x] = el.y if absf(el.x) > 20.0 else 0.0
		# The forearm's turn (the elbow bent): the palm against the elbow's inner side, about the forearm.
		var a := (_pos(s + "LowerArm") - _pos(s + "UpperArm")).normalized()
		var f := (_pos(s + "Hand") - _pos(s + "LowerArm")).normalized()
		if el.x > 30.0:
			var inner := -a - f * (-a).dot(f)
			var medial := a.cross(f) * lat
			var p: Vector3 = _basis(s + "Hand") * (palm_local[s] as Vector3)
			p = p - f * p.dot(f)
			if inner.length() > 1e-4 and medial.length() > 1e-4 and p.length() > 1e-4:
				m["forearm_turn" + x] = rad_to_deg(atan2(p.dot(medial.normalized()), p.dot(inner.normalized())))
		# The wrist: the hand's axis against where the wrist at rest would have it — towards its back, its thumb's side.
		var r0: Basis = _basis(s + "LowerArm") * (hand_rest[s] as Basis)
		var hf: Array = hand_frame[s]
		var h: Vector3 = _basis(s + "Hand") * (hf[0] as Vector3)
		var along: float = h.dot(r0 * (hf[0] as Vector3))
		m["wrist_ext" + x] = rad_to_deg(atan2(h.dot(r0 * (hf[2] as Vector3)), along))
		m["wrist_dev" + x] = rad_to_deg(atan2(h.dot(r0 * (hf[1] as Vector3)), along))
		# The hip: the thigh in the pelvis's frame.
		var t: Vector3 = P.inverse() * (_pos(s + "LowerLeg") - _pos(s + "UpperLeg")).normalized()
		var hr: Vector2 = hip_rest[s]
		m["hip_flex" + x] = rad_to_deg(atan2(t.z, -t.y)) - hr.x
		# (abduction: the thigh out of the middle plane, whatever its flexion)
		m["hip_abd" + x] = rad_to_deg(asin(clampf(t.x * lat, -1.0, 1.0))) - hr.y
		var kn := _hinge(s + "UpperLeg", s + "LowerLeg", s + "Foot")
		m["knee_flex" + x] = kn.x
		m["knee_side" + x] = kn.y if absf(kn.x) > 20.0 else 0.0
		m["ankle_pf" + x] = _ankle_angle(false, s) - float(ankle_rest[s])
		arm_in = minf(arm_in, minf(_seg_dist(_pos(s + "LowerArm"), trunk_a, trunk_b), _seg_dist(_pos(s + "Hand"), trunk_a, trunk_b)))
	m["arm_in_trunk"] = arm_in
	m["knees_apart"] = _pos("LeftLowerLeg").distance_to(_pos("RightLowerLeg"))
	m["ankles_apart"] = _pos("LeftFoot").distance_to(_pos("RightFoot"))
	return m


## How far a measure is outside its range (0: within; degrees, or metres for the body's volume).
static func beyond(p_name: String, p_value: float) -> float:
	var key := p_name
	if key.ends_with("_l") or key.ends_with("_r"):
		key = key.substr(0, key.length() - 2)
	var lim: Array = LIMITS.get(key, [])
	if lim.is_empty():
		return 0.0
	if key == "forearm_turn":
		# (the palm turned outwards past supination: between −160° and the limit's −20°)
		return maxf(0.0, minf(p_value - (-160.0), float(lim[0]) - p_value)) if p_value < float(lim[0]) and p_value > -160.0 else 0.0
	# (half a degree's slack: a joint held at its limit is within it)
	var over := maxf(0.0, maxf(float(lim[0]) - p_value, p_value - float(lim[1])))
	return over if over > (0.005 if key in ["arm_in_trunk", "knees_apart", "ankles_apart"] else 0.5) else 0.0
