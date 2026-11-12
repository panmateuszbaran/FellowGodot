extends Node3D
## FellowEngine's image regression suite: FellowDestructible3D's structural mode (milestone 80). A wall prefractured in
## 40 pieces, struck twice as the scene starts (two holes, the pieces blown out), and a pillar broken at its foot (its
## upper part falling as one body), under the physics at the capture's fixed frame rate.


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.85
	return m


func _structure(size: Vector3, pos: Vector3, outer: Material, inner: Material, count: int) -> FellowDestructible3D:
	var body := StaticBody3D.new()
	body.position = pos
	var mi := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	mi.mesh = mesh
	mi.set_surface_override_material(0, outer)
	body.add_child(mi)
	var cs := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = size
	cs.shape = shape
	body.add_child(cs)
	var d := FellowDestructible3D.new()
	d.piece_count = count
	d.seed = 11
	d.prefracture = true
	d.structural = true
	d.break_on_impact = false
	d.density = 2200.0
	d.interior_material = inner
	body.add_child(d)
	add_child(body)
	return d


func _ready() -> void:
	var ground := StaticBody3D.new()
	var gs := CollisionShape3D.new()
	gs.shape = WorldBoundaryShape3D.new()
	ground.add_child(gs)
	add_child(ground)
	var plane := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(30, 30)
	plane.mesh = pm
	plane.material_override = _material(Color(0.38, 0.42, 0.36))
	add_child(plane)
	var wall := _structure(Vector3(4, 3, 0.25), Vector3(-1.2, 1.5, -1.0), _material(Color(0.62, 0.4, 0.32)), _material(Color(0.75, 0.7, 0.62)), 40)
	var pillar := _structure(Vector3(0.5, 3, 0.5), Vector3(2.4, 1.5, -0.5), _material(Color(0.62, 0.62, 0.6)), _material(Color(0.78, 0.76, 0.7)), 14)
	wall.break_apart(Vector3(-2.2, 1.7, -0.88), Vector3(0, 0, -1500))
	wall.break_apart(Vector3(-0.2, 1.0, -0.88), Vector3(0, 0, -1500))
	pillar.break_apart(Vector3(2.4, 0.3, -0.25), Vector3(0, 0, -400))
