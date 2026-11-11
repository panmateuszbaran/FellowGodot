extends SceneTree
## Builds the showcase's scenes and resources from code (no third-party asset): res://main.tscn, res://player.tscn and
## the files under res://world and res://character. Run it again after changing it:
##
##   godot --headless --path showcase -s res://tools/build_showcase.gd
##
## The valley (512 m): a FellowTerrain3D (1 m grid) whose layers are painted by rules (rock on the slopes, sand at the
## lake's shore, the village's earth and its path, meadows, forest on the hills) and whose foliage — grass, flowers,
## bushes, broadleaf trees, pines, rocks — is scattered by the terrain around the camera; a lake (FellowWater3D); a
## village of six houses around a campfire (FellowGasVolume3D, a log burning with FellowBurnable3D); the weather
## (FellowWeather3D with FellowWind3D, FellowRain3D and FellowLightning3D); the sky (FellowEngine's atmosphere and
## clouds, project settings); a FellowPipelineWarmer3D; the player (player.gd, the humanoid of humanoid_builder.gd).

const Humanoid := preload("res://tools/humanoid_builder.gd")

const SIZE := 512.0
const SPACING := 1.0
const VILLAGE := Vector2(-40.0, -30.0)
const VILLAGE_R := 30.0
const VILLAGE_H := 3.0
const LAKE := Vector2(64.0, 58.0)
const LAKE_R := 46.0
const WATER := 0.0
const LAKE_BED := -4.5

## The houses round the square: the angle (degrees) and the distance from its centre.
const HOUSES := [[15.0, 17.5], [70.0, 18.5], [128.0, 17.0], [190.0, 19.0], [245.0, 17.5], [300.0, 18.0]]

var noise_a := FastNoiseLite.new()
var noise_b := FastNoiseLite.new()
var noise_c := FastNoiseLite.new()
var scene_root: Node3D


func _init() -> void:
	noise_a.seed = 1207
	noise_a.frequency = 0.006
	noise_a.fractal_octaves = 4
	noise_b.seed = 88
	noise_b.frequency = 0.03
	noise_b.fractal_octaves = 2
	noise_c.seed = 4242
	noise_c.frequency = 0.012
	DirAccess.make_dir_recursive_absolute("res://world")
	DirAccess.make_dir_recursive_absolute("res://character")
	var t0 := Time.get_ticks_msec()
	_build_player()
	_build_neural_player()
	_build_main()
	print("showcase built in %.1f s" % ((Time.get_ticks_msec() - t0) / 1000.0))
	quit(0)


# ------------------------------------------------------------------ helpers

func _save(p_res: Resource, p_path: String) -> Resource:
	var err := ResourceSaver.save(p_res, p_path, ResourceSaver.FLAG_COMPRESS)
	if err != OK:
		push_error("could not save %s: %d" % [p_path, err])
	p_res.take_over_path(p_path)
	return p_res


func _add(p_parent: Node, p_node: Node, p_name := "") -> Node:
	if p_name != "":
		p_node.name = p_name
	p_parent.add_child(p_node)
	p_node.owner = scene_root
	return p_node


func _mat(p_color: Color, p_rough := 0.85, p_metal := 0.0) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = p_color
	m.roughness = p_rough
	m.metallic = p_metal
	return m


func _wind_mat(p_color: Color, p_rough: float, p_two_sided: bool, p_height: float, p_bend: float, p_flutter: float) -> StandardMaterial3D:
	var m := _mat(p_color, p_rough)
	if p_two_sided:
		m.cull_mode = BaseMaterial3D.CULL_DISABLED
	# FellowEngine's Wind group of StandardMaterial3D (decision 0103): swayed by the scene's FellowWind3D.
	m.set("wind", true)
	m.set("wind_height", p_height)
	m.set("wind_bend", p_bend)
	m.set("wind_flutter", p_flutter)
	return m


func _mesh_instance(p_parent: Node, p_mesh: Mesh, p_xform: Transform3D, p_mat: Material = null, p_name := "") -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = p_mesh
	mi.transform = p_xform
	if p_mat:
		mi.material_override = p_mat
	_add(p_parent, mi, p_name)
	return mi


## A static collider on the buildings' layer (2): the simple character collides with it, the MotionBricks one steers
## round it.
func _obstacle(p_parent: Node3D, p_shape: Shape3D, p_xform: Transform3D, p_name := "Collision") -> void:
	var body := StaticBody3D.new()
	body.collision_layer = 2
	body.collision_mask = 0
	_add(p_parent, body, p_name)
	var cs := CollisionShape3D.new()
	cs.shape = p_shape
	cs.transform = p_xform
	_add(body, cs)


func _box(p_size: Vector3) -> BoxMesh:
	var b := BoxMesh.new()
	b.size = p_size
	return b


# ------------------------------------------------------------------ the terrain

func height(x: float, z: float) -> float:
	var r := Vector2(x, z).length()
	var n1 := noise_a.get_noise_2d(x, z)
	var n2 := noise_b.get_noise_2d(x, z)
	var h := 4.0 + n1 * 5.0 + n2 * 0.5
	h = maxf(h, 1.2)
	# Hills rising to the map's edge, rougher as they rise.
	var rim := smoothstep(135.0, 245.0, r)
	h += rim * (48.0 + n1 * 26.0 + noise_c.get_noise_2d(x, z) * 14.0 * rim)
	# The village's plateau.
	var dv := Vector2(x, z).distance_to(VILLAGE)
	h = lerpf(h, VILLAGE_H + n2 * 0.12, smoothstep(VILLAGE_R + 28.0, VILLAGE_R - 2.0, dv))
	# The lake's bowl (its shore wavy).
	var dl := Vector2(x, z).distance_to(LAKE) * (1.0 + 0.12 * noise_c.get_noise_2d(x * 2.0, z * 2.0))
	h = lerpf(h, LAKE_BED, smoothstep(LAKE_R + 26.0, LAKE_R - 14.0, dl))
	return h


func normal_at(x: float, z: float) -> Vector3:
	var e := 1.0
	return Vector3(height(x - e, z) - height(x + e, z), 2.0 * e, height(x, z - e) - height(x, z + e)).normalized()


func _segment_distance(p: Vector2, a: Vector2, b: Vector2) -> float:
	var ab := b - a
	var t := clampf((p - a).dot(ab) / ab.length_squared(), 0.0, 1.0)
	return p.distance_to(a + ab * t)


func _path_distance(p: Vector2) -> float:
	# The path from the village's square to the lake's shore, bending with the noise.
	var a := VILLAGE
	var b := LAKE + (VILLAGE - LAKE).normalized() * (LAKE_R - 4.0)
	var ab := b - a
	var t := clampf((p - a).dot(ab) / ab.length_squared(), 0.0, 1.0)
	var q := a + ab * t + Vector2(-ab.y, ab.x).normalized() * sin(t * PI * 2.0) * 7.0
	return p.distance_to(q)


func _build_terrain() -> Node3D:
	var n := int(SIZE / SPACING) + 1
	var heights := PackedFloat32Array()
	heights.resize(n * n)
	for j in n:
		var z := -SIZE * 0.5 + j * SPACING
		for i in n:
			heights[j * n + i] = height(-SIZE * 0.5 + i * SPACING, z)
	var img := Image.create_from_data(n, n, false, Image.FORMAT_RF, heights.to_byte_array())
	_save(img, "res://world/heightmap.res")
	# The layers' weights, at half the heights' resolution: 1 rock, 2 earth, 3 sand, 4 meadow (grass and flowers grow
	# there), 5 forest floor (trees and bushes); grass (0) takes what they leave.
	var sn := (n - 1) / 2 + 1
	var splat := Image.create(sn, sn, false, Image.FORMAT_RGBA8)
	var splat2 := Image.create(sn, sn, false, Image.FORMAT_RGBA8)
	for j in sn:
		for i in sn:
			var x := -SIZE * 0.5 + i * SPACING * 2.0
			var z := -SIZE * 0.5 + j * SPACING * 2.0
			var p := Vector2(x, z)
			var h := height(x, z)
			var ny := normal_at(x, z).y
			var r := p.length()
			var dv := p.distance_to(VILLAGE)
			var dl := p.distance_to(LAKE)
			var dp := _path_distance(p)
			var detail := noise_b.get_noise_2d(x * 3.0, z * 3.0)
			var rock := clampf(smoothstep(0.8, 0.66, ny) + smoothstep(65.0, 95.0, h) * 0.6, 0.0, 1.0)
			var sand := smoothstep(1.4, 0.3, h - WATER) * smoothstep(LAKE_R + 46.0, LAKE_R + 26.0, dl)
			var earth := maxf(smoothstep(8.5, 5.5, dv) * clampf(0.8 + detail * 0.5, 0.0, 1.0), smoothstep(2.2, 0.9, dp))
			for hs in HOUSES:
				var door := VILLAGE + Vector2(sin(deg_to_rad(hs[0])), cos(deg_to_rad(hs[0]))) * (float(hs[1]) - 2.5)
				earth = maxf(earth, smoothstep(1.6, 0.6, _segment_distance(p, VILLAGE, door)) * clampf(0.85 + detail * 0.4, 0.0, 1.0))
			var forest := maxf(smoothstep(150.0, 175.0, r), smoothstep(0.28, 0.42, noise_c.get_noise_2d(x * 1.3, z * 1.3)) * smoothstep(VILLAGE_R + 10.0, VILLAGE_R + 25.0, dv))
			forest *= smoothstep(LAKE_R + 14.0, LAKE_R + 26.0, dl) * smoothstep(4.0, 9.0, dp)
			var meadow := (1.0 - forest) * smoothstep(-0.35, -0.1, noise_a.get_noise_2d(x * 4.0, z * 4.0)) * smoothstep(10.0, 16.0, dv) * smoothstep(1.5, 3.5, dp)
			# Priority: rock, sand, earth, then the forest and the meadow share what is left.
			var left := 1.0
			var w1 := rock * left
			left -= w1
			var w3 := sand * left
			left -= w3
			var w2 := earth * left
			left -= w2
			var w5 := forest * left
			left -= w5
			var w4 := meadow * left
			splat.set_pixel(i, j, Color(w1, w2, w3, w4))
			splat2.set_pixel(i, j, Color(w5, 0.0, 0.0, 0.0))
	_save(splat, "res://world/splat.res")
	_save(splat2, "res://world/splat2.res")
	var t: Node3D = ClassDB.instantiate("FellowTerrain3D")
	t.position = Vector3(-SIZE * 0.5, 0.0, -SIZE * 0.5)
	t.set("vertex_spacing", SPACING)
	var colours := [Color(0.33, 0.43, 0.2), Color(0.38, 0.35, 0.31), Color(0.45, 0.35, 0.25), Color(0.68, 0.62, 0.47),
		Color(0.37, 0.47, 0.2), Color(0.24, 0.27, 0.15)]
	var layers := []
	for c in colours:
		var l = ClassDB.instantiate("FellowTerrainLayer")
		l.set("albedo_color", c) # sRGB, as a material's (as main.tscn has them)
		l.set("roughness", 0.92)
		layers.append(l)
	layers[1].set("triplanar", true)
	layers[1].set("roughness", 0.8)
	layers[3].set("roughness", 0.75)
	t.set("layers", layers)
	t.set("heightmap", img)
	t.set("splat_map", splat)
	t.set("splat_map_2", splat2)
	t.set("foliage", _foliage())
	return t


# ------------------------------------------------------------------ foliage

func _grass_mesh() -> ArrayMesh:
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var cols := PackedColorArray()
	var idx := PackedInt32Array()
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	for b in 9:
		var ang := TAU * float(b) / 9.0 + rng.randf_range(-0.3, 0.3)
		var dir := Vector3(cos(ang), 0.0, sin(ang))
		var side := Vector3(-dir.z, 0.0, dir.x)
		var root := dir * rng.randf_range(0.02, 0.1)
		var h := rng.randf_range(0.3, 0.52)
		var lean := dir * rng.randf_range(0.08, 0.2)
		var w := 0.018
		var base := verts.size()
		verts.append_array(PackedVector3Array([root - side * w, root + side * w,
			root + lean * 0.45 + Vector3(0, h * 0.55, 0) - side * w * 0.7, root + lean * 0.45 + Vector3(0, h * 0.55, 0) + side * w * 0.7,
			root + lean + Vector3(0, h, 0)]))
		var nrm := (dir * 0.25 + Vector3.UP).normalized()
		var tip := Color(0.75, 0.82, 0.45).lerp(Color(0.62, 0.7, 0.35), rng.randf())
		for v in 5:
			norms.append(nrm)
		cols.append_array(PackedColorArray([Color(0.35, 0.45, 0.2), Color(0.35, 0.45, 0.2), Color(0.55, 0.65, 0.3), Color(0.55, 0.65, 0.3), tip]))
		idx.append_array(PackedInt32Array([base, base + 1, base + 2, base + 1, base + 3, base + 2, base + 2, base + 3, base + 4]))
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = verts
	arr[Mesh.ARRAY_NORMAL] = norms
	arr[Mesh.ARRAY_COLOR] = cols
	arr[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	var m := _wind_mat(Color(0.62, 0.72, 0.42), 0.85, true, 0.55, 1.0, 0.0)
	m.vertex_color_use_as_albedo = true
	mesh.surface_set_material(0, m)
	return _save(mesh, "res://world/grass.res")


func _flower_mesh(p_name: String, p_petal: Color) -> ArrayMesh:
	var mesh := ArrayMesh.new()
	var st := SurfaceTool.new()
	var stem := CylinderMesh.new()
	stem.top_radius = 0.006
	stem.bottom_radius = 0.008
	stem.height = 0.32
	stem.radial_segments = 4
	stem.rings = 1
	st.append_from(stem, 0, Transform3D(Basis(), Vector3(0, 0.16, 0)))
	st.set_material(_wind_mat(Color(0.3, 0.45, 0.18), 0.8, false, 0.35, 1.0, 0.0))
	st.commit(mesh)
	var petals := SphereMesh.new()
	petals.radius = 0.045
	petals.height = 0.03
	petals.radial_segments = 8
	petals.rings = 3
	var st2 := SurfaceTool.new()
	st2.append_from(petals, 0, Transform3D(Basis(), Vector3(0, 0.33, 0)))
	var pm := _wind_mat(p_petal, 0.6, false, 0.35, 1.0, 0.2)
	st2.set_material(pm)
	st2.commit(mesh)
	return _save(mesh, "res://world/%s.res" % p_name)


func _broadleaf_mesh() -> ArrayMesh:
	var mesh := ArrayMesh.new()
	var trunk := CylinderMesh.new()
	trunk.top_radius = 0.16
	trunk.bottom_radius = 0.32
	trunk.height = 5.0
	trunk.radial_segments = 8
	trunk.rings = 2
	var st := SurfaceTool.new()
	st.append_from(trunk, 0, Transform3D(Basis(), Vector3(0, 2.5, 0)))
	st.set_material(_wind_mat(Color(0.33, 0.25, 0.18), 0.95, false, 9.0, 0.25, 0.0))
	st.commit(mesh)
	var crown := SphereMesh.new()
	crown.radius = 1.0
	crown.height = 2.0
	crown.radial_segments = 12
	crown.rings = 7
	var st2 := SurfaceTool.new()
	var blobs := [[Vector3(0, 5.3, 0), 2.4], [Vector3(1.2, 6.2, -0.4), 1.7], [Vector3(-1.1, 6.4, 0.6), 1.6], [Vector3(0.2, 7.6, 0.1), 1.5], [Vector3(-0.3, 4.6, -1.2), 1.4]]
	for b in blobs:
		st2.append_from(crown, 0, Transform3D(Basis().scaled(Vector3(b[1], b[1] * 0.85, b[1])), b[0]))
	st2.set_material(_wind_mat(Color(0.2, 0.36, 0.13), 0.85, false, 9.0, 0.6, 0.5))
	st2.commit(mesh)
	return _save(mesh, "res://world/tree_broadleaf.res")


func _pine_mesh() -> ArrayMesh:
	var mesh := ArrayMesh.new()
	var trunk := CylinderMesh.new()
	trunk.top_radius = 0.08
	trunk.bottom_radius = 0.26
	trunk.height = 9.0
	trunk.radial_segments = 7
	trunk.rings = 1
	var st := SurfaceTool.new()
	st.append_from(trunk, 0, Transform3D(Basis(), Vector3(0, 4.5, 0)))
	st.set_material(_wind_mat(Color(0.3, 0.22, 0.16), 0.95, false, 11.0, 0.2, 0.0))
	st.commit(mesh)
	var cone := CylinderMesh.new()
	cone.top_radius = 0.0
	cone.bottom_radius = 1.0
	cone.height = 1.0
	cone.radial_segments = 10
	cone.rings = 1
	var st2 := SurfaceTool.new()
	for k in 5:
		var y := 2.4 + k * 1.55
		var r := 2.3 - k * 0.38
		st2.append_from(cone, 0, Transform3D(Basis().scaled(Vector3(r, 2.6, r)), Vector3(0, y + 1.1, 0)))
	st2.set_material(_wind_mat(Color(0.12, 0.25, 0.14), 0.85, false, 11.0, 0.45, 0.3))
	st2.commit(mesh)
	return _save(mesh, "res://world/tree_pine.res")


func _bush_mesh() -> ArrayMesh:
	var mesh := ArrayMesh.new()
	var s := SphereMesh.new()
	s.radius = 1.0
	s.height = 2.0
	s.radial_segments = 10
	s.rings = 6
	var st := SurfaceTool.new()
	for b in [[Vector3(0, 0.45, 0), 0.75], [Vector3(0.5, 0.4, 0.2), 0.55], [Vector3(-0.45, 0.35, -0.1), 0.6], [Vector3(0.1, 0.7, -0.3), 0.5]]:
		st.append_from(s, 0, Transform3D(Basis().scaled(Vector3(b[1], b[1] * 0.8, b[1])), b[0]))
	st.set_material(_wind_mat(Color(0.22, 0.34, 0.14), 0.85, false, 1.2, 0.5, 0.6))
	st.commit(mesh)
	return _save(mesh, "res://world/bush.res")


func _rock_mesh() -> ArrayMesh:
	var s := SphereMesh.new()
	s.radius = 1.0
	s.height = 2.0
	s.radial_segments = 12
	s.rings = 8
	var arr := s.get_mesh_arrays()
	var verts: PackedVector3Array = arr[Mesh.ARRAY_VERTEX]
	var rn := FastNoiseLite.new()
	rn.seed = 3
	rn.frequency = 1.3
	for i in verts.size():
		var v := verts[i]
		var k := 1.0 + 0.28 * rn.get_noise_3dv(v)
		verts[i] = Vector3(v.x * k * 1.3, maxf(v.y, -0.35) * k * 0.75, v.z * k)
	arr[Mesh.ARRAY_VERTEX] = verts
	arr[Mesh.ARRAY_NORMAL] = null
	arr[Mesh.ARRAY_TANGENT] = null
	var st := SurfaceTool.new()
	st.create_from_arrays(arr)
	st.generate_normals()
	st.set_material(_mat(Color(0.47, 0.46, 0.43), 0.85))
	var mesh := st.commit()
	return _save(mesh, "res://world/rock.res")


func _kind(p_mesh: Mesh, p_layer: int, p_density: float, p_range: float, p_scale: Vector2, p_seed: int, p_shadows: bool, p_min_weight := 0.4) -> Resource:
	var f = ClassDB.instantiate("FellowTerrainFoliage")
	f.set("mesh", p_mesh)
	f.set("layer", p_layer)
	f.set("density", p_density)
	f.set("range", p_range)
	f.set("scale_min", p_scale.x)
	f.set("scale_max", p_scale.y)
	f.set("seed", p_seed)
	f.set("cast_shadows", p_shadows)
	f.set("min_weight", p_min_weight)
	f.set("sink", 0.05)
	return f


func _foliage() -> Array:
	var kinds := []
	kinds.append(_kind(_grass_mesh(), 4, 2.2, 70.0, Vector2(0.8, 1.35), 11, false, 0.35))
	kinds.append(_kind(_grass_mesh(), 0, 0.5, 45.0, Vector2(0.6, 1.0), 12, false, 0.6))
	kinds.append(_kind(_flower_mesh("flower_white", Color(0.95, 0.93, 0.85)), 4, 0.05, 45.0, Vector2(0.8, 1.2), 21, false))
	kinds.append(_kind(_flower_mesh("flower_yellow", Color(0.95, 0.78, 0.15)), 4, 0.05, 45.0, Vector2(0.8, 1.2), 22, false))
	kinds.append(_kind(_flower_mesh("flower_violet", Color(0.55, 0.35, 0.8)), 4, 0.035, 45.0, Vector2(0.8, 1.2), 23, false))
	kinds.append(_kind(_bush_mesh(), 5, 0.012, 160.0, Vector2(0.7, 1.4), 31, true, 0.3))
	kinds.append(_kind(_broadleaf_mesh(), 5, 0.009, 900.0, Vector2(0.8, 1.3), 41, true, 0.3))
	var pine = _kind(_pine_mesh(), 5, 0.014, 900.0, Vector2(0.8, 1.4), 42, true, 0.3)
	pine.set("height_min", 8.0)
	kinds.append(pine)
	var rock = _kind(_rock_mesh(), 1, 0.006, 400.0, Vector2(0.4, 2.2), 51, true, 0.4)
	rock.set("align_to_normal", 0.6)
	rock.set("random_yaw", true)
	kinds.append(rock)
	return kinds


# ------------------------------------------------------------------ the village

func _house(p_parent: Node3D, p_pos: Vector3, p_yaw: float, p_size: Vector3, p_wall: Material, p_roof: Material, p_wood: Material, p_stone: Material, p_window: Material, p_index: int) -> void:
	var body := StaticBody3D.new()
	body.transform = Transform3D(Basis(Vector3.UP, p_yaw), p_pos)
	body.collision_layer = 2 # the buildings' layer: the MotionBricks character steers round it
	_add(p_parent, body, "House%d" % p_index)
	var w := p_size.x
	var h := p_size.y
	var d := p_size.z
	var roof_h := 2.3
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(w, h + roof_h, d)
	shape.shape = box
	shape.position = Vector3(0, (h + roof_h) * 0.5, 0)
	_add(body, shape, "Collision")
	_mesh_instance(body, _box(Vector3(w + 0.2, 0.5, d + 0.2)), Transform3D(Basis(), Vector3(0, 0.0, 0)), p_stone, "Plinth")
	_mesh_instance(body, _box(Vector3(w, h, d)), Transform3D(Basis(), Vector3(0, h * 0.5, 0)), p_wall, "Walls")
	var roof := PrismMesh.new()
	roof.size = Vector3(d + 0.9, roof_h, w + 0.9)
	_mesh_instance(body, roof, Transform3D(Basis(Vector3.UP, PI * 0.5), Vector3(0, h + roof_h * 0.5, 0)), p_roof, "Roof")
	# Timber: the corners and a beam under the eaves.
	for cx in [-1.0, 1.0]:
		for cz in [-1.0, 1.0]:
			_mesh_instance(body, _box(Vector3(0.22, h, 0.22)), Transform3D(Basis(), Vector3(cx * (w * 0.5 - 0.05), h * 0.5, cz * (d * 0.5 - 0.05))), p_wood)
	_mesh_instance(body, _box(Vector3(w + 0.1, 0.22, d + 0.1)), Transform3D(Basis(), Vector3(0, h - 0.11, 0)), p_wood, "Beam")
	_mesh_instance(body, _box(Vector3(0.7, 1.6, 0.7)), Transform3D(Basis(), Vector3(w * 0.25, h + roof_h * 0.7, d * 0.12)), p_stone, "Chimney")
	# The door faces +Z (the square), windows beside it and on the sides.
	_mesh_instance(body, _box(Vector3(1.05, 2.1, 0.12)), Transform3D(Basis(), Vector3(0, 1.05, d * 0.5 + 0.02)), p_wood, "Door")
	for wx in [-w * 0.3, w * 0.3]:
		_mesh_instance(body, _box(Vector3(0.95, 0.95, 0.1)), Transform3D(Basis(), Vector3(wx, 1.75, d * 0.5 + 0.02)), p_window)
	for sx in [-1.0, 1.0]:
		_mesh_instance(body, _box(Vector3(0.1, 0.95, 0.95)), Transform3D(Basis(), Vector3(sx * (w * 0.5 + 0.02), 1.75, 0)), p_window)
	# A lamp over the door.
	var lamp_mat := _mat(Color(1.0, 0.85, 0.6))
	lamp_mat.emission_enabled = true
	lamp_mat.emission = Color(1.0, 0.75, 0.45)
	lamp_mat.emission_energy_multiplier = 3.0
	_mesh_instance(body, SphereMesh.new(), Transform3D(Basis().scaled(Vector3(0.18, 0.18, 0.18)), Vector3(0.9, 2.45, d * 0.5 + 0.25)), lamp_mat, "Lamp")
	var light := OmniLight3D.new()
	light.position = Vector3(0.9, 2.35, d * 0.5 + 0.6)
	light.omni_range = 9.0
	light.light_color = Color(1.0, 0.72, 0.42)
	light.light_energy = 0.0
	light.add_to_group("showcase_lamps", true)
	_add(body, light, "LampLight")


func _build_village(p_root: Node3D, p_window: Material) -> Node3D:
	var village := Node3D.new()
	var centre := Vector3(VILLAGE.x, height(VILLAGE.x, VILLAGE.y), VILLAGE.y)
	village.position = centre
	_add(p_root, village, "Village")
	var walls := [Color(0.86, 0.82, 0.74), Color(0.8, 0.72, 0.6), Color(0.9, 0.88, 0.84), Color(0.74, 0.66, 0.56)]
	var roofs := [Color(0.42, 0.16, 0.12), Color(0.26, 0.27, 0.3), Color(0.36, 0.24, 0.16)]
	var wood := _mat(Color(0.28, 0.19, 0.12), 0.9)
	var stone := _mat(Color(0.42, 0.41, 0.39), 0.9)
	var rng := RandomNumberGenerator.new()
	rng.seed = 99
	var angles := []
	for hs in HOUSES:
		angles.append(hs[0])
	for i in HOUSES.size():
		var a := deg_to_rad(HOUSES[i][0])
		var r: float = HOUSES[i][1]
		var local := Vector3(sin(a) * r, 0.0, cos(a) * r)
		var world := centre + local
		var ground := height(world.x, world.z) - centre.y
		# Facing the square: the door (+Z) towards the centre.
		var yaw := atan2(-local.x, -local.z)
		var size := Vector3(rng.randf_range(6.0, 8.0), rng.randf_range(3.0, 3.6), rng.randf_range(5.0, 6.2))
		_house(village, Vector3(local.x, ground - 0.15, local.z), yaw, size, _mat(walls[i % walls.size()], 0.9), _mat(roofs[i % roofs.size()], 0.75), wood, stone, p_window, i)
	_build_campfire(village)
	# Benches round the fire.
	var log_mesh := CylinderMesh.new()
	log_mesh.top_radius = 0.2
	log_mesh.bottom_radius = 0.22
	log_mesh.height = 2.2
	log_mesh.radial_segments = 10
	for i in 4:
		var a := TAU * (float(i) + 0.5) / 4.0
		var p := Vector3(sin(a) * 3.4, 0.2, cos(a) * 3.4)
		_mesh_instance(village, log_mesh, Transform3D(Basis(Vector3.UP, a + PI * 0.5) * Basis(Vector3.RIGHT, PI * 0.5), p), wood, "Bench%d" % i)
		var bench_shape := BoxShape3D.new()
		bench_shape.size = Vector3(2.2, 0.45, 0.45)
		_obstacle(village, bench_shape, Transform3D(Basis(Vector3.UP, a + PI * 0.5) * Basis(Vector3.UP, PI * 0.5), p), "Bench%dCollision" % i)
	# A well, barrels and crates.
	var well := Node3D.new()
	well.position = Vector3(8.5, 0.0, -6.0)
	_add(village, well, "Well")
	var ring := CylinderMesh.new()
	ring.top_radius = 0.9
	ring.bottom_radius = 0.95
	ring.height = 0.9
	ring.radial_segments = 16
	_mesh_instance(well, ring, Transform3D(Basis(), Vector3(0, 0.45, 0)), stone)
	var water_disc := CylinderMesh.new()
	water_disc.top_radius = 0.75
	water_disc.bottom_radius = 0.75
	water_disc.height = 0.02
	_mesh_instance(well, water_disc, Transform3D(Basis(), Vector3(0, 0.8, 0)), _mat(Color(0.05, 0.08, 0.1), 0.05))
	for sx in [-0.8, 0.8]:
		_mesh_instance(well, _box(Vector3(0.12, 2.0, 0.12)), Transform3D(Basis(), Vector3(sx, 1.0, 0)), wood)
	var well_roof := PrismMesh.new()
	well_roof.size = Vector3(1.4, 0.7, 2.2)
	_mesh_instance(well, well_roof, Transform3D(Basis(Vector3.UP, PI * 0.5), Vector3(0, 2.2, 0)), _mat(roofs[0], 0.75))
	var well_body := StaticBody3D.new()
	well_body.collision_layer = 2
	_add(well, well_body, "Collision")
	var ws := CollisionShape3D.new()
	var wcyl := CylinderShape3D.new()
	wcyl.radius = 0.95
	wcyl.height = 0.9
	ws.shape = wcyl
	ws.position = Vector3(0, 0.45, 0)
	_add(well_body, ws)
	var barrel := CylinderMesh.new()
	barrel.top_radius = 0.32
	barrel.bottom_radius = 0.32
	barrel.height = 0.9
	barrel.radial_segments = 12
	var barrel_mat := _mat(Color(0.4, 0.27, 0.15), 0.8)
	var crate_mat := _mat(Color(0.55, 0.42, 0.26), 0.9)
	for i in 6:
		var a := deg_to_rad(angles[i] + 24.0)
		var p := Vector3(sin(a) * 13.0, 0.0, cos(a) * 13.0)
		p.y = height(centre.x + p.x, centre.z + p.z) - centre.y
		var pile := BoxShape3D.new()
		pile.size = Vector3(1.6, 1.2, 1.2)
		_obstacle(village, pile, Transform3D(Basis(), p + Vector3(0.3, 0.6, 0.1)), "Pile%dCollision" % i)
		if i % 2 == 0:
			_mesh_instance(village, barrel, Transform3D(Basis(), p + Vector3(0, 0.45, 0)), barrel_mat, "Barrel%d" % i)
			_mesh_instance(village, barrel, Transform3D(Basis(), p + Vector3(0.7, 0.45, 0.2)), barrel_mat)
		else:
			_mesh_instance(village, _box(Vector3(0.8, 0.8, 0.8)), Transform3D(Basis(Vector3.UP, 0.3 * i), p + Vector3(0, 0.4, 0)), crate_mat, "Crate%d" % i)
			_mesh_instance(village, _box(Vector3(0.6, 0.6, 0.6)), Transform3D(Basis(Vector3.UP, 0.7 * i), p + Vector3(0.1, 1.1, 0.05)), crate_mat)
	# Lamp posts along the path to the lake, and a pier.
	var post := CylinderMesh.new()
	post.top_radius = 0.06
	post.bottom_radius = 0.09
	post.height = 3.2
	post.radial_segments = 6
	var lamp_mat := _mat(Color(1.0, 0.85, 0.6))
	lamp_mat.emission_enabled = true
	lamp_mat.emission = Color(1.0, 0.75, 0.45)
	lamp_mat.emission_energy_multiplier = 3.0
	var shore := LAKE + (VILLAGE - LAKE).normalized() * (LAKE_R + 2.0)
	for k in 4:
		var t := (float(k) + 0.6) / 4.6
		var p2 := VILLAGE.lerp(shore, t)
		var ab := shore - VILLAGE
		p2 += Vector2(-ab.y, ab.x).normalized() * (sin(t * PI * 2.0) * 7.0 + 2.6)
		var base := Vector3(p2.x, height(p2.x, p2.y), p2.y) - centre
		_mesh_instance(village, post, Transform3D(Basis(), base + Vector3(0, 1.6, 0)), wood, "LampPost%d" % k)
		_mesh_instance(village, SphereMesh.new(), Transform3D(Basis().scaled(Vector3(0.22, 0.22, 0.22)), base + Vector3(0, 3.3, 0)), lamp_mat)
		var l := OmniLight3D.new()
		l.position = base + Vector3(0, 3.2, 0)
		l.omni_range = 12.0
		l.light_color = Color(1.0, 0.75, 0.45)
		l.light_energy = 0.0
		l.add_to_group("showcase_lamps", true)
		_add(village, l, "PostLight%d" % k)
	var pier := Node3D.new()
	var dir := (LAKE - shore).normalized()
	var pier_start := LAKE + (VILLAGE - LAKE).normalized() * (LAKE_R + 6.0)
	pier.position = Vector3(pier_start.x, WATER + 0.55, pier_start.y) - centre
	pier.rotation.y = atan2(dir.x, dir.y)
	_add(village, pier, "Pier")
	var plank_mat := _mat(Color(0.45, 0.33, 0.22), 0.85)
	_mesh_instance(pier, _box(Vector3(2.2, 0.16, 14.0)), Transform3D(Basis(), Vector3(0, 0, 7.0)), plank_mat, "Deck")
	for k in 5:
		for sx in [-1.0, 1.0]:
			_mesh_instance(pier, _box(Vector3(0.18, 3.0, 0.18)), Transform3D(Basis(), Vector3(sx * 1.0, -1.4, 1.0 + k * 3.0)), wood)
	var deck_body := StaticBody3D.new()
	_add(pier, deck_body, "Collision")
	var ds := CollisionShape3D.new()
	var dbox := BoxShape3D.new()
	dbox.size = Vector3(2.2, 0.16, 14.0)
	ds.shape = dbox
	ds.position = Vector3(0, 0, 7.0)
	_add(deck_body, ds)
	return village


func _build_campfire(p_village: Node3D) -> void:
	# A gas volume with kindling (Burn Lab's), a log burning on it, a ring of stones.
	var fire: Node3D = ClassDB.instantiate("FellowGasVolume3D")
	# Tall enough for the smoke to thin out before it leaves through the top (it fades out at the box's sides and top:
	# edge_fade, so the box does not show).
	fire.set("size", Vector3(4.0, 6.5, 4.0))
	fire.set("resolution", 72)
	fire.set("smoke_dissipation", 0.22)
	fire.set("edge_fade", 0.7)
	fire.set("prewarm_steps", 240) # burning when the game starts
	fire.set("turbulence", 5.0)
	fire.set("vorticity", 0.35)
	fire.set("fire_intensity", 18.0)
	fire.set("physics_obstacles", false)
	fire.set("closed_faces", 4) # the floor: the smoke leaves through the sides as well as the top
	fire.set("fire_light_shadows", true)
	fire.set("fire_light_smoothing", 0.15) # the light flickers as a fire's, not with every step's flames
	_add(p_village, fire, "Campfire")
	var kindling: Node3D = ClassDB.instantiate("FellowGasEmitter3D")
	kindling.set("shape", 2)
	kindling.set("radius", 0.25)
	kindling.set("height", 0.12)
	kindling.position = Vector3(0, 0.08, 0)
	kindling.set("fuel", 2.5)
	kindling.set("temperature", 900.0)
	kindling.set("smoke", 0.1)
	kindling.set("noise", 0.75)
	kindling.set("noise_scale", 0.15)
	_add(fire, kindling, "Kindling")
	var log_mi := MeshInstance3D.new()
	var cm := CylinderMesh.new()
	cm.top_radius = 0.09
	cm.bottom_radius = 0.095
	cm.height = 1.0
	cm.radial_segments = 16
	log_mi.mesh = cm
	log_mi.material_override = _mat(Color(0.26, 0.17, 0.1), 0.9)
	log_mi.transform = Transform3D(Basis(Vector3.FORWARD, PI * 0.5), Vector3(0, 0.18, 0))
	_add(fire, log_mi, "Log")
	var burn: Node = ClassDB.instantiate("FellowBurnable3D")
	burn.set("resolution", 32)
	_add(log_mi, burn, "Burnable")
	var stone := SphereMesh.new()
	stone.radius = 0.5
	stone.height = 1.0
	stone.radial_segments = 8
	stone.rings = 5
	var ring_shape := CylinderShape3D.new()
	ring_shape.radius = 1.15
	ring_shape.height = 2.0 # above the flames: nobody walks through the fire
	_obstacle(fire, ring_shape, Transform3D(Basis(), Vector3(0, 1.0, 0)), "RingCollision")
	var stone_mat := _mat(Color(0.36, 0.35, 0.34), 0.9)
	for i in 11:
		var a := TAU * float(i) / 11.0
		_mesh_instance(fire, stone, Transform3D(Basis(Vector3.UP, a * 3.0).scaled(Vector3(0.42, 0.3, 0.32)), Vector3(sin(a) * 0.95, 0.07, cos(a) * 0.95)), stone_mat)


# ------------------------------------------------------------------ the player

func _build_player() -> void:
	var hb = Humanoid.new()
	hb.define_bones()
	var mesh: ArrayMesh = _save(hb.build_mesh(), "res://character/humanoid_mesh.res")
	var skin: Skin = _save(hb.build_skin(), "res://character/humanoid_skin.tres")
	var clips: AnimationLibrary = _save(hb.build_clips(), "res://character/humanoid_clips.res")
	var player := CharacterBody3D.new()
	player.name = "Player"
	player.set_script(load("res://scripts/player.gd"))
	scene_root = player
	player.floor_snap_length = 0.4
	player.collision_mask = 3 # the terrain and the buildings
	player.floor_max_angle = deg_to_rad(50.0)
	var shape := CollisionShape3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.3
	capsule.height = 1.75
	shape.shape = capsule
	shape.position = Vector3(0, 0.875, 0)
	_add(player, shape, "CollisionShape3D")
	var model := Node3D.new()
	_add(player, model, "Model")
	var sk: Skeleton3D = hb.build_skeleton()
	_add(model, sk)
	var body := MeshInstance3D.new()
	body.mesh = mesh
	body.skin = skin
	_add(sk, body, "Body")
	body.skeleton = NodePath("..")
	# Footprints in the snow (FellowSnowTracker3D under each foot).
	for side in ["l", "r"]:
		var ba := BoneAttachment3D.new()
		_add(sk, ba, "Foot_" + side)
		ba.bone_name = "foot_" + side
		var tr: Node3D = ClassDB.instantiate("FellowSnowTracker3D")
		tr.set("radius", 0.07)
		tr.set("contact_height", 0.12)
		tr.position = Vector3(0, 0.04, 0)
		_add(ba, tr, "SnowTracker")
	var tree := AnimationTree.new()
	_add(player, tree, "AnimationTree")
	tree.root_node = NodePath("../Model")
	tree.add_animation_library("", clips)
	var bt := AnimationNodeBlendTree.new()
	var loco := AnimationNodeBlendSpace1D.new()
	loco.min_space = 0.0
	loco.max_space = 2.0
	for j in 3:
		var an := AnimationNodeAnimation.new()
		an.animation = ["idle", "walk", "run"][j]
		loco.add_blend_point(an, float(j), -1, ["idle", "walk", "run"][j])
	bt.add_node("locomotion", loco, Vector2(0, 0))
	bt.add_node("speed", AnimationNodeTimeScale.new(), Vector2(220, 0))
	var fall := AnimationNodeAnimation.new()
	fall.animation = "fall"
	bt.add_node("fall", fall, Vector2(220, 160))
	bt.add_node("air", AnimationNodeBlend2.new(), Vector2(440, 0))
	bt.connect_node("speed", 0, "locomotion")
	bt.connect_node("air", 0, "speed")
	bt.connect_node("air", 1, "fall")
	bt.connect_node("output", 0, "air")
	tree.tree_root = bt
	var pivot := Node3D.new()
	pivot.position = Vector3(0, 1.55, 0)
	_add(player, pivot, "CameraPivot")
	var arm := SpringArm3D.new()
	arm.spring_length = 4.5
	arm.collision_mask = 2 # the buildings (the script keeps the camera over the terrain and its grass)
	arm.margin = 0.2
	arm.rotation.x = deg_to_rad(-14.0)
	var sphere := SphereShape3D.new()
	sphere.radius = 0.25
	arm.shape = sphere
	_add(pivot, arm, "SpringArm3D")
	var cam := Camera3D.new()
	cam.fov = 68.0
	cam.near = 0.08
	cam.far = 2000.0
	cam.current = true
	cam.position = Vector3(0.45, 0.0, 0.0) # over the shoulder
	_add(arm, cam, "Camera3D")
	var ps := PackedScene.new()
	var err := ps.pack(player)
	if err != OK:
		push_error("could not pack the player: %d" % err)
	ResourceSaver.save(ps, "res://player.tscn")
	player.free()


func _build_neural_player() -> void:
	# The MotionBricks character (scripts/neural_player.gd): used when showcase/external holds the network's weights
	# and the SOMA mannequin (not part of the repository).
	var root := Node3D.new()
	root.name = "Player"
	root.set_script(load("res://scripts/neural_player.gd"))
	scene_root = root
	var ch: Node3D = ClassDB.instantiate("FellowMotionCharacter3D")
	ch.set("body_color", Color(0.5, 0.52, 0.56))
	_add(root, ch, "Character")
	var pivot := Node3D.new()
	pivot.position = Vector3(0, 1.55, 0)
	_add(root, pivot, "CameraPivot")
	var arm := SpringArm3D.new()
	arm.spring_length = 4.5
	arm.collision_mask = 2 # the buildings (the script keeps the camera over the terrain and its grass)
	arm.margin = 0.2
	arm.rotation.x = deg_to_rad(-14.0)
	var sphere := SphereShape3D.new()
	sphere.radius = 0.25
	arm.shape = sphere
	_add(pivot, arm, "SpringArm3D")
	var cam := Camera3D.new()
	cam.fov = 68.0
	cam.near = 0.08
	cam.far = 2000.0
	cam.current = true
	cam.position = Vector3(0.45, 0.0, 0.0)
	_add(arm, cam, "Camera3D")
	var ps := PackedScene.new()
	if ps.pack(root) != OK:
		push_error("could not pack the MotionBricks player")
	ResourceSaver.save(ps, "res://neural_player.tscn")
	root.free()


# ------------------------------------------------------------------ the main scene

func _build_main() -> void:
	var root := Node3D.new()
	root.name = "Showcase"
	root.set_script(load("res://scripts/showcase.gd"))
	scene_root = root
	# The sky: FellowEngine's atmosphere (project setting) drawn by the sky shader, stars at night.
	var env := Environment.new()
	var sky := Sky.new()
	var sky_mat := ShaderMaterial.new()
	sky_mat.shader = load("res://shaders/atmosphere_sky.gdshader")
	sky.sky_material = sky_mat
	sky.process_mode = Sky.PROCESS_MODE_INCREMENTAL
	sky.radiance_size = Sky.RADIANCE_SIZE_256
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_color = Color(0.08, 0.1, 0.16)
	env.ambient_light_sky_contribution = 0.85
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.tonemap_exposure = 1.0
	env.ssao_enabled = true
	env.ssao_radius = 1.2
	env.ssao_intensity = 1.6
	env.glow_enabled = false
	env.fog_enabled = true
	env.fog_density = 0.0
	env.fog_light_color = Color(0.6, 0.65, 0.72)
	env.fog_sky_affect = 0.4
	var we := WorldEnvironment.new()
	we.environment = env
	var attr := CameraAttributesPractical.new()
	we.camera_attributes = attr
	_add(root, we, "WorldEnvironment")
	var sun := DirectionalLight3D.new()
	sun.light_energy = 2.0
	sun.light_angular_distance = 0.5
	sun.shadow_enabled = true
	sun.shadow_blur = 1.0
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 180.0
	sun.directional_shadow_blend_splits = true
	sun.rotation_degrees = Vector3(-40.0, 220.0, 0.0)
	_add(root, sun, "Sun")
	var moon := DirectionalLight3D.new()
	moon.light_energy = 0.07
	moon.light_color = Color(0.62, 0.72, 1.0)
	moon.shadow_enabled = true
	moon.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS
	moon.directional_shadow_max_distance = 60.0
	moon.visible = false
	_add(root, moon, "Moon")
	var terrain := _build_terrain()
	_add(root, terrain, "Terrain")
	var water: Node3D = ClassDB.instantiate("FellowWater3D")
	water.position = Vector3(LAKE.x, WATER, LAKE.y)
	water.set("extent", Vector2((LAKE_R + 40.0) * 2.0, (LAKE_R + 40.0) * 2.0))
	water.set("wind_from_scene", true)
	water.set("wave_scale", 0.45)
	water.set("sun_path", NodePath("../Sun"))
	_add(root, water, "Lake")
	var window := _mat(Color(0.95, 0.8, 0.5), 0.3)
	window.emission_enabled = true
	window.emission = Color(1.0, 0.72, 0.4)
	window.emission_energy_multiplier = 0.2
	_save(window, "res://world/window_glow.tres")
	_build_village(root, window)
	# The weather: it drives the wind, the rain (and snow) and the lightning, the clouds, the fog and the sunlight.
	var wind: Node = ClassDB.instantiate("FellowWind3D")
	wind.set("speed", 4.0)
	_add(root, wind, "Wind")
	var rain: Node = ClassDB.instantiate("FellowRain3D")
	rain.set("rain", 0.0)
	rain.set("wetness", 0.0)
	rain.set("snow", 0.0)
	rain.set("streak_width", 0.004)
	rain.set("snowing_time", 120.0) # a white valley in two minutes of snow (the default: 15)
	rain.set("melting_time", 300.0)
	rain.set("wetting_time", 40.0)
	rain.set("drying_time", 240.0)
	_add(root, rain, "Rain")
	var lightning: Node = ClassDB.instantiate("FellowLightning3D")
	lightning.set("seed", 7)
	_add(root, lightning, "Lightning")
	var weather: Node = ClassDB.instantiate("FellowWeather3D")
	weather.set("wind_path", NodePath("../Wind"))
	weather.set("rain_path", NodePath("../Rain"))
	weather.set("lightning_path", NodePath("../Lightning"))
	weather.set("sun_path", NodePath("../Sun"))
	weather.set("environment_path", NodePath("../WorldEnvironment"))
	weather.set("transition_time", 20.0)
	_add(root, weather, "Weather")
	# The pipelines around the camera compiled while the scene starts (decision 0162), with its shelf of the scene's
	# meshes (0167).
	var warmer: Node = ClassDB.instantiate("FellowPipelineWarmer3D")
	_add(root, warmer, "PipelineWarmer")
	# The player, by the campfire, the lake behind it.
	var player: Node3D = load("res://player.tscn").instantiate()
	# Across the fire from the lake: the fire, the village and the lake beyond it in view.
	var spawn := VILLAGE - (LAKE - VILLAGE).normalized() * 9.0
	player.position = Vector3(spawn.x, height(spawn.x, spawn.y) + 0.2, spawn.y)
	player.set("water_level", WATER)
	_add(root, player, "Player")
	var to_fire := VILLAGE - spawn
	player.set("start_yaw", atan2(-to_fire.x, -to_fire.y))
	var hud := CanvasLayer.new()
	hud.set_script(load("res://scripts/hud.gd"))
	_add(root, hud, "HUD")
	var ps := PackedScene.new()
	var err := ps.pack(root)
	if err != OK:
		push_error("could not pack the main scene: %d" % err)
	ResourceSaver.save(ps, "res://main.tscn")
	root.free()
