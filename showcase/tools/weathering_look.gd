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
##
## --off=1: the FellowWeathering3D hidden (the town as before milestone 190); --set=streaks=0.5 sets one of its
## properties (a number); --mat=res://rooftops/materials_photo/stone_rustic.tres:normal_strength=0.2 sets a shader
## material's parameter; --debug=1 draws the streaks red and the damp green, without the patina; --lite=1 keeps the
## square's buildings, its fountain, the paving and the light alone (no city beyond, no country, no people, props or
## cloth: lavapipe's whole rooftops outgrows 6 GB).

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
	var debug_colors := false
	var props := {}
	var mats := []
	var scene_props := {}
	var env_props := {}
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
	DirAccess.make_dir_recursive_absolute(out)
	for m in mats:
		var path: String = m.get_slice(":", 0) + ":" + m.get_slice(":", 1) if m.begins_with("res:") else m.get_slice(":", 0)
		var kv: String = m.substr(path.length() + 1)
		var mat := load(path) as ShaderMaterial
		mat.set_shader_parameter(kv.get_slice("=", 0), str_to_var(kv.get_slice("=", 1)))
		_held.append(mat) # held: a material no longer referenced is freed, and loaded again unchanged
	var sc: Node = (load("res://rooftops/rooftops.tscn") as PackedScene).instantiate()
	if lite:
		for n in ["Skyline", "CityHouses", "Landscape", "Trees_cypress", "Trees_olive", "Trees_pine", "Trees_broadleaf",
				"Villas", "Trees_villa_cypress", "Birds", "Player", "Crowd", "CameraRig", "ReferenceShot", "Demo",
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
	for k in scene_props:
		sc.set(k, scene_props[k]) # rooftops.gd's (milestone 193), before its _ready
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
	quit()
