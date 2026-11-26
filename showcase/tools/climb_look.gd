extends SceneTree
## Milestone 196: the character's climbing filmed (the owner: the climbing and free-running animations must be the best
## they can be). The demo plays from a shot (`-- --demo --demo_from=house`: the run at CasaCrema, the climb up its front
## from hold to hold, over the eave and up the tiles), the frame rate fixed (pass --fixed-fps 60); every --every-th
## frame is saved (<dir>/f%04d.png) from a camera of its own: "side" beside the body along the wall (the limbs against
## the stone), "back" behind and below it, or "demo" (the demo's own cameras). Each frame's state goes to
## <dir>/frames.json (the player's state, the traversal's, the body's place, the hands' and feet's distances to their
## targets, the elbows' and knees' bend directions) for climb_metrics.py.
##
##   godot --path showcase --resolution 960x540 --fixed-fps 60 -s res://tools/climb_look.gd -- --demo --demo_from=house \
##       --out=<dir> [--seconds=30] [--skip=0] [--every=2] [--cam=side] [--lite=1] [--mocap_all=1] [--images=0]
##   godot ... -s res://tools/climb_look.gd -- --program=hang --out=<dir> [--seconds=18] ...
##   godot ... -s res://tools/climb_look.gd -- --program=course --out=<dir> [--seconds=10] ...
##   godot ... -s res://tools/climb_look.gd -- --program=keys --keys_yaw=<deg> --out=<dir> [--seconds=6] ...
##   godot ... -s res://tools/climb_look.gd -- --program=fountain --out=<dir> [--seconds=5] ...   (a run at its middle)
##   godot ... -s res://tools/climb_look.gd -- --program=bump --bump=walk|run|sprint --out=<dir> [--seconds=8] ...
##       (milestone 208: from 6 m at one of the talkers in the square's middle, into them and on; its reactions printed)
##   godot ... -s res://tools/climb_look.gd -- --program=people --out=<dir> [--dist=2.6] ...
##       (milestone 209: beside a walker in a skirt — the cloth — the player out of the way; --people_side=-1)
##   godot ... -s res://tools/climb_look.gd -- --program=hay --out=<dir> [--dist=4.2] [--hay_from=<deg>] [--hay_turn=<deg/s>] ...
##       (milestone 210: round the hay cart)
##   godot ... -s res://tools/climb_look.gd -- --program=weather --view=square|brazier|chimney [--weather=<1-7>] \
##       [--weather_seq=<key>@<s>,...] [--weather_transition=<s>] --out=<dir> ...   (milestone 211: the weather, the
##       brazier, the chimneys' smoke from a still camera; the weather turned to each key at its second, over
##       WeatherKeys' transition or the one given; the brazier's and the weather's state printed each second)
##   godot ... -s res://tools/climb_look.gd -- --program=osteria --view=door|inside|walkin --out=<dir> ...   (milestone
##       213: CasaRosa's osteria — from the square through its arch, inside, or the player walking in; its state
##       printed)
## --drop=<node>,...: the scene's top-level nodes left out (an A/B). --setting=<project setting>:<value> for this run.
##   godot ... -s res://tools/climb_look.gd -- --program=talk [--talk_to=<group>] --out=<dir> [--seconds=10] ...
##       (milestone 212: the player walks up to one of a group and talks — E at 1.4 s, then every 2.6 s — the camera over
##       the player's shoulder; the crowd's social counts printed)
##   godot ... -s res://tools/climb_look.gd -- --program=lookup --demo --demo_from=house --out=<dir> [--seconds=16] ...
##       (milestone 212: the demo's climb up CasaCrema's front; the camera on the square behind the people looking up
##       and pointing) --draw_all=1: every frame drawn (saved as --every
## says): what runs on the GPU while drawing goes on between the saved frames.
##
## --cam=hand: the hands on the edge from out and above; --cam=handside: the left hand in profile from along the edge
## (milestone 198). --warm=<n>: the n frames before --skip drawn too, not saved (the exposure settled).
## --images=0 (headless): frames.json alone. --jpg=1: the frames as JPEG (f%04d.jpg). --program=hang (no demo): at CasaCrema's front up to its first hold, hanging
## still for 4 s, along the hold each way, still again, then on up from hold to hold.
##
## --lite=1 keeps the square's houses and CasaCrema (no city beyond, no country, no people: lavapipe's whole rooftops
## outgrows 6 GB); --mocap_all=1 plays every clip of the owner's Mixamo library (rooftops_player.gd's MOCAP_KEEP_OWN too).

const KEEP := ["PalazzoOcra", "Campanile", "CasaCrema", "PalazzoSiena", "CasaBianca", "CasaOcraBassa", "CasaRosa",
		"CasaRiferimento", "Fountain", "Near4CasaCrema"]
const LIMBS := [["LeftUpperArm", "LeftLowerArm", "LeftHand"], ["RightUpperArm", "RightLowerArm", "RightHand"],
		["LeftUpperLeg", "LeftLowerLeg", "LeftFoot"], ["RightUpperLeg", "RightLowerLeg", "RightFoot"]]

var _out := "user://climb_look"
var _seconds := 30.0
var _skip := 0
var _every := 2
var _warm := 0 # milestone 198: frames drawn (not saved) before the first saved one
var _cam_mode := "side"
var _lite := false
var _images := true
var _jpg := false
var _dist := 3.0 # the camera's distance from the body
var _program := "" # "hang": at CasaCrema's front, up to its first hold, hanging still, along it each way, on up
var _player: CharacterBody3D
var _skel: Skeleton3D
var _cam: Camera3D
var _cam_pos := Vector3()
var _look := Vector3()
var _frames: Array = []
var _posed := {} # the bones as drawn (after the skeleton's modifiers: read when it says it is updated)
var _posed_xf := {} # milestone 198: the grip's bones' transforms as drawn
const BONES := ["Hips", "LeftUpperArm", "LeftLowerArm", "LeftHand", "RightUpperArm", "RightLowerArm", "RightHand", "LeftUpperLeg",
		"LeftLowerLeg", "LeftFoot", "RightUpperLeg", "RightLowerLeg", "RightFoot", "Head"]
## Milestone 198: the bones of a hand's grip (each side's), their transforms kept for the fingers' tips.
const GRIP_BONES := ["MiddleProximal", "MiddleIntermediate", "ThumbMetacarpal", "ThumbProximal", "Hand", "LowerArm"]


func _init() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			_out = a.get_slice("=", 1)
		elif a.begins_with("--seconds="):
			_seconds = float(a.get_slice("=", 1))
		elif a.begins_with("--skip="):
			_skip = int(a.get_slice("=", 1))
		elif a.begins_with("--every="):
			_every = maxi(1, int(a.get_slice("=", 1)))
		elif a.begins_with("--warm="):
			_warm = maxi(0, int(a.get_slice("=", 1)))
		elif a.begins_with("--cam="):
			_cam_mode = a.get_slice("=", 1)
		elif a == "--lite=1":
			_lite = true
		elif a == "--images=0":
			_images = false
		elif a == "--jpg=1":
			_jpg = true
		elif a.begins_with("--dist="):
			_dist = float(a.get_slice("=", 1))
		elif a.begins_with("--program="):
			_program = a.get_slice("=", 1)
		elif a.begins_with("--keys_yaw="):
			_keys_yaw = float(a.get_slice("=", 1))
		elif a.begins_with("--people_side="):
			_people_side = float(a.get_slice("=", 1))
		elif a.begins_with("--hay_from="):
			_hay_from = float(a.get_slice("=", 1))
		elif a.begins_with("--hay_turn="):
			_hay_turn = float(a.get_slice("=", 1))
		elif a.begins_with("--setting="):
			# (a project setting for this run: --setting=rendering/fellow/...:value)
			var kv := a.get_slice("=", 1)
			var key := kv.get_slice(":", 0)
			var val := kv.get_slice(":", 1)
			ProjectSettings.set_setting(key, (val == "true") if val in ["true", "false"] else float(val))
		elif a == "--omni_dp=1":
			_omni_dp = true
		elif a.begins_with("--env_set="):
			_env_set = a.get_slice("=", 1).split(",")
		elif a == "--perf=1":
			_perf = true
			_draw_all = true
		elif a == "--passes=1":
			_perf = true
			_passes = true
			_draw_all = true
		elif a == "--draw_all=1":
			_draw_all = true
		elif a.begins_with("--drop="):
			_drop = a.get_slice("=", 1).split(",")
		elif a.begins_with("--talk_to="):
			_talk_group = int(a.get_slice("=", 1))
		elif a == "--crowd=1":
			_crowd_in = true
		elif a.begins_with("--view="):
			_view = a.get_slice("=", 1)
		elif a.begins_with("--weather_transition="):
			_weather_transition = float(a.get_slice("=", 1))
		elif a.begins_with("--weather_seq="):
			for step in a.get_slice("=", 1).split(","):
				_weather_seq.append([int(step.get_slice("@", 0)), float(step.get_slice("@", 1))])
		elif a.begins_with("--bump="):
			_bump_pace = a.get_slice("=", 1)
		elif a.begins_with("--keys_turn="):
			_keys_turn = float(a.get_slice("=", 1))
		elif a.begins_with("--keys_sprint="):
			_keys_sprint = a.get_slice("=", 1) == "1"
		elif a.begins_with("--keys_always="):
			_keys_always = a.get_slice("=", 1) == "1"
	DirAccess.make_dir_recursive_absolute(_out)
	var sc: Node = (load("res://rooftops/rooftops.tscn") as PackedScene).instantiate()
	if _lite:
		for n in ["Skyline", "CityHouses", "Landscape", "Trees_cypress", "Trees_olive", "Trees_pine", "Trees_broadleaf",
				"Villas", "Trees_villa_cypress", "Birds", "Crowd", "PipelineWarmer"]:
			if n == "Crowd" and (_program in ["bump", "people", "talk", "lookup", "osteria"] or _program == "weather" and _crowd_in):
				continue # (milestone 208: the people kept to be run into)
			var c := sc.get_node_or_null(n)
			if c:
				sc.remove_child(c)
				c.free()
		var town := sc.get_node("Town")
		for c in town.get_children():
			if not (c.name in KEEP) and not (_program == "hay" and c.name == "HayCart") and not (_program in ["weather", "osteria"] and c.name in WEATHER_KEEP):
				town.remove_child(c)
				c.free()
	for n in _drop:
		# (--drop=<node path>,...: nodes of the scene left out — an A/B)
		var c := sc.get_node_or_null(n)
		if c:
			c.get_parent().remove_child(c)
			c.free()
	for kv in _env_set:
		for n in sc.find_children("*", "WorldEnvironment", true, false):
			var val := kv.get_slice(":", 1)
			(n as WorldEnvironment).environment.set(kv.get_slice(":", 0), (val == "true") if val in ["true", "false"] else float(val))
	if _lite:
		# (the light simplified for lavapipe: no SDFGI, SSIL or volumetric fog — the motion is what is looked at)
		for n in sc.find_children("*", "WorldEnvironment", true, false):
			var env := (n as WorldEnvironment).environment
			env.sdfgi_enabled = false
			env.ssil_enabled = false
			env.volumetric_fog_enabled = false
			env.ambient_light_energy = 1.6
	root.add_child(sc)
	_player = sc.get_node("Player")
	_skel = _player.get_node("Model/Mannequin/Skeleton3D")
	_skel.skeleton_updated.connect(_on_posed)
	await process_frame
	if _omni_dp:
		# (milestone 219: every shadowed omni light — the fire lights of the gas volumes too — dual paraboloid)
		for l in sc.find_children("*", "OmniLight3D", true, false):
			(l as OmniLight3D).omni_shadow_mode = OmniLight3D.SHADOW_DUAL_PARABOLOID
		for g in sc.find_children("*", "FellowGasVolume3D", true, false):
			var fl := g.call("get_fire_light_node") as OmniLight3D
			if fl:
				fl.omni_shadow_mode = OmniLight3D.SHADOW_DUAL_PARABOLOID
	if _cam_mode != "demo":
		_cam = Camera3D.new()
		_cam.fov = 50.0
		sc.add_child(_cam)
	if _program == "hang":
		_player.set("autopilot", true)
		_player.global_position = Vector3(13.1, 0.0, -9.6)
		_player.set("facing", 0.0)
		_player.rotation.y = 0.0
	elif _program == "course":
		_course(sc)
	elif _program == "keys":
		_player.set("autopilot", true)
		_player.global_position = Vector3(13.1, 0.0, -9.6)
		_player.set("facing", 0.0)
		_player.rotation.y = 0.0
	elif _program == "bump":
		_bump_setup(sc)
	elif _program == "people":
		_people_setup(sc)
	elif _program == "hay":
		_player.set("autopilot", true)
		_player.global_position = Vector3(-30.0, 0.0, -30.0)
		_hay_cart = sc.get_node_or_null("Town/HayCart")
	elif _program == "talk":
		_talk_setup(sc)
	elif _program == "lookup":
		pass # (the demo plays: pass --demo --demo_from=house)
	elif _program == "osteria":
		_player.set("autopilot", true)
		_osteria = sc.get_node_or_null("Town/CasaRosa/Osteria")
		_weather_keys = sc.get_node_or_null("WeatherKeys")
		_brazier = sc.get_node_or_null("Brazier")
		if _view == "walkin":
			_player.global_position = Vector3(-6.45, 0.0, 9.2)
			_player.set("facing", PI)
			_player.rotation.y = PI
		else:
			_player.global_position = Vector3(-30.0, 0.0, -30.0)
	elif _program == "weather":
		_player.set("autopilot", true)
		_player.global_position = Vector3(-30.0, 0.0, -30.0)
		_weather_keys = sc.get_node_or_null("WeatherKeys")
		_brazier = sc.get_node_or_null("Brazier")
	elif _program == "fountain":
		_player.set("autopilot", true)
		var c := (sc.get_node("Town/Fountain") as Node3D).global_position
		_player.global_position = c + Vector3(5.0, 0.0, 0.3)
		_player.set("facing", PI * 0.5)
		_player.rotation.y = PI * 0.5
	var total := _skip + int(_seconds * 60.0)
	var vp := root.get_viewport_rid()
	if _perf:
		_images = false
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
		RenderingServer.viewport_set_measure_render_time(vp, true)
	for i in total:
		# (only the frames saved are drawn)
		var draw := _images and i >= _skip and (i - _skip) % _every == 0
		# (and --warm frames before the first, unsaved: the exposure settled)
		var warm := _images and i < _skip and i >= _skip - _warm
		RenderingServer.viewport_set_update_mode(vp, RenderingServer.VIEWPORT_UPDATE_ALWAYS if (draw or warm or _draw_all) else RenderingServer.VIEWPORT_UPDATE_DISABLED)
		if _program == "hang":
			_drive(i / 60.0)
		elif _program == "course":
			_drive_course(i / 60.0)
		elif _program == "keys":
			_drive_keys(i)
		elif _program == "bump":
			_drive_bump(i)
		elif _program == "people" or _program == "hay":
			_player.set("wish", Vector3())
		elif _program == "weather":
			_player.set("wish", Vector3())
			_drive_weather(i)
		elif _program == "talk":
			_drive_talk(i)
		elif _program == "osteria":
			if _view == "walkin":
				var goal := Vector3(-6.6, 0.0, 17.6)
				var to := goal - _player.global_position
				to.y = 0.0
				_player.set("wish", to.normalized() * 0.5 if to.length() > 0.2 else Vector3())
			else:
				_player.set("wish", Vector3())
			if not _weather_seq.is_empty():
				_drive_weather(i)
			if i % 60 == 0 and _osteria:
				print("climb_look: osteria t=%.0f player %s %s" % [i / 60.0, _player.global_position.snapped(Vector3(0.01, 0.01, 0.01)), _osteria.call("get_stats")])
		elif _program == "lookup":
			if i % 60 == 0:
				var crowd := _player.get_parent().get_node_or_null("Crowd")
				print("climb_look: lookup t=%.0f player y %.2f %s" % [i / 60.0, _player.global_position.y, crowd.call("get_social_stats") if crowd else {}])
		elif _program == "fountain":
			# (milestone 207: a run at the fountain's middle — onto the basin's rim, across it, into the column)
			_player.set("wish", Vector3(-1.0, 0.0, 0.0))
		if _cam:
			_place_camera(i == 0)
			_cam.current = true
		await process_frame
		_record(i)
		if _passes and i >= _skip:
			var rd := RenderingServer.get_rendering_device()
			var n := rd.get_captured_timestamps_count() if rd else 0
			if n > 1:
				_pass_frames += 1
				for k in range(1, n):
					var name := rd.get_captured_timestamp_name(k - 1)
					var dt := float(rd.get_captured_timestamp_gpu_time(k) - rd.get_captured_timestamp_gpu_time(k - 1)) / 1e6
					_pass_sum[name] = float(_pass_sum.get(name, 0.0)) + dt
		if _perf and i >= _skip:
			_perf_gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(vp))
			_perf_cpu.append(RenderingServer.viewport_get_measured_render_time_cpu(vp) + RenderingServer.get_frame_setup_time_cpu())
		if draw:
			await RenderingServer.frame_post_draw
			var shot := root.get_texture().get_image()
			if _jpg:
				shot.save_jpg("%s/f%04d.jpg" % [_out, (i - _skip) / _every], 0.9)
			else:
				shot.save_png("%s/f%04d.png" % [_out, (i - _skip) / _every])
	var f := FileAccess.open(_out.path_join("frames.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify({"every": _every, "skip": _skip, "frames": _frames}))
	f.close()
	print("climb_look: %d frames" % total)
	if _perf and _perf_gpu.size() > 0:
		var g := Array(_perf_gpu)
		var c := Array(_perf_cpu)
		g.sort()
		c.sort()
		var mean := func(a: Array) -> float: return a.reduce(func(x, y): return x + y, 0.0) / maxf(a.size(), 1)
		print("climb_look: perf %d frames: GPU mean %.2f ms, median %.2f, p95 %.2f; render CPU mean %.2f ms, median %.2f" % [g.size(),
				mean.call(g), g[g.size() / 2], g[int(g.size() * 0.95)], mean.call(c), c[c.size() / 2]])
	if _passes and _pass_frames > 0:
		var names := _pass_sum.keys()
		names.sort_custom(func(x, y): return float(_pass_sum[x]) > float(_pass_sum[y]))
		var all_ms := 0.0
		for nm in names:
			all_ms += float(_pass_sum[nm]) / _pass_frames
		print("climb_look: passes %d frames, %.2f ms the GPU between the first and the last marker" % [_pass_frames, all_ms])
		for nm in names.slice(0, 40):
			print("climb_look: pass %7.3f ms  %s" % [float(_pass_sum[nm]) / _pass_frames, nm])
	var crowd := _player.get_parent().get_node_or_null("Crowd")
	if crowd and crowd.has_method("get_reaction_stats"):
		print("climb_look: the people ", crowd.call("get_reaction_stats"))
		if ClassDB.class_exists(&"FellowCloth3D"):
			# (milestone 208: the skirts' cloth — how many, their particles, the simulation's time)
			var skirts := crowd.find_children("Skirt", "", true, false)
			print("climb_look: skirts %d, %s" % [skirts.size(), ClassDB.class_call_static(&"FellowCloth3D", &"get_global_stats") if ClassDB.class_has_method(&"FellowCloth3D", &"get_global_stats") else {}])
	quit()


## The "course" program (milestone 196): on the square, along +x — a low wall to vault (0.95 m high, 0.35 m thick) at
## a sprint, then a block 3 m high and 2 m deep: a jump at it, its edge caught, up onto its top from the hang.
const COURSE_Z := 8.0
const COURSE_X := -2.0

func _box(p_parent: Node, p_name: String, p_size: Vector3, p_at: Vector3) -> void:
	var body := StaticBody3D.new()
	body.name = p_name
	p_parent.add_child(body)
	body.global_position = p_at
	var cs := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = p_size
	cs.shape = shape
	body.add_child(cs)
	var mi := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = p_size
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.72, 0.64, 0.52)
	mat.roughness = 0.9
	mesh.material = mat
	mi.mesh = mesh
	body.add_child(mi)


func _course(p_scene: Node) -> void:
	# (the square's props and people out of the way)
	var crowd := p_scene.get_node_or_null("Crowd")
	if crowd:
		crowd.queue_free()
	var town := p_scene.get_node("Town")
	for c in town.get_children():
		var n := String(c.name)
		if n.begins_with("Bollard") or n.begins_with("Barrel") or n.begins_with("Crate") or n.begins_with("Stall") or n == "HayCart":
			c.queue_free()
	_box(p_scene, "CourseVault", Vector3(0.35, 0.95, 3.0), Vector3(COURSE_X + 5.0, 0.475, COURSE_Z))
	_box(p_scene, "CourseBlock", Vector3(2.0, 3.0, 3.0), Vector3(COURSE_X + 11.0, 1.5, COURSE_Z))
	_player.get("traversal").set("hang_first", true) # (the block's edge caught and hung from, then climbed onto)
	_player.set("autopilot", true)
	_player.global_position = Vector3(COURSE_X, 0.0, COURSE_Z)
	_player.set("facing", -PI / 2.0) # (facing +x: the model looks down its −Z)
	_player.rotation.y = -PI / 2.0


var _course_hung := -1.0

func _drive_course(t: float) -> void:
	var trav: Node = _player.get("traversal")
	var go := Vector3(1.0, 0.0, 0.0)
	if trav.is_hanging() and _course_hung < 0.0:
		_course_hung = t
	if _course_hung < 0.0:
		_player.set("wish", go if t > 0.3 else Vector3())
		_player.set("want_sprint", t > 0.3)
	elif t < _course_hung + 1.0:
		_player.set("wish", Vector3())
		_player.set("want_sprint", false)
	else:
		# up onto the top from the hang, then still on it
		var on_top: bool = _player.global_position.y > 2.0 and not trav.is_active()
		_player.set("wish", Vector3() if on_top else go)
		_player.set("want_walk", true)


## The "hang" program: a run at CasaCrema's front (the jump at the wall catches its first hold), then hanging still
## (HANG_STILL s), along the hold to the left and back to the right, still again, then up the front from hold to hold.
const HANG_STILL := 4.0

func _drive(t: float) -> void:
	var trav: Node = _player.get("traversal")
	var into := Vector3(0.0, 0.0, -1.0)
	_player.set("want_sprint", false)
	if t < 2.2 and not trav.is_hanging():
		_player.set("wish", into)
		_player.set("want_sprint", t > 0.2)
		return
	if not trav.is_hanging() and not trav.is_transferring() and t < 2.2:
		return
	var u := t - 2.2
	var w := Vector3()
	if u < HANG_STILL:
		w = Vector3()
	elif u < HANG_STILL + 1.6:
		w = Vector3(-1.0, 0.0, 0.0) # the body's left facing the wall is the world's -x here
	elif u < HANG_STILL + 3.2:
		w = Vector3(1.0, 0.0, 0.0)
	elif u < HANG_STILL * 2.0 + 3.2:
		w = Vector3()
	else:
		w = into
	_player.set("wish", w)


## The "bump" program (milestone 208, the owner: "running into the NPCs, knocking someone over"): one of the three
## talking in the square's middle (round (1, 5.5)) run at from 6 m at --bump's pace and on 4 m past them.
var _bump_pace := "run"
var _bump_target: Node3D
var _bump_from := Vector3()
var _bump_dir := Vector3()
var _bump_last := ""
var _lying := [0.0, 0.0, 0, 0]

func _bump_setup(sc: Node) -> void:
	_player.set("autopilot", true)
	var crowd := sc.get_node("Crowd")
	var best := 1e9
	for c in crowd.get_children():
		if c is Node3D and (c as Node3D).position.distance_to(Vector3(1.0, 0.0, 5.5)) < best:
			best = (c as Node3D).position.distance_to(Vector3(1.0, 0.0, 5.5))
			_bump_target = c
	var at := _bump_target.global_position
	_bump_dir = (at - Vector3(1.0, 0.0, 5.5)).normalized() if at.distance_to(Vector3(1.0, 0.0, 5.5)) > 0.1 else Vector3(1, 0, 0)
	# (from outside the group, through the one person to its middle and beyond)
	_bump_from = at + _bump_dir * 6.0
	_player.global_position = _bump_from
	var yaw := atan2(_bump_dir.x, _bump_dir.z)
	_player.set("facing", yaw)
	_player.rotation.y = yaw


func _drive_bump(i: int) -> void:
	var t := i / 60.0
	var crowd := _player.get_parent().get_node("Crowd")
	_player.set("want_sprint", _bump_pace == "sprint" and t > 0.3)
	_player.set("want_walk", _bump_pace == "walk")
	var gone := (_player.global_position - _bump_from).dot(-_bump_dir)
	_player.set("wish", -_bump_dir if t > 0.3 and gone < 10.0 else Vector3())
	var st: Dictionary = crowd.call("get_reaction_stats")
	var ps: Array = crowd.call("get_person_state", _bump_target)
	if int(ps[1]) == 3:
		# (lying: how still the bodies lie — their fastest and their mean speed, the frames counted)
		var top := 0.0
		var sum := 0.0
		var n := 0
		for b in _bump_target.find_children("*", "PhysicalBone3D", true, false):
			var v := (b as PhysicalBone3D).linear_velocity.length()
			top = maxf(top, v)
			sum += v
			n += 1
		_lying[0] = maxf(_lying[0], top)
		_lying[1] += sum / maxf(n, 1)
		_lying[2] += 1
	elif int(ps[1]) == 4 and "--bump_trace=1" in OS.get_cmdline_user_args():
		var top := 0.0
		var who := ""
		for b in _bump_target.find_children("*", "PhysicalBone3D", true, false):
			var v := (b as PhysicalBone3D).linear_velocity.length()
			if v > top:
				top = v
				who = (b as PhysicalBone3D).bone_name
		var hb: PhysicalBone3D = null
		for b in _bump_target.find_children("*", "PhysicalBone3D", true, false):
			if (b as PhysicalBone3D).bone_name == "Hips":
				hb = b
		var skel := _bump_target.find_child("Skeleton3D", true, false) as Skeleton3D
		var anim_hips := skel.global_transform * skel.get_bone_global_pose(skel.find_bone("Hips")).origin
		var pa := skel.get_node_or_null("Physical")
		var tg := Vector3()
		if pa:
			var bones: PackedStringArray = pa.call("get_body_bones")
			var targets: Array = pa.call("get_body_targets")
			for k in bones.size():
				if bones[k] == "Hips":
					tg = (targets[k] as Transform3D).origin
		print("bump getup t %.3f fastest %.2f %s hips body %s v %s target %s anim %s" % [t, top, who, hb.global_position if hb else Vector3(), hb.linear_velocity if hb else Vector3(), tg, anim_hips])
	if int(ps[1]) != 3 and _lying[2] > 0 and _lying[3] == 0:
		_lying[3] = 1
		print("bump lying: frames %d, the bodies' fastest %.3f m/s, their mean %.4f m/s" % [_lying[2], _lying[0], _lying[1] / _lying[2]])
	var key := "%s %s" % [crowd.call("get_person_state", _bump_target), _bump_target.find_child("AnimationPlayer", true, false).get("current_animation")]
	if key != _bump_last:
		print("bump t %.2f %s at %s player %s v %.2f" % [t, key, _bump_target.global_position, _player.global_position, (_player.velocity as Vector3).length()])
		if key.begins_with("[\"home\"") and not _bump_last.begins_with("[\"home\""):
			var stats: Dictionary = crowd.call("get_person_stats", _bump_target)
			if not stats.is_empty():
				stats.erase("get_up_clips")
				print("bump stats ", stats)
		_bump_last = key


## The "people" program (milestone 209, the owner: "the skirts not solids but cloth"): the camera beside a walker in a
## skirt (the first of the crowd's), keeping pace; the player out of the way.
var _people_target: Node3D
var _people_side := 1.0 # --people_side=-1: from its other side
var _hay_cart: Node3D
## Milestone 211: the "weather" program.
const WEATHER_KEEP := ["StallA", "StallB", "HayCart", "Awning0", "Awning1"]
var _view := "square"
## Milestone 212: "talk", "lookup".
var _talk_group := 0
var _crowd_in := false # --crowd=1: the weather program with the people (the rain sends them off the square)
var _talk_target: Node3D
var _osteria: Node3D # milestone 213
var _talk_at := Vector3()
var _drop: PackedStringArray = []
var _env_set: PackedStringArray = [] # --env_set=name:value,...: the WorldEnvironment's Environment's properties
var _omni_dp := false # --omni_dp=1 (milestone 219): every shadowed omni light dual paraboloid (an A/B)
var _perf := false # --perf=1: every frame drawn, none saved, vsync off; the GPU's and the CPU's time a frame printed
## --passes=1 (milestone 217): --perf and the GPU's time in each of the engine's passes (its RENDER_TIMESTAMP markers, with
## --setting=rendering/fellow/profiling/pass_timestamps:true — they fence the passes: the frame's total is not --perf's).
var _passes := false
var _pass_sum := {}
var _pass_frames := 0
var _perf_gpu: PackedFloat64Array = []
var _perf_cpu: PackedFloat64Array = []
var _draw_all := false # --draw_all=1: every frame drawn, every --every-th saved (what the GPU runs while drawing — the
		# burning logs, milestone 211 — goes on in the frames between)
var _weather_seq: Array = [] # [key, second]
var _weather_transition := -1.0 # s (-1: WeatherKeys' own)
var _weather_keys: Node
var _brazier: Node3D
var _hay_from := 0.0 # --hay_from=<deg>: where round the cart the camera starts
var _hay_turn := 16.0 # --hay_turn=<deg/s>: how fast it goes round (0: still, milestone 215's straws in the wind)

func _people_setup(sc: Node) -> void:
	_player.set("autopilot", true)
	_player.global_position = Vector3(-30.0, 0.0, -30.0)
	var crowd := sc.get_node("Crowd")
	var walkers: Array = crowd.get("_walkers")
	for w in walkers:
		var node: Node3D = w.node
		if node.find_child("Skirt", true, false):
			_people_target = node
			break


## The "keys" program (milestone 207, the owner: "climbing, when I hold W and press space the body sometimes goes along
## the edge, left or right at random"): a run at CasaCrema's front, the first hold caught, 1 s still, then the stick
## held at the wall --keys_yaw degrees off square (the camera's yaw off the wall's) and jump pressed every 0.5 s.
var _keys_yaw := 0.0
var _keys_always := false # --keys_always=1: jump pressed on top too (not only hanging)
var _keys_sprint := false # --keys_sprint=1: the sprint held too
var _keys_turn := 0.0 # --keys_turn=<s>: from then on, on the roof, back down it and off the eave

## The "weather" program (milestone 211): the weather turned as --weather_seq says; each second the weather's and the
## brazier's state printed.
func _drive_weather(i: int) -> void:
	var t := i / 60.0
	for step in _weather_seq:
		if i == int(round(float(step[1]) * 60.0)) and _weather_keys:
			var tr := float(_weather_keys.get("transition")) if _weather_transition < 0.0 else _weather_transition
			_weather_keys.call("turn_to", int(step[0]), 0.0 if i == 0 else tr)
	if i % 60 == 0 and _weather_keys:
		var w: Node = _weather_keys.get_node_or_null(_weather_keys.get("weather_path"))
		var st: Dictionary = w.call("get_current_state") if w else {}
		print("climb_look: weather t=%.0f key %d wind %.1f rain %.2f clouds %.2f sun %.2f fog %.4f; brazier %s" % [t,
				_weather_keys.call("get_weather_key"), st.get("wind_speed", 0.0), st.get("intensity", 0.0) if int(st.get("precipitation", 0)) else 0.0,
				st.get("cloud_coverage", 0.0), st.get("sunlight", 0.0), st.get("fog_density", 0.0),
				_brazier.call("get_stats") if _brazier else {}])
		var crowd := _player.get_parent().get_node_or_null("Crowd")
		if crowd and crowd.has_method("get_social_stats"):
			print("climb_look: people t=%.0f %s" % [t, crowd.call("get_social_stats")])
		var night := _player.get_parent().get_node_or_null("Night")
		if night and night.has_method("get_stats"):
			print("climb_look: night t=%.0f %s exposure %.2f" % [t, night.call("get_stats"),
					((_player.get_parent().get_node("WorldEnvironment") as WorldEnvironment).camera_attributes as CameraAttributes).exposure_multiplier])
		var smoke := _player.get_parent().get_node_or_null("ChimneySmoke")
		if smoke:
			for c in smoke.get_children():
				print("climb_look: chimney %s t=%.0f %s" % [c.name, t, c.call("get_stats")])
		var rain := _player.get_parent().get_node_or_null("Rain")
		if rain:
			var drops: Node = rain.call("get_drops") if rain.has_method("get_drops") else null
			var env: Environment = (_player.get_parent().get_node("WorldEnvironment") as WorldEnvironment).environment
			print("climb_look: rain t=%.0f rain %.2f precipitation %d wetness %.2f snow %.2f drops %s; fog %.0f-%.0f m %s" % [t, rain.get("rain"), rain.get("precipitation"),
					rain.get("wetness"), rain.get("snow"), [drops.get("emitting"), drops.get("amount"), drops.get("visible")] if drops else "-",
					env.fog_depth_begin, env.fog_depth_end, env.fog_light_color])


## The "talk" program (milestone 212): the player 3.2 m from one of a group (--talk_to=<group index>, its first
## person), walking up to them; E pressed at 1.4 s, then every 2.6 s.
func _talk_setup(sc: Node) -> void:
	_player.set("autopilot", true)
	var crowd := sc.get_node_or_null("Crowd")
	var groups: Array = crowd.get("groups") if crowd else []
	var c: Vector3 = (groups[clampi(_talk_group, 0, groups.size() - 1)] as Array)[0] if groups.size() > 0 else Vector3()
	# (the person of the group on the side the player comes from: the nearest to a point 3 m off the group's centre)
	var from := c + Vector3(0.0, 0.0, 3.6)
	var best: Node3D = null
	for n in crowd.get_children():
		if n is Node3D and n.has_node("AnimationPlayer") and (n as Node3D).position.distance_to(c) < 1.2:
			if best == null or (n as Node3D).position.distance_to(from) < best.position.distance_to(from):
				best = n
	_talk_target = best
	var at := best.global_position if best else c
	var dir := Vector3(from.x - at.x, 0.0, from.z - at.z).normalized()
	_player.global_position = at + dir * 3.2
	_talk_at = at + dir * 1.1
	var f := atan2(dir.x, dir.z)
	_player.set("facing", f)
	_player.rotation.y = f


func _drive_talk(i: int) -> void:
	var pp := _player.global_position
	var to := Vector3(_talk_at.x - pp.x, 0.0, _talk_at.z - pp.z)
	_player.set("wish", to.normalized() * 0.5 if to.length() > 0.12 else Vector3())
	var crowd := _player.get_parent().get_node_or_null("Crowd")
	if crowd == null:
		return
	if i == 84 or (i > 84 and (i - 84) % 156 == 0):
		print("climb_look: talk t=%.1f E -> %s" % [i / 60.0, crowd.call("talk_to_nearest")])
	if i % 60 == 0:
		print("climb_look: talk t=%.0f %s" % [i / 60.0, crowd.call("get_social_stats")])


func _drive_keys(i: int) -> void:
	var trav: Node = _player.get("traversal")
	var t := i / 60.0
	var into := Vector3(0.0, 0.0, -1.0)
	_player.set("want_sprint", false)
	if t < 2.2 and not trav.is_hanging():
		_player.set("wish", into)
		_player.set("want_sprint", t > 0.2)
		return
	var u := t - 3.2
	if u < 0.0:
		_player.set("wish", Vector3())
		return
	_player.set("wish", Basis(Vector3.UP, deg_to_rad(_keys_yaw)) * into)
	_player.set("want_sprint", _keys_sprint)
	if _keys_turn > 0.0 and t >= _keys_turn and not trav.is_hanging() and not trav.is_active():
		# (on the roof: back down it and off the eave — a fall to the street)
		_player.set("wish", -into)
		return
	if i % 30 == 0 and (trav.is_hanging() or _keys_always):
		_player.set("want_jump", true)
		_player.set("_jump_buffer", 0.15) # (as _read_input's press)


func _on_posed() -> void:
	var xf := _skel.get_global_transform_interpolated()
	for n in BONES:
		_posed[n] = xf * _skel.get_bone_global_pose(_skel.find_bone(n)).origin
	for side in ["Left", "Right"]:
		for g in GRIP_BONES:
			var b := _skel.find_bone(side + g)
			if b >= 0:
				_posed_xf[side + g] = xf * _skel.get_bone_global_pose(b)


## The end of a finger's last bone (its parent's length on along its axis, the finger straight at rest), world space.
func _tip(n: String) -> Vector3:
	var b := _skel.find_bone(n)
	if b < 0 or not _posed_xf.has(n):
		return Vector3()
	var rest := _skel.get_bone_rest(b)
	var axis := (rest.basis.inverse() * rest.origin).normalized()
	var t: Transform3D = _posed_xf[n]
	return t * (axis * rest.origin.length())


## How deep a point is in the stone: reached from 0.3 m out from the wall and from 0.3 m above, the nearer surface met
## on the way (0: either way is clear — under an overhang, or in the open).
func _depth(p: Vector3, inward: Vector3) -> float:
	var space := _player.get_world_3d().direct_space_state
	var least := INF
	for from in [p - inward * 0.3, p + Vector3.UP * 0.3]:
		var q := PhysicsRayQueryParameters3D.create(from, p)
		q.exclude = [_player.get_rid()]
		q.hit_back_faces = false
		var hit := space.intersect_ray(q)
		if hit.is_empty():
			return 0.0
		least = minf(least, (hit["position"] as Vector3).distance_to(p))
	return least


## Milestone 203: the gap from a point to the first surface (not the player's) along a horizontal direction, measured from
## 0.6 m behind it (so a point inside the wall reads negative); 2.0 when nothing is within 1.4 m.
func _front_gap(p: Vector3, d: Vector3) -> float:
	var space := _player.get_world_3d().direct_space_state
	var from := p - d * 0.6
	var q := PhysicsRayQueryParameters3D.create(from, p + d * 1.4)
	q.exclude = [_player.get_rid()]
	q.hit_back_faces = false
	var hit := space.intersect_ray(q)
	if hit.is_empty():
		return 2.0
	return (hit["position"] as Vector3).distance_to(from) - 0.6


func _bone(n: String) -> Vector3:
	if _posed.has(n):
		return _posed[n]
	return _skel.global_transform * _skel.get_bone_global_pose(_skel.find_bone(n)).origin


## Beside the body along the wall it climbs (the traversal's inward), or behind and below it; eased (no shake of its own).
func _place_camera(p_first: bool) -> void:
	var trav: Node = _player.get("traversal")
	var hips := _bone("Hips")
	var inward := -_player.global_basis.z
	var hints: Dictionary = trav.get_pose_targets()
	if hints.get("active", false) and hints.has("inward"):
		inward = hints["inward"]
	inward.y = 0.0
	inward = inward.normalized() if inward.length() > 0.1 else Vector3(0, 0, -1)
	var side := Vector3.UP.cross(inward).normalized()
	var want: Vector3
	if _cam_mode == "handside" and hints.get("active", false):
		# Milestone 198: the left hand on the edge in profile, from along the edge.
		var at: Vector3 = hints["hand_left"]
		_cam_pos = _cam_pos.lerp(at + side * _dist - inward * 0.12 + Vector3.UP * 0.02, 1.0 if p_first else 0.25)
		_look = _look.lerp(at - inward * 0.08 - Vector3.UP * 0.04, 1.0 if p_first else 0.3)
		_cam.global_position = _cam_pos
		_cam.look_at(_look, Vector3.UP)
		return
	if _cam_mode == "hand" and hints.get("active", false):
		# Milestone 198: the hands on the edge, from out and a little to the side and above.
		var at: Vector3 = (hints["hand_left"] + hints["hand_right"]) * 0.5
		_cam_pos = _cam_pos.lerp(at - inward * _dist + side * (_dist * 0.45) + Vector3.UP * (_dist * 0.35), 1.0 if p_first else 0.25)
		_look = _look.lerp(at - Vector3.UP * 0.05, 1.0 if p_first else 0.3)
		_cam.global_position = _cam_pos
		_cam.look_at(_look, Vector3.UP)
		return
	if _program == "talk" and _talk_target:
		# (over the player's right shoulder, the one talked to ahead)
		var pp := _player.global_position
		var to := _talk_target.global_position - pp
		to.y = 0.0
		to = to.normalized() if to.length() > 0.1 else Vector3(0, 0, -1)
		var right := to.cross(Vector3.UP).normalized()
		var want_at := pp - to * 1.9 + right * 0.75 + Vector3.UP * 1.75
		_cam_pos = want_at if p_first else _cam_pos.lerp(want_at, 0.08)
		var look_at := _talk_target.global_position + Vector3.UP * 1.55
		_look = look_at if p_first else _look.lerp(look_at, 0.12)
		_cam.global_position = _cam_pos
		_cam.look_at(_look, Vector3.UP)
		return
	if _program == "osteria":
		if _view == "walkin":
			# (behind the player and above, following)
			var pp := _player.global_position
			var want_at := pp + Vector3(0.6, 1.9, -2.6)
			_cam_pos = want_at if p_first else _cam_pos.lerp(want_at, 0.1)
			_look = (pp + Vector3(0, 1.2, 2.0)) if p_first else _look.lerp(pp + Vector3(0, 1.2, 2.0), 0.15)
			_cam.global_position = _cam_pos
			_cam.look_at(_look, Vector3.UP)
		elif p_first:
			if _view == "inside":
				_cam.global_position = Vector3(-11.9, 1.7, 14.95)
				_cam.look_at(Vector3(-7.6, 1.05, 21.2), Vector3.UP)
			else:
				_cam.global_position = Vector3(-5.1, 1.65, 10.6)
				_cam.look_at(Vector3(-7.2, 1.3, 17.8), Vector3.UP)
		return
	if _program == "lookup":
		if p_first:
			# (on the square behind the group nearest CasaCrema, looking up at its front over their heads)
			_cam.global_position = Vector3(9.4, 1.9, -1.2)
			_cam.look_at(Vector3(12.6, 5.2, -11.5), Vector3.UP)
		return
	if _program == "weather":
		if p_first:
			var at := Vector3(-7.2, 0.0, 4.2)
			if _view == "brazier":
				# (the brazier from 4 m, a person's eyes, the fountain behind)
				_cam.global_position = at + Vector3(2.6, 1.65, 3.0)
				_cam.look_at(at + Vector3(0.0, 1.35, 0.0), Vector3.UP)
			elif _view == "chimney":
				# (CasaCrema's chimney from over the square, beside the bell tower)
				_cam.global_position = Vector3(6.0, 15.8, -8.5)
				_cam.look_at(Vector3(9.85, 16.6, -16.59), Vector3.UP)
			else:
				# (the square from its south-east corner: the brazier, the fountain, PalazzoOcra's and CasaCrema's chimneys)
				_cam.global_position = Vector3(7.5, 2.4, 11.5)
				_cam.look_at(Vector3(-5.0, 6.5, -8.0), Vector3.UP)
		return
	if _program == "hay" and _hay_cart:
		# (milestone 210: round the hay cart, slowly, at a person's height and from above)
		var t := float(_frames.size()) / 60.0
		var a := deg_to_rad(_hay_from + _hay_turn * t)
		var c := _hay_cart.global_position + Vector3.UP * 1.3
		_cam.global_position = c + Vector3(cos(a), 0.0, sin(a)) * _dist + Vector3.UP * (0.4 + 0.35 * _dist * 0.3)
		_cam.look_at(c + Vector3.UP * 0.1, Vector3.UP)
		return
	if _program == "people" and _people_target:
		# (milestone 209: beside a walker in a skirt, keeping pace with it)
		var at := _people_target.global_position
		var side_of := _people_target.global_basis.x.normalized() * _people_side
		var want_at := at + side_of * _dist + Vector3.UP * 1.0 - _people_target.global_basis.z.normalized() * 0.6
		_cam_pos = want_at if p_first else _cam_pos.lerp(want_at, 0.1)
		_look = (at + Vector3.UP * 0.65) if p_first else _look.lerp(at + Vector3.UP * 0.65, 0.2)
		_cam.global_position = _cam_pos
		_cam.look_at(_look, Vector3.UP)
		return
	if _cam_mode == "npc" and _bump_target:
		# (milestone 208: still, beside the person run into, looking at where they stand and fall)
		if p_first:
			var across := Vector3.UP.cross(_bump_dir).normalized()
			var at := _bump_target.global_position - _bump_dir * 0.5
			_cam.global_position = at + across * (_dist * 0.7) + Vector3.UP * 1.4 + _bump_dir * (_dist * 0.5)
			_cam.look_at(at - _bump_dir * 0.4 + Vector3.UP * 0.55, Vector3.UP)
		return
	if _cam_mode == "back":
		want = hips - inward * _dist + Vector3.UP * -0.4 + side * 0.6
	else:
		want = hips - inward * (_dist * 0.55) + side * _dist + Vector3.UP * 0.3
	var rate := 1.0 if p_first else 0.12
	_cam_pos = _cam_pos.lerp(want, rate)
	_look = _look.lerp(hips + Vector3.UP * 0.1, 1.0 if p_first else 0.2)
	_cam.global_position = _cam_pos
	_cam.look_at(_look, Vector3.UP)


func _record(p_frame: int) -> void:
	var trav: Node = _player.get("traversal")
	var hints: Dictionary = trav.get_pose_targets()
	var r := {"i": p_frame, "state": String(_player.get("state")), "hang": trav.is_hanging(), "move": trav.is_transferring(),
			"climb": trav.is_climbing(), "mantle": trav.is_active(), "body": _v(_player.global_position),
			"hips": _v(_bone("Hips"))}
	var tpose: Node = _player.get_node_or_null("Model/Mannequin/Skeleton3D/TraversalPose")
	if tpose and tpose.has_method("get_hang_fit_offset"):
		r["fit"] = _v(tpose.get_hang_fit_offset())
	# Each limb: its joints, and the plane it bends in (the normal of upper, joint, end) — a flip of that normal from one
	# frame to the next is an elbow or knee turning over.
	var limbs := []
	for l in LIMBS:
		var a := _bone(l[0])
		var b := _bone(l[1])
		var c := _bone(l[2])
		limbs.append({"a": _v(a), "b": _v(b), "c": _v(c), "n": _v((b - a).cross(c - b).normalized())})
	r["limbs"] = limbs
	# Milestone 203: the head and the upper chest, the body's velocity, and how far the face, the chest, the hips and the
	# knees are from the wall in front of the body (along the traversal's inward, or the body's facing; negative: in it).
	r["head"] = _v(_bone("Head"))
	r["chest"] = _v(_bone("UpperChest"))
	r["vel"] = _v(_player.velocity)
	var fwd := -_player.global_basis.z
	if hints.get("active", false) and hints.has("inward"):
		fwd = hints["inward"]
	fwd.y = 0.0
	fwd = fwd.normalized() if fwd.length() > 0.1 else Vector3(0, 0, -1)
	r["fwd"] = _v(fwd)
	var gaps := {}
	for n in ["Head", "UpperChest", "Hips", "LeftLowerLeg", "RightLowerLeg"]:
		gaps[n] = snappedf(_front_gap(_bone(n) + (Vector3.UP * 0.08 if n == "Head" else Vector3()), fwd), 0.0001)
	r["gaps"] = gaps
	if hints.get("active", false):
		for k in ["hand_left", "hand_right", "foot_left", "foot_right"]:
			r[k] = _v(hints[k])
		r["hand_weights"] = [hints["hand_weights"].x, hints["hand_weights"].y]
		r["foot_weights"] = [hints["foot_weights"].x, hints["foot_weights"].y]
		r["hand_grips"] = [hints["hand_grips"].x, hints["hand_grips"].y]
		# Milestone 198: each hand by its hold — the elbow, wrist, middle finger's knuckle, middle joint and tip, the
		# thumb's middle joint and tip: out from the edge (along -inward) and up from it (along the top's normal); how deep
		# in the stone each bone goes (for climb_metrics.py and the grip's measurements).
		if hints.has("hand_reach"):
			var grips := []
			for s in 2:
				var side := "Left" if s == 0 else "Right"
				var inward: Vector3 = hints["inward"]
				var n: Vector3 = hints["hold_normal_left" if s == 0 else "hold_normal_right"]
				var edge: Vector3 = hints.get("hold_left" if s == 0 else "hold_right", hints["hand_left" if s == 0 else "hand_right"]) - inward * 0.06 - Vector3.UP * 0.02
				var pts := [_bone(side + "LowerArm"), _bone(side + "Hand"), (_posed_xf.get(side + "MiddleProximal", Transform3D()) as Transform3D).origin,
						(_posed_xf.get(side + "MiddleIntermediate", Transform3D()) as Transform3D).origin, _tip(side + "MiddleIntermediate"),
						(_posed_xf.get(side + "ThumbProximal", Transform3D()) as Transform3D).origin, _tip(side + "ThumbProximal")]
				var rel := []
				for p in pts:
					rel.append([snappedf(-(p - edge).dot(inward), 0.0001), snappedf((p - edge).dot(n), 0.0001)])
				# Along each bone (the forearm, the palm, the finger's two bones, the thumb's), every 1.5 cm: how deep in the
				# stone the point is (the least of from the wall's side and from above).
				var segs := []
				for pair in [[0, 1], [1, 2], [2, 3], [3, 4], [1, 5], [5, 6]]:
					var a: Vector3 = pts[pair[0]]
					var b: Vector3 = pts[pair[1]]
					var n_s := maxi(1, int(ceil(a.distance_to(b) / 0.015)))
					var deepest := 0.0
					for j in range(n_s + 1):
						deepest = maxf(deepest, _depth(a.lerp(b, float(j) / n_s), inward))
					segs.append(snappedf(deepest, 0.0001))
				grips.append({"grip": hints["hand_grips"].x if s == 0 else hints["hand_grips"].y,
						"reach": hints["hand_reach"].x if s == 0 else hints["hand_reach"].y, "rel": rel, "into": segs.max(), "segs": segs,
						"lip": _v2(hints.get("hold_lip_left" if s == 0 else "hold_lip_right", Vector2()))})
			r["grips"] = grips
			# (the wrists' turn against the forearms, as quaternions)
			var wr := []
			for side in ["Left", "Right"]:
				if _posed_xf.has(side + "Hand") and _posed_xf.has(side + "LowerArm"):
					var q := ((_posed_xf[side + "LowerArm"] as Transform3D).basis.inverse() * (_posed_xf[side + "Hand"] as Transform3D).basis).get_rotation_quaternion()
					wr.append([q.x, q.y, q.z, q.w])
			r["wrists"] = wr
	_frames.append(r)


func _v2(p: Vector2) -> Array:
	return [snappedf(p.x, 0.0001), snappedf(p.y, 0.0001)]


func _v(p: Vector3) -> Array:
	return [snappedf(p.x, 0.0001), snappedf(p.y, 0.0001), snappedf(p.z, 0.0001)]
