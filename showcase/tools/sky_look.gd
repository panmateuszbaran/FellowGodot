extends SceneTree
## The rooftops scene's sky seen from over the square (milestone 182, decision 0324), without the square: the scene's
## sky (its sky shader, environment, Sun, and rooftops.gd's clouds, haze and sky sun) over a plain, a camera 30 m up
## looking towards the sky's sun, across it, away from it, up, and straight up, and from 20 km up straight down (the
## clouds' outlines as a satellite sees them, for benchmarks/tools/sky/sky_metrics.py), each picture after the clouds'
## temporal accumulation has settled.
## --set=<setting>=<value> (repeatable) changes a project setting first (e.g.
## --set=rendering/fellow/post/clouds/realism=false); --scene=<property>=<value> one of rooftops.gd's (e.g.
## --scene=sky_brightness=2); --env=<property>=<value> one of the Environment's (milestone 191, e.g. --env=fog_sky_affect=0.3).
##
##   godot --path showcase --rendering-driver vulkan --resolution 1280x720 --fixed-fps 30 -s res://tools/sky_look.gd -- \
##       --out=<dir> [--views=sun,side,away,up,zenith,sat] [--settle=40]
##
## Writes <dir>/<view>.png; with --frames=<n> also the n frames after it (<dir>/<view>_<i>.png: the clouds' stability
## from frame to frame).

const VIEWS := {
	"sun": [0.0, 12.0, 70.0, 30.0], # the sun's azimuth + x degrees, the pitch, the field of view, the height (m)
	"side": [90.0, 14.0, 70.0, 30.0],
	"away": [180.0, 18.0, 70.0, 30.0],
	"up": [150.0, 55.0, 70.0, 30.0],
	"zenith": [0.0, 90.0, 90.0, 30.0],
	"sat": [0.0, -90.0, 90.0, 20000.0],
	"zoom": [165.0, 38.0, 15.0, 30.0], # a telephoto look at a cloud's surface
}


func _init() -> void:
	var out := "user://sky_look"
	var views := PackedStringArray(["sun", "side", "away", "up", "zenith", "sat"])
	var settle := 40
	var scene_props := {}
	var frames := 0
	var env_props := {}
	var sky_props := {}
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.get_slice("=", 1)
		elif a.begins_with("--views="):
			views = a.get_slice("=", 1).split(",")
		elif a.begins_with("--settle="):
			settle = int(a.get_slice("=", 1))
		elif a.begins_with("--set="):
			var kv := a.substr(6)
			var value = str_to_var(kv.get_slice("=", 1))
			ProjectSettings.set_setting(kv.get_slice("=", 0), value if value != null else kv.get_slice("=", 1))
		elif a.begins_with("--frames="):
			frames = int(a.get_slice("=", 1))
		elif a.begins_with("--scene="):
			var kv := a.substr(8)
			scene_props[kv.get_slice("=", 0)] = str_to_var(kv.get_slice("=", 1))
		elif a.begins_with("--skymat="):
			var kv := a.substr(9)
			sky_props[kv.get_slice("=", 0)] = str_to_var(kv.get_slice("=", 1))
		elif a.begins_with("--env="):
			var kv := a.substr(6)
			env_props[kv.get_slice("=", 0)] = str_to_var(kv.get_slice("=", 1))
	DirAccess.make_dir_recursive_absolute(out)

	# The rooftops scene's sky, as rooftops.tscn sets it up (its square left out).
	var sc := Node3D.new()
	var sun := DirectionalLight3D.new()
	sun.name = "Sun"
	sun.transform = Transform3D(Basis(Vector3(0.6156615, 0.0, 0.7880107), Vector3(0.32051283, 0.9135454, -0.2504121), Vector3(-0.71988356, 0.40673664, 0.56243473)), Vector3())
	sun.light_color = Color(1.0, 0.9, 0.78)
	sun.light_energy = 4.0
	sun.light_angular_distance = 2.5
	sc.add_child(sun)
	var env := Environment.new()
	var sky_mat := ShaderMaterial.new()
	sky_mat.shader = load("res://shaders/atmosphere_sky.gdshader")
	for k in sky_props:
		sky_mat.set_shader_parameter(k, sky_props[k])
	var sky := Sky.new()
	sky.sky_material = sky_mat
	sky.process_mode = Sky.PROCESS_MODE_REALTIME
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.tonemap_exposure = 1.3
	env.tonemap_agx_contrast = 0.95
	env.fog_enabled = true
	env.fog_light_color = Color(0.85, 0.72, 0.58)
	env.fog_density = 0.0008
	env.fog_aerial_perspective = 0.8
	env.adjustment_enabled = true
	env.adjustment_saturation = 0.9
	for k in env_props:
		env.set(k, env_props[k])
	var we := WorldEnvironment.new()
	we.name = "WorldEnvironment"
	we.environment = env
	we.camera_attributes = CameraAttributesPractical.new()
	sc.add_child(we)
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(4000.0, 4000.0)
	var ground_mat := StandardMaterial3D.new()
	ground_mat.albedo_color = Color(0.35, 0.33, 0.28)
	plane.material = ground_mat
	ground.mesh = plane
	sc.add_child(ground)
	sc.set_script(load("res://scripts/rooftops.gd"))
	for k in scene_props:
		sc.set(k, scene_props[k])
	root.add_child(sc)
	await process_frame

	var to_sun := sun.global_basis.z.normalized()
	var sun_azimuth := rad_to_deg(atan2(to_sun.x, to_sun.z))
	var cam := Camera3D.new()
	sc.add_child(cam)
	cam.global_position = Vector3(0.0, 30.0, 0.0)
	cam.far = 4000.0
	cam.make_current()
	for v in views:
		var spec: Array = VIEWS[v]
		var az := deg_to_rad(sun_azimuth + spec[0])
		var pitch := deg_to_rad(spec[1])
		cam.fov = spec[2]
		cam.global_position = Vector3(0.0, spec[3], 0.0)
		ground.visible = spec[3] < 1000.0 # from above, the planet's ground (the sky's) under the clouds
		var dir := Vector3(sin(az) * cos(pitch), sin(pitch), cos(az) * cos(pitch))
		var up := Vector3.UP if absf(dir.y) < 0.99 else Vector3(sin(az), 0.0, cos(az))
		cam.global_basis = Basis.looking_at(dir, up)
		for k in settle:
			await process_frame
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png("%s/%s.png" % [out, v])
		for i in frames:
			await RenderingServer.frame_post_draw
			root.get_texture().get_image().save_png("%s/%s_%02d.png" % [out, v, i])
		print("sky_look: %s" % v)
	quit()
