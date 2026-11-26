class_name FellowPerson
extends RefCounted
## Milestone 232: a MakeHuman person (tools/fellow_characters/make_characters.py: CC0 bodies, skins, hair and clothes,
## their bones named after Godot's humanoid profile) worn over the mannequin of rooftops/npc.tscn. The mannequin keeps
## its skeleton and everything that drives it (its clips, motion matching, foot placement, physical animation); its
## mesh is hidden, and a RetargetModifier3D under its skeleton hands every pose to the person's skeleton, its rests
## different (its rests first turned to the mannequin's silhouette); it keeps its own height, the hips' motion scaled to
## it. Its materials for the engine: the skin with subsurface scattering, the hair, the
## eyebrows and eyelashes cut by their alpha, the eyes wet.

const SKIN_SSS := 0.35
const ALPHA_CUT := 0.45


## p_match_height: the person scaled so that its hips stand at the mannequin's (the player: its hands and feet where
## the traversal puts the mannequin's, on the holds and ledges); otherwise its own height.
static func dress(p_npc: Node3D, p_person: PackedScene, p_rng: RandomNumberGenerator = null, p_match_height := false) -> Node3D:
	var skel := p_npc.find_child("Skeleton3D", true, false) as Skeleton3D
	if skel == null or p_person == null:
		return null
	var person := p_person.instantiate() as Node3D
	var pskel := person.find_child("Skeleton3D", true, false) as Skeleton3D
	if pskel == null:
		person.free()
		return null
	# The mannequin's own meshes hidden (its skeleton stays: it is what the animation drives).
	for c in skel.get_children():
		if c is MeshInstance3D:
			(c as MeshInstance3D).visible = false
	# The person's skeleton (with its meshes) moved out of its glTF scene, under the retargeting modifier.
	pskel.get_parent().remove_child(pskel)
	person.free()
	# Its rests turned to the mannequin's silhouette and its motion scale set before it goes under the modifier (which
	# caches both skeletons' rests when a child skeleton arrives).
	var profile := SkeletonProfileHumanoid.new()
	_match_silhouette(pskel, skel)
	# Its own height kept: the hips' motion (bobbing, crouching) scaled by the ratio of the hips' heights.
	var hips_m := skel.find_bone("Hips")
	var hips_p := pskel.find_bone("Hips")
	if hips_m >= 0 and hips_p >= 0:
		var hm := skel.get_bone_global_rest(hips_m).origin.y
		var hp := pskel.get_bone_global_rest(hips_p).origin.y
		if hm > 0.01 and hp > 0.01:
			pskel.motion_scale = skel.motion_scale * hp / hm
			if p_match_height:
				pskel.scale = Vector3.ONE * (hm / hp)
	var retarget := RetargetModifier3D.new()
	retarget.name = "Person"
	retarget.profile = profile
	skel.add_child(retarget)
	pskel.transform = Transform3D(Basis.from_scale(pskel.scale), Vector3())
	retarget.add_child(pskel)
	if p_match_height:
		_contact_ik(pskel, skel)
	# One tint for its clothes, one for its hair (from p_rng: twins in a crowd dressed apart).
	var cloth_tint := Color.WHITE
	var hair_tint := Color.WHITE
	if p_rng:
		cloth_tint = Color.from_hsv(p_rng.randf(), p_rng.randf_range(0.0, 0.45), p_rng.randf_range(0.7, 1.0))
		hair_tint = Color.from_hsv(0.08, p_rng.randf_range(0.0, 0.3), p_rng.randf_range(0.55, 1.0))
	for mi in pskel.find_children("*", "MeshInstance3D", true, false):
		_materials(mi as MeshInstance3D, cloth_tint, hair_tint)
	_face(pskel, skel, p_rng)
	return pskel


## Milestone 234: a face for a person made with the face's shapes (ARKit's 52, MakeHuman's Faceunits): a
## FellowFaceAnimator named "Face" on the body, the eyebrows, eyelashes, teeth and tongue following it; blinking, its
## mouth talking when `talking` is set. Milestone 235: with the rig's eye bones, its gaze (the face's `look_target_path`,
## saccades without one) turns the eyes (fellow_person_eyes.gd on the mannequin's skeleton, after the hand-over), and
## a "pain" expression beside the built-in ones.
const PAIN := {"brow_down_left": 0.8, "brow_down_right": 0.8, "brow_inner_up": 0.35, "eye_squint_left": 0.7,
		"eye_squint_right": 0.7, "cheek_squint_left": 0.6, "cheek_squint_right": 0.6, "nose_sneer_left": 0.5,
		"nose_sneer_right": 0.5, "mouth_wide": 0.45}

static func _face(p_person: Skeleton3D, p_mannequin: Skeleton3D, p_rng: RandomNumberGenerator) -> void:
	var body: MeshInstance3D = null
	var followers: Array = []
	for mi in p_person.find_children("*", "MeshInstance3D", true, false):
		var m := (mi as MeshInstance3D)
		if m.mesh == null or m.find_blend_shape_by_name(&"jawOpen") < 0 and m.find_blend_shape_by_name(&"eyeBlinkLeft") < 0:
			continue
		var mat := m.mesh.surface_get_material(0)
		if body == null and mat and mat.resource_name.ends_with(".body"):
			body = m
		else:
			followers.append(m)
	if body == null or not ClassDB.class_exists(&"FellowFaceAnimator"):
		return
	var face: Node = ClassDB.instantiate(&"FellowFaceAnimator")
	face.name = "Face"
	p_person.add_child(face)
	face.set("mesh_path", face.get_path_to(body))
	var paths: Array[NodePath] = []
	for f in followers:
		paths.append(face.get_path_to(f))
	face.set("follower_paths", paths)
	face.set("seed", p_rng.randi() if p_rng else 0)
	face.set("expressions", {"pain": PAIN})
	var eyes: SkeletonModifier3D = load("res://scripts/fellow_person_eyes.gd").new()
	eyes.name = "PersonEyes"
	if eyes.call("setup", p_person, face):
		face.set("gaze", true)
		p_mannequin.add_child(eyes)
	else:
		face.set("gaze", false)
		eyes.free()


## The player's hands and feet put where the mannequin's are (milestone 233): the arms' and legs' proportions differ (the
## hands hanging from a ledge 11-16 cm off it), so right after the pose is handed over a two-bone IK takes each of the
## person's limbs to the mannequin's hand or foot (fellow_person_contacts.gd, on the mannequin's skeleton after the
## RetargetModifier3D).
static func _contact_ik(p_person: Skeleton3D, p_mannequin: Skeleton3D) -> void:
	var contacts: SkeletonModifier3D = load("res://scripts/fellow_person_contacts.gd").new()
	contacts.name = "PersonContacts"
	contacts.call("setup", p_person, p_mannequin)
	p_mannequin.add_child(contacts)


## The person's retargeting runs after the mannequin's other modifiers (added later: a head's look, the falling bodies):
## moved back to the last place.
static func keep_last(p_skeleton: Skeleton3D) -> void:
	for n in [^"Person", ^"PersonContacts", ^"PersonEyes"]:
		var r := p_skeleton.get_node_or_null(n)
		if r:
			p_skeleton.move_child(r, -1)


## The person's limbs' rests turned bone by bone (parents first) so that each points where the mannequin's does: MPFB's
## rest is an A-pose (arms 45 degrees down, legs apart), the mannequin's a T-pose, and RetargetModifier3D hands over each
## bone's turn from its rest — from different rests the arms end up 45 degrees off (crossed over the body). The trunk,
## neck and head keep theirs (their small tilts are the body's own). Only the rests change, not the skin's binds: at rest
## the person stands as the mannequin does.
const LIMB_TAILS := {"Shoulder": "UpperArm", "UpperArm": "LowerArm", "LowerArm": "Hand", "Hand": "MiddleProximal",
		"ThumbMetacarpal": "ThumbProximal", "ThumbProximal": "ThumbDistal", "IndexProximal": "IndexIntermediate",
		"IndexIntermediate": "IndexDistal", "MiddleProximal": "MiddleIntermediate", "MiddleIntermediate": "MiddleDistal",
		"RingProximal": "RingIntermediate", "RingIntermediate": "RingDistal", "LittleProximal": "LittleIntermediate",
		"LittleIntermediate": "LittleDistal", "UpperLeg": "LowerLeg", "LowerLeg": "Foot", "Foot": "Toes"}


static func _match_silhouette(p_person: Skeleton3D, p_mannequin: Skeleton3D) -> void:
	var n := p_person.get_bone_count()
	var global := []
	global.resize(n)
	for b in n:
		global[b] = p_person.get_bone_global_rest(b)
	for b in n: # (bones are listed after their parents)
		var bname := p_person.get_bone_name(b)
		var side := "Left" if bname.begins_with("Left") else "Right" if bname.begins_with("Right") else ""
		var part := bname.trim_prefix(side)
		if side == "" or not LIMB_TAILS.has(part):
			continue
		var tail := side + String(LIMB_TAILS[part])
		var mb := p_mannequin.find_bone(bname)
		var pt := p_person.find_bone(tail)
		var mt := p_mannequin.find_bone(tail)
		if mb < 0 or pt < 0 or mt < 0:
			continue
		var g: Transform3D = global[b]
		var dir_p := ((global[pt] as Transform3D).origin - g.origin).normalized() # (the tail where the parents' turns left it)
		var dir_m := (p_mannequin.get_bone_global_rest(mt).origin - p_mannequin.get_bone_global_rest(mb).origin).normalized()
		if dir_p.length_squared() < 0.5 or dir_m.length_squared() < 0.5 or dir_p.dot(dir_m) > 0.99999:
			continue
		g.basis = Basis(Quaternion(dir_p, dir_m)) * g.basis
		global[b] = g
		# The descendants follow (their local rests unchanged).
		for c in range(b + 1, n):
			if _descends(p_person, c, b):
				global[c] = (global[p_person.get_bone_parent(c)] as Transform3D) * p_person.get_bone_rest(c)
	for b in n:
		var parent := p_person.get_bone_parent(b)
		var local: Transform3D = (global[b] as Transform3D) if parent < 0 else (global[parent] as Transform3D).affine_inverse() * (global[b] as Transform3D)
		p_person.set_bone_rest(b, local)
	p_person.reset_bone_poses()


static func _descends(p_skel: Skeleton3D, p_bone: int, p_ancestor: int) -> bool:
	var b := p_skel.get_bone_parent(p_bone)
	while b >= 0:
		if b == p_ancestor:
			return true
		b = p_skel.get_bone_parent(b)
	return false


static func _materials(p_mesh: MeshInstance3D, p_cloth_tint: Color, p_hair_tint: Color) -> void:
	if p_mesh.mesh == null:
		return
	for i in p_mesh.mesh.get_surface_count():
		var m := p_mesh.mesh.surface_get_material(i) as StandardMaterial3D
		if m == null:
			continue
		var n := m.resource_name
		m = m.duplicate() as StandardMaterial3D
		if n.ends_with(".body"):
			m.subsurf_scatter_enabled = true
			m.subsurf_scatter_skin_mode = true
			m.subsurf_scatter_strength = SKIN_SSS
			m.roughness = 0.55
			m.metallic_specular = 0.4
		elif m.transparency != BaseMaterial3D.TRANSPARENCY_DISABLED:
			# Hair cards, eyebrows, eyelashes: cut, both sides drawn.
			m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
			m.alpha_scissor_threshold = ALPHA_CUT
			m.cull_mode = BaseMaterial3D.CULL_DISABLED
			m.roughness = 0.6
			m.albedo_color *= p_hair_tint
		elif n.contains("eye") or n.contains("low-poly") or n.contains("high-poly"):
			m.roughness = 0.12
		else:
			m.roughness = maxf(m.roughness, 0.75) # cloth
			m.metallic = 0.0
			m.albedo_color *= p_cloth_tint
		p_mesh.set_surface_override_material(i, m)
