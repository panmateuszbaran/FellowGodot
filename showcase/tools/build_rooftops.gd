extends SceneTree
## Builds the rooftops scene (`res://rooftops/rooftops.tscn`): a Renaissance square at the golden hour — palazzi and
## houses round it, a bell tower with a beam out over a hay cart, a fountain, market stalls, banners and laundry in the
## wind, the city and its dome beyond — from the models made in Blender by `tools/blender/build_rooftops_models.py`.
##
##   python tools/blender/build_rooftops_models.py <dir>        (Blender's Python, or the bpy module)
##   godot --headless --path showcase --script tools/build_rooftops.gd -- <dir>
##
## Each model's meshes are saved under `rooftops/models` with the scene's materials (procedural shaders in
## `rooftops/shaders`, their settings in `rooftops/materials`), its collision mesh as a trimesh shape; the scene places
## them and adds the light, the sky, the cloth, the water and the player.

const OUT := "res://rooftops"

## Where each model stands: its centre on the ground (x, z) and its heading (degrees; 0: its front towards +Z).
const PLACES := {
	"PalazzoOcra": [Vector2(-9.0, -18.0), 0.0],
	"Campanile": [Vector2(2.5, -16.1), 0.0],
	"CasaCrema": [Vector2(12.2, -17.0), 0.0],
	"PalazzoSiena": [Vector2(22.5, -3.0), -90.0],
	"CasaBianca": [Vector2(21.5, 8.0), -90.0],
	"CasaOcraBassa": [Vector2(6.0, 18.0), 180.0],
	"CasaRosa": [Vector2(-9.0, 18.5), 180.0],
	"Fountain": [Vector2(-3.5, 1.0), 22.5],
	"StallA": [Vector2(-12.5, 4.0), 80.0],
	"StallB": [Vector2(11.0, 7.5), -105.0],
	"Skyline": [Vector2(0.0, 0.0), 0.0],
}

const BUILDINGS := ["PalazzoOcra", "Campanile", "CasaCrema", "PalazzoSiena", "CasaBianca", "CasaOcraBassa", "CasaRosa"]

## The ring of houses round the square (copies of its houses, their meshes shared): closing its west side, lining the
## streets out of it before the city's blocks (the Skyline model leaves this ring empty). [model, position, degrees]
const NEAR := [
	["CasaBianca", Vector2(-23.6, -9.5), 90.0], ["CasaCrema", Vector2(-23.1, 10.2), 90.0],
	["CasaOcraBassa", Vector2(-36.0, 2.0), 90.0],
	["CasaBianca", Vector2(-8.6, 28.0), 90.0], ["CasaCrema", Vector2(4.7, 27.6), -90.0],
	["CasaRosa", Vector2(33.5, -2.7), 0.0], ["CasaOcraBassa", Vector2(33.6, 8.4), 180.0],
	["CasaBianca", Vector2(1.1, -27.0), 90.0], ["CasaOcraBassa", Vector2(12.0, -28.0), -90.0],
]

## The buildings' footprints (their width and depth on the ground, the rusticated ground floor's 0.1 m included), as
## tools/blender/build_rooftops_models.py makes them.
const FOOTPRINTS := {
	"PalazzoOcra": Vector2(14.2, 10.2), "Campanile": Vector2(6.44, 6.44), "CasaCrema": Vector2(8.6, 8.2),
	"PalazzoSiena": Vector2(9.8, 11.2), "CasaBianca": Vector2(7.6, 9.2), "CasaOcraBassa": Vector2(11.2, 8.2),
	"CasaRosa": Vector2(10.2, 9.2),
}
const PAVEMENT := 1.2 ## the raised pavements' width along the fronts (m)
const KERB := 0.08 ## and their height

## The physics layers: 1 the world, 2 what can be climbed (the buildings' walls), 3 the hay (a leap's landing).
const LAYER_WORLD := 1
const LAYER_CLIMB := 2
const LAYER_HAY := 4

var models_dir := ""
var scene_root: Node3D
var mats := {}
var models := {}


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	models_dir = args[0] if args.size() > 0 else "/tmp/claude-0/ac_build"
	for d in ["", "/models", "/materials"]:
		DirAccess.make_dir_recursive_absolute(OUT + d)
	var t0 := Time.get_ticks_msec()
	seed(1234)
	_make_materials()
	for f in DirAccess.get_files_at(models_dir):
		if f.ends_with(".glb") and f != "Mannequin.glb":
			_load_model(models_dir.path_join(f))
	if FileAccess.file_exists(models_dir.path_join("Mannequin.glb")):
		models["Mannequin"] = {}
	_build_scene()
	print("rooftops built in %.1f s" % ((Time.get_ticks_msec() - t0) / 1000.0))
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


func _shader_mat(p_shader: String, p_params: Dictionary) -> ShaderMaterial:
	var m := ShaderMaterial.new()
	m.shader = load(OUT + "/shaders/" + p_shader + ".gdshader")
	for k in p_params:
		m.set_shader_parameter(k, p_params[k])
	return m


# ------------------------------------------------------------------ materials

func _make_materials() -> void:
	var plaster := {
		"plaster_ochre": [Color(0.8, 0.62, 0.42), 0.3, 1.0],
		"plaster_rose": [Color(0.8, 0.6, 0.53), 0.25, 2.0],
		"plaster_cream": [Color(0.86, 0.8, 0.68), 0.35, 3.0],
		"plaster_sienna": [Color(0.74, 0.48, 0.34), 0.3, 4.0],
		"plaster_white": [Color(0.86, 0.85, 0.8), 0.2, 5.0],
	}
	for n in plaster:
		mats[n] = _shader_mat("plaster", {"color": plaster[n][0], "fallen": plaster[n][1], "seed": plaster[n][2]})
		# The city beyond the square: the same plaster, its windows drawn on it (no geometry that far).
		mats["far_" + n] = _shader_mat("plaster", {"color": plaster[n][0], "fallen": 0.0, "seed": plaster[n][2], "far_windows": true})
	# Milestone 153: the city's houses (instances): the plaster's colour and its weathering's seed each instance's.
	mats["far_plaster"] = _shader_mat("plaster", {"color": Color(0.82, 0.7, 0.55), "fallen": 0.12, "instance_tint": true})
	mats["stone"] = _shader_mat("masonry", {"color": Color(0.76, 0.72, 0.64), "joint_color": Color(0.6, 0.57, 0.51),
			"block_size": Vector2(0.72, 0.34), "joint_width": 0.006, "joint_depth": 0.006, "chamfer": 0.008,
			"face_rough": 0.0015, "variation": 0.1})
	mats["stone_rustic"] = _shader_mat("masonry", {"color": Color(0.63, 0.6, 0.53), "joint_color": Color(0.42, 0.4, 0.36),
			"block_size": Vector2(1.15, 0.46), "joint_width": 0.014, "joint_depth": 0.035, "chamfer": 0.045,
			"face_rough": 0.012, "variation": 0.16, "hue_variation": 0.05, "grime": 0.45, "roughness": 0.88, "seed": 2.0})
	mats["brick"] = _shader_mat("masonry", {"color": Color(0.58, 0.31, 0.2), "joint_color": Color(0.64, 0.6, 0.52),
			"block_size": Vector2(0.27, 0.075), "joint_width": 0.01, "joint_depth": 0.006, "chamfer": 0.004,
			"face_rough": 0.0015, "variation": 0.22, "hue_variation": 0.08, "grime": 0.25, "seed": 3.0})
	mats["water_stone"] = _shader_mat("masonry", {"color": Color(0.72, 0.7, 0.64), "joint_color": Color(0.5, 0.5, 0.46),
			"block_size": Vector2(1.3, 0.5), "joint_width": 0.006, "joint_depth": 0.004, "chamfer": 0.01,
			"face_rough": 0.002, "variation": 0.06, "grime": 0.5, "seed": 4.0})
	mats["ground_stone"] = _shader_mat("masonry", {"color": Color(0.6, 0.57, 0.52), "joint_color": Color(0.38, 0.36, 0.33),
			"block_size": Vector2(0.95, 0.55), "row_offset": 0.38, "joint_width": 0.012, "joint_depth": 0.01,
			"chamfer": 0.015, "face_rough": 0.003, "slab_tilt": 0.012, "variation": 0.12, "grime": 0.0,
			"ground_dirt": 0.0, "roughness": 0.78, "seed": 5.0})
	mats["cobble"] = _shader_mat("masonry", {"color": Color(0.5, 0.47, 0.43), "joint_color": Color(0.3, 0.28, 0.26),
			"block_size": Vector2(0.22, 0.15), "joint_width": 0.02, "joint_depth": 0.02, "chamfer": 0.03,
			"face_rough": 0.004, "variation": 0.2, "grime": 0.0, "ground_dirt": 0.0, "seed": 6.0})
	mats["roof"] = _shader_mat("roof_tiles", {})
	# The ground: the square's slabs and bands, the fan round the fountain, the pavements along the fronts, the streets'
	# cobbles (rooftops/shaders/paving.gdshader), told where the houses stand.
	var walls := PackedVector4Array()
	for n in FOOTPRINTS:
		walls.append(_footprint(n))
	mats["paving"] = _shader_mat("paving", {"walls": walls, "wall_count": walls.size(), "pavement_width": PAVEMENT,
			"fountain": PLACES["Fountain"][0]})
	mats["kerb"] = _shader_mat("masonry", {"color": Color(0.58, 0.58, 0.56), "joint_color": Color(0.3, 0.3, 0.29),
			"block_size": Vector2(1.1, 0.4), "joint_width": 0.006, "joint_depth": 0.004, "chamfer": 0.01,
			"face_rough": 0.002, "variation": 0.1, "grime": 0.3, "ground_dirt": 0.3, "seed": 7.0})
	mats["wood"] = _shader_mat("wood", {"color": Color(0.42, 0.27, 0.16), "board_width": 0.19})
	mats["wood_dark"] = _shader_mat("wood", {"color": Color(0.24, 0.15, 0.09), "bleach": 0.15, "seed": 2.0})
	mats["shutter_green"] = _shader_mat("wood", {"louvres": true, "paint": Color(0.2, 0.33, 0.24), "wear": 0.35})
	mats["shutter_brown"] = _shader_mat("wood", {"louvres": true, "paint": Color(0.4, 0.21, 0.12), "wear": 0.3, "seed": 5.0})
	mats["glass"] = _shader_mat("window_glass", {})
	mats["hay"] = _shader_mat("hay", {})
	mats["cloth"] = _shader_mat("banner_cloth", {"pattern": 1})
	var iron := StandardMaterial3D.new()
	iron.albedo_color = Color(0.07, 0.07, 0.075)
	iron.metallic = 0.75
	iron.roughness = 0.55
	mats["iron"] = iron
	var bronze := StandardMaterial3D.new()
	bronze.albedo_color = Color(0.55, 0.37, 0.18)
	bronze.metallic = 1.0
	bronze.roughness = 0.38
	mats["bronze"] = bronze
	for n in mats:
		_save(mats[n], "%s/materials/%s.tres" % [OUT, n])
	_photo_materials(plaster)
	# The photographic versions beside them (rooftops/materials_photo; their textures are not in the repository): the
	# scene puts them in place of the procedural ones when the textures are there (scripts/rooftops.gd).
	DirAccess.make_dir_recursive_absolute(OUT + "/materials_photo")
	for n in photo:
		var path := "%s/materials_photo/%s.tres" % [OUT, n]
		_save(photo[n], path)
		# Their textures by path only (each machine imports them with its own uids).
		var text := FileAccess.get_file_as_string(path)
		var rx := RegEx.create_from_string(' uid="uid://[a-z0-9]+"( path="res://rooftops/textures/)')
		var out := FileAccess.open(path, FileAccess.WRITE)
		out.store_string(rx.sub(text, "$1", true))
		out.close()


var _tex := {}
var photo := {} # the photographic materials by the name of the procedural one each replaces


## A photographic texture set (tools/fetch_textures.py) by its role: [colour, normal, ARM, its size in metres, its
## colour's mean linear luminance].
func _texset(p_role: String) -> Array:
	var info: Dictionary = _tex[p_role]
	var dir := OUT + "/textures/"
	var maps: Dictionary = info["maps"]
	var dims: Array = info["dimensions_mm"] if info["dimensions_mm"] else [2000, 2000]
	var lum := 0.25
	var img := Image.load_from_file(ProjectSettings.globalize_path(dir + maps["diff"]))
	if img:
		img.resize(16, 16, Image.INTERPOLATE_BILINEAR)
		var sum := 0.0
		for y in 16:
			for x in 16:
				var c := img.get_pixel(x, y).srgb_to_linear()
				sum += c.r * 0.3 + c.g * 0.55 + c.b * 0.15
		lum = sum / 256.0
	return [load(dir + maps["diff"]), load(dir + maps["nor_gl"]), load(dir + maps["arm"]), float(dims[0]) / 1000.0, lum]


func _photo(p_role: String, p_params: Dictionary) -> ShaderMaterial:
	var t := _texset(p_role)
	var params := {"albedo_tex": t[0], "normal_tex": t[1], "arm_tex": t[2], "tex_size": t[3], "tex_lum": t[4]}
	params.merge(p_params, true)
	return _shader_mat("facade_pbr", params)


## The buildings' and the ground's materials from Poly Haven's photographs (CC0) where they have been fetched
## (tools/fetch_textures.py, then imported by the editor), each by the name of the procedural one it replaces; the far
## city keeps its procedural ones.
func _photo_materials(p_plaster: Dictionary) -> void:
	var f := FileAccess.open(OUT + "/textures/textures.json", FileAccess.READ)
	if f == null or not ResourceLoader.exists(OUT + "/textures/" + "plastered_wall_diff_2k.jpg"):
		print("rooftops: no photographic textures (tools/fetch_textures.py): the procedural materials")
		return
	_tex = JSON.parse_string(f.get_as_text())
	var brick := _texset("brick")
	for n in p_plaster:
		photo[n] = _photo("plaster_smooth", {"tint": p_plaster[n][0], "tint_strength": 0.9, "brightness": 1.0, "seed": p_plaster[n][2],
				"fallen": p_plaster[n][1], "patch_albedo": brick[0], "patch_normal": brick[1], "patch_size": brick[3],
				"specular": 0.22, "roughness_bias": 0.08, "stains": 0.45, "variation": 0.12})
	photo["far_plaster"] = _photo("plaster_smooth", {"tint": Color(0.82, 0.7, 0.55), "tint_strength": 0.9, "brightness": 1.0,
			"fallen": 0.15, "patch_albedo": brick[0], "patch_normal": brick[1], "patch_size": brick[3], "specular": 0.22,
			"roughness_bias": 0.08, "stains": 0.5, "variation": 0.14, "instance_tint": true})
	# Laid textures (blocks, bricks, tiles, boards) are not turned for the anti-tiling: their courses must stay level.
	photo["stone"] = _photo("stone", {"tint": Color(0.8, 0.76, 0.66), "tint_strength": 0.45, "specular": 0.25, "stains": 0.3,
			"anti_tile": false, "seed": 1.0})
	photo["water_stone"] = _photo("stone", {"tint": Color(0.82, 0.8, 0.74), "tint_strength": 0.6, "specular": 0.25, "stains": 0.5,
			"anti_tile": false, "seed": 2.0})
	# The ground floors' rustication: the sandstone's blocks, larger and rougher, greyer than the dressings.
	photo["stone_rustic"] = _photo("stone", {"tint": Color(0.76, 0.71, 0.62), "tint_strength": 0.5, "specular": 0.22,
			"stains": 0.4, "ground_dirt": 0.9, "normal_strength": 2.2, "tex_size": 4.2, "variation": 0.16, "anti_tile": false,
			"seed": 3.0})
	photo["kerb"] = _photo("stone", {"tint": Color(0.62, 0.62, 0.6), "tint_strength": 0.7, "specular": 0.25, "anti_tile": false, "seed": 4.0})
	photo["brick"] = _photo("brick", {"tint": Color(0.56, 0.38, 0.3), "tint_strength": 0.5, "normal_strength": 0.75, "specular": 0.2,
			"stains": 0.3, "variation": 0.1, "anti_tile": false, "seed": 5.0})
	photo["roof"] = _photo("roof", {"specular": 0.22, "roughness_bias": 0.05, "stains": 0.25, "variation": 0.15, "ground_dirt": 0.0,
			"anti_tile": false, "seed": 6.0})
	photo["wood"] = _photo("wood", {"specular": 0.2, "stains": 0.0, "ground_dirt": 0.3, "anti_tile": false, "seed": 7.0})
	photo["wood_dark"] = _photo("wood_dark", {"specular": 0.2, "brightness": 0.6, "stains": 0.0, "ground_dirt": 0.3, "anti_tile": false,
			"seed": 8.0})
	photo["shutter_green"] = _photo("shutter_green", {"specular": 0.25, "stains": 0.0, "anti_tile": false, "seed": 9.0})
	photo["shutter_brown"] = _photo("shutter_painted", {"tint": Color(0.42, 0.22, 0.12), "tint_strength": 0.8, "specular": 0.25,
			"stains": 0.0, "anti_tile": false, "seed": 10.0})
	photo["ground_stone"] = _photo("square_slabs", {"specular": 0.25, "stains": 0.0, "ground_dirt": 0.0, "seed": 11.0})
	var walls := PackedVector4Array()
	for n in FOOTPRINTS:
		walls.append(_footprint(n))
	var sl := _texset("square_slabs")
	var fan := _texset("street_fan")
	var cob := _texset("cobbles")
	var pav := _texset("pavement")
	photo["paving"] = _shader_mat("paving_pbr", {"walls": walls, "wall_count": walls.size(), "pavement_width": PAVEMENT,
			"fountain": PLACES["Fountain"][0], "slabs_albedo": sl[0], "slabs_normal": sl[1], "slabs_arm": sl[2], "slabs_size": sl[3] * 2.2,
			"slabs_lum": sl[4], "slabs_tint": Color(0.62, 0.59, 0.54), "slabs_tint_strength": 0.8,
			"fan_albedo": fan[0], "fan_normal": fan[1], "fan_arm": fan[2], "fan_size": fan[3],
			"cob_albedo": cob[0], "cob_normal": cob[1], "cob_arm": cob[2], "cob_size": cob[3],
			"pave_albedo": pav[0], "pave_normal": pav[1], "pave_arm": pav[2], "pave_size": pav[3]})
	print("rooftops: photographic materials (Poly Haven, CC0)")


# ------------------------------------------------------------------ models

func _load_model(p_path: String) -> void:
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	var err := doc.append_from_file(p_path, state)
	if err != OK:
		push_error("could not read %s: %d" % [p_path, err])
		return
	var root: Node = doc.generate_scene(state)
	var name := p_path.get_file().get_basename()
	var info := {"empties": {}, "shapes": []}
	for n in _all(root):
		if n is MeshInstance3D:
			if String(n.name).contains("_col_"):
				# A convex piece of the collision (tools/blender/ac_common.py, Collider).
				var shape := (n as MeshInstance3D).mesh.create_convex_shape(true, false)
				info["shapes"].append([shape, (n as Node3D).transform])
				continue
			var mesh := _remap((n as MeshInstance3D).mesh, false, "far_" if name == "Skyline" else "")
			if name == "SkylineHouses":
				# Milestone 153: the city's houses' prototypes, kept by name (saved with their instances).
				mesh.resource_name = String(n.name)
				info.get_or_add("protos", {})[String(n.name)] = mesh
				continue
			mesh.resource_name = name
			info["mesh"] = _save(mesh, "%s/models/%s.res" % [OUT, name])
		elif n is Node3D and n != root and not (n is ImporterMeshInstance3D):
			info["empties"][String(n.name)] = (n as Node3D).transform
	models[name] = info
	root.free()


func _all(p_node: Node) -> Array:
	var out := [p_node]
	for c in p_node.get_children():
		out.append_array(_all(c))
	return out


## The model's mesh with the scene's materials (by the Blender material's name) and no unused surfaces.
func _remap(p_mesh: Mesh, p_collision: bool, p_prefix := "") -> ArrayMesh:
	var out := ArrayMesh.new()
	for i in p_mesh.get_surface_count():
		var arrays := p_mesh.surface_get_arrays(i)
		var mname := ""
		var m := p_mesh.surface_get_material(i)
		if m:
			mname = m.resource_name
		out.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		if not p_collision:
			if mats.has(p_prefix + mname):
				out.surface_set_material(out.get_surface_count() - 1, mats[p_prefix + mname])
			elif mats.has(mname):
				out.surface_set_material(out.get_surface_count() - 1, mats[mname])
			else:
				push_warning("no material for '%s'" % mname)
	return out


## A building's footprint on the ground (axis-aligned: the buildings stand square to the axes): centre x, centre z,
## half its size along x and z.
func _footprint(p_name: String) -> Vector4:
	var p: Array = PLACES[p_name]
	var size: Vector2 = FOOTPRINTS[p_name]
	var turned := absf(fmod(absf(float(p[1])), 180.0) - 90.0) < 1.0
	var half := Vector2(size.y, size.x) * 0.5 if turned else size * 0.5
	return Vector4(p[0].x, p[0].y, half.x, half.y)


func _pavements(p_root: Node3D) -> void:
	# The raised pavements along the fronts: a slab KERB high round each building, PAVEMENT wide, its top the ground's
	# shader (the pavement's flags there), its sides kerbstones; a box to walk on.
	var body := StaticBody3D.new()
	body.collision_layer = LAYER_WORLD
	body.collision_mask = 0
	_add(p_root, body, "Pavements")
	var top := SurfaceTool.new()
	top.begin(Mesh.PRIMITIVE_TRIANGLES)
	var side := SurfaceTool.new()
	side.begin(Mesh.PRIMITIVE_TRIANGLES)
	var k := 0
	for n in FOOTPRINTS:
		var f := _footprint(n)
		var hx := f.z + PAVEMENT
		var hz := f.w + PAVEMENT
		var c := Vector3(f.x, 0.0, f.y)
		var q := [c + Vector3(-hx, KERB, -hz), c + Vector3(hx, KERB, -hz), c + Vector3(hx, KERB, hz), c + Vector3(-hx, KERB, hz)]
		for v in [q[0], q[1], q[2], q[0], q[2], q[3]]:
			top.set_normal(Vector3.UP)
			top.set_uv(Vector2(v.x, v.z))
			top.add_vertex(v)
		for e in 4:
			var a: Vector3 = q[e]
			var b: Vector3 = q[(e + 1) % 4]
			var nrm := (b - a).cross(Vector3.UP).normalized() * -1.0
			var len := a.distance_to(b)
			var a0 := Vector3(a.x, 0.0, a.z)
			var b0 := Vector3(b.x, 0.0, b.z)
			for vv in [[a0, 0.0, 0.0], [b, len, KERB], [b0, len, 0.0], [a0, 0.0, 0.0], [a, 0.0, KERB], [b, len, KERB]]:
				side.set_normal(nrm)
				side.set_uv(Vector2(vv[1], 1.0 - vv[2]))
				side.add_vertex(vv[0])
		var cs := CollisionShape3D.new()
		var box := BoxShape3D.new()
		box.size = Vector3(hx * 2.0, KERB, hz * 2.0)
		cs.shape = box
		cs.position = c + Vector3(0.0, KERB * 0.5, 0.0)
		_add(body, cs, "Collision%d" % k)
		k += 1
	top.generate_tangents()
	var mesh := top.commit()
	mesh.surface_set_material(0, mats["paving"])
	side.generate_tangents()
	side.commit(mesh)
	mesh.surface_set_material(1, mats["kerb"])
	_save(mesh, OUT + "/models/pavements.res")
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	_add(body, mi, "Mesh")


func _place(p_name: String) -> Transform3D:
	var p: Array = PLACES[p_name]
	var pos: Vector2 = p[0]
	return Transform3D(Basis(Vector3.UP, deg_to_rad(p[1])), Vector3(pos.x, 0.0, pos.y))


func _model(p_parent: Node3D, p_name: String, p_xform: Transform3D, p_layer := LAYER_WORLD | LAYER_CLIMB) -> Node3D:
	var info: Dictionary = models[p_name]
	var node: Node3D
	if not info["shapes"].is_empty():
		var body := StaticBody3D.new()
		body.collision_layer = p_layer
		body.collision_mask = 0
		node = body
		node.transform = p_xform
		_add(p_parent, node, p_name)
		var k := 0
		for piece in info["shapes"]:
			var cs := CollisionShape3D.new()
			cs.shape = piece[0]
			cs.transform = piece[1]
			_add(node, cs, "Collision%d" % k)
			k += 1
	else:
		node = Node3D.new()
		node.transform = p_xform
		_add(p_parent, node, p_name)
	var mi := MeshInstance3D.new()
	mi.mesh = info["mesh"]
	_add(node, mi, "Mesh")
	return node


# ------------------------------------------------------------------ the scene

func _build_scene() -> void:
	var root := Node3D.new()
	root.name = "Rooftops"
	# Milestone 154: physics interpolation (project.godot) for the player alone — the body moved at the physics ticks,
	# drawn between them, so that the camera following it at the display's rate does not shake; everything else (the
	# crowd, the birds, the camera) moves in the idle frames and is drawn where it is.
	root.physics_interpolation_mode = Node.PHYSICS_INTERPOLATION_MODE_OFF
	root.set_script(load("res://scripts/rooftops.gd"))
	scene_root = root
	_environment(root)
	_ground(root)
	_pavements(root)
	var town := Node3D.new()
	_add(root, town, "Town")
	for n in BUILDINGS:
		if models.has(n):
			_model(town, n, _place(n))
	for i in NEAR.size():
		var near: Array = NEAR[i]
		if models.has(near[0]):
			var p: Vector2 = near[1]
			_model(town, near[0], Transform3D(Basis(Vector3.UP, deg_to_rad(near[2])), Vector3(p.x, 0.0, p.y))).name = "Near%d%s" % [i, near[0]]
	for n in ["Fountain", "StallA", "StallB"]:
		if models.has(n):
			_model(town, n, _place(n), LAYER_WORLD)
	if models.has("Skyline"):
		var sky_node := _model(root, "Skyline", _place("Skyline"))
		(sky_node.get_node("Mesh") as MeshInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_skyline_houses(root)
	_landscape(root)
	_hay_cart(town)
	_props(town)
	_fountain_water(town)
	var wind: Node = ClassDB.instantiate("FellowWind3D")
	wind.set("speed", 3.5)
	wind.set("direction", Vector3(0.8, 0.0, 0.6).normalized())
	_add(root, wind, "Wind")
	_banners(town)
	_awnings(town)
	_dust(town)
	var birds := Node3D.new()
	birds.set_script(load("res://scripts/rooftops_birds.gd"))
	_add(root, birds, "Birds")
	_laundry(town)
	_lamps(town)
	var cam := Camera3D.new()
	cam.fov = 60.0
	cam.far = 1500.0
	cam.current = false
	cam.transform = Transform3D(Basis(), Vector3(-6.0, 1.7, 9.0)).looking_at(Vector3(4.0, 9.0, -14.0))
	_add(root, cam, "ViewCamera")
	if models.has("Campanile"):
		var vp := Marker3D.new()
		vp.transform = _viewpoint_xform()
		_add(root, vp, "Viewpoint")
		# The foot of the climb up the bell tower's front (its holds), facing the wall: the demo's run ends here.
		var foot := Marker3D.new()
		var e: Transform3D = _place("Campanile") * (models["Campanile"]["empties"].get("Campanile_climb", Transform3D()) as Transform3D)
		foot.transform = Transform3D(Basis.looking_at(-(_place("Campanile").basis * Vector3(0.0, 0.0, 1.0)), Vector3.UP), Vector3(e.origin.x, 0.0, e.origin.z))
		_add(root, foot, "ClimbStart")
	if models.has("Mannequin"):
		var player: Node3D = _build_player().instantiate()
		player.position = Vector3(-6.0, 0.05, 8.0)
		player.set("viewpoint_path", NodePath("../Viewpoint"))
		_add(root, player, "Player")
		_crowd(root)
		var rig := Node3D.new()
		rig.set_script(load("res://scripts/rooftops_camera.gd"))
		_add(root, rig, "CameraRig")
		var pc := Camera3D.new()
		pc.fov = 62.0
		pc.near = 0.08
		pc.far = 1500.0
		pc.current = true
		_add(rig, pc, "Camera3D")
		var demo := Node.new()
		demo.set_script(load("res://scripts/rooftops_demo.gd"))
		_add(root, demo, "Demo")
	var warmer: Node = ClassDB.instantiate("FellowPipelineWarmer3D")
	_add(root, warmer, "PipelineWarmer")
	var ps := PackedScene.new()
	var err := ps.pack(root)
	if err != OK:
		push_error("could not pack the rooftops scene: %d" % err)
	ResourceSaver.save(ps, OUT + "/rooftops.tscn")
	root.free()


func _environment(p_root: Node3D) -> void:
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
	env.ambient_light_sky_contribution = 1.0
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.tonemap_exposure = 1.0
	env.ssao_enabled = true
	env.ssao_radius = 1.0
	env.ssao_intensity = 1.8
	env.ssao_detail = 0.6
	# No screen-space reflections: on rough stone and old glass they read as a sheen on everything; the puddles and
	# windows reflect the sky's radiance.
	env.ssr_enabled = false
	env.ssil_enabled = true
	env.ssil_radius = 4.0
	env.ssil_intensity = 1.2
	# The light bounced off the sunlit fronts into the shade (the square's other side, the streets): SDFGI.
	env.sdfgi_enabled = true
	env.sdfgi_use_occlusion = true
	env.sdfgi_cascades = 4
	env.sdfgi_min_cell_size = 0.25
	env.sdfgi_bounce_feedback = 0.6
	env.sdfgi_energy = 1.0
	env.fog_enabled = true
	# Milestone 153: the golden hour's haze — the city's far houses and the hills paler and bluer with distance (the
	# sky's colour mixed in), as the eye reads depth.
	env.fog_density = 0.0008
	env.fog_aerial_perspective = 0.8
	env.fog_light_color = Color(0.85, 0.72, 0.58)
	env.fog_sky_affect = 0.3
	env.volumetric_fog_enabled = true
	env.volumetric_fog_density = 0.0016
	env.volumetric_fog_albedo = Color(0.92, 0.93, 0.96)
	env.volumetric_fog_anisotropy = 0.7
	env.volumetric_fog_length = 64.0
	env.volumetric_fog_sky_affect = 0.0
	env.adjustment_enabled = true
	env.adjustment_saturation = 1.1
	env.adjustment_contrast = 1.05
	var we := WorldEnvironment.new()
	we.environment = env
	we.camera_attributes = CameraAttributesPractical.new()
	_add(p_root, we, "WorldEnvironment")
	# The sun low in the west-south-west: the golden hour, long shadows across the square.
	var sun := DirectionalLight3D.new()
	sun.light_energy = 2.8
	sun.light_color = Color(1.0, 0.9, 0.78) # the atmosphere colours it further (rendering/fellow/post/atmosphere/sun_transmittance)
	sun.light_angular_distance = 0.6
	sun.shadow_enabled = true
	sun.shadow_blur = 1.2
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 140.0
	sun.directional_shadow_blend_splits = true
	sun.light_volumetric_fog_energy = 1.2
	# From the south-west, over the ring of houses on the west (lower, its shadow would fill the square): the square's
	# north and east fronts lit, the bell tower's face with its holds.
	sun.rotation_degrees = Vector3(-24.0, -52.0, 0.0)
	_add(p_root, sun, "Sun")


func _ground(p_root: Node3D) -> void:
	# The city's paving (the square's, the streets'): a disc to the walls (215 m), UVs in metres; a plane to stand on.
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var seg := 64
	var radii := [0.0, 20.0, 45.0, 80.0, 130.0, 180.0, 216.0]
	for k in radii.size() - 1:
		for i in seg:
			var a0 := TAU * i / seg
			var a1 := TAU * (i + 1) / seg
			var r0: float = radii[k]
			var r1: float = radii[k + 1]
			var q := [Vector3(cos(a0) * r0, 0, sin(a0) * r0), Vector3(cos(a1) * r0, 0, sin(a1) * r0),
					Vector3(cos(a1) * r1, 0, sin(a1) * r1), Vector3(cos(a0) * r1, 0, sin(a0) * r1)]
			for v in [q[0], q[2], q[1], q[0], q[3], q[2]]:
				st.set_normal(Vector3.UP)
				st.set_uv(Vector2(v.x, v.z))
				st.add_vertex(v)
	st.generate_tangents()
	var mesh := st.commit()
	mesh.surface_set_material(0, mats["paving"])
	_save(mesh, OUT + "/models/ground.res")
	var body := StaticBody3D.new()
	body.collision_layer = LAYER_WORLD
	body.collision_mask = 0
	_add(p_root, body, "Ground")
	var cs := CollisionShape3D.new()
	cs.shape = WorldBoundaryShape3D.new()
	_add(body, cs, "Collision")
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	_add(body, mi, "Mesh")


# ------------------------------------------------------------------ the country round the city

var _tn := FastNoiseLite.new()
var _tn2 := FastNoiseLite.new()
const LAND_SEG := 256
const LAND_RINGS := 76


## The country's height at (x, z): flat by the walls, rolling past them, hills rising from 300 m to 200 m high.
func _terrain_h(x: float, z: float) -> float:
	var r := sqrt(x * x + z * z)
	var t := smoothstep(260.0, 1000.0, r)
	var base := _tn.get_noise_2d(x, z) * 0.5 + 0.5
	var hills := pow(base, 1.35) * 210.0 * t + 30.0 * t
	var roll := _tn2.get_noise_2d(x, z) * 7.0 * smoothstep(215.0, 340.0, r)
	return hills + roll - 0.3 * (1.0 - smoothstep(212.0, 228.0, r))


## The terrain mesh's own height at (x, z) (its rings and segments interpolated as its triangles do): what the trees and
## the farms stand on.
func _mesh_h(x: float, z: float) -> float:
	var r := sqrt(x * x + z * z)
	var kf := float(LAND_RINGS) * pow(clampf((r - 206.0) / (2600.0 - 206.0), 0.0, 1.0), 1.0 / 1.9)
	var k0 := mini(int(floor(kf)), LAND_RINGS - 1)
	var fk := kf - k0
	var af := fposmod(atan2(z, x), TAU) / TAU * LAND_SEG
	var i0 := int(floor(af)) % LAND_SEG
	var fi: float = af - floor(af)
	var hs := []
	for kk in [k0, k0 + 1]:
		var rr: float = 206.0 + (2600.0 - 206.0) * pow(float(kk) / LAND_RINGS, 1.9)
		for ii in [i0, (i0 + 1) % LAND_SEG]:
			var a: float = TAU * ii / LAND_SEG
			hs.append(_terrain_h(cos(a) * rr, sin(a) * rr))
	return lerpf(lerpf(hs[0], hs[1], fi), lerpf(hs[2], hs[3], fi), fk)


func _slope(x: float, z: float) -> float:
	var e := 4.0
	var dx := (_terrain_h(x + e, z) - _terrain_h(x - e, z)) / (2.0 * e)
	var dz := (_terrain_h(x, z + e) - _terrain_h(x, z - e)) / (2.0 * e)
	return sqrt(dx * dx + dz * dz)


func _landscape(p_root: Node3D) -> void:
	_tn.seed = 77
	_tn.frequency = 0.0015
	_tn.fractal_octaves = 5
	_tn2.seed = 78
	_tn2.frequency = 0.007
	_tn2.fractal_octaves = 3
	# The terrain: rings from the walls out to 2.6 km, closer together near the city.
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var seg := LAND_SEG
	var rings := LAND_RINGS
	for k in rings + 1:
		var r := 206.0 + (2600.0 - 206.0) * pow(float(k) / rings, 1.9)
		for i in seg:
			var a := TAU * i / seg
			var x := cos(a) * r
			var z := sin(a) * r
			st.set_uv(Vector2(x, z))
			st.add_vertex(Vector3(x, _terrain_h(x, z), z))
	for k in rings:
		for i in seg:
			var j := (i + 1) % seg
			var a0 := k * seg + i
			var a1 := k * seg + j
			var b0 := (k + 1) * seg + i
			var b1 := (k + 1) * seg + j
			for idx in [a0, b1, a1, a0, b0, b1]:
				st.add_index(idx)
	st.generate_normals()
	var mesh := st.commit()
	var m := ShaderMaterial.new()
	m.shader = load(OUT + "/shaders/landscape.gdshader")
	_save(m, OUT + "/materials/landscape.tres")
	mesh.surface_set_material(0, m)
	_save(mesh, OUT + "/models/landscape.res")
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_add(p_root, mi, "Landscape")
	_trees(p_root)
	_villas(p_root)


func _tree_mesh(p_kind: String) -> ArrayMesh:
	# Low trees for far off: a cypress's spindle, an olive's grey crown, a stone pine's umbrella, a broadleaf's blob;
	# vertex colours shaded darker inside and low (the instance's colour multiplies them).
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var trunk := Color(0.25, 0.18, 0.12)
	var prof := []
	var green := Color(0.12, 0.2, 0.1)
	var trunk_h := 0.0
	match p_kind:
		"cypress":
			prof = [[0.0, 0.3], [0.55, 0.3], [0.85, 2.0], [0.9, 4.5], [0.75, 7.0], [0.45, 9.0], [0.15, 10.6], [0.0, 11.2]]
			green = Color(0.1, 0.17, 0.08)
			trunk_h = 0.5
		"olive":
			prof = [[0.0, 1.0], [1.4, 1.2], [2.0, 2.0], [1.9, 2.9], [1.3, 3.6], [0.0, 3.9]]
			green = Color(0.36, 0.41, 0.28)
			trunk_h = 1.3
		"pine":
			prof = [[0.0, 7.5], [3.6, 7.8], [4.4, 8.6], [3.6, 9.6], [0.0, 10.0]]
			green = Color(0.16, 0.24, 0.11)
			trunk_h = 8.0
		_:
			prof = [[0.0, 1.5], [2.6, 2.4], [3.2, 4.5], [2.6, 6.6], [0.0, 7.6]]
			green = Color(0.15, 0.23, 0.1)
			trunk_h = 2.0
	var n := 9
	var rng := RandomNumberGenerator.new()
	rng.seed = hash(p_kind)
	var jit := []
	for k in prof.size():
		var row := []
		for i in n:
			row.append(1.0 + rng.randf_range(-0.18, 0.18) if k > 0 and k < prof.size() - 1 else 1.0)
		jit.append(row)
	for k in prof.size() - 1:
		for i in n:
			var j := (i + 1) % n
			var a0 := TAU * i / n
			var a1 := TAU * j / n
			var p := []
			for c in [[k, a0, i], [k, a1, j], [k + 1, a1, j], [k + 1, a0, i]]:
				var r: float = prof[c[0]][0] * jit[c[0]][c[2]]
				p.append(Vector3(cos(c[1]) * r, prof[c[0]][1], sin(c[1]) * r))
			for v in [p[0], p[2], p[1], p[0], p[3], p[2]]:
				var shade := 0.65 + 0.35 * clampf(v.y / prof[prof.size() - 1][1], 0.0, 1.0)
				st.set_color(Color(green.r * shade, green.g * shade, green.b * shade))
				st.add_vertex(v)
	# The trunk.
	for i in 5:
		var a0 := TAU * i / 5
		var a1 := TAU * (i + 1) / 5
		var r := 0.12 if p_kind != "pine" else 0.22
		var q := [Vector3(cos(a0) * r, 0, sin(a0) * r), Vector3(cos(a1) * r, 0, sin(a1) * r), Vector3(cos(a1) * r, trunk_h, sin(a1) * r), Vector3(cos(a0) * r, trunk_h, sin(a0) * r)]
		for v in [q[0], q[2], q[1], q[0], q[3], q[2]]:
			st.set_color(trunk)
			st.add_vertex(v)
	st.generate_normals()
	var mesh := st.commit()
	var mat := StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.roughness = 0.95
	mat.backlight_enabled = true
	mat.backlight = Color(0.12, 0.16, 0.06)
	mesh.surface_set_material(0, mat)
	return mesh


func _trees(p_root: Node3D) -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 2024
	var sets := {"cypress": [], "olive": [], "pine": [], "broadleaf": []}
	# Olive groves: patches of rows on gentle slopes.
	for g in 46:
		var a := rng.randf() * TAU
		var r := rng.randf_range(250.0, 1400.0)
		var c := Vector2(cos(a), sin(a)) * r
		var turn := rng.randf() * PI
		var w := rng.randf_range(40.0, 90.0)
		var d := rng.randf_range(30.0, 60.0)
		var u := -w * 0.5
		while u < w * 0.5:
			var v := -d * 0.5
			while v < d * 0.5:
				var off := Vector2(u, v).rotated(turn) + Vector2(rng.randf_range(-0.8, 0.8), rng.randf_range(-0.8, 0.8))
				var pt := c + off
				if _slope(pt.x, pt.y) < 0.25 and pt.length() > 225.0:
					sets["olive"].append([pt, rng.randf_range(0.75, 1.15)])
				v += 7.0
			u += 7.0
	# Cypress avenues along the roads out of the city and to the farms, and singles on the hills.
	for l in 30:
		var a := rng.randf() * TAU
		var r := rng.randf_range(235.0, 1100.0)
		var start := Vector2(cos(a), sin(a)) * r
		var dir := Vector2(cos(a), sin(a)).rotated(rng.randf_range(-1.2, 1.2))
		var length := rng.randf_range(80.0, 260.0)
		var t := 0.0
		while t < length:
			for side in [-4.0, 4.0]:
				var pt: Vector2 = start + dir * t + dir.orthogonal() * side
				if pt.length() > 222.0:
					sets["cypress"].append([pt, rng.randf_range(0.8, 1.25)])
			t += rng.randf_range(6.0, 8.0)
	for i in 500:
		var a := rng.randf() * TAU
		var pt := Vector2(cos(a), sin(a)) * rng.randf_range(225.0, 1800.0)
		sets["cypress"].append([pt, rng.randf_range(0.7, 1.3)])
	# Stone pines in clumps by the walls and on the hills.
	for g in 34:
		var a := rng.randf() * TAU
		var c := Vector2(cos(a), sin(a)) * rng.randf_range(225.0, 900.0)
		for i in rng.randi_range(3, 7):
			sets["pine"].append([c + Vector2(rng.randf_range(-14, 14), rng.randf_range(-14, 14)), rng.randf_range(0.8, 1.2)])
	# Woods on the steep slopes.
	for i in 9000:
		var a := rng.randf() * TAU
		var pt := Vector2(cos(a), sin(a)) * rng.randf_range(300.0, 2400.0)
		if _slope(pt.x, pt.y) > 0.22 + rng.randf() * 0.1:
			sets["broadleaf"].append([pt, rng.randf_range(0.8, 1.4)])
	var tint := {"cypress": [0.85, 1.15], "olive": [0.85, 1.12], "pine": [0.85, 1.1], "broadleaf": [0.75, 1.25]}
	for kind in sets:
		var list: Array = sets[kind]
		var xforms := []
		var cols := []
		for i in list.size():
			var pt: Vector2 = list[i][0]
			var sc: float = list[i][1]
			var y := _mesh_h(pt.x, pt.y)
			var b := Basis(Vector3.UP, rng.randf() * TAU).scaled(Vector3(sc, sc * rng.randf_range(0.9, 1.15), sc))
			xforms.append(Transform3D(b, Vector3(pt.x, y - 0.3, pt.y)))
			var tt := rng.randf_range(tint[kind][0], tint[kind][1])
			cols.append(Color(tt, tt * rng.randf_range(0.95, 1.05), tt))
		_instances(p_root, "Trees_" + kind, _tree_mesh(kind), xforms, cols)
		print("trees %s: %d" % [kind, list.size()])


## Milestone 153: the city's houses beyond the square — prototypes made in Blender (SkylineHouses.glb: windows in
## their openings, shutters, sills, string courses, cornices, eaves on rafters, tiled roofs, terraces, chimneys; a near
## set and a plainer far one) placed in the blocks (SkylineHouses.json), a MultiMesh for each prototype, each house's
## plaster its own colour; the near ones (within NEAR_CITY of the square) cast shadows.
const NEAR_CITY := 95.0

func _skyline_houses(p_root: Node3D) -> void:
	if not models.has("SkylineHouses") or not FileAccess.file_exists(models_dir.path_join("SkylineHouses.json")):
		return
	var places: Array = JSON.parse_string(FileAccess.get_file_as_string(models_dir.path_join("SkylineHouses.json")))
	var protos: Dictionary = models["SkylineHouses"].get("protos", {})
	var groups := {}
	for p in places:
		# Blender's (x, y) on the ground is (x, -y) here; its turn about Z, this one about Y.
		var pos := Vector3(float(p[1]), 0.0, -float(p[2]))
		var near := Vector2(pos.x, pos.z).length() < NEAR_CITY
		var key := ("HN%02d" if near else "HF%02d") % int(p[0])
		if not protos.has(key):
			continue
		var b := Basis(Vector3.UP, float(p[3])) * Basis.from_scale(Vector3(float(p[4]), 1.0, 1.0))
		var c: Array = p[5]
		var g: Array = groups.get_or_add(key, [[], []])
		g[0].append(Transform3D(b, pos))
		g[1].append(Color(float(c[0]), float(c[1]), float(c[2]), float(p[6])))
	var city := Node3D.new()
	_add(p_root, city, "CityHouses")
	DirAccess.make_dir_recursive_absolute(OUT + "/models/houses")
	var keys := groups.keys()
	keys.sort()
	var tris := 0
	for key in keys:
		var g: Array = groups[key]
		var mmi := _instances(city, key, protos[key], g[0], g[1], "houses/")
		if String(key).begins_with("HN"):
			mmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
		var t := 0
		for i in (protos[key] as Mesh).get_surface_count():
			t += (protos[key] as Mesh).surface_get_array_index_len(i) / 3
		tris += t * g[0].size()
	print("city houses: %d in %d prototypes, %d triangles" % [places.size(), keys.size(), tris])


## Instances of a mesh, made into a MultiMesh when the scene starts (scripts/rooftops_multimesh.gd).
func _instances(p_parent: Node3D, p_name: String, p_mesh: Mesh, p_xforms: Array, p_cols: Array, p_dir := "") -> MultiMeshInstance3D:
	var data := PackedFloat32Array()
	data.resize(p_xforms.size() * 16)
	for i in p_xforms.size():
		var t: Transform3D = p_xforms[i]
		var c: Color = p_cols[i]
		var row := [t.basis.x.x, t.basis.y.x, t.basis.z.x, t.origin.x, t.basis.x.y, t.basis.y.y, t.basis.z.y, t.origin.y,
				t.basis.x.z, t.basis.y.z, t.basis.z.z, t.origin.z, c.r, c.g, c.b, c.a]
		for k in 16:
			data[i * 16 + k] = row[k]
	_save(p_mesh, "%s/models/%s%s_mesh.res" % [OUT, p_dir, p_name.to_lower()])
	var inst: Resource = load("res://scripts/rooftops_instances.gd").new()
	inst.set("count", p_xforms.size())
	inst.set("data", data)
	_save(inst, "%s/models/%s%s_instances.res" % [OUT, p_dir, p_name.to_lower()])
	var mmi := MultiMeshInstance3D.new()
	mmi.set_script(load("res://scripts/rooftops_multimesh.gd"))
	mmi.set("mesh", p_mesh)
	mmi.set("instances", inst)
	mmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_add(p_parent, mmi, p_name)
	return mmi


func _villas(p_root: Node3D) -> void:
	# Farmhouses and villas on the hills: a block, sometimes a tower or a wing, a hip roof; cypresses round them.
	var rng := RandomNumberGenerator.new()
	rng.seed = 99
	var walls := {}
	var roofs := SurfaceTool.new()
	roofs.begin(Mesh.PRIMITIVE_TRIANGLES)
	var plasters := ["plaster_ochre", "plaster_rose", "plaster_cream", "plaster_sienna", "plaster_white"]
	for p in plasters:
		walls[p] = SurfaceTool.new()
		walls[p].begin(Mesh.PRIMITIVE_TRIANGLES)
	var cyp := []
	for i in 90:
		var a := rng.randf() * TAU
		var c := Vector2(cos(a), sin(a)) * rng.randf_range(250.0, 1700.0)
		if _slope(c.x, c.y) > 0.2:
			continue
		var y := _mesh_h(c.x, c.y)
		var turn := rng.randf() * PI
		var pl: String = plasters[rng.randi() % plasters.size()]
		var parts := [[Vector2(), Vector2(rng.randf_range(10, 16), rng.randf_range(8, 11)), rng.randf_range(6.5, 9.5)]]
		if rng.randf() < 0.4:
			parts.append([Vector2(rng.randf_range(-3, 3), rng.randf_range(-2, 2)), Vector2(4.5, 4.5), parts[0][2] + rng.randf_range(4, 7)])
		if rng.randf() < 0.4:
			parts.append([Vector2(parts[0][1].x * 0.5 + 3.5, 0), Vector2(7, 7), parts[0][2] - 2.5])
		for part in parts:
			var ctr: Vector2 = c + (part[0] as Vector2).rotated(turn)
			_house_block(walls[pl], roofs, Vector3(ctr.x, y - 1.0, ctr.y), part[1], part[2] + 1.0, turn)
		for k in rng.randi_range(4, 9):
			var off := Vector2(rng.randf_range(10, 22) * (1.0 if rng.randf() < 0.5 else -1.0), rng.randf_range(-12, 12)).rotated(turn)
			cyp.append(c + off)
	var mesh := ArrayMesh.new()
	for p in plasters:
		(walls[p] as SurfaceTool).generate_normals()
		(walls[p] as SurfaceTool).commit(mesh)
		mesh.surface_set_material(mesh.get_surface_count() - 1, mats["far_" + p])
	roofs.generate_normals()
	roofs.commit(mesh)
	mesh.surface_set_material(mesh.get_surface_count() - 1, mats["roof"])
	_save(mesh, OUT + "/models/villas.res")
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_add(p_root, mi, "Villas")
	var xforms := []
	var cols := []
	for i in cyp.size():
		var pt: Vector2 = cyp[i]
		var sc := rng.randf_range(0.8, 1.3)
		xforms.append(Transform3D(Basis().scaled(Vector3(sc, sc, sc)), Vector3(pt.x, _mesh_h(pt.x, pt.y) - 0.3, pt.y)))
		cols.append(Color(1, 1, 1))
	_instances(p_root, "Trees_villa_cypress", _tree_mesh("cypress"), xforms, cols)


## A block (its walls UV'd in metres along them and up them, for the far plaster's windows) and its hip roof.
func _house_block(p_walls: SurfaceTool, p_roofs: SurfaceTool, p_base: Vector3, p_size: Vector2, p_h: float, p_turn: float) -> void:
	var b := Basis(Vector3.UP, p_turn)
	var hx := p_size.x * 0.5
	var hz := p_size.y * 0.5
	var c := [Vector3(-hx, 0, -hz), Vector3(hx, 0, -hz), Vector3(hx, 0, hz), Vector3(-hx, 0, hz)]
	for e in 4:
		var a: Vector3 = c[e]
		var d: Vector3 = c[(e + 1) % 4]
		var len := a.distance_to(d)
		var q := [[a, 0.0, 0.0], [d, len, 0.0], [d + Vector3(0, p_h, 0), len, p_h], [a + Vector3(0, p_h, 0), 0.0, p_h]]
		for vi in [0, 1, 2, 0, 2, 3]:
			var v: Array = q[vi]
			p_walls.set_uv(Vector2(v[1], 1.0 - v[2]))
			p_walls.add_vertex(p_base + b * (v[0] as Vector3))
	var ex := hx + 0.6
	var ez := hz + 0.6
	var zr := minf(ex, ez) * 0.42
	var top := Vector3(0, p_h, 0)
	var r0: Vector3
	var r1: Vector3
	var faces := []
	if ex >= ez:
		r0 = top + Vector3(-(ex - ez), zr, 0)
		r1 = top + Vector3(ex - ez, zr, 0)
		var k := [top + Vector3(-ex, 0, -ez), top + Vector3(ex, 0, -ez), top + Vector3(ex, 0, ez), top + Vector3(-ex, 0, ez)]
		faces = [[k[0], r0, r1, k[1]], [k[1], r1, k[2]], [k[2], r1, r0, k[3]], [k[3], r0, k[0]]]
	else:
		r0 = top + Vector3(0, zr, -(ez - ex))
		r1 = top + Vector3(0, zr, ez - ex)
		var k := [top + Vector3(-ex, 0, -ez), top + Vector3(ex, 0, -ez), top + Vector3(ex, 0, ez), top + Vector3(-ex, 0, ez)]
		faces = [[k[0], r0, k[1]], [k[1], r0, r1, k[2]], [k[2], r1, k[3]], [k[3], r1, r0, k[0]]]
	for f in faces:
		var tri := [[0, 1, 2]] if f.size() == 3 else [[0, 1, 2], [0, 2, 3]]
		var n := ((f[1] as Vector3) - (f[0] as Vector3)).cross((f[2] as Vector3) - (f[0] as Vector3)).normalized()
		var up_s := (Vector3.UP - n * n.y).normalized()
		var along := up_s.cross(n).normalized()
		for t in tri:
			for vi in t:
				var v: Vector3 = f[vi]
				p_roofs.set_uv(Vector2(v.dot(along), -v.dot(up_s)))
				p_roofs.add_vertex(p_base + b * v)


func _hay_cart(p_parent: Node3D) -> void:
	if not models.has("HayCart") or not models.has("Campanile"):
		return
	# Under the bell tower's beam: the leap's landing.
	var beam := _viewpoint_xform()
	var out := -beam.basis.z
	# Out from the beam and to the side away from the climb up the tower's front (under its other arch).
	var at := beam.origin + out * 3.4 + beam.basis.x * 2.4
	var x := Transform3D(Basis(Vector3.UP, deg_to_rad(84.0)), Vector3(at.x, 0.0, at.z))
	var cart := _model(p_parent, "HayCart", x, LAYER_WORLD | LAYER_HAY)
	cart.add_to_group("hay", true)


## The bell tower's beam's end: its -Z out along the beam, over the square.
func _viewpoint_xform() -> Transform3D:
	var place := _place("Campanile")
	var e: Transform3D = place * (models["Campanile"]["empties"].get("Campanile_viewpoint", Transform3D()) as Transform3D)
	var out := place.basis * Vector3(0.0, 0.0, 1.0)
	return Transform3D(Basis.looking_at(out, Vector3.UP), e.origin)


func _props(p_parent: Node3D) -> void:
	# Stone posts at the pavements' corners and across the street mouths; barrels and
	# crates by the stalls and the doors; grates round the fountain.
	var k := 0
	var posts := []
	if models.has("Bollard"):
		for n in FOOTPRINTS:
			var f := _footprint(n)
			for sx in [-1.0, 1.0]:
				for sz in [-1.0, 1.0]:
					posts.append(Vector3(f.x + sx * (f.z + PAVEMENT + 0.3), 0.0, f.y + sz * (f.w + PAVEMENT + 0.3)))
		for x in [-3.2, -0.3]:
			posts.append(Vector3(x, 0.0, 13.3))
		# Across the west street's mouth.
		for z in [-4.5, -1.0, 2.5, 5.0]:
			posts.append(Vector3(-18.0, 0.0, z))
		for p in posts:
			# Not in the way of the climb up the tower's front, nor in the square's middle.
			if (p as Vector3).distance_to(Vector3(1.26, 0.0, -11.0)) < 2.5:
				continue
			_model(p_parent, "Bollard", Transform3D(Basis(Vector3.UP, randf() * TAU), p), LAYER_WORLD).name = "Bollard%d" % k
			k += 1
	var goods := [["Barrel", Vector3(-13.4, 0, 1.6)], ["Barrel", Vector3(-12.8, 0, 1.2)], ["Crate", Vector3(-13.6, 0, 6.6)],
			["Crate", Vector3(-13.5, 0.5, 6.5)], ["Barrel", Vector3(12.6, 0, 9.6)], ["Crate", Vector3(9.7, 0, 9.8)],
			["Barrel", Vector3(-6.0, 0.08, -12.2)], ["Crate", Vector3(15.1, 0.08, -12.2)], ["Barrel", Vector3(15.9, 0.08, 2.9)]]
	for g in goods:
		if models.has(g[0]):
			_model(p_parent, g[0], Transform3D(Basis(Vector3.UP, randf() * TAU), g[1]), LAYER_WORLD).name = "%s%d" % [g[0], k]
			k += 1
	if models.has("Drain"):
		var c: Vector2 = PLACES["Fountain"][0]
		for i in 4:
			var a := PI * 0.25 + PI * 0.5 * i
			var at := Vector3(c.x + cos(a) * 8.6, 0.002, c.y + sin(a) * 8.6)
			_model(p_parent, "Drain", Transform3D(Basis(Vector3.UP, a), at), LAYER_WORLD).name = "Drain%d" % i


func _fountain_water(p_parent: Node3D) -> void:
	if not models.has("Fountain"):
		return
	var m := ShaderMaterial.new()
	var sh := Shader.new()
	sh.code = """shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;
// The fountain's water: dark and clear, rings spreading from where the jet falls.
#include "res://rooftops/shaders/rooftops_common.gdshaderinc"
varying vec3 world_pos;
void vertex() { world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz; }
void fragment() {
	vec2 p = world_pos.xz;
	vec2 c = (MODEL_MATRIX * vec4(0.0, 0.0, 0.0, 1.0)).xz;
	float d = length(p - c);
	float h = 0.004 * sin(d * 18.0 - TIME * 5.0) * exp(-d * 0.5);
	h += 0.0015 * rt_noise(p * 6.0 + TIME * 0.6) + 0.001 * rt_noise(p * 14.0 - TIME * 0.9);
	ALBEDO = vec3(0.03, 0.05, 0.05);
	ROUGHNESS = 0.03;
	SPECULAR = 0.6;
	NORMAL = rt_bump(NORMAL, VERTEX, h, 1.0);
}
"""
	sh.resource_path = ""
	m.shader = sh
	var plane := CylinderMesh.new()
	plane.top_radius = 2.5
	plane.bottom_radius = 2.5
	plane.height = 0.02
	plane.radial_segments = 32
	plane.rings = 1
	plane.material = m
	var mi := MeshInstance3D.new()
	mi.mesh = plane
	mi.transform = _place("Fountain").translated_local(Vector3(0.0, 0.55, 0.0))
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_add(p_parent, mi, "FountainWater")
	# The jet: drops thrown up from the bowl's top, falling back into the basin.
	var drops := GPUParticles3D.new()
	drops.amount = 600
	drops.lifetime = 1.4
	drops.fixed_fps = 0
	drops.visibility_aabb = AABB(Vector3(-3, -1, -3), Vector3(6, 5, 6))
	var pm := ParticleProcessMaterial.new()
	pm.direction = Vector3(0, 1, 0)
	pm.spread = 14.0
	pm.initial_velocity_min = 2.6
	pm.initial_velocity_max = 3.2
	pm.gravity = Vector3(0, -9.8, 0)
	pm.scale_min = 0.6
	pm.scale_max = 1.3
	pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	pm.emission_sphere_radius = 0.05
	pm.collision_mode = ParticleProcessMaterial.COLLISION_DISABLED
	drops.process_material = pm
	var quad := QuadMesh.new()
	quad.size = Vector2(0.03, 0.09)
	var dm := StandardMaterial3D.new()
	dm.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	dm.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	dm.albedo_color = Color(0.85, 0.92, 1.0, 0.45)
	dm.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	dm.billboard_keep_scale = true
	quad.material = dm
	drops.draw_pass_1 = quad
	drops.transform = _place("Fountain").translated_local(Vector3(0.0, 2.72, 0.0))
	_add(p_parent, drops, "FountainJet")


func _cloth(p_parent: Node3D, p_name: String, p_xform: Transform3D, p_size: Vector2, p_pin: int, p_mat: Material, p_res := 18) -> Node3D:
	var c: Node3D = ClassDB.instantiate("FellowCloth3D")
	c.set("size", p_size)
	c.set("resolution", p_res)
	c.set("pin_mode", p_pin)
	c.set("material", p_mat)
	c.set("collide_with_world", false)
	c.transform = p_xform
	_add(p_parent, c, p_name)
	return c


func _banners(p_parent: Node3D) -> void:
	var colours := [[Color(0.58, 0.07, 0.06), Color(0.86, 0.66, 0.24)], [Color(0.1, 0.16, 0.38), Color(0.86, 0.72, 0.3)], [Color(0.12, 0.3, 0.16), Color(0.9, 0.82, 0.6)]]
	var k := 0
	for n in BUILDINGS:
		if not models.has(n):
			continue
		var place := _place(n)
		for e in models[n]["empties"]:
			if not String(e).contains("_banner_"):
				continue
			var tip: Transform3D = place * (models[n]["empties"][e] as Transform3D)
			# The pole's direction out of the wall: the empty's heading (Blender's local +X, out of the facade).
			var out := tip.basis.x.normalized()
			out.y = 0.0
			out = out.normalized()
			var size := Vector2(1.05, 2.9)
			var origin := tip.origin - out * 1.2
			var basis := Basis(out, Vector3.UP, out.cross(Vector3.UP)).orthonormalized()
			var col: Array = colours[k % colours.size()]
			var m := _shader_mat("banner_cloth", {"field": col[0], "band": col[1], "pattern": 0, "size": size})
			_cloth(p_parent, "Banner%d" % k, Transform3D(basis, origin + Vector3(0.0, -0.03, 0.0)), size, 1, m, 20)
			k += 1


func _awnings(p_parent: Node3D) -> void:
	var colours := [[Color(0.8, 0.74, 0.6), Color(0.62, 0.14, 0.1)], [Color(0.78, 0.72, 0.58), Color(0.18, 0.3, 0.45)]]
	var k := 0
	for n in ["StallA", "StallB"]:
		if not models.has(n):
			continue
		# The awning from the back posts' tops (2.7 m) down to the front posts' (2.3 m), pinned at its corners.
		var place := _place(n)
		var back := place * Vector3(-1.45, 2.72, -0.85)
		var front_dir := place.basis * Vector3(0.0, -0.4, 1.7)
		var x_axis := place.basis * Vector3(1.0, 0.0, 0.0)
		var down := front_dir.normalized()
		var z_axis := x_axis.cross(-down).normalized()
		var basis := Basis(x_axis, -down, z_axis)
		# On the stall's model (Blender's +Y is the back): the posts at x ±1.4, y ±0.8.
		var size := Vector2(2.9, front_dir.length() * 1.04)
		var col: Array = colours[k]
		var m := _shader_mat("banner_cloth", {"field": col[0], "band": col[1], "pattern": 1, "stripe_width": 0.32, "size": size})
		_cloth(p_parent, "Awning%d" % k, Transform3D(basis, back), size, 4, m, 16)
		k += 1


func _laundry(p_parent: Node3D) -> void:
	# Lines across the street on the south side and across the alley between the east houses, clothes pegged on them.
	var lines := [[Vector3(-4.0, 8.2, 15.0), Vector3(0.5, 7.9, 15.4)], [Vector3(-4.0, 6.1, 17.6), Vector3(0.5, 6.4, 17.1)],
			[Vector3(19.0, 8.6, 1.8), Vector3(19.4, 8.4, 4.3)]]
	var cloth_cols := [Color(0.92, 0.9, 0.84), Color(0.72, 0.78, 0.86), Color(0.86, 0.8, 0.62), Color(0.7, 0.3, 0.25), Color(0.95, 0.94, 0.9)]
	var rope := StandardMaterial3D.new()
	rope.albedo_color = Color(0.5, 0.45, 0.36)
	rope.roughness = 0.9
	var k := 0
	for li in lines.size():
		var a: Vector3 = lines[li][0]
		var b: Vector3 = lines[li][1]
		var cyl := CylinderMesh.new()
		cyl.top_radius = 0.008
		cyl.bottom_radius = 0.008
		cyl.height = a.distance_to(b)
		cyl.radial_segments = 5
		cyl.material = rope
		var mi := MeshInstance3D.new()
		mi.mesh = cyl
		var dir := (b - a).normalized()
		var basis := Basis(dir.cross(Vector3.UP).normalized(), dir, dir.cross(dir.cross(Vector3.UP)).normalized() * -1.0).orthonormalized()
		mi.transform = Transform3D(basis, (a + b) * 0.5)
		_add(p_parent, mi, "LaundryLine%d" % li)
		var length := a.distance_to(b)
		var t := 0.12
		while t < length - 0.6:
			var w := randf_range(0.5, 1.1)
			var h := randf_range(0.6, 1.3)
			var p := a + dir * t
			var cb := Basis(dir, Vector3.UP, dir.cross(Vector3.UP)).orthonormalized()
			var m := StandardMaterial3D.new()
			m.albedo_color = cloth_cols[k % cloth_cols.size()]
			m.roughness = 0.95
			m.cull_mode = BaseMaterial3D.CULL_DISABLED
			m.backlight_enabled = true
			m.backlight = m.albedo_color * 0.5
			_cloth(p_parent, "Laundry%d" % k, Transform3D(cb, p), Vector2(w, h), 1, m, 12)
			t += w + randf_range(0.15, 0.4)
			k += 1


func _dust(p_parent: Node3D) -> void:
	# Motes in the low sun over the square.
	var dust := GPUParticles3D.new()
	dust.amount = 500
	dust.lifetime = 14.0
	dust.preprocess = 14.0
	dust.visibility_aabb = AABB(Vector3(-20, -1, -20), Vector3(40, 12, 40))
	var pm := ParticleProcessMaterial.new()
	pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
	pm.emission_box_extents = Vector3(18.0, 5.0, 18.0)
	pm.gravity = Vector3(0.0, -0.02, 0.0)
	pm.initial_velocity_min = 0.02
	pm.initial_velocity_max = 0.12
	pm.spread = 180.0
	pm.turbulence_enabled = true
	pm.turbulence_noise_strength = 0.4
	pm.turbulence_noise_scale = 6.0
	pm.scale_min = 0.5
	pm.scale_max = 1.4
	dust.process_material = pm
	var quad := QuadMesh.new()
	quad.size = Vector2(0.018, 0.018)
	var dm := StandardMaterial3D.new()
	dm.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	dm.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	dm.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	dm.albedo_color = Color(1.0, 0.85, 0.6, 0.35)
	dm.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	quad.material = dm
	dust.draw_pass_1 = quad
	dust.position = Vector3(2.0, 5.0, 0.0)
	_add(p_parent, dust, "Dust")


func _lamps(p_parent: Node3D) -> void:
	var glow := StandardMaterial3D.new()
	glow.albedo_color = Color(1.0, 0.8, 0.5)
	glow.emission_enabled = true
	glow.emission = Color(1.0, 0.68, 0.35)
	glow.emission_energy_multiplier = 3.0
	var bulb := SphereMesh.new()
	bulb.radius = 0.07
	bulb.height = 0.14
	bulb.material = glow
	var k := 0
	for n in BUILDINGS:
		if not models.has(n):
			continue
		var place := _place(n)
		for e in models[n]["empties"]:
			if not String(e).contains("_lamp_"):
				continue
			var x: Transform3D = place * (models[n]["empties"][e] as Transform3D)
			var mi := MeshInstance3D.new()
			mi.mesh = bulb
			mi.transform = Transform3D(Basis(), x.origin + Vector3(0.0, -0.02, 0.0))
			mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			_add(p_parent, mi, "Lamp%d" % k)
			var light := OmniLight3D.new()
			light.light_color = Color(1.0, 0.7, 0.4)
			light.light_energy = 0.8
			light.omni_range = 5.0
			light.position = Vector3(0.0, -0.05, 0.0)
			_add(mi, light, "Light")
			k += 1


# ------------------------------------------------------------------ the character

## Clips that loop (glTF says nothing of it).
const LOOPS := ["idle", "walk", "run", "sprint", "gait_walk", "gait_run", "gait_sprint", "fall", "hang_idle", "hang_shimmy",
		"climb_idle", "climb_move", "dive", "perch", "sit", "talk"]


func _build_player() -> PackedScene:
	DirAccess.make_dir_recursive_absolute(OUT + "/character")
	var doc := GLTFDocument.new()
	var st := GLTFState.new()
	doc.append_from_file(models_dir.path_join("Mannequin.glb"), st)
	var src: Node = doc.generate_scene(st)
	var skel: Skeleton3D = src.find_child("Skeleton3D", true, false)
	var body_mi: MeshInstance3D = src.find_child("MannequinBody", true, false)
	var ap_src: AnimationPlayer = src.find_child("AnimationPlayer", true, false)
	# The mannequin's materials: a satin shell, graphite joints.
	var shell := StandardMaterial3D.new()
	shell.albedo_color = Color(0.86, 0.84, 0.8)
	shell.roughness = 0.38
	shell.clearcoat_enabled = true
	shell.clearcoat = 0.35
	shell.clearcoat_roughness = 0.3
	shell.rim_enabled = true
	shell.rim = 0.25
	shell.rim_tint = 0.4
	var joint := StandardMaterial3D.new()
	joint.albedo_color = Color(0.09, 0.09, 0.1)
	joint.metallic = 0.65
	joint.roughness = 0.32
	joint.resource_name = "mannequin_joint" # (the crowd dresses the other surfaces)
	_save(shell, OUT + "/character/mannequin_shell.tres")
	_save(joint, OUT + "/character/mannequin_joint.tres")
	var mesh := body_mi.mesh as ArrayMesh
	for i in mesh.get_surface_count():
		var n := mesh.surface_get_material(i).resource_name if mesh.surface_get_material(i) else ""
		mesh.surface_set_material(i, joint if n == "mannequin_joint" else shell)
	mesh.resource_name = "Mannequin"
	_save(mesh, OUT + "/character/mannequin_mesh.res")
	_save(body_mi.skin, OUT + "/character/mannequin_skin.res")
	var lib: AnimationLibrary = ap_src.get_animation_library(&"")
	for a in lib.get_animation_list():
		var anim := lib.get_animation(a)
		anim.loop_mode = Animation.LOOP_LINEAR if String(a) in LOOPS else Animation.LOOP_NONE
	# The mantles' clips on their own (FellowTraversal3D plays them, the library refers to them).
	for a in ["mantle_high", "mantle_low", "vault", "hang_climb"]:
		_save(lib.get_animation(a), "%s/character/clip_%s.res" % [OUT, a])
	_save(lib, OUT + "/character/mannequin_clips.res")
	_mann = [mesh, lib]

	var player := CharacterBody3D.new()
	player.name = "Player"
	player.physics_interpolation_mode = Node.PHYSICS_INTERPOLATION_MODE_ON
	player.set_script(load("res://scripts/rooftops_player.gd"))
	player.collision_layer = 8
	player.collision_mask = LAYER_WORLD
	player.floor_snap_length = 0.3
	player.floor_max_angle = deg_to_rad(50.0)
	var prev := scene_root
	scene_root = player
	var cs := CollisionShape3D.new()
	var cap := CapsuleShape3D.new()
	cap.radius = 0.3
	cap.height = 1.8
	cs.shape = cap
	cs.position = Vector3(0.0, 0.9, 0.0)
	_add(player, cs, "CollisionShape3D")
	var model := Node3D.new()
	model.rotation = Vector3(0.0, PI, 0.0)
	_add(player, model, "Model")
	var mann := Node3D.new()
	_add(model, mann, "Mannequin")
	skel.owner = null
	body_mi.owner = null
	skel.get_parent().remove_child(skel)
	_add(mann, skel, "Skeleton3D")
	body_mi.owner = player
	body_mi.mesh = mesh
	var pose: Node = ClassDB.instantiate("FellowTraversalPose3D")
	# The body's weight (decision 0290): the hips swing on when a leap is caught, lag when it starts; the hands' and feet's
	# targets eased.
	pose.set("body_sway", 1.0)
	pose.set("sway_frequency", 1.7)
	pose.set("sway_damping", 0.32)
	pose.set("sway_max", 0.16)
	pose.set("limb_smoothing", 0.05)
	# The hands on the stone (decision 0291): hanging, the body moved until the shoulders are within reach of the holds.
	pose.set("hang_fit", true)
	_add(skel, pose, "TraversalPose")
	# Milestone 154: the head turned to where the body goes (the player script moves the target and sets the weight).
	var look := LookAtModifier3D.new()
	look.bone_name = "Head"
	look.forward_axis = SkeletonModifier3D.BONE_AXIS_PLUS_Z
	look.primary_rotation_axis = Vector3.AXIS_Y
	look.use_secondary_rotation = true
	look.relative = true
	look.use_angle_limitation = true
	look.symmetry_limitation = true
	look.primary_limit_angle = deg_to_rad(110.0) # ±55° to the sides
	look.primary_damp_threshold = 0.6
	look.secondary_limit_angle = deg_to_rad(70.0) # ±35° up and down
	look.secondary_damp_threshold = 0.6
	look.duration = 0.25
	look.transition_type = Tween.TRANS_SINE
	look.ease_type = Tween.EASE_IN_OUT
	look.influence = 0.0
	_add(skel, look, "HeadLook")
	var look_target := Node3D.new()
	look_target.top_level = true
	look_target.physics_interpolation_mode = Node.PHYSICS_INTERPOLATION_MODE_OFF # moved in the idle frames
	_add(player, look_target, "LookTarget")
	look.target_node = NodePath("../../../../LookTarget")
	var ap := AnimationPlayer.new()
	ap.add_animation_library(&"", lib)
	_add(player, ap, "AnimationPlayer")
	ap.root_node = NodePath("../Model")
	var at := AnimationTree.new()
	at.tree_root = _anim_tree()
	_add(player, at, "AnimationTree")
	at.anim_player = NodePath("../AnimationPlayer")
	at.root_node = NodePath("../Model")
	at.root_motion_track = NodePath("Mannequin/Skeleton3D:Root")
	# Advanced in the idle frames (milestone 154: smooth at any display rate; at the physics ticks the limbs moved in 60 Hz
	# steps under a body drawn between them); the player switches it to the physics ticks for the way out of the hay,
	# whose root motion the ticks read (at the idle frame's rate a tick would read the same motion twice, or none).
	at.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_IDLE
	var trav: Node = ClassDB.instantiate("FellowTraversal3D")
	trav.set("climb_mask", LAYER_CLIMB)
	trav.set("collision_mask", LAYER_WORLD)
	trav.set("climb_speed", 1.5)
	trav.set("shimmy_speed", 1.3)
	# Climbing from hold to hold (decision 0282): a ledge within 1.25 m reached hand over hand, one up to 2.3 m leapt to;
	# the feet braced on the wall.
	trav.set("hang_reach_up", 1.25)
	trav.set("hang_jump_height", 2.3)
	trav.set("hang_brace_height", 0.42)
	# Across from hold to hold (decision 0285): a gap to a ledge to the side up to 0.8 m reached hand over hand.
	trav.set("hang_reach_side", 0.8)
	# A leap winds up for 0.15 s, springs, flies hands first and is caught with its speed (decision 0290); up onto a ledge
	# from a hang with a clip of its own.
	trav.set("hang_leap_windup", 0.15)
	trav.set("hang_clip", lib.get_animation(&"hang_climb"))
	trav.set("high_clip", lib.get_animation(&"mantle_high"))
	trav.set("low_clip", lib.get_animation(&"mantle_low"))
	trav.set("vault_clip", lib.get_animation(&"vault"))
	trav.set("clip_root_bone", &"Root")
	_add(player, trav, "Traversal")
	scene_root = prev
	src.free()
	var ps := PackedScene.new()
	var err := ps.pack(player)
	if err != OK:
		push_error("could not pack the player: %d" % err)
	ResourceSaver.save(ps, OUT + "/player.tscn")
	player.free()
	return load(OUT + "/player.tscn")


var _mann := [] # the mannequin's mesh (its materials set) and clips, from _build_player()


## The square's people's figure (rooftops_crowd.gd dresses each one): the mannequin, its front +Z, its clips played by
## its own AnimationPlayer.
func _build_npc() -> PackedScene:
	var doc := GLTFDocument.new()
	var st := GLTFState.new()
	doc.append_from_file(models_dir.path_join("Mannequin.glb"), st)
	var src: Node = doc.generate_scene(st)
	var skel: Skeleton3D = src.find_child("Skeleton3D", true, false)
	var body_mi: MeshInstance3D = src.find_child("MannequinBody", true, false)
	var npc := Node3D.new()
	npc.name = "NPC"
	var prev := scene_root
	scene_root = npc
	var mann := Node3D.new()
	_add(npc, mann, "Mannequin")
	skel.owner = null
	body_mi.owner = null
	skel.get_parent().remove_child(skel)
	_add(mann, skel, "Skeleton3D")
	body_mi.owner = npc
	body_mi.mesh = _mann[0]
	body_mi.skin = load(OUT + "/character/mannequin_skin.res")
	var ap := AnimationPlayer.new()
	ap.add_animation_library(&"", _mann[1])
	_add(npc, ap, "AnimationPlayer")
	ap.root_node = NodePath("..")
	scene_root = prev
	src.free()
	var ps := PackedScene.new()
	ps.pack(npc)
	ResourceSaver.save(ps, OUT + "/npc.tscn")
	npc.free()
	return load(OUT + "/npc.tscn")


## The square's people (rooftops_crowd.gd): walkers on routes round the square, across it and along its streets out,
## groups talking, people on the palaces' benches, the stall keepers.
func _crowd(p_root: Node3D) -> void:
	var crowd := Node3D.new()
	crowd.set_script(load("res://scripts/rooftops_crowd.gd"))
	crowd.set("npc_scene", _build_npc())
	var P := func(a: Array) -> PackedVector3Array:
		var out := PackedVector3Array()
		for v in a:
			out.append(Vector3(v.x, 0.0, v.y))
		return out
	crowd.set("routes", [
		# Round the square by its fronts, one way and (further in) the other.
		[P.call([Vector2(-15.0, -10.6), Vector2(14.6, -10.6), Vector2(14.6, 11.6), Vector2(-15.0, 11.6)]), true, 7],
		[P.call([Vector2(-10.5, -5.0), Vector2(-10.5, 8.6), Vector2(8.4, 8.6), Vector2(8.4, -5.0)]), true, 5],
		# Across it, past the fountain.
		[P.call([Vector2(14.0, -9.5), Vector2(6.0, -2.0), Vector2(0.5, 6.5), Vector2(-8.0, 11.5)]), false, 3],
		# In and out of the streets.
		[P.call([Vector2(-1.8, 34.0), Vector2(-1.8, 12.5), Vector2(-7.5, 9.5)]), false, 3],
		[P.call([Vector2(32.0, 3.0), Vector2(16.0, 3.0), Vector2(10.0, -1.0)]), false, 2],
		[P.call([Vector2(6.8, -26.0), Vector2(6.8, -12.0), Vector2(9.0, -6.0)]), false, 2],
		[P.call([Vector2(-32.0, 2.0), Vector2(-16.5, 2.0), Vector2(-10.0, -2.5)]), false, 2],
	])
	crowd.set("groups", [[Vector3(1.0, 0.0, 5.5), 3], [Vector3(-9.6, 0.0, 7.0), 2], [Vector3(12.6, 0.0, -9.6), 2],
			[Vector3(14.0, 0.0, -4.0), 2], [Vector3(-7.0, 0.0, -9.2), 3]])
	var seats: Array[Transform3D] = []
	for n in ["PalazzoOcra", "PalazzoSiena"]:
		if not models.has(n):
			continue
		for e in models[n]["empties"]:
			if String(e).contains("_seat_"):
				var x: Transform3D = _place(n) * (models[n]["empties"][e] as Transform3D)
				seats.append(Transform3D(x.basis.orthonormalized(), Vector3(x.origin.x, 0.08, x.origin.z)))
	crowd.set("seats", seats)
	var keepers: Array[Transform3D] = []
	for n in ["StallA", "StallB"]:
		if models.has(n):
			# Behind the counter (the stall's front is Blender's -Y, Godot's +Z), facing out over it.
			var place := _place(n)
			keepers.append(Transform3D(place.basis, place * Vector3(0.3, 0.0, -0.2)))
	crowd.set("keepers", keepers)
	_add(p_root, crowd, "Crowd")


func _anim_tree() -> AnimationNodeBlendTree:
	var bt := AnimationNodeBlendTree.new()
	# Milestone 154: the gaits blended at the same moment of their stride — each a cycle of 1 s (gait_walk, gait_run,
	# gait_sprint) played together at the cycle's rate for the body's speed (loco/rate/scale, set by the controller:
	# the feet do not slide, the legs do not cross as one gait gives way to the next); idle blended in below a walk.
	var loco := AnimationNodeBlendTree.new()
	var gait := AnimationNodeBlendSpace1D.new()
	for pt in [["gait_walk", 1.6], ["gait_run", 4.4], ["gait_sprint", 7.0]]:
		var a := AnimationNodeAnimation.new()
		a.animation = pt[0]
		gait.add_blend_point(a, pt[1], -1, pt[0])
	gait.min_space = 1.6
	gait.max_space = 7.0
	gait.sync = true
	loco.add_node(&"gait", gait, Vector2(0, 0))
	loco.add_node(&"rate", AnimationNodeTimeScale.new(), Vector2(200, 0))
	loco.connect_node(&"rate", 0, &"gait")
	var idle := AnimationNodeAnimation.new()
	idle.animation = "idle"
	loco.add_node(&"idle", idle, Vector2(200, 200))
	loco.add_node(&"move", AnimationNodeBlend2.new(), Vector2(400, 100))
	loco.connect_node(&"move", 0, &"idle")
	loco.connect_node(&"move", 1, &"rate")
	loco.connect_node(&"output", 0, &"move")
	bt.add_node(&"loco", loco, Vector2(0, 0))
	var hang := AnimationNodeBlendSpace1D.new()
	for pt in [["hang_idle", 0.0], ["hang_shimmy", 1.0]]:
		var a := AnimationNodeAnimation.new()
		a.animation = pt[0]
		hang.add_blend_point(a, pt[1], -1, pt[0])
	bt.add_node(&"hang", hang, Vector2(0, 200))
	var climb := AnimationNodeBlendSpace1D.new()
	for pt in [["climb_idle", 0.0], ["climb_move", 1.0]]:
		var a := AnimationNodeAnimation.new()
		a.animation = pt[0]
		climb.add_blend_point(a, pt[1], -1, pt[0])
	bt.add_node(&"climb", climb, Vector2(0, 400))
	var states := ["loco", "jump", "fall", "land", "roll", "hang", "climb", "climb_jump", "dive_start", "dive", "hay_exit", "perch",
			"hang_reach_l", "hang_reach_r", "hang_leap_up", "hang_leap_l", "hang_leap_r", "hang_across_l", "hang_across_r", "jump_wall"]
	var tr := AnimationNodeTransition.new()
	tr.xfade_time = 0.2
	# Milestone 153: the cross-fades eased in and out (no linear swap of one pose for the next).
	var ease := Curve.new()
	ease.add_point(Vector2(0.0, 0.0), 0.0, 0.0)
	ease.add_point(Vector2(1.0, 1.0), 0.0, 0.0)
	tr.xfade_curve = ease
	for i in states.size():
		tr.add_input(states[i])
		tr.set_input_reset(i, true)
	bt.add_node(&"state", tr, Vector2(400, 200))
	for i in states.size():
		var n: String = states[i]
		if not bt.has_node(n):
			var a := AnimationNodeAnimation.new()
			a.animation = n
			bt.add_node(n, a, Vector2(0, 600 + 100 * i))
		if n.begins_with("hang_leap_") or n.begins_with("hang_reach_") or n.begins_with("hang_across_"):
			# Milestone 153: a move's clip played over the move's time (a leap's depends on its kind and its wind-up).
			var ts := AnimationNodeTimeScale.new()
			bt.add_node(&"ts_" + n, ts, Vector2(200, 600 + 100 * i))
			bt.connect_node(&"ts_" + n, 0, n)
			bt.connect_node(&"state", i, &"ts_" + n)
		else:
			bt.connect_node(&"state", i, n)
	bt.connect_node(&"output", 0, &"state")
	return bt
