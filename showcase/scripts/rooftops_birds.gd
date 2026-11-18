extends Node3D
## The rooftops' birds: pigeons on the square that peck and shuffle about until someone runs at them, then burst up
## with their wings beating, wheel over the square and settle again further off; and swifts circling high over the
## roofs. Each bird a body and two wings (made here), the wings beating about their shoulders.

@export var pigeons := 14
@export var swifts := 9
@export var center := Vector3(1.0, 0.0, 3.5) ## where the pigeons gather
@export var spread := 3.5
@export var flee_distance := 4.0
@export var player_path := NodePath("../Player")

var _birds: Array = []
var _player: Node3D
var _rng := RandomNumberGenerator.new()
var _time := 0.0


static func _bird_meshes(p_color: Color, p_scale: float) -> Array:
	var mat := StandardMaterial3D.new()
	mat.albedo_color = p_color
	mat.roughness = 0.85
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	# The body: a spindle along -Z (its head forward), wider at the breast; the head a smaller spindle above its front.
	var ring := 6
	var prof := [[0.0, 0.17, 0.0], [0.03, 0.12, 0.05], [0.05, 0.03, 0.06], [0.045, -0.06, 0.04], [0.025, -0.13, 0.015], [0.0, -0.2, 0.0]]
	for k in prof.size() - 1:
		for i in ring:
			var a0 := TAU * i / ring
			var a1 := TAU * (i + 1) / ring
			var r0: float = prof[k][0]
			var r1: float = prof[k + 1][0]
			var z0: float = -prof[k][1]
			var z1: float = -prof[k + 1][1]
			var p := [Vector3(cos(a0) * r0, sin(a0) * r0 * 0.9, z0), Vector3(cos(a1) * r0, sin(a1) * r0 * 0.9, z0),
					Vector3(cos(a1) * r1, sin(a1) * r1 * 0.9, z1), Vector3(cos(a0) * r1, sin(a0) * r1 * 0.9, z1)]
			for v in [p[0], p[1], p[2], p[0], p[2], p[3]]:
				st.add_vertex(v * p_scale)
	# The tail: a fan behind.
	for v in [Vector3(0, 0.0, 0.17), Vector3(-0.05, -0.01, 0.27), Vector3(0.05, -0.01, 0.27)]:
		st.add_vertex(v * p_scale)
	st.generate_normals()
	var body := st.commit()
	body.surface_set_material(0, mat)
	var st2 := SurfaceTool.new()
	st2.begin(Mesh.PRIMITIVE_TRIANGLES)
	# A wing from its shoulder out along +X, swept back: a quad and its tip.
	var w := [Vector3(0, 0, -0.04), Vector3(0.14, 0.0, -0.05), Vector3(0.3, 0.0, 0.04), Vector3(0.14, 0.0, 0.08), Vector3(0, 0, 0.07)]
	for v in [w[0], w[1], w[3], w[0], w[3], w[4], w[1], w[2], w[3]]:
		st2.add_vertex(v * p_scale)
	st2.generate_normals()
	var wing := st2.commit()
	wing.surface_set_material(0, mat)
	return [body, wing]


func _ready() -> void:
	_rng.seed = 4711
	_player = get_node_or_null(player_path)
	var pm := _bird_meshes(Color(0.46, 0.48, 0.53), 1.0)
	var sm := _bird_meshes(Color(0.08, 0.08, 0.09), 0.7)
	for i in pigeons:
		var b := _make(pm, false)
		var a := _rng.randf() * TAU
		var r := sqrt(_rng.randf()) * spread
		b.node.position = center + Vector3(cos(a) * r, 0.0, sin(a) * r)
		b.node.rotation.y = _rng.randf() * TAU
		_birds.append(b)
	for i in swifts:
		var b := _make(sm, true)
		b.radius = _rng.randf_range(14.0, 30.0)
		b.height = _rng.randf_range(30.0, 46.0)
		b.speed = _rng.randf_range(0.35, 0.6) * (1.0 if _rng.randf() < 0.6 else -1.0)
		b.phase = _rng.randf() * TAU
		_birds.append(b)


func _make(p_meshes: Array, p_swift: bool) -> Dictionary:
	var node := Node3D.new()
	add_child(node)
	var body := MeshInstance3D.new()
	body.mesh = p_meshes[0]
	body.position = Vector3(0, 0.1, 0)
	node.add_child(body)
	var wings := []
	for side in [1.0, -1.0]:
		var pivot := Node3D.new()
		pivot.position = Vector3(0.04 * side, 0.13, -0.03)
		pivot.scale = Vector3(side, 1, 1)
		node.add_child(pivot)
		var wm := MeshInstance3D.new()
		wm.mesh = p_meshes[1]
		pivot.add_child(wm)
		wings.append(pivot)
	return {"node": node, "body": body, "wings": wings, "swift": p_swift, "state": "ground", "vel": Vector3(),
			"t": _rng.randf() * 10.0, "peck": _rng.randf() * 3.0, "walk": Vector3(), "flap": _rng.randf() * TAU,
			"radius": 0.0, "height": 0.0, "speed": 0.0, "phase": 0.0, "land": Vector3(), "air_t": 0.0}


func _process(delta: float) -> void:
	_time += delta
	var ppos := _player.global_position if _player else Vector3(1e6, 0, 1e6)
	var pspeed := 0.0
	if _player is CharacterBody3D:
		pspeed = (_player as CharacterBody3D).velocity.length()
	for b in _birds:
		var n: Node3D = b.node
		if b.swift:
			var a: float = b.phase + _time * b.speed
			var pos := Vector3(cos(a) * b.radius, b.height + sin(_time * 0.7 + b.phase) * 2.0, sin(a) * b.radius)
			var tangent := Vector3(-sin(a), 0.0, cos(a)) * signf(b.speed)
			n.position = pos
			n.look_at(n.global_position + tangent, Vector3.UP)
			n.rotate_object_local(Vector3.FORWARD, -0.5 * signf(b.speed))
			_flap(b, delta, 14.0, 0.9 if fmod(_time + b.phase, 1.6) < 0.7 else 0.0)
			continue
		b.t += delta
		match b.state:
			"ground":
				# Pecking: the body tipped forward now and then; shuffling a few steps.
				b.peck -= delta
				var tip := 0.0
				if b.peck < 0.0:
					tip = sin(clampf(-b.peck / 0.35, 0.0, 1.0) * PI) * 0.6
					if b.peck < -0.35:
						b.peck = _rng.randf_range(0.6, 2.5)
						if _rng.randf() < 0.4:
							b.walk = Vector3(_rng.randf_range(-1, 1), 0, _rng.randf_range(-1, 1)).normalized() * 0.25
				(b.body as Node3D).rotation.x = -tip
				if (b.walk as Vector3).length() > 0.02:
					n.position += (b.walk as Vector3) * delta
					n.look_at(n.global_position + b.walk, Vector3.UP)
					b.walk = (b.walk as Vector3).move_toward(Vector3(), delta * 0.2)
				_flap(b, delta, 0.0, 0.0)
				var d := Vector2(n.position.x - ppos.x, n.position.z - ppos.z).length()
				if d < flee_distance + pspeed * 0.25 and absf(ppos.y - n.position.y) < 3.0:
					b.state = "fly"
					b.air_t = 0.0
					var away := Vector3(n.position.x - ppos.x, 0.0, n.position.z - ppos.z).normalized()
					away = away.rotated(Vector3.UP, _rng.randf_range(-0.7, 0.7))
					b.vel = away * _rng.randf_range(2.0, 3.5) + Vector3.UP * _rng.randf_range(2.5, 4.0)
					var la := _rng.randf() * TAU
					b.land = center + Vector3(cos(la), 0, sin(la)) * _rng.randf_range(6.0, 12.0)
			"fly":
				# Up and away with the wings beating, round over the square, back down to land.
				b.air_t += delta
				var to_land: Vector3 = b.land - n.position
				var climb := 1.0 - smoothstep(4.0, 9.0, b.air_t)
				var want := Vector3(to_land.x, 0.0, to_land.z).normalized() * 4.5 + Vector3.UP * (3.0 * climb - 1.2 * (1.0 - climb))
				if b.air_t < 1.2:
					want = b.vel + Vector3.UP * 1.5
				b.vel = (b.vel as Vector3).lerp(want, 1.0 - exp(-1.5 * delta))
				n.position += (b.vel as Vector3) * delta
				var hv := Vector3(b.vel.x, 0.0, b.vel.z)
				if hv.length() > 0.1:
					n.look_at(n.global_position + b.vel, Vector3.UP)
				_flap(b, delta, 11.0 if b.air_t < 2.0 or b.vel.y > 0.0 else 6.0, 1.0)
				if b.air_t > 5.0 and n.position.y < 0.15 and to_land.length() < 3.0:
					n.position.y = 0.0
					n.rotation = Vector3(0.0, n.rotation.y, 0.0)
					b.state = "ground"
					b.peck = 1.0
				elif n.position.y < 0.0 and b.air_t > 2.0:
					n.position.y = 0.0


func _flap(b: Dictionary, delta: float, rate: float, amount: float) -> void:
	b.flap += delta * rate * TAU
	var open := amount > 0.0
	for i in 2:
		var w: Node3D = b.wings[i]
		if open:
			w.rotation = Vector3(0.0, 0.25, sin(b.flap) * 0.9 * amount + 0.15)
		else:
			# Folded along the body.
			w.rotation = Vector3(0.0, 1.35, -0.1)
