extends SceneTree
## FellowEngine's water interaction in pictures (milestone 176, decision 0318): wooden crates (FellowDestructible3D)
## dropped and broken in the air over a FellowWater3D sea, their pieces splashing in and floating; a barrel dropped in; a raft
## pushed through the water, its wake's foam. Frames written as PNGs (--out=<dir>, every --every-th frame from --from to
## --to seconds); --before: as milestone 175 (debris_floats off, no foam made by what floats).
##
##   VK_ICD_FILENAMES=.../lvp_icd.json xvfb-run -a godot --path benchmarks/tools/buoyancy -s interaction_demo.gd \
##       --rendering-driver vulkan --resolution 960x540 --fixed-fps 30 -- --out=<dir> [--before]

var _out := ""
var _from := 0.0
var _to := 9.0
var _every := 1
var _before := false
var _frame := 0
var _cam: Camera3D
var _raft: RigidBody3D
var _crates := []
var _crates_made := 0
var _barrel: RigidBody3D
var _wood: StandardMaterial3D
var _inside: StandardMaterial3D


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		var v := a.get_slice("=", 1)
		if a.begins_with("--out="):
			_out = v
		elif a.begins_with("--from="):
			_from = float(v)
		elif a.begins_with("--to="):
			_to = float(v)
		elif a.begins_with("--every="):
			_every = int(v)
		elif a == "--before":
			_before = true
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var sm := ProceduralSkyMaterial.new()
	sm.sky_top_color = Color(0.25, 0.42, 0.7)
	sm.sky_horizon_color = Color(0.68, 0.76, 0.86)
	sky.sky_material = sm
	e.sky = sky
	e.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	e.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.environment = e
	root.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-35.0, -150.0, 0.0)
	sun.shadow_enabled = true
	sun.light_energy = 1.4
	root.add_child(sun)
	var water: Node3D = ClassDB.instantiate("FellowWater3D")
	water.set("wind_speed", 6.0)
	water.set("fetch", 10.0)
	water.set("wind_direction", 30.0)
	if _before:
		water.set("body_foam_size", 0.0)
	root.add_child(water)
	water.set("sun_path", water.get_path_to(sun))
	_wood = StandardMaterial3D.new()
	_wood.albedo_color = Color(0.58, 0.4, 0.23)
	_wood.roughness = 0.8
	_inside = StandardMaterial3D.new()
	_inside.albedo_color = Color(0.82, 0.66, 0.45)
	_inside.roughness = 0.9
	var paint := StandardMaterial3D.new()
	paint.albedo_color = Color(0.75, 0.22, 0.15)
	paint.roughness = 0.6
	# Crates dropped (where, when), each broken in the air 0.45 s later (1 m down, 5-6 m over the water).
	_crates.append({"at": Vector3(-2.5, 6.5, 3.0), "t": 0.1})
	_crates.append({"at": Vector3(1.5, 7.0, 5.0), "t": 1.0})
	_crates.append({"at": Vector3(4.0, 6.0, 1.0), "t": 1.9})
	# A barrel dropped at 3 s.
	var cy := CylinderShape3D.new()
	cy.radius = 0.35
	cy.height = 1.0
	var cm := CylinderMesh.new()
	cm.top_radius = 0.35
	cm.bottom_radius = 0.35
	cm.height = 1.0
	_barrel = _body(cy, cm, paint, 400.0, Transform3D(Basis(Vector3.RIGHT, 0.3), Vector3(-0.5, 4.5, 0.0)))
	_barrel.freeze = true
	# A raft pushed across.
	var rs := Vector3(2.4, 0.3, 1.2)
	var rbm := BoxMesh.new()
	rbm.size = rs
	var rsh := BoxShape3D.new()
	rsh.size = rs
	_raft = _body(rsh, rbm, _wood, 450.0, Transform3D(Basis(), Vector3(-14.0, 0.1, 7.5)))
	_cam = Camera3D.new()
	_cam.fov = 55.0
	_cam.far = 3000.0
	root.add_child(_cam)
	_cam.look_at_from_position(Vector3(0.0, 4.5, -10.0), Vector3(0.0, 0.0, 3.0))


func _body(p_shape: Shape3D, p_mesh: Mesh, p_material: Material, p_density: float, p_xform: Transform3D) -> RigidBody3D:
	var body := RigidBody3D.new()
	var cs := CollisionShape3D.new()
	cs.shape = p_shape
	body.add_child(cs)
	var mi := MeshInstance3D.new()
	mi.mesh = p_mesh
	mi.material_override = p_material
	body.add_child(mi)
	var buoy: Node3D = ClassDB.instantiate("FellowBuoyancy3D")
	if _before:
		buoy.set("foam", 0.0)
	body.add_child(buoy)
	var aabb := p_mesh.get_aabb()
	var vol := aabb.size.x * aabb.size.y * aabb.size.z
	if p_shape is CylinderShape3D:
		vol = PI * p_shape.radius * p_shape.radius * p_shape.height
	body.mass = p_density * vol
	root.add_child(body)
	body.global_transform = p_xform
	return body


func _crate(p_at: Vector3) -> Node3D:
	var body := RigidBody3D.new()
	var mi := MeshInstance3D.new()
	mi.name = "Mesh"
	mi.mesh = BoxMesh.new()
	mi.set_surface_override_material(0, _wood)
	body.add_child(mi)
	var cs := CollisionShape3D.new()
	cs.shape = BoxShape3D.new()
	body.add_child(cs)
	body.mass = 550.0
	var d: Node3D = ClassDB.instantiate("FellowDestructible3D")
	d.name = "Destructible"
	d.set("density", 550.0)
	d.set("piece_count", 20)
	d.set("debris_lifetime", 0.0)
	d.set("break_on_impact", false)
	d.set("interior_material", _inside)
	d.set("debris_floats", not _before)
	d.set("prefracture", true) # broken the frame it is told
	d.set("seed", _crates_made)
	_crates_made += 1
	body.add_child(d)
	var buoy: Node3D = ClassDB.instantiate("FellowBuoyancy3D") # the crate whole floats too
	if _before:
		buoy.set("foam", 0.0)
	body.add_child(buoy)
	body.position = p_at
	body.angular_velocity = Vector3(0.7, 0.3, -0.5)
	root.add_child(body)
	return d


func _physics_process(_d: float) -> bool:
	var t := float(_frame) / 30.0
	for c in _crates:
		if not c.has("d") and t >= c.t:
			c.d = _crate(c.at)
		elif c.has("d") and not c.has("broken") and t >= c.t + 0.45:
			c.broken = true
			var body: Node3D = c.d.get_parent()
			c.d.call("break_apart", body.global_position + Vector3(0.2, 0.3, -0.1))
	if _barrel.freeze and t >= 3.0:
		_barrel.freeze = false
	if t < 8.5:
		var lv := _raft.linear_velocity
		_raft.linear_velocity = Vector3(3.5, lv.y, 0.0)
	return false


func _process(_d: float) -> bool:
	_frame += 1
	var t := float(_frame) / 30.0
	if _out != "" and t >= _from and t <= _to and _frame % _every == 0:
		var img := root.get_texture().get_image()
		img.save_png(_out.path_join("frame_%04d.png" % _frame))
	return t > _to
