extends SceneTree
## Milestone 232: a MakeHuman person (showcase/people/*.glb) worn over the rooftops' mannequin (FellowPerson.dress),
## playing one of its clips on a plain ground under a sun and a lamp; frames saved from a camera at its front, side or
## face, or the whole cast in a row.
##
##   godot --path showcase -s res://tools/person_look.gd -- --person=giulia [--clip=walk] [--view=front|side|face|cast]
##       [--seconds=2] [--every=30] --out=<dir>
##   --mannequin=1: the mannequin's own mesh too (the person moved 1 m aside), to compare the poses.

var _out := "user://person_look"
var _person := "giulia"
var _clip := "walk"
var _view := "front"
var _seconds := 2.0
var _every := 30
var _mannequin := false
var _global := false
var _raw := false
var _talk := false # --talk=1 (milestone 234): the person's face talking
var _face := {} # --face=jawOpen:1,mouthSmileLeft:0.8 (milestone 234): blend shapes set on the person
const CAST := ["giulia", "marco", "rosa", "aldo", "amara", "kenji"]


func _init() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			_out = a.get_slice("=", 1)
		elif a.begins_with("--person="):
			_person = a.get_slice("=", 1)
		elif a.begins_with("--clip="):
			_clip = a.get_slice("=", 1)
		elif a.begins_with("--view="):
			_view = a.get_slice("=", 1)
		elif a.begins_with("--seconds="):
			_seconds = float(a.get_slice("=", 1))
		elif a.begins_with("--every="):
			_every = maxi(1, int(a.get_slice("=", 1)))
		elif a == "--mannequin=1":
			_mannequin = true
		elif a == "--global=1":
			_global = true
		elif a == "--talk=1":
			_talk = true
		elif a.begins_with("--face="):
			for pair in a.get_slice("=", 1).split(","):
				_face[pair.get_slice(":", 0)] = float(pair.get_slice(":", 1))
		elif a == "--raw=1":
			_raw = true # the glTF scene as it is, at rest, beside the dressed mannequin
	DirAccess.make_dir_recursive_absolute(_out)
	_run.call_deferred()


func _npc(p_name: String, p_at: Vector3) -> Node3D:
	var npc := (load("res://rooftops/npc.tscn") as PackedScene).instantiate() as Node3D
	root.add_child(npc)
	npc.position = p_at
	var pskel := FellowPerson.dress(npc, load("res://people/%s.glb" % p_name) as PackedScene)
	if pskel and _global:
		(pskel.get_parent() as RetargetModifier3D).use_global_pose = true
	if pskel == null:
		push_error("person_look: %s not dressed" % p_name)
	if pskel and not _face.is_empty():
		for mi in pskel.find_children("*", "MeshInstance3D", true, false):
			for k in _face:
				var bi := (mi as MeshInstance3D).find_blend_shape_by_name(k)
				if bi >= 0:
					(mi as MeshInstance3D).set_blend_shape_value(bi, _face[k])
	if pskel and _talk and pskel.get_node_or_null(^"Face"):
		pskel.get_node(^"Face").set("talking", true)
	if pskel and _mannequin:
		for c in (npc.find_child("Skeleton3D", true, false) as Skeleton3D).get_children():
			if c is MeshInstance3D:
				(c as MeshInstance3D).visible = true
		pskel.position.x += 1.0
	if _raw:
		var raw := (load("res://people/%s.glb" % p_name) as PackedScene).instantiate() as Node3D
		root.add_child(raw)
		raw.position = p_at + Vector3(-1.0, 0, 0)
	var ap := npc.get_node_or_null("AnimationPlayer") as AnimationPlayer
	if ap and ap.has_animation(_clip):
		ap.play(_clip)
	return npc


func _run() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.55, 0.62, 0.7)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.55, 0.57, 0.6)
	env.ambient_light_energy = 1.0
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	var we := WorldEnvironment.new()
	we.environment = env
	root.add_child(we)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40, 25, 0)
	sun.light_energy = 2.2
	sun.shadow_enabled = true
	root.add_child(sun)
	var ground := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(30, 30)
	ground.mesh = pm
	var gm := StandardMaterial3D.new()
	gm.albedo_color = Color(0.62, 0.6, 0.56)
	ground.material_override = gm
	root.add_child(ground)
	var cam := Camera3D.new()
	cam.fov = 40.0
	root.add_child(cam)
	var npcs: Array[Node3D] = []
	if _view == "cast":
		for i in CAST.size():
			npcs.append(_npc(CAST[i], Vector3((i - (CAST.size() - 1) * 0.5) * 0.9, 0, 0)))
		cam.position = Vector3(0, 1.1, 6.2)
		cam.look_at(Vector3(0, 0.9, 0))
	else:
		npcs.append(_npc(_person, Vector3.ZERO))
	var frames := int(_seconds * 60.0)
	for i in frames:
		if _view != "cast":
			# (clips with root motion walk off: the camera follows the hips over the ground)
			var sk := npcs[0].find_child("Skeleton3D", true, false) as Skeleton3D
			var at := npcs[0].global_position
			if sk and sk.find_bone("Hips") >= 0:
				at = sk.global_transform * sk.get_bone_global_pose(sk.find_bone("Hips")).origin
				at.y = 0.0
			if _view == "side":
				cam.position = at + Vector3(3.2, 1.0, 0)
				cam.look_at(at + Vector3(0, 0.9, 0))
			elif _view == "face":
				# (at the person's head: its height its own)
				var head := at + Vector3(0, 1.5, 0)
				var ps := sk.find_child("Skeleton3D", true, false) as Skeleton3D if sk else null
				if ps and ps.find_bone("Head") >= 0:
					head = ps.global_transform * ps.get_bone_global_pose(ps.find_bone("Head")).origin + Vector3(0, 0.07, 0)
				cam.position = head + Vector3(0.12, 0.02, 0.62)
				cam.look_at(head)
			else:
				cam.position = at + Vector3(0.4, 1.1, 3.4)
				cam.look_at(at + Vector3(0, 0.9, 0))
		await process_frame
		if i % _every == _every - 1:
			await RenderingServer.frame_post_draw
			root.get_texture().get_image().save_png("%s/f%04d.png" % [_out, i / _every])
	print("person_look: %d frames" % frames)
	quit()
