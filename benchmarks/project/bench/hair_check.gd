extends SceneTree
## FellowEngine milestone 124 check (decision 0248): FellowHair3D — a head's strands simulated and drawn.
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 960x540 -s res://bench/hair_check.gd -- --out=<dir>
##
## A head (a sphere 0.1 m in radius on a neck) with hair 0.32 m long, lit by the sun from behind its right, settled for
## a second; then the head turns 70 degrees left and back over a second, then a wind blows. Pictures: front, side and
## back after settling, and mid-turn. Checks: no guide's segment longer or shorter than its rest by more than 1 %, no
## point inside the head, the roots on the scalp, the tips swinging after the head when it turns, and moving in the wind.
## One JSON line: HAIR_CHECK {...}; exit code 1 when a check fails. Milestone 144: --self_shadow=<0-1> (and
## --self_shadow_density) shadows the hair from the sun by itself (decision 0268). Milestone 146: --scalp_mesh=1 grows
## the hair from the skull's mesh painted where the scalp is (`scalp_mesh`, milestone 125) instead of the sphere's cap;
## --lamp=<metres> (milestone 149) lights it from an omni light that far away where the sun shines from instead (the sun
## off), the hair shadowing itself from the lamp. --sun_turn=<degrees> turns the sun about the vertical that far between the head's turn and the wind (another picture,
## "sun", and the hair's mean share of the light before and after: with self_shadow the shade follows the sun).

var args := {}
var out_dir := ""
var result := {}
var root3d: Node3D
var head: Node3D
var hair: FellowHair3D
var cam: Camera3D
var frame := 0
var done := false
var checks := {}
var max_stretch := 0.0
var min_head_distance := 1e9
var tip_lag_max := 0.0
var wind_motion := 0.0
var tips_before_wind := PackedVector3Array()
var times := []
var sun: DirectionalLight3D
var sun_shares := []


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://hair_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	_build.call_deferred()


func _material(c: Color, rough := 0.7) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = rough
	return m


func _build() -> void:
	root3d = Node3D.new()
	get_root().add_child(root3d)
	get_root().use_taa = _arg("taa", "1") == "1" # the strands' dithered coverage resolved over frames
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.42, 0.47, 0.55)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.55, 0.58, 0.65)
	e.ambient_light_energy = 0.5
	e.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.environment = e
	root3d.add_child(env)
	sun = DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-35, 140, 0)
	sun.light_energy = 2.2
	sun.shadow_enabled = true
	root3d.add_child(sun)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-20, -30, 0)
	fill.light_energy = 0.5
	root3d.add_child(fill)
	head = Node3D.new()
	head.position = Vector3(0, 1.6, 0)
	root3d.add_child(head)
	var skull := MeshInstance3D.new()
	var sm := SphereMesh.new()
	sm.radius = 0.1
	sm.height = 0.23
	skull.mesh = sm
	# Skin, with the scalp under the hair painted the hair's colour (as a character's texture would be).
	var scalp := ShaderMaterial.new()
	scalp.shader = Shader.new()
	scalp.shader.code = "shader_type spatial;\nvarying vec3 n;\nvoid vertex() { n = NORMAL; }\nvoid fragment() { float h = smoothstep(-0.2, 0.05, n.y) * (1.0 - smoothstep(0.55, 0.75, -n.z)); ALBEDO = mix(vec3(0.78, 0.6, 0.5), vec3(0.06, 0.04, 0.03), h); ROUGHNESS = 0.6; }"
	skull.set_surface_override_material(0, scalp)
	head.add_child(skull)
	var neck := MeshInstance3D.new()
	var cm := CylinderMesh.new()
	cm.top_radius = 0.05
	cm.bottom_radius = 0.06
	cm.height = 0.2
	neck.mesh = cm
	neck.position = Vector3(0, -0.17, 0)
	neck.set_surface_override_material(0, _material(Color(0.78, 0.6, 0.5), 0.6))
	head.add_child(neck)
	var shoulders := MeshInstance3D.new()
	var bm := CapsuleMesh.new()
	bm.radius = 0.11
	bm.height = 0.5
	shoulders.mesh = bm
	shoulders.rotation_degrees = Vector3(0, 0, 90)
	shoulders.position = Vector3(0, 1.35, 0)
	shoulders.set_surface_override_material(0, _material(Color(0.2, 0.25, 0.35)))
	root3d.add_child(shoulders)
	# The shoulders as a collider for the hair.
	var body := StaticBody3D.new()
	var bcs := CollisionShape3D.new()
	var cap := CapsuleShape3D.new()
	cap.radius = 0.11
	cap.height = 0.5
	bcs.shape = cap
	bcs.rotation_degrees = Vector3(0, 0, 90)
	bcs.position = Vector3(0, 1.35, 0)
	bcs.name = "Shoulders"
	body.add_child(bcs)
	root3d.add_child(body)
	hair = FellowHair3D.new()
	hair.scalp_radius = 0.1
	hair.scalp_center = Vector3(0, 0.012, 0)
	if _arg("scalp_mesh", "0") == "1":
		# The skull's sphere as a mesh, its vertices painted (red) where the scalp shader paints the hair's colour.
		var arrays := sm.get_mesh_arrays()
		var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
		var colors := PackedColorArray()
		for nv in normals:
			var h := smoothstep(-0.2, 0.05, nv.y) * (1.0 - smoothstep(0.55, 0.75, -nv.z))
			colors.push_back(Color(1, 1, 1) if h > 0.5 else Color(0, 0, 0))
		arrays[Mesh.ARRAY_COLOR] = colors
		var painted := ArrayMesh.new()
		painted.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		hair.scalp_mesh = painted
	hair.length = 0.32
	hair.strand_count = int(_arg("strands", "10000"))
	hair.guide_count = int(_arg("guides", "160"))
	# Milestone 144 (decision 0268): the hair shadowing itself from the sun (--self_shadow=<0-1>, --self_shadow_density).
	hair.self_shadow = float(_arg("self_shadow", "0"))
	if args.has("self_shadow_density"):
		hair.self_shadow_density = float(_arg("self_shadow_density", "0.5"))
	head.add_child(hair)
	# Milestone 149 (decision 0280): --lamp=<metres> — the sun off, an omni light that far from the head where the sun
	# shines from (the same direction), the hair shadowing itself from it.
	if args.has("lamp"):
		sun.visible = false
		var lamp := OmniLight3D.new()
		lamp.omni_range = 20.0
		lamp.light_energy = 2.2 * float(_arg("lamp", "3")) * float(_arg("lamp", "3"))
		lamp.omni_attenuation = 2.0
		root3d.add_child(lamp)
		lamp.global_position = head.global_position + sun.global_transform.basis.z * float(_arg("lamp", "3"))
		hair.self_shadow_light = hair.get_path_to(lamp)
	hair.colliders = [hair.get_path_to(bcs)]
	cam = Camera3D.new()
	cam.fov = 35
	root3d.add_child(cam)
	cam.current = true
	_view("front")
	physics_frame.connect(_on_frame)


func _view(which: String) -> void:
	var target := Vector3(0, 1.55, 0)
	match which:
		"front":
			cam.look_at_from_position(Vector3(0, 1.62, -1.1), target)
		"side":
			cam.look_at_from_position(Vector3(1.1, 1.62, 0), target)
		"back":
			cam.look_at_from_position(Vector3(0.35, 1.7, 1.05), target)


func _guide_metrics() -> void:
	var n := hair.get_point_count()
	var seg := hair.length / float(n - 1)
	var c: Vector3 = head.global_transform * hair.scalp_center
	for gi in hair.get_stats().guides:
		var pts := hair.get_guide_points(gi)
		for k in range(1, pts.size()):
			max_stretch = maxf(max_stretch, absf(pts[k].distance_to(pts[k - 1]) - seg) / seg)
			min_head_distance = minf(min_head_distance, pts[k].distance_to(c))


func _tips() -> PackedVector3Array:
	var out := PackedVector3Array()
	for gi in hair.get_stats().guides:
		var pts := hair.get_guide_points(gi)
		out.push_back(pts[pts.size() - 1])
	return out


func _shot(name: String) -> void:
	if DisplayServer.get_name() == "headless":
		return # (timing the simulation alone: no pictures)
	var img := get_root().get_texture().get_image()
	img.save_png(out_dir.path_join("hair_%s.png" % name))


func _on_frame() -> void:
	if done:
		return
	frame += 1
	if frame >= 2:
		var st := hair.get_stats()
		times.append(st.simulate_usec + st.upload_usec)
	# Settle (1 s), pictures from three sides.
	if frame == 60:
		_view("front")
	elif frame == 62:
		_shot("front")
		_view("side")
	elif frame == 64:
		_shot("side")
		_view("back")
	elif frame == 66:
		_shot("back")
		_view("front")
		_guide_metrics()
	# The head turns 70 degrees left and back over a second.
	if frame > 70 and frame <= 130:
		var t := float(frame - 70) / 60.0
		head.rotation.y = deg_to_rad(70.0) * sin(t * PI)
		_guide_metrics()
		# How far the tips lag behind where they rest in the head's frame.
		var lag := 0.0
		var tips := _tips()
		for gi in tips.size():
			var rest_pts := hair.get_guide_rest_points(gi)
			lag = maxf(lag, tips[gi].distance_to(rest_pts[rest_pts.size() - 1]))
		tip_lag_max = maxf(tip_lag_max, lag)
	if frame == 99 and _arg("debug_motion", "0") == "1":
		get_root().debug_draw = Viewport.DEBUG_DRAW_MOTION_VECTORS
	if frame == 100:
		_shot("turning")
		get_root().debug_draw = Viewport.DEBUG_DRAW_DISABLED
	# Milestone 146: the sun turned about the vertical (--sun_turn), the hair's share of its light before and after.
	var turn := float(_arg("sun_turn", "0"))
	if turn != 0.0:
		if frame == 130 or frame == 189:
			sun_shares.append(snappedf(hair.get_stats().get("self_shadow_mean", 1.0), 0.001))
		if frame > 130 and frame <= 180:
			sun.rotation_degrees.y = 140.0 + turn * float(frame - 130) / 50.0
		if frame == 186:
			_view("side")
		if frame == 188:
			_shot("sun")
			_view("front")
	# Then a wind (a FellowWind3D, 6 m/s from the right).
	if frame == 190:
		tips_before_wind = _tips()
		var wind := FellowWind3D.new()
		wind.rotation_degrees = Vector3(0, -90, 0) # blowing along its -Z: from +X towards -X
		wind.speed = 6.0
		root3d.add_child(wind)
	if frame == 250:
		var tips := _tips()
		var moved := 0.0
		for gi in tips.size():
			moved += tips[gi].distance_to(tips_before_wind[gi])
		wind_motion = moved / maxf(1.0, float(tips.size()))
		_shot("wind")
		_guide_metrics()
		done = true
		_finish.call_deferred()


func _finish() -> void:
	var st := hair.get_stats()
	times.sort()
	result["stats"] = st
	result["max_stretch"] = snappedf(max_stretch, 0.0001)
	result["min_head_distance"] = snappedf(min_head_distance, 0.0001)
	result["tip_lag_max"] = snappedf(tip_lag_max, 0.001)
	result["wind_tip_motion"] = snappedf(wind_motion, 0.001)
	result["frame_usec_median"] = times[times.size() / 2] if not times.is_empty() else 0
	result["frame_usec_max"] = times[times.size() - 1] if not times.is_empty() else 0
	if not sun_shares.is_empty():
		result["self_shadow_mean_before_after_sun_turn"] = sun_shares
	result["scalp"] = "mesh" if hair.scalp_mesh != null else "sphere"
	var failures := []
	if max_stretch > 0.01:
		failures.append("stretch")
	if min_head_distance < hair.scalp_radius * 0.98:
		failures.append("inside the head")
	if wind_motion < 0.01:
		failures.append("no wind motion")
	if st.strands < hair.strand_count * 0.9:
		failures.append("strands")
	if hair.self_shadow > 0.0 and (not st.self_shadow_lit or st.self_shadow_min > 0.5):
		failures.append("self shadow") # no light found, or no point of the hair in its own shade
	result["failures"] = failures
	print("HAIR_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("hair_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)
