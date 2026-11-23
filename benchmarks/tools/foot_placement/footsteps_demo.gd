extends SceneTree
## FellowEngine's footsteps (milestone 181, decision 0323): a CMU mocap walk (its root motion driving a CharacterBody3D)
## along a path of strips — stone, a wooden floor, a steel deck, tiles, carpet, gravel, sand, snow, grass, dry leaves,
## dirt, mud, a puddle — a FellowFootsteps3D last on its skeleton hearing each heel, the sole rolling onto the ball,
## the toe lifting, on what the ray under the foot finds (the strips: a FellowContactSound3D's material for the hard
## ones, "fellow_surface" metadata for the rest). The footstep signal's events printed ("STEP t foot ground speed").
## A stick figure drawn on the bones; the camera beside it. Record it with Movie Maker:
##
##   VK_ICD_FILENAMES=.../lvp_icd.json xvfb-run -a godot --path benchmarks/tools/foot_placement --rendering-driver vulkan \
##       --resolution 960x540 --fixed-fps 30 --write-movie <dir>/m.png -s footsteps_demo.gd -- --data=<cmu dir> \
##       [--clip=35_08] [--seconds=24] [--shoe=0]
##
## (--data: a folder with CMU's BVH files in 016/, 035/ or 069/; --timing: the viewport's render times; --silent;
## --terrain: a FellowTerrain3D's grass blending into gravel instead of the strips.)

const CMU_SCALE := 0.056444
const STRIP := 3.0
const GROUNDS := [["stone", 2, Color(0.55, 0.55, 0.53)], ["wood", 0, Color(0.55, 0.38, 0.22)], ["metal", 1, Color(0.5, 0.52, 0.55)],
		["tile", 6, Color(0.85, 0.85, 0.8)], ["carpet", -1, Color(0.5, 0.15, 0.15)], ["gravel", -1, Color(0.47, 0.45, 0.42)],
		["sand", -1, Color(0.85, 0.75, 0.5)], ["snow", -1, Color(0.95, 0.96, 0.98)], ["grass", -1, Color(0.3, 0.5, 0.2)],
		["leaves", -1, Color(0.6, 0.38, 0.15)], ["dirt", -1, Color(0.4, 0.3, 0.2)], ["mud", -1, Color(0.3, 0.22, 0.14)],
		["water", -1, Color(0.3, 0.4, 0.45)]]

var _data := ""
var _clip := "35_08"
var _seconds := 24.0
var _shoe := 0
var _timing := false
var _silent := false
var _frame := 0
var _anim: Animation
var _lib := AnimationLibrary.new()
var _bvh: Dictionary
var _body: CharacterBody3D
var _skel: Skeleton3D
var _player: AnimationPlayer
var _steps: Node
var _cam: Camera3D
var _vy := 0.0
var _gpu_ms := []
var _cpu_ms := []
var _count := {}
var _time := 0.0
var _terrain := false
# The clip looped where its gait matches (the right foot down, the left lifting): from 3.70 s back to 0.42 s.
const LOOP_FROM := 3.70
const LOOP_TO := 0.42


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		var v := a.get_slice("=", 1)
		if a.begins_with("--data="):
			_data = v
		elif a.begins_with("--clip="):
			_clip = v
		elif a.begins_with("--seconds="):
			_seconds = float(v)
		elif a.begins_with("--shoe="):
			_shoe = int(v)
		elif a == "--timing":
			_timing = true
		elif a == "--silent":
			_silent = true
		elif a == "--terrain":
			_terrain = true
	if not _load_clip():
		quit(1)
		return
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	e.sky = sky
	e.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	e.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.environment = e
	root.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50.0, -30.0, 0.0)
	sun.shadow_enabled = true
	root.add_child(sun)
	if _terrain:
		_build_terrain()
	else:
		for i in GROUNDS.size():
			_strip(i)
	_make_character()
	_cam = Camera3D.new()
	_cam.fov = 50.0
	root.add_child(_cam)


func _load_clip() -> bool:
	for sub in ["016", "035", "069"]:
		var f := _data.path_join(sub).path_join(_clip + ".bvh")
		if FileAccess.file_exists(f):
			_bvh = ClassDB.class_call_static("FellowMotionDatabase", "import_bvh", f, CMU_SCALE, true)
			break
	if not _bvh.has("animation"):
		printerr("footsteps: no %s.bvh in %s" % [_clip, _data])
		return false
	_anim = _bvh["animation"]
	_split_root(_anim)
	_lib.add_animation(_clip, _anim)
	return true


## A game rig's root bone from the mocap's hips: Root on the ground under the hips, turned with their heading.
func _split_root(p_anim: Animation) -> void:
	var pt := p_anim.find_track(NodePath("Skeleton3D:Hips"), Animation.TYPE_POSITION_3D)
	var rt := p_anim.find_track(NodePath("Skeleton3D:Hips"), Animation.TYPE_ROTATION_3D)
	var root_pt := p_anim.add_track(Animation.TYPE_POSITION_3D)
	p_anim.track_set_path(root_pt, NodePath("Skeleton3D:Root"))
	var root_rt := p_anim.add_track(Animation.TYPE_ROTATION_3D)
	p_anim.track_set_path(root_rt, NodePath("Skeleton3D:Root"))
	for k in p_anim.track_get_key_count(pt):
		var t := p_anim.track_get_key_time(pt, k)
		var p: Vector3 = p_anim.track_get_key_value(pt, k)
		var q: Quaternion = p_anim.track_get_key_value(rt, k)
		var fwd := q * Vector3(0, 0, 1)
		var yaw := Quaternion(Vector3.UP, atan2(fwd.x, fwd.z))
		var ground := Vector3(p.x, 0.0, p.z)
		p_anim.position_track_insert_key(root_pt, t, ground)
		p_anim.rotation_track_insert_key(root_rt, t, yaw)
		p_anim.track_set_key_value(pt, k, yaw.inverse() * (p - ground))
		p_anim.track_set_key_value(rt, k, yaw.inverse() * q)


## Strip i along +z: a hard ground by its FellowContactSound3D's material, the rest by "fellow_surface" metadata.
func _strip(p_i: int) -> void:
	var g: Array = GROUNDS[p_i]
	var b := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var bs := BoxShape3D.new()
	bs.size = Vector3(4.0, 0.2, STRIP)
	cs.shape = bs
	b.add_child(cs)
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = bs.size
	mi.mesh = bm
	var m := StandardMaterial3D.new()
	m.albedo_color = g[2]
	m.roughness = 0.3 if g[0] == "water" or g[0] == "tile" else 0.9
	m.metallic = 0.8 if g[0] == "metal" else 0.0
	mi.material_override = m
	b.add_child(mi)
	b.position = Vector3(0.0, -0.1, 1.5 + STRIP * p_i)
	root.add_child(b)
	if int(g[1]) >= 0:
		var snd: Node = ClassDB.instantiate("FellowContactSound3D")
		snd.set("material", int(g[1]))
		b.add_child(snd)
	else:
		b.set_meta("fellow_surface", g[0])
	var label := Label3D.new()
	label.text = g[0]
	label.font_size = 64
	label.pixel_size = 0.004
	label.position = Vector3(1.6, 0.12, 1.5 + STRIP * p_i)
	label.rotation_degrees = Vector3(-90.0, 90.0, 0.0)
	root.add_child(label)


## --terrain: a flat FellowTerrain3D, grass (its base layer) blending into gravel (layer 1) from z 4 to 14: the steps
## hear its layers' surfaces by their weights.
func _build_terrain() -> void:
	var t: Node3D = ClassDB.instantiate("FellowTerrain3D")
	var h := Image.create(64, 64, false, Image.FORMAT_RF)
	h.fill(Color(0, 0, 0))
	var splat := Image.create(64, 64, false, Image.FORMAT_RGBA8)
	# The terrain's first vertex at its origin: placed so that it spans x -32..31, z -8..55.
	for y in 64:
		var z := float(y) - 8.0
		var w := clampf((z - 4.0) / 10.0, 0.0, 1.0)
		for x in 64:
			splat.set_pixel(x, y, Color(w, 0, 0, 0))
	var grass = ClassDB.instantiate("FellowTerrainLayer")
	grass.set("albedo_color", Color(0.3, 0.5, 0.2))
	grass.set("surface", 8)
	var gravel = ClassDB.instantiate("FellowTerrainLayer")
	gravel.set("albedo_color", Color(0.5, 0.48, 0.45))
	gravel.set("surface", 5)
	t.set("layers", [grass, gravel])
	t.set("vertex_spacing", 1.0)
	t.set("heightmap", h)
	t.set("splat_map", splat)
	t.position = Vector3(-32.0, 0.0, -8.0)
	root.add_child(t)


func _make_character() -> void:
	_body = CharacterBody3D.new()
	_body.collision_layer = 2
	_body.collision_mask = 1
	_body.floor_snap_length = 0.3
	var cs := CollisionShape3D.new()
	var cap := CapsuleShape3D.new()
	cap.radius = 0.3
	cap.height = 1.8
	cs.shape = cap
	cs.position = Vector3(0, 0.9, 0)
	_body.add_child(cs)
	root.add_child(_body)
	_body.position = Vector3(0.0, 0.0, 0.3) if not _terrain else Vector3(0.0, 0.0, -2.0)
	_skel = ClassDB.class_call_static("FellowMotionDatabase", "create_skeleton", _bvh)
	_skel.name = "Skeleton3D"
	var root_bone := _skel.add_bone("Root")
	_skel.set_bone_parent(_skel.find_bone("Hips"), root_bone)
	_body.add_child(_skel)
	_player = AnimationPlayer.new()
	_body.add_child(_player)
	_player.root_node = NodePath("..")
	_player.add_animation_library("", _lib)
	_player.root_motion_track = NodePath("Skeleton3D:Root")
	_player.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
	_player.play(_clip)
	_player.seek(LOOP_TO, true)
	_steps = ClassDB.instantiate("FellowFootsteps3D")
	_steps.set("left_foot_bone", "LeftFoot")
	_steps.set("right_foot_bone", "RightFoot")
	_steps.set("left_toe_bone", "LeftToeBase")
	_steps.set("right_toe_bone", "RightToeBase")
	_steps.set("contact_height", 0.05) # CMU's soles stand 3-5 cm over the ground
	_steps.set("shoe", _shoe)
	_steps.set("sound_enabled", not _silent)
	_skel.add_child(_steps)
	_steps.connect("footstep", func(p_foot: int, _at: Vector3, p_ground: int, p_speed: float) -> void:
		var name: String = GROUNDS[_ground_index(p_ground)][0] if _ground_index(p_ground) >= 0 else str(p_ground)
		_count[name] = int(_count.get(name, 0)) + 1
		var st: Dictionary = _steps.call("get_foot_state", p_foot)
		print("STEP %.2f %d %s %.2f (%d %.2f)" % [_time, p_foot, name, p_speed, st.ground2, st.weight2]))
	# A stick figure on the bones.
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.85, 0.6, 0.35)
	for b in _skel.get_bone_count():
		var parent := _skel.get_bone_parent(b)
		if parent < 0 or _skel.get_bone_name(parent) == "Root":
			continue
		var a := _skel.get_bone_global_rest(parent).origin
		var c := _skel.get_bone_global_rest(b).origin
		var length := a.distance_to(c)
		if length < 0.03:
			continue
		var att := BoneAttachment3D.new()
		att.bone_name = _skel.get_bone_name(parent)
		_skel.add_child(att)
		var mi := MeshInstance3D.new()
		var cm := CapsuleMesh.new()
		cm.radius = 0.035
		cm.height = length + 0.07
		mi.mesh = cm
		mi.material_override = mat
		var local := _skel.get_bone_global_rest(parent).affine_inverse() * c
		mi.position = local * 0.5
		var dir := local.normalized()
		if absf(dir.dot(Vector3.UP)) < 0.999:
			mi.basis = Basis(Vector3.UP.cross(dir).normalized(), Vector3.UP.angle_to(dir))
		elif dir.y < 0.0:
			mi.basis = Basis(Vector3.RIGHT, PI)
		att.add_child(mi)


func _ground_index(p_ground: int) -> int:
	var names := ["stone", "wood", "metal", "tile", "carpet", "gravel", "sand", "snow", "grass", "leaves", "dirt", "mud", "water"]
	if p_ground < 0 or p_ground >= names.size():
		return -1
	for i in GROUNDS.size():
		if GROUNDS[i][0] == names[p_ground]:
			return i
	return -1


func _process(p_delta: float) -> bool:
	var dt := p_delta
	_time += dt
	if _player.current_animation_position + dt >= minf(LOOP_FROM, _anim.length):
		_player.play(_clip, 0.1)
		_player.seek(LOOP_TO, false)
	_player.advance(dt)
	var rm := _player.get_root_motion_position()
	var v := _body.global_basis * (rm / dt)
	_vy = 0.0 if _body.is_on_floor() else _vy - 9.8 * dt
	_body.velocity = Vector3(v.x, _vy, v.z)
	_body.move_and_slide()
	var at := _body.global_position
	_cam.look_at_from_position(at + Vector3(3.2, 1.5, -0.6), at + Vector3(0.0, 0.7, 0.4))
	_frame += 1
	if _timing:
		var vp := root.get_viewport_rid()
		if _frame == 1:
			RenderingServer.viewport_set_measure_render_time(vp, true)
		elif _time > 2.0:
			_gpu_ms.append(RenderingServer.viewport_get_measured_render_time_gpu(vp))
			_cpu_ms.append(RenderingServer.viewport_get_measured_render_time_cpu(vp))
	if _time >= _seconds:
		if _timing:
			var g := 0.0
			var c := 0.0
			for x in _gpu_ms:
				g += x
			for x in _cpu_ms:
				c += x
			print("TIMING frames %d gpu_ms_mean %.3f cpu_ms_mean %.3f %s" % [_gpu_ms.size(), g / maxf(1.0, _gpu_ms.size()), c / maxf(1.0, _cpu_ms.size()), "silent" if _silent else "sound"])
		print("steps by ground: ", _count)
		print("footsteps: ", _steps.call("get_sound_stats"))
		print("walked %.1f m, at %s" % [_body.global_position.z, _body.global_position])
		return true
	return false
