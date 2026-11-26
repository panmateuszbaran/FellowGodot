extends Node3D
## The rooftops' people: passers-by walking round the square, across it and in and out of its streets, groups standing
## talking, people sitting on the palaces' stone benches, the stall keepers. Each one the mannequin (rooftops/npc.tscn)
## dressed by colour — a tunic's colour on its shell, darker joints — some in a long robe, some in a beret or a hat; its
## clip played by its own AnimationPlayer (the walk's speed matched to its pace). The walkers step aside for the
## player. Everything is set from the data the builder (tools/build_rooftops.gd) gives it.
##
## Milestone 208 (the owner: "more interaction with the NPCs — running into them, knocking someone over, not passing
## through them"): each one a capsule the player meets (LAYER_PEOPLE). Run into (`bump()`, from the player's slide), by
## how fast the body met them along the contact: under NUDGE_MAX a shove (they give a little way and come back); under
## KNOCK_MIN a stagger (the clip "stagger_back" or "stagger_fwd": two steps the way of the push, turned to it); faster,
## or landed on, knocked down — the skeleton's bodies (FellowPhysicalAnimation3D, built for it and cleared after) fall
## and lie as a ragdoll, then get up (the clip "get_up_back" or "get_up_front", placed where the body lies, the bodies
## blended into it: `get_up_from_animation`). Then a walker walks on back into its way, the others walk back to their
## place. The people sitting are only shoved.

@export var npc_scene: PackedScene
@export var routes: Array = [] ## each [PackedVector3Array points, loop (bool), walkers (int)]
@export var groups: Array = [] ## each [Vector3 centre, count]
@export var seats: Array[Transform3D] = [] ## +X out from the bench, the origin on the ground in front of it
@export var keepers: Array[Transform3D] = [] ## behind the stalls' counters, +Z towards their fronts
@export var player_path := NodePath("../Player")
@export var seed := 7

const PALETTE := [
	Color(0.45, 0.1, 0.08), Color(0.6, 0.44, 0.2), Color(0.32, 0.22, 0.14), Color(0.2, 0.26, 0.36), Color(0.18, 0.27, 0.16),
	Color(0.74, 0.68, 0.56), Color(0.09, 0.09, 0.1), Color(0.42, 0.4, 0.42), Color(0.5, 0.2, 0.26), Color(0.26, 0.3, 0.24),
]

const LAYER_WORLD := 1
const LAYER_PEOPLE := 16
const LAYER_RAGDOLL := 32 # (the bodies of one knocked down: the world, not the player)
const NUDGE_MAX := 1.8 # m/s
const KNOCK_MIN := 5.2 # m/s: a sprint (7), not a run (4.4)
const NUDGE_EVERY := 0.5 # s
const NUDGE_FAR := 0.35 # m
const DOWN_WAIT := 1.1 # s lying still before getting up
const GET_UP_BLEND := 0.8 # s, from where the body lies into the clip
const ROOT_TRACK := ^"Mannequin/Skeleton3D:Root"
enum { REACT_NONE, REACT_NUDGE, REACT_STAGGER, REACT_KNOCK }

var _rng := RandomNumberGenerator.new()
var _walkers: Array = []
var _people: Array = [] # milestone 208: everyone — {node, ap, skel, shape, kind, clip, speed, home, face, react, ...}
var _bumps := 0 # (tools: how many reactions so far)
var _held: Array = [] # the clothes' meshes and materials, held until the people are gone (freed after them)
var _player: Node3D


func _ready() -> void:
	add_to_group(&"crowd")
	if npc_scene == null:
		return
	_rng.seed = seed
	_player = get_node_or_null(player_path)
	for r in routes:
		var pts: PackedVector3Array = r[0]
		var loop: bool = r[1]
		var length := _length(pts, loop)
		for i in int(r[2]):
			var w := {"pts": pts, "loop": loop, "length": length, "s": _rng.randf() * length * (1.0 if loop else 2.0),
					"speed": _rng.randf_range(1.05, 1.55), "lane": _rng.randf_range(-0.7, 0.7), "push": 0.0, "pace": 1.0}
			w["node"] = _person("walk", (w["speed"] as float) / 1.6)
			w["detour"] = Vector3()
			w["person"] = _register(w["node"], "walk", "walk", (w["speed"] as float) / 1.6)
			w.person["walker"] = w # (milestone 222: a walker going home at night finds its way back)
			_walkers.append(w)
	for g in groups:
		var c: Vector3 = g[0]
		var n: int = g[1]
		for i in n:
			var a := TAU * i / n + _rng.randf_range(-0.3, 0.3)
			var p := c + Vector3(cos(a), 0.0, sin(a)) * _rng.randf_range(0.55, 0.7)
			var clip := "talk" if _rng.randf() < 0.7 else "idle"
			var node := _person(clip, 1.0)
			node.position = p
			_face(node, c - p)
			_register(node, "stand", clip, 1.0)
	for s in seats:
		if _rng.randf() < 0.3:
			continue
		var node := _person("sit", 1.0)
		node.position = s.origin
		_face(node, s.basis.x)
		_register(node, "seat", "sit", 1.0)
	for k in keepers:
		var clip := "idle" if _rng.randf() < 0.5 else "talk"
		var node := _person(clip, 1.0)
		node.position = k.origin
		_face(node, k.basis.z)
		_register(node, "stand", clip, 1.0)
	_social_ready()


func _person(p_clip: String, p_speed: float) -> Node3D:
	var node: Node3D = npc_scene.instantiate()
	add_child(node)
	var s := _rng.randf_range(0.93, 1.06)
	node.scale = Vector3(s, s, s)
	var tunic := StandardMaterial3D.new()
	_held.append(tunic)
	tunic.albedo_color = PALETTE[_rng.randi() % PALETTE.size()]
	tunic.roughness = 0.88
	var dark := StandardMaterial3D.new()
	_held.append(dark)
	dark.albedo_color = (tunic.albedo_color as Color).darkened(0.65)
	dark.roughness = 0.8
	var body := node.find_child("MannequinBody", true, false) as MeshInstance3D
	if body:
		for i in body.mesh.get_surface_count():
			var m := body.mesh.surface_get_material(i)
			body.set_surface_override_material(i, dark if m and m.resource_name == "mannequin_joint" else tunic)
	var skel := node.find_child("Skeleton3D", true, false) as Skeleton3D
	if skel:
		# A long robe for some (from the hips past the knees), a hat for some.
		if _rng.randf() < 0.4 and p_clip != "sit":
			var robe_mat := StandardMaterial3D.new()
			robe_mat.albedo_color = PALETTE[_rng.randi() % PALETTE.size()].darkened(0.15)
			robe_mat.roughness = 0.9
			robe_mat.cull_mode = BaseMaterial3D.CULL_DISABLED # (its inside seen from below as it swings)
			_held.append(robe_mat)
			_skirt(skel, robe_mat)
		var hat_kind := _rng.randf()
		if hat_kind < 0.45:
			var hat := CylinderMesh.new()
			var beret := hat_kind < 0.25
			hat.top_radius = 0.16 if beret else 0.09
			hat.bottom_radius = 0.12 if beret else 0.2
			hat.height = 0.06 if beret else 0.1
			hat.radial_segments = 16
			hat.rings = 1
			var hat_mat := StandardMaterial3D.new()
			hat_mat.albedo_color = PALETTE[_rng.randi() % PALETTE.size()].darkened(0.3)
			hat_mat.roughness = 0.9
			hat.material = hat_mat
			_attach(skel, "Head", hat, Vector3(0.0, 0.19, 0.0))
	var ap := node.get_node_or_null("AnimationPlayer") as AnimationPlayer
	if ap:
		ap.play(p_clip)
		ap.speed_scale = p_speed
		ap.seek(_rng.randf() * ap.current_animation_length, true)
	return node


## Milestone 208 (the owner: "the skirts not solids but cloth with a simulated material"): a long robe from the waist
## past the knees, flaring, as FellowCloth3D on the figure's skin — its waistband pinned to the hips (vertex colour red),
## the rest skinned from the hips towards the thighs down its length and kept within SKIRT_FREE of its skinned place
## (none at the waistband, all of it at the hem), not behind it into the body (the backstop), out of the legs and the
## hands (capsules on the thighs, shins, forearms and hands), swung by the body, the legs and the wind. Simulated within
## SKIRT_SIMULATE m of the camera.
const SKIRT_TOP := 1.0 # m over the ground at rest (the hips' at 0.965)
const SKIRT_HEM := 0.27
const SKIRT_FREE := 0.22 # m
const SKIRT_SIMULATE := 32.0 # m

func _skirt(p_skel: Skeleton3D, p_mat: Material) -> void:
	var hips := p_skel.find_bone("Hips")
	var leg_l := p_skel.find_bone("LeftUpperLeg")
	var leg_r := p_skel.find_bone("RightUpperLeg")
	if hips < 0 or leg_l < 0 or leg_r < 0:
		return
	var seg := 18
	var rings := 9
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var colors := PackedColorArray()
	var bones := PackedInt32Array()
	var weights := PackedFloat32Array()
	var idx := PackedInt32Array()
	for r in rings:
		var u := float(r) / float(rings - 1)
		var y := lerpf(SKIRT_TOP, SKIRT_HEM, u)
		var rad := lerpf(0.175, 0.31, u * u * 0.4 + u * 0.6)
		for k in seg:
			var a := TAU * float(k) / float(seg)
			var x := sin(a) * rad
			verts.append(Vector3(x, y, cos(a) * rad * 0.92))
			normals.append(Vector3(sin(a), 0.0, cos(a)))
			colors.append(Color(1.0 if r == 0 else 0.0, u * u, 0.0))
			# Down the skirt more of each side on its thigh (the hem half on it): it moves with the stride.
			var t := 0.7 * u
			var side := clampf(0.5 + x / (2.0 * rad), 0.0, 1.0)
			bones.append_array([hips, leg_l, leg_r, 0])
			weights.append_array([1.0 - t, t * side, t * (1.0 - side), 0.0])
	for r in rings - 1:
		for k in seg:
			var a := r * seg + k
			var b := r * seg + (k + 1) % seg
			idx.append_array([a, b, a + seg, b, b + seg, a + seg])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_COLOR] = colors
	arrays[Mesh.ARRAY_BONES] = bones
	arrays[Mesh.ARRAY_WEIGHTS] = weights
	arrays[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	_held.append(mesh)
	var cloth: GeometryInstance3D = ClassDB.instantiate(&"FellowCloth3D")
	cloth.name = "Skirt"
	cloth.set("source_mesh", mesh)
	cloth.set("pin_mode", 5) # PIN_VERTEX_COLOR
	cloth.set("material", p_mat)
	cloth.set("collide_with_world", false)
	cloth.set("follow_skin", true)
	cloth.set("skin_max_distance", SKIRT_FREE)
	cloth.set("skin_distance_mode", 1) # SKIN_DISTANCE_FROM_PINS
	cloth.set("skin_backstop", true)
	cloth.set("skin_backstop_distance", 0.01)
	cloth.set("thickness", 0.02)
	cloth.set("wind_scale", 0.35)
	cloth.set("mass", 0.6)
	cloth.set("substep_rate", 240) # (a skirt's swing needs no flag's 480 a second)
	cloth.set("lod_distance", 12.0)
	cloth.set("simulation_distance", SKIRT_SIMULATE)
	cloth.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	# (on the skeleton's parent, as the skeleton: the mesh is in its space)
	(p_skel.get_parent() as Node3D).add_child(cloth)
	cloth.transform = p_skel.transform
	cloth.set("skeleton_path", cloth.get_path_to(p_skel))
	var paths := []
	# (the legs, and the hands and forearms swinging by the hips)
	for b in [["LeftUpperLeg", 0.105, 0.44], ["RightUpperLeg", 0.105, 0.44], ["LeftLowerLeg", 0.08, 0.44], ["RightLowerLeg", 0.08, 0.44],
			["LeftLowerArm", 0.05, 0.26], ["RightLowerArm", 0.05, 0.26], ["LeftHand", 0.06, 0.2], ["RightHand", 0.06, 0.2]]:
		var at := BoneAttachment3D.new()
		at.bone_name = b[0]
		p_skel.add_child(at)
		var cs := CollisionShape3D.new()
		var cap := CapsuleShape3D.new()
		cap.radius = b[1]
		cap.height = b[2]
		_held.append(cap)
		cs.shape = cap
		cs.position = Vector3(0.0, (b[2] as float) * 0.5, 0.0)
		at.add_child(cs)
		paths.append(cloth.get_path_to(cs))
	cloth.set("colliders", paths)


func _attach(p_skel: Skeleton3D, p_bone: String, p_mesh: Mesh, p_offset: Vector3) -> void:
	if p_skel.find_bone(p_bone) < 0:
		return
	var at := BoneAttachment3D.new()
	at.bone_name = p_bone
	p_skel.add_child(at)
	var mi := MeshInstance3D.new()
	mi.mesh = p_mesh
	_held.append(p_mesh)
	_held.append(p_mesh.surface_get_material(0))
	mi.position = p_offset
	at.add_child(mi)


func _face(p_node: Node3D, p_dir: Vector3) -> void:
	var d := Vector3(p_dir.x, 0.0, p_dir.z)
	if d.length() > 1e-3:
		p_node.rotation.y = atan2(d.x, d.z)


static func _length(p_pts: PackedVector3Array, p_loop: bool) -> float:
	var l := 0.0
	for i in p_pts.size() - 1:
		l += p_pts[i].distance_to(p_pts[i + 1])
	if p_loop:
		l += p_pts[p_pts.size() - 1].distance_to(p_pts[0])
	return l


## The point at p_s metres along the path (a loop wraps; an open path goes there and back over twice its length).
static func _at(p_pts: PackedVector3Array, p_loop: bool, p_length: float, p_s: float) -> Vector3:
	var s := fposmod(p_s, p_length * (1.0 if p_loop else 2.0))
	if not p_loop and s > p_length:
		s = 2.0 * p_length - s
	var n := p_pts.size()
	var segs := n if p_loop else n - 1
	for i in segs:
		var a := p_pts[i]
		var b := p_pts[(i + 1) % n]
		var l := a.distance_to(b)
		if s <= l:
			return a.lerp(b, s / maxf(l, 1e-4))
		s -= l
	return p_pts[n - 1] if not p_loop else p_pts[0]


func _process(delta: float) -> void:
	var pp := _player.global_position if _player else Vector3(1e6, 0.0, 1e6)
	for p in _people:
		if p.react != "" and p.react != "walk_on":
			_react(p, delta)
		elif (p.shove as Vector3).length_squared() > 1e-6:
			# (a shove given back)
			p.shove = (p.shove as Vector3) * exp(-3.0 * delta)
			if p.kind != "walk":
				(p.node as Node3D).position = (p.home as Vector3) + (p.shove as Vector3)
	for w in _walkers:
		var person: Dictionary = w.person
		if person.react != "" and person.react != "walk_on" or person.act in ["talk", "point", "leave", "away", "back"]:
			continue # (its way waits: the reaction moves it; milestone 212: talking, pointing; 222: gone home)
		w.s += (w.speed as float) * (w.pace as float) * delta
		var pts: PackedVector3Array = w.pts
		var here := _at(pts, w.loop, w.length, w.s)
		var ahead := _at(pts, w.loop, w.length, (w.s as float) + 0.7)
		var dir := ahead - here
		dir.y = 0.0
		if dir.length() < 1e-3:
			continue
		dir = dir.normalized()
		var side := Vector3(-dir.z, 0.0, dir.x)
		# Stepping aside for the player (ahead of them, near): pushed off to the side the player is not on.
		var to_p := Vector3(pp.x - here.x, 0.0, pp.z - here.z)
		var push := 0.0
		if to_p.length() < 2.2 and absf(pp.y - here.y) < 2.0:
			push = -signf(to_p.dot(side) - (w.lane as float) * 0.1) * (1.0 - to_p.length() / 2.2) * 1.2
		w.push = lerpf(w.push, push, 1.0 - exp(-4.0 * delta))
		var node: Node3D = w.node
		# (milestone 208: off its way after a reaction, back into it as it walks on; a shove given back)
		w.detour = (w.detour as Vector3) * exp(-0.9 * delta)
		if person.react == "walk_on" and (w.detour as Vector3).length() < 0.03:
			person.react = ""
		node.position = here + side * ((w.lane as float) + (w.push as float)) + (w.detour as Vector3) + (person.shove as Vector3)
		var goal := atan2(dir.x, dir.z)
		node.rotation.y = lerp_angle(node.rotation.y, goal, 1.0 - exp(-6.0 * delta))
	_social(delta)


# ------------------------------------------------------------------ milestone 208: run into, knocked over, up again


## A person's record and its capsule (sitting: lower, over the bench).
func _register(p_node: Node3D, p_kind: String, p_clip: String, p_speed: float) -> Dictionary:
	var body := AnimatableBody3D.new()
	body.name = "Body"
	body.sync_to_physics = false
	body.collision_layer = LAYER_PEOPLE
	body.collision_mask = 0
	var cs := CollisionShape3D.new()
	var cap := CapsuleShape3D.new()
	cap.radius = 0.27
	cap.height = 1.15 if p_kind == "seat" else 1.72
	cs.shape = cap
	cs.position = Vector3(0.0, 0.6, -0.12) if p_kind == "seat" else Vector3(0.0, 0.86, 0.0)
	body.add_child(cs)
	p_node.add_child(body)
	_held.append(cap)
	body.set_meta("person", _people.size())
	var p := {"node": p_node, "ap": p_node.get_node_or_null("AnimationPlayer"), "skel": p_node.find_child("Skeleton3D", true, false),
			"shape": cs, "kind": p_kind, "clip": p_clip, "speed": p_speed, "home": p_node.position, "face": p_node.rotation.y,
			"react": "", "t": 0.0, "shove": Vector3(), "nudged": -1.0, "turn_to": 0.0, "pa": null, "wait": 0, "knock_v": Vector3(), "down_t": 0.0,
			"act": "", "look": null, "look_w": 0.0, "knocked": 0}
	_people.append(p)
	return p


## Run into by the player (its velocity before the contact, the contact's normal on the player): the reaction given,
## REACT_NONE if it was not a person or one already reacting.
func bump(p_collider: Object, p_velocity: Vector3, p_normal: Vector3) -> int:
	if not (p_collider is Node) or not (p_collider as Node).has_meta("person"):
		return REACT_NONE
	var p: Dictionary = _people[int((p_collider as Node).get_meta("person"))]
	var speed := maxf(0.0, -p_velocity.dot(p_normal))
	var from_above := p_normal.y > 0.6 and p_velocity.y < -2.5
	var push := Vector3(p_velocity.x, 0.0, p_velocity.z)
	if push.length() < 0.2:
		push = -Vector3(p_normal.x, 0.0, p_normal.z)
	if push.length() < 1e-3:
		push = (p.node as Node3D).global_basis.z
	push = push.normalized()
	if p.react != "" and p.react != "walk_on" and p.react != "home" and not (p.react == "stagger" and speed >= KNOCK_MIN):
		return REACT_NONE
	_bumps += 1
	if p.act != "" and p.act != "away" and (p.kind != "seat" and speed >= NUDGE_MAX or from_above):
		# (milestone 212: talking, pointing, leaving — over; a walker's way and the others' place as before)
		if p.act == "talk" and not _talk.is_empty():
			_talk = {}
		if p.kind == "walk" and p.act in ["leave", "back"]:
			_walker_detour(p) # (milestone 222: its way from where the bump caught it)
		p.act = ""
		_bubbles.call("hush", p.node)
	if p.kind == "seat" or (speed < NUDGE_MAX and not from_above):
		# (a shove at most every NUDGE_EVERY, and no further than NUDGE_FAR: leant on, they give a little way)
		var now := Time.get_ticks_msec() / 1000.0
		if now - float(p.nudged) < NUDGE_EVERY:
			return REACT_NONE
		p.nudged = now
		p.shove = ((p.shove as Vector3) + push * (0.1 + 0.08 * speed)).limit_length(NUDGE_FAR)
		return REACT_NUDGE
	if speed < KNOCK_MIN and not from_above:
		_stagger(p, push)
		if _rng.randf() < 0.7:
			_bubbles.call("say", p.node, ["Ehi!", "Attento!", "Watch it!", "Oof!"][_rng.randi() % 4], 1.6, _head_height(p))
		return REACT_STAGGER
	_knock(p, push * clampf(speed * 0.7, 2.5, 4.5))
	p.knocked = int(p.knocked) + 1
	return REACT_KNOCK


func _stagger(p: Dictionary, p_push: Vector3) -> void:
	var node: Node3D = p.node
	var front := Vector3(node.global_basis.z.x, 0.0, node.global_basis.z.z).normalized()
	# (pushed against its front: back; from behind: forward — turned square to the push as it goes)
	var back := front.dot(p_push) < 0.0
	p.turn_to = atan2(-p_push.x, -p_push.z) if back else atan2(p_push.x, p_push.z)
	_begin(p, "stagger")
	_root_motion(p, true)
	var ap: AnimationPlayer = p.ap
	ap.speed_scale = 1.0
	ap.play("stagger_back" if back else "stagger_fwd", 0.08)


func _knock(p: Dictionary, p_velocity: Vector3) -> void:
	var skel: Skeleton3D = p.skel
	var ap: AnimationPlayer = p.ap
	_begin(p, "knock")
	(p.shape as CollisionShape3D).disabled = true
	_root_motion(p, false)
	skel.modifier_callback_mode_process = Skeleton3D.MODIFIER_CALLBACK_MODE_PROCESS_PHYSICS
	ap.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_PHYSICS
	var pa: Node = p.pa
	if pa == null:
		pa = ClassDB.instantiate(&"FellowPhysicalAnimation3D")
		pa.name = "Physical"
		var sc := (p.node as Node3D).scale.x
		pa.set("total_mass", 70.0 * sc * sc * sc)
		pa.set("anatomical_limits", true)
		pa.set("auto_get_up", false)
		pa.set("collision_layer", LAYER_RAGDOLL)
		pa.set("collision_mask", LAYER_WORLD)
		skel.add_child(pa)
		p.pa = pa
	pa.call("build")
	# (knocked down once the bodies stand where the animation has them: the next physics steps)
	p.wait = 2
	p.knock_v = p_velocity


func _begin(p: Dictionary, p_react: String) -> void:
	p.react = p_react
	p.t = 0.0
	p.down_t = 0.0
	# (a walker's way waits — its place on it, its stepping aside — and where the reaction takes it is its detour; the
	# others walk back to their place, `home`, after it)


func _root_motion(p: Dictionary, p_on: bool) -> void:
	var ap: AnimationPlayer = p.ap
	ap.root_motion_track = ROOT_TRACK if p_on else NodePath()


func _physics_process(_delta: float) -> void:
	for p in _people:
		if p.react == "getup":
			# (the get-up's clip is played in the physics step, with the bodies: its root motion taken there)
			_move_by(p, (p.node as Node3D).basis * (p.ap as AnimationPlayer).get_root_motion_position())
		if p.react == "knock" and int(p.wait) > 0:
			p.wait = int(p.wait) - 1
			if int(p.wait) == 0:
				(p.ap as AnimationPlayer).pause()
				p.pa.call("knock_down", p.knock_v)
				p.react = "down"


func _react(p: Dictionary, delta: float) -> void:
	var node: Node3D = p.node
	var ap: AnimationPlayer = p.ap
	p.t = float(p.t) + delta
	match String(p.react):
		"stagger", "getup":
			if String(p.react) == "stagger":
				node.rotation.y = lerp_angle(node.rotation.y, float(p.turn_to), 1.0 - exp(-22.0 * delta))
				_move_by(p, node.basis * ap.get_root_motion_position())
			if not ap.is_playing() or ap.current_animation_position >= ap.current_animation_length - 1e-3:
				if String(p.react) == "getup":
					_stand_again(p)
				_root_motion(p, false)
				_after(p)
		"down":
			var pa: Node = p.pa
			var lying := int(pa.call("get_state")) == 3 # FellowPhysicalAnimation3D.STATE_DOWN
			p.down_t = float(p.down_t) + (delta if lying else 0.0)
			if float(p.down_t) >= DOWN_WAIT or float(p.t) > 5.0:
				_get_up(p)
		"home":
			# Walking back to its place (the walk's pace), then facing as it was and back to what it was doing; waiting
			# while the player stands in the way.
			var to := (p.home as Vector3) - node.position
			to.y = 0.0
			var pp := _player.global_position if _player else Vector3(1e6, 0.0, 1e6)
			var in_way := Vector3(pp.x - node.position.x, 0.0, pp.z - node.position.z)
			if to.length() >= 0.06 and in_way.length() < 1.0 and in_way.dot(to) > 0.0 and absf(pp.y - node.position.y) < 1.5:
				if ap.current_animation != String(p.clip):
					ap.speed_scale = float(p.speed)
					ap.play(String(p.clip), 0.3)
				node.rotation.y = lerp_angle(node.rotation.y, atan2(in_way.x, in_way.z), 1.0 - exp(-5.0 * delta))
				return
			if to.length() < 0.06:
				node.position = p.home
				node.rotation.y = lerp_angle(node.rotation.y, float(p.face), 1.0 - exp(-8.0 * delta))
				if absf(angle_difference(node.rotation.y, float(p.face))) < 0.05:
					p.react = ""
					p.shove = Vector3()
					ap.speed_scale = float(p.speed)
					ap.play(String(p.clip), 0.3)
			else:
				if ap.current_animation != "walk":
					ap.speed_scale = 1.0 / 1.6
					ap.play("walk", 0.25)
				node.position += to.normalized() * minf(to.length(), 1.0 * delta)
				node.rotation.y = lerp_angle(node.rotation.y, atan2(to.x, to.z), 1.0 - exp(-8.0 * delta))


## A reaction's way: a walker's detour from its way, the others' place.
func _move_by(p: Dictionary, p_by: Vector3) -> void:
	(p.node as Node3D).position += p_by
	if p.kind == "walk":
		for w in _walkers:
			if w.person == p:
				w.detour = (w.detour as Vector3) + p_by


## Lying: placed where the body lies, the get-up clip for the side it lies on played there, the bodies blended into it.
func _get_up(p: Dictionary) -> void:
	var node: Node3D = p.node
	var ap: AnimationPlayer = p.ap
	var pa: Node = p.pa
	var hips := Vector3()
	var head := Vector3()
	for b in (p.skel as Skeleton3D).find_children("*", "PhysicalBone3D", true, false):
		var pb := b as PhysicalBone3D
		if pb.bone_name == "Hips":
			hips = pb.global_position
		elif pb.bone_name == "Head":
			head = pb.global_position
	var face := int(pa.call("get_landed_face")) # 0 on the back, 1 on the face, 2 on a side
	var d := Vector3(head.x - hips.x, 0.0, head.z - hips.z)
	d = d.normalized() if d.length() > 1e-3 else node.global_basis.z
	# (on a side: the clip for the way the chest turns, up or down — the hips' front, the skeleton's +Z)
	var skel: Skeleton3D = p.skel
	var front := (skel.global_basis * skel.get_bone_global_pose(skel.find_bone("Hips")).basis).z
	var down := face == 1 or (face == 2 and front.y < 0.0)
	var ground := hips.y - 0.15
	var q := PhysicsRayQueryParameters3D.create(hips + Vector3.UP * 0.3, hips + Vector3.DOWN * 1.5, LAYER_WORLD)
	var hit := get_world_3d().direct_space_state.intersect_ray(q)
	if not hit.is_empty():
		ground = (hit.position as Vector3).y
	var was := node.global_position
	# (on the face the clip's head is ahead of it, its front +Z; on the back behind it)
	node.global_rotation = Vector3(0.0, atan2(d.x, d.z) if down else atan2(-d.x, -d.z), 0.0)
	node.global_position = Vector3(hips.x, ground, hips.z)
	if p.kind == "walk":
		for w in _walkers:
			if w.person == p:
				w.detour = (w.detour as Vector3) + (node.global_position - was)
	_root_motion(p, true)
	ap.speed_scale = 1.0
	ap.play("get_up_front" if down else "get_up_back")
	ap.seek(0.0, true)
	pa.call("get_up_from_animation", GET_UP_BLEND)
	p.react = "getup"
	p.t = 0.0
	_bubbles.call("say", node, ["Ow... my back.", "Maleducato!", "Are you mad?!", "Santo cielo..."][_rng.randi() % 4], 2.4, 1.3)


## Standing after the get-up: the bodies cleared (they follow the animation within millimetres), the capsule back.
func _stand_again(p: Dictionary) -> void:
	var pa: Node = p.pa
	if pa:
		pa.call("clear")
	(p.skel as Skeleton3D).modifier_callback_mode_process = Skeleton3D.MODIFIER_CALLBACK_MODE_PROCESS_IDLE
	(p.ap as AnimationPlayer).callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_IDLE
	(p.shape as CollisionShape3D).disabled = false


func _after(p: Dictionary) -> void:
	var ap: AnimationPlayer = p.ap
	if p.kind == "walk":
		p.react = "walk_on"
		ap.speed_scale = float(p.speed)
		ap.play("walk", 0.3)
	else:
		p.react = "home"


## (tools) a person's reaction now ("" if none) and its bodies' state (-1 without them).
func get_person_state(p_node: Node) -> Array:
	for p in _people:
		if p.node == p_node:
			return [String(p.react), int(p.pa.call("get_state")) if p.pa != null and bool(p.pa.call("is_built")) else -1]
	return ["?", -1]


## (tools) a person's bodies' balance statistics (FellowPhysicalAnimation3D.get_balance_stats(); empty if never knocked).
func get_person_stats(p_node: Node) -> Dictionary:
	for p in _people:
		if p.node == p_node and p.pa != null:
			return p.pa.call("get_balance_stats")
	return {}


## (tools) how many people reacted, and how many are reacting now.
func get_reaction_stats() -> Dictionary:
	var now := 0
	for p in _people:
		if p.react != "" and p.react != "walk_on":
			now += 1
	return {"bumps": _bumps, "reacting": now, "people": _people.size()}


# ------------------------------------------------------------------ milestone 212: talking, looking, the weather

## Milestone 212 (the owner: "some interactions with the NPCs"):
##  * they look at the player (the head, LookAtModifier3D) — those within LOOK_NEAR, and everyone within LOOK_UP_FAR
##    while the player is up on a wall or a roof; the LOOK_MAX nearest at once;
##  * someone pointing up at the player climbing ("point_up"), shouting, at most once every POINT_EVERY;
##  * talked to (E within TALK_REACH, in front of the player, on the ground): they stop, turn to the player, wave
##    ("wave") and talk ("talk"), their lines in a bubble (rooftops_bubbles.gd) — about the weather, the player's
##    climbing, being knocked down by them — E for the next; they go back to what they were doing after;
##  * in the rain (its intensity over RAIN_LEAVE) the square's standing groups leave it by the nearest street and come
##    back when it stops; the walkers hurry; the stall keepers huddle under their awnings;
##  * round the brazier (fire_people of them): in the cold (under COLD) they warm their hands ("warm_hands");
##  * run into, they shout.
@export var fire_path := NodePath("../Brazier")
@export var fire_people := 3
@export var weather_path := NodePath("../Weather")

const LOOK_NEAR := 7.0 # m
const LOOK_UP_FAR := 24.0 # m
const LOOK_MAX := 10
const TALK_REACH := 2.4 # m
const POINT_EVERY := 6.0 # s
const RAIN_LEAVE := 0.3 # the weather's rain intensity
const RAIN_BACK := 0.12
const COLD := 5.0 # °C
const FIRE_RING := 1.2 # m from the brazier's middle
const HURRY := 1.45 # the walkers' pace in the rain
## The streets out of the square: its mouth and on along the street to where they are out of sight.
const EXITS := [[Vector3(-1.8, 0, 12.5), Vector3(-1.8, 0, 30.0)], [Vector3(16.0, 0, 3.0), Vector3(30.0, 0, 3.0)],
		[Vector3(6.8, 0, -12.0), Vector3(6.8, 0, -24.0)], [Vector3(-16.5, 0, 5.0), Vector3(-30.0, 0, 5.0)]]
## What stands in the square (a circle each: x, z, radius), walked round.
const OBSTACLES := [Vector3(-3.5, 1.0, 3.1), Vector3(-7.2, 4.2, 0.9), Vector3(-1.14, -7.35, 2.3), Vector3(-12.0, -6.5, 1.9),
		Vector3(11.0, 7.5, 1.9)]

var _bubbles: CanvasLayer
var _weather: Node
var _rain := 0.0 # the weather's rain now (not snow)
var _cold := false
## Milestone 218: night on the square (rooftops_night.gd's get_night() over NIGHT_AWAY): the standing groups go home or
## into the osteria as in the rain, and back by day; every LANTERN_EVERY-th walker carries a lantern.
const NIGHT_AWAY := 0.6
const LANTERN_EVERY := 3
var _night := false
var _night_node: Node
var _lanterns: Array = [] # [BoneAttachment3D]
## Milestone 225: breath in the cold — under COLD everyone near the camera (and the player) breathes out a small puff of
## mist every few seconds (a GPUParticles3D at the mouth: BREATH_NEAR metres, the puffs left in the air, lit by the
## scene), and stops in the warm.
const BREATH_NEAR := 22.0
var _breaths: Array = [] # [GPUParticles3D, the person's Dictionary or null for the player, its pace]
var _breath_on := false
var _sample_t := 0.0
var _point_t := 0.0
var _up_t := -1e9 # when the player was last seen up there (s)
var _talk := {} # the conversation: {p, lines, i, t}
var _hint_p = null
var _talks := 0 # (tools)
var _points := 0
var _now := 0.0
var _player_skel: Skeleton3D
var _shelter: Array = [] # milestone 213: the osteria's places out of the rain ({out, in, spot, taken})
var _player_head := -1


func _social_ready() -> void:
	_weather = get_node_or_null(weather_path)
	_bubbles = CanvasLayer.new()
	_bubbles.set_script(load("res://scripts/rooftops_bubbles.gd"))
	add_child(_bubbles)
	if _player:
		_player_skel = _player.get_node_or_null("Model/Mannequin/Skeleton3D") as Skeleton3D
		if _player_skel:
			_player_head = _player_skel.find_bone("Head")
	var fire := get_node_or_null(fire_path) as Node3D
	if fire and fire_people > 0:
		var c := fire.global_position
		for i in fire_people:
			var a := TAU * (i + 0.35) / fire_people + _rng.randf_range(-0.25, 0.25)
			var node := _person("talk" if i % 2 == 0 else "idle", 1.0)
			node.position = c + Vector3(cos(a), 0.0, sin(a)) * FIRE_RING
			_face(node, c - node.position)
			var p := _register(node, "stand", "talk" if i % 2 == 0 else "idle", 1.0)
			p.fire = true
	# Milestone 213: the osteria's people (its host, its guests) and its shelter from the rain.
	for o in get_tree().get_nodes_in_group(&"osteria"):
		for e in o.call("get_people"):
			var xf: Transform3D = e.xform
			var node := _person(String(e.clip), 1.0)
			node.global_position = xf.origin
			_face(node, -xf.basis.z)
			var p := _register(node, String(e.kind), String(e.clip), 1.0)
			p.inside = true
		_shelter.append_array(o.call("get_shelter"))
	for p in _people:
		_look_setup(p)
	# Milestone 227: footprints in the snow — everyone's feet and the player's.
	for p in _people:
		_foot_trackers(p.skel)
	if _player_skel:
		_foot_trackers(_player_skel)


## The head turned to a target of its own (LookAtModifier3D after the clip, as the player's: tools/build_rooftops.gd).
func _look_setup(p: Dictionary) -> void:
	var skel: Skeleton3D = p.skel
	if skel == null:
		return
	var target := Node3D.new()
	target.name = "LookTarget"
	target.top_level = true
	(p.node as Node3D).add_child(target)
	var look := LookAtModifier3D.new()
	look.name = "HeadLook"
	look.bone_name = "Head"
	look.relative = true
	look.duration = 0.35
	look.transition_type = Tween.TRANS_SINE
	look.ease_type = Tween.EASE_IN_OUT
	look.use_angle_limitation = true
	look.symmetry_limitation = true
	look.primary_limit_angle = deg_to_rad(120.0)
	look.primary_damp_threshold = 0.6
	look.secondary_limit_angle = deg_to_rad(80.0)
	look.secondary_damp_threshold = 0.6
	look.influence = 0.0
	look.active = false
	skel.add_child(look)
	look.target_node = look.get_path_to(target)
	p.look = look
	p.look_target = target
	p.look_w = 0.0


func _player_eyes() -> Vector3:
	if _player_skel and _player_head >= 0:
		return _player_skel.global_transform * _player_skel.get_bone_global_pose(_player_head).origin
	return _player.global_position + Vector3.UP * 1.6


## The player up on a wall or a roof (over the square's ground).
func _player_up() -> bool:
	return _player != null and _player.global_position.y > 2.5


func _player_ground_near(p_at: Vector3, p_reach: float) -> bool:
	if _player == null or not (_player as CharacterBody3D).is_on_floor():
		return false
	var d := _player.global_position - p_at
	return Vector2(d.x, d.z).length() < p_reach and absf(d.y) < 1.0


func _busy(p: Dictionary) -> bool:
	return p.react != "" and p.react != "walk_on" and p.react != "home"


func _social(delta: float) -> void:
	if _player == null:
		return
	_now += delta
	_sample_t -= delta
	if _sample_t <= 0.0:
		_sample_t = 0.5
		_sample_weather()
		_weather_acts()
	var up := _player_up()
	if up:
		_up_t = _now
	var eyes := _player_eyes()
	var pp := _player.global_position
	# Who looks: the nearest LOOK_MAX of those near, or (the player up there) within LOOK_UP_FAR.
	var want: Array = []
	for p in _people:
		if _busy(p) or p.get("look") == null or p.act == "away" and not (p.node as Node3D).visible:
			continue
		var d := Vector2(pp.x - (p.node as Node3D).global_position.x, pp.z - (p.node as Node3D).global_position.z).length()
		if p.act == "talk" or d < LOOK_NEAR or (up and d < LOOK_UP_FAR):
			want.append([0.0 if p.act == "talk" else d, p])
	want.sort_custom(func(a, b): return a[0] < b[0])
	var looking := {}
	for k in mini(want.size(), LOOK_MAX):
		looking[want[k][1].node] = true
	for p in _people:
		var look: LookAtModifier3D = p.get("look")
		if look == null:
			continue
		var goal := 1.0 if looking.has(p.node) and not _busy(p) else 0.0
		p.look_w = move_toward(float(p.look_w), goal, delta / 0.5)
		look.influence = p.look_w
		look.active = p.look_w > 0.001
		if look.active:
			(p.look_target as Node3D).global_position = eyes
	# Someone pointing up at the player.
	_point_t -= delta
	if up and _point_t <= 0.0:
		_point_t = POINT_EVERY
		_point_at_player(pp)
	_talk_update(delta)
	_acts(delta)


func _sample_weather() -> void:
	if _weather == null:
		return
	var st: Dictionary = _weather.call("get_current_state")
	_rain = float(st.get("intensity", 0.0)) if int(st.get("precipitation", 0)) == 1 else 0.0
	_cold = float(st.get("temperature", 18.0)) < COLD
	if _night_node == null:
		_night_node = get_node_or_null(^"../Night")
	var night := _night_node != null and float(_night_node.call("get_night")) > NIGHT_AWAY
	if night != _night:
		_night = night
		_light_lanterns(night)


## Milestone 218: the walkers' lanterns lit at night (made the first time: a small iron lantern in the right hand, its
## candle's warm light — no shadows), put out by day.
func _light_lanterns(p_on: bool) -> void:
	if p_on and _lanterns.is_empty():
		var body := CylinderMesh.new()
		body.top_radius = 0.05
		body.bottom_radius = 0.06
		body.height = 0.16
		var glow := StandardMaterial3D.new()
		glow.albedo_color = Color(0.25, 0.2, 0.15)
		glow.emission_enabled = true
		glow.emission = Color(1.0, 0.62, 0.3)
		glow.emission_energy_multiplier = 4.0
		body.material = glow
		for i in _walkers.size():
			if i % LANTERN_EVERY != 0:
				continue
			var p: Dictionary = _walkers[i].person
			var skel := p.skel as Skeleton3D
			if skel == null or skel.find_bone("RightHand") < 0:
				continue
			var at := BoneAttachment3D.new()
			at.name = "Lantern"
			at.bone_name = "RightHand"
			skel.add_child(at)
			var mi := MeshInstance3D.new()
			mi.mesh = body
			mi.position = Vector3(0.0, -0.16, 0.0)
			mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			at.add_child(mi)
			var light := OmniLight3D.new()
			light.light_color = Color(1.0, 0.66, 0.36)
			light.light_energy = 1.2
			light.omni_range = 5.0
			light.omni_attenuation = 1.4
			light.shadow_enabled = false
			light.position = Vector3(0.0, -0.16, 0.0)
			at.add_child(light)
			_lanterns.append(at)
		_held.append(body)
		_held.append(glow)
	for at in _lanterns:
		(at as Node3D).visible = p_on


# ------------------------------------------------------------------ pointing


func _point_at_player(p_at: Vector3) -> void:
	var best = null
	var best_d := LOOK_UP_FAR
	for p in _people:
		if _busy(p) or p.act != "" or p.kind == "seat" or p.get("fire", false) and _cold:
			continue
		if _now - float(p.get("pointed", -1e9)) < 30.0:
			continue
		var at := (p.node as Node3D).global_position
		var d := Vector2(p_at.x - at.x, p_at.z - at.z).length()
		if d < best_d and d > 3.0:
			best_d = d
			best = p
	if best == null:
		return
	var p: Dictionary = best
	p.pointed = _now
	p.act = "point"
	p.t = 0.0
	var to := p_at - (p.node as Node3D).global_position
	p.turn_to = atan2(to.x, to.z)
	var ap: AnimationPlayer = p.ap
	ap.speed_scale = 1.0
	ap.play("point_up", 0.25)
	_points += 1
	_bubbles.call("say", p.node, ["Guarda! Up there!", "Look — on the roof!", "Madonna, he'll break his neck!",
			"Hey! Get down from there!", "Mamma mia, look at him climb!"][_rng.randi() % 5], 2.8, _head_height(p))


# ------------------------------------------------------------------ talking


func _head_height(p: Dictionary) -> float:
	return (1.62 if p.kind == "seat" else 2.12) * (p.node as Node3D).scale.y


func _unhandled_input(event: InputEvent) -> void:
	if _player == null or not event.is_action_pressed(&"interact") or event.is_echo():
		return
	if not _talk.is_empty():
		_talk_next()
		get_viewport().set_input_as_handled()
		return
	var p = _talkable()
	if p != null:
		_talk_start(p)
		get_viewport().set_input_as_handled()


## The one in front of the player within TALK_REACH, nearest (the player on the ground).
func _talkable():
	if _player == null or not (_player as CharacterBody3D).is_on_floor() or int(_player.get("mode")) != 0:
		return null
	var pp := _player.global_position
	var f: float = _player.get("facing")
	var ahead := Vector3(-sin(f), 0.0, -cos(f))
	var best = null
	var best_s := 1e9
	for p in _people:
		if _busy(p) or p.act == "away" or p.act == "leave" or p.act == "back":
			continue
		var at := (p.node as Node3D).global_position
		var d := Vector3(at.x - pp.x, 0.0, at.z - pp.z)
		var l := d.length()
		if l > TALK_REACH or absf(at.y - pp.y) > 1.0:
			continue
		var facing := d.normalized().dot(ahead) if l > 0.05 else 1.0
		if facing < 0.15:
			continue
		var s := l * (2.0 - facing)
		if s < best_s:
			best_s = s
			best = p
	return best


## (tools) talk to the one in front of the player (as E does): whether there was one.
func talk_to_nearest() -> bool:
	if not _talk.is_empty():
		_talk_next()
		return true
	var p = _talkable()
	if p == null:
		return false
	_talk_start(p)
	return true


func _talk_start(p: Dictionary) -> void:
	_talks += 1
	p.act = "talk"
	p.t = 0.0
	var ap: AnimationPlayer = p.ap
	var warming := ap.current_animation == "warm_hands" or ap.current_animation == "huddle"
	if p.kind != "seat" and not warming:
		var to := _player.global_position - (p.node as Node3D).global_position
		p.turn_to = atan2(to.x, to.z)
		ap.speed_scale = 1.0
		ap.play("wave", 0.25)
	_talk = {"p": p, "lines": _lines(p), "i": -1, "t": 0.0}
	_talk_next()


func _talk_next() -> void:
	var p: Dictionary = _talk.p
	_talk.i = int(_talk.i) + 1
	_talk.t = 0.0
	var lines: Array = _talk.lines
	if int(_talk.i) >= lines.size():
		_talk_end(true)
		return
	var last := int(_talk.i) == lines.size() - 1
	_bubbles.call("say", p.node, lines[_talk.i], 0.0, _head_height(p), not last)


func _talk_end(p_farewell: bool) -> void:
	var p: Dictionary = _talk.p
	_talk = {}
	if p_farewell:
		_bubbles.call("say", p.node, ["Arrivederci!", "A presto!", "Ciao, ciao.", "Buona serata!", "Go with God."][_rng.randi() % 5], 2.2, _head_height(p))
	else:
		_bubbles.call("hush", p.node)
	_act_done(p)


func _talk_update(delta: float) -> void:
	if not _talk.is_empty():
		var p: Dictionary = _talk.p
		_talk.t = float(_talk.t) + delta
		if _busy(p) or p.act != "talk":
			# (run into while talking: the talk is over)
			_talk = {}
			_bubbles.call("hush", p.node)
			return
		var at := (p.node as Node3D).global_position
		var away := Vector2(_player.global_position.x - at.x, _player.global_position.z - at.z).length()
		if away > TALK_REACH + 2.2:
			_talk_end(true)
		elif float(_talk.t) > 7.0:
			_talk_next()
		return
	var h = _talkable()
	if h != null:
		_bubbles.call("hint", h.node, _head_height(h))


## What they say: a greeting; the weather, or the player's climbing, or being knocked down by them; a word on the town.
func _lines(p: Dictionary) -> Array:
	var r := _rng
	var out: Array = []
	if int(p.get("knocked", 0)) > 0:
		out.append(["You! You knocked me flat!", "Ah, it's you. My back still aches.", "You again? Keep your distance, ragazzo."][r.randi() % 3])
	else:
		out.append(["Buonasera!", "Buonasera, forestiero.", "Salve!", "Ah, buonasera!"][r.randi() % 4])
	if _now - _up_t < 90.0:
		out.append(["I saw you up on the roofs! Are you mad?", "Climbing the palazzo like a cat... the guards will hear of it.",
				"From up there you can see all the way to the hills, no?"][r.randi() % 3])
	var st: Dictionary = _weather.call("get_current_state") if _weather else {}
	var precip := int(st.get("precipitation", 0))
	var strength := float(st.get("intensity", 0.0))
	if _night and precip == 0:
		out.append(["Late to be out, forestiero. The osteria is still open.", "Look at the moon over the tower!",
				"Mind the dark corners — and the watch."][r.randi() % 3])
	elif precip == 1 and float(st.get("lightning_rate", 0.0)) > 1.0:
		out.append(["Hear that thunder? Saint Barbara, protect us.", "A storm like this, the bells should be ringing."][r.randi() % 2])
	elif precip == 1 and strength > 0.1:
		out.append(["What rain! Better find a roof.", "The stones will be slippery tonight, watch your step."][r.randi() % 2])
	elif precip == 2 and strength > 0.1:
		out.append(["Snow in the piazza! My grandmother never saw such a winter.", "Brr... come, warm your hands at the fire."][r.randi() % 2])
	elif float(st.get("cloud_coverage", 0.0)) > 0.7:
		out.append(["Grey skies. It'll rain before vespers.", "Smell that? Rain is coming."][r.randi() % 2])
	else:
		out.append(["What a light this evening — like honey on the stones.", "A fine evening for a walk, eh?"][r.randi() % 2])
	if p.get("fire", false):
		out.append("Old Carlo keeps this fire going all night.")
	elif p.kind == "stand" and keepers.size() > 0 and p.home.distance_to(keepers[0].origin) < 2.0:
		out.append("Peaches, the best in Tuscany! Two soldi.")
	else:
		out.append(["The bell tower? Two hundred steps, but the view...", "They say there's a passage under the square, from the old palazzo.",
				"The fountain's water comes down from the hills, cold as ice.", "Market day tomorrow — come early."][r.randi() % 4])
	return out


# ------------------------------------------------------------------ the acts: pointing, talking, leaving in the rain


## An act over: back to what they were doing (a walker walks on; the others turn back to their place).
func _act_done(p: Dictionary) -> void:
	p.act = ""
	var ap: AnimationPlayer = p.ap
	if p.kind == "walk":
		ap.speed_scale = float(p.speed) * (HURRY if _rain > RAIN_LEAVE else 1.0)
		ap.play("walk", 0.3)
	elif p.kind == "stand":
		p.react = "home"
	else:
		ap.play(String(p.clip), 0.3)


func _acts(delta: float) -> void:
	for p in _people:
		if _busy(p):
			continue
		var node: Node3D = p.node
		var ap: AnimationPlayer = p.ap
		match String(p.act):
			"talk", "point":
				p.t = float(p.t) + delta
				if p.kind != "seat" and not (ap.current_animation in ["warm_hands", "huddle"]):
					node.rotation.y = lerp_angle(node.rotation.y, float(p.turn_to), 1.0 - exp(-6.0 * delta))
				var done := not ap.is_playing() or ap.current_animation_position >= ap.current_animation_length - 1e-3
				if p.act == "talk" and ap.current_animation == "wave" and done:
					ap.play("talk", 0.3)
				elif p.act == "point" and done:
					_act_done(p)
			"leave", "back":
				var path: Array = p.path
				if path.is_empty():
					if p.act == "leave" and p.get("shelter") != null:
						# (inside, standing out of the rain, facing out)
						p.act = "away"
						_face(node, -(p.shelter.spot as Transform3D).basis.z)
						ap.speed_scale = 1.0
						ap.play("talk" if _rng.randf() < 0.5 else "huddle", 0.4)
					elif p.act == "leave":
						p.act = "away"
						node.visible = false
						(p.shape as CollisionShape3D).disabled = true
					elif p.kind == "walk":
						_walker_rejoin(p) # (milestone 222: back on its way)
					else:
						p.act = ""
						p.react = "home" # (the last of the way: the home reaction's walk to its place)
					continue
				var to: Vector3 = (path[0] as Vector3) - node.position
				to.y = 0.0
				var step := 1.35 * delta
				if to.length() <= step:
					node.position = Vector3((path[0] as Vector3).x, node.position.y, (path[0] as Vector3).z)
					path.pop_front()
				else:
					node.position += to.normalized() * step
					node.rotation.y = lerp_angle(node.rotation.y, atan2(to.x, to.z), 1.0 - exp(-8.0 * delta))


## The weather's say on the people (twice a second): in the rain the groups leave and the keepers huddle, after it they
## come back; in the cold the brazier's ring warm their hands.
func _weather_acts() -> void:
	var raining := _rain > RAIN_LEAVE
	var away := raining or _night # (milestone 218: and at night)
	for p in _people:
		if _busy(p):
			continue
		var ap: AnimationPlayer = p.ap
		# (milestone 229: at night the stall keepers close up and go home too; in the rain they huddle under their awnings)
		if p.kind == "stand" and (not _is_keeper(p) or (_night and not raining) or p.act == "away") and not p.get("inside", false):
			if away and p.act == "" and _rng.randf() < 0.35:
				_leave(p)
			elif _rain < RAIN_BACK and not _night and p.act == "away" and _rng.randf() < 0.25:
				_come_back(p)
		var idle: bool = p.act == "" and p.react == ""
		if not idle:
			continue
		var clip := String(p.clip)
		if p.get("fire", false) and _cold:
			clip = "warm_hands"
		elif _is_keeper(p) and raining:
			clip = "huddle"
		if p.kind == "walk":
			ap.speed_scale = float(p.speed) * (HURRY if raining else 1.0)
		elif ap.current_animation != clip:
			ap.speed_scale = 1.0 if clip in ["warm_hands", "huddle"] else float(p.speed)
			ap.play(clip, 0.5)
	for w in _walkers:
		w.pace = HURRY if raining else 1.0
	_walkers_home()
	_breathe()


## Milestone 222: at night the walkers without a lantern go home — WALKERS_HOME of them, each down the street nearest it
## (EXITS) — and by day come back out of it to their way: the square at night is its lantern carriers', the osteria's
## and the few who stay.
const WALKERS_HOME := 0.75
func _walkers_home() -> void:
	for i in _walkers.size():
		var w: Dictionary = _walkers[i]
		var p: Dictionary = w.person
		if _busy(p) or p.react != "":
			continue
		var lantern := i % LANTERN_EVERY == 0
		if _night and not lantern and p.act == "" and _rng.randf() < 0.2:
			if fmod(float(i) * 0.618034, 1.0) >= WALKERS_HOME:
				continue
			var from := (p.node as Node3D).position
			var best: Array = EXITS[0]
			for e in EXITS:
				if from.distance_to(e[0]) < from.distance_to(best[0]):
					best = e
			p.path = _around(from, best[0]) + [best[1]]
			p.exit = best
			p.act = "leave"
			var ap: AnimationPlayer = p.ap
			ap.speed_scale = 1.35 / 1.6
			ap.play("walk", 0.35)
			if _rng.randf() < 0.3:
				var lines: Array = ["Buonanotte!", "A domani!", "Home, before the lamps go out."]
				_bubbles.call("say", p.node, lines[_rng.randi() % lines.size()], 2.2, _head_height(p))
		elif not _night and p.act == "away" and p.get("exit") != null and _rng.randf() < 0.25:
			# (out of the street it went down, to the nearest point of its way)
			var e: Array = p.exit
			var node: Node3D = p.node
			node.position = Vector3(e[1].x, node.position.y, e[1].z)
			node.visible = true
			(p.shape as CollisionShape3D).disabled = false
			var pts: PackedVector3Array = w.pts
			var best_s := 0.0
			var best_d := 1e9
			var span: float = (w.length as float) * (1.0 if w.loop else 2.0)
			for k in 64:
				var s_k := span * float(k) / 64.0
				var d := _at(pts, w.loop, w.length, s_k).distance_to(e[0])
				if d < best_d:
					best_d = d
					best_s = s_k
			w.s = best_s
			p.path = [e[0]] + _around(e[0], _at(pts, w.loop, w.length, best_s))
			p.act = "back"
			var ap: AnimationPlayer = p.ap
			ap.speed_scale = 1.35 / 1.6
			ap.play("walk", 0.35)


## Milestone 227: a FellowSnowTracker3D under each foot (FellowEngine's snow tracks, decision 0130: a print where a
## foot stands, a groove where it drags) — the square's snow trodden through to the cobbles where its people walk and
## stand.
func _foot_trackers(p_skel: Skeleton3D) -> void:
	if p_skel == null or not ClassDB.class_exists(&"FellowSnowTracker3D") or "--tracks=0" in OS.get_cmdline_user_args():
		return
	for bone in ["LeftFoot", "RightFoot"]:
		if p_skel.find_bone(bone) < 0:
			continue
		var at := BoneAttachment3D.new()
		at.name = "Track" + bone
		at.bone_name = bone
		p_skel.add_child(at)
		var tr: Node3D = ClassDB.instantiate(&"FellowSnowTracker3D")
		tr.set("radius", 0.06)
		tr.set("contact_height", 0.14)
		# (down to the cobbles: the snow's 8 cm trodden through where people walk — at 0.8 of it, decision 0130's
		# default, the dents were hardly seen under the snow's flat light)
		tr.set("depth", 0.1)
		at.add_child(tr)


## Milestone 225: the breaths made the first time it is cold (one emitter on each person's head and the player's),
## then each breathing while it is cold, seen and within BREATH_NEAR of the camera.
var _breath_off := "--breath=0" in OS.get_cmdline_user_args() # (tools: an A/B)
func _breathe() -> void:
	if _breath_off or (not _cold and not _breath_on):
		return
	if _cold and _breaths.is_empty():
		_make_breaths()
	_breath_on = _cold
	var cam := get_viewport().get_camera_3d()
	var eye := cam.global_position if cam else Vector3(1e6, 0.0, 1e6)
	for b in _breaths:
		var e := b[0] as GPUParticles3D
		var on := _cold and e.is_visible_in_tree() and e.global_position.distance_to(eye) < BREATH_NEAR
		if e.emitting != on:
			e.emitting = on
		# (milestone 226: faster breathing walking, hurrying away or talking)
		var p = b[1]
		var pace := 1.0
		if p != null:
			if p.act == "talk" or p.act == "point":
				pace = 1.5
			elif p.act == "leave" or p.act == "back":
				pace = 1.4
			elif p.kind == "walk":
				pace = 1.25
		e.speed_scale = float(b[2]) * pace


func _make_breaths() -> void:
	var tex := GradientTexture2D.new()
	tex.fill = GradientTexture2D.FILL_RADIAL
	tex.fill_from = Vector2(0.5, 0.5)
	tex.fill_to = Vector2(1.0, 0.5)
	tex.width = 32
	tex.height = 32
	var soft := Gradient.new()
	soft.set_color(0, Color(1, 1, 1, 1))
	soft.set_color(1, Color(1, 1, 1, 0))
	tex.gradient = soft
	var mat := StandardMaterial3D.new()
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	mat.vertex_color_use_as_albedo = true
	mat.albedo_texture = tex
	mat.roughness = 1.0
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	var quad := QuadMesh.new()
	quad.size = Vector2(0.22, 0.22)
	quad.material = mat
	var ramp := Gradient.new()
	ramp.set_color(0, Color(0.93, 0.95, 0.98, 0.0))
	ramp.set_color(1, Color(0.93, 0.95, 0.98, 0.0))
	ramp.add_point(0.1, Color(0.93, 0.95, 0.98, 0.5))
	ramp.add_point(0.4, Color(0.93, 0.95, 0.98, 0.25))
	var ramp_tex := GradientTexture1D.new()
	ramp_tex.gradient = ramp
	var grow := Curve.new()
	grow.max_value = 2.0
	grow.add_point(Vector2(0.0, 0.3))
	grow.add_point(Vector2(1.0, 1.8))
	var grow_tex := CurveTexture.new()
	grow_tex.curve = grow
	var pm := ParticleProcessMaterial.new()
	pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	pm.emission_sphere_radius = 0.015
	pm.direction = Vector3(0.0, -0.2, 1.0)
	pm.spread = 14.0
	pm.initial_velocity_min = 0.4
	pm.initial_velocity_max = 0.6
	pm.gravity = Vector3(0.0, 0.05, 0.0)
	pm.damping_min = 0.5
	pm.damping_max = 0.8
	pm.scale_min = 0.8
	pm.scale_max = 1.2
	pm.scale_curve = grow_tex
	pm.color_ramp = ramp_tex
	_held.append_array([tex, soft, mat, quad, ramp, ramp_tex, grow, grow_tex, pm])
	var heads: Array = []
	for p in _people:
		heads.append([p.skel, p])
	if _player_skel:
		heads.append([_player_skel, null])
	for h in heads:
		var skel := h[0] as Skeleton3D
		if skel == null or skel.find_bone("Head") < 0:
			continue
		var at := BoneAttachment3D.new()
		at.name = "Breath"
		at.bone_name = "Head"
		skel.add_child(at)
		var e := GPUParticles3D.new()
		e.amount = 6
		e.lifetime = 2.2
		e.explosiveness = 0.75
		e.randomness = 0.3
		e.local_coords = false
		var pace := _rng.randf_range(0.6, 0.75) # (a breath every 3-3.7 s standing, each its own)
		e.speed_scale = pace
		e.preprocess = _rng.randf_range(0.0, 2.2)
		e.process_material = pm
		e.draw_pass_1 = quad
		e.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		e.visibility_aabb = AABB(Vector3(-1.5, -1.5, -1.5), Vector3(3.0, 3.0, 3.0))
		e.emitting = false
		e.position = Vector3(0.0, 0.05, 0.11)
		at.add_child(e)
		_breaths.append([e, h[1], pace])


## Milestone 222: a walker come back to its way walks on along it (from where it came out, its lane eased in).
func _walker_rejoin(p: Dictionary) -> void:
	_walker_detour(p)
	_act_done(p)


## Milestone 222: a walker off its way (going home, coming back, stopped by a bump) eases back into it from where it is.
func _walker_detour(p: Dictionary) -> void:
	var w: Dictionary = p.walker
	var node: Node3D = p.node
	var here := _at(w.pts, w.loop, w.length, w.s)
	var ahead := _at(w.pts, w.loop, w.length, (w.s as float) + 0.7)
	var dir := Vector3(ahead.x - here.x, 0.0, ahead.z - here.z).normalized()
	var side := Vector3(-dir.z, 0.0, dir.x)
	var off := node.position - (here + side * (w.lane as float))
	off.y = 0.0
	w.detour = off
	w.push = 0.0


func _is_keeper(p: Dictionary) -> bool:
	for k in keepers:
		if (p.home as Vector3).distance_to(k.origin) < 0.1:
			return true
	return false


func _leave(p: Dictionary) -> void:
	var from := (p.node as Node3D).position
	var best: Array = EXITS[0]
	for e in EXITS:
		if from.distance_to(e[0]) < from.distance_to(best[0]):
			best = e
	# (milestone 213: into the osteria, if a place is free there and its door nearer than the street)
	var shelter = null
	for sh in _shelter:
		if not sh.taken and from.distance_to(to_local(sh.out)) < from.distance_to(best[0]) + 4.0:
			if shelter == null or from.distance_to(to_local(sh.out)) < from.distance_to(to_local(shelter.out)):
				shelter = sh
	if shelter != null:
		shelter.taken = true
		p.shelter = shelter
		var spot: Transform3D = shelter.spot
		p.path = _around(from, to_local(shelter.out)) + [to_local(shelter.in), to_local(spot.origin)]
		p.act = "leave"
	else:
		p.path = _around(from, best[0]) + [best[1]]
		p.exit = best
		p.act = "leave"
	var ap: AnimationPlayer = p.ap
	ap.speed_scale = 1.35 / 1.6
	ap.play("walk", 0.35)
	if _rng.randf() < 0.4:
		var lines: Array = ["Andiamo, it's pouring!", "Quick, inside!", "Ugh, my good shoes..."]
		if _night and _rain <= RAIN_LEAVE:
			# (milestone 218: going home, or for a glass, at night)
			lines = ["Buonanotte!", "Time for a glass, then bed.", "A domani!"]
		_bubbles.call("say", p.node, lines[_rng.randi() % lines.size()], 2.2, _head_height(p))


func _come_back(p: Dictionary) -> void:
	var node: Node3D = p.node
	if p.get("shelter") != null:
		# (out of the osteria the way they came, back to their place)
		var sh: Dictionary = p.shelter
		sh.taken = false
		p.shelter = null
		p.path = [to_local(sh.in), to_local(sh.out)] + _around(to_local(sh.out), p.home)
		p.act = "back"
		var aw: AnimationPlayer = p.ap
		aw.speed_scale = 1.35 / 1.6
		aw.play("walk", 0.35)
		return
	var e: Array = p.exit
	node.position = Vector3(e[1].x, node.position.y, e[1].z)
	node.visible = true
	(p.shape as CollisionShape3D).disabled = false
	p.path = [e[0]] + _around(e[0], p.home)
	p.act = "back"
	var ap: AnimationPlayer = p.ap
	ap.speed_scale = 1.35 / 1.6
	ap.play("walk", 0.35)


## The way from p_a to p_b round what stands in the square (OBSTACLES): the points to walk to, p_b last.
func _around(p_a: Vector3, p_b: Vector3) -> Array:
	var out: Array = []
	var a := p_a
	for o in OBSTACLES:
		var c := Vector3(o.x, 0.0, o.y)
		var ab := Vector3(p_b.x - a.x, 0.0, p_b.z - a.z)
		var l2 := ab.length_squared()
		if l2 < 1e-4:
			continue
		var t := clampf(Vector3(c.x - a.x, 0.0, c.z - a.z).dot(ab) / l2, 0.0, 1.0)
		var near := Vector3(a.x, 0.0, a.z) + ab * t
		var off := near - c
		if off.length() < o.z + 0.35 and t > 0.0 and t < 1.0:
			var n := off.normalized() if off.length() > 1e-3 else Vector3(-ab.z, 0.0, ab.x).normalized()
			var w: Vector3 = c + n * (o.z + 0.6)
			out.append(Vector3(w.x, p_a.y, w.z))
			a = w
	out.append(p_b)
	return out


## (tools) the social layer's counts and the people's acts.
func get_social_stats() -> Dictionary:
	var acts := {}
	var looking := 0
	for p in _people:
		var a := String(p.act) if p.act != "" else "-"
		acts[a] = int(acts.get(a, 0)) + 1
		if float(p.get("look_w", 0.0)) > 0.5:
			looking += 1
	return {"talks": _talks, "points": _points, "looking": looking, "acts": acts, "rain": snappedf(_rain, 0.01), "cold": _cold,
			"talking": not _talk.is_empty(), "night": _night, "lanterns": _lanterns.size() if _night else 0}
