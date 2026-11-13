extends Node3D
## FellowEngine's image regression suite: FellowVehicle3D and FellowWheel3D (milestone 77). A car already at 43 km/h
## turning hard under throttle (its body rolling, its front wheels steered, the wheels' spokes turning with their
## spin) and a car parked across a slope with one wheel on a block (its suspension compressed unevenly), under the
## physics at the capture's fixed frame rate.


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.6
	return m


func _box(pos: Vector3, size: Vector3, color: Color, rot := Vector3()) -> void:
	var b := StaticBody3D.new()
	b.position = pos
	b.rotation_degrees = rot
	var cs := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = size
	cs.shape = shape
	b.add_child(cs)
	var mi := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	mi.mesh = mesh
	mi.material_override = _material(color)
	b.add_child(mi)
	add_child(b)


func _car(xf: Transform3D, color: Color) -> FellowVehicle3D:
	var car := FellowVehicle3D.new()
	car.center_of_mass_mode = RigidBody3D.CENTER_OF_MASS_MODE_CUSTOM
	car.center_of_mass = Vector3(0, 0.45, 0.1)
	car.transform = xf
	var cs := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = Vector3(1.8, 0.55, 4.2)
	cs.shape = shape
	cs.position = Vector3(0, 0.62, 0)
	car.add_child(cs)
	var body := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = shape.size
	body.mesh = bm
	body.material_override = _material(color)
	body.position = cs.position
	car.add_child(body)
	var cabin := MeshInstance3D.new()
	var cm := BoxMesh.new()
	cm.size = Vector3(1.5, 0.45, 2.0)
	cabin.mesh = cm
	cabin.material_override = _material(color.darkened(0.45))
	cabin.position = Vector3(0, 1.12, 0.2)
	car.add_child(cabin)
	var tyre := _material(Color(0.08, 0.08, 0.08))
	var rim := _material(Color(0.8, 0.8, 0.82))
	for x in [-0.82, 0.82]:
		for z in [-1.35, 1.35]:
			var w := FellowWheel3D.new()
			w.position = Vector3(x, 0.34, z)
			w.steering = z < 0.0
			w.driven = z > 0.0
			w.handbrake = z > 0.0
			var wm := MeshInstance3D.new()
			var cyl := CylinderMesh.new()
			cyl.top_radius = 0.34
			cyl.bottom_radius = 0.34
			cyl.height = 0.24
			cyl.radial_segments = 20
			wm.mesh = cyl
			wm.material_override = tyre
			wm.rotation_degrees = Vector3(0, 0, 90)
			w.add_child(wm)
			var spoke := MeshInstance3D.new()
			var sm := BoxMesh.new()
			sm.size = Vector3(0.26, 0.5, 0.07)
			spoke.mesh = sm
			spoke.material_override = rim
			w.add_child(spoke)
			car.add_child(w)
	add_child(car)
	return car


func _ready() -> void:
	var ground := StaticBody3D.new()
	var gs := CollisionShape3D.new()
	gs.shape = WorldBoundaryShape3D.new()
	ground.add_child(gs)
	add_child(ground)
	var plane := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(60, 60)
	plane.mesh = pm
	plane.material_override = _material(Color(0.36, 0.38, 0.36))
	add_child(plane)
	for i in 8:
		_box(Vector3(-6.0 + i * 3.0, 0.005, -2.0), Vector3(1.5, 0.01, 0.15), Color(0.85, 0.85, 0.8))
	var a := _car(Transform3D(Basis.from_euler(Vector3(0, deg_to_rad(-90.0), 0)), Vector3(-5.0, 0.0, 0.5)), Color(0.8, 0.2, 0.15))
	a.linear_velocity = Vector3(12.0, 0, 0)
	a.throttle = 0.6
	a.steering = 1.0
	# A ramp across the view and a block under one wheel.
	_box(Vector3(5.0, 0.35, -5.5), Vector3(6, 0.2, 6), Color(0.5, 0.47, 0.42), Vector3(0, 0, 8))
	_box(Vector3(4.2, 0.45, -4.15), Vector3(0.6, 0.2, 0.6), Color(0.6, 0.55, 0.3), Vector3(0, 0, 8))
	var b := _car(Transform3D(Basis.from_euler(Vector3(0, 0, deg_to_rad(8.0))), Vector3(5.0, 0.6, -5.5)), Color(0.2, 0.4, 0.75))
	b.handbrake = true
	b.brake = 1.0
	($Camera as Camera3D).look_at_from_position(to_global(Vector3(-0.5, 3.0, 4.0)), to_global(Vector3(3.6, 0.6, -3.2)))
