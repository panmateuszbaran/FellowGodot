extends SceneTree
## FellowEngine's character course (milestone 169): CharacterBody3D capsules walking and running along lanes of steps,
## ledges, a ceiling over steps, a steep slope and rubble, headless, one physics tick per frame. Prints and writes JSON:
## per lane and speed how far the body got, how high, its ticks off the floor, the floor state's changes, its largest
## rise and drop in a tick, and move_and_slide()'s time.
##
##   godot --headless --path benchmarks/tools/character_course -s course.gd -- --out=<file.json> [--step=0.35]
##       [--seconds=10] [--speeds=1.6,5.0] [--lanes=stairs15,ledge30,...] [--radius=0.3]
##
## --step sets CharacterBody3D.floor_step_height where the build has it (FellowEngine, decision 0311); run_course.py
## runs it on both physics engines.

const TREAD := 0.3
const FLIGHT := 6 # steps up, then down
const LANDING := 2.0
const GOAL_X := 10.0

var _chars: Array[CharacterBody3D] = []
var _info: Array[Dictionary] = []
var _tick := 0
var _ticks := 600
var _out := ""
var _step := -1.0
var _radius := 0.3
var _speeds := PackedFloat64Array([1.6, 5.0])
var _lane_names := PackedStringArray(["flat", "stairs10", "stairs15", "stairs20", "stairs25", "stairs30", "stairs40",
		"stairs15_diag", "stairs15_ceiling", "ledge20", "ledge30", "ledge60", "slope50", "rubble", "stop15_55", "stop15_62",
		"stop25_55", "stop25_62"])


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		var v := a.get_slice("=", 1)
		if a.begins_with("--out="):
			_out = v
		elif a.begins_with("--step="):
			_step = float(v)
		elif a.begins_with("--seconds="):
			_ticks = int(float(v) * 60.0)
		elif a.begins_with("--speeds="):
			_speeds = v.split_floats(",")
		elif a.begins_with("--lanes="):
			_lane_names = v.split(",")
		elif a.begins_with("--radius="):
			_radius = float(v)
	var world := Node3D.new()
	root.add_child(world)
	var z := 0.0
	for lane in _lane_names:
		for speed in _speeds:
			_build_lane(world, lane, z)
			_add_char(world, lane, speed, z)
			z += 6.0
	print("course: %d characters, %s, engine %s, step %s" % [_chars.size(), ",".join(_lane_names),
			ProjectSettings.get_setting("physics/3d/physics_engine"), str(_step)])


func _box(p_parent: Node, p_from: Vector3, p_to: Vector3, p_basis := Basis()) -> void:
	var sb := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var bs := BoxShape3D.new()
	bs.size = (p_to - p_from).abs()
	cs.shape = bs
	sb.add_child(cs)
	sb.transform = Transform3D(p_basis, (p_from + p_to) * 0.5)
	p_parent.add_child(sb)


func _build_lane(p_parent: Node, p_lane: String, z: float) -> void:
	var w := 4.0 if p_lane.ends_with("_diag") else 2.0
	var z0 := z - w * 0.5
	var z1 := z + w * 0.5
	if p_lane.ends_with("_diag"):
		z0 = z - 1.0
		z1 = z + 9.0
	# The ground: a slab under the whole lane.
	_box(p_parent, Vector3(-5.0, -1.0, z0), Vector3(25.0, 0.0, z1))
	if p_lane.begins_with("stairs") or p_lane.begins_with("stop"):
		# stopNN_XX: stairs of NN cm, the body stopping at x = 0.XX m (on or near the third step's edge) and standing.
		var h := float(p_lane.substr(6, 2) if p_lane.begins_with("stairs") else p_lane.substr(4, 2)) * 0.01
		var length := 2.0 * FLIGHT * TREAD + LANDING
		for k in range(1, FLIGHT + 1):
			# Stacked boxes: the k-th from the k-th riser up to the k-th riser down.
			_box(p_parent, Vector3((k - 1) * TREAD, 0.0, z0), Vector3(length - (k - 1) * TREAD, k * h, z1))
		if p_lane.ends_with("_ceiling"):
			# A ceiling 1.9 m over the lower floor, from before the stairs to over their third step: the body (1.8 m)
			# fits under it on the floor, not on the first step.
			_box(p_parent, Vector3(-1.0, 1.9, z0), Vector3(3 * TREAD, 2.5, z1))
	elif p_lane.begins_with("ledge"):
		var h := float(p_lane.substr(5, 2)) * 0.01
		_box(p_parent, Vector3(-5.0, 0.0, z0), Vector3(1.0, h, z1))
	elif p_lane == "slope50":
		# A wedge rising 1 m at 50 degrees from x = 0.
		var a := deg_to_rad(50.0)
		var run := 1.0 / tan(a)
		var hyp := Vector2(run, 1.0).length()
		var b := Basis(Vector3(0, 0, 1), a)
		var c := Vector3(run * 0.5, 0.5, z) + b * Vector3(0, -0.5, 0) # the slab's top along the slope, 1 m thick
		var sb := StaticBody3D.new()
		var cs := CollisionShape3D.new()
		var bs := BoxShape3D.new()
		bs.size = Vector3(hyp, 1.0, z1 - z0)
		cs.shape = bs
		sb.add_child(cs)
		sb.transform = Transform3D(b, c)
		p_parent.add_child(sb)
		_box(p_parent, Vector3(run, 0.0, z0), Vector3(run + 3.0, 1.0, z1))
	elif p_lane == "rubble":
		var rng := RandomNumberGenerator.new()
		rng.seed = 1693
		for i in 40:
			var x := rng.randf_range(0.0, 9.0)
			var zz := rng.randf_range(z0 + 0.2, z1 - 0.2)
			var sx := rng.randf_range(0.2, 0.5)
			var sz := rng.randf_range(0.2, 0.5)
			var hh := rng.randf_range(0.04, 0.25)
			var yaw := Basis(Vector3.UP, rng.randf_range(0.0, PI))
			var sb := StaticBody3D.new()
			var cs := CollisionShape3D.new()
			var bs := BoxShape3D.new()
			bs.size = Vector3(sx, hh, sz)
			cs.shape = bs
			sb.add_child(cs)
			sb.transform = Transform3D(yaw, Vector3(x, hh * 0.5, zz))
			p_parent.add_child(sb)


func _add_char(p_parent: Node, p_lane: String, p_speed: float, z: float) -> void:
	var cb := CharacterBody3D.new()
	var cs := CollisionShape3D.new()
	var cap := CapsuleShape3D.new()
	cap.radius = _radius
	cap.height = 1.8
	cs.shape = cap
	cs.position.y = 0.9
	cb.add_child(cs)
	var start := Vector3(-2.0, 0.0, z)
	if p_lane.begins_with("ledge"):
		start.y = float(p_lane.substr(5, 2)) * 0.01
	if p_lane.ends_with("_diag"):
		start = Vector3(-2.0, 0.0, z - 0.5)
	cb.position = start
	if _step >= 0.0 and "floor_step_height" in cb:
		cb.set("floor_step_height", _step)
	p_parent.add_child(cb)
	var dir := Vector3(1, 0, 0)
	if p_lane.ends_with("_diag"):
		dir = Vector3(cos(deg_to_rad(35.0)), 0, sin(deg_to_rad(35.0)))
	_chars.append(cb)
	_info.append({"lane": p_lane, "speed": p_speed, "dir": dir, "start": cb.position, "airborne": 0, "changes": 0,
			"was_floor": true, "max_rise": 0.0, "max_drop": 0.0, "max_y": cb.position.y, "us": 0, "stuck": 0,
			"done_tick": -1, "profile": PackedFloat32Array(), "ceiling_hits": 0, "moves": 0,
			"stop_x": float(p_lane.get_slice("_", 1)) * 0.01 if p_lane.begins_with("stop") else INF,
			"stopped_at": Vector3.INF, "drift": 0.0})


const SETTLE := 20 # ticks standing first (on the floor), not measured


func _physics_process(p_delta: float) -> bool:
	for i in _chars.size():
		var cb := _chars[i]
		var inf := _info[i]
		if inf.done_tick >= 0:
			continue # at the goal: stopped, no longer measured
		var dir: Vector3 = inf.dir
		var before := cb.global_position
		var v := cb.velocity
		var speed: float = inf.speed if _tick >= SETTLE else 0.0
		if before.x >= inf.stop_x:
			speed = 0.0
			if inf.stopped_at == Vector3.INF:
				inf.stopped_at = before
			inf.drift = maxf(inf.drift, before.distance_to(inf.stopped_at))
		v.x = dir.x * speed
		v.z = dir.z * speed
		if not cb.is_on_floor():
			v.y -= 9.8 * p_delta
		cb.velocity = v
		var t0 := Time.get_ticks_usec()
		cb.move_and_slide()
		if _tick < SETTLE:
			inf.was_floor = cb.is_on_floor()
			continue
		inf.us += Time.get_ticks_usec() - t0
		inf.moves += 1
		var after := cb.global_position
		var dy := after.y - before.y
		inf.max_rise = maxf(inf.max_rise, dy)
		inf.max_drop = minf(inf.max_drop, dy)
		inf.max_y = maxf(inf.max_y, after.y)
		var on := cb.is_on_floor()
		if not on:
			inf.airborne += 1
		if on != inf.was_floor:
			inf.changes += 1
		inf.was_floor = on
		if cb.is_on_ceiling():
			inf.ceiling_hits += 1
		var along: float = (after - (inf.start as Vector3)).dot(dir)
		var moved: float = (after - before).slide(Vector3.UP).length()
		if inf.done_tick < 0:
			if after.x >= GOAL_X:
				inf.done_tick = _tick - SETTLE
			elif moved < 0.1 * inf.speed * p_delta:
				inf.stuck += 1
		if _tick % 6 == 0:
			inf.profile.append(snappedf(along, 0.001))
			inf.profile.append(snappedf(after.y, 0.001))
	_tick += 1
	if _tick > _ticks + SETTLE:
		_finish()
		return true
	return false


func _finish() -> void:
	var rows := []
	for i in _chars.size():
		var inf := _info[i]
		var cb := _chars[i]
		var row := {
			"lane": inf.lane, "speed": inf.speed,
			"end_x": snappedf(cb.global_position.x, 0.001),
			"reached": inf.done_tick >= 0,
			"seconds": snappedf(inf.done_tick / 60.0, 0.01) if inf.done_tick >= 0 else -1.0,
			"max_y": snappedf(inf.max_y, 0.001),
			"airborne_ticks": inf.airborne,
			"floor_changes": inf.changes,
			"stuck_ticks": inf.stuck,
			"max_rise": snappedf(inf.max_rise, 0.001),
			"max_drop": snappedf(inf.max_drop, 0.001),
			"ceiling_ticks": inf.ceiling_hits,
			"drift": snappedf(inf.drift, 0.0001),
			"us_per_move": snappedf(float(inf.us) / maxf(inf.moves, 1), 0.01),
			"profile": inf.profile,
		}
		rows.append(row)
		print("%-18s %4.1f m/s  x %7.2f %s  max_y %.3f  air %3d  changes %3d  stuck %3d  rise %.3f drop %.3f  ceil %3d  drift %.4f  %.1f us" % [
				row.lane, row.speed, row.end_x, ("in %.2f s" % row.seconds) if row.reached else "not reached", row.max_y,
				row.airborne_ticks, row.floor_changes, row.stuck_ticks, row.max_rise, row.max_drop, row.ceiling_ticks,
				row.drift, row.us_per_move])
	if _out != "":
		var f := FileAccess.open(_out, FileAccess.WRITE)
		f.store_string(JSON.stringify({"engine": ProjectSettings.get_setting("physics/3d/physics_engine"), "step": _step,
				"radius": _radius, "rows": rows}, " "))
