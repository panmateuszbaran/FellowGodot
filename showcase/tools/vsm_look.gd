extends SceneTree
## Virtual shadow maps looked at (milestone 255, decision 0396): a shadow lab built here — a ground, and at 2 to 150 m
## along the view a row of casters that need a sharp shadow (a pole 4 cm thick, a fence of 5 cm slats, a box, a ball,
## a lattice of 2 cm bars) — under a low sun, from a few cameras. Writes <dir>/<view>.png and <dir>/<view>.txt (the
## virtual shadow map's statistics after the picture).
##
##   godot --path showcase --rendering-driver vulkan --resolution 1280x720 -s res://tools/vsm_look.gd -- \
##       --out=<dir> [--views=row,near,far,top] [--settle=12] [--vsm=1] [--setting=<a project setting>=<value>]...
##       [--atlas=4096] [--split=0.1] [--move=1] [--scatter=10000]
##
## --scatter=N: N more boxes scattered over 400 x 400 m (the scene cull's time for many instances);
## --vsm=1: rendering/fellow/shadows/virtual/enabled on; --atlas: the directional shadow atlas's size (the cascades';
## 16384 with --split=0.02 and one split makes a reference near the camera); --move=1: the ball swings (a dynamic
## caster) between frames.

const VIEWS := {
	"row": [Vector3(0.6, 1.7, 0.0), Vector3(0.0, 0.6, -40.0), 70.0], # from, to, field of view
	"near": [Vector3(1.6, 1.6, -0.5), Vector3(-0.2, 0.0, -3.0), 60.0],
	"far": [Vector3(0.5, 2.0, -60.0), Vector3(0.0, 0.4, -150.0), 40.0],
	"top": [Vector3(2.0, 6.0, -8.0), Vector3(-1.0, 0.0, -12.0), 60.0],
	# A closed room of thick walls, its door towards the camera: no light inside but through the door.
	"room": [Vector3(-12.0, 1.6, -4.0), Vector3(-12.0, 0.3, -12.0), 60.0],
}
const DISTANCES := [2.0, 5.0, 10.0, 20.0, 40.0, 80.0, 150.0]

var _ball: MeshInstance3D


func _mat(p_color: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = p_color
	m.roughness = 0.9
	return m


func _mesh(p_parent: Node3D, p_mesh: Mesh, p_pos: Vector3, p_mat: Material) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = p_mesh
	mi.position = p_pos
	mi.material_override = p_mat
	p_parent.add_child(mi)
	return mi


func _build(p_root: Node3D) -> void:
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	env.environment.background_mode = Environment.BG_SKY
	env.environment.sky = sky
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	p_root.add_child(env)

	var sun := DirectionalLight3D.new()
	sun.name = "Sun"
	sun.rotation_degrees = Vector3(-32.0, 35.0, 0.0)
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 200.0
	sun.light_energy = 1.2
	p_root.add_child(sun)

	var ground := PlaneMesh.new()
	ground.size = Vector2(500.0, 500.0)
	_mesh(p_root, ground, Vector3(0.0, 0.0, -100.0), _mat(Color(0.75, 0.73, 0.70)))

	var dark := _mat(Color(0.35, 0.3, 0.28))
	var pole := CylinderMesh.new()
	pole.top_radius = 0.02
	pole.bottom_radius = 0.02
	pole.height = 2.0
	var box := BoxMesh.new()
	box.size = Vector3(0.6, 0.6, 0.6)
	var slat := BoxMesh.new()
	slat.size = Vector3(0.05, 1.0, 0.03)
	var bar := BoxMesh.new()
	bar.size = Vector3(0.02, 0.02, 1.2)
	var ball := SphereMesh.new()
	ball.radius = 0.3
	ball.height = 0.6
	for d in DISTANCES:
		var x := -1.5
		_mesh(p_root, pole, Vector3(x, 1.0, -d), dark)
		_mesh(p_root, box, Vector3(x + 1.2, 0.3, -d), dark)
		# The fence: 8 slats 5 cm wide, 5 cm apart.
		for i in 8:
			_mesh(p_root, slat, Vector3(x + 2.2 + i * 0.1, 0.5, -d), dark)
		# The lattice: 2 cm bars 4 cm apart, 1.2 m up.
		for i in 6:
			_mesh(p_root, bar, Vector3(x + 3.4 + i * 0.06, 1.2, -d), dark)
		var b := _mesh(p_root, ball, Vector3(x + 4.4, 0.6, -d), dark)
		if d == 5.0:
			_ball = b
	# The room: 6 x 6 m inside, walls 0.3 m thick overlapping at the corners, 3 m high, a slab roof over them; a door
	# 1.2 x 2.2 m in its front (+Z) wall, at x = -12, z = -7 .. -13.
	var wall := _mat(Color(0.8, 0.78, 0.74))
	var c := Vector3(-12.0, 0.0, -10.0)
	var walls := [
		[Vector3(0.0, 1.5, -3.15), Vector3(6.6, 3.0, 0.3)], # back
		[Vector3(-3.15, 1.5, 0.0), Vector3(0.3, 3.0, 6.6)], # left
		[Vector3(3.15, 1.5, 0.0), Vector3(0.3, 3.0, 6.6)], # right
		[Vector3(-1.95, 1.5, 3.15), Vector3(2.7, 3.0, 0.3)], # front, left of the door
		[Vector3(1.95, 1.5, 3.15), Vector3(2.7, 3.0, 0.3)], # front, right of the door
		[Vector3(0.0, 2.6, 3.15), Vector3(1.4, 0.8, 0.3)], # over the door
		[Vector3(0.0, 3.15, 0.0), Vector3(6.8, 0.3, 6.8)], # the roof
	]
	for w in walls:
		var bm := BoxMesh.new()
		bm.size = w[1]
		_mesh(p_root, bm, c + w[0], wall)


func _init() -> void:
	var out := "user://vsm_look"
	var views := PackedStringArray(VIEWS.keys())
	var settle := 12
	var move := false
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.get_slice("=", 1)
		elif a.begins_with("--views="):
			views = a.get_slice("=", 1).split(",")
		elif a.begins_with("--settle="):
			settle = int(a.get_slice("=", 1))
		elif a == "--vsm=1":
			ProjectSettings.set_setting("rendering/fellow/shadows/virtual/enabled", true)
		elif a.begins_with("--atlas="):
			RenderingServer.directional_shadow_atlas_set_size(int(a.get_slice("=", 1)), true)
		elif a == "--move=1":
			move = true
		elif a.begins_with("--setting="):
			var kv := a.substr(10)
			ProjectSettings.set_setting(kv.get_slice("=", 0), str_to_var(kv.get_slice("=", 1)))
	var split := -1.0
	var scatter := 0
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--split="):
			split = float(a.get_slice("=", 1))
		elif a.begins_with("--scatter="):
			scatter = int(a.get_slice("=", 1))
	DirAccess.make_dir_recursive_absolute(out)

	var root := Node3D.new()
	_build(root)
	var rng := RandomNumberGenerator.new()
	rng.seed = 255
	var small := BoxMesh.new()
	small.size = Vector3(0.5, 1.0, 0.5)
	var grey := _mat(Color(0.5, 0.5, 0.5))
	for i in scatter:
		_mesh(root, small, Vector3(rng.randf_range(-200.0, 200.0), 0.5, rng.randf_range(-300.0, 100.0)), grey)
	if split > 0.0:
		var sun: DirectionalLight3D = root.get_node("Sun")
		sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
		sun.directional_shadow_max_distance = 200.0 * split
	var cam := Camera3D.new()
	root.add_child(cam)
	get_root().add_child(root)
	cam.make_current()

	for v in views:
		var spec: Array = VIEWS[v]
		cam.fov = spec[2]
		cam.look_at_from_position(spec[0], spec[1])
		for i in settle:
			if move and _ball:
				_ball.position.x = 2.9 + sin(float(i) * 0.7) * 0.5
			await process_frame
		await RenderingServer.frame_post_draw
		var img := get_root().get_texture().get_image()
		img.save_png(out.path_join(v + ".png"))
		var stats: Dictionary = RenderingServer.fellow_get_stats() if RenderingServer.has_method("fellow_get_stats") else {}
		var lines := PackedStringArray()
		for k in stats.keys():
			if String(k).begins_with("shadows_virtual"):
				lines.append("%s %s" % [k, stats[k]])
		var f := FileAccess.open(out.path_join(v + ".txt"), FileAccess.WRITE)
		f.store_string("\n".join(lines) + "\n")
		print("vsm_look: ", v, " ", " ".join(lines))
	quit()
