extends Node3D
## FellowEngine's image regression suite: FellowCloth3D (milestone 73). A flag on a pole in the wind (a FellowWind3D
## with gusts), a curtain hanging from its top edge, a sheet fallen onto a sphere and a box, each simulated from the
## scene's start at the capture's fixed frame rate: the same folds every run.


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.85
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	return m


func _static(shape: Shape3D, pos: Vector3, mesh: Mesh) -> void:
	var b := StaticBody3D.new()
	b.position = pos
	var cs := CollisionShape3D.new()
	cs.shape = shape
	b.add_child(cs)
	if mesh:
		var mi := MeshInstance3D.new()
		mi.mesh = mesh
		mi.material_override = _material(Color(0.55, 0.56, 0.6))
		b.add_child(mi)
	add_child(b)


func _ready() -> void:
	var wind := FellowWind3D.new()
	wind.speed = 7.0
	wind.gustiness = 0.5
	wind.rotation_degrees = Vector3(0, -90, 0)
	add_child(wind)
	_static(WorldBoundaryShape3D.new(), Vector3(), null)
	var ground := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(30, 30)
	ground.mesh = pm
	ground.material_override = _material(Color(0.38, 0.42, 0.36))
	add_child(ground)
	var pole := MeshInstance3D.new()
	var cyl := CylinderMesh.new()
	cyl.top_radius = 0.035
	cyl.bottom_radius = 0.035
	cyl.height = 4.0
	pole.mesh = cyl
	pole.position = Vector3(-3, 2, -1)
	add_child(pole)
	var flag := FellowCloth3D.new()
	flag.size = Vector2(1.8, 1.1)
	flag.pin_mode = FellowCloth3D.PIN_LEFT_EDGE
	flag.material = _material(Color(0.75, 0.12, 0.1))
	flag.position = Vector3(-2.96, 3.9, -1)
	add_child(flag)
	var curtain := FellowCloth3D.new()
	curtain.size = Vector2(1.6, 2.2)
	curtain.pin_mode = FellowCloth3D.PIN_TOP_EDGE
	curtain.wind_scale = 0.25
	curtain.material = _material(Color(0.2, 0.3, 0.7))
	curtain.position = Vector3(0.0, 2.6, -1.5)
	add_child(curtain)
	var sph := SphereShape3D.new()
	sph.radius = 0.4
	var sm := SphereMesh.new()
	sm.radius = 0.4
	sm.height = 0.8
	_static(sph, Vector3(3.0, 0.4, 0.5), sm)
	var bx := BoxShape3D.new()
	bx.size = Vector3(0.6, 0.6, 0.6)
	var bm := BoxMesh.new()
	bm.size = bx.size
	_static(bx, Vector3(4.1, 0.3, 0.5), bm)
	var sheet := FellowCloth3D.new()
	sheet.size = Vector2(2.4, 1.8)
	sheet.resolution = 24
	sheet.pin_mode = FellowCloth3D.PIN_NONE
	sheet.wind_scale = 0.0
	sheet.turbulence = 0.0
	sheet.material = _material(Color(0.85, 0.82, 0.7))
	sheet.rotation_degrees = Vector3(-90, 0, 0)
	sheet.position = Vector3(2.4, 1.0, -0.4)
	add_child(sheet)
