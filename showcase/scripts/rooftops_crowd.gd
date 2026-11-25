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
					"speed": _rng.randf_range(1.05, 1.55), "lane": _rng.randf_range(-0.7, 0.7), "push": 0.0}
			w["node"] = _person("walk", (w["speed"] as float) / 1.6)
			w["detour"] = Vector3()
			w["person"] = _register(w["node"], "walk", "walk", (w["speed"] as float) / 1.6)
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
		if person.react != "" and person.react != "walk_on":
			continue # (its way waits: the reaction moves it)
		w.s += (w.speed as float) * delta
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
			"react": "", "t": 0.0, "shove": Vector3(), "nudged": -1.0, "turn_to": 0.0, "pa": null, "wait": 0, "knock_v": Vector3(), "down_t": 0.0}
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
	if p.react != "" and p.react != "walk_on" and not (p.react == "stagger" and speed >= KNOCK_MIN):
		return REACT_NONE
	_bumps += 1
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
		return REACT_STAGGER
	_knock(p, push * clampf(speed * 0.7, 2.5, 4.5))
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
