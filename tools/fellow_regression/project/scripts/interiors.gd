extends Node3D
## FellowEngine's image regression suite: FellowInteriorMaterial3D (milestone 188). A wall of two storeys of three
## windows, their glass with rooms behind it (each window's UV2: the metres right of its middle and up from its room's
## floor), half the rooms' lamps lit in a dim day, seen at an angle; beside it a block whose glass is a grid of rooms
## (rooms_from_uv2 off).


func _box(size: Vector3, pos: Vector3, mat: Material) -> void:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = size
	mi.mesh = bm
	mi.position = pos
	mi.material_override = mat
	add_child(mi)


func _ready() -> void:
	var wall := StandardMaterial3D.new()
	wall.albedo_color = Color(0.75, 0.62, 0.45)
	var glass: Material = ClassDB.instantiate("FellowInteriorMaterial3D")
	glass.set("daylight", 0.5)
	glass.set("lamp_share", 0.5)
	glass.set("pane_size", Vector2(0.5, 0.6))
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for row in 2:
		var y0 := row * 3.2
		_box(Vector3(9.0, 0.9, 0.4), Vector3(0, y0 + 0.45, -0.2), wall)
		_box(Vector3(9.0, 3.2 - 2.6, 0.4), Vector3(0, y0 + 2.9, -0.2), wall)
		for c in 4:
			_box(Vector3(2.0, 1.7, 0.4), Vector3(-4.5 + c * 3.0, y0 + 1.75, -0.2), wall)
		for c in 3:
			var cx := -3.0 + c * 3.0
			var pts := [Vector2(-0.5, 0.0), Vector2(0.5, 0.0), Vector2(0.5, 1.7), Vector2(-0.5, 1.7)]
			for i in [0, 2, 1, 0, 3, 2]:
				var p: Vector2 = pts[i]
				st.set_normal(Vector3(0, 0, 1))
				st.set_uv2(p + Vector2(0.0, 0.9))
				st.add_vertex(Vector3(cx + p.x, y0 + 0.9 + p.y, -0.14))
	var glass_mesh := MeshInstance3D.new()
	glass_mesh.mesh = st.commit()
	glass_mesh.material_override = glass
	add_child(glass_mesh)
	# A block of glass rooms in a grid, round the corner.
	var grid: Material = ClassDB.instantiate("FellowInteriorMaterial3D")
	grid.set("rooms_from_uv2", false)
	grid.set("seed", 3.0)
	var block := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = Vector3(4.0, 6.4, 4.0)
	block.mesh = bm
	block.position = Vector3(7.0, 3.2, -2.5)
	block.material_override = grid
	add_child(block)
