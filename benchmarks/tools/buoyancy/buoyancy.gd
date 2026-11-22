extends SceneTree
## FellowEngine's buoyancy (milestone 175, decision 0317): RigidBody3Ds with a FellowBuoyancy3D on FellowWater3D,
## headless (the waves on the CPU; no GPU needed). Jolt Physics, 60 Hz.
##
## * still water: boxes of 250, 500 and 750 kg/m³, a sphere, a barrel on its side, a capsule, a convex wedge; their
##   displaced volume at rest against their mass / the water's density (Archimedes), and how long they take to settle;
##   a box of 1500 kg/m³ sinks;
## * stability: a raft tilted 30° (rights itself), a tall post (lies down), a cube of half the water's density (floats
##   edge up: a cube between 0.21 and 0.79 of the water's density is unstable face up);
## * waves (12 m/s wind, 60 km fetch): a buoy's height against the surface's under it, with and without the water's
##   own motion (follow_waves); a raft; twenty crates; the CPU time.
##
##   godot --headless --fixed-fps 60 --path benchmarks/tools/buoyancy -s buoyancy.gd -- [--out=<file.json>] [--seconds=20]
##       [--set=linear_drag:2,angular_drag:2,...]

var _out := ""
var _seconds := 20.0
var _params := {}
var _still: Node3D
var _sea: Node3D
var _bodies := {} # name -> {body, buoy, ...}
var _frame := 0
var _frames := 0
var _rows := {}


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		var v := a.get_slice("=", 1)
		if a.begins_with("--out="):
			_out = v
		elif a.begins_with("--seconds="):
			_seconds = float(v)
		elif a.begins_with("--set="):
			for kv in v.split(","):
				_params[kv.get_slice(":", 0)] = float(kv.get_slice(":", 1))
	_frames = int(round(_seconds * 60.0))
	# Two bodies of water side by side: still (x < 0) and a sea (x > 100).
	_still = _water(Vector3(-50, 0, 0), Vector2(100, 100), 0.0)
	_sea = _water(Vector3(150, 0, 0), Vector2(100, 100), 1.0)
	_sea.set("wind_speed", 12.0)
	_sea.set("fetch", 60.0)
	_sea.set("time_auto", false)
	var x := -90.0
	for d in [250.0, 500.0, 750.0]:
		_add("box_%d" % int(d), _box(Vector3(1.0, 0.5, 1.0)), d, Vector3(x, 0.6, 0))
		x += 6.0
	_add("sphere_500", _sphere(0.4), 500.0, Vector3(x, 0.6, 0))
	x += 6.0
	_add("barrel_400", _cylinder(0.3, 0.9), 400.0, Vector3(x, 0.6, 0), Basis(Vector3.BACK, PI * 0.5))
	x += 6.0
	_add("capsule_600", _capsule(0.3, 1.2), 600.0, Vector3(x, 0.8, 0), Basis(Vector3.BACK, PI * 0.5))
	x += 6.0
	var wedge := ConvexPolygonShape3D.new()
	wedge.points = PackedVector3Array([Vector3(-0.6, -0.3, -0.4), Vector3(0.6, -0.3, -0.4), Vector3(-0.6, -0.3, 0.4),
			Vector3(0.6, -0.3, 0.4), Vector3(-0.6, 0.3, 0.0), Vector3(0.6, 0.3, 0.0)])
	_add("wedge_500", wedge, 500.0, Vector3(x, 0.6, 0))
	x += 6.0
	_add("sinker_1500", _box(Vector3(0.5, 0.5, 0.5)), 1500.0, Vector3(x, 0.3, 0))
	x += 6.0
	_add("raft_tilted", _box(Vector3(2.0, 0.3, 1.2)), 500.0, Vector3(x, 0.4, 0), Basis(Vector3.BACK, deg_to_rad(30.0)))
	x += 6.0
	_add("post_500", _box(Vector3(0.4, 1.6, 0.4)), 500.0, Vector3(x, 0.5, 0), Basis(Vector3.BACK, deg_to_rad(2.0)))
	x += 6.0
	_add("cube_500", _box(Vector3(1.0, 1.0, 1.0)), 500.0, Vector3(x, 0.6, 0), Basis(Vector3.BACK, deg_to_rad(2.0)))
	# The sea.
	_add("buoy", _sphere(0.25), 300.0, Vector3(150, 0.5, 0))
	_add("buoy_still_drag", _sphere(0.25), 300.0, Vector3(150, 0.5, 10), Basis(), false)
	_add("raft_sea", _box(Vector3(3.0, 0.3, 2.0)), 500.0, Vector3(150, 0.5, -10))
	var rng := RandomNumberGenerator.new()
	rng.seed = 5
	for i in 20:
		_add("crate_%d" % i, _box(Vector3(0.6, 0.6, 0.6)), 400.0, Vector3(170.0 + rng.randf_range(-15, 15), 1.0,
				rng.randf_range(-30, 30)), Basis(Vector3.UP, rng.randf() * TAU))


func _water(p_at: Vector3, p_extent: Vector2, p_scale: float) -> Node3D:
	var w: Node3D = ClassDB.instantiate("FellowWater3D")
	w.set("extent", p_extent)
	w.set("wave_scale", p_scale)
	root.add_child(w)
	w.global_position = p_at
	return w


func _box(p_size: Vector3) -> Shape3D:
	var s := BoxShape3D.new()
	s.size = p_size
	return s


func _sphere(p_r: float) -> Shape3D:
	var s := SphereShape3D.new()
	s.radius = p_r
	return s


func _cylinder(p_r: float, p_h: float) -> Shape3D:
	var s := CylinderShape3D.new()
	s.radius = p_r
	s.height = p_h
	return s


func _capsule(p_r: float, p_h: float) -> Shape3D:
	var s := CapsuleShape3D.new()
	s.radius = p_r
	s.height = p_h
	return s


func _volume(p_shape: Shape3D) -> float:
	if p_shape is BoxShape3D:
		var z: Vector3 = p_shape.size
		return z.x * z.y * z.z
	if p_shape is SphereShape3D:
		return 4.0 / 3.0 * PI * pow(p_shape.radius, 3)
	if p_shape is CylinderShape3D:
		return PI * p_shape.radius * p_shape.radius * p_shape.height
	if p_shape is CapsuleShape3D:
		var r: float = p_shape.radius
		return PI * r * r * (p_shape.height - 2.0 * r) + 4.0 / 3.0 * PI * r * r * r
	return 1.2 * 0.6 * 0.8 * 0.5 # the wedge: a triangular prism


func _add(p_name: String, p_shape: Shape3D, p_density: float, p_at: Vector3, p_basis := Basis(), p_follow := true) -> void:
	var body := RigidBody3D.new()
	body.name = p_name
	var cs := CollisionShape3D.new()
	cs.shape = p_shape
	body.add_child(cs)
	var vol := _volume(p_shape)
	body.mass = p_density * vol
	var buoy: Node3D = ClassDB.instantiate("FellowBuoyancy3D")
	buoy.set("follow_waves", p_follow)
	for k in _params:
		buoy.set(k, _params[k])
	body.add_child(buoy)
	root.add_child(body)
	body.global_transform = Transform3D(p_basis, p_at)
	_bodies[p_name] = {"body": body, "buoy": buoy, "volume": vol, "density": p_density, "y": [], "surface": [], "tilt": [],
			"vy": [], "settled_at": -1.0, "share": []}


func _physics_process(_d: float) -> bool:
	_sea.set("time", float(_frame) / 60.0)
	_frame += 1
	var t := float(_frame) / 60.0
	for n in _bodies:
		var c: Dictionary = _bodies[n]
		var body: RigidBody3D = c.body
		var p := body.global_position
		c.y.append(p.y)
		var water: Node3D = _sea if p.x > 100.0 else _still
		c.surface.append(float(water.call("get_surface_height", p)))
		c.tilt.append(rad_to_deg(acos(clampf((body.global_basis * Vector3.UP).dot(Vector3.UP), -1.0, 1.0))))
		c.vy.append(body.linear_velocity.y)
		c.share.append(float(c.buoy.call("get_submerged_share")))
		if c.settled_at < 0.0 and t > 1.0 and body.linear_velocity.length() < 0.01 and body.angular_velocity.length() < 0.01:
			c.settled_at = t
		elif body.linear_velocity.length() > 0.02:
			c.settled_at = -1.0
	if _frame >= _frames:
		_finish()
		return true
	return false


func _mean(a: Array, from: int) -> float:
	var s := 0.0
	for i in range(from, a.size()):
		s += a[i]
	return s / maxf(1.0, float(a.size() - from))


func _finish() -> void:
	var last := int(_frames * 0.75)
	print("still water (the last quarter of %.0f s):" % _seconds)
	for n in _bodies:
		var c: Dictionary = _bodies[n]
		if (c.body as Node3D).global_position.x > 100.0 and not n.begins_with("crate"):
			pass
		if n.begins_with("crate") or n.begins_with("buoy") or n == "raft_sea":
			continue
		var share := _mean(c.share, last)
		var displaced: float = share * c.volume
		var wanted: float = minf(c.density / 1000.0, 1.0) * c.volume
		var row := {"displaced_m3": displaced, "archimedes_m3": wanted, "error_pct": (displaced - wanted) / wanted * 100.0,
				"y": _mean(c.y, last), "tilt_deg": _mean(c.tilt, last), "settled_s": c.settled_at,
				"points": int(c.buoy.call("get_point_count"))}
		_rows[n] = row
		print("  %-12s displaced %.4f m³ (Archimedes %.4f: %+.1f %%), centre at %+.3f m, tilt %.1f°, settled %s, %d points" % [n,
				displaced, wanted, row.error_pct, row.y, row.tilt_deg, ("%.1f s" % c.settled_at) if c.settled_at >= 0.0 else "no",
				row.points])
	print("the sea (Hs %.2f m):" % float(_sea.call("get_significant_wave_height")))
	var from := 120
	for n in ["buoy", "buoy_still_drag", "raft_sea"]:
		var c: Dictionary = _bodies[n]
		var rel := []
		for i in range(from, c.y.size()):
			rel.append(c.y[i] - c.surface[i])
		var m := _mean(rel, 0)
		var dev := 0.0
		var sy := 0.0
		var ss := 0.0
		var syy := 0.0
		var sss := 0.0
		var sys := 0.0
		var k := 0
		for i in range(from, c.y.size()):
			dev += pow(c.y[i] - c.surface[i] - m, 2)
			sy += c.y[i]
			ss += c.surface[i]
			syy += c.y[i] * c.y[i]
			sss += c.surface[i] * c.surface[i]
			sys += c.y[i] * c.surface[i]
			k += 1
		var cov := sys / k - (sy / k) * (ss / k)
		var corr := cov / sqrt(maxf(1e-12, (syy / k - pow(sy / k, 2)) * (sss / k - pow(ss / k, 2))))
		var heave := sqrt(maxf(0.0, syy / k - pow(sy / k, 2)))
		var surf := sqrt(maxf(0.0, sss / k - pow(ss / k, 2)))
		_rows[n] = {"corr": corr, "rms_vs_surface_m": sqrt(dev / k), "heave_rms_m": heave, "surface_rms_m": surf,
				"tilt_mean_deg": _mean(c.tilt, from)}
		print("  %-16s heave rms %.3f m (the surface's %.3f), correlation %.3f, off the surface rms %.3f m, tilt %.1f°" % [n,
				heave, surf, corr, sqrt(dev / k), _mean(c.tilt, from)])
	var floating := 0
	var lost := 0
	var worst := 0.0
	for n in _bodies:
		if not n.begins_with("crate"):
			continue
		var c: Dictionary = _bodies[n]
		var d: float = c.y[-1] - c.surface[-1]
		worst = maxf(worst, absf(d))
		if absf(d) < 0.6:
			floating += 1
		else:
			lost += 1
	var us := 0.0
	var pts := 0
	for n in _bodies:
		us += float(_bodies[n].buoy.call("get_stats").usec_per_tick)
		pts += int(_bodies[n].buoy.call("get_point_count"))
	var ws: Dictionary = _sea.call("get_cpu_wave_stats")
	_rows["crates"] = {"floating": floating, "lost": lost, "worst_m": worst}
	_rows["cpu"] = {"bodies": _bodies.size(), "points": pts, "usec_per_body": us / _bodies.size(),
			"water_usec_per_update": ws.usec_per_update, "variance_share": ws.variance_share}
	print("  crates: %d of 20 on the surface (worst %.2f m off)" % [floating, worst])
	print("CPU: %d bodies, %d points, %.1f µs a body a tick; the sea's waves %.0f µs an update (%.1f %% of its variance)" % [
			_bodies.size(), pts, us / _bodies.size(), ws.usec_per_update, float(ws.variance_share) * 100.0])
	if _out != "":
		var f := FileAccess.open(_out, FileAccess.WRITE)
		f.store_string(JSON.stringify(_rows, "\t"))
		f.close()
