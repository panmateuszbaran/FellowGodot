extends Node3D
## The rooftops scene: a Renaissance square at the golden hour (built by tools/build_rooftops.gd). Its sky's settings
## (few clouds, high and thin, so that the low sun reaches the square); its photographic materials (Poly Haven's,
## tools/fetch_textures.py) put in place of the procedural ones when their textures have been fetched and imported; and
## the mouse: captured for looking round, Esc frees it, a click takes it again.

@export var cloud_coverage := 0.15
@export var cloud_density := 0.7


func _ready() -> void:
	ProjectSettings.set_setting("rendering/fellow/post/clouds/coverage", cloud_coverage)
	ProjectSettings.set_setting("rendering/fellow/post/clouds/density", cloud_density)
	_photo_materials()
	if not OS.get_cmdline_user_args().has("--demo") and DisplayServer.get_name() != "headless":
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.physical_keycode == KEY_ESCAPE:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	elif event is InputEventMouseButton and event.pressed and Input.mouse_mode != Input.MOUSE_MODE_CAPTURED:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	elif event is InputEventKey and event.pressed and event.physical_keycode == KEY_F11:
		var fs := DisplayServer.window_get_mode() == DisplayServer.WINDOW_MODE_FULLSCREEN
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED if fs else DisplayServer.WINDOW_MODE_FULLSCREEN)


## The photographic materials (rooftops/materials_photo/<name>) in place of the procedural ones of the same name
## (rooftops/materials/<name>) on every mesh, if the textures are there (fetched by tools/fetch_textures.py, imported).
func _photo_materials() -> void:
	const PHOTO := "res://rooftops/materials_photo/"
	if not ResourceLoader.exists("res://rooftops/textures/plastered_wall_diff_2k.jpg") or not DirAccess.dir_exists_absolute(PHOTO):
		return
	var swap := {}
	for f in DirAccess.get_files_at(PHOTO):
		var file := f.trim_suffix(".remap")
		if file.ends_with(".tres") or file.ends_with(".res"):
			var m := load(PHOTO + file) as Material
			if m:
				swap["res://rooftops/materials/" + file] = m
	var stack: Array[Node] = [self]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		stack.append_array(n.get_children())
		var mmi := n as MultiMeshInstance3D
		if mmi and mmi.multimesh and mmi.multimesh.mesh:
			# Milestone 153: the city's houses (instances): their mesh's own materials swapped (it is theirs alone).
			var mesh := mmi.multimesh.mesh
			for i in mesh.get_surface_count():
				var m := mesh.surface_get_material(i)
				if m and swap.has(m.resource_path):
					mesh.surface_set_material(i, swap[m.resource_path])
			continue
		var mi := n as MeshInstance3D
		if mi == null or mi.mesh == null:
			continue
		for i in mi.mesh.get_surface_count():
			var m := mi.mesh.surface_get_material(i)
			if m and swap.has(m.resource_path):
				mi.set_surface_override_material(i, swap[m.resource_path])
