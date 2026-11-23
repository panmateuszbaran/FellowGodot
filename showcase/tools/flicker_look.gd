extends SceneTree
## Shadows flickering on the rooftops' buildings (milestone 188): the scene as it plays (the player hidden), a camera held
## still for --still frames and then moved along the square at a walk's speed for --moving frames, every frame saved
## (<dir>/f%04d.png) for tools' frame-to-frame differences. --set=<setting>=<value> (repeatable) changes a project
## setting first (e.g. --set=rendering/fellow/shadows/directional_cache=false); --plain_glass=1 puts a plain material on
## the windows' glass (its shader is long to compile on lavapipe).
##
##   godot --path showcase --rendering-driver vulkan --resolution 640x360 --fixed-fps 60 -s res://tools/flicker_look.gd \
##       -- --out=<dir> [--view=east] [--still=30] [--moving=60] [--settle=20] [--configs=base,no_contact,...]
##
## --configs: one sequence after another in one run, each in <dir>/<config>/ with one feature off — base (none),
## no_contact (FellowEngine's contact shadows), no_cache (the directional shadow cache), no_taa, no_ssao (SSAO and
## SSIL), no_soft (the directional shadows' soft filter), no_sdfgi.

const VIEWS := {
	# from, to, the way it moves (m a second)
	"east": [Vector3(2.0, 3.0, 4.0), Vector3(22.0, 6.5, 0.0), Vector3(0.0, 0.0, -1.4)],
	"north": [Vector3(4.0, 2.2, 8.0), Vector3(-2.0, 7.0, -18.0), Vector3(-1.4, 0.0, 0.0)],
	"walk": [Vector3(-6.0, 1.8, 6.0), Vector3(-6.0, 4.0, -18.0), Vector3(0.0, 0.0, -1.4)],
}


func _init() -> void:
	var out := "user://flicker_look"
	var view := "east"
	var still := 30
	var moving := 60
	var settle := 20
	var plain_glass := false
	var configs := PackedStringArray()
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.get_slice("=", 1)
		elif a.begins_with("--view="):
			view = a.get_slice("=", 1)
		elif a.begins_with("--still="):
			still = int(a.get_slice("=", 1))
		elif a.begins_with("--moving="):
			moving = int(a.get_slice("=", 1))
		elif a.begins_with("--settle="):
			settle = int(a.get_slice("=", 1))
		elif a.begins_with("--configs="):
			configs = a.get_slice("=", 1).split(",")
		elif a == "--plain_glass=1":
			plain_glass = true
		elif a.begins_with("--set="):
			var kv := a.substr(6)
			var value = str_to_var(kv.get_slice("=", 1))
			ProjectSettings.set_setting(kv.get_slice("=", 0), value if value != null else kv.get_slice("=", 1))
	DirAccess.make_dir_recursive_absolute(out)
	var sc: Node = (load("res://rooftops/rooftops.tscn") as PackedScene).instantiate()
	if plain_glass:
		# Every mesh's glass a plain dark material (surface overrides; the city's houses' shared meshes changed).
		var glass: Material = load("res://rooftops/materials/glass.tres")
		var plain := StandardMaterial3D.new()
		plain.albedo_color = Color(0.03, 0.03, 0.03)
		plain.roughness = 0.1
		var stack: Array[Node] = [sc]
		while not stack.is_empty():
			var n: Node = stack.pop_back()
			stack.append_array(n.get_children())
			var mi := n as MeshInstance3D
			if mi and mi.mesh:
				for i in mi.mesh.get_surface_count():
					if mi.mesh.surface_get_material(i) == glass:
						mi.set_surface_override_material(i, plain)
			var mmi := n as MultiMeshInstance3D
			if mmi and mmi.get("mesh") is Mesh:
				var m: Mesh = mmi.get("mesh")
				for i in m.get_surface_count():
					if m.surface_get_material(i) == glass:
						m.surface_set_material(i, plain)
	root.add_child(sc)
	await process_frame
	var player: Node3D = sc.get_node_or_null("Player")
	if player:
		player.visible = false
		player.process_mode = Node.PROCESS_MODE_DISABLED
	var cam := Camera3D.new()
	cam.fov = 60.0
	sc.add_child(cam)
	var p: Array = VIEWS[view]
	var from: Vector3 = p[0]
	var look: Vector3 = p[1] - p[0]
	if configs.is_empty():
		configs = PackedStringArray([""])
	var env: Environment = null
	for n in sc.find_children("*", "WorldEnvironment", true, false):
		env = (n as WorldEnvironment).environment
	for config in configs:
		_configure(config, env, true)
		var dir := out if config == "" else out.path_join(config)
		DirAccess.make_dir_recursive_absolute(dir)
		for i in settle + still + moving:
			var k := maxi(i - settle - still, 0)
			cam.current = true
			cam.global_position = from + p[2] * (k / 60.0)
			cam.look_at(cam.global_position + look, Vector3.UP)
			await process_frame
			if i >= settle:
				await RenderingServer.frame_post_draw
				root.get_texture().get_image().save_png("%s/f%04d.png" % [dir, i - settle])
		_configure(config, env, false)
		print("flicker_look: %s %d frames" % [config, still + moving])
	quit()


## One feature off for a sequence (p_off), or on again after it.
func _configure(p_config: String, p_env: Environment, p_off: bool) -> void:
	match p_config:
		"no_contact":
			ProjectSettings.set_setting("rendering/fellow/post/contact_shadows/enabled", not p_off)
		"no_cache":
			ProjectSettings.set_setting("rendering/fellow/shadows/directional_cache", not p_off)
		"no_taa":
			root.use_taa = not p_off
		"no_ssao":
			if p_env:
				p_env.ssao_enabled = not p_off
				p_env.ssil_enabled = not p_off
		"no_soft":
			RenderingServer.directional_soft_shadow_filter_set_quality(RenderingServer.SHADOW_QUALITY_HARD if p_off else RenderingServer.SHADOW_QUALITY_SOFT_MEDIUM)
		"no_sdfgi":
			if p_env:
				p_env.sdfgi_enabled = not p_off
