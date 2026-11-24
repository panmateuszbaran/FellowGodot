extends RefCounted
## Milestone 188 (decision 0330): each window's glass given its UV2 for FellowInteriorMaterial3D's rooms — the metres
## right of the window's middle and up from its room's floor, in the facade's frame (its side the up axis crossed with
## its normal, as the material has it). A window is a connected piece of the glass (its triangles sharing corners); its
## room's floor 0.9 m under it (a sill), less for a tall window (a door's glass, a shop's) so that it fits under the
## room's ceiling; a tall window's glass (a door's, a shop's: its room's floor at its bottom) is given a vertex colour
## whose alpha is 0 (the material draws no curtains behind it), the others white.
##
##   var arrays := WindowUV2.add(mesh.surface_get_arrays(i), 3.6)  # then add_surface_from_arrays(..., arrays)


## The surface's arrays with UV2 set (a copy), for rooms [param p_room_height] high (the material's room_size.y).
static func add(p_arrays: Array, p_room_height := 3.6) -> Array:
	var v: PackedVector3Array = p_arrays[Mesh.ARRAY_VERTEX]
	var normals: PackedVector3Array = p_arrays[Mesh.ARRAY_NORMAL] if p_arrays[Mesh.ARRAY_NORMAL] != null else PackedVector3Array()
	var index: PackedInt32Array = p_arrays[Mesh.ARRAY_INDEX] if p_arrays[Mesh.ARRAY_INDEX] != null else PackedInt32Array()
	if index.is_empty():
		index.resize(v.size())
		for i in v.size():
			index[i] = i
	# The corners welded by where they are, then joined by the triangles.
	var welded := {}
	var corner := PackedInt32Array()
	corner.resize(v.size())
	for i in v.size():
		var k := Vector3i(roundi(v[i].x * 1000.0), roundi(v[i].y * 1000.0), roundi(v[i].z * 1000.0))
		if not welded.has(k):
			welded[k] = welded.size()
		corner[i] = welded[k]
	var parent := []
	parent.resize(welded.size())
	for i in parent.size():
		parent[i] = i
	for t in range(0, index.size() - 2, 3):
		var a := _root(parent, corner[index[t]])
		for j in [1, 2]:
			var b := _root(parent, corner[index[t + j]])
			if b != a:
				parent[b] = a
	# Each window's frame and extent.
	var windows := {}
	for i in v.size():
		var r := _root(parent, corner[i])
		if not windows.has(r):
			windows[r] = {"normal": Vector3.ZERO, "vertices": PackedInt32Array()}
		windows[r]["normal"] += normals[i] if normals.size() > i else Vector3.ZERO
		windows[r]["vertices"].append(i)
	var uv2 := PackedVector2Array()
	uv2.resize(v.size())
	var colors := PackedColorArray()
	colors.resize(v.size())
	for r in windows:
		var w: Dictionary = windows[r]
		var n: Vector3 = w["normal"].normalized() if w["normal"].length() > 1e-6 else Vector3(0, 0, 1)
		var side := Vector3.UP.cross(n)
		side = side.normalized() if side.length() > 1e-3 else Vector3(1, 0, 0)
		var lo := INF
		var hi := -INF
		var bottom := INF
		for i in w["vertices"]:
			var s := v[i].dot(side)
			lo = minf(lo, s)
			hi = maxf(hi, s)
			bottom = minf(bottom, v[i].y)
		var top := -INF
		for i in w["vertices"]:
			top = maxf(top, v[i].y)
		var middle := (lo + hi) * 0.5
		var sill := clampf(p_room_height - 0.35 - (top - bottom), 0.0, 0.9)
		for i in w["vertices"]:
			uv2[i] = Vector2(v[i].dot(side) - middle, v[i].y - bottom + sill)
			colors[i] = Color(1, 1, 1, 1.0 if sill > 0.0 else 0.0)
	var out := p_arrays.duplicate()
	out[Mesh.ARRAY_TEX_UV2] = uv2
	out[Mesh.ARRAY_COLOR] = colors
	return out


## The rooftops' windows' glass (milestone 188): FellowInteriorMaterial3D's rooms behind them in the golden hour —
## no lamps lit (milestone 159: one lamp had lit the climbed window white beside the reference's dark glass), each
## window's panes a little out of true.
static func glass_material() -> Material:
	var m: Material = ClassDB.instantiate("FellowInteriorMaterial3D")
	m.set("room_size", Vector3(3.6, ROOM_HEIGHT, 4.5))
	m.set("daylight", 1.0)
	m.set("reflection", 0.3)
	m.set("lamp_share", 0.0)
	m.set("curtain_share", 0.6)
	m.set("pane_size", Vector2(0.6, 0.75))
	return m


const ROOM_HEIGHT := 3.6


static func _root(p_parent: Array, p_a: int) -> int:
	var a := p_a
	while p_parent[a] != a:
		p_parent[a] = p_parent[p_parent[a]]
		a = p_parent[a]
	return a
