extends SceneTree
## FellowEngine's character push course (milestone 170): CharacterBody3D capsules walking into crates of 5-300 kg,
## running into a ball, walking along a seesaw and onto a light plank on two crates, headless. Prints and writes JSON per
## lane: how far the body and the object went, the object's tilt and its largest upward speed (launched?), the body's
## speed while pushing and move_and_slide()'s time.
##
##   godot --headless --path benchmarks/tools/character_course -s push_course.gd -- --out=<file.json> [--push=500]
##       [--mass=80] [--seconds=6]
##
## --push and --mass set CharacterBody3D.rigid_push_force and rigid_standing_mass where the build has them (FellowEngine,
## decision 0312); run_course.py --course=push runs it on both physics engines.

var _chars: Array[CharacterBody3D] = []
var _info: Array[Dictionary] = []
var _tick := 0
var _ticks := 360
var _out := ""
var _push := -1.0
var _mass := -1.0

const SETTLE := 30
const LANES := ["crate5", "crate20", "crate80", "crate300", "ball", "seesaw", "plank"]


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		var v := a.get_slice("=", 1)
		if a.begins_with("--out="):
			_out = v
		elif a.begins_with("--push="):
			_push = float(v)
		elif a.begins_with("--mass="):
			_mass = float(v)
		elif a.begins_with("--seconds="):
			_ticks = int(float(v) * 60.0)
	var world := Node3D.new()
	root.add_child(world)
	var z := 0.0
	for lane in LANES:
		_build(world, lane, z)
		z += 8.0
	print("push course: engine %s, push %s N, mass %s kg" % [ProjectSettings.get_setting("physics/3d/physics_engine"),
			str(_push), str(_mass)])


func _static_box(p_parent: Node, p_center: Vector3, p_size: Vector3) -> StaticBody3D:
	var sb := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var bs := BoxShape3D.new()
	bs.size = p_size
	cs.shape = bs
	sb.add_child(cs)
	sb.position = p_center
	p_parent.add_child(sb)
	return sb


func _rigid(p_parent: Node, p_shape: Shape3D, p_center: Vector3, p_mass: float) -> RigidBody3D:
	var rb := RigidBody3D.new()
	var cs := CollisionShape3D.new()
	cs.shape = p_shape
	rb.add_child(cs)
	rb.mass = p_mass
	var pm := PhysicsMaterial.new()
	pm.friction = 0.6
	rb.physics_material_override = pm
	rb.position = p_center
	p_parent.add_child(rb)
	return rb


func _build(p_parent: Node, p_lane: String, z: float) -> void:
	_static_box(p_parent, Vector3(5.0, -0.5, z), Vector3(20.0, 1.0, 6.0))
	var speed := 1.6
	var start := Vector3(-1.0, 0.0, z)
	var obj: RigidBody3D
	if p_lane.begins_with("crate"):
		var box := BoxShape3D.new()
		box.size = Vector3(0.8, 0.8, 0.8)
		obj = _rigid(p_parent, box, Vector3(1.0, 0.4, z), float(p_lane.substr(5)))
	elif p_lane == "ball":
		var sph := SphereShape3D.new()
		sph.radius = 0.11
		obj = _rigid(p_parent, sph, Vector3(1.0, 0.11, z), 0.45)
		speed = 5.0
	elif p_lane == "seesaw":
		# A 4 m plank (30 kg) on a hinge 0.5 m up, level; the body starts on its left end and walks over it.
		var pivot := _static_box(p_parent, Vector3(0.0, 0.2, z), Vector3(0.2, 0.4, 0.4))
		var plank := BoxShape3D.new()
		plank.size = Vector3(4.0, 0.1, 1.2)
		obj = _rigid(p_parent, plank, Vector3(0.0, 0.5, z), 30.0)
		var hinge := HingeJoint3D.new() # its axis its local Z (across the plank): the plank tips along X
		hinge.position = Vector3(0.0, 0.45, z)
		p_parent.add_child(hinge)
		hinge.node_a = hinge.get_path_to(pivot)
		hinge.node_b = hinge.get_path_to(obj)
		hinge.set_flag(HingeJoint3D.FLAG_USE_LIMIT, true)
		hinge.set_param(HingeJoint3D.PARAM_LIMIT_UPPER, deg_to_rad(15.0))
		hinge.set_param(HingeJoint3D.PARAM_LIMIT_LOWER, deg_to_rad(-15.0))
		start = Vector3(-1.7, 0.56, z)
		speed = 1.0
	elif p_lane == "plank":
		# A loose 2.4 m plank (15 kg) across two 0.4 m crates of 25 kg 2 m apart, the body starting on its left end and
		# walking along it and off.
		var crate := BoxShape3D.new()
		crate.size = Vector3(0.4, 0.4, 0.8)
		_rigid(p_parent, crate, Vector3(0.0, 0.2, z), 25.0)
		_rigid(p_parent, crate, Vector3(2.0, 0.2, z), 25.0)
		var board := BoxShape3D.new()
		board.size = Vector3(2.4, 0.05, 0.6)
		obj = _rigid(p_parent, board, Vector3(1.0, 0.425, z), 15.0)
		start = Vector3(-0.1, 0.46, z)
		speed = 1.0
	var cb := CharacterBody3D.new()
	var cs := CollisionShape3D.new()
	var cap := CapsuleShape3D.new()
	cap.radius = 0.3
	cap.height = 1.8
	cs.shape = cap
	cs.position.y = 0.9
	cb.add_child(cs)
	cb.position = start
	cb.set("floor_step_height", 0.35)
	if _push >= 0.0 and "rigid_push_force" in cb:
		cb.set("rigid_push_force", _push)
	if _mass >= 0.0 and "rigid_standing_mass" in cb:
		cb.set("rigid_standing_mass", _mass)
	p_parent.add_child(cb)
	_chars.append(cb)
	_info.append({"lane": p_lane, "speed": speed, "obj": obj, "start": start, "obj_start": obj.position,
			"max_up": 0.0, "max_tilt": 0.0, "us": 0, "moves": 0, "tilt_at": {}, "pushing_speed": [], "max_speed": 0.0,
			"min_tilt": 0.0})


func _physics_process(p_delta: float) -> bool:
	_tick += 1
	for i in _chars.size():
		var cb := _chars[i]
		var inf := _info[i]
		var v := cb.velocity
		var s: float = inf.speed if _tick > SETTLE else 0.0
		v.x = s
		v.z = 0.0
		if not cb.is_on_floor():
			v.y -= 9.8 * p_delta
		cb.velocity = v
		var before := cb.global_position
		var t0 := Time.get_ticks_usec()
		cb.move_and_slide()
		if _tick <= SETTLE:
			continue
		inf.us += Time.get_ticks_usec() - t0
		inf.moves += 1
		var obj: RigidBody3D = inf.obj
		inf.max_up = maxf(inf.max_up, obj.linear_velocity.y)
		inf.max_speed = maxf(inf.max_speed, Vector2(obj.linear_velocity.x, obj.linear_velocity.z).length())
		var tilt := rad_to_deg(obj.global_basis.y.angle_to(Vector3.UP))
		inf.max_tilt = maxf(inf.max_tilt, tilt)
		if inf.lane == "seesaw":
			# The plank's slope: positive when its right end is up.
			var t := rad_to_deg(atan2(obj.global_basis.x.y, obj.global_basis.x.x))
			inf.max_tilt = maxf(inf.max_tilt, t)
			inf.min_tilt = minf(inf.min_tilt, t)
			for mark in [-1.2, 1.2]:
				if not inf.tilt_at.has(mark) and cb.global_position.x >= mark:
					inf.tilt_at[mark] = snappedf(t, 0.01)
		if obj.global_position.distance_to(inf.obj_start) > 0.05 and inf.lane.begins_with("crate"):
			inf.pushing_speed.append((cb.global_position.x - before.x) / p_delta)
	if _tick > _ticks + SETTLE:
		_finish()
		return true
	return false


func _finish() -> void:
	var rows := []
	for i in _chars.size():
		var inf := _info[i]
		var cb := _chars[i]
		var obj: RigidBody3D = inf.obj
		var ps: Array = inf.pushing_speed
		var mean := 0.0
		for v in ps:
			mean += v
		mean = mean / maxf(ps.size(), 1)
		var row := {"lane": inf.lane, "body_moved": snappedf(cb.global_position.x - inf.start.x, 0.001),
				"object_moved": snappedf(obj.global_position.x - inf.obj_start.x, 0.001),
				"object_y": snappedf(obj.global_position.y - inf.obj_start.y, 0.001),
				"max_up_speed": snappedf(inf.max_up, 0.001), "max_tilt": snappedf(inf.max_tilt, 0.01),
				"max_speed": snappedf(inf.max_speed, 0.001),
				"min_tilt": snappedf(inf.min_tilt, 0.01), "tilt_at": inf.tilt_at,
				"pushing_speed": snappedf(mean, 0.001), "on_floor": cb.is_on_floor(),
				"us_per_move": snappedf(float(inf.us) / maxf(inf.moves, 1), 0.01)}
		rows.append(row)
		print("%-9s body %6.2f m  object %6.2f m (y %+.3f)  speed %.2f up %.2f m/s  tilt %6.2f..%6.2f deg %s  pushing %.2f m/s  floor %d  %.1f us" % [
				row.lane, row.body_moved, row.object_moved, row.object_y, row.max_speed, row.max_up_speed, row.min_tilt,
				row.max_tilt, str(row.tilt_at), row.pushing_speed, int(row.on_floor), row.us_per_move])
	if _out != "":
		var f := FileAccess.open(_out, FileAccess.WRITE)
		f.store_string(JSON.stringify({"engine": ProjectSettings.get_setting("physics/3d/physics_engine"), "push": _push,
				"mass": _mass, "rows": rows}, " "))
