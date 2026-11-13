extends Node3D
## FellowEngine's image regression suite: FellowDestructible3D (milestone 74). A crate, a pillar and a pane of glass,
## prefractured (their patterns computed when they are ready, on the main thread: the same pieces every run) and broken
## as the scene starts, their pieces flying and falling under the physics at the capture's fixed frame rate; the cut
## faces in a lighter material.


func _material(c: Color, alpha := 1.0) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(c, alpha)
	m.roughness = 0.85
	if alpha < 1.0:
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		m.roughness = 0.1
	return m


func _destructible(mesh: Mesh, shape: Shape3D, pos: Vector3, outer: Material, inner: Material, count: int, seed: int) -> FellowDestructible3D:
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
	d.seed = seed
	d.prefracture = true
	d.break_on_impact = false
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
	var wood := _material(Color(0.55, 0.38, 0.22))
	var inside := _material(Color(0.85, 0.7, 0.48))
	var box := BoxMesh.new()
	var box_shape := BoxShape3D.new()
	var crate := _destructible(box, box_shape, Vector3(-2.5, 0.5, 0), wood, inside, 12, 3)
	var pm2 := BoxMesh.new()
	pm2.size = Vector3(0.5, 3.0, 0.5)
	var ps := BoxShape3D.new()
	ps.size = pm2.size
	var stone := _material(Color(0.62, 0.62, 0.6))
	var pillar := _destructible(pm2, ps, Vector3(0.2, 1.5, -1.0), stone, _material(Color(0.78, 0.76, 0.7)), 14, 5)
	pillar.density = 2500.0
	var gl := BoxMesh.new()
	gl.size = Vector3(1.6, 1.2, 0.02)
	var gls := BoxShape3D.new()
	gls.size = gl.size
	var glass_mat := _material(Color(0.7, 0.85, 0.9), 0.55)
	var glass := _destructible(gl, gls, Vector3(2.8, 1.3, 0.2), glass_mat, glass_mat, 24, 9)
	glass.density = 2500.0
	crate.burst_speed = 3.0
	crate.break_apart(to_global(Vector3(-2.1, 0.95, 0.4)), Vector3(0, 0, -900))
	pillar.break_apart(to_global(Vector3(0.2, 2.0, -0.75)), Vector3(0, 0, -400))
	glass.break_apart(to_global(Vector3(2.8, 1.3, 0.21)), Vector3(0, 0, -15))
