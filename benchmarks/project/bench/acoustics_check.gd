extends SceneTree
## FellowEngine milestone 78 check (decision 0186): FellowAcoustics3D — sound through the scene's geometry.
##
##   godot --path benchmarks/project --headless --fixed-fps 60 -s res://bench/acoustics_check.gd -- --out=<dir>
##   godot --path benchmarks/project --fixed-fps 60 --resolution 64x64 --write-movie <dir>/m.png
##       -s res://bench/acoustics_check.gd -- --out=<dir> --listen=1          (the mix measured, Movie Maker's audio)
##   godot ... -s res://bench/acoustics_check.gd -- --out=<dir> --many=256 [--seconds=6]
##
## A listener (the camera) at the origin, noise sources 8 m away: one in the open, one behind a wall, one past a
## wall's edge, one inside a car's body (its own body does not block it), one moving from the open to behind a wall
## (how the gain changes from frame to frame); then the listener carried into a closed room, under a roof and back to
## the open (the room's enclosure, decay, predelay and the reverb send). With --listen=1 the sources play one after
## another and the master bus is captured: their loudness and highs (the stock mix with the occlusion's gain and
## shelf). One JSON line: ACOUSTICS_CHECK {...}; exit code 1 when a lane did otherwise than expected. --many=N: N
## sources among walls, the node's time and rays a frame. One JSON line: ACOUSTICS_MANY {...}.

var args := {}
var out_dir := ""
var result := {}
var root3d: Node3D
var cam: Camera3D
var acoustics: FellowAcoustics3D
var frame := 0
var listen := false
var many := 0
var many_seconds := 6.0
var sources := {}
var mover: AudioStreamPlayer3D
var data := {}
var capture: AudioEffectCapture
var capture_idx := -1
var listen_plan := []
var done := false


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://acoustics_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	listen = _arg("listen", "0") == "1"
	many = int(_arg("many", "0"))
	many_seconds = float(_arg("seconds", "6"))
	_build.call_deferred()


# A second of white noise (seeded), looping.
func _noise() -> AudioStreamWAV:
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	var n := 44100
	var bytes := PackedByteArray()
	bytes.resize(n * 2)
	for i in n:
		bytes.encode_s16(i * 2, int(rng.randf_range(-0.5, 0.5) * 32767.0))
	var w := AudioStreamWAV.new()
	w.format = AudioStreamWAV.FORMAT_16_BITS
	w.mix_rate = 44100
	w.stereo = false
	w.data = bytes
	w.loop_mode = AudioStreamWAV.LOOP_FORWARD
	w.loop_end = n
	return w


func _box(pos: Vector3, size: Vector3, parent: Node = null) -> StaticBody3D:
	var b := StaticBody3D.new()
	b.position = pos
	var cs := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = size
	cs.shape = shape
	b.add_child(cs)
	var mi := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	mi.mesh = mesh
	b.add_child(mi)
	(parent if parent else root3d).add_child(b)
	return b


func _source(name: String, pos: Vector3, parent: Node = null) -> AudioStreamPlayer3D:
	var p := AudioStreamPlayer3D.new()
	p.name = name
	p.stream = _noise()
	p.position = pos
	p.unit_size = 10.0
	(parent if parent else root3d).add_child(p)
	sources[name] = p
	return p


func _build() -> void:
	root3d = Node3D.new()
	get_root().add_child(root3d)
	cam = Camera3D.new()
	root3d.add_child(cam)
	cam.position = Vector3(0, 1.7, 0)
	cam.current = true
	# The ground.
	_box(Vector3(0, -0.5, 0), Vector3(1000, 1, 1000))
	if many > 0:
		_build_many()
		return
	# East: in the open. North: behind a wall 6 x 4 m at 4 m. West: past a wall's edge (the line to it 0.1 m clear of
	# the edge, the source's other side behind it). South: inside a car's body (a rigid body's box round it).
	_source("open", Vector3(8, 1.7, 0))
	_box(Vector3(0, 2, -4), Vector3(6, 4, 0.3))
	_source("wall", Vector3(0, 1.7, -8))
	_box(Vector3(-4, 2, -3.3), Vector3(0.3, 4, 6))
	_source("edge", Vector3(-8, 1.7, -0.4))
	var car := RigidBody3D.new()
	car.freeze = true
	car.position = Vector3(0, 0, 8)
	var cs := CollisionShape3D.new()
	var bs := BoxShape3D.new()
	bs.size = Vector3(1.8, 1.4, 4.2)
	cs.shape = bs
	cs.position = Vector3(0, 0.9, 0)
	car.add_child(cs)
	root3d.add_child(car)
	_source("car", Vector3(0, 1.2, 0.6), car)
	# A source going round the listener at 7 m (0.5 rad/s), behind a wall 3 m long across its path at 4 m for part of
	# the circle.
	_box(Vector3(2.83, 2, 2.83), Vector3(0.3, 4, 3), null).rotation_degrees = Vector3(0, -45, 0)
	mover = _source("moving", Vector3(7, 1.7, 0))
	# Rooms for the listener, far away: closed (8 x 8 x 4 m, walls 0.3 m), a roof on posts (10 x 10 m at 4 m).
	var room := Vector3(300, 0, 0)
	_box(room + Vector3(0, 2, -4), Vector3(8.3, 4, 0.3))
	_box(room + Vector3(0, 2, 4), Vector3(8.3, 4, 0.3))
	_box(room + Vector3(-4, 2, 0), Vector3(0.3, 4, 8))
	_box(room + Vector3(4, 2, 0), Vector3(0.3, 4, 8))
	_box(room + Vector3(0, 4.15, 0), Vector3(8.3, 0.3, 8.3))
	if listen:
		_source("inroom", room + Vector3(2.5, 1.7, 1.0))
	var roof := Vector3(-300, 0, 0)
	_box(roof + Vector3(0, 4.15, 0), Vector3(10, 0.3, 10))
	acoustics = FellowAcoustics3D.new()
	root3d.add_child(acoustics)
	if listen:
		_setup_listen()
	else:
		for n in sources:
			sources[n].play()
	physics_frame.connect(_on_frame)


func _setup_listen() -> void:
	# The master bus captured; each source alone for a second (after half a second to settle), then the same
	# source with the node disabled (stock), the reverb off but in the room.
	acoustics.reverb = false
	capture = AudioEffectCapture.new()
	capture.buffer_length = 2.0
	AudioServer.add_bus_effect(0, capture)
	capture_idx = AudioServer.get_bus_effect_count(0) - 1
	for n in ["open", "wall", "edge", "car"]:
		listen_plan.append({"source": n, "fellow": true})
		listen_plan.append({"source": n, "fellow": false})
	# A source in the closed room with the listener: the reverb's send (with the node) and its tail after it stops.
	listen_plan.append({"source": "inroom", "fellow": true, "room": true})
	listen_plan.append({"source": "inroom", "fellow": false, "room": true})


func _measure(name: String, key: String, value) -> void:
	if not data.has(name):
		data[name] = {}
	data[name][key] = value


var listen_step := -1
var listen_t := 0.0
var listen_acc := {}
var listen_tail := {}
var lp_state := Vector2()


# Loudness (RMS, dB) and the share of energy above about 2 kHz (a one-pole low-pass split) of what was captured.
func _listen_tick(dt: float) -> void:
	var avail := capture.get_frames_available()
	var buf := capture.get_buffer(avail)
	var step: Dictionary = listen_plan[listen_step] if listen_step >= 0 else {}
	var length := 2.2 if step.get("room", false) else 1.5
	if listen_step >= 0 and listen_t > 0.5:
		var a := 1.0 - exp(-TAU * 2000.0 / 44100.0)
		# The tail from 0.2 s after the stop (what was mixed before it has passed the capture by then).
		var acc: Dictionary = listen_acc if listen_t <= 1.5 else (listen_tail if listen_t > 1.7 else {"sum": 0.0, "hi": 0.0, "n": 0})
		for f in buf:
			var x := (f.x + f.y) * 0.5
			lp_state.x += (x - lp_state.x) * a
			var hi := x - lp_state.x
			acc.sum += x * x
			acc.hi += hi * hi
			acc.n += 1
	listen_t += dt
	if listen_step >= 0 and listen_t >= 1.5 and listen_t - dt < 1.5:
		# The direct sound measured; in a room the source stops and what is heard after it is its reverb's tail.
		sources[step.source].stop()
	if listen_step < 0 or listen_t >= length:
		if listen_step >= 0:
			var rms := sqrt(listen_acc.sum / maxf(1.0, listen_acc.n))
			var tag := "fellow" if step.fellow else "stock"
			_measure("listen_" + step.source, tag + "_db", snappedf(linear_to_db(maxf(rms, 1e-9)), 0.01))
			_measure("listen_" + step.source, tag + "_high_share", snappedf(listen_acc.hi / maxf(1e-12, listen_acc.sum), 0.0001))
			if step.get("room", false):
				var tail := sqrt(listen_tail.sum / maxf(1.0, listen_tail.n))
				_measure("listen_" + step.source, tag + "_tail_db", snappedf(linear_to_db(maxf(tail, 1e-9)), 0.01))
				if step.fellow:
					_measure("listen_" + step.source, "room", acoustics.get_room())
		listen_step += 1
		if listen_step >= listen_plan.size():
			done = true
			_finish.call_deferred()
			return
		var next: Dictionary = listen_plan[listen_step]
		acoustics.enabled = next.fellow
		acoustics.reverb = next.get("room", false) and next.fellow
		cam.position = Vector3(300, 1.7, 0) if next.get("room", false) else Vector3(0, 1.7, 0)
		sources[next.source].play()
		listen_t = 0.0
		listen_acc = {"sum": 0.0, "hi": 0.0, "n": 0}
		listen_tail = {"sum": 0.0, "hi": 0.0, "n": 0}


var last_gain_db := 0.0


func _on_frame() -> void:
	if done:
		return
	frame += 1
	var dt := 1.0 / 60.0
	if many > 0:
		_many_tick()
		return
	if listen:
		_listen_tick(dt)
		return
	var t := frame * dt
	# The moving source round the listener.
	var ang := t * 0.5
	mover.position = Vector3(cos(ang) * 7.0, 1.7, sin(ang) * 7.0)
	var occ := acoustics.get_occlusion(mover)
	var gain_db := occ * acoustics.occluded_volume_db
	if frame > 2 and frame < 780:
		_measure("moving", "max_step_db", snappedf(maxf(data.get("moving", {}).get("max_step_db", 0.0), absf(gain_db - last_gain_db)), 0.001))
	if frame < 780:
		_measure("moving", "max_occlusion", snappedf(maxf(data.get("moving", {}).get("max_occlusion", 0.0), occ), 0.001))
	last_gain_db = gain_db
	# The listener: among the sources for the moving one's lap (13 s), then in the open, in the closed room and under
	# the roof (3 s each).
	if frame == 90:
		for n in ["open", "wall", "edge", "car"]:
			var p: AudioStreamPlayer3D = sources[n]
			_measure(n, "occlusion", snappedf(acoustics.get_occlusion(p), 0.001))
			_measure(n, "gain_db", snappedf(acoustics.get_occlusion(p) * acoustics.occluded_volume_db, 0.01))
		_measure("stats", "stats", acoustics.get_stats())
	if frame == 780:
		cam.position = Vector3(0, 1.7, 300)
	if frame == 960:
		_measure("room_open", "room", acoustics.get_room())
		cam.position = Vector3(300, 1.7, 0)
	if frame == 1140:
		_measure("room_closed", "room", acoustics.get_room())
		cam.position = Vector3(-300, 1.7, 0)
	if frame == 1320:
		_measure("room_roof", "room", acoustics.get_room())
		done = true
		_finish.call_deferred()


func _build_many() -> void:
	# A grid of walls 6 m apart, N sources among them within 60 m.
	for i in range(-5, 6):
		for j in range(-5, 6):
			if (i + j) % 2 == 0:
				_box(Vector3(i * 6.0 + 3.0, 2, j * 6.0), Vector3(0.3, 4, 4))
	var rng := RandomNumberGenerator.new()
	rng.seed = 3
	for k in many:
		var p := _source("s%d" % k, Vector3(rng.randf_range(-30, 30), 1.0, rng.randf_range(-30, 30)))
		p.play()
	acoustics = FellowAcoustics3D.new()
	root3d.add_child(acoustics)
	physics_frame.connect(_on_frame)


var many_usec: PackedInt64Array = []
var many_rays: PackedInt64Array = []


func _many_tick() -> void:
	cam.position = Vector3(sin(frame * 0.01) * 20.0, 1.7, cos(frame * 0.013) * 20.0)
	if frame < 30:
		return
	var st := acoustics.get_stats()
	many_usec.append(st.usec)
	many_rays.append(st.rays)
	if frame >= 30 + int(many_seconds * 60.0):
		done = true
		var s := 0
		var r := 0
		var mx := 0
		for i in many_usec.size():
			s += many_usec[i]
			r += many_rays[i]
			mx = maxi(mx, many_usec[i])
		var res := {"sources": many, "frames": many_usec.size(), "usec_mean": snappedf(float(s) / many_usec.size(), 0.1),
			"usec_max": mx, "rays_mean": snappedf(float(r) / many_rays.size(), 0.1), "stats": st,
			"physics_engine": ProjectSettings.get_setting("physics/3d/physics_engine")}
		print("ACOUSTICS_MANY " + JSON.stringify(res))
		var f := FileAccess.open(out_dir.path_join("acoustics_many.json"), FileAccess.WRITE)
		f.store_string(JSON.stringify(res, "  "))
		f.close()
		quit(0)


func _finish() -> void:
	var failures := []
	var expect := func(name: String, ok: bool) -> void:
		_measure(name, "ok", ok)
		if not ok:
			failures.append(name)
	if listen:
		for n in ["open", "car"]:
			var d: Dictionary = data["listen_" + n]
			expect.call("listen_" + n, absf(d.fellow_db - d.stock_db) < 0.5)
		var w: Dictionary = data["listen_wall"]
		expect.call("listen_wall", w.fellow_db < w.stock_db - 8.0 and w.fellow_high_share < w.stock_high_share * 0.5)
		var r: Dictionary = data["listen_inroom"]
		# The reverb's tail (0.2-0.7 s after the source stops) within 40 dB of the direct sound with the node; nothing
		# after the stock source stops.
		expect.call("listen_inroom", r.fellow_tail_db > r.fellow_db - 40.0 and r.stock_tail_db < -100.0)
		var e: Dictionary = data["listen_edge"]
		expect.call("listen_edge", e.fellow_db < e.stock_db - 1.0 and e.fellow_db > w.fellow_db - 0.5 + (w.stock_db - e.stock_db))
	else:
		expect.call("open", data.open.occlusion < 0.01)
		expect.call("wall", data.wall.occlusion > 0.99)
		expect.call("edge", data.edge.occlusion > 0.1 and data.edge.occlusion < 0.9)
		expect.call("car", data.car.occlusion < 0.01)
		expect.call("moving", data.moving.max_occlusion > 0.9 and data.moving.max_step_db < 2.0)
		var ro: Dictionary = data.room_open.room
		var rc: Dictionary = data.room_closed.room
		var rr: Dictionary = data.room_roof.room
		expect.call("room_open", ro.enclosure < 0.05 and ro.send < 0.03)
		expect.call("room_closed", rc.enclosure > 0.95 and rc.rt60 > 0.2 and rc.rt60 < 2.0 and rc.predelay_msec > 5.0 and rc.predelay_msec < 40.0)
		expect.call("room_roof", rr.enclosure > 0.1 and rr.enclosure < 0.8)
	result["lanes"] = data
	result["failures"] = failures
	result["global"] = FellowAcoustics3D.get_global_stats()
	result["physics_engine"] = ProjectSettings.get_setting("physics/3d/physics_engine")
	print("ACOUSTICS_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("acoustics_listen.json" if listen else "acoustics_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)
