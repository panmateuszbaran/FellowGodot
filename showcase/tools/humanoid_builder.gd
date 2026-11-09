extends RefCounted
## The showcase's character, built in code (no third-party asset): a humanoid of 52 bones (spine, head, arms with five
## three-bone fingers, legs; Crowd Lab's skeleton, benchmarks/project/scenes/crowd_lab.gd) skinned with tubes around its
## bones, coloured by region (skin, jacket, trousers, boots), and its clips: idle, walk, run and a falling pose. The
## builder (build_showcase.gd) saves the mesh, the skin and the clips as resources; the player scene uses them.
## The character faces +Z, its feet at y = 0.

const RINGS := 9
const RADIAL := 14
const KEY_FPS := 30.0
const CLIPS := ["idle", "walk", "run", "fall"]
const CLIP_LENGTH := [3.0, 1.0, 0.62, 1.0]
## Ground speed of the walk and run clips (metres per second): the player scales their playback to its own speed.
const WALK_CLIP_SPEED := 1.4
const RUN_CLIP_SPEED := 3.6

const SKIN := Color(0.86, 0.66, 0.52)
const JACKET := Color(0.55, 0.13, 0.1)
const SHIRT := Color(0.86, 0.82, 0.72)
const TROUSERS := Color(0.16, 0.2, 0.27)
const BOOTS := Color(0.2, 0.13, 0.08)
const HAIR := Color(0.12, 0.08, 0.05)

var names := PackedStringArray()
var parents := PackedInt32Array()
var heads := PackedVector3Array()
var tails := PackedVector3Array()
var radii := PackedFloat32Array()
var globals: Array[Transform3D] = []
var rests: Array[Transform3D] = []


func _add(p_name: String, p_parent: int, p_head: Vector3, p_tail: Vector3, p_radius: float) -> int:
	names.append(p_name)
	parents.append(p_parent)
	heads.append(p_head)
	tails.append(p_tail)
	radii.append(p_radius)
	return names.size() - 1


func define_bones() -> void:
	var hips := _add("hips", -1, Vector3(0, 0.95, 0), Vector3(0, 1.05, 0), 0.15)
	var s1 := _add("spine1", hips, Vector3(0, 1.05, 0), Vector3(0, 1.2, 0), 0.14)
	var s2 := _add("spine2", s1, Vector3(0, 1.2, 0), Vector3(0, 1.35, 0), 0.155)
	var s3 := _add("spine3", s2, Vector3(0, 1.35, 0), Vector3(0, 1.48, 0), 0.165)
	var neck := _add("neck", s3, Vector3(0, 1.48, 0), Vector3(0, 1.58, 0), 0.06)
	_add("head", neck, Vector3(0, 1.57, 0.01), Vector3(0, 1.82, 0.01), 0.115)
	for side in [-1.0, 1.0]:
		var sfx := "_l" if side < 0.0 else "_r"
		var cl := _add("clavicle" + sfx, s3, Vector3(0.03 * side, 1.45, 0), Vector3(0.18 * side, 1.46, 0), 0.06)
		var ua := _add("upper_arm" + sfx, cl, Vector3(0.18 * side, 1.46, 0), Vector3(0.45 * side, 1.46, 0), 0.055)
		var la := _add("lower_arm" + sfx, ua, Vector3(0.45 * side, 1.46, 0), Vector3(0.7 * side, 1.46, 0), 0.045)
		var hand := _add("hand" + sfx, la, Vector3(0.7 * side, 1.46, 0), Vector3(0.8 * side, 1.46, 0), 0.038)
		for f in 5:
			var z := (float(f) - 2.0) * 0.016
			var parent := hand
			var x0 := 0.8
			for p in 3:
				var x1 := x0 + 0.03 - 0.005 * p
				parent = _add("finger%d_%d%s" % [f, p, sfx], parent, Vector3(x0 * side, 1.46, z), Vector3(x1 * side, 1.46, z), 0.011)
				x0 = x1
	for side in [-1.0, 1.0]:
		var sfx := "_l" if side < 0.0 else "_r"
		var ul := _add("upper_leg" + sfx, hips, Vector3(0.1 * side, 0.93, 0), Vector3(0.1 * side, 0.5, 0), 0.085)
		var ll := _add("lower_leg" + sfx, ul, Vector3(0.1 * side, 0.5, 0), Vector3(0.1 * side, 0.08, 0), 0.065)
		var foot := _add("foot" + sfx, ll, Vector3(0.1 * side, 0.08, -0.02), Vector3(0.1 * side, 0.04, 0.12), 0.05)
		_add("toe" + sfx, foot, Vector3(0.1 * side, 0.04, 0.12), Vector3(0.1 * side, 0.035, 0.2), 0.035)
	# Rest transforms: each bone at its head, its +Y towards its tail.
	for i in names.size():
		var y := (tails[i] - heads[i]).normalized()
		var ref := Vector3.FORWARD if absf(y.dot(Vector3.FORWARD)) < 0.9 else Vector3.RIGHT
		var x := y.cross(ref).normalized()
		var z := x.cross(y).normalized()
		var g := Transform3D(Basis(x, y, z), heads[i])
		globals.append(g)
		var p := parents[i]
		rests.append(g if p < 0 else globals[p].affine_inverse() * g)


func _colour(p_bone: String, u: float, a: float) -> Color:
	if p_bone == "head":
		# Hair over the back and top of the head.
		var back := -cos(a) # the ring's direction: local Z is the character's back for the head bone
		return HAIR if u > 0.62 or (u > 0.25 and back > 0.35) else SKIN
	if p_bone == "neck" or p_bone.begins_with("hand") or p_bone.begins_with("finger"):
		return SKIN
	if p_bone.begins_with("lower_arm"):
		return SHIRT if u > 0.82 else JACKET
	if p_bone.begins_with("spine") or p_bone.begins_with("clavicle") or p_bone.begins_with("upper_arm"):
		return JACKET
	if p_bone.begins_with("foot") or p_bone.begins_with("toe"):
		return BOOTS
	if p_bone.begins_with("lower_leg"):
		return BOOTS if u > 0.7 else TROUSERS
	return TROUSERS


func build_mesh() -> ArrayMesh:
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var colours := PackedColorArray()
	var bones := PackedInt32Array()
	var weights := PackedFloat32Array()
	var indices := PackedInt32Array()
	for b in names.size():
		var g := globals[b]
		var length := (tails[b] - heads[b]).length()
		var base := verts.size()
		for r in RINGS:
			var u := float(r) / float(RINGS - 1)
			var radius := radii[b] * (0.35 + 0.65 * sin(PI * (0.08 + 0.84 * u)))
			for s in RADIAL:
				var a := TAU * float(s) / float(RADIAL)
				var local_n := Vector3(cos(a), 0.0, sin(a))
				var flat := 1.0
				if names[b].begins_with("spine") or names[b] == "hips":
					flat = 0.72 if absf(local_n.z) > 0.7 else 1.0 # a torso deeper side to side than front to back
				verts.append(g * (Vector3(local_n.x, 0.0, local_n.z * flat) * radius + Vector3(0.0, u * length, 0.0)))
				normals.append((g.basis * local_n).normalized())
				colours.append(_colour(names[b], u, a))
				var p := parents[b]
				var w_parent := 0.0 if p < 0 else clampf(0.5 * (1.0 - u / 0.3), 0.0, 0.5)
				bones.append_array(PackedInt32Array([b, maxi(p, 0), 0, 0]))
				weights.append_array(PackedFloat32Array([1.0 - w_parent, w_parent, 0.0, 0.0]))
		for r in RINGS - 1:
			for s in RADIAL:
				var s2 := (s + 1) % RADIAL
				var i0 := base + r * RADIAL + s
				var i1 := base + r * RADIAL + s2
				var i2 := base + (r + 1) * RADIAL + s
				var i3 := base + (r + 1) * RADIAL + s2
				indices.append_array(PackedInt32Array([i0, i1, i2, i1, i3, i2]))
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = verts
	arr[Mesh.ARRAY_NORMAL] = normals
	arr[Mesh.ARRAY_COLOR] = colours
	arr[Mesh.ARRAY_BONES] = bones
	arr[Mesh.ARRAY_WEIGHTS] = weights
	arr[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	var mat := StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.vertex_color_is_srgb = true
	mat.roughness = 0.72
	mesh.surface_set_material(0, mat)
	return mesh


func build_skin() -> Skin:
	var skin := Skin.new()
	for b in names.size():
		skin.add_named_bind(names[b], globals[b].affine_inverse())
	return skin


## Rotation (Euler XYZ, radians) added to the rest pose of p_bone at t seconds into clip p_clip.
func _swing(p_clip: int, p_bone: String, t: float) -> Vector3:
	var w: float = TAU / float(CLIP_LENGTH[p_clip])
	var side := -1.0 if p_bone.ends_with("_l") else 1.0
	if p_clip == 3:
		# Falling: knees up, arms out.
		if p_bone.begins_with("upper_leg"):
			return Vector3(-0.55 if side < 0.0 else -0.2, 0.0, 0.0)
		if p_bone.begins_with("lower_leg"):
			return Vector3(0.9 if side < 0.0 else 0.5, 0.0, 0.0)
		if p_bone.begins_with("upper_arm"):
			return Vector3(0.0, 0.0, 0.35 * side)
		if p_bone.begins_with("lower_arm"):
			return Vector3(0.0, 0.3, 0.0)
		if p_bone.begins_with("finger"):
			return Vector3(0.0, 0.0, 0.3 * side)
		return Vector3.ZERO
	var amp: float = [0.15, 1.0, 1.6][p_clip]
	if p_clip == 0:
		# Idle: breathing, a slow sway, arms down.
		if p_bone.begins_with("upper_arm"):
			return Vector3(0.05 * sin(w * t) * side, 0.0, 1.25 * side)
		if p_bone.begins_with("lower_arm"):
			return Vector3(0.0, 0.15, 0.0)
		if p_bone.begins_with("spine"):
			return Vector3(0.02 * sin(w * t), 0.0, 0.015 * sin(w * t * 0.5))
		if p_bone == "head":
			return Vector3(0.04 * sin(w * t * 0.5), 0.12 * sin(w * t * 0.33), 0.0)
		if p_bone.begins_with("finger"):
			return Vector3(0.0, 0.0, 0.35 * side)
		return Vector3.ZERO
	if p_bone.begins_with("upper_leg"):
		return Vector3(0.45 * amp * sin(w * t) * side, 0.0, 0.0)
	if p_bone.begins_with("lower_leg"):
		return Vector3(0.55 * amp * maxf(0.0, sin(w * t * side + 1.0)), 0.0, 0.0)
	if p_bone.begins_with("foot"):
		return Vector3(0.2 * amp * sin(w * t * side + 2.0), 0.0, 0.0)
	if p_bone.begins_with("toe"):
		return Vector3(0.15 * amp * maxf(0.0, sin(w * t * side + 2.5)), 0.0, 0.0)
	if p_bone.begins_with("upper_arm"):
		return Vector3(0.35 * amp * sin(w * t + PI) * side, 0.0, (1.15 - 0.25 * (amp - 1.0)) * side)
	if p_bone.begins_with("lower_arm"):
		return Vector3(0.0, 0.25 * amp * (1.0 + sin(w * t + PI)) + 0.2 * (amp - 1.0), 0.0)
	if p_bone.begins_with("clavicle") or p_bone.begins_with("hand"):
		return Vector3(0.05 * amp * sin(w * t), 0.0, 0.03 * sin(w * t + side))
	if p_bone.begins_with("finger"):
		var p := float(p_bone.substr(8, 1).to_int())
		return Vector3(0.0, 0.0, (0.35 + 0.15 * sin(w * t + p)) * side)
	if p_bone == "spine1":
		return Vector3(0.08 * (amp - 1.0) + 0.03 * amp * sin(2.0 * w * t), 0.06 * amp * sin(w * t), 0.0)
	if p_bone.begins_with("spine"):
		return Vector3(0.03 * amp * sin(2.0 * w * t), 0.06 * amp * sin(w * t), 0.0)
	if p_bone == "neck" or p_bone == "head":
		return Vector3(0.05 * sin(w * t * 0.5) - 0.04 * (amp - 1.0), 0.0, 0.0)
	if p_bone == "hips":
		return Vector3(0.0, 0.08 * amp * sin(w * t), 0.04 * amp * sin(w * t))
	return Vector3.ZERO


func build_clips() -> AnimationLibrary:
	var lib := AnimationLibrary.new()
	for c in CLIPS.size():
		var anim := Animation.new()
		var length: float = CLIP_LENGTH[c]
		anim.length = length
		anim.loop_mode = Animation.LOOP_LINEAR
		var keys := maxi(1, int(round(length * KEY_FPS)))
		for b in names.size():
			var track := anim.add_track(Animation.TYPE_ROTATION_3D)
			anim.track_set_path(track, NodePath("Skeleton3D:" + names[b]))
			var rest_q := rests[b].basis.get_rotation_quaternion()
			for k in keys:
				var t := length * float(k) / float(keys)
				anim.rotation_track_insert_key(track, t, rest_q * Quaternion.from_euler(_swing(c, names[b], t)))
		# The hips bob twice a step (walk and run).
		if c == 1 or c == 2:
			var track := anim.add_track(Animation.TYPE_POSITION_3D)
			anim.track_set_path(track, NodePath("Skeleton3D:hips"))
			var bob := 0.025 if c == 1 else 0.05
			for k in keys:
				var t := length * float(k) / float(keys)
				anim.position_track_insert_key(track, t, rests[0].origin + Vector3(0.0, -bob * 0.5 + bob * 0.5 * cos(2.0 * TAU * t / length), 0.0))
		lib.add_animation(CLIPS[c], anim)
	return lib


## A Skeleton3D with the bones at rest (the builder adds it to the player's scene).
func build_skeleton() -> Skeleton3D:
	var sk := Skeleton3D.new()
	sk.name = "Skeleton3D"
	for b in names.size():
		sk.add_bone(names[b])
	for b in names.size():
		sk.set_bone_parent(b, parents[b])
		sk.set_bone_rest(b, rests[b])
	sk.reset_bone_poses()
	return sk
