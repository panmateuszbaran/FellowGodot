extends SceneTree
## Milestone 188 (decision 0330): the rooftops' windows given rooms behind them without Blender — every model's glass
## (rooftops/models, the city's houses in rooftops/models/houses) given its windows' UV2 (tools/window_uv2.gd) and
## rooftops/materials/glass.tres made FellowInteriorMaterial3D. build_rooftops.gd does the same when it builds the
## scene from Blender's models; this brings the saved scene there.
##
##   godot --headless --path showcase -s res://tools/rooftops_windows.gd

const WindowUV2 := preload("res://tools/window_uv2.gd")


func _init() -> void:
	var glass_path := "res://rooftops/materials/glass.tres"
	var glass := WindowUV2.glass_material()
	ResourceSaver.save(glass, glass_path)
	glass.take_over_path(glass_path)
	var meshes := 0
	for dir in ["res://rooftops/models", "res://rooftops/models/houses"]:
		for f in DirAccess.get_files_at(dir):
			if not f.ends_with(".res"):
				continue
			var path: String = dir.path_join(f)
			var m = load(path)
			if not (m is ArrayMesh):
				continue
			var has_glass := false
			for i in m.get_surface_count():
				var mat: Material = m.surface_get_material(i)
				if mat and mat.resource_path == glass_path:
					has_glass = true
			if not has_glass:
				continue
			var out := ArrayMesh.new()
			out.resource_name = m.resource_name
			for i in m.get_surface_count():
				var arrays: Array = m.surface_get_arrays(i)
				var mat: Material = m.surface_get_material(i)
				if mat and mat.resource_path == glass_path:
					arrays = WindowUV2.add(arrays, WindowUV2.ROOM_HEIGHT)
					mat = glass
				out.add_surface_from_arrays(m.surface_get_primitive_type(i), arrays)
				out.surface_set_name(i, m.surface_get_name(i))
				out.surface_set_material(i, mat)
			ResourceSaver.save(out, path, ResourceSaver.FLAG_COMPRESS)
			meshes += 1
			print("rooftops_windows: ", path)
	print("rooftops_windows: %d meshes' glass given rooms" % meshes)
	quit()
