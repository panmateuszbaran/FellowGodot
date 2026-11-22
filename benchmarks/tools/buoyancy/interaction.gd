extends SceneTree
## FellowEngine's water interaction (milestone 176, decision 0318), headless (the waves on the CPU; no GPU needed).
## Jolt Physics, 60 Hz.
##
## * debris: crates of FellowDestructible3D broken in the air over still water and over a 12 m/s sea — wood (600
##   kg/m³: its pieces float, each at its density's draft), stone (2 400: they sink to the bottom), wood with
##   debris_floats off (they fall through the water as before);
## * the waves on a worker thread: the sea's clock its own (time_auto), the main thread's time in the waves' updates
##   against the worker's;
## * out of reach: crates on piers over the sea, 2.5 m and 5 m above it (the ticks skipped without the waves);
## * foam made by what floats, on a pond: a crate dropped in (its splash, the water_entered signal), a raft pushed
##   through the water at 3 m/s (its trail), the foam fading after it stops; the foam grid's CPU time; a crate made
##   under the surface (it rises through it without a splash).
##
##   godot --headless --fixed-fps 60 --path benchmarks/tools/buoyancy -s interaction.gd -- [--out=<file.json>]
##       [--seconds=14] [--frame_work_ms=4 (the rest of a frame's main-thread work, spun after the nodes' process)]

const BREAK_FRAME := 20

var _out := ""
var _seconds := 14.0
var _frame_work_ms := 0.0
var _frames := 0
var _frame := 0
var _still: Node3D
var _sea: Node3D
var _pond: Node3D
var _sunk_start: RigidBody3D
var _tank_foam := 0.0
var _crates := {} # name -> {d, body, pieces, entered}
var _piers := []
var _floaters := []
var _drop: RigidBody3D
var _raft: RigidBody3D
var _entered := {} # body name -> [speeds]
var _foam := [] # [t, cells, total]
var _rows := {}
var _wood: StandardMaterial3D


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		var v := a.get_slice("=", 1)
		if a.begins_with("--out="):
			_out = v
		elif a.begins_with("--seconds="):
			_seconds = float(v)
		elif a.begins_with("--frame_work_ms="):
			_frame_work_ms = float(v)
	_frames = int(round(_seconds * 60.0))
	if _frame_work_ms > 0.0:
		# The rest of a game's frame on the main thread after the nodes' process (drawing, scripts): spun.
		var gs := GDScript.new()
		gs.source_code = "extends Node\nvar ms := 0.0\nfunc _process(_d: float) -> void:\n\tvar t := Time.get_ticks_usec()\n\twhile Time.get_ticks_usec() - t < int(ms * 1000.0):\n\t\tpass\n"
		gs.reload()
		var n := Node.new()
		n.set_script(gs)
		n.set("ms", _frame_work_ms)
		n.process_priority = 1000
		root.add_child(n)
	_wood = StandardMaterial3D.new()
	_still = _water(Vector3(-50, 0, 0), Vector2(100, 100), 0.0)
	_sea = _water(Vector3(150, 0, 0), Vector2(100, 100), 1.0)
	_sea.set("wind_speed", 12.0)
	_sea.set("fetch", 60.0)
	_pond = _water(Vector3(-250, 0, 0), Vector2(60, 60), 0.0)
	# The bottom under the still water and the pond.
	_static_box(Vector3(100, 1, 100), Vector3(-50, -6.5, 0))
	_static_box(Vector3(60, 1, 60), Vector3(-250, -6.5, 0))
	# Debris.
	_crate("wood_still", Vector3(-80, 6, 30), 600.0, true)
	_crate("stone_still", Vector3(-60, 6, 30), 2400.0, true)
	_crate("wood_no_floating", Vector3(-40, 6, 30), 600.0, false)
	_crate("wood_sea", Vector3(150, 8, 20), 600.0, true)
	# Twenty crates floating on the sea (the waves asked for every tick); piers over it.
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	for i in 20:
		_floaters.append(_box_body("float_%d" % i, Vector3(0.6, 0.6, 0.6), 400.0,
				Vector3(150.0 + rng.randf_range(-20, 20), 1.0, rng.randf_range(-40, 0))))
	for h in [2.5, 5.0]:
		_static_box(Vector3(4, 0.4, 4), Vector3(120 + h * 8.0, h - 0.2, 30))
		for k in 4:
			var b := _box_body("pier_%.1f_%d" % [h, k], Vector3(0.6, 0.6, 0.6), 400.0, Vector3(120 + h * 8.0 - 1.0 + (k % 2) * 2.0, h + 0.3, 29.0 + (k / 2) * 2.0))
			_piers.append({"h": h, "body": b})
	# The pond: a crate dropped from 4 m, a raft pushed along it; a camera over it (the foam's window follows it).
	_drop = _box_body("drop", Vector3(0.8, 0.8, 0.8), 400.0, Vector3(-250, 4.0, -10))
	_raft = _box_body("raft", Vector3(2.0, 0.3, 1.0), 500.0, Vector3(-245, 0.05, -20))
	for b: RigidBody3D in [_drop, _raft]:
		var buoy: Node = b.get_node("Buoyancy")
		buoy.connect("water_entered", _on_entered.bind(b.name))
	# A crate made under the pond's surface, away from the rest (no splash: it was not seen out of the water).
	_sunk_start = _box_body("made_under", Vector3(0.8, 0.8, 0.8), 400.0, Vector3(-262, -1.5, 15))
	_sunk_start.linear_velocity = Vector3(0, -3, 0) # going down as it is made (a piece thrown from a break under water)
	_sunk_start.get_node("Buoyancy").connect("water_entered", _on_entered.bind("made_under"))
	var cam := Camera3D.new()
	root.add_child(cam)
	cam.position = Vector3(-250, 20, 0)
	cam.current = true


func _water(p_at: Vector3, p_extent: Vector2, p_scale: float) -> Node3D:
	var w: Node3D = ClassDB.instantiate("FellowWater3D")
	w.set("extent", p_extent)
	w.set("wave_scale", p_scale)
	w.position = p_at
	root.add_child(w)
	return w


func _static_box(p_size: Vector3, p_at: Vector3) -> void:
	var b := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var s := BoxShape3D.new()
	s.size = p_size
	cs.shape = s
	b.add_child(cs)
	b.position = p_at
	root.add_child(b)


func _box_body(p_name: String, p_size: Vector3, p_density: float, p_at: Vector3) -> RigidBody3D:
	var body := RigidBody3D.new()
	body.name = p_name
	var cs := CollisionShape3D.new()
	var s := BoxShape3D.new()
	s.size = p_size
	cs.shape = s
	body.add_child(cs)
	body.mass = p_density * p_size.x * p_size.y * p_size.z
	var buoy: Node3D = ClassDB.instantiate("FellowBuoyancy3D")
	buoy.name = "Buoyancy"
	body.add_child(buoy)
	body.position = p_at
	root.add_child(body)
	return body


# A 1 m crate (a RigidBody3D with a FellowDestructible3D), falling, broken in the air at BREAK_FRAME.
func _crate(p_name: String, p_at: Vector3, p_density: float, p_floats: bool) -> void:
	var body := RigidBody3D.new()
	body.name = p_name
	var mi := MeshInstance3D.new()
	mi.name = "Mesh"
	var bm := BoxMesh.new()
	mi.mesh = bm
	mi.set_surface_override_material(0, _wood)
	body.add_child(mi)
	var cs := CollisionShape3D.new()
	var s := BoxShape3D.new()
	cs.shape = s
	body.add_child(cs)
	body.mass = p_density
	var d: Node3D = ClassDB.instantiate("FellowDestructible3D")
	d.name = "Destructible"
	d.set("density", p_density)
	d.set("piece_count", 24)
	d.set("debris_lifetime", 0.0)
	d.set("debris_floats", p_floats)
	d.set("break_on_impact", false)
	d.set("prefracture", true) # the pattern made at ready: the pieces spawn on the frame they are broken
	body.add_child(d)
	body.position = p_at
	root.add_child(body)
	var c := {"d": d, "body": body, "pieces": [], "density": p_density, "share": {}, "y": {}, "vy": {}, "sink": 0.0}
	d.connect("broken", func(p: Array) -> void: c.pieces = p)
	_crates[p_name] = c


func _on_entered(p_speed: float, p_name: String) -> void:
	if not _entered.has(p_name):
		_entered[p_name] = []
	_entered[p_name].append([float(_frame) / 60.0, p_speed])


func _physics_process(_d: float) -> bool:
	_frame += 1
	var t := float(_frame) / 60.0
	if _frame == BREAK_FRAME:
		for n in _crates:
			var c: Dictionary = _crates[n]
			c.d.call("break_apart", c.body.global_position + Vector3(0.1, 0.2, 0.05))
	# The raft pushed at 3 m/s along +z from 2 s to 8 s.
	if t >= 2.0 and t < 8.0:
		var lv := _raft.linear_velocity
		_raft.linear_velocity = Vector3(0.0, lv.y, 3.0)
	if not _entered.has("made_under") or _entered.made_under.size() < 2:
		# Until it comes back down after rising out (a splash then is right): the foam round it.
		for dx in [-1.0, -0.5, 0.0, 0.5, 1.0]:
			for dz in [-1.0, -0.5, 0.0, 0.5, 1.0]:
				_tank_foam = maxf(_tank_foam, float(_pond.call("get_foam", _sunk_start.global_position + Vector3(dx, 0, dz))))
	if _frame % 6 == 0:
		var st: Dictionary = _pond.call("get_body_foam_stats")
		_foam.append([t, int(st.cells), float(st.total)])
	# How fast pieces reach the bottom (6 m down: between 4 and 5.5 m).
	for n in _crates:
		var c: Dictionary = _crates[n]
		for p in c.pieces:
			if is_instance_valid(p) and p.global_position.y < -4.0 and p.global_position.y > -5.5:
				c.sink = minf(c.sink, p.linear_velocity.y)
	# The debris' state over the last 2 s.
	if t > _seconds - 2.0:
		for n in _crates:
			var c: Dictionary = _crates[n]
			for p in c.pieces:
				if not is_instance_valid(p):
					continue
				var rb: RigidBody3D = p
				var b: Node = rb.get_node_or_null("Buoyancy")
				var key := rb.get_instance_id()
				if not c.share.has(key):
					c.share[key] = []
					c.y[key] = []
					c.vy[key] = []
				c.share[key].append(float(b.call("get_submerged_share")) if b else 0.0)
				c.y[key].append(rb.global_position.y)
				c.vy[key].append(rb.linear_velocity.y)
	if _frame >= _frames:
		_finish()
		return true
	return false


func _mean(a: Array) -> float:
	var s := 0.0
	for x in a:
		s += float(x)
	return s / maxf(1.0, float(a.size()))


func _finish() -> void:
	print("debris (24 pieces a crate, broken in the air; the last 2 s of %.0f s):" % _seconds)
	for n in _crates:
		var c: Dictionary = _crates[n]
		var afloat := 0
		var sunk := 0
		var shares := []
		var displaced := 0.0
		var mass := 0.0
		var us := 0.0
		var pts := 0
		var floor_ := 0
		for p in c.pieces:
			if not is_instance_valid(p):
				continue
			var rb: RigidBody3D = p
			var key := rb.get_instance_id()
			var sh := _mean(c.share[key])
			var y := _mean(c.y[key])
			var b: Node = rb.get_node_or_null("Buoyancy")
			if b:
				var st: Dictionary = b.call("get_stats")
				us += float(st.usec_per_tick)
				pts += int(st.points)
			if y < -5.0:
				floor_ += 1
			if sh > 0.02 and sh < 0.98 and absf(_mean(c.vy[key])) < 0.5:
				afloat += 1
				shares.append(sh)
				# Archimedes: at rest the debris displace their weight of water — the pile as a whole (pieces
				# resting on others are held up by them, those under them pushed down).
				var vol: float = float(b.call("get_volume"))
				displaced += sh * vol * 1000.0
				mass += rb.mass
			elif sh >= 0.98 or y < -0.5:
				sunk += 1
		var np: int = c.pieces.size()
		var arch := displaced / mass - 1.0 if mass > 0.0 else 0.0
		var row := {"pieces": np, "afloat": afloat, "sunk": sunk, "on_the_bottom": floor_,
				"mean_share": _mean(shares), "displaced_vs_mass": arch, "usec_per_piece": us / maxf(1.0, np), "points": pts,
				"fastest_at_the_bottom": -float(c.sink)}
		_rows[n] = row
		var down := " (reaching the bottom at up to %.1f m/s)" % -float(c.sink) if c.sink < 0.0 else ""
		print("  %-17s %d pieces: %d afloat (mean share under water %.3f; the water they displace against their mass %+.2f %%), %d sunk%s, %d on the bottom; %.1f µs a piece a tick, %d points" % [
				n, np, afloat, row.mean_share, arch * 100.0, sunk, down, floor_, row.usec_per_piece, pts])
	# The sea's waves.
	var ws: Dictionary = _sea.call("get_cpu_wave_stats")
	_rows["waves"] = ws
	print("the sea's waves: %d updates, %d on the worker; %.0f µs an update on the worker, %.1f µs on the main thread" % [
			int(ws.updates), int(ws.threaded_updates), float(ws.usec_per_update), float(ws.main_usec_per_update)])
	var fu := 0.0
	for b: RigidBody3D in _floaters:
		fu += float(b.get_node("Buoyancy").call("get_stats").usec_per_tick)
	_rows["floaters_usec"] = fu / _floaters.size()
	for h in [2.5, 5.0]:
		var sk := 0.0
		var tk := 0.0
		var u := 0.0
		for e in _piers:
			if e.h != h:
				continue
			var st: Dictionary = e.body.get_node("Buoyancy").call("get_stats")
			sk += float(st.skipped_ticks)
			tk += float(st.ticks)
			u += float(st.usec_per_tick)
		_rows["pier_%.1f" % h] = {"skipped": sk / maxf(1.0, tk), "usec": u / 4.0}
		print("  crates on a pier %.1f m over the sea: %.0f %% of the ticks skipped, %.2f µs a tick (floating: %.2f)" % [
				h, 100.0 * sk / maxf(1.0, tk), u / 4.0, fu / _floaters.size()])
	# Foam.
	var fs: Dictionary = _pond.call("get_body_foam_stats")
	_rows["entered"] = _entered
	_rows["foam"] = _foam
	_rows["foam_usec_per_frame"] = fs.usec_per_frame
	print("water_entered %s" % str(_entered))
	print("  a crate made 1.5 m under the surface going down at 3 m/s, rising back through it: the most foam round it %.3f (none) until it fell back in" % _tank_foam)
	_rows["made_under_foam"] = _tank_foam
	var lines := []
	for row in _foam:
		var tt: float = row[0]
		if is_equal_approx(fmod(tt, 1.0), 0.0) or (tt > 0.9 and tt < 1.6):
			lines.append("%.1f s: %d cells, %.1f" % [tt, row[1], row[2]])
	print("  foam (cells of 0.25 m covered, their sum): " + "; ".join(lines))
	print("  the foam grid: %.0f µs a frame while there is foam (%d frames)" % [float(fs.usec_per_frame), int(fs.frames)])
	if _out != "":
		var f := FileAccess.open(_out, FileAccess.WRITE)
		f.store_string(JSON.stringify(_rows, "\t"))
		f.close()
