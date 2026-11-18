extends Node3D
## The rooftops' people: passers-by walking round the square, across it and in and out of its streets, groups standing
## talking, people sitting on the palaces' stone benches, the stall keepers. Each one the mannequin (rooftops/npc.tscn)
## dressed by colour — a tunic's colour on its shell, darker joints — some in a long robe, some in a beret or a hat; its
## clip played by its own AnimationPlayer (the walk's speed matched to its pace). The walkers step aside for the
## player. Everything is set from the data the builder (tools/build_rooftops.gd) gives it.

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

var _rng := RandomNumberGenerator.new()
var _walkers: Array = []
var _held: Array = [] # the clothes' meshes and materials, held until the people are gone (freed after them)
var _player: Node3D


func _ready() -> void:
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
			_walkers.append(w)
	for g in groups:
		var c: Vector3 = g[0]
		var n: int = g[1]
		for i in n:
			var a := TAU * i / n + _rng.randf_range(-0.3, 0.3)
			var p := c + Vector3(cos(a), 0.0, sin(a)) * _rng.randf_range(0.55, 0.7)
			var node := _person("talk" if _rng.randf() < 0.7 else "idle", 1.0)
			node.position = p
			_face(node, c - p)
	for s in seats:
		if _rng.randf() < 0.3:
			continue
		var node := _person("sit", 1.0)
		node.position = s.origin
		_face(node, s.basis.x)
	for k in keepers:
		var node := _person("idle" if _rng.randf() < 0.5 else "talk", 1.0)
		node.position = k.origin
		_face(node, k.basis.z)


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
			var robe := CylinderMesh.new()
			robe.top_radius = 0.17
			robe.bottom_radius = 0.3
			robe.height = 0.78
			robe.radial_segments = 14
			robe.rings = 1
			var robe_mat := StandardMaterial3D.new()
			robe_mat.albedo_color = PALETTE[_rng.randi() % PALETTE.size()].darkened(0.15)
			robe_mat.roughness = 0.9
			robe.material = robe_mat
			_attach(skel, "Hips", robe, Vector3(0.0, -0.33, 0.0))
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
	for w in _walkers:
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
		node.position = here + side * ((w.lane as float) + (w.push as float))
		var goal := atan2(dir.x, dir.z)
		node.rotation.y = lerp_angle(node.rotation.y, goal, 1.0 - exp(-6.0 * delta))
