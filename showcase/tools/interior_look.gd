extends SceneTree
## The rooms behind the windows (milestone 188, decision 0330) looked at: a facade of three storeys of windows in the
## golden hour's sun, the windows' glass FellowInteriorMaterial3D (or, with --material=flat, the rooftops' old glass:
## dark panes, curtains drawn on them), from the front, at an angle from either side, from the street below and close
## up. Writes <dir>/<view>.png.
##
##   godot --path showcase --rendering-driver vulkan --resolution 1280x720 -s res://tools/interior_look.gd -- \
##       --out=<dir> [--material=interior|flat|proto] [--views=front,left,right,below,close,street] [--lamps=0.3]
##       [--night=1] [--daylight=0.3] [--reflection=0.45] [--house=<a rooftops model>]

const STOREY := 3.2
const SPACING := 2.8
const WIN := Vector2(1.0, 1.7)
const SILL := 0.9
const RECESS := 0.14


func _quad_uv2(st: SurfaceTool, c: Vector3, w: float, h: float) -> void:
	# A window's glass facing +Z, its UV2 the metres right of its middle and up from its room's floor (the material's
	# rooms_from_uv2), the floor a sill's height under it.
	var pts := [Vector2(-w * 0.5, 0.0), Vector2(w * 0.5, 0.0), Vector2(w * 0.5, h), Vector2(-w * 0.5, h)]
	for i in [0, 2, 1, 0, 3, 2]:
		var p: Vector2 = pts[i]
		st.set_normal(Vector3(0, 0, 1))
		st.set_uv(Vector2(c.x + p.x, -(c.y + p.y)))
		st.set_uv2(p + Vector2(0.0, SILL))
		st.add_vertex(c + Vector3(p.x, p.y, 0.0))


func _box(parent: Node3D, size: Vector3, pos: Vector3, mat: Material) -> void:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = size
	mi.mesh = bm
	mi.position = pos
	mi.material_override = mat
	parent.add_child(mi)


func _init() -> void:
	var args := {}
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv := a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	var out := str(args.get("out", "user://interior_look"))
	DirAccess.make_dir_recursive_absolute(out)
	var night := str(args.get("night", "0")) == "1"
	# The project's clouds would shade the facade by chance: off for this look.
	ProjectSettings.set_setting("rendering/fellow/post/clouds/enabled", false)
	var scene := Node3D.new()
	root.add_child(scene)

	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var psky := ProceduralSkyMaterial.new()
	if night:
		psky.sky_top_color = Color(0.02, 0.03, 0.06)
		psky.sky_horizon_color = Color(0.06, 0.06, 0.09)
		psky.ground_horizon_color = Color(0.04, 0.04, 0.05)
		psky.ground_bottom_color = Color(0.02, 0.02, 0.02)
	sky.sky_material = psky
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.ssao_enabled = true
	var we := WorldEnvironment.new()
	we.environment = env
	scene.add_child(we)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-22.0, 32.0, 0.0) if not night else Vector3(-40.0, 120.0, 0.0)
	sun.light_color = Color(1.0, 0.78, 0.55) if not night else Color(0.5, 0.6, 0.9)
	sun.light_energy = 3.0 if not night else 0.05
	sun.shadow_enabled = true
	scene.add_child(sun)

	var plaster := StandardMaterial3D.new()
	plaster.albedo_color = Color(0.78, 0.62, 0.42)
	plaster.roughness = 0.9
	var stone := StandardMaterial3D.new()
	stone.albedo_color = Color(0.72, 0.68, 0.6)
	stone.roughness = 0.85
	var frame := StandardMaterial3D.new()
	frame.albedo_color = Color(0.28, 0.18, 0.1)
	frame.roughness = 0.7

	var kind := str(args.get("material", "interior"))
	var glass: Material
	if kind == "flat":
		glass = load("res://rooftops/materials/glass.tres")
	elif kind == "proto":
		var sm := ShaderMaterial.new()
		sm.shader = load("res://rooftops/shaders/window_interior.gdshader")
		sm.set_shader_parameter("lamp_share", float(args.get("lamps", "0.0")))
		sm.set_shader_parameter("daylight", float(args.get("daylight", "0.05" if night else "0.3")))
		sm.set_shader_parameter("reflection", float(args.get("reflection", "0.45")))
		sm.set_shader_parameter("pane_size", Vector2(0.5, 0.425))
		glass = sm
	else:
		glass = ClassDB.instantiate("FellowInteriorMaterial3D")
		glass.set("lamp_share", float(args.get("lamps", "0.0")))
		glass.set("daylight", float(args.get("daylight", "0.05" if night else "0.3")))
		glass.set("reflection", float(args.get("reflection", "0.45")))
		glass.set("pane_size", Vector2(0.5, 0.425))

	# The facade: three storeys, five bays; the wall built round the openings, the glass set back in them.
	var facade := Node3D.new()
	scene.add_child(facade)
	var cols := 5
	var rows := 3
	var width := cols * SPACING
	var height := rows * STOREY + 0.6
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for r in rows:
		var y0 := r * STOREY
		# Below the sills and above the windows: whole strips.
		_box(facade, Vector3(width, SILL, 0.4), Vector3(0, y0 + SILL * 0.5, -0.2), plaster)
		var top := y0 + SILL + WIN.y
		var next := (r + 1) * STOREY
		_box(facade, Vector3(width, next - top, 0.4), Vector3(0, (top + next) * 0.5, -0.2), plaster)
		# The piers between the windows.
		for c in cols + 1:
			var x0 := -width * 0.5 + (c - 0.5) * SPACING + WIN.x * 0.5
			var x1 := -width * 0.5 + (c + 0.5) * SPACING - WIN.x * 0.5
			x0 = maxf(x0, -width * 0.5)
			x1 = minf(x1, width * 0.5)
			if x1 > x0:
				_box(facade, Vector3(x1 - x0, WIN.y, 0.4), Vector3((x0 + x1) * 0.5, y0 + SILL + WIN.y * 0.5, -0.2), plaster)
		for c in cols:
			var cx := -width * 0.5 + (c + 0.5) * SPACING
			_quad_uv2(st, Vector3(cx, y0 + SILL, -RECESS), WIN.x, WIN.y)
			# A stone sill and the casement's frame: a mullion and a transom.
			_box(facade, Vector3(WIN.x + 0.2, 0.08, 0.3), Vector3(cx, y0 + SILL - 0.04, 0.0), stone)
			_box(facade, Vector3(0.05, WIN.y, 0.05), Vector3(cx, y0 + SILL + WIN.y * 0.5, -RECESS + 0.02), frame)
			_box(facade, Vector3(WIN.x, 0.05, 0.05), Vector3(cx, y0 + SILL + WIN.y * 0.5, -RECESS + 0.02), frame)
	_box(facade, Vector3(width + 0.4, 0.6, 0.6), Vector3(0, rows * STOREY + 0.3, -0.1), stone)
	st.generate_tangents()
	var gm := MeshInstance3D.new()
	gm.mesh = st.commit()
	gm.material_override = glass
	facade.add_child(gm)
	_box(scene, Vector3(60, 0.2, 60), Vector3(0, -0.1, 10), stone)

	if args.has("house"):
		# One of the rooftops' houses instead (its glass the saved material), in front of the facade.
		facade.visible = false
		var house := MeshInstance3D.new()
		house.mesh = load("res://rooftops/models/%s.res" % args["house"])
		house.position = Vector3(0, 0, -6.0)
		scene.add_child(house)
		for i in house.mesh.get_surface_count():
			var m: Material = house.mesh.surface_get_material(i)
			if m and m.get_class() == "FellowInteriorMaterial3D":
				house.set_surface_override_material(i, glass)
	var cam := Camera3D.new()
	cam.fov = 50.0
	scene.add_child(cam)
	cam.current = true
	var views := {
		"front": [Vector3(0, 4.8, 13.0), Vector3(0, 4.8, 0)],
		"left": [Vector3(-9.0, 4.0, 7.0), Vector3(-1.0, 4.6, 0)],
		"right": [Vector3(9.0, 5.5, 6.0), Vector3(1.0, 4.6, 0)],
		"below": [Vector3(1.0, 1.6, 3.5), Vector3(0.5, 8.0, 0)],
		"close": [Vector3(-1.0, 4.6, 2.2), Vector3(SPACING * 0.5 - 1.4, 4.5, 0)],
		"street": [Vector3(-2.5, 1.7, 4.5), Vector3(0.0, 1.8, 0)],
	}
	await process_frame
	var names := str(args.get("views", "front,left,right,below,close,street")).split(",")
	for v in names:
		var p: Array = views[v]
		cam.global_position = p[0]
		cam.look_at(p[1], Vector3.UP)
		for k in 8:
			await process_frame
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png("%s/%s.png" % [out, v])
		print("interior_look: ", v)
	quit()
