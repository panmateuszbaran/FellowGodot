extends SceneTree
## FellowEngine's buoyancy in pictures (milestone 175, decision 0317): crates, barrels, a raft, a post and a cube of
## half the water's density on a FellowWater3D sea (12 m/s wind), each a RigidBody3D with a FellowBuoyancy3D. Frames
## written as PNGs (--out=<dir>, every --every-th frame from --from to --to seconds) for a GIF.
##
##   VK_ICD_FILENAMES=.../lvp_icd.json xvfb-run -a godot --path benchmarks/tools/buoyancy -s demo.gd \
##       --rendering-driver vulkan --resolution 960x540 --fixed-fps 30 -- --out=<dir>

var _out := ""
var _from := 4.0
var _to := 8.0
var _every := 2
var _frame := 0
var _cam: Camera3D


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
	sun.name = "Sun"
	sun.rotation_degrees = Vector3(-32.0, -140.0, 0.0)
	sun.shadow_enabled = true
	sun.light_energy = 1.4
	root.add_child(sun)
	var water: Node3D = ClassDB.instantiate("FellowWater3D")
	water.set("wind_speed", 9.0)
	water.set("fetch", 20.0)
	water.set("wind_direction", 30.0)
	root.add_child(water)
	water.set("sun_path", water.get_path_to(sun))
	var wood := StandardMaterial3D.new()
	wood.albedo_color = Color(0.55, 0.38, 0.22)
	wood.roughness = 0.8
	var dark := StandardMaterial3D.new()
	dark.albedo_color = Color(0.32, 0.2, 0.12)
	dark.roughness = 0.7
	var paint := StandardMaterial3D.new()
	paint.albedo_color = Color(0.75, 0.22, 0.15)
	paint.roughness = 0.6
	var rng := RandomNumberGenerator.new()
	rng.seed = 3
	for i in 9:
		var p := Vector3(rng.randf_range(-7, 7), 0.6, rng.randf_range(-4, 6))
		_body(_box_shape(Vector3(0.7, 0.7, 0.7)), _box_mesh(Vector3(0.7, 0.7, 0.7)), wood, 380.0, Transform3D(Basis(Vector3.UP, rng.randf() * TAU), p))
	for i in 5:
		var p := Vector3(rng.randf_range(-7, 7), 0.6, rng.randf_range(-4, 6))
		var cy := CylinderShape3D.new()
		cy.radius = 0.3
		cy.height = 0.9
		var cm := CylinderMesh.new()
		cm.top_radius = 0.3
		cm.bottom_radius = 0.3
		cm.height = 0.9
		_body(cy, cm, dark, 350.0, Transform3D(Basis(Vector3.UP, rng.randf() * TAU) * Basis(Vector3.BACK, PI * 0.5), p))
	_body(_box_shape(Vector3(2.6, 0.25, 1.6)), _box_mesh(Vector3(2.6, 0.25, 1.6)), wood, 450.0, Transform3D(Basis(Vector3.UP, 0.4), Vector3(1.0, 0.5, 2.5)))
	_body(_box_shape(Vector3(0.35, 2.2, 0.35)), _box_mesh(Vector3(0.35, 2.2, 0.35)), dark, 500.0, Transform3D(Basis(Vector3.BACK, 0.05), Vector3(-3.0, 1.2, 1.0)))
	_body(_box_shape(Vector3(1.0, 1.0, 1.0)), _box_mesh(Vector3(1.0, 1.0, 1.0)), paint, 500.0, Transform3D(Basis(Vector3.BACK, 0.05), Vector3(4.0, 0.7, 0.0)))
	_cam = Camera3D.new()
	_cam.fov = 55.0
	_cam.far = 3000.0
	root.add_child(_cam)
	_cam.look_at_from_position(Vector3(0.0, 3.2, -11.0), Vector3(0.0, 0.0, 1.5))


func _box_shape(p_size: Vector3) -> Shape3D:
	var s := BoxShape3D.new()
	s.size = p_size
	return s


func _box_mesh(p_size: Vector3) -> Mesh:
	var m := BoxMesh.new()
	m.size = p_size
	return m


func _body(p_shape: Shape3D, p_mesh: Mesh, p_material: Material, p_density: float, p_xform: Transform3D) -> void:
	var body := RigidBody3D.new()
	var cs := CollisionShape3D.new()
	cs.shape = p_shape
	body.add_child(cs)
	var mi := MeshInstance3D.new()
	mi.mesh = p_mesh
	mi.material_override = p_material
	body.add_child(mi)
	body.add_child(ClassDB.instantiate("FellowBuoyancy3D"))
	var aabb := p_mesh.get_aabb()
	var vol := aabb.size.x * aabb.size.y * aabb.size.z
	if p_shape is CylinderShape3D:
		vol = PI * p_shape.radius * p_shape.radius * p_shape.height
	body.mass = p_density * vol
	root.add_child(body)
	body.global_transform = p_xform


func _process(_d: float) -> bool:
	_frame += 1
	var t := float(_frame) / float(Engine.get_frames_per_second() if Engine.get_frames_per_second() > 0 else 30)
	t = float(_frame) / 30.0
	if _out != "" and t >= _from and t <= _to and _frame % _every == 0:
		var img := root.get_texture().get_image()
		img.save_png(_out.path_join("frame_%04d.png" % _frame))
	return t > _to
