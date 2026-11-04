extends "res://bench/scene_base.gd"
## MATCHING LAB (built in code): characters animated by motion matching (FellowMotionMatcher3D,
## docs/decisions/0149-motion-matching.md) on the CMU motion capture database's locomotion (subjects 16, 35 and 69:
## walking, jogging, running, turning, stopping, side and back steps; free for research and commercial use, not part of
## this repository: benchmarks/scripts/get_cmu_locomotion.sh fetches it into res://external/matching/cmu).
##
## --matching_crowd=N characters (16) wander a plaza like Motion Lab's: each walks, jogs or runs to waypoints at its own
## pace, swerves round the others, stops and stands a while. The database is built from the BVH files on the first run
## and saved as res://external/matching/cmu.res (--matching_rebuild=1 builds it again).
## --matching_ik=0 / --matching_lock=0: no foot IK / no foot locking; --matching_inertialization=0: hard cuts;
## --matching_sync=0: the root follows the animation alone; --matching_interval=<s> (0.1): the search interval;
## --matching_lod=<d1,d2>: LOD distances (none); --matching_subjects=<016,035,069>; --matching_threads=0: each
## character's update on the main thread (default: FellowMotionMatcher3D.advance_group() on the worker threads).
## The simulation steps at 60 Hz from set_time(); a step back in time continues it.

const STEP := 1.0 / 60.0
const CMU_SCALE := 0.056444  # CMU's units (1/0.45 inch) in metres.

var sun: DirectionalLight3D
var crowd := 16
var data_dir := ""
var db_path := ""
var database = null  # FellowMotionDatabase
var bvh_skeleton := {}
var body_mesh: ArrayMesh
var body_skin: Skin
var _area := 16.0
var _chars: Array = []
var _matchers: Array = []
var _rng_state: Array = []
var _last_t := 0.0
var _acc := 0.0
var _root: Node3D
var build_info := {}
var matcher_wall_usec := 0
var matcher_steps := 0


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "matching"
	crowd = int(args.get("matching_crowd", "16"))
	data_dir = str(args.get("matching_data", OS.get_environment("FELLOW_MATCHING_DATA")))
	if data_dir == "":
		data_dir = ProjectSettings.globalize_path("res://external/matching/cmu")
	db_path = str(args.get("matching_db", "res://external/matching/cmu.res"))
	_area = clampf(sqrt(float(crowd)) * 4.0, 12.0, 60.0)
	_setup_common()
	_setup_environment()
	_build_ground()
	_build_lights()
	if not ClassDB.class_exists("FellowMotionMatcher3D"):
		load_error = "this build has no FellowMotionMatcher3D (modules/fellow_animation)"
		return
	if not _load_database():
		return
	_build_body()
	_root = Node3D.new()
	_root.name = "Crowd"
	add_child(_root)
	_build_crowd()
	_define_cameras()
	info = {"stats": scene_stats(self), "characters": crowd, "database": database.get_info(), "build": build_info,
		"notes": "Motion matching: FellowMotionMatcher3D on the CMU mocap database's locomotion, foot locking and two-bone IK."}


func _bvh_files() -> Array:
	var subjects := str(args.get("matching_subjects", "016,035,069")).split(",", false)
	var files := []
	for sub in subjects:
		var d := data_dir.path_join(sub)
		if not DirAccess.dir_exists_absolute(d):
			continue
		var names := Array(DirAccess.get_files_at(d))
		names.sort()
		for f in names:
			if not f.ends_with(".bvh"):
				continue
			var n := int(f.get_basename().split("_")[1])
			# Subject 16's jumps are not locomotion.
			if sub == "016" and (n <= 7 or n == 9 or n == 10):
				continue
			files.append(d.path_join(f))
	return files


func _load_database() -> bool:
	var files := _bvh_files()
	if files.is_empty():
		load_error = "no CMU BVH files in %s: run benchmarks/scripts/get_cmu_locomotion.sh (or --matching_data=<dir>)" % data_dir
		return false
	# The skeleton (every CMU file has the same hierarchy; the first one's offsets).
	bvh_skeleton = ClassDB.class_call_static("FellowMotionDatabase", "import_bvh", files[0], CMU_SCALE, true)
	if bvh_skeleton.is_empty():
		load_error = "cannot read " + files[0]
		return false
	var t0 := Time.get_ticks_usec()
	if args.get("matching_rebuild", "0") != "1" and ResourceLoader.exists(db_path):
		database = load(db_path)
		build_info = {"loaded_ms": (Time.get_ticks_usec() - t0) / 1000.0, "path": db_path}
		if database and database.get_frame_count() > 0:
			return true
	var anims := []
	for f in files:
		var b: Dictionary = ClassDB.class_call_static("FellowMotionDatabase", "import_bvh", f, CMU_SCALE, true)
		if b.has("animation"):
			anims.append(b["animation"])
	var t1 := Time.get_ticks_usec()
	var skel: Skeleton3D = ClassDB.class_call_static("FellowMotionDatabase", "create_skeleton", bvh_skeleton)
	database = ClassDB.instantiate("FellowMotionDatabase")
	var err: int = database.build(skel, anims, {})
	skel.free()
	var t2 := Time.get_ticks_usec()
	if err != OK:
		load_error = "FellowMotionDatabase.build() failed (%d)" % err
		return false
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(db_path.get_base_dir()))
	var serr := ResourceSaver.save(database, db_path)
	build_info = {"clips": anims.size(), "import_ms": (t1 - t0) / 1000.0, "build_ms": (t2 - t1) / 1000.0,
		"save_ms": (Time.get_ticks_usec() - t2) / 1000.0, "saved": serr == OK, "path": db_path}
	return true


func _setup_environment() -> void:
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var sm := ProceduralSkyMaterial.new()
	sm.sky_top_color = Color(0.32, 0.47, 0.72)
	sm.sky_horizon_color = Color(0.72, 0.76, 0.8)
	sm.ground_horizon_color = Color(0.5, 0.48, 0.45)
	sky.sky_material = sm
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.ssao_enabled = true


func _build_ground() -> void:
	var img := Image.create(256, 256, true, Image.FORMAT_RGB8)
	for y in 256:
		for x in 256:
			var tile := ((x / 32) + (y / 32)) % 2
			var v := 0.46 if tile == 0 else 0.4
			var seam := 1.0 if (x % 32 == 0 or y % 32 == 0) else 0.0
			v -= seam * 0.08
			img.set_pixel(x, y, Color(v, v * 0.98, v * 0.95))
	img.generate_mipmaps()
	var m := StandardMaterial3D.new()
	m.albedo_texture = ImageTexture.create_from_image(img)
	m.roughness = 0.85
	var ground := maxf(_area * 12.0, 240.0)
	m.uv1_scale = Vector3(ground * 0.25, ground * 0.25, 1)
	var mi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(ground, ground)
	mi.mesh = pm
	mi.material_override = m
	add_child(mi)


func _build_lights() -> void:
	sun = DirectionalLight3D.new()
	sun.rotation = Vector3(deg_to_rad(-48.0), deg_to_rad(-35.0), 0.0)
	sun.light_energy = 1.3
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = _area * 2.5
	add_child(sun)


# A mannequin skinned to the skeleton: a tapered prism along each bone (rigid to its parent), a sphere for the head.
func _build_body() -> void:
	var skel: Skeleton3D = ClassDB.class_call_static("FellowMotionDatabase", "create_skeleton", bvh_skeleton)
	var st := SurfaceTool.new()
	st.set_skin_weight_count(SurfaceTool.SKIN_4_WEIGHTS)
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var n := skel.get_bone_count()
	for b in n:
		var p := skel.get_bone_parent(b)
		if p < 0:
			continue
		var a: Vector3 = skel.get_bone_global_rest(p).origin
		var c: Vector3 = skel.get_bone_global_rest(b).origin
		if a.distance_to(c) < 0.01:
			continue
		var pname := skel.get_bone_name(p)
		var r := 0.035
		if pname in ["Hips", "LowerBack", "Spine", "Spine1"]:
			r = 0.09
		elif pname.ends_with("UpLeg"):
			r = 0.065
		elif pname.ends_with("Leg") or pname.ends_with("Arm") and not pname.ends_with("ForeArm"):
			r = 0.05
		elif pname.ends_with("Foot") or pname.ends_with("ForeArm") or pname.begins_with("Neck"):
			r = 0.04
		elif pname.ends_with("Hand") or pname.ends_with("Fingers") or pname.ends_with("FingerBase") or pname.ends_with("Thumb") or pname.ends_with("ToeBase"):
			r = 0.025
		elif pname.ends_with("Shoulder"):
			r = 0.045
		_prism(st, a, c, r, r * 0.8, p)
		if skel.get_bone_name(b) == "Head":
			_sphere(st, c + Vector3(0, 0.1, 0.02), 0.1, b)
	st.generate_normals()
	body_mesh = st.commit()
	body_skin = skel.create_skin_from_rest_transforms()
	skel.free()


func _prism(st: SurfaceTool, a: Vector3, c: Vector3, ra: float, rc: float, bone: int) -> void:
	var axis := (c - a).normalized()
	var u := axis.cross(Vector3.UP if absf(axis.y) < 0.9 else Vector3.RIGHT).normalized()
	var v := axis.cross(u)
	var sides := 8
	var ring_a := []
	var ring_c := []
	for i in sides:
		var ang := TAU * i / sides
		var d := u * cos(ang) + v * sin(ang)
		ring_a.append(a + d * ra)
		ring_c.append(c + d * rc)
	for i in sides:
		var j := (i + 1) % sides
		_tri(st, ring_a[i], ring_c[i], ring_c[j], bone)
		_tri(st, ring_a[i], ring_c[j], ring_a[j], bone)
		_tri(st, a, ring_a[j], ring_a[i], bone)
		_tri(st, c, ring_c[i], ring_c[j], bone)


func _sphere(st: SurfaceTool, center: Vector3, r: float, bone: int) -> void:
	var rings := 6
	var segs := 10
	for i in rings:
		var t0 := PI * i / rings
		var t1 := PI * (i + 1) / rings
		for j in segs:
			var p0 := TAU * j / segs
			var p1 := TAU * (j + 1) / segs
			var a := center + Vector3(sin(t0) * cos(p0), cos(t0), sin(t0) * sin(p0)) * r
			var b := center + Vector3(sin(t1) * cos(p0), cos(t1), sin(t1) * sin(p0)) * r
			var c := center + Vector3(sin(t1) * cos(p1), cos(t1), sin(t1) * sin(p1)) * r
			var d := center + Vector3(sin(t0) * cos(p1), cos(t0), sin(t0) * sin(p1)) * r
			_tri(st, a, c, b, bone)
			_tri(st, a, d, c, bone)


func _tri(st: SurfaceTool, a: Vector3, b: Vector3, c: Vector3, bone: int) -> void:
	for p in [a, b, c]:
		st.set_bones(PackedInt32Array([bone, 0, 0, 0]))
		st.set_weights(PackedFloat32Array([1.0, 0.0, 0.0, 0.0]))
		st.add_vertex(p)


func _new_rng(i: int) -> RandomNumberGenerator:
	var r := RandomNumberGenerator.new()
	r.seed = 7919 * (i + 1)
	return r


func _build_crowd() -> void:
	for c in _chars:
		c.queue_free()
	_chars.clear()
	_matchers.clear()
	_rng_state.clear()
	var cols := int(ceil(sqrt(float(crowd))))
	var lod := str(args.get("matching_lod", "")).split_floats(",", false)
	for i in crowd:
		var r := _new_rng(i)
		var ch := Node3D.new()
		ch.name = "Character%d" % i
		var gx := (i % cols - (cols - 1) * 0.5) * (_area * 1.4 / cols)
		var gz := (i / cols - (cols - 1) * 0.5) * (_area * 1.4 / cols)
		ch.position = Vector3(gx, 0, gz)
		ch.rotation = Vector3(0, r.randf() * TAU, 0)
		_root.add_child(ch)
		var skel: Skeleton3D = ClassDB.class_call_static("FellowMotionDatabase", "create_skeleton", bvh_skeleton)
		ch.add_child(skel)
		var mi := MeshInstance3D.new()
		mi.mesh = body_mesh
		mi.skin = body_skin
		var mat := StandardMaterial3D.new()
		mat.albedo_color = Color.from_hsv(r.randf(), 0.25 + 0.3 * r.randf(), 0.55 + 0.35 * r.randf())
		mat.roughness = 0.6
		mi.material_override = mat
		skel.add_child(mi)
		mi.skeleton = NodePath("..")
		var mm = ClassDB.instantiate("FellowMotionMatcher3D")
		mm.database = database
		mm.auto_update = false
		mm.foot_ik = args.get("matching_ik", "1") == "1"
		mm.foot_locking = args.get("matching_lock", "1") == "1"
		mm.inertialization = args.get("matching_inertialization", "1") == "1"
		mm.synchronization = args.get("matching_sync", "1") == "1"
		mm.search_interval = float(args.get("matching_interval", "0.1"))
		if lod.size() > 0:
			mm.lod_distances = PackedFloat32Array(Array(lod))
		skel.add_child(mm)
		_chars.append(ch)
		_matchers.append(mm)
		# Pace: walkers, joggers, runners, idlers.
		var kind := r.randf()
		var sp := r.randf_range(1.1, 1.5) if kind < 0.45 else (r.randf_range(2.4, 3.0) if kind < 0.75 else (r.randf_range(3.2, 3.6) if kind < 0.9 else r.randf_range(1.0, 1.3)))
		var pause_bias := 0.6 if kind >= 0.9 else 0.25
		_rng_state.append({"wp": _waypoint(r), "pause": r.randf_range(0.0, 1.0), "speed": sp, "rng": r, "pause_bias": pause_bias})
	_last_t = 0.0
	_acc = 0.0


func _waypoint(r: RandomNumberGenerator) -> Vector3:
	return Vector3(r.randf_range(-_area * 0.8, _area * 0.8), 0, r.randf_range(-_area * 0.8, _area * 0.8))


func _steer(i: int, dt: float) -> void:
	var mm = _matchers[i]
	var s: Dictionary = _rng_state[i]
	var pos: Vector3 = mm.get_simulation_position()
	pos.y = 0
	if s["pause"] > 0.0:
		s["pause"] -= dt
		mm.desired_velocity = Vector3.ZERO
		return
	var to: Vector3 = s["wp"] - pos
	var d := to.length()
	if d < 1.0:
		var r: RandomNumberGenerator = s["rng"]
		s["wp"] = _waypoint(r)
		if r.randf() < s["pause_bias"]:
			s["pause"] = r.randf_range(2.0, 5.0)
			mm.desired_velocity = Vector3.ZERO
			return
		to = s["wp"] - pos
		d = to.length()
	var v: Vector3 = to / maxf(d, 1e-3) * float(s["speed"])
	# Separation from the others within 1.6 m.
	var push := Vector3.ZERO
	for j in _matchers.size():
		if j == i:
			continue
		var o: Vector3 = _matchers[j].get_simulation_position()
		o.y = 0
		var dd := pos - o
		var l := dd.length()
		if l > 1e-3 and l < 1.6:
			push += dd / l * (1.6 - l)
	v += push * 2.5
	if v.length() > float(s["speed"]):
		v = v.normalized() * float(s["speed"])
	mm.desired_velocity = v


func _step(dt: float) -> void:
	for i in _matchers.size():
		_steer(i, dt)
	var t0 := Time.get_ticks_usec()
	if args.get("matching_threads", "1") == "1":
		ClassDB.class_call_static("FellowMotionMatcher3D", "advance_group", _matchers, dt)
	else:
		for mm in _matchers:
			mm.advance(dt)
	matcher_wall_usec += Time.get_ticks_usec() - t0
	matcher_steps += 1


func set_time(t: float) -> void:
	if _matchers.is_empty():
		return
	if t + 1e-6 < _last_t:
		_last_t = t
	_acc += t - _last_t
	_last_t = t
	while _acc >= STEP * 0.5:
		_step(STEP)
		_acc -= STEP


func sample_extra() -> Dictionary:
	if _matchers.is_empty():
		return {}
	var g: Dictionary = ClassDB.class_call_static("FellowMotionMatcher3D", "get_global_stats")
	var d := {}
	for k in ["updates", "update_usec", "update_usec_max", "update_usec_p50", "update_usec_p99", "update_usec_p999", "searches", "search_usec", "search_usec_max", "search_usec_p50", "search_usec_p99", "search_usec_p999", "frames_checked", "transitions", "lod0_updates", "lod1_updates", "lod2_updates"]:
		d["matching_" + k] = g.get(k, 0)
	var slide := 0.0
	var slide_locked := 0.0
	var contact := 0.0
	var traj := 0.0
	for mm in _matchers:
		var s: Dictionary = mm.get_stats()
		slide += float(s["foot_slide_distance"])
		slide_locked += float(s["foot_slide_locked"])
		contact += float(s["foot_contact_time"])
		traj += float(s["trajectory_error"])
	d["matching_foot_slide_speed"] = slide / maxf(contact, 1e-6)
	d["matching_foot_slide_locked_speed"] = slide_locked / maxf(contact, 1e-6)
	d["matching_foot_contact_time"] = contact
	d["matching_trajectory_error"] = traj / _matchers.size()
	d["matching_wall_usec_per_step"] = float(matcher_wall_usec) / maxi(matcher_steps, 1)
	return d


func _define_cameras() -> void:
	var h := _area
	fixed_cameras = {
		"matching_aerial": {"pos": Vector3(-h * 0.9, h * 0.7, h * 0.9), "target": Vector3(0, 0, 0), "fov": 55.0, "time": 12.0, "purpose": "the whole crowd from above"},
		"matching_street": {"pos": Vector3(0.0, 1.6, h * 1.05), "target": Vector3(0, 1.0, -h * 0.2), "fov": 55.0, "time": 12.0, "purpose": "eye level at the plaza's edge: walkers, joggers, runners"},
		"matching_close": {"pos": Vector3(2.5, 1.4, 3.5), "target": Vector3(0, 0.95, 0), "fov": 45.0, "time": 12.0, "purpose": "a few characters near the middle, full size"},
	}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(-h * 0.9, h * 0.7, h * 0.9), Vector3(0, 0, 0), 55.0],
		[10.0, Vector3(h * 0.7, h * 0.35, h * 0.7), Vector3(0, 0.5, 0), 55.0],
		[20.0, Vector3(0.0, 1.6, h * 1.05), Vector3(0, 1.0, -h * 0.2), 55.0],
		[30.0, Vector3(h * 0.8, 2.5, -h * 0.6), Vector3(0, 0.8, 0), 55.0],
	])
	temporal_seqs = {
		"matching_walk": {"start": 6.0, "frames": 240, "purpose": "4 s of the crowd at eye level",
			"path": CameraPath.from_array([[0.0, Vector3(-4, 1.6, h * 1.0), Vector3(0, 1.0, -h * 0.2), 50.0], [4.0, Vector3(4, 1.6, h * 1.0), Vector3(0, 1.0, -h * 0.2), 50.0]])},
		"matching_close": {"start": 20.0, "frames": 300, "purpose": "5 s from a low camera inside the plaza: feet, turns, stops",
			"path": CameraPath.from_array([[0.0, Vector3(0.0, 1.0, h * 0.45), Vector3(0, 0.6, 0.0), 40.0], [5.0, Vector3(0.0, 1.0, h * 0.45), Vector3(0, 0.6, 0.0), 40.0]])},
	}
