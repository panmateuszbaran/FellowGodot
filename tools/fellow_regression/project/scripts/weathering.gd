extends Node3D
## FellowEngine's image regression suite: FellowWeathering3D (milestone 190). A corner of a two-storey house — plaster
## over a stone base, windows with stone sills, a cornice under the eaves, a balcony — on a paved ground, weathered: a
## patina, streaks hanging from the sills, the cornice and the balcony, damp at the walls' foot. A crate beside it is
## on layer 2, outside the weathering's cull mask (as a game's characters would be).


func _box(size: Vector3, pos: Vector3, mat: Material, layer := 1) -> void:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = size
	mi.mesh = bm
	mi.position = pos
	mi.material_override = mat
	mi.layers = layer
	add_child(mi)


func _mat(c: Color, rough := 0.9) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = rough
	return m


func _ready() -> void:
	var plaster := _mat(Color(0.82, 0.7, 0.52))
	var stone := _mat(Color(0.62, 0.58, 0.52))
	var ground := _mat(Color(0.45, 0.43, 0.4))
	var dark := _mat(Color(0.08, 0.08, 0.09), 0.3)
	var wood := _mat(Color(0.35, 0.22, 0.12))
	_box(Vector3(30, 0.2, 30), Vector3(0, -0.1, 0), ground)
	# The front wall (facing +z) 10 m wide, 7 m high, with a stone base; windows on two storeys.
	_box(Vector3(10, 1.0, 0.5), Vector3(0, 0.5, -0.25), stone)
	for row in 2:
		var y0 := 1.0 + row * 3.0
		_box(Vector3(10, 1.0, 0.5), Vector3(0, y0 + 0.5, -0.25), plaster)
		_box(Vector3(10, 0.4, 0.5), Vector3(0, y0 + 2.8, -0.25), plaster)
		for c in 4:
			_box(Vector3(1.6, 1.6, 0.5), Vector3(-4.2 + c * 2.8, y0 + 1.8, -0.25), plaster)
		for c in 3:
			var cx := -2.8 + c * 2.8
			_box(Vector3(1.2, 1.6, 0.05), Vector3(cx, y0 + 1.8, -0.4), dark)
			# The sill: 8 cm proud of the wall, 10 cm wider each side.
			_box(Vector3(1.4, 0.08, 0.2), Vector3(cx, y0 + 0.96, 0.04), stone)
	_box(Vector3(10, 0.5, 0.5), Vector3(0, 7.25, -0.25), plaster)
	# The cornice under the eaves: 40 cm proud.
	_box(Vector3(10.8, 0.3, 0.9), Vector3(0, 7.65, -0.05), stone)
	# A balcony on the upper storey, before the middle window.
	_box(Vector3(1.8, 0.12, 0.9), Vector3(0, 4.0, 0.45), stone)
	# The side wall (facing +x), plain plaster over the stone base.
	_box(Vector3(0.5, 1.0, 8.0), Vector3(5.25, 0.5, -4.25), stone)
	_box(Vector3(0.5, 6.8, 8.0), Vector3(5.25, 4.4, -4.25), plaster)
	_box(Vector3(0.9, 0.3, 8.4), Vector3(5.45, 7.65, -4.25), stone)
	# A crate on layer 2: not weathered.
	_box(Vector3(0.8, 0.8, 0.8), Vector3(3.0, 0.4, 1.2), wood, 2)
	var w: Node3D = ClassDB.instantiate("FellowWeathering3D")
	w.set("heightfield_size", 32.0)
	w.set("heightfield_layers", 6)
	w.set("heightfield_layer_height", 1.75)
	w.set("heightfield_resolution", 1024)
	w.set("cull_mask", 1)
	w.position = Vector3(0, -0.2, 0) # the layers from just under the ground up
	add_child(w)
