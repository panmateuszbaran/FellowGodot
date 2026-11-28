extends SceneTree
## The rooftops' weathering looked at (milestone 190, decision 0332): the scene as it plays (its sun, sky, clouds and
## people; the player hidden) from a few cameras — across the square at its east and north fronts, close to a
## palazzo's windows, at a house's foot in the street — each picture after the lighting has settled. Writes
## <dir>/<view>.png.
##
##   godot --path showcase --rendering-driver vulkan --resolution 1280x720 -s res://tools/weathering_look.gd -- \
##       --out=<dir> [--views=east,north,close,foot,rustic] [--settle=40] [--off=1] [--set=<property>=<value>]...
##       [--mat=<material>:<parameter>=<value>]... [--debug=1] [--lite=1]
##       [--scene=<rooftops.gd's property>=<value>]... [--env=<the Environment's property>=<value>]... (milestone 193)
##       [--node=<a node's path in the scene>:<property>=<value>]... [--cast_city=1] [--timing=1] (milestone 199)
##       [--setting=<a project setting>=<value>]... (milestone 199)
##
## --off=1: the FellowWeathering3D hidden (the town as before milestone 190); --set=streaks=0.5 sets one of its
## properties (a number); --mat=res://rooftops/materials_photo/stone_rustic.tres:normal_strength=0.2 sets a shader
## material's parameter; --debug=1 draws the streaks red and the damp green, without the patina; --lite=1 keeps the
## square's buildings, its fountain, the paving and the light alone (no city beyond, no country, no people, props or
## cloth: lavapipe's whole rooftops outgrows 6 GB); --lite=2 (milestone 196) keeps the whole town, the city beyond and the
## country, without the people, birds, props, cloth and particles.

const VIEWS := {
	"east": [Vector3(2.0, 3.0, 4.0), Vector3(22.0, 6.5, 0.0), 55.0], # from, to, field of view
	"north": [Vector3(4.0, 2.2, 8.0), Vector3(-2.0, 7.0, -18.0), 60.0],
	"close": [Vector3(13.5, 6.4, 1.5), Vector3(17.6, 5.8, -3.5), 50.0],
	"foot": [Vector3(-3.5, 1.6, 9.5), Vector3(-9.0, 2.6, 14.5), 60.0],
	"rustic": [Vector3(15.5, 1.7, 0.5), Vector3(21.0, 1.8, -3.0), 55.0],
	"ref": [Vector3(-12.0, 1.7, 13.0), Vector3(-21.0, 6.0, 2.0), 62.0],
	"ref2": [Vector3(-14.0, 1.7, 8.0), Vector3(-23.0, 6.5, 1.0), 62.0],
	# Milestone 193: the square and its sky — towards the campanile, and towards the sun.
	"sky_n": [Vector3(2.0, 1.7, 12.0), Vector3(-2.0, 14.0, -20.0), 70.0],
	"sky_sun": [Vector3(4.0, 1.7, -6.0), Vector3(-20.0, 14.0, 13.0), 70.0],
	# Milestone 196: the view from the campanile's beam (the viewpoint) over the city to the hills — south over the
	# square, west into the sun, east with it behind.
	"vista_s": [Vector3(1.3, 27.5, -10.0), Vector3(1.3, 12.0, 200.0), 65.0],
	"vista_w": [Vector3(1.3, 27.5, -10.0), Vector3(-200.0, 10.0, 40.0), 65.0],
	"vista_e": [Vector3(1.3, 27.5, -10.0), Vector3(200.0, 10.0, -30.0), 65.0],
}
## --lite=1's town.
const KEEP := ["PalazzoOcra", "Campanile", "CasaCrema", "PalazzoSiena", "CasaBianca", "CasaOcraBassa", "CasaRosa",
		"CasaRiferimento", "Fountain"]


var _held := []


func _init() -> void:
	var out := "user://weathering_look"
	var views := PackedStringArray(VIEWS.keys())
	var settle := 40
	var off := false
	var lite := false
	var far := false
	var debug_colors := false
	var props := {}
	var mats := []
	var scene_props := {}
	var env_props := {}
	var node_props := []
	var cast_city := false
	var timing := false
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.get_slice("=", 1)
		elif a.begins_with("--views="):
			views = a.get_slice("=", 1).split(",")
		elif a.begins_with("--settle="):
			settle = int(a.get_slice("=", 1))
		elif a == "--off=1":
			off = true
		elif a == "--lite=1":
			lite = true
		elif a == "--lite=2":
			far = true
		elif a == "--debug=1":
			debug_colors = true
		elif a.begins_with("--mat="):
			mats.append(a.substr(6))
		elif a.begins_with("--scene="):
			var kv := a.substr(8)
			scene_props[kv.get_slice("=", 0)] = str_to_var(kv.get_slice("=", 1))
		elif a.begins_with("--env="):
			var kv := a.substr(6)
			env_props[kv.get_slice("=", 0)] = str_to_var(kv.get_slice("=", 1))
		elif a.begins_with("--set="):
			var kv := a.substr(6)
			props[kv.get_slice("=", 0)] = str_to_var(kv.get_slice("=", 1))
		elif a.begins_with("--node="):
			node_props.append(a.substr(7))
		elif a == "--cast_city=1":
			cast_city = true
		elif a == "--timing=1":
			timing = true
		elif a.begins_with("--setting="):
			# Milestone 199: a project setting (--setting=rendering/fellow/post/bloom/enabled=false), before the scene.
			var kv := a.substr(10)
			ProjectSettings.set_setting(kv.get_slice("=", 0), str_to_var(kv.get_slice("=", 1)))
	DirAccess.make_dir_recursive_absolute(out)
	for m in mats:
		var path: String = m.get_slice(":", 0) + ":" + m.get_slice(":", 1) if m.begins_with("res:") else m.get_slice(":", 0)
		var kv: String = m.substr(path.length() + 1)
		var mat := load(path) as Material
		if mat is ShaderMaterial:
			(mat as ShaderMaterial).set_shader_parameter(kv.get_slice("=", 0), str_to_var(kv.get_slice("=", 1)))
		else:
			# Milestone 199: any other material's property (--mat=res://rooftops/materials/dust_layer.tres:density=0.0003).
			mat.set(kv.get_slice("=", 0), str_to_var(kv.get_slice("=", 1)))
		_held.append(mat) # held: a material no longer referenced is freed, and loaded again unchanged
	var sc: Node = (load("res://rooftops/rooftops.tscn") as PackedScene).instantiate()
	if lite:
		for n in ["Skyline", "CityHouses", "Landscape", "Trees_cypress", "Trees_olive", "Trees_pine", "Trees_broadleaf",
				"Trees_wood", "Villas", "Trees_villa_cypress", "Birds", "Player", "Crowd", "CameraRig", "ReferenceShot", "Demo",
				"PipelineWarmer"]:
			var c := sc.get_node_or_null(n)
			if c:
				sc.remove_child(c)
				c.free()
		var town := sc.get_node("Town")
		for c in town.get_children():
			if not (c.name in KEEP):
				town.remove_child(c)
				c.free()
	if far:
		for n in ["Birds", "Player", "Crowd", "CameraRig", "ReferenceShot", "Demo", "PipelineWarmer"]:
			var c := sc.get_node_or_null(n)
			if c:
				sc.remove_child(c)
				c.free()
		var town := sc.get_node("Town")
		for c in town.get_children():
			if c is FellowCloth3D or c is GPUParticles3D or String(c.name).begins_with("Laundry") or String(c.name).begins_with("Bollard") \
					or String(c.name).begins_with("Barrel") or String(c.name).begins_with("Crate") or String(c.name).begins_with("Stall"):
				town.remove_child(c)
				c.free()
	for k in scene_props:
		sc.set(k, scene_props[k]) # rooftops.gd's (milestone 193), before its _ready
	# Milestone 199: a node's property (--node=Sun:directional_shadow_max_distance=260); the city's far houses' shadows.
	for np in node_props:
		var path: String = np.get_slice(":", 0)
		var kv: String = np.substr(path.length() + 1)
		var target := sc.get_node_or_null(path)
		if target:
			target.set(kv.get_slice("=", 0), str_to_var(kv.get_slice("=", 1)))
		else:
			push_warning("no node " + path)
	if cast_city:
		for c in sc.find_children("*", "GeometryInstance3D", true, false):
			if String(c.get_parent().name) == "CityHouses":
				(c as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	for n in sc.find_children("*", "WorldEnvironment", true, false):
		for k in env_props:
			(n as WorldEnvironment).environment.set(k, env_props[k])
	root.add_child(sc)
	var w: Node3D = sc.get_node_or_null("Weathering")
	if w:
		w.visible = not off
		for k in props:
			w.set(k, props[k])
		if debug_colors:
			w.set("stain_color", Color(1.0, 0.0, 0.0))
			w.set("damp_color", Color(0.0, 1.0, 0.0))
			w.set("patina", 0.0)
	var player: Node3D = sc.get_node_or_null("Player")
	if player:
		player.visible = false
		player.process_mode = Node.PROCESS_MODE_DISABLED
	await process_frame
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
		print("weathering_look: ", v)
		if ProjectSettings.get_setting("rendering/fellow/shadows/virtual/enabled", false):
			# Milestone 255: the virtual shadow map's pages and the scene cull's time for them (decision 0396).
			var st: Dictionary = RenderingServer.fellow_get_stats()
			var parts := PackedStringArray()
			for k in st.keys():
				if String(k).begins_with("shadows_virtual"):
					parts.append("%s=%s" % [String(k).trim_prefix("shadows_virtual_"), st[k]])
			print("weathering_look_vsm %s %s" % [v, " ".join(parts)])
		if timing:
			# Milestone 199: the view's frame on the GPU and the CPU, the mean of 120 frames (ms).
			var vp := root.get_viewport_rid()
			RenderingServer.viewport_set_measure_render_time(vp, true)
			var gpu := 0.0
			var cpu := 0.0
			for k in 120:
				cam.current = true
				await process_frame
				await RenderingServer.frame_post_draw
				gpu += RenderingServer.viewport_get_measured_render_time_gpu(vp)
				cpu += RenderingServer.viewport_get_measured_render_time_cpu(vp)
			print("weathering_look_timing %s %.3f %.3f" % [v, gpu / 120.0, cpu / 120.0])
	quit()
