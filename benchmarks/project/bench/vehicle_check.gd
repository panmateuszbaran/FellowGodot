extends SceneTree
## FellowEngine milestone 77 check (decision 0185): FellowVehicle3D and FellowWheel3D.
##
##   godot --path benchmarks/project --headless --fixed-fps 60 -s res://bench/vehicle_check.gd -- --out=<dir>
##   godot --path benchmarks/project --fixed-fps 60 --resolution 1280x720 -s res://bench/vehicle_check.gd
##       -- --out=<dir> --pictures=1 [--frames=1] [--grid=1 --shot=640x540]
##   godot ... -s res://bench/vehicle_check.gd -- --out=<dir> --many=32 [--seconds=10] [--substeps=4] [--contact=0|1|2]
##
## Lanes, each a 1 400 kg rear-driven car (400 N·m, six gears, 0.34 m wheels on 0.3 m of suspension) driven by a
## program: full throttle from rest (the time to 100 km/h, the gears it went through), a stop from 100 km/h (the distance,
## the heading kept), a circle of 40 m at rising speed (the largest lateral acceleration held), a lane change at 70 km/h
## (the roll, the heading after), ten 10 cm bumps at 40 km/h, a handbrake turn at 50 km/h (the rear's slip), reversing
## with the brake held, a jump off a 1.5 m ramp at 60 km/h (upright after), a car parked on a turning turntable (kept
## where it carries it), a car driving off a slab on frictionless ground (the slab pushed back), a car parked on a
## 12-degree slope with the handbrake (held where it is) and one without a brake (rolling back), full throttle on ice (friction 0.25: a quarter of the speed),
## coasting from 80 km/h (the engine's braking); milestone 101 (decision 0209): a 12 cm curb and a 25 cm trench, each
## by a car on rays and one whose tyres are cylinders; milestone 105 (decision 0213): cars whose bodies dent
## (FellowVehicleDamage3D) driven into a wall at 30 km/h (the front dented, the rear not) and at 5 km/h (nothing), the
## bumps lane's car undented. One JSON line:
## VEHICLE_CHECK {...}; exit
## code 1 when a lane did otherwise than expected. --many=N: N cars driving circles, the vehicles' step a frame and the
## frame's (headless). One JSON line: VEHICLE_MANY {...}.

const RUN_SECONDS := 26.0

var args := {}
var out_dir := ""
var result := {}
var root3d: Node3D
var lanes: Array = []
var frame := 0
var pictures := false
var save_frames := false
var grid := false
var shot_size := Vector2i(480, 320)
var strip_times := [2.0, 6.0, 10.0, 14.0, 18.0, 22.0]
var strip_lanes := ["accel", "brake", "skidpad", "lane_change", "bumps", "jump", "crash"]
var shots := {}
var viewports := {}
var cameras := {}
var many := 0
var many_seconds := 10.0
var many_cars: Array = []
var many_frames: PackedFloat64Array = []
var many_last := 0
var done := false
var turntable: AnimatableBody3D
var slab: RigidBody3D


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://vehicle_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	pictures = _arg("pictures", "0") == "1"
	save_frames = _arg("frames", "0") == "1"
	grid = _arg("grid", "0") == "1"
	many = int(_arg("many", "0"))
	many_seconds = float(_arg("seconds", "10"))
	var ss: PackedStringArray = _arg("shot", "480x320").split("x")
	shot_size = Vector2i(int(ss[0]), int(ss[1]))
	_build.call_deferred()


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.8
	return m


func _box(pos: Vector3, size: Vector3, color := Color(0.6, 0.6, 0.58), rot := Vector3()) -> StaticBody3D:
	var b := StaticBody3D.new()
	b.position = pos
	b.rotation_degrees = rot
	var cs := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = size
	cs.shape = shape
	b.add_child(cs)
	var mi := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	mi.mesh = mesh
	mi.material_override = _material(color)
	b.add_child(mi)
	root3d.add_child(b)
	return b


# A car: the body a box (its centre of mass low), four wheels (the front steering, the rear driven and on the
# handbrake), meshes on each.
func _car(pos: Vector3, color: Color) -> FellowVehicle3D:
	var car := FellowVehicle3D.new()
	car.mass = 1400.0
	car.center_of_mass_mode = RigidBody3D.CENTER_OF_MASS_MODE_CUSTOM
	car.center_of_mass = Vector3(0, 0.45, 0.1)
	car.position = pos
	var cs := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = Vector3(1.8, 0.55, 4.2)
	cs.shape = shape
	cs.position = Vector3(0, 0.62, 0)
	car.add_child(cs)
	var body_mesh := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = shape.size
	body_mesh.mesh = bm
	body_mesh.material_override = _material(color)
	body_mesh.position = cs.position
	car.add_child(body_mesh)
	var cabin := MeshInstance3D.new()
	var cm := BoxMesh.new()
	cm.size = Vector3(1.5, 0.45, 2.0)
	cabin.mesh = cm
	cabin.material_override = _material(color.darkened(0.4))
	cabin.position = Vector3(0, 1.1, 0.2)
	car.add_child(cabin)
	var tyre := _material(Color(0.1, 0.1, 0.1))
	for x in [-0.82, 0.82]:
		for z in [-1.35, 1.35]:
			var w := FellowWheel3D.new()
			w.name = ("Front" if z < 0.0 else "Rear") + ("Left" if x < 0.0 else "Right")
			w.position = Vector3(x, 0.34, z)
			w.steering = z < 0.0
			w.driven = z > 0.0
			w.handbrake = z > 0.0
			var wm := MeshInstance3D.new()
			var cyl := CylinderMesh.new()
			cyl.top_radius = 0.34
			cyl.bottom_radius = 0.34
			cyl.height = 0.24
			cyl.radial_segments = 20
			wm.mesh = cyl
			wm.material_override = tyre
			wm.rotation_degrees = Vector3(0, 0, 90)
			w.add_child(wm)
			var hub := MeshInstance3D.new()
			var hm := BoxMesh.new()
			hm.size = Vector3(0.26, 0.4, 0.08)
			hub.mesh = hm
			hub.material_override = _material(Color(0.75, 0.75, 0.78))
			w.add_child(hub)
			car.add_child(w)
	root3d.add_child(car)
	return car


# Milestone 105: the car's body a subdivided box (its vertices to dent), a FellowVehicleDamage3D under it.
func _damage_car(car: FellowVehicle3D) -> FellowVehicle3D:
	for c in car.get_children():
		if c is MeshInstance3D and (c as MeshInstance3D).mesh is BoxMesh and ((c as MeshInstance3D).mesh as BoxMesh).size.z > 4.0:
			var bm: BoxMesh = ((c as MeshInstance3D).mesh as BoxMesh).duplicate()
			bm.subdivide_width = 8
			bm.subdivide_height = 4
			bm.subdivide_depth = 20
			(c as MeshInstance3D).mesh = bm
			c.name = "Body"
			car.set_meta("body_mesh", bm)  # the dents replace the instance's mesh with a copy
	var dmg := FellowVehicleDamage3D.new()
	dmg.name = "Damage"
	car.add_child(dmg)
	dmg.impacted.connect(func(p: Vector3, j: float, depth: float) -> void:
		if not car.has_meta("impacts"):
			car.set_meta("impacts", [])
		car.get_meta("impacts").append([snappedf(j, 1.0), snappedf(depth, 0.0001), snappedf(car.get_speed_kmh(), 0.1)]))
	return car


# The body's dents by region (its original vertices' z: the front -2.1 m, the rear +2.1 m): the largest in each, and its
# direction's share along +Z (into the body, for the front).
func _dents(car: FellowVehicle3D) -> Dictionary:
	var dmg: FellowVehicleDamage3D = car.get_node("Damage")
	var arrays: Array = (car.get_meta("body_mesh") as BoxMesh).get_mesh_arrays()
	var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var front := 0.0
	var rear := 0.0
	var middle := 0.0
	var along := 0.0
	for i in verts.size():
		var o := dmg.get_vertex_offset(NodePath("../Body"), 0, i)
		var l := o.length()
		if verts[i].z < -1.6:
			if l > front:
				front = l
				along = o.z / maxf(l, 1e-9)
		elif verts[i].z > 1.6:
			rear = maxf(rear, l)
		else:
			middle = maxf(middle, l)
	var st := dmg.get_stats()
	return {"front_m": snappedf(front, 0.0001), "rear_m": snappedf(rear, 0.0001), "middle_m": snappedf(middle, 0.0001), "front_along_z": snappedf(along, 0.001),
		"deepest_m": snappedf(dmg.get_deepest_dent(), 0.0001), "damage": snappedf(dmg.get_damage(), 0.001), "impacts": car.get_meta("impacts", []),
		"dented_meshes": st.dented_meshes, "deform_usec": st.deform_usec, "vertices": st.vertices}


func _contact(car: FellowVehicle3D, shape: int) -> void:
	for w in car.get_children():
		if w is FellowWheel3D:
			(w as FellowWheel3D).contact_shape = shape


func _front_left(car: FellowVehicle3D) -> FellowWheel3D:
	return car.get_node("FrontLeft") as FellowWheel3D


func _lane(name: String, car: FellowVehicle3D) -> Dictionary:
	var lane := {"name": name, "car": car, "start": car.global_position, "t": 0.0, "phase": 0, "data": {}, "gears": []}
	car.gear_changed.connect(func(g: int) -> void: lane.gears.append(g))
	lanes.append(lane)
	return lane


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
	sun.rotation_degrees = Vector3(-50, 35, 0)
	sun.shadow_enabled = true
	root3d.add_child(sun)
	_box(Vector3(0, -0.5, -400), Vector3(2000, 1, 2000), Color(0.36, 0.4, 0.36))
	# Lane markings every 10 m (to see the speed).
	for i in 60:
		_box(Vector3(-30, 0.005, -i * 10.0), Vector3(400, 0.01, 0.3), Color(0.8, 0.8, 0.75)).get_child(0).disabled = true

	if many > 0:
		var cols := int(ceil(sqrt(float(many))))
		for i in many:
			var c := _car(Vector3(float(i % cols) * 60.0, 0.1, -float(i / cols) * 60.0), Color.from_hsv(float(i) / many, 0.6, 0.8))
			c.throttle = 0.35
			c.steering = 0.35
			c.substeps = int(_arg("substeps", "4"))
			if _arg("contact", "0") != "0":
				_contact(c, int(_arg("contact", "0")))  # milestone 101: the tyres' shapes swept (1 a sphere, 2 a cylinder)
			many_cars.append(c)
		var cam := Camera3D.new()
		root3d.add_child(cam)
		cam.look_at_from_position(Vector3(-30, 40, 30), Vector3(cols * 30.0, 0, -cols * 30.0))
		physics_frame.connect(_on_frame)
		return

	_lane("accel", _car(Vector3(0, 0.1, 0), Color(0.8, 0.2, 0.15)))
	_lane("brake", _car(Vector3(12, 0.1, 0), Color(0.2, 0.4, 0.8)))
	_lane("skidpad", _car(Vector3(-120, 0.1, -200), Color(0.9, 0.7, 0.1)))
	_lane("lane_change", _car(Vector3(24, 0.1, 0), Color(0.2, 0.7, 0.3)))
	# Ten bumps 10 cm high every 3 m from 40 m ahead.
	for i in 10:
		_box(Vector3(36, 0.05, -40.0 - i * 3.0), Vector3(3.0, 0.1, 0.4), Color(0.5, 0.45, 0.4))
	_lane("bumps", _car(Vector3(36, 0.1, 0), Color(0.6, 0.3, 0.7)))
	_lane("handbrake", _car(Vector3(48, 0.1, 0), Color(0.9, 0.5, 0.2)))
	_lane("reverse", _car(Vector3(60, 0.1, 0), Color(0.5, 0.5, 0.5)))
	# A ramp 1.5 m high (15 degrees) 80 m ahead, a landing beyond.
	var ramp_len := 1.5 / sin(deg_to_rad(15.0))
	_box(Vector3(72, 0.75 - 0.1 * cos(deg_to_rad(15.0)), -80.0 - ramp_len * 0.5 * cos(deg_to_rad(15.0))), Vector3(4.0, 0.2, ramp_len), Color(0.55, 0.5, 0.45), Vector3(15, 0, 0))
	_lane("jump", _car(Vector3(72, 0.1, 0), Color(0.2, 0.7, 0.8)))
	# A turntable 7 m across turning 0.5 rad/s, a car parked on it 3 m off its axis (the brake and the handbrake held).
	turntable = AnimatableBody3D.new()
	turntable.position = Vector3(96, 0.15, -10)
	var tcs := CollisionShape3D.new()
	var tshape := CylinderShape3D.new()
	tshape.radius = 7.0
	tshape.height = 0.3
	tcs.shape = tshape
	turntable.add_child(tcs)
	var tmi := MeshInstance3D.new()
	var tmesh := CylinderMesh.new()
	tmesh.top_radius = 7.0
	tmesh.bottom_radius = 7.0
	tmesh.height = 0.3
	tmi.mesh = tmesh
	tmi.material_override = _material(Color(0.45, 0.42, 0.5))
	turntable.add_child(tmi)
	var stripe := MeshInstance3D.new()
	var smesh := BoxMesh.new()
	smesh.size = Vector3(13.5, 0.02, 0.3)
	stripe.mesh = smesh
	stripe.position = Vector3(0, 0.16, 0)
	stripe.material_override = _material(Color(0.9, 0.85, 0.3))
	turntable.add_child(stripe)
	root3d.add_child(turntable)
	var tl := _lane("turntable", _car(Vector3(99, 0.4, -10), Color(0.85, 0.3, 0.6)))
	tl.car.brake = 1.0
	tl.car.handbrake = true
	# A 3 000 kg slab on frictionless ground, a car on it driving off: the slab pushed back by its tyres.
	var ice := _box(Vector3(108, 0.0, -2), Vector3(5, 0.2, 30), Color(0.8, 0.85, 0.9))
	var ice_pm := PhysicsMaterial.new()
	ice_pm.friction = 0.0
	ice.physics_material_override = ice_pm
	slab = RigidBody3D.new()
	slab.mass = 3000.0
	slab.position = Vector3(108, 0.3, -2)
	var scs := CollisionShape3D.new()
	var sshape := BoxShape3D.new()
	sshape.size = Vector3(4, 0.4, 10)
	scs.shape = sshape
	slab.add_child(scs)
	var smi := MeshInstance3D.new()
	var sbm := BoxMesh.new()
	sbm.size = sshape.size
	smi.mesh = sbm
	smi.material_override = _material(Color(0.55, 0.45, 0.35))
	slab.add_child(smi)
	root3d.add_child(slab)
	_lane("slab", _car(Vector3(108, 0.6, 0), Color(0.3, 0.6, 0.9)))
	# A strip of ice (friction 0.25) 200 m long, full throttle from rest on it.
	var strip := _box(Vector3(132, 0.005, -90), Vector3(4, 0.01, 200), Color(0.85, 0.9, 0.95))
	var strip_pm := PhysicsMaterial.new()
	strip_pm.friction = 0.25
	strip.physics_material_override = strip_pm
	_lane("ice", _car(Vector3(132, 0.11, 0), Color(0.4, 0.8, 0.9)))
	_lane("coast", _car(Vector3(144, 0.1, 0), Color(0.6, 0.6, 0.3)))
	# A 12-degree slope (21 %), a car parked on it facing up with the handbrake held.
	var slope_c := Vector3(120, 2.0, -10)
	var slope_n := Basis.from_euler(Vector3(deg_to_rad(12.0), 0, 0)).y
	_box(slope_c, Vector3(4, 0.2, 24), Color(0.5, 0.48, 0.42), Vector3(12, 0, 0))
	var parked := _car(slope_c + slope_n * 0.45, Color(0.7, 0.7, 0.2))
	parked.rotation_degrees = Vector3(12, 0, 0)
	parked.handbrake = true
	parked.can_sleep = false # held by its tyres, not by sleeping
	_lane("slope", parked)
	# The same slope beside it, a car on it with no brake: it rolls back down.
	_box(slope_c + Vector3(6, 0, 0), Vector3(4, 0.2, 24), Color(0.5, 0.48, 0.42), Vector3(12, 0, 0))
	var free := _car(slope_c + Vector3(6, 0, 0) + slope_n * 0.45, Color(0.7, 0.5, 0.2))
	free.rotation_degrees = Vector3(12, 0, 0)
	free.can_sleep = false
	_lane("slope_free", free)
	# Milestone 101 (decision 0209): on raised roads (slabs 0.3 m high: GodotPhysics3D's shape casts are unreliable
	# against the 2 km ground box), a curb 12 cm high 20 m ahead, driven onto at 15 km/h, and a trench 25 cm wide and
	# 30 cm deep, crossed at 10 km/h — each by a car on rays and one whose tyres are cylinders.
	for k in 2:
		var x := 200.0 + k * 12.0
		_box(Vector3(x, 0.15, -5.0), Vector3(4.0, 0.3, 30.0), Color(0.55, 0.52, 0.48))
		_box(Vector3(x, 0.21, -70.0), Vector3(4.0, 0.42, 100.0), Color(0.6, 0.57, 0.52))
		var c := _car(Vector3(x, 0.4, 0), Color(0.75, 0.35, 0.3) if k == 0 else Color(0.3, 0.75, 0.4))
		_contact(c, FellowWheel3D.CONTACT_RAY if k == 0 else FellowWheel3D.CONTACT_CYLINDER)
		_lane("curb_ray" if k == 0 else "curb_cylinder", c)
	for k in 2:
		var x := 224.0 + k * 12.0
		_box(Vector3(x, 0.15, -10.0), Vector3(4.0, 0.3, 40.0), Color(0.55, 0.52, 0.48))
		_box(Vector3(x, 0.15, -130.25), Vector3(4.0, 0.3, 200.0), Color(0.55, 0.52, 0.48))
		var c := _car(Vector3(x, 0.4, 0), Color(0.75, 0.35, 0.3) if k == 0 else Color(0.3, 0.75, 0.4))
		_contact(c, FellowWheel3D.CONTACT_RAY if k == 0 else FellowWheel3D.CONTACT_CYLINDER)
		_lane("gap_ray" if k == 0 else "gap_cylinder", c)
	# Milestone 105 (decision 0213): cars whose bodies dent (a body of 8 x 4 x 20 quads, FellowVehicleDamage3D) driven
	# into a wall 30 m ahead at 30 km/h and at 5 km/h; the bumps lane's car dents nothing.
	for k in 2:
		var x := 260.0 + k * 12.0
		_box(Vector3(x, 1.0, -30.0), Vector3(6.0, 2.0, 1.0), Color(0.62, 0.6, 0.58))
		_lane("crash" if k == 0 else "tap", _damage_car(_car(Vector3(x, 0.1, 0), Color(0.85, 0.25, 0.2) if k == 0 else Color(0.25, 0.5, 0.85))))
	_damage_car(_find("bumps").car)

	var cam := Camera3D.new()
	root3d.add_child(cam)
	cam.look_at_from_position(Vector3(36, 30, 40), Vector3(36, 0, -40))
	if pictures:
		_make_viewports()
	physics_frame.connect(_on_frame)


func _make_viewports() -> void:
	var grid_box: GridContainer
	if grid:
		grid_box = GridContainer.new()
		grid_box.columns = 3
		grid_box.add_theme_constant_override("h_separation", 0)
		grid_box.add_theme_constant_override("v_separation", 0)
		get_root().add_child(grid_box)
	for name in strip_lanes:
		var vp := SubViewport.new()
		vp.size = shot_size
		vp.world_3d = get_root().world_3d
		vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		var cam := Camera3D.new()
		cam.fov = 60
		vp.add_child(cam)
		cameras[name] = cam
		if grid:
			var cell := Control.new()
			cell.custom_minimum_size = Vector2(shot_size)
			var holder := SubViewportContainer.new()
			holder.size = Vector2(shot_size)
			holder.add_child(vp)
			cell.add_child(holder)
			var label := Label.new()
			label.text = name
			label.add_theme_font_size_override("font_size", 20)
			label.add_theme_color_override("font_outline_color", Color.BLACK)
			label.add_theme_constant_override("outline_size", 6)
			label.position = Vector2(10, 6)
			cell.add_child(label)
			grid_box.add_child(cell)
		else:
			get_root().add_child(vp)
		viewports[name] = vp


func _find(name: String) -> Dictionary:
	for lane in lanes:
		if lane.name == name:
			return lane
	return {}


# Holds a speed (km/h) with the throttle and brake.
func _cruise(car: FellowVehicle3D, kmh: float) -> void:
	var err := kmh - car.get_speed_kmh()
	car.throttle = clampf(err * 0.15, 0.0, 1.0)
	car.brake = clampf(-err * 0.1, 0.0, 1.0)


func _heading(car: FellowVehicle3D) -> float:
	var f := -car.global_basis.z
	return rad_to_deg(atan2(-f.x, -f.z))


func _drive(lane: Dictionary, dt: float) -> void:
	var car: FellowVehicle3D = lane.car
	lane.t += dt
	var t: float = lane.t
	var d: Dictionary = lane.data
	var kmh := car.get_speed_kmh()
	d["max_kmh"] = maxf(d.get("max_kmh", 0.0), kmh)
	d["min_up"] = minf(d.get("min_up", 1.0), car.global_basis.y.dot(Vector3.UP))
	d["min_y"] = minf(d.get("min_y", 99.0), car.global_position.y)
	match lane.name:
		"accel":
			car.throttle = 1.0
			if t >= 3.0 and not d.has("kmh_3s"):
				d["kmh_3s"] = snappedf(kmh, 0.1)
			if kmh >= 100.0 and not d.has("t100"):
				d["t100"] = snappedf(t, 0.01)
				d["d100"] = snappedf(car.global_position.distance_to(lane.start), 0.1)
			if t > 20.0:
				car.throttle = 0.0
				car.brake = 1.0
		"brake":
			if lane.phase == 0:
				car.throttle = 1.0
				if kmh >= 100.0:
					lane.phase = 1
					d["brake_from"] = car.global_position
					d["heading0"] = _heading(car)
					d["brake_kmh"] = snappedf(kmh, 0.1)
			elif lane.phase == 1:
				car.throttle = 0.0
				car.brake = 1.0
				if kmh < 0.5:
					lane.phase = 2
					d["stop_m"] = snappedf(car.global_position.distance_to(d.brake_from), 0.01)
					d["heading_change"] = snappedf(absf(_heading(car) - d.heading0), 0.1)
					d.erase("brake_from")
		"skidpad":
			# A steady steer, the speed rising 1 km/h a second from 30: the lateral acceleration it holds (the speed
			# times the yaw rate) until it slides (the yaw rate falls behind the speed's).
			var target := 30.0 + t * 2.0
			_cruise(car, target)
			car.steering = 0.25
			# The lateral acceleration: the velocity's change across the body, smoothed over about a quarter second.
			var v: Vector3 = car.linear_velocity
			var side := car.global_basis.x
			side.y = 0.0
			side = side.normalized()
			if d.has("v_prev"):
				var a_now := absf((v - d.v_prev).dot(side)) / dt
				d["a_smooth"] = lerpf(d.get("a_smooth", a_now), a_now, 0.067)
			d["v_prev"] = v
			if t > 4.0:
				d["a_lat_max_g"] = snappedf(maxf(d.get("a_lat_max_g", 0.0), d.get("a_smooth", 0.0) / 9.8), 0.001)
		"lane_change":
			_cruise(car, 70.0)
			var roll := rad_to_deg(asin(clampf(car.global_basis.x.dot(Vector3.UP), -1.0, 1.0)))
			if t > 8.0 and t < 8.6:
				car.steering = 0.35
			elif t >= 8.6 and t < 9.6:
				car.steering = -0.35
			elif t >= 9.6 and t < 10.2:
				car.steering = 0.35
			else:
				car.steering = 0.0
			if t > 7.0:
				d["roll_max_deg"] = snappedf(maxf(d.get("roll_max_deg", 0.0), absf(roll)), 0.1)
			if t > 14.0 and not d.has("heading_after"):
				d["heading_after"] = snappedf(_heading(car), 0.1)
				d["kmh_after"] = snappedf(kmh, 0.1)
		"bumps":
			_cruise(car, 40.0)
			for w in car.get_children():
				if w is FellowWheel3D:
					d["comp_max"] = snappedf(maxf(d.get("comp_max", 0.0), (w as FellowWheel3D).get_compression()), 0.001)
			if car.global_position.z < -45.0 and car.global_position.z > -75.0:
				d["pitch_max_deg"] = snappedf(maxf(d.get("pitch_max_deg", 0.0), absf(rad_to_deg(asin(clampf(-car.global_basis.z.dot(Vector3.UP), -1.0, 1.0))))), 0.1)
		"curb_ray", "curb_cylinder", "gap_ray", "gap_cylinder":
			# Milestone 101: the front left suspension's largest change in a step and the body's vertical acceleration
			# while the front axle meets the curb (or the trench), the suspension's least compression over the trench.
			var curb: bool = lane.name.begins_with("curb")
			_cruise(car, 15.0 if curb else 10.0)
			var w := _front_left(car)
			var axle_z: float = car.global_position.z - 1.35
			var comp := w.get_compression()
			var vy := car.linear_velocity.y
			if d.has("comp_prev"):
				var near: bool = (axle_z < -17.0 and axle_z > -23.0) if curb else (axle_z < -28.5 and axle_z > -32.0)
				if near:
					d["comp_step_max"] = snappedf(maxf(d.get("comp_step_max", 0.0), absf(comp - d.comp_prev)), 0.0001)
					d["a_y_max_g"] = snappedf(maxf(d.get("a_y_max_g", 0.0), absf(vy - d.vy_prev) / dt / 9.81), 0.001)
					d["comp_min_near"] = snappedf(minf(d.get("comp_min_near", 1.0), comp), 0.0001)
				elif axle_z > (-15.0 if curb else -26.0) and axle_z < (-10.0 if curb else -24.0):
					d["comp_before"] = snappedf(comp, 0.0001)
			d["comp_prev"] = comp
			d["vy_prev"] = vy
			if t > 18.0 and not d.has("end_y"):
				d["end_y"] = snappedf(car.global_position.y, 0.001)
				d["end_z"] = snappedf(car.global_position.z, 0.1)
		"handbrake":
			if t < 8.0:
				_cruise(car, 50.0)
			else:
				car.throttle = 0.0
				car.steering = 0.6
				car.handbrake = t < 9.5
				for w in car.get_children():
					if w is FellowWheel3D and (w as FellowWheel3D).handbrake:
						d["rear_slip_max_deg"] = snappedf(maxf(d.get("rear_slip_max_deg", 0.0), absf(rad_to_deg((w as FellowWheel3D).get_slip_angle()))), 0.1)
				d["yaw_rate_max"] = snappedf(maxf(d.get("yaw_rate_max", 0.0), absf(car.angular_velocity.y)), 0.01)
		"turntable":
			# The car where the turntable carries it (parked: the brake and the handbrake held): its place in the
			# turntable's frame kept.
			car.brake = 1.0
			car.handbrake = true
			if t > 1.5:
				var local := turntable.global_transform.affine_inverse() * car.global_position
				if not d.has("local0"):
					d["local0"] = local
				d["drift_max"] = snappedf(maxf(d.get("drift_max", 0.0), Vector2(local.x - d.local0.x, local.z - d.local0.z).length()), 0.001)
				d["turned_deg"] = snappedf(rad_to_deg(0.5 * t), 0.1)
		"coast":
			# Up to 80 km/h, then the throttle lifted: the engine's braking and the air's over 3 s.
			if lane.phase == 0:
				_cruise(car, 80.0)
				if t > 14.0:
					lane.phase = 1
					d["coast_from"] = snappedf(kmh, 0.1)
					d["coast_t0"] = t
					d["coast_gear"] = car.get_gear()
			elif lane.phase == 1:
				car.throttle = 0.0
				car.brake = 0.0
				if t - d.coast_t0 >= 3.0:
					lane.phase = 2
					d["coast_decel"] = snappedf((d.coast_from - kmh) / 3.6 / 3.0, 0.01)
		"ice":
			car.throttle = 1.0
			if t >= 3.0 and not d.has("kmh_3s"):
				d["kmh_3s"] = snappedf(kmh, 0.1)
			if t > 8.0:
				car.throttle = 0.0
				car.brake = 1.0
		"slope":
			car.handbrake = true
			if t > 2.0:
				if not d.has("p0"):
					d["p0"] = car.global_position
				d["drift_max"] = snappedf(maxf(d.get("drift_max", 0.0), car.global_position.distance_to(d.p0)), 0.0001)
				d["sleeping_frames"] = d.get("sleeping_frames", 0) + (1 if car.sleeping else 0)
		"slope_free":
			if t > 2.0 and t <= 7.0:
				if not d.has("p0"):
					d["p0"] = car.global_position
				d["rolled_m"] = snappedf(car.global_position.distance_to(d.p0), 0.01)
			if t > 7.0:
				car.handbrake = true
		"crash", "tap":
			# Up to speed, held until the wall stops the car; a second held; backed off 3 m (the brake held at a standstill
			# engages reverse) to show its front; held there.
			if lane.phase == 0:
				_cruise(car, 30.0 if lane.name == "crash" else 5.0)
				d["kmh_impact"] = snappedf(maxf(d.get("kmh_impact", 0.0), kmh), 0.1)
				if car.global_position.z < -26.0 and kmh < 1.0 and t > 2.0:
					lane.phase = 1
					d["stopped_z"] = snappedf(car.global_position.z, 0.01)
					d["t_stop"] = t
			elif lane.phase == 1:
				car.throttle = 0.0
				car.handbrake = true
				if t > d.t_stop + 1.0:
					lane.phase = 2
			elif lane.phase == 2:
				car.handbrake = false
				car.brake = 0.5
				if car.global_position.z > d.stopped_z + 3.0:
					lane.phase = 3
			else:
				car.brake = 0.0
				car.handbrake = true
		"slab":
			# On the slab: its momentum and the car's along Z sum to about none while the car speeds up on it.
			if t > 1.0:
				car.throttle = 0.4
			if t > 1.0 and t < 2.4:
				var p_car := car.mass * car.linear_velocity.z
				var p_slab := slab.mass * slab.linear_velocity.z
				d["p_car"] = snappedf(p_car, 1.0)
				d["p_slab"] = snappedf(p_slab, 1.0)
		"reverse":
			if t > 1.0:
				car.brake = 1.0
			d["min_kmh"] = snappedf(minf(d.get("min_kmh", 0.0), kmh), 0.1)
			d["gear_end"] = car.get_gear()
		"jump":
			if lane.phase == 0:
				_cruise(car, 60.0)
				if car.global_position.z < -78.0:
					lane.phase = 1
			else:
				car.throttle = 0.3
				var air := true
				for w in car.get_children():
					if w is FellowWheel3D and (w as FellowWheel3D).is_in_contact():
						air = false
				if air:
					d["air_frames"] = d.get("air_frames", 0) + 1
				d["height_max"] = snappedf(maxf(d.get("height_max", 0.0), car.global_position.y), 0.01)


func _on_frame() -> void:
	if done:
		return
	frame += 1
	if many > 0:
		_many_tick()
		return
	var dt := 1.0 / 60.0
	if turntable:
		turntable.rotate_y(0.5 * dt)
	for lane in lanes:
		_drive(lane, dt)
	for name in cameras:
		var car: FellowVehicle3D = _find(name).car
		var cam: Camera3D = cameras[name]
		var back := car.global_basis.z
		back.y = 0.0
		back = back.normalized() if back.length() > 0.1 else Vector3(0, 0, 1)
		if name == "crash":
			# Milestone 105: the front, where the wall dents it (from ahead and above, once the car has backed off the wall).
			cam.look_at_from_position(car.global_position - back * 4.6 + Vector3(0.9, 1.9, 0), car.global_position - back * 2.0 + Vector3(0, 0.55, 0))
		else:
			cam.look_at_from_position(car.global_position + back * 9.0 + Vector3(4, 4, 0), car.global_position + Vector3(0, 0.8, 0))
	if pictures:
		_shoot()
	if frame >= int(RUN_SECONDS * 60.0):
		done = true
		_finish.call_deferred()


func _shoot() -> void:
	for name in viewports:
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
		var car: FellowVehicle3D = lane.car
		var d: Dictionary = lane.data
		var e := d.duplicate()
		for k in ["v_prev", "a_smooth", "local0", "p0", "coast_t0", "comp_prev", "vy_prev", "t_stop"]:
			e.erase(k)
		e["gears"] = lane.gears
		e["end"] = [snappedf(car.global_position.x - lane.start.x, 0.1), snappedf(car.global_position.y, 0.01), snappedf(car.global_position.z, 0.1)]
		# Upright all the way, its body never on the ground (a hard landing may touch it: on its wheels at the end).
		var upright: bool = d.get("min_up", 0.0) > 0.7 and (d.get("min_y", -9.0) > -0.3 or (lane.name == "jump" and car.global_position.y > -0.2))
		var ok := upright
		match lane.name:
			"accel":
				# 0-100 km/h in 5-12 s (400 N·m, 1 400 kg), up through the gears.
				ok = ok and d.has("t100") and d.t100 > 5.0 and d.t100 < 12.0 and lane.gears.size() >= 2
			"brake":
				# From 100 km/h on grip 1.0: 35-60 m, straight.
				ok = ok and d.has("stop_m") and d.stop_m > 30.0 and d.stop_m < 60.0 and d.heading_change < 5.0
			"skidpad":
				ok = ok and d.get("a_lat_max_g", 0.0) > 0.6 and d.get("a_lat_max_g", 0.0) < 1.2
			"lane_change":
				ok = ok and d.get("roll_max_deg", 99.0) < 10.0 and absf(d.get("heading_after", 99.0)) < 15.0 and d.get("kmh_after", 0.0) > 50.0
			"bumps":
				ok = ok and d.get("comp_max", 1.0) <= 0.3 and d.get("pitch_max_deg", 99.0) < 8.0
				# Milestone 105: its body undented over ten bumps.
				var dn := _dents(car)
				e["dents"] = {"deepest_m": dn.deepest_m, "impacts": dn.impacts}
				ok = ok and dn.deepest_m == 0.0
			"handbrake":
				ok = ok and d.get("rear_slip_max_deg", 0.0) > 8.0
			"reverse":
				ok = ok and d.get("min_kmh", 0.0) < -5.0 and d.get("gear_end", 0) == -1
			"jump":
				ok = ok and d.get("air_frames", 0) > 10 and d.get("min_up", 0.0) > 0.7
			"coast":
				# More than the air alone (0.15 m/s² at 80 km/h), less than braking.
				ok = ok and d.get("coast_decel", 0.0) > 0.3 and d.get("coast_decel", 9.0) < 3.0
			"ice":
				# A quarter of the grip: about a quarter of the speed of the dry lane's at 3 s (about 45 km/h).
				ok = ok and d.get("kmh_3s", 0.0) > 5.0 and d.get("kmh_3s", 99.0) < 25.0
			"slope":
				ok = ok and d.get("drift_max", 99.0) < 0.02
			"slope_free":
				# Rolled back at least a metre in 5 s (the tyres hold where they stand only with the brakes).
				ok = ok and d.get("rolled_m", 0.0) > 1.0
			"turntable":
				ok = ok and d.get("turned_deg", 0.0) != 0.0 and d.get("drift_max", 99.0) < 0.2
			"slab":
				ok = ok and absf(d.get("p_car", 0.0)) > 2000.0 and absf(d.get("p_car", 0.0) + d.get("p_slab", 0.0)) < 0.15 * absf(d.get("p_car", 0.0))
			"curb_ray", "curb_cylinder", "gap_ray", "gap_cylinder":
				# Both onto the curb and over the trench (driven on past them).
				var curb: bool = lane.name.begins_with("curb")
				ok = ok and d.get("end_z", 0.0) < -40.0 and d.get("end_y", -1.0) > (0.25 if curb else 0.15)
			"crash":
				# Milestone 105: the front dented 5-30 cm into the body, the rear and the middle not; the car stopped by the wall.
				var dn := _dents(car)
				e["dents"] = dn
				ok = ok and dn.front_m > 0.05 and dn.front_m <= 0.3 and dn.rear_m == 0.0 and dn.front_along_z > 0.9 and dn.impacts.size() >= 1 and d.get("stopped_z", -99.0) > -30.0
			"tap":
				# At 5 km/h: under the threshold, nothing dented.
				var dn := _dents(car)
				e["dents"] = dn
				ok = ok and dn.deepest_m == 0.0 and dn.impacts.is_empty() and dn.dented_meshes == 0
		e["ok"] = ok
		if not ok:
			failures.append(lane.name)
		out[lane.name] = e
	# Milestone 101: the cylinders meet the curb over several steps (the rays all at once, in one step), and bridge the
	# trench (the rays drop into it).
	if out.has("curb_cylinder"):
		var cr: Dictionary = out.curb_ray
		var cc: Dictionary = out.curb_cylinder
		var gr: Dictionary = out.gap_ray
		var gc: Dictionary = out.gap_cylinder
		var gr_drop: float = gr.get("comp_before", 0.0) - gr.get("comp_min_near", 0.0)
		var gc_drop: float = gc.get("comp_before", 0.0) - gc.get("comp_min_near", 0.0)
		var cmp := {"curb_step_ray": cr.get("comp_step_max", 0.0), "curb_step_cylinder": cc.get("comp_step_max", 0.0),
			"curb_a_ray_g": cr.get("a_y_max_g", 0.0), "curb_a_cylinder_g": cc.get("a_y_max_g", 0.0),
			"gap_drop_ray": snappedf(gr_drop, 0.0001), "gap_drop_cylinder": snappedf(gc_drop, 0.0001)}
		cmp["ok"] = cmp.curb_step_cylinder < cmp.curb_step_ray * 0.5 and cmp.curb_a_cylinder_g < cmp.curb_a_ray_g and cmp.gap_drop_ray > 0.05 and cmp.gap_drop_cylinder < 0.03
		out["contact_shapes"] = cmp
		if not cmp.ok:
			failures.append("contact_shapes")
	result["lanes"] = out
	result["failures"] = failures
	result["global"] = FellowVehicle3D.get_global_stats()
	result["physics_engine"] = ProjectSettings.get_setting("physics/3d/physics_engine")
	if pictures:
		_save_strip()
	print("VEHICLE_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("vehicle_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)


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
	strip.save_png(out_dir.path_join("vehicle_strip.png"))


func _many_tick() -> void:
	var now := Time.get_ticks_usec()
	if frame == 120:
		FellowVehicle3D.reset_global_stats()
		many_last = now
		return
	if frame < 120:
		return
	many_frames.append(float(now - many_last) / 1000.0)
	many_last = now
	if frame >= 120 + int(many_seconds * 60.0):
		done = true
		var st := FellowVehicle3D.get_global_stats()
		var n := float(frame - 120)
		var fr := many_frames.duplicate()
		fr.sort()
		var upright := 0
		for c in many_cars:
			upright += 1 if (c as FellowVehicle3D).global_basis.y.dot(Vector3.UP) > 0.7 else 0
		var r := {"cars": many, "frames": int(n), "stats": st,
			"vehicles_ms_per_frame": snappedf(float(st.step_usec) / n / 1000.0, 0.001),
			"step_us_each": snappedf(float(st.step_usec) / maxf(1.0, float(st.steps)), 0.01),
			"frame_ms_mean": snappedf(_mean(many_frames), 0.001), "frame_ms_p99": snappedf(fr[int(fr.size() * 0.99)], 0.001),
			"upright": upright, "threads": OS.get_processor_count(),
			"physics_engine": ProjectSettings.get_setting("physics/3d/physics_engine")}
		print("VEHICLE_MANY " + JSON.stringify(r))
		var f := FileAccess.open(out_dir.path_join("vehicle_many.json"), FileAccess.WRITE)
		f.store_string(JSON.stringify(r, "  "))
		f.close()
		quit(0)


func _mean(a: PackedFloat64Array) -> float:
	var s := 0.0
	for v in a:
		s += v
	return s / maxf(1.0, float(a.size()))
