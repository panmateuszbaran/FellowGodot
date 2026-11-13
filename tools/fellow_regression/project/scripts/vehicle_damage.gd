extends Node3D
## FellowEngine's image regression suite: FellowVehicleDamage3D (milestones 105-109). A parked car hit four times as
## the scene starts (apply_impact(): the same hits every run): its front dented, its front-left wheel's toe bent in
## (wheel_bend), its bumper hanging from its left end (hang_depth), its right mirror off and falling; the meshes'
## copies, the wheel's turn, the bumper's pin and the mirror's body under the physics at the capture's fixed frame
## rate.


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


func _part(car: FellowVehicle3D, name: String, size: Vector3, pos: Vector3, color: Color) -> void:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = size
	mi.mesh = bm
	mi.name = name
	mi.material_override = _material(color)
	mi.position = pos
	car.add_child(mi)


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
	var car := _car(Transform3D(Basis(), Vector3(0, 0.0, 0)), Color(0.85, 0.55, 0.15))
	car.handbrake = true
	car.brake = 1.0
	# The body subdivided (its vertices to dent); a bumper and a mirror that can come off.
	for c in car.get_children():
		if c is MeshInstance3D and (c as MeshInstance3D).mesh is BoxMesh and ((c as MeshInstance3D).mesh as BoxMesh).size.z > 4.0:
			var bm: BoxMesh = ((c as MeshInstance3D).mesh as BoxMesh).duplicate()
			bm.subdivide_width = 8
			bm.subdivide_height = 4
			bm.subdivide_depth = 20
			(c as MeshInstance3D).mesh = bm
	_part(car, "Bumper", Vector3(1.86, 0.22, 0.14), Vector3(0, 0.48, -2.05), Color(0.12, 0.12, 0.14))
	_part(car, "Mirror", Vector3(0.22, 0.12, 0.08), Vector3(0.86, 1.05, -0.75), Color(0.12, 0.12, 0.14))
	var dmg := FellowVehicleDamage3D.new()
	dmg.name = "Damage"
	dmg.wheel_bend = 6.0
	dmg.detachable_paths = [NodePath("../Bumper"), NodePath("../Mirror")]
	dmg.hang_depth = 0.05
	dmg.detach_depth = 0.6
	car.add_child(dmg)
	var full := dmg.impulse_threshold + 2.0 * dmg.max_depth / dmg.softness
	# The front's middle, the left wing ahead of the front-left wheel, the bumper's right end, the right mirror twice.
	dmg.apply_impact(car.to_global(Vector3(0.2, 0.7, -2.1)), car.global_basis * Vector3(0, 0, 1), dmg.impulse_threshold + 0.15 / dmg.softness)
	dmg.apply_impact(car.to_global(Vector3(-0.9, 0.55, -1.75)), car.global_basis * Vector3(1, 0, 0), full)
	dmg.apply_impact(car.to_global(Vector3(0.85, 0.48, -2.12)), car.global_basis * Vector3(0, 0, 1), dmg.impulse_threshold + 0.06 / dmg.softness)
	dmg.apply_impact(car.to_global(Vector3(0.95, 1.05, -0.75)), car.global_basis * Vector3(-1, 0, 0), full)
	dmg.apply_impact(car.to_global(Vector3(0.95, 1.05, -0.75)), car.global_basis * Vector3(-1, 0, 0), full)
	($Camera as Camera3D).look_at_from_position(to_global(Vector3(-1.9, 1.5, -5.0)), to_global(Vector3(0.25, 0.5, -1.6)))
