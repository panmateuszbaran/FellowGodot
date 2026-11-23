extends SceneTree
## FellowEngine's contact sound in a yard (milestone 180, decision 0322): on a stone floor a wooden crate dropped, a
## steel pipe dropped on a wooden ramp and rolling off it, stones knocking each other, a glass bottle falling on a
## wooden table, the crate pushed and sliding, a steel bar dropped flat, a rubber ball bouncing, a wooden crate breaking
## when it lands (its FellowContactSound3D's impact signal breaks it) and its pieces clattering. Every sound synthesised from the bodies'
## contacts as the scene runs (FellowContactSound3D on each; the floor, the table and the ramp give their materials).
## Record it with Movie Maker (frames and sound):
##
##   VK_ICD_FILENAMES=.../lvp_icd.json xvfb-run -a godot --path benchmarks/tools/world_sound --rendering-driver vulkan \
##       --resolution 960x540 --fixed-fps 30 --write-movie <dir>/m.png -s impact_demo.gd [-- --seconds=17]
##
## Each body's sound statistics printed at the end and every impact as it happens ("IMPACT t body speed"); --timing:
## the viewport's GPU and CPU render times from 2 s ("TIMING ..."); --silent: every sound off.

enum { WOOD, METAL, STONE, GLASS, PLASTIC, RUBBER, CERAMIC }

var _seconds := 17.0
var _timing := false
var _silent := false
var _gpu_ms := []
var _cpu_ms := []
var _frame := 0
var _cam: Camera3D
var _bodies := {}
var _events := []
var _sounds := []
var _impacts := 0


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--seconds="):
			_seconds = float(a.get_slice("=", 1))
		elif a == "--timing":
			_timing = true
		elif a == "--silent":
			_silent = true
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	e.sky = sky
	e.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	e.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.environment = e
	root.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-55.0, 35.0, 0.0)
	sun.shadow_enabled = true
	root.add_child(sun)
	# The yard: a stone floor, a wooden table, a wooden ramp.
	_static(Vector3(20.0, 0.2, 20.0), Transform3D(Basis(), Vector3(0.0, -0.1, 0.0)), Color(0.55, 0.54, 0.52), STONE)
	_static(Vector3(1.2, 0.05, 0.8), Transform3D(Basis(), Vector3(-1.6, 0.8, -0.6)), Color(0.5, 0.36, 0.22), WOOD)
	for x in [-2.1, -1.1]:
		for z in [-0.9, -0.3]:
			_static(Vector3(0.06, 0.78, 0.06), Transform3D(Basis(), Vector3(x, 0.39, z)), Color(0.45, 0.32, 0.2), WOOD)
	_static(Vector3(2.4, 0.06, 0.8), Transform3D(Basis(Vector3.FORWARD, -0.3), Vector3(1.7, 0.4, -1.4)), Color(0.48, 0.35, 0.22), WOOD)
	# What falls, when, from where.
	_events = [
		[0.5, "crate", Callable(self, "_drop_crate")],
		[2.5, "pipe", Callable(self, "_drop_pipe")],
		[4.5, "stones", Callable(self, "_drop_stones")],
		[6.5, "bottle", Callable(self, "_drop_bottle")],
		[8.2, "push", Callable(self, "_push_crate")],
		[10.0, "bar", Callable(self, "_drop_bar")],
		[11.8, "ball", Callable(self, "_drop_ball")],
		[13.8, "breaking crate", Callable(self, "_drop_breakable")],
	]
	_cam = Camera3D.new()
	_cam.fov = 55.0
	root.add_child(_cam)
	_place_camera(0.0)


func _material(p_color: Color, p_metal: float = 0.0, p_rough: float = 0.8) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = p_color
	m.metallic = p_metal
	m.roughness = p_rough
	return m


func _sound(p_body: CollisionObject3D, p_material: int, p_wall: float = 0.0) -> Node:
	var s: Node = ClassDB.instantiate("FellowContactSound3D")
	s.set("material", p_material)
	s.set("wall_thickness", p_wall)
	s.set("sound_enabled", not _silent)
	p_body.add_child(s)
	_sounds.append(s)
	return s


func _static(p_size: Vector3, p_xform: Transform3D, p_color: Color, p_material: int) -> void:
	var b := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var bs := BoxShape3D.new()
	bs.size = p_size
	cs.shape = bs
	b.add_child(cs)
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = p_size
	mi.mesh = bm
	mi.material_override = _material(p_color)
	b.add_child(mi)
	b.transform = p_xform
	root.add_child(b)
	_sound(b, p_material)


func _rigid(p_name: String, p_shape: Shape3D, p_mesh: Mesh, p_mat: Material, p_mass: float, p_xform: Transform3D, p_material: int, p_wall: float) -> RigidBody3D:
	var b := RigidBody3D.new()
	b.name = p_name
	b.mass = p_mass
	var cs := CollisionShape3D.new()
	cs.shape = p_shape
	b.add_child(cs)
	var mi := MeshInstance3D.new()
	mi.mesh = p_mesh
	mi.material_override = p_mat
	b.add_child(mi)
	b.transform = p_xform
	root.add_child(b)
	var s := _sound(b, p_material, p_wall)
	s.connect("impact", func(_at: Vector3, p_speed: float, _other: Object) -> void:
		_impacts += 1
		print("IMPACT %.2f %s %.2f" % [float(_frame) / 30.0, p_name, p_speed]))
	_bodies[p_name] = b
	return b


func _box(p_size: Vector3) -> Array:
	var s := BoxShape3D.new()
	s.size = p_size
	var m := BoxMesh.new()
	m.size = p_size
	return [s, m]


func _cylinder(p_radius: float, p_height: float) -> Array:
	var s := CylinderShape3D.new()
	s.radius = p_radius
	s.height = p_height
	var m := CylinderMesh.new()
	m.top_radius = p_radius
	m.bottom_radius = p_radius
	m.height = p_height
	return [s, m]


func _drop_crate() -> void:
	var sm := _box(Vector3(0.6, 0.6, 0.6))
	_rigid("crate", sm[0], sm[1], _material(Color(0.62, 0.45, 0.27)), 20.0, Transform3D(Basis(Vector3(1, 0, 1).normalized(), 0.25), Vector3(0.0, 1.6, 0.0)), WOOD, 0.02)


func _drop_pipe() -> void:
	var sm := _cylinder(0.03, 1.0)
	# Across the ramp near its top: it lands, rolls down and off onto the floor.
	_rigid("pipe", sm[0], sm[1], _material(Color(0.6, 0.6, 0.62), 0.9, 0.35), 4.2, Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3(2.5, 1.2, -1.4)), METAL, 0.003)


func _drop_stones() -> void:
	var mat := _material(Color(0.42, 0.41, 0.4))
	var spots := [Vector3(0.9, 1.2, 0.9), Vector3(1.0, 1.55, 0.95), Vector3(0.85, 1.9, 0.85)]
	for i in 3:
		var sm := _box(Vector3(0.12, 0.1, 0.08))
		_rigid("stone %d" % i, sm[0], sm[1], mat, 2.5, Transform3D(Basis(Vector3(1, 1, 0).normalized(), 0.6 * i), spots[i]), STONE, 0.0)


func _drop_bottle() -> void:
	var sm := _cylinder(0.04, 0.3)
	_rigid("bottle", sm[0], sm[1], _material(Color(0.25, 0.5, 0.3), 0.0, 0.1), 0.5, Transform3D(Basis(Vector3.RIGHT, 1.2), Vector3(-1.5, 1.4, -0.55)), GLASS, 0.003)


func _push_crate() -> void:
	var b: RigidBody3D = _bodies.get("crate")
	if b:
		b.apply_central_impulse(Vector3(-1.0, 0.0, 0.6).normalized() * 20.0 * 2.4)


func _drop_bar() -> void:
	var sm := _box(Vector3(1.0, 0.03, 0.03))
	_rigid("bar", sm[0], sm[1], _material(Color(0.5, 0.5, 0.55), 0.9, 0.4), 7.1, Transform3D(Basis(Vector3.UP, 0.7), Vector3(0.3, 1.0, 1.3)), METAL, 0.0)


func _drop_ball() -> void:
	var s := SphereShape3D.new()
	s.radius = 0.1
	var m := SphereMesh.new()
	m.radius = 0.1
	m.height = 0.2
	var b := _rigid("ball", s, m, _material(Color(0.8, 0.2, 0.15), 0.0, 0.6), 0.6, Transform3D(Basis(), Vector3(-0.6, 2.0, 1.0)), RUBBER, 0.0)
	var pm := PhysicsMaterial.new()
	pm.bounce = 0.75
	b.physics_material_override = pm


func _drop_breakable() -> void:
	var sm := _box(Vector3(0.5, 0.5, 0.5))
	var b := _rigid("breaking crate", sm[0], sm[1], _material(Color(0.66, 0.5, 0.3)), 15.0, Transform3D(Basis(Vector3.UP, 0.4), Vector3(-0.5, 3.0, -0.3)), WOOD, 0.0)
	var inside := _material(Color(0.8, 0.66, 0.45))
	var d: Node3D = ClassDB.instantiate("FellowDestructible3D")
	d.name = "Destructible"
	d.set("density", 600.0)
	d.set("piece_count", 14)
	d.set("debris_lifetime", 0.0)
	d.set("break_on_impact", false)
	d.set("interior_material", inside)
	d.set("prefracture", true)
	b.add_child(d)
	var snd: Node = null
	for c in b.get_children():
		if c.get_class() == "FellowContactSound3D":
			snd = c
	# Its impact breaks it (the contact sound's signal: what a game uses to break, to wake guards, to dust).
	snd.connect("impact", func(p_at: Vector3, p_speed: float, _o: Object) -> void:
		if p_speed > 4.0 and is_instance_valid(d) and not d.get_meta("broken", false):
			d.set_meta("broken", true)
			d.call_deferred("break_apart", p_at, Vector3(0.0, 8.0, 0.0)))


func _place_camera(p_t: float) -> void:
	var a := 0.45 + 0.02 * p_t
	var at := Vector3(sin(a) * 3.6, 1.5, cos(a) * 3.6)
	_cam.look_at_from_position(at, Vector3(0.0, 0.5, -0.3))


func _process(_d: float) -> bool:
	var t := float(_frame) / 30.0
	_place_camera(t)
	while not _events.is_empty() and t >= _events[0][0]:
		var ev: Array = _events.pop_front()
		(ev[2] as Callable).call()
	_frame += 1
	if _timing:
		var vp := root.get_viewport_rid()
		if _frame == 1:
			RenderingServer.viewport_set_measure_render_time(vp, true)
		elif t >= 2.0:
			_gpu_ms.append(RenderingServer.viewport_get_measured_render_time_gpu(vp))
			_cpu_ms.append(RenderingServer.viewport_get_measured_render_time_cpu(vp))
	if t >= _seconds:
		if _timing:
			var g := 0.0
			var c := 0.0
			for v in _gpu_ms:
				g += v
			for v in _cpu_ms:
				c += v
			print("TIMING frames %d gpu_ms_mean %.3f cpu_ms_mean %.3f %s" % [_gpu_ms.size(), g / maxf(1.0, _gpu_ms.size()), c / maxf(1.0, _cpu_ms.size()), "silent" if _silent else "sound"])
		var pieces := 0
		var piece_impacts := 0
		for n in root.get_children():
			if n is RigidBody3D and not _bodies.values().has(n):
				for c in n.get_children():
					if c.get_class() == "FellowContactSound3D":
						pieces += 1
						piece_impacts += int(c.call("get_sound_stats").impacts)
		for k in _bodies:
			if is_instance_valid(_bodies[k]):
				for c in _bodies[k].get_children():
					if c.get_class() == "FellowContactSound3D":
						print(k, ": ", c.call("get_sound_stats"))
		print("pieces with sound %d, their impacts %d; impacts signalled %d" % [pieces, piece_impacts, _impacts])
		return true
	return false
