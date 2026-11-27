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
const HAIR_ROUGHNESS := 0.8
const HAIR_SPECULAR := 0.25
## The hair's drawing: 1 (milestone 236) blended at its edges after an opaque pre-pass, 0 cut by its alpha (232-235).
static var hair_mode := 1
## Milestone 241: the clothes' fabric (suits, felt hats) with a sheen (BaseMaterial3D.sheen, decision 0383): its
## colour CLOTH_SHEEN times the clothes' tint, its roughness CLOTH_SHEEN_ROUGHNESS.
static var cloth_sheen := true
const CLOTH_SHEEN := 0.3
const CLOTH_SHEEN_ROUGHNESS := 0.5
## Milestone 243: the skin by region (tools/fellow_characters/make_skin_maps.py, decision 0385) — its roughness (lips,
## eyelids, the T-zone glossier than the body), a second sharper lobe for its oil (the clearcoat), the light through the
## ears from behind. Off: one roughness for the whole body (232-242).
static var skin_maps := true
const SKIN_OIL := 0.2 # the clearcoat's strength at the map's full value
const SKIN_OIL_ROUGHNESS := 0.5
const SKIN_TRANSMITTANCE := Color(0.95, 0.32, 0.22)
static var _skin_textures: Array[Texture2D] = []


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
	if strand_hair:
		_strands(pskel, p_person.resource_path.get_basename() + "_hair.json", hair_tint, p_match_height)
	return pskel


## Milestone 244: the hair as strands (FellowHair3D, decision 0386) over its cards, from the guides traced from the
## person's MakeHuman hair (<name>_hair.json: tools/fellow_characters/hair_guides.py). The guides are in the model's
## space; the head bone's bind pose takes them into its own, so that they ride the head as its skin does. The cards stay
## under them, darker (the hair's inner layers, which the strands shade), and are what is left of it far away
## (STRAND_LOD: the strands thin out beyond it).
static var strand_hair := false # (milestone 244: FellowHair3D with traced guides — not yet better than the cards at play distances)
const STRANDS := 24000
const STRAND_LOD := 3.0 # m
const STRAND_SIMULATION := 12.0 # m
const CARD_UNDER := 0.75 # the cards' albedo under the strands
static var _hair_cache := {}

static func _strands(p_person: Skeleton3D, p_json: String, p_tint: Color, p_player: bool) -> void:
	if not ClassDB.class_exists(&"FellowHair3D") or not FileAccess.file_exists(p_json):
		return
	var data: Dictionary = _hair_cache.get(p_json, {})
	if data.is_empty():
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(p_json))
		if not parsed is Dictionary or int(parsed.get("points", 0)) < 2:
			return
		data = parsed
		_hair_cache[p_json] = data
	var head := p_person.find_bone("Head")
	if head < 0:
		return
	# The head's bind pose (model space to the bone's) from a skinned mesh of the person.
	var bind := Transform3D()
	var found := false
	for mi in p_person.find_children("*", "MeshInstance3D", true, false):
		var skin := (mi as MeshInstance3D).skin
		if skin == null:
			continue
		for i in skin.get_bind_count():
			if skin.get_bind_name(i) == &"Head" or skin.get_bind_bone(i) == head:
				bind = skin.get_bind_pose(i)
				found = true
				break
		if found:
			break
	if not found:
		return
	var pts := PackedVector3Array()
	for g in data["guides"]:
		for q in g:
			pts.append(bind * Vector3(q[0], q[1], q[2]))
	var att := BoneAttachment3D.new()
	att.name = "HairAttachment"
	att.bone_name = "Head"
	p_person.add_child(att)
	var hair: Node3D = ClassDB.instantiate(&"FellowHair3D")
	hair.name = "Strands"
	var hc: Array = data["head_center"]
	hair.set("scalp_center", bind * Vector3(hc[0], hc[1], hc[2]))
	hair.set("scalp_radius", float(data.get("head_radius", 0.085)))
	hair.set("collision_margin", 0.004)
	hair.set("segments", int(data["points"]) - 1)
	hair.set("guide_shape_points", int(data["points"]))
	hair.set("guide_shapes", pts)
	hair.set("strand_count", STRANDS)
	hair.set("spread", 1.0)
	hair.set("length_variation", 0.15)
	hair.set("stiffness", 0.7)
	hair.set("lod_distance", STRAND_LOD)
	hair.set("simulation_distance", STRAND_SIMULATION)
	var rc: Array = data["root_color"]
	var tc: Array = data["tip_color"]
	hair.set("root_color", Color(rc[0], rc[1], rc[2]) * p_tint * 0.8)
	hair.set("tip_color", Color(tc[0], tc[1], tc[2]) * p_tint)
	hair.set("root_width", 0.2)
	hair.set("tip_width", 0.08)
	hair.set("self_shadow", 0.6 if p_player else 0.0)
	att.add_child(hair)
	# The cards under the strands, darker.
	for mi in p_person.find_children("*", "MeshInstance3D", true, false):
		var m := mi as MeshInstance3D
		for i in m.get_surface_override_material_count():
			var mat := m.get_surface_override_material(i) as StandardMaterial3D
			if mat and mat.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA_DEPTH_PRE_PASS:
				mat.albedo_color *= Color(CARD_UNDER, CARD_UNDER, CARD_UNDER)


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
			if skin_maps:
				_skin_regions(m)
		elif m.transparency != BaseMaterial3D.TRANSPARENCY_DISABLED:
			# Hair cards, eyebrows, eyelashes: both sides drawn. The eyebrows and eyelashes cut by their alpha; the hair
			# (milestone 236) drawn where it is opaque before the other passes and blended at its edges after them
			# (soft strands instead of hard cut ones), duller (black hair shone like plastic).
			var brow := n.contains("eyebrow") or n.contains("eyelash")
			if hair_mode == 1 and not brow:
				m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_DEPTH_PRE_PASS
				m.roughness = HAIR_ROUGHNESS
				m.metallic_specular = HAIR_SPECULAR
				if hair_flow:
					_hair_highlights(m, n.get_extension() if n.contains(".") else n)
			else:
				m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
				m.alpha_scissor_threshold = ALPHA_CUT
				m.roughness = 0.6
			m.cull_mode = BaseMaterial3D.CULL_DISABLED
			m.albedo_color *= p_hair_tint
		elif n.contains("eye") or n.contains("low-poly") or n.contains("high-poly"):
			m.roughness = 0.12
			if n.contains("high-poly"):
				_wet_eyes(m)
		else:
			m.roughness = maxf(m.roughness, 0.75) # cloth
			m.metallic = 0.0
			m.albedo_color *= p_cloth_tint
			if cloth_sheen and (n.contains("suit") or n.contains("fedora")):
				m.sheen = true
				m.sheen_color = Color(CLOTH_SHEEN, CLOTH_SHEEN, CLOTH_SHEEN) * p_cloth_tint
				m.sheen_roughness = CLOTH_SHEEN_ROUGHNESS
		p_mesh.set_surface_override_material(i, m)


## Milestone 244: the hair's highlights stretched along its painted strands — BaseMaterial3D's anisotropy with the
## hairstyle's flow map (hair_<name>_flow.png, make_skin_maps.py: the texture's own strand direction), a little
## glossier than 236's flat hair (a highlight that runs across the strands reads as hair; a round one as plastic).
static var hair_flow := true
const HAIR_FLOW_ROUGHNESS := 0.55
const HAIR_FLOW_SPECULAR := 0.3
const HAIR_ANISOTROPY := 0.85
## Curly hair has no band of light (its strands point every way): kept as 236 made it (an afro's top went grey).
const HAIR_CURLY := ["afro"]

static func _hair_highlights(p_mat: StandardMaterial3D, p_style: String) -> void:
	var path := "res://people/hair_%s_flow.png" % p_style
	if not ResourceLoader.exists(path) or HAIR_CURLY.any(func(c: String) -> bool: return p_style.contains(c)):
		return
	p_mat.anisotropy_enabled = true
	p_mat.anisotropy = HAIR_ANISOTROPY
	p_mat.anisotropy_flowmap = load(path) as Texture2D
	p_mat.roughness = HAIR_FLOW_ROUGHNESS
	p_mat.metallic_specular = HAIR_FLOW_SPECULAR


## Milestone 243: the high-poly eyes — their cornea's island is transparent in the texture (cut), the eyeball wet (a
## sharp clearcoat over a duller sclera), darkened towards the lids (eye_occlusion.png, also for the direct light).
static func _wet_eyes(p_mat: StandardMaterial3D) -> void:
	p_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	p_mat.alpha_scissor_threshold = 0.5
	p_mat.roughness = 0.3
	p_mat.clearcoat_enabled = true
	p_mat.clearcoat = 1.0
	p_mat.clearcoat_roughness = 0.04
	var occ := load("res://people/eye_occlusion.png") as Texture2D
	if occ:
		p_mat.ao_enabled = true
		p_mat.ao_texture = occ
		p_mat.ao_texture_channel = BaseMaterial3D.TEXTURE_CHANNEL_RED
		p_mat.ao_light_affect = 0.85


static func _skin_regions(p_mat: StandardMaterial3D) -> void:
	if _skin_textures.is_empty():
		for f in ["skin_roughness", "skin_oil", "skin_transmittance"]:
			_skin_textures.append(load("res://people/%s.png" % f) as Texture2D)
	if _skin_textures[0] == null:
		return
	p_mat.roughness = 1.0 # (the map's values are the roughness)
	p_mat.roughness_texture = _skin_textures[0]
	p_mat.roughness_texture_channel = BaseMaterial3D.TEXTURE_CHANNEL_RED
	if _skin_textures[1] != null:
		p_mat.clearcoat_enabled = true
		p_mat.clearcoat = SKIN_OIL
		p_mat.clearcoat_roughness = SKIN_OIL_ROUGHNESS
		p_mat.clearcoat_texture = _skin_textures[1]
	if _skin_textures[2] != null:
		p_mat.subsurf_scatter_transmittance_enabled = true
		p_mat.subsurf_scatter_transmittance_color = SKIN_TRANSMITTANCE
		p_mat.subsurf_scatter_transmittance_texture = _skin_textures[2]
		p_mat.subsurf_scatter_transmittance_depth = 0.25
		p_mat.subsurf_scatter_transmittance_boost = 0.2
