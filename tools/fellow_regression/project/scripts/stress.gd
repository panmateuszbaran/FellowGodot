extends Node3D
## FellowEngine's image regression suite: FellowDestructible3D's stress (milestone 122). An L of stone — a column with
## an arm held 2 m out at its top, too weak to carry it — settled as the scene starts (the arm cracks at its root and
## falls), and a weak wall with a hole blown in its foot (the part over the hole cracks and falls), under the physics at
## the capture's fixed frame rate.


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.85
	return m


func _l_mesh() -> ArrayMesh:
	var poly := PackedVector2Array([Vector2(0, 0), Vector2(0.5, 0), Vector2(0.5, 2.5), Vector2(2.5, 2.5), Vector2(2.5, 3.0), Vector2(0, 3.0)])
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var tris := Geometry2D.triangulate_polygon(poly)
	for i in range(0, tris.size(), 3):
		var a := poly[tris[i]]
		var b := poly[tris[i + 1]]
		var c := poly[tris[i + 2]]
		for v in [a, c, b]:
			st.set_uv(v)
			st.add_vertex(Vector3(v.x, v.y, 0.25))
		for v in [a, b, c]:
			st.set_uv(v)
			st.add_vertex(Vector3(v.x, v.y, -0.25))
	for i in poly.size():
		var p := poly[i]
		var q := poly[(i + 1) % poly.size()]
		for v in [Vector3(p.x, p.y, 0.25), Vector3(q.x, q.y, 0.25), Vector3(q.x, q.y, -0.25), Vector3(p.x, p.y, 0.25), Vector3(q.x, q.y, -0.25), Vector3(p.x, p.y, -0.25)]:
			st.set_uv(Vector2(v.x + v.y, v.z))
			st.add_vertex(v)
	st.generate_normals()
	return st.commit()


func _structure(mesh: Mesh, shape: Shape3D, pos: Vector3, outer: Material, inner: Material, count: int, strength: float) -> FellowDestructible3D:
	var body := StaticBody3D.new()
	body.position = pos
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.set_surface_override_material(0, outer)
	body.add_child(mi)
	var cs := CollisionShape3D.new()
	cs.shape = shape
	body.add_child(cs)
	var d := FellowDestructible3D.new()
	d.piece_count = count
	d.seed = 11
	d.prefracture = true
	d.structural = true
	d.strength = strength
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
	var l_mesh := _l_mesh()
	var l := _structure(l_mesh, l_mesh.create_trimesh_shape(), Vector3(-2.6, 0, -1.0), _material(Color(0.62, 0.62, 0.6)), _material(Color(0.78, 0.76, 0.7)), 30, 150.0)
	var box := BoxMesh.new()
	box.size = Vector3(4, 3, 0.25)
	var box_shape := BoxShape3D.new()
	box_shape.size = box.size
	var wall := _structure(box, box_shape, Vector3(2.8, 1.5, -1.5), _material(Color(0.62, 0.4, 0.32)), _material(Color(0.75, 0.7, 0.62)), 40, 20.0)
	wall.break_radius = 0.9
	l.update_structure()
	wall.break_apart(to_global(Vector3(2.8, 0.45, -1.38)), Vector3(0, 0, -2500))
