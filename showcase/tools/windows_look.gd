extends SceneTree
## The rooftops' windows looked at (milestone 188, decision 0330): the scene as it plays (its sun, sky, clouds and
## people; the player hidden), a camera across the square at its east and north fronts, close to a palazzo's windows
## and in the street under a house, each picture after the lighting has settled. Writes <dir>/<view>.png.
##
##   godot --path showcase --rendering-driver vulkan --resolution 1280x720 -s res://tools/windows_look.gd -- \
##       --out=<dir> [--views=east,north,close,street] [--settle=16] [--glass=<property>=<value>]... [--before=1]
##
## --before=1: the windows' glass as before milestone 188 (rooftops/shaders/window_glass.gdshader: dark panes, curtains
## drawn on them); --glass=daylight=0.5 sets one of the glass material's properties.

const VIEWS := {
	"east": [Vector3(2.0, 3.0, 4.0), Vector3(22.0, 6.5, 0.0), 55.0], # from, to, field of view
	"north": [Vector3(4.0, 2.2, 8.0), Vector3(-2.0, 7.0, -18.0), 60.0],
	"close": [Vector3(13.5, 6.4, 1.5), Vector3(17.6, 5.8, -3.5), 50.0],
	"street": [Vector3(-3.5, 1.7, 9.5), Vector3(-9.0, 6.5, 14.0), 60.0],
}


func _init() -> void:
	var out := "user://windows_look"
	var views := PackedStringArray(VIEWS.keys())
	var settle := 16
	var glass_props := {}
	var before := false
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.get_slice("=", 1)
		elif a.begins_with("--views="):
			views = a.get_slice("=", 1).split(",")
		elif a.begins_with("--settle="):
			settle = int(a.get_slice("=", 1))
		elif a.begins_with("--glass="):
			var kv := a.substr(8)
			glass_props[kv.get_slice("=", 0)] = str_to_var(kv.get_slice("=", 1))
		elif a == "--before=1":
			before = true
	DirAccess.make_dir_recursive_absolute(out)
	var sc: Node = (load("res://rooftops/rooftops.tscn") as PackedScene).instantiate()
	root.add_child(sc)
	await process_frame
	var glass: Material = load("res://rooftops/materials/glass.tres")
	for k in glass_props:
		glass.set(k, glass_props[k])
	if before:
		var old := ShaderMaterial.new()
		old.shader = load("res://rooftops/shaders/window_glass.gdshader")
		old.set_shader_parameter("lead_width", 0.0)
		old.set_shader_parameter("lit_share", 0.0)
		old.set_shader_parameter("lamp_energy", 0.3)
		var stack: Array[Node] = [sc]
		while not stack.is_empty():
			var n: Node = stack.pop_back()
			stack.append_array(n.get_children())
			var mmi := n as MultiMeshInstance3D
			if mmi and mmi.multimesh and mmi.multimesh.mesh:
				for i in mmi.multimesh.mesh.get_surface_count():
					if mmi.multimesh.mesh.surface_get_material(i) == glass:
						mmi.multimesh.mesh.surface_set_material(i, old)
			var mi := n as MeshInstance3D
			if mi and mi.mesh:
				for i in mi.mesh.get_surface_count():
					if mi.mesh.surface_get_material(i) == glass:
						mi.set_surface_override_material(i, old)
	var player: Node3D = sc.get_node_or_null("Player")
	if player:
		player.visible = false
		player.process_mode = Node.PROCESS_MODE_DISABLED
	var cam := Camera3D.new()
	sc.add_child(cam)
	cam.current = true
	for v in views:
		var p: Array = VIEWS[v]
		cam.fov = p[2]
		cam.global_position = p[0]
		cam.look_at(p[1], Vector3.UP)
		for k in settle:
			cam.current = true
			await process_frame
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png("%s/%s.png" % [out, v])
		print("windows_look: ", v)
	quit()
