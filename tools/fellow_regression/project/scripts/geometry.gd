extends Node3D
# Built the same every run: a town of boxes under a FellowHLOD3D (its proxies at a distance), a MultiMesh of 4000
# instances (culled per instance on the GPU), and a large subdivided ground (split into clusters).
# Without hlod (instances.tscn) the town's boxes are plain nodes: stock features only, for the stock group (a
# FellowHLOD3D builds instanced proxies with MultiMesh culling on and merged ones without, so with FellowEngine off its
# town is drawn from other vertices).

@export var hlod := true


func _ready() -> void:
	var box := BoxMesh.new()
	var town: Node3D = ClassDB.instantiate("FellowHLOD3D") if hlod else Node3D.new()
	if hlod:
		town.set("distance", 20.0)
		town.set("cell_size", 16.0)
	add_child(town)
	var rng := RandomNumberGenerator.new()
	rng.seed = 54
	for i in 400:
		var m := MeshInstance3D.new()
		m.mesh = box
		var h := rng.randf_range(1.0, 6.0)
		m.scale = Vector3(rng.randf_range(1.5, 3.0), h, rng.randf_range(1.5, 3.0))
		m.position = Vector3((i % 20) * 4.0 - 40.0, h * 0.5, -int(i / 20) * 4.0 - 30.0)
		town.add_child(m)
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	var ball := SphereMesh.new()
	ball.radius = 0.15
	ball.height = 0.3
	ball.radial_segments = 8 # 64 triangles: 4000 of the default 4096-triangle sphere were 85 M primitives a frame
	ball.rings = 4 # (shadows included), four seconds a frame on lavapipe
	mm.mesh = ball
	mm.instance_count = 4000
	for i in mm.instance_count:
		mm.set_instance_transform(i, Transform3D(Basis(), Vector3(rng.randf_range(-30, 30), 0.15, rng.randf_range(-25, 5))))
	var mmi := MultiMeshInstance3D.new()
	mmi.multimesh = mm
	add_child(mmi)
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(200, 200)
	plane.subdivide_width = 200
	plane.subdivide_depth = 200
	ground.mesh = plane
	add_child(ground)
