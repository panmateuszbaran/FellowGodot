extends SceneTree
## FellowEngine milestone 73 check (decision 0181): FellowCloth3D; milestone 76 (decision 0184): pinned to bones;
## milestone 112 (decisions 0220, 0221): tearing, self-collision.
##
##   godot --path benchmarks/project --headless --fixed-fps 60 -s res://bench/cloth_check.gd -- --out=<dir>
##   godot --path benchmarks/project --fixed-fps 60 --resolution 1280x720 -s res://bench/cloth_check.gd
##       -- --out=<dir> --pictures=1 [--frames=1] [--grid=1 --shot=640x360] [--motion=1]
##   godot ... -s res://bench/cloth_check.gd -- --out=<dir> --many=64 [--seconds=20] [--tear_strain=0.5] [--self=1]
##       [--self_tri=1] [--self_friction=1]
##
## Lanes, each a cloth in a situation: a flag on a pole in the wind (FellowWind3D, gusts), a curtain hanging still, a
## sheet dropped on a sphere and a box (the physics world's), a sheet dropped on a height map, a cape pinned at two
## corners on a node circling a capsule it collides with, an awning pinned at its corners, a banner from a source mesh
## (a PlaneMesh), a flag hit by an impulse, a flag beyond the simulation distance, a cape pinned to a skeleton's chest
## bone (twisting) on a body circling, colliding with capsules on its bones (the pins' drift in the bone's space
## measured); milestone 112 (decision 0220): a curtain with a hole cut in it (tear()), one cut and made whole again
## (reset()), a sheet held at its corners a ball goes through (tear_strain 0.5, and 0), a flag cut off its pole (its
## cut part on the ground); decision 0221: a sheet let go of its rail falling in a heap, with self-collision (no two
## particles closer than half its distance, but for neighbours at rest) and without; milestone 115 (decisions 0228,
## 0229): the heap in still air with the layers a quarter of an edge apart, by particles only and against the triangles
## too (no particle within half the distance of another layer's triangle), and the sheet heaped on a 25-degree slope
## without and with friction between its layers (held on it). For each after the run: the largest
## strain of the grid's edges (stretched over rest), positions finite, the deepest a particle went into a collider,
## where the cloth went (a flag downwind, a curtain hanging, a sheet resting on what it fell on). The cost: the
## simulation's time a frame (--many=N flags in the wind). With --pictures=1 the lanes filmed (cloth_strip.png),
## --motion=1 the motion vectors drawn (cloth_motion.png: the cloth's own motion in them). One JSON line:
## CLOTH_CHECK {...} (CLOTH_MANY {...}); exit code 1 when a lane failed.

const RUN_SECONDS := 4.0

var args := {}
var out_dir := ""
var result := {}
var root3d: Node3D
var lanes: Array = []
var frame := 0
var pictures := false
var save_frames := false
var grid := false
var motion := false
var shot_size := Vector2i(480, 320)
var many := 0
var many_seconds := 20.0
var many_frames: PackedFloat64Array = []
var many_last := 0
var wind: FellowWind3D
var mover: Node3D
var mover_capsule: CollisionShape3D
var walker: Node3D # milestone 76: the cape on a skeleton's chest
var walker_skeleton: Skeleton3D
var walker_capsules: Array = []
var done := false
var shots := {}
var viewports := {}
var strip_lanes := ["flag", "curtain", "sheet_on_shapes", "cape", "awning", "banner_mesh", "cape_bones", "tear_cut", "tear_strain", "tear_off", "self_heap", "self_heap_off", "self_heap_thin", "self_heap_tri", "self_slope", "self_slope_friction"]
var strip_times := [0.5, 1.0, 2.0, 3.9]


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://cloth_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	pictures = _arg("pictures", "0") == "1"
	save_frames = _arg("frames", "0") == "1"
	grid = _arg("grid", "0") == "1"
	motion = _arg("motion", "0") == "1"
	many = int(_arg("many", "0"))
	many_seconds = float(_arg("seconds", "20"))
	var ss: PackedStringArray = _arg("shot", "480x320").split("x")
	shot_size = Vector2i(int(ss[0]), int(ss[1]))
	_build.call_deferred()


func _material(c: Color, two_sided := false) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.85
	if two_sided:
		m.cull_mode = BaseMaterial3D.CULL_DISABLED
	return m


func _static(shape: Shape3D, xf: Transform3D, mesh: Mesh, color := Color(0.6, 0.6, 0.62)) -> StaticBody3D:
	var b := StaticBody3D.new()
	b.transform = xf
	var cs := CollisionShape3D.new()
	cs.shape = shape
	b.add_child(cs)
	if mesh:
		var mi := MeshInstance3D.new()
		mi.mesh = mesh
		mi.material_override = _material(color)
		b.add_child(mi)
	root3d.add_child(b)
	return b


func _pole(at: Vector3, height: float) -> void:
	var mi := MeshInstance3D.new()
	var cyl := CylinderMesh.new()
	cyl.top_radius = 0.035
	cyl.bottom_radius = 0.035
	cyl.height = height
	mi.mesh = cyl
	mi.material_override = _material(Color(0.35, 0.33, 0.3))
	mi.position = at + Vector3(0, height * 0.5, 0)
	root3d.add_child(mi)


func _cloth(name: String, xf: Transform3D, size: Vector2, res: int, pin: int, color: Color) -> FellowCloth3D:
	var c := FellowCloth3D.new()
	c.name = name
	c.size = size
	c.resolution = res
	c.pin_mode = pin
	c.material = _material(color, true)
	c.transform = xf
	c.lod_distance = 0.0 # the lanes at full quality (the course's camera is far); the "lod" lane has its own
	root3d.add_child(c)
	return c


func _lane(name: String, cloth: FellowCloth3D, x: float) -> Dictionary:
	var lane := {"name": name, "cloth": cloth, "x": x, "max_strain": 0.0, "strain_now": 0.0, "strain_mean_now": 0.0, "finite": true, "deepest": 0.0, "colliders": []}
	lanes.append(lane)
	return lane


func _grid_dims(c: FellowCloth3D) -> Vector2i:
	var longest := maxf(maxf(c.size.x, c.size.y), 0.001)
	return Vector2i(maxi(2, roundi(c.resolution * c.size.x / longest) + 1), maxi(2, roundi(c.resolution * c.size.y / longest) + 1))


func _build() -> void:
	root3d = Node3D.new()
	get_root().add_child(root3d)
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.55, 0.65, 0.78)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.6, 0.62, 0.66)
	e.ambient_light_energy = 0.6
	env.environment = e
	root3d.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-45, 30, 0)
	sun.shadow_enabled = true
	root3d.add_child(sun)
	wind = FellowWind3D.new()
	wind.speed = 7.0
	wind.gustiness = 0.5
	wind.rotation_degrees = Vector3(0, -90, 0) # blowing towards +X
	root3d.add_child(wind)
	# The ground (a world boundary: a plane).
	var ground := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(400, 60)
	ground.mesh = pm
	ground.material_override = _material(Color(0.36, 0.42, 0.34))
	ground.position = Vector3(150, 0, 0)
	root3d.add_child(ground)
	_static(WorldBoundaryShape3D.new(), Transform3D(), null)

	if many > 0:
		_build_many()
		physics_frame.connect(_on_frame)
		return

	var x := 0.0
	# A flag on a pole: its left edge pinned, the wind along +X.
	_pole(Vector3(x, 0, 0), 4.0)
	var flag := _cloth("Flag", Transform3D(Basis(), Vector3(x + 0.04, 3.9, 0)), Vector2(1.8, 1.1), 24, FellowCloth3D.PIN_LEFT_EDGE, Color(0.75, 0.12, 0.1))
	_lane("flag", flag, x)
	x += 8.0
	# A curtain: its top edge pinned, no wind on it.
	var curtain := _cloth("Curtain", Transform3D(Basis(), Vector3(x - 1.0, 3.0, 0)), Vector2(2.0, 2.4), 24, FellowCloth3D.PIN_TOP_EDGE, Color(0.2, 0.3, 0.7))
	curtain.wind_scale = 0.0
	curtain.turbulence = 0.0
	_lane("curtain", curtain, x)
	x += 8.0
	# A sheet dropped on a sphere and a box (the world's colliders, found by the cloth's query).
	var sph := SphereShape3D.new()
	sph.radius = 0.5
	var sm := SphereMesh.new()
	sm.radius = 0.5
	sm.height = 1.0
	_static(sph, Transform3D(Basis(), Vector3(x - 0.6, 0.5, 0)), sm)
	var bx := BoxShape3D.new()
	bx.size = Vector3(0.8, 0.8, 0.8)
	var bm := BoxMesh.new()
	bm.size = bx.size
	_static(bx, Transform3D(Basis(), Vector3(x + 0.7, 0.4, 0)), bm)
	var sheet := _cloth("Sheet", Transform3D(Basis(Vector3(1, 0, 0), deg_to_rad(-90.0)), Vector3(x - 1.5, 1.6, -1.25)), Vector2(3.0, 2.5), 30, FellowCloth3D.PIN_NONE, Color(0.85, 0.82, 0.7))
	sheet.wind_scale = 0.0
	sheet.turbulence = 0.0
	var l := _lane("sheet_on_shapes", sheet, x)
	l["colliders"] = [["sphere", Vector3(x - 0.6, 0.5, 0), 0.5], ["box", Vector3(x + 0.7, 0.4, 0), Vector3(0.4, 0.4, 0.4)]]
	x += 8.0
	# A sheet dropped on a height map.
	var hm := HeightMapShape3D.new()
	hm.map_width = 9
	hm.map_depth = 9
	var heights := PackedFloat32Array()
	for j in 9:
		for i in 9:
			heights.append(0.4 * sin(i * 0.8) * cos(j * 0.7) + 0.5)
	hm.map_data = heights
	_static(hm, Transform3D(Basis(), Vector3(x, 0, 0)), null)
	var sheet2 := _cloth("SheetOnMap", Transform3D(Basis(Vector3(1, 0, 0), deg_to_rad(-90.0)), Vector3(x - 1.5, 1.8, -1.5)), Vector2(3.0, 3.0), 24, FellowCloth3D.PIN_NONE, Color(0.8, 0.7, 0.5))
	sheet2.wind_scale = 0.0
	sheet2.turbulence = 0.0
	l = _lane("sheet_on_heightmap", sheet2, x)
	l["heightmap"] = [hm, Vector3(x, 0, 0), Vector2(1, 1)]
	x += 8.0
	# A cape: two corners pinned on a node circling at walking speed; a capsule (its body) it collides with.
	mover = Node3D.new()
	mover.position = Vector3(x, 0, 0)
	root3d.add_child(mover)
	var body := CharacterBody3D.new()
	mover.add_child(body)
	mover_capsule = CollisionShape3D.new()
	var cap := CapsuleShape3D.new()
	cap.radius = 0.25
	cap.height = 1.6
	mover_capsule.shape = cap
	mover_capsule.position = Vector3(0, 1.0, 0)
	body.add_child(mover_capsule)
	var capm := MeshInstance3D.new()
	var cm := CapsuleMesh.new()
	cm.radius = 0.25
	cm.height = 1.6
	capm.mesh = cm
	capm.material_override = _material(Color(0.3, 0.32, 0.36))
	capm.position = mover_capsule.position
	body.add_child(capm)
	var cape := FellowCloth3D.new()
	cape.name = "Cape"
	cape.size = Vector2(0.6, 1.1)
	cape.resolution = 16
	cape.pin_mode = FellowCloth3D.PIN_TOP_CORNERS
	cape.material = _material(Color(0.5, 0.1, 0.45), true)
	cape.position = Vector3(-0.3, 1.65, 0.27) # across the back (+Z: the body walks towards -Z)
	cape.collide_with_world = false
	mover.add_child(cape)
	cape.colliders = [cape.get_path_to(mover_capsule)]
	l = _lane("cape", cape, x)
	l["capsule"] = mover_capsule
	x += 8.0
	# An awning: its corners pinned, lying flat (turned about X), sagging.
	var awning := _cloth("Awning", Transform3D(Basis(Vector3(1, 0, 0), deg_to_rad(-90.0)), Vector3(x - 1.0, 2.5, -1.0)), Vector2(2.0, 2.0), 20, FellowCloth3D.PIN_CORNERS, Color(0.9, 0.75, 0.2))
	awning.wind_scale = 0.3
	_lane("awning", awning, x)
	x += 8.0
	# A banner from a source mesh: a PlaneMesh facing +Z, subdivided; its top edge pinned.
	var src := PlaneMesh.new()
	src.size = Vector2(1.2, 2.0)
	src.subdivide_width = 11
	src.subdivide_depth = 19
	src.orientation = PlaneMesh.FACE_Z
	var banner := FellowCloth3D.new()
	banner.name = "Banner"
	banner.source_mesh = src
	banner.pin_mode = FellowCloth3D.PIN_TOP_EDGE
	banner.material = _material(Color(0.1, 0.5, 0.3), true)
	banner.wind_scale = 0.0
	banner.turbulence = 0.0
	banner.position = Vector3(x, 2.0, 0)
	root3d.add_child(banner)
	_lane("banner_mesh", banner, x)
	x += 8.0
	# A flag hit by an impulse at 2 s (no wind on it).
	_pole(Vector3(x, 0, 0), 4.0)
	var hit := _cloth("HitFlag", Transform3D(Basis(), Vector3(x + 0.04, 3.9, 0)), Vector2(1.8, 1.1), 24, FellowCloth3D.PIN_LEFT_EDGE, Color(0.9, 0.9, 0.9))
	hit.wind_scale = 0.0
	hit.turbulence = 0.0
	_lane("impulse", hit, x)
	x += 8.0
	# A flag at the level of detail past lod_distance (half the substeps), in the wind.
	_pole(Vector3(x, 0, 0), 4.0)
	var lod := _cloth("LodFlag", Transform3D(Basis(), Vector3(x + 0.04, 3.9, 0)), Vector2(1.8, 1.1), 24, FellowCloth3D.PIN_LEFT_EDGE, Color(0.2, 0.6, 0.8))
	lod.lod_distance = 1.0
	_lane("lod", lod, x)
	x += 8.0
	# A flag beyond its simulation distance.
	var far := _cloth("FarFlag", Transform3D(Basis(), Vector3(x + 0.04, 3.9, 0)), Vector2(1.8, 1.1), 24, FellowCloth3D.PIN_LEFT_EDGE, Color(0.5, 0.5, 0.5))
	far.simulation_distance = 1.0
	_lane("far", far, x)

	# Milestone 76: a cape on a skeleton — its top edge pinned to the Chest bone (the chest twisting 35 degrees each way
	# twice a second), the body circling at 1.6 m/s, colliding with capsules on the hips and the chest (BoneAttachment3Ds).
	x += 8.0
	walker = Node3D.new()
	walker.position = Vector3(x, 0, 0)
	root3d.add_child(walker)
	walker_skeleton = Skeleton3D.new()
	walker.add_child(walker_skeleton)
	for b in [["Hips", "", Vector3(0, 0.95, 0)], ["Spine", "Hips", Vector3(0, 0.15, 0)], ["Chest", "Spine", Vector3(0, 0.25, 0)], ["Neck", "Chest", Vector3(0, 0.25, 0)]]:
		var bi := walker_skeleton.add_bone(b[0])
		if b[1] != "":
			walker_skeleton.set_bone_parent(bi, walker_skeleton.find_bone(b[1]))
		walker_skeleton.set_bone_rest(bi, Transform3D(Basis(), b[2]))
		walker_skeleton.set_bone_pose_position(bi, b[2])
	var wmat := _material(Color(0.6, 0.6, 0.62))
	for b in [["Hips", 0.2, 0.55, Vector3(0, 0.05, 0)], ["Chest", 0.21, 0.6, Vector3(0, 0.05, 0)]]:
		var wat := BoneAttachment3D.new()
		wat.bone_name = b[0]
		walker_skeleton.add_child(wat)
		var wcs := CollisionShape3D.new()
		var bcap := CapsuleShape3D.new()
		bcap.radius = b[1]
		bcap.height = b[2]
		wcs.shape = bcap
		wcs.position = b[3]
		wat.add_child(wcs)
		var wmi := MeshInstance3D.new()
		var wcm := CapsuleMesh.new()
		wcm.radius = b[1]
		wcm.height = b[2]
		wmi.mesh = wcm
		wmi.material_override = wmat
		wmi.position = b[3]
		wat.add_child(wmi)
		walker_capsules.append(wcs)
	var bcape := _cloth("BoneCape", Transform3D(Basis(), Vector3(-0.4, 1.55, 0.24)), Vector2(0.8, 1.1), 16, FellowCloth3D.PIN_TOP_EDGE, Color(0.15, 0.3, 0.6))
	bcape.get_parent().remove_child(bcape)
	walker.add_child(bcape)
	bcape.collide_with_world = false
	bcape.wind_scale = 0.25 # the scene's wind is the flags' gale: the cape swings with the body more than it streams
	bcape.skeleton_path = bcape.get_path_to(walker_skeleton)
	bcape.pin_bones = PackedStringArray(["Chest"])
	var wpaths := []
	for wc in walker_capsules:
		wpaths.append(bcape.get_path_to(wc))
	bcape.colliders = wpaths
	l = _lane("cape_bones", bcape, x)
	l["bone_capsules"] = walker_capsules

	# Milestone 112 (decision 0220): tearing. A curtain cut at its middle at 1.1 s (tear(): a hole), one cut and made
	# whole again (reset()), a sheet held at its four corners a ball goes through at 8 m/s, tearing at half again an
	# edge's length (and one that does not tear), a flag in a breeze cut off its pole (the cut part falls to the ground).
	x += 8.0
	var tc := _cloth("TearCut", Transform3D(Basis(), Vector3(x - 1.0, 3.0, 0)), Vector2(2.0, 2.4), 24, FellowCloth3D.PIN_TOP_EDGE, Color(0.7, 0.25, 0.2))
	tc.wind_scale = 0.0
	tc.turbulence = 0.0
	_watch_tears(_lane("tear_cut", tc, x))
	x += 8.0
	var trs := _cloth("TearReset", Transform3D(Basis(), Vector3(x - 1.0, 3.0, 0)), Vector2(2.0, 2.4), 24, FellowCloth3D.PIN_TOP_EDGE, Color(0.6, 0.3, 0.25))
	trs.wind_scale = 0.0
	trs.turbulence = 0.0
	_watch_tears(_lane("tear_reset", trs, x))
	for lane_name in ["tear_strain", "tear_strain_off"]:
		x += 8.0
		var ts := _cloth("Tear_" + lane_name, Transform3D(Basis(), Vector3(x - 1.0, 3.0, 0)), Vector2(2.0, 2.0), 24, FellowCloth3D.PIN_CORNERS, Color(0.85, 0.6, 0.3))
		ts.wind_scale = 0.0
		ts.turbulence = 0.0
		ts.tear_strain = 0.5 if lane_name == "tear_strain" else 0.0
		var ball := StaticBody3D.new() # moved by the script: the cloth finds it in the physics world
		var bcs := CollisionShape3D.new()
		var bsp := SphereShape3D.new()
		bsp.radius = 0.3
		bcs.shape = bsp
		ball.add_child(bcs)
		var bmi := MeshInstance3D.new()
		var bsm := SphereMesh.new()
		bsm.radius = 0.3
		bsm.height = 0.6
		bmi.mesh = bsm
		bmi.material_override = _material(Color(0.25, 0.25, 0.28))
		ball.add_child(bmi)
		ball.position = Vector3(x, 2.0, 3.0)
		root3d.add_child(ball)
		l = _lane(lane_name, ts, x)
		l["ball"] = ball
		_watch_tears(l)
	x += 8.0
	_pole(Vector3(x, 0, 0), 4.0)
	var toff := _cloth("TearOff", Transform3D(Basis(), Vector3(x + 0.04, 3.9, 0)), Vector2(1.8, 1.1), 24, FellowCloth3D.PIN_LEFT_EDGE, Color(0.9, 0.8, 0.3))
	toff.wind_scale = 0.3 # a breeze: the cut part does not land flat on its edge and stand
	_watch_tears(_lane("tear_off", toff, x))
	# Milestone 112 (decision 0221): self-collision. A sheet hung by its top edge, turned to the breeze, let go at 0.5 s:
	# it falls in a heap; and the same without self-collision.
	# Milestone 115: the same heap, in still air, with the layers as close as a quarter of an edge — particles only (they
	# slip between each other's), and kept off the triangles too (decision 0228).
	for lane_name in ["self_heap", "self_heap_off", "self_heap_thin", "self_heap_tri"]:
		x += 8.0
		var sc := _cloth("Self_" + lane_name, Transform3D(Basis(Vector3.UP, deg_to_rad(30.0)), Vector3(x - 0.9, 2.6, 0)), Vector2(1.8, 2.4), 24, FellowCloth3D.PIN_TOP_EDGE, Color(0.55, 0.75, 0.5))
		sc.wind_scale = 0.3
		sc.simulation_distance = 1000.0 # far from the course's camera
		sc.self_collision = not lane_name.ends_with("_off")
		if lane_name in ["self_heap_thin", "self_heap_tri"]:
			sc.wind_scale = 0.0 # still air
			sc.self_collision_distance = 0.028 # a quarter of the mean edge (0.113 m)
			sc.self_collision_triangles = lane_name == "self_heap_tri"
		_lane(lane_name, sc, x)
	# Decision 0229: the sheet let go over a 25-degree slope (its friction with colliders 1): it falls in a heap on it,
	# whose upper layers slide down over the lower ones — and pull them along — without friction between the layers.
	for lane_name in ["self_slope", "self_slope_friction"]:
		x += 10.0
		var slope := BoxShape3D.new()
		slope.size = Vector3(6.0, 0.2, 3.0)
		var slope_mesh := BoxMesh.new()
		slope_mesh.size = slope.size
		_static(slope, Transform3D(Basis(Vector3(0, 0, 1), deg_to_rad(-25.0)), Vector3(x, 1.0, 0)), slope_mesh)
		var ss := _cloth("Self_" + lane_name, Transform3D(Basis(Vector3.UP, deg_to_rad(90.0)), Vector3(x, 4.2, 0.9)), Vector2(1.8, 2.4), 24, FellowCloth3D.PIN_TOP_EDGE, Color(0.55, 0.75, 0.5))
		ss.wind_scale = 0.0
		ss.friction = 1.0
		ss.simulation_distance = 1000.0
		ss.self_collision = true
		ss.self_collision_distance = 0.028
		ss.self_collision_triangles = true
		ss.self_collision_friction = 1.0 if lane_name == "self_slope_friction" else 0.0
		_lane(lane_name, ss, x)

	var cam := Camera3D.new()
	root3d.add_child(cam)
	cam.look_at_from_position(Vector3(40, 9, 22), Vector3(40, 1.5, 0))
	if pictures:
		_make_viewports()
	physics_frame.connect(_on_frame)


func _build_many() -> void:
	# N flags on poles in rows, the wind on all of them; the camera among them (the nearest within simulation reach).
	var cols := int(ceil(sqrt(float(many))))
	for i in many:
		var p := Vector3(float(i % cols) * 4.0, 0, float(i / cols) * 4.0)
		_pole(p, 4.0)
		var f := _cloth("Flag%d" % i, Transform3D(Basis(), p + Vector3(0.04, 3.9, 0)), Vector2(1.8, 1.1), 24, FellowCloth3D.PIN_LEFT_EDGE, Color.from_hsv(float(i) / many, 0.6, 0.8))
		f.simulation_distance = 1000.0
		f.lod_distance = float(_arg("lod", "1000"))
		f.tear_strain = float(_arg("tear_strain", "0")) # milestone 112: the check's cost (a flag in the wind does not tear)
		f.self_collision = _arg("self", "0") == "1" # milestone 112: self-collision's cost
		f.self_collision_triangles = _arg("self_tri", "0") == "1" # milestone 115: against the triangles too
		f.self_collision_friction = float(_arg("self_friction", "0"))
	var cam := Camera3D.new()
	root3d.add_child(cam)
	cam.look_at_from_position(Vector3(-6, 8, -6), Vector3(cols * 2.0, 2, cols * 2.0))


func _make_viewports() -> void:
	var grid_box: GridContainer
	if grid:
		grid_box = GridContainer.new()
		grid_box.columns = 3
		grid_box.add_theme_constant_override("h_separation", 0)
		grid_box.add_theme_constant_override("v_separation", 0)
		get_root().add_child(grid_box)
	for name in strip_lanes:
		var lane: Dictionary = _find(name)
		var vp := SubViewport.new()
		vp.size = shot_size
		vp.world_3d = get_root().world_3d
		vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		if motion and name == "flag":
			vp.debug_draw = Viewport.DEBUG_DRAW_MOTION_VECTORS
		var cam := Camera3D.new()
		cam.fov = 50
		vp.add_child(cam)
		var x: float = lane.x
		match name:
			"flag":
				cam.look_at_from_position(Vector3(x + 1.0, 3.0, 4.2), Vector3(x + 1.0, 3.2, 0))
			"curtain":
				cam.look_at_from_position(Vector3(x + 1.5, 2.2, 4.5), Vector3(x, 1.8, 0))
			"sheet_on_shapes":
				cam.look_at_from_position(Vector3(x + 1.0, 2.6, 4.0), Vector3(x, 0.5, 0))
			"cape":
				cam.look_at_from_position(Vector3(x + 3.0, 2.0, 3.5), Vector3(x, 1.2, 0))
			"awning":
				cam.look_at_from_position(Vector3(x + 2.5, 1.2, 3.5), Vector3(x, 2.2, 0))
			"banner_mesh":
				cam.look_at_from_position(Vector3(x + 2.0, 2.2, 3.5), Vector3(x, 2.0, 0))
			"tear_cut":
				cam.look_at_from_position(Vector3(x + 1.5, 2.2, 4.5), Vector3(x, 1.8, 0))
			"tear_strain":
				cam.look_at_from_position(Vector3(x + 3.0, 2.4, 3.5), Vector3(x, 2.0, 0))
			"tear_off":
				cam.look_at_from_position(Vector3(x + 1.0, 2.2, 4.8), Vector3(x + 1.0, 1.9, 0))
			"self_heap", "self_heap_off", "self_heap_thin", "self_heap_tri":
				cam.look_at_from_position(Vector3(x + 2.0, 1.9, 3.4), Vector3(x + 0.7, 0.15, 0.4))
			"self_slope", "self_slope_friction":
				cam.look_at_from_position(Vector3(x + 1.0, 3.0, 5.0), Vector3(x + 0.5, 1.2, 0.0))
			"cape_bones":
				cam.look_at_from_position(Vector3(x + 4.0, 2.4, 5.0), Vector3(x, 1.0, 0))
		if grid:
			var cell := Control.new()
			cell.custom_minimum_size = Vector2(shot_size)
			var holder := SubViewportContainer.new()
			holder.size = Vector2(shot_size)
			holder.add_child(vp)
			cell.add_child(holder)
			var label := Label.new()
			label.text = String(name)
			label.add_theme_font_size_override("font_size", 20)
			label.add_theme_color_override("font_outline_color", Color.BLACK)
			label.add_theme_constant_override("outline_size", 6)
			label.position = Vector2(10, 6)
			cell.add_child(label)
			grid_box.add_child(cell)
		else:
			get_root().add_child(vp)
		viewports[name] = vp


func _watch_tears(lane: Dictionary) -> void:
	lane["tears"] = []
	(lane.cloth as FellowCloth3D).torn.connect(func(pos: Vector3, edges: int) -> void: lane.tears.append([pos, edges, frame]))


func _find(name: String) -> Dictionary:
	for lane in lanes:
		if lane.name == name:
			return lane
	return {}


func _measure(lane: Dictionary, pins := false) -> void:
	var c: FellowCloth3D = lane.cloth
	var n := c.get_particle_count()
	for i in n:
		var p := c.get_particle_position(i)
		if not (is_finite(p.x) and is_finite(p.y) and is_finite(p.z)):
			lane.finite = false
			return
	if c.source_mesh == null:
		var d := _grid_dims(c)
		var dx := c.size.x / (d.x - 1)
		var dy := c.size.y / (d.y - 1)
		var worst := 0.0
		var sum := 0.0
		var count := 0
		for j in d.y:
			for i in d.x:
				var k := j * d.x + i
				if i + 1 < d.x:
					var s := c.get_particle_position(k).distance_to(c.get_particle_position(k + 1)) / dx - 1.0
					worst = maxf(worst, s)
					sum += maxf(s, 0.0)
					count += 1
				if j + 1 < d.y:
					var s2 := c.get_particle_position(k).distance_to(c.get_particle_position(k + d.x)) / dy - 1.0
					worst = maxf(worst, s2)
					sum += maxf(s2, 0.0)
					count += 1
		lane.max_strain = maxf(lane.max_strain, worst)
		lane.strain_now = worst
		lane.strain_mean_now = sum / maxf(1.0, float(count))
	for col in lane.colliders:
		for i in n:
			var p := c.get_particle_position(i)
			var depth := 0.0
			if col[0] == "sphere":
				depth = col[2] - p.distance_to(col[1])
			else:
				var q: Vector3 = (p - col[1]).abs()
				var h: Vector3 = col[2]
				if q.x < h.x and q.y < h.y and q.z < h.z:
					depth = minf(minf(h.x - q.x, h.y - q.y), h.z - q.z)
			lane.deepest = maxf(lane.deepest, depth)
	if lane.has("bone_capsules") and pins:
		var chest_xf := walker_skeleton.global_transform * walker_skeleton.get_bone_global_pose(walker_skeleton.find_bone("Chest"))
		var inv := chest_xf.affine_inverse()
		if not lane.has("pin_local"):
			var pl := {}
			for i in n:
				if c.is_particle_pinned(i):
					pl[i] = inv * c.get_particle_position(i)
			lane["pin_local"] = pl
			lane["pin_drift"] = 0.0
		for i in lane.pin_local:
			lane.pin_drift = maxf(lane.pin_drift, (inv * c.get_particle_position(i)).distance_to(lane.pin_local[i]))
		for cs in lane.bone_capsules:
			var capsule: CapsuleShape3D = (cs as CollisionShape3D).shape
			var gx: Transform3D = (cs as CollisionShape3D).global_transform
			var half := capsule.height * 0.5 - capsule.radius
			var a := gx * Vector3(0, half, 0)
			var b := gx * Vector3(0, -half, 0)
			for i in n:
				var p := c.get_particle_position(i)
				var tt := clampf((p - b).dot(a - b) / maxf((a - b).length_squared(), 1e-9), 0.0, 1.0)
				lane.deepest = maxf(lane.deepest, capsule.radius - p.distance_to(b.lerp(a, tt)))
	if lane.has("capsule"):
		var cs: CollisionShape3D = lane.capsule
		var a := cs.global_position + Vector3(0, 0.55, 0)
		var b := cs.global_position - Vector3(0, 0.55, 0)
		for i in n:
			var p := c.get_particle_position(i)
			var t := clampf((p - b).dot(a - b) / (a - b).length_squared(), 0.0, 1.0)
			lane.deepest = maxf(lane.deepest, 0.25 - p.distance_to(b.lerp(a, t)))
	if lane.has("heightmap"):
		var hm: HeightMapShape3D = lane.heightmap[0]
		var o: Vector3 = lane.heightmap[1]
		var sc: Vector2 = lane.heightmap[2]
		for i in n:
			var p := c.get_particle_position(i)
			var fx := (p.x - o.x) / sc.x + 4.0
			var fz := (p.z - o.z) / sc.y + 4.0
			if fx >= 0.0 and fz >= 0.0 and fx < 8.0 and fz < 8.0:
				var ix := int(fx)
				var iz := int(fz)
				var tx := fx - ix
				var tz := fz - iz
				var hh := hm.map_data
				var h := lerpf(lerpf(hh[iz * 9 + ix], hh[iz * 9 + ix + 1], tx), lerpf(hh[(iz + 1) * 9 + ix], hh[(iz + 1) * 9 + ix + 1], tx), tz)
				lane.deepest = maxf(lane.deepest, h - p.y)


func _on_frame() -> void:
	if done:
		return
	frame += 1
	if many > 0:
		_many_tick()
		return
	if mover:
		# The cape's node walks a circle of 2.5 m at 1.6 m/s, facing where it goes.
		var t := frame / 60.0
		var a := t * 1.6 / 2.5
		var lx: float = _find("cape").x
		mover.position = Vector3(lx + cos(a) * 2.5, 0, sin(a) * 2.5)
		mover.look_at(mover.position + Vector3(-sin(a), 0, cos(a)), Vector3.UP)
	if frame == 120:
		var hit: FellowCloth3D = _find("impulse").cloth
		hit.apply_impulse(hit.global_position + Vector3(1.2, -0.5, 0), Vector3(0, 0, 0.4), 0.6)
	# Milestone 112: the tears.
	if frame == 66:
		for nm in ["tear_cut", "tear_reset"]:
			var tcl: FellowCloth3D = _find(nm).cloth
			_find(nm)["torn_by_call"] = tcl.tear(tcl.global_position + Vector3(1.0, -1.2, 0.0), 0.25)
		# The flag cut between its 4th and 5th columns: each row's edge across the gap at its middle (the cells there go).
		var tof: Dictionary = _find("tear_off")
		var fc: FellowCloth3D = tof.cloth
		var fd := _grid_dims(fc)
		var cut := 0
		for j in fd.y:
			cut += fc.tear((fc.get_particle_position(j * fd.x + 3) + fc.get_particle_position(j * fd.x + 4)) * 0.5, 0.02)
		tof["torn_by_call"] = cut
	if frame == 150:
		(_find("tear_reset").cloth as FellowCloth3D).reset()
	if frame == 30:
		for nm in ["self_heap", "self_heap_off", "self_heap_thin", "self_heap_tri", "self_slope", "self_slope_friction"]:
			var hc: FellowCloth3D = _find(nm).cloth
			for i in hc.get_particle_count():
				if hc.is_particle_pinned(i):
					hc.set_particle_pinned(i, false)
	if frame == 210:
		# Milestone 115: the heaps' particles half a second before the end (how much they still move).
		for nm in ["self_heap", "self_heap_off", "self_heap_thin", "self_heap_tri", "self_slope", "self_slope_friction"]:
			var hc2: FellowCloth3D = _find(nm).cloth
			var at := []
			for i in hc2.get_particle_count():
				at.append(hc2.get_particle_position(i))
			_find(nm)["pos_210"] = at
	for lane in lanes:
		if lane.has("ball"):
			# Thrown along -Z at 8 m/s from 0.8 s: through the curtain's middle at about 1.18 s.
			(lane.ball as Node3D).position = Vector3(lane.x, 2.0, 3.0 - 8.0 * maxf(0.0, frame / 60.0 - 0.8))
	if frame > 30 and frame % 10 == 0:
		for lane in lanes:
			_measure(lane, true)
	# The walker moved after the measurement: its cloth was simulated (in the frame's processing) with the pose it had.
	if walker:
		var tw := frame / 60.0
		var aw := tw * 1.6 / 2.5
		var wx: float = _find("cape_bones").x
		walker.position = Vector3(wx + cos(aw) * 2.5, 0, sin(aw) * 2.5)
		walker.look_at(walker.position + Vector3(-sin(aw), 0, cos(aw)), Vector3.UP)
		var chest := walker_skeleton.find_bone("Chest")
		walker_skeleton.set_bone_pose_rotation(chest, Quaternion(Vector3.UP, deg_to_rad(35.0) * sin(tw * TAU * 2.0)))
	if pictures:
		_shoot()
	if frame >= int(RUN_SECONDS * 60.0):
		done = true
		_finish.call_deferred()


func _shoot() -> void:
	for name in strip_lanes:
		var vp: SubViewport = viewports[name]
		if save_frames:
			DirAccess.make_dir_recursive_absolute(out_dir.path_join("frames_" + name))
			vp.get_texture().get_image().save_png(out_dir.path_join("frames_%s/%04d.png" % [name, frame]))
		for k in strip_times.size():
			if frame == int(strip_times[k] * 60.0):
				if not shots.has(name):
					shots[name] = {}
				shots[name][k] = vp.get_texture().get_image()


func _finish() -> void:
	var failures := []
	var out := {}
	for lane in lanes:
		_measure(lane)
		var c: FellowCloth3D = lane.cloth
		var st := c.get_stats()
		var e := {"particles": st.particles, "triangles": st.triangles, "max_strain": snappedf(lane.max_strain, 0.0001),
			"torn_edges": st.get("torn_edges", 0), "loose_particles": st.get("loose_particles", 0),
			"strain_end": snappedf(lane.strain_now, 0.0001), "strain_mean_end": snappedf(lane.strain_mean_now, 0.00001), "finite": lane.finite,
			"deepest_cm": snappedf(lane.deepest * 100.0, 0.01), "simulate_usec": st.simulate_usec}
		var ok: bool = lane.finite
		var box := AABB(c.get_particle_position(0), Vector3())
		for i in c.get_particle_count():
			box = box.expand(c.get_particle_position(i))
		e["bounds"] = [snappedf(box.position.x - lane.x, 0.01), snappedf(box.position.y, 0.01), snappedf(box.position.z, 0.01), snappedf(box.end.x - lane.x, 0.01), snappedf(box.end.y, 0.01), snappedf(box.end.z, 0.01)]
		match lane.name:
			"flag":
				# Streaming downwind (+X): the free edge out from the pole, not hanging.
				var d := _grid_dims(c)
				var tip := Vector3()
				for j in d.y:
					tip += c.get_particle_position(j * d.x + d.x - 1)
				tip /= d.y
				e["free_edge"] = [snappedf(tip.x - lane.x, 0.01), snappedf(tip.y, 0.01)]
				ok = ok and tip.x - lane.x > 1.2 and lane.strain_mean_now < 0.015 and lane.strain_now < 0.25
			"curtain":
				# Hanging straight: the bottom edge under the top within a few centimetres, its length kept.
				ok = ok and box.end.y > 2.99 and box.position.y < 0.7 and box.position.y > 0.5 and absf(box.position.z) < 0.15 and lane.strain_now < 0.05
			"sheet_on_shapes":
				ok = ok and lane.deepest < 0.03 and box.position.y > -0.01 and box.end.y < 1.2
			"sheet_on_heightmap":
				ok = ok and lane.deepest < 0.03 and box.position.y > -0.01
			"cape":
				ok = ok and lane.deepest < 0.03 and lane.strain_mean_now < 0.03 and lane.strain_now < 0.3
			"awning":
				ok = ok and box.position.y < 2.5 and box.position.y > 1.6 and lane.strain_mean_now < 0.015 and lane.strain_now < 0.25
			"banner_mesh":
				ok = ok and box.end.y > 2.99 and box.position.y < 1.1
			"impulse":
				ok = ok and lane.strain_mean_now < 0.015 and lane.strain_now < 0.25 and lane.max_strain < 1.0
			"lod":
				var d2 := _grid_dims(c)
				var tip2 := Vector3()
				for j in d2.y:
					tip2 += c.get_particle_position(j * d2.x + d2.x - 1)
				tip2 /= d2.y
				e["free_edge"] = [snappedf(tip2.x - lane.x, 0.01), snappedf(tip2.y, 0.01)]
				ok = ok and st.lod and tip2.x - lane.x > 1.2 and lane.strain_mean_now < 0.03 and lane.strain_now < 0.3
			"far":
				ok = ok and st.far
			"tear_cut", "tear_reset", "tear_strain", "tear_strain_off", "tear_off":
				ok = ok and _check_tear(lane, c, st, box, e)
			"self_heap", "self_heap_off", "self_heap_thin", "self_heap_tri", "self_slope", "self_slope_friction":
				ok = ok and _check_self(lane, c, st, box, e)
			"cape_bones":
				# The pins on the chest (they twist with it, within a millimetre), nothing inside the body.
				e["pin_drift_mm"] = snappedf(lane.get("pin_drift", 1.0) * 1000.0, 0.01)
				e["bone_pins"] = st.get("bone_pins", 0)
				ok = ok and st.get("bone_pins", 0) > 0 and st.get("bone_pins", 0) == lane.get("pin_local", {}).size() and lane.get("pin_drift", 1.0) < 0.001 and lane.deepest < 0.03 and lane.strain_mean_now < 0.03
		e["ok"] = ok
		if not ok:
			failures.append(lane.name)
		out[lane.name] = e
	result["lanes"] = out
	result["failures"] = failures
	result["global"] = FellowCloth3D.get_global_stats()
	result["physics_engine"] = ProjectSettings.get_setting("physics/3d/physics_engine")
	if pictures:
		_save_strip()
	print("CLOTH_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("cloth_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)


func _check_tear(lane: Dictionary, c: FellowCloth3D, st: Dictionary, box: AABB, e: Dictionary) -> bool:
	var tears: Array = lane.get("tears", [])
	e["tear_signals"] = tears.size()
	e["torn_by_call"] = lane.get("torn_by_call", 0)
	e["triangles_all"] = st.get("triangles_all", 0)
	if not tears.is_empty():
		var first: Vector3 = tears[0][0]
		e["first_tear"] = [snappedf(first.x - lane.x, 0.01), snappedf(first.y, 0.01), snappedf(first.z, 0.01), tears[0][2]]
	e["top"] = snappedf(box.end.y, 0.01)
	e["bottom"] = snappedf(box.position.y, 0.01)
	match lane.name:
		"tear_cut":
			# A hole at the middle (a fifth of a metre and more across): edges and triangles gone, its inside loose; the
			# curtain still hanging whole around it (its bottom where the curtain's is).
			var at: Vector3 = tears[0][0] if not tears.is_empty() else Vector3(1e9, 0, 0)
			return tears.size() == 1 and int(lane.get("torn_by_call", 0)) > 20 and st.torn_edges >= int(lane.torn_by_call) and st.triangles < st.triangles_all \
					and st.loose_particles > 0 and at.distance_to(Vector3(lane.x, 1.8, 0)) < 0.1 and box.end.y > 2.99 and box.position.y < 0.7 and box.position.y > 0.5
		"tear_reset":
			return tears.size() == 1 and int(lane.get("torn_by_call", 0)) > 20 and st.torn_edges == 0 and st.triangles == st.triangles_all and st.loose_particles == 0 \
					and box.end.y > 2.99 and box.position.y < 0.7 and box.position.y > 0.5 and absf(box.position.z) < 0.15 and lane.strain_now < 0.05
		"tear_strain":
			# Torn where the ball went through (about 2 m up, at the middle); the sheet still on its corners.
			var at2: Vector3 = tears[0][0] if not tears.is_empty() else Vector3(1e9, 0, 0)
			return not tears.is_empty() and st.torn_edges > 10 and Vector2(at2.x - lane.x, at2.y - 2.0).length() < 0.5 and box.end.y > 2.99
		"tear_strain_off":
			return tears.is_empty() and st.torn_edges == 0 and st.triangles == st.triangles_all and box.end.y > 2.99
		"tear_off":
			# The cut part on the ground, the strip by the pole still hanging from it.
			var d := _grid_dims(c)
			var piece_top := -1e9
			var strip_bottom := 1e9
			for j in d.y:
				for i in d.x:
					var p := c.get_particle_position(j * d.x + i)
					if i >= 4:
						piece_top = maxf(piece_top, p.y)
					else:
						strip_bottom = minf(strip_bottom, p.y)
			e["piece_top"] = snappedf(piece_top, 0.01)
			e["strip_bottom"] = snappedf(strip_bottom, 0.01)
			return tears.size() == d.y and int(lane.get("torn_by_call", 0)) == d.y and piece_top < 0.5 and strip_bottom > 2.6
	return false


# Milestone 112: how close the cloth comes to itself — the nearest two particles that are not near each other at rest
# (more than 1.5 cells apart), and how many particles have such a one within half the self-collision distance.
func _check_self(lane: Dictionary, c: FellowCloth3D, st: Dictionary, box: AABB, e: Dictionary) -> bool:
	var dims := _grid_dims(c)
	var dx := c.size.x / (dims.x - 1)
	var dy := c.size.y / (dims.y - 1)
	var cellsz := maxf(dx, dy)
	var d: float = st.get("self_distance", 0.0)
	if d <= 0.0:
		d = 0.75 * (dx + dy) * 0.5 # what the cloth would use
	var n := c.get_particle_count()
	var pos := []
	for i in n:
		pos.append(c.get_particle_position(i))
	var nearest := 1e9
	var close := {}
	for i in n:
		var ri := Vector2(i % dims.x * dx, i / dims.x * dy)
		for j in range(i + 1, n):
			var rj := Vector2(j % dims.x * dx, j / dims.x * dy)
			if ri.distance_to(rj) < 1.5 * cellsz:
				continue
			var dd: float = (pos[i] as Vector3).distance_to(pos[j])
			nearest = minf(nearest, dd)
			if dd < 0.5 * d:
				close[i] = true
				close[j] = true
	e["self_distance_cm"] = snappedf(d * 100.0, 0.01)
	e["nearest_cm"] = snappedf(nearest * 100.0, 0.01)
	e["close_particles"] = close.size()
	e["self_pairs"] = st.get("self_pairs", 0)
	e["height"] = snappedf(box.end.y, 0.01)
	e["footprint_m2"] = snappedf(box.size.x * box.size.z, 0.01)
	if lane.has("pos_210"):
		# The mean speed over the last half second (frames 210-240): a heap still sliding, or settled.
		var moved := 0.0
		var drift := Vector3()
		for i in n:
			moved += (pos[i] as Vector3).distance_to(lane.pos_210[i])
			drift += (pos[i] as Vector3) - (lane.pos_210[i] as Vector3)
		drift /= n
		var inner := 0.0
		for i in n:
			inner += ((pos[i] as Vector3) - (lane.pos_210[i] as Vector3) - drift).length()
		var secs := (frame - 210) / 60.0
		e["speed_last_cm_s"] = snappedf(moved / n / secs * 100.0, 0.01)
		e["drift_last_cm_s"] = snappedf(drift.length() / secs * 100.0, 0.01) # the heap as a whole
		e["inner_last_cm_s"] = snappedf(inner / n / secs * 100.0, 0.01) # its parts against each other
		if lane.name.begins_with("self_slope"):
			var downhill := Vector3(cos(deg_to_rad(25.0)), -sin(deg_to_rad(25.0)), 0.0)
			e["downhill_last_cm_s"] = snappedf(drift.dot(downhill) / secs * 100.0, 0.01)
	# Milestone 115: how close particles come to the other layers' triangles (those not within 1.5 cells at rest of any
	# of the triangle's corners): the nearest, and the particles within half the distance of one (through a layer, or
	# about to be). The grid's quads split as the cloth builds them (the diagonals alternating).
	var tri_near := 1e9
	var tri_close := {}
	for cy in dims.y - 1:
		for cx in dims.x - 1:
			var i00 := cy * dims.x + cx
			var i11 := i00 + dims.x + 1
			var quad := [[i00, i00 + 1, i00 + dims.x], [i00 + 1, i11, i00 + dims.x]]
			if (cx + cy) & 1:
				quad = [[i00, i00 + 1, i11], [i00, i11, i00 + dims.x]]
			for tri in quad:
				var a: Vector3 = pos[tri[0]]
				var b: Vector3 = pos[tri[1]]
				var cc: Vector3 = pos[tri[2]]
				var lo := a.min(b).min(cc) - Vector3.ONE * d
				var hi := a.max(b).max(cc) + Vector3.ONE * d
				var nrm := (b - a).cross(cc - a)
				if nrm.length_squared() < 1e-12:
					continue
				nrm = nrm.normalized()
				for i in n:
					var p: Vector3 = pos[i]
					if p.x < lo.x or p.y < lo.y or p.z < lo.z or p.x > hi.x or p.y > hi.y or p.z > hi.z:
						continue
					var ri := Vector2(i % dims.x, i / dims.x)
					var far := true
					for k in tri:
						if ri.distance_to(Vector2(k % dims.x, k / dims.x)) < 1.5:
							far = false
							break
					if not far:
						continue
					var dist := absf((p - a).dot(nrm))
					var q := p - nrm * (p - a).dot(nrm)
					# Inside the triangle (the same side of each edge as the normal).
					if (b - a).cross(q - a).dot(nrm) < 0.0 or (cc - b).cross(q - b).dot(nrm) < 0.0 or (a - cc).cross(q - cc).dot(nrm) < 0.0:
						continue
					tri_near = minf(tri_near, dist)
					if dist < 0.5 * d:
						tri_close[i] = true
	e["nearest_triangle_cm"] = snappedf(tri_near * 100.0, 0.01)
	e["close_to_triangles"] = tri_close.size()
	e["self_tri_pairs"] = st.get("self_tri_pairs", 0)
	if lane.name.ends_with("_off"):
		return st.get("self_pairs", 0) == 0
	if lane.name == "self_heap_thin":
		return st.get("self_pairs", 0) > 0 and st.get("self_tri_pairs", 0) == 0
	if lane.name == "self_heap_tri" or lane.name == "self_slope":
		return nearest > 0.5 * d and close.is_empty() and tri_close.is_empty() and st.get("self_tri_pairs", 0) > 0
	if lane.name == "self_slope_friction":
		# Held on the slope by its own layers.
		return nearest > 0.5 * d and close.is_empty() and tri_close.is_empty() and st.get("self_tri_pairs", 0) > 0 and absf(float(e.get("downhill_last_cm_s", 99.0))) < 5.0
	return nearest > 0.5 * d and close.is_empty() and st.get("self_pairs", 0) > 0


func _save_strip() -> void:
	var w := shot_size.x
	var h := shot_size.y
	var strip := Image.create(w * strip_times.size(), h * strip_lanes.size(), false, Image.FORMAT_RGBA8)
	for r in strip_lanes.size():
		for k in strip_times.size():
			var img: Image = shots.get(strip_lanes[r], {}).get(k)
			if img == null:
				continue
			img.convert(Image.FORMAT_RGBA8)
			strip.blit_rect(img, Rect2i(0, 0, w, h), Vector2i(k * w, r * h))
	strip.save_png(out_dir.path_join("cloth_strip.png"))
	if motion:
		var img: Image = shots.get("flag", {}).get(strip_times.size() - 1)
		if img:
			img.save_png(out_dir.path_join("cloth_motion.png"))


func _many_tick() -> void:
	var now := Time.get_ticks_usec()
	if frame == 60:
		FellowCloth3D.reset_global_stats()
		many_last = now
		return
	if frame < 60:
		return
	many_frames.append(float(now - many_last) / 1000.0)
	many_last = now
	if frame >= 60 + int(many_seconds * 60.0):
		done = true
		var st := FellowCloth3D.get_global_stats()
		var n := float(frame - 60)
		var fr := many_frames.duplicate()
		fr.sort()
		var r := {"flags": many, "frames": int(n), "stats": st, "tear_strain": float(_arg("tear_strain", "0")), "self_collision": _arg("self", "0") == "1", "self_tri": _arg("self_tri", "0") == "1", "self_friction": float(_arg("self_friction", "0")),
			"cloth_main_ms_per_frame": snappedf(float(st.frame_usec) / n / 1000.0, 0.001),
			"cloth_worker_ms_per_frame": snappedf(float(st.simulate_usec) / n / 1000.0, 0.001),
			"particles_per_frame": snappedf(float(st.particles) / n, 0.1),
			"substeps_per_frame": snappedf(float(st.substeps) / maxf(1.0, float(st.simulations)), 0.01),
			"frame_ms_mean": snappedf(_mean(many_frames), 0.001), "frame_ms_p99": snappedf(fr[int(fr.size() * 0.99)], 0.001),
			"threads": OS.get_processor_count()}
		print("CLOTH_MANY " + JSON.stringify(r))
		var f := FileAccess.open(out_dir.path_join("cloth_many.json"), FileAccess.WRITE)
		f.store_string(JSON.stringify(r, "  "))
		f.close()
		quit(0)


func _mean(a: PackedFloat64Array) -> float:
	var s := 0.0
	for v in a:
		s += v
	return s / maxf(1.0, float(a.size()))
