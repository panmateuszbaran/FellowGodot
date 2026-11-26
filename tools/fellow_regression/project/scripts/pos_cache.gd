extends Node3D
## FellowEngine's image regression suite: the positional shadow cache (milestone 231, decision 0373). An omni light with
## a cube shadow, one with dual paraboloids and a spot light over a yard of static casters (boxes, cylinders, a
## MultiMesh ring); a ball circles between them every frame, so their shadows are drawn every frame. With the cache, the
## static casters come from each face's copy and the ball is drawn over it. A pillar is hidden at frame 20 and a crate
## moved at frame 26: cached casters that change (the faces are made again without them, the crate then drawn over the
## copy until it is static again). The picture must be stock's (suite groups pos_cache_*).

var _frame := 0
var _ball: MeshInstance3D
var _pillar: MeshInstance3D
var _crate: MeshInstance3D


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.8
	return m


func _mesh(mesh: Mesh, pos: Vector3, c: Color, rot_y := 0.0) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = _material(c)
	mi.position = pos
	mi.rotation.y = rot_y
	add_child(mi)
	return mi


func _box(size: Vector3) -> BoxMesh:
	var b := BoxMesh.new()
	b.size = size
	return b


func _ready() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.04, 0.04, 0.06)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.1, 0.1, 0.12)
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)

	var ground := PlaneMesh.new()
	ground.size = Vector2(14, 14)
	_mesh(ground, Vector3.ZERO, Color(0.75, 0.72, 0.68))
	_mesh(_box(Vector3(0.8, 1.2, 0.8)), Vector3(-1.2, 0.6, -1.4), Color(0.8, 0.4, 0.3), 0.4)
	_mesh(_box(Vector3(2.4, 1.6, 0.25)), Vector3(0.6, 0.8, -2.6), Color(0.6, 0.6, 0.7))
	_mesh(_box(Vector3(0.5, 0.5, 0.5)), Vector3(2.0, 0.25, 1.6), Color(0.3, 0.6, 0.3), 0.8)
	var cyl := CylinderMesh.new()
	cyl.top_radius = 0.3
	cyl.bottom_radius = 0.35
	cyl.height = 1.4
	_mesh(cyl, Vector3(-2.6, 0.7, 1.2), Color(0.7, 0.7, 0.4))
	_mesh(cyl, Vector3(3.2, 0.7, -1.0), Color(0.5, 0.4, 0.7))
	var tall := CylinderMesh.new()
	tall.top_radius = 0.18
	tall.bottom_radius = 0.18
	tall.height = 2.4
	_pillar = _mesh(tall, Vector3(-0.4, 1.2, 0.9), Color(0.85, 0.85, 0.85))
	_crate = _mesh(_box(Vector3(0.7, 0.7, 0.7)), Vector3(1.3, 0.35, -0.6), Color(0.7, 0.5, 0.3), 0.2)

	# A ring of small boxes in one MultiMesh.
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.mesh = _box(Vector3(0.22, 0.45, 0.22))
	mm.instance_count = 24
	for i in 24:
		var a := TAU * float(i) / 24.0
		mm.set_instance_transform(i, Transform3D(Basis(Vector3.UP, a), Vector3(cos(a) * 4.6, 0.225, sin(a) * 4.6)))
	var mmi := MultiMeshInstance3D.new()
	mmi.multimesh = mm
	mmi.material_override = _material(Color(0.6, 0.5, 0.5))
	add_child(mmi)

	var sphere := SphereMesh.new()
	sphere.radius = 0.35
	sphere.height = 0.7
	_ball = _mesh(sphere, Vector3(1.6, 0.9, 0.0), Color(0.9, 0.9, 0.2))

	var cube := OmniLight3D.new()
	cube.position = Vector3(-2.2, 2.2, -0.4)
	cube.light_color = Color(1.0, 0.75, 0.5)
	cube.light_energy = 3.0
	cube.omni_range = 7.0
	cube.shadow_enabled = true
	cube.omni_shadow_mode = OmniLight3D.SHADOW_CUBE
	add_child(cube)

	var dp := OmniLight3D.new()
	dp.position = Vector3(2.4, 1.9, 0.6)
	dp.light_color = Color(0.5, 0.65, 1.0)
	dp.light_energy = 3.0
	dp.omni_range = 6.5
	dp.shadow_enabled = true
	dp.omni_shadow_mode = OmniLight3D.SHADOW_DUAL_PARABOLOID
	add_child(dp)

	var spot := SpotLight3D.new()
	spot.position = Vector3(0.3, 5.0, 2.8)
	spot.light_energy = 6.0
	spot.spot_range = 10.0
	spot.spot_angle = 38.0
	spot.shadow_enabled = true
	add_child(spot)
	spot.look_at(Vector3(0.0, 0.0, -0.4))


func _process(_delta: float) -> void:
	_frame += 1
	var t := float(_frame) * 0.06
	_ball.position = Vector3(cos(t) * 1.6, 0.9 + 0.3 * sin(t * 1.7), sin(t) * 1.6)
	if _frame == 20:
		_pillar.visible = false
	if _frame == 26:
		_crate.position += Vector3(-0.5, 0.0, 0.3)
