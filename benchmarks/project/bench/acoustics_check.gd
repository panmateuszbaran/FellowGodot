extends SceneTree
## FellowEngine milestone 78 check (decision 0186): FellowAcoustics3D — sound through the scene's geometry.
##
##   godot --path benchmarks/project --headless --fixed-fps 60 -s res://bench/acoustics_check.gd -- --out=<dir>
##   godot --path benchmarks/project --fixed-fps 60 --resolution 64x64 --write-movie <dir>/m.png
##       -s res://bench/acoustics_check.gd -- --out=<dir> --listen=1          (the mix measured, Movie Maker's audio)
##   godot ... -s res://bench/acoustics_check.gd -- --out=<dir> --many=256 [--seconds=6] [--portals=1] [--rooms=2]
##
## A listener (the camera) at the origin, noise sources 8 m away: one in the open, one behind a wall, one past a
## wall's edge, one inside a car's body (its own body does not block it), one moving from the open to behind a wall
## (how the gain changes from frame to frame); then the listener carried into a closed room, under a roof and back to
## the open (the room's enclosure, decay, predelay and the reverb send). With --listen=1 the sources play one after
## another and the master bus is captured: their loudness and highs (the stock mix with the occlusion's gain and
## shelf). One JSON line: ACOUSTICS_CHECK {...}; exit code 1 when a lane did otherwise than expected. --many=N: N
## sources among walls, the node's time and rays a frame. One JSON line: ACOUSTICS_MANY {...}.
## Milestone 99 (decision 0207): three walls with doorways round the listener, a FellowAcousticPortal3D in each — one
## open (the source behind it heard through it, from its side), one closed (transmission 0), one shut by a door; then
## the door taken away and the closed one opened half. Milestone 111 (decision 0218): source_rooms 2 — the listener in
## the open, a source in a hall seen through its doorway and one in a closed closet (each sent to its own room's
## reverb), one in the open (the listener's); then the listener in the hall (its source the listener's, the closet's
## and the open one's their own); then source_rooms 0 (every source the listener's again, the buses gone). With --listen=1 the open one heard with and without portals and
## stock (its level, highs and stereo balance). --portals=1 with --many: an opening beside each wall.

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
const PORTALS_AT := Vector3(0, 0, -300)
const ROOMS_AT := Vector3(600, 0, 0)
var south_portal: FellowAcousticPortal3D
var door: StaticBody3D


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


# Milestone 99: a wall 12 x 4 x 0.3 m along its local X (turned by p_yaw), a doorway 1.2 x 2.2 m centred at p_door_x.
func _doorway_wall(at: Vector3, yaw: float, door_x: float) -> Node3D:
	var w := Node3D.new()
	root3d.add_child(w)
	w.position = at
	w.rotation_degrees = Vector3(0, yaw, 0)
	var a := door_x - 0.6
	var b := door_x + 0.6
	_box(Vector3((a - 6.0) * 0.5, 2, 0), Vector3(a + 6.0, 4, 0.3), w)
	_box(Vector3((b + 6.0) * 0.5, 2, 0), Vector3(6.0 - b, 4, 0.3), w)
	_box(Vector3(door_x, 3.1, 0), Vector3(1.2, 1.8, 0.3), w)
	return w


func _portal(at: Vector3, yaw: float, transmission: float) -> FellowAcousticPortal3D:
	var p := FellowAcousticPortal3D.new()
	p.size = Vector2(1.2, 2.2)
	p.transmission = transmission
	p.position = at
	p.rotation_degrees = Vector3(0, yaw, 0)
	root3d.add_child(p)
	return p


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
	# Milestone 90 (decision 0198): north-east, 12 m away, behind a pane of glass at 9 m (its metadata
	# "fellow_occlusion" 0.3), beyond the moving source's circle.
	var glass := _box(Vector3(6.36, 2, -6.36), Vector3(0.3, 4, 3), null)
	glass.rotation_degrees = Vector3(0, 45, 0)
	glass.set_meta(&"fellow_occlusion", 0.3)
	_source("glass", Vector3(8.49, 1.7, -8.49))
	# Milestone 91 (decision 0199): south-west, 12 m away, behind two panes (at 8 and 10 m, 0.3 each); and 20 degrees
	# west of south, behind a pane at 9 m and a wall at 10.5 m.
	for d in [8.0, 10.0]:
		var pane := _box(Vector3(-0.7071 * d, 2.0, 0.7071 * d), Vector3(3, 4, 0.3), null)
		pane.rotation_degrees = Vector3(0, -45, 0)
		pane.set_meta(&"fellow_occlusion", 0.3)
	_source("glass2", Vector3(-8.49, 1.7, 8.49))
	var dir20 := Vector3(-sin(deg_to_rad(20.0)), 0, cos(deg_to_rad(20.0)))
	var pane9 := _box(dir20 * 9.0 + Vector3(0, 2, 0), Vector3(3, 4, 0.3), null)
	pane9.rotation_degrees = Vector3(0, -20, 0)
	pane9.set_meta(&"fellow_occlusion", 0.3)
	_box(dir20 * 10.5 + Vector3(0, 2, 0), Vector3(3, 4, 0.3), null).rotation_degrees = Vector3(0, -20, 0)
	_source("glass_wall", dir20 * 12.0 + Vector3(0, 1.7, 0))
	# Milestone 92 (decision 0200): 15 degrees south of east, 12 m away, behind a curtain at 9 m that lets the level
	# through and stops the highs (fellow_occlusion 0.1, fellow_occlusion_highs 0.8).
	var dir105 := Vector3(sin(deg_to_rad(105.0)), 0, -cos(deg_to_rad(105.0)))
	var curtain := _box(dir105 * 9.0 + Vector3(0, 2, 0), Vector3(3, 4, 0.1), null)
	curtain.rotation_degrees = Vector3(0, -105, 0)
	curtain.set_meta(&"fellow_occlusion", 0.1)
	curtain.set_meta(&"fellow_occlusion_highs", 0.8)
	_source("curtain", dir105 * 12.0 + Vector3(0, 1.7, 0))
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
	# Milestone 99 (decision 0207): openings, far to the north (the listener carried there last). Walls 12 x 4 m,
	# 0.3 m thick, 4 m from the listener on three sides, each with a doorway 1.2 x 2.2 m 3 m to one side and a portal
	# in it; behind each a source 8 m from the listener. North: the portal open; south: closed (transmission 0, opened
	# half later); west: the doorway shut by a door (a slab in it; taken away later).
	var pa := PORTALS_AT
	_doorway_wall(pa + Vector3(0, 0, -4), 0.0, 3.0)
	_portal(pa + Vector3(3, 1.1, -4), 0.0, 1.0)
	_source("portal_open", pa + Vector3(0, 1.7, -8))
	_doorway_wall(pa + Vector3(0, 0, 4), 0.0, 3.0)
	south_portal = _portal(pa + Vector3(3, 1.1, 4), 0.0, 0.0)
	_source("portal_closed", pa + Vector3(0, 1.7, 8))
	var west := _doorway_wall(pa + Vector3(-4, 0, 0), 90.0, 3.0)
	door = _box(Vector3(3, 1.1, 0), Vector3(1.2, 2.2, 0.1), west)
	_portal(pa + Vector3(-4, 1.1, -3), 90.0, 1.0)
	_source("portal_door", pa + Vector3(-8, 1.7, 0))
	# Milestone 111: far to the east, a hall 20 x 20 x 8 m 10 m from the listener with a doorway 2 x 3 m facing it, a
	# closet 4 x 4 x 3 m 25 m to the south (closed), a source in each and one in the open 6 m west.
	var ra := ROOMS_AT
	var hall := ra + Vector3(20, 0, 0)
	_box(hall + Vector3(-10, 5.5, 0), Vector3(0.3, 5, 20))
	_box(hall + Vector3(-10, 1.5, -5.5), Vector3(0.3, 3, 9))
	_box(hall + Vector3(-10, 1.5, 5.5), Vector3(0.3, 3, 9))
	_box(hall + Vector3(10, 4, 0), Vector3(0.3, 8, 20))
	_box(hall + Vector3(0, 4, -10), Vector3(20.3, 8, 0.3))
	_box(hall + Vector3(0, 4, 10), Vector3(20.3, 8, 0.3))
	_box(hall + Vector3(0, 8.15, 0), Vector3(20.3, 0.3, 20.3))
	_source("hall", hall + Vector3(0, 1.7, 0))
	var closet := ra + Vector3(0, 0, 25)
	_box(closet + Vector3(0, 1.5, -2), Vector3(4.3, 3, 0.3))
	_box(closet + Vector3(0, 1.5, 2), Vector3(4.3, 3, 0.3))
	_box(closet + Vector3(-2, 1.5, 0), Vector3(0.3, 3, 4))
	_box(closet + Vector3(2, 1.5, 0), Vector3(0.3, 3, 4))
	_box(closet + Vector3(0, 3.15, 0), Vector3(4.3, 0.3, 4.3))
	_source("closet", closet + Vector3(0, 1.5, 0))
	_source("open3", ra + Vector3(-6, 1.7, 0))
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
	for n in ["open", "wall", "edge", "car", "glass", "curtain"]:
		listen_plan.append({"source": n, "fellow": true})
		listen_plan.append({"source": n, "fellow": false})
	# Milestone 99: the source behind the north wall heard through its doorway, without the portals, and stock (its
	# stereo balance: the doorway is to the right).
	listen_plan.append({"source": "portal_open", "fellow": true, "at": PORTALS_AT})
	listen_plan.append({"source": "portal_open", "fellow": true, "at": PORTALS_AT, "no_portals": true})
	listen_plan.append({"source": "portal_open", "fellow": false, "at": PORTALS_AT})
	# A source in the closed room with the listener: the reverb's send (with the node) and its tail after it stops.
	listen_plan.append({"source": "inroom", "fellow": true, "room": true})
	listen_plan.append({"source": "inroom", "fellow": false, "room": true})
	# Milestone 111: the source in the hall heard from the open by its doorway, with source_rooms 2 (the hall's reverb)
	# and 0 (the listener's: the open's, nothing).
	listen_plan.append({"source": "hall", "fellow": true, "room": true, "at": ROOMS_AT, "rooms": 2})
	listen_plan.append({"source": "hall", "fellow": true, "room": true, "at": ROOMS_AT, "rooms": 0, "no_rooms": true})


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
			acc.l = acc.get("l", 0.0) + f.x * f.x
			acc.r = acc.get("r", 0.0) + f.y * f.y
	listen_t += dt
	if listen_step >= 0 and listen_t >= 1.5 and listen_t - dt < 1.5:
		# The direct sound measured; in a room the source stops and what is heard after it is its reverb's tail.
		sources[step.source].stop()
	if listen_step < 0 or listen_t >= length:
		if listen_step >= 0:
			var rms := sqrt(listen_acc.sum / maxf(1.0, listen_acc.n))
			var tag := ("no_portals" if step.get("no_portals", false) else ("no_rooms" if step.get("no_rooms", false) else "fellow")) if step.fellow else "stock"
			_measure("listen_" + step.source, tag + "_db", snappedf(linear_to_db(maxf(rms, 1e-9)), 0.01))
			if step.has("at"):
				# Milestone 99: the right channel over the left (dB).
				_measure("listen_" + step.source, tag + "_balance_db", snappedf(10.0 * log(maxf(listen_acc.r, 1e-12) / maxf(listen_acc.l, 1e-12)) / log(10.0), 0.01))
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
		acoustics.portals = not next.get("no_portals", false)
		acoustics.source_rooms = next.get("rooms", 0)
		cam.position = Vector3(300, 1.7, 0) if next.get("room", false) else Vector3(0, 1.7, 0)
		if next.has("at"):
			cam.position = next.at + Vector3(0, 1.7, 0)
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
		for n in ["open", "wall", "edge", "car", "glass", "glass2", "glass_wall", "curtain"]:
			var p: AudioStreamPlayer3D = sources[n]
			_measure(n, "occlusion", snappedf(acoustics.get_occlusion(p), 0.001))
			_measure(n, "occlusion_highs", snappedf(acoustics.get_occlusion_highs(p), 0.001))
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
		cam.position = PORTALS_AT + Vector3(0, 1.7, 0)
	if frame == 1500 or frame == 1680:
		# Milestone 99: through the openings; then the door taken away and the closed portal opened half.
		var tag := "" if frame == 1500 else "_after"
		for n in ["portal_open", "portal_closed", "portal_door"]:
			var p: AudioStreamPlayer3D = sources[n]
			var heard := acoustics.get_heard_position(p) - cam.position
			_measure(n + tag, "occlusion", snappedf(acoustics.get_occlusion(p), 0.001))
			_measure(n + tag, "occlusion_highs", snappedf(acoustics.get_occlusion_highs(p), 0.001))
			_measure(n + tag, "portal_weight", snappedf(acoustics.get_portal_weight(p), 0.001))
			_measure(n + tag, "heard_from", [snappedf(heard.x, 0.01), snappedf(heard.y, 0.01), snappedf(heard.z, 0.01)])
			_measure(n + tag, "heard_distance", snappedf(heard.length(), 0.01))
		if frame == 1500:
			_measure("portal_stats", "stats", acoustics.get_stats())
			door.queue_free()
			south_portal.transmission = 0.5
	if frame == 1680:
		# Milestone 111: the sources' rooms, the listener in the open by the hall.
		acoustics.source_rooms = 2
		cam.position = ROOMS_AT + Vector3(0, 1.7, 0)
	if frame == 1800 or frame == 1920 or frame == 2010:
		var tag: String = {1800: "_open", 1920: "_hall", 2010: "_off"}[frame]
		for n in ["hall", "closet", "open3"]:
			var sr: Dictionary = acoustics.get_source_room(sources[n])
			for k in ["bus", "enclosure", "mean_distance", "rt60", "bus_rt60", "bus_room_size", "send"]:
				var v = sr.get(k)
				_measure("rooms_" + n + tag, k, snappedf(v, 0.001) if v is float else String(v))
		_measure("rooms_stats" + tag, "stats", acoustics.get_stats())
		_measure("rooms_stats" + tag, "room_bus_present", AudioServer.get_bus_index(&"FellowReverbRoom1") >= 0)
		if frame == 1800:
			cam.position = ROOMS_AT + Vector3(16, 1.7, 3)  # in the hall
		elif frame == 1920:
			acoustics.source_rooms = 0
		else:
			done = true
			_finish.call_deferred()


func _build_many() -> void:
	# A grid of walls 6 m apart, N sources among them within 60 m.
	for i in range(-5, 6):
		for j in range(-5, 6):
			if (i + j) % 2 == 0:
				_box(Vector3(i * 6.0 + 3.0, 2, j * 6.0), Vector3(0.3, 4, 4))
				if _arg("portals", "0") == "1":
					# Milestone 99: an opening beside each wall (in its plane, past its end): paths round it.
					_portal(Vector3(i * 6.0 + 3.0, 1.1, j * 6.0 + 2.6), 90.0, 1.0)
	var rng := RandomNumberGenerator.new()
	rng.seed = 3
	for k in many:
		var p := _source("s%d" % k, Vector3(rng.randf_range(-30, 30), 1.0, rng.randf_range(-30, 30)))
		p.play()
	acoustics = FellowAcoustics3D.new()
	acoustics.source_rooms = int(_arg("rooms", "0"))  # milestone 111: --rooms=N
	root3d.add_child(acoustics)
	physics_frame.connect(_on_frame)


var many_usec: PackedInt64Array = []
var many_rays: PackedInt64Array = []
var many_portal_paths := 0


func _many_tick() -> void:
	cam.position = Vector3(sin(frame * 0.01) * 20.0, 1.7, cos(frame * 0.013) * 20.0)
	if frame < 30:
		return
	var st := acoustics.get_stats()
	many_usec.append(st.usec)
	many_rays.append(st.rays)
	many_portal_paths += st.portal_paths
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
			"portal_paths_per_frame": snappedf(float(many_portal_paths) / many_usec.size(), 0.01),
			"source_rooms": acoustics.source_rooms,
			"physics_engine": ProjectSettings.get_setting("physics/3d/physics_engine")}
		print("ACOUSTICS_MANY " + JSON.stringify(res))
		var f := FileAccess.open(out_dir.path_join("acoustics_many.json"), FileAccess.WRITE)
		f.store_string(JSON.stringify(res, "  "))
		f.close()
		_quit_quietly()


func _finish() -> void:
	await _stop_sounds()
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
		# Milestone 90: through the glass, less than through the wall (its 0.3: -3.6 dB and the shelf -5.4 dB).
		var gl: Dictionary = data["listen_glass"]
		expect.call("listen_glass", gl.fellow_db < gl.stock_db - 3.0 and gl.fellow_db > gl.stock_db - 9.0 and gl.fellow_high_share > w.fellow_high_share)
		# Milestone 92: through the curtain, the highs cut more than through the glass, the level less.
		var cu: Dictionary = data["listen_curtain"]
		expect.call("listen_curtain", cu.fellow_high_share < gl.fellow_high_share * 0.6 and cu.fellow_db < cu.stock_db - 2.0)
		# Milestone 99: through the doorway louder and brighter than through the wall (the portals off), from the right;
		# stock centred.
		var lp: Dictionary = data["listen_portal_open"]
		expect.call("listen_portal_open", lp.fellow_db > lp.no_portals_db + 4.0 and lp.fellow_high_share > lp.no_portals_high_share * 2.0 and lp.fellow_balance_db > 2.0 and absf(lp.stock_balance_db) < 1.0 and absf(lp.no_portals_balance_db) < 1.0)
		# Milestone 111: the hall heard from the open — its reverb's tail with source_rooms (within 40 dB of the direct
		# sound), more than 6 dB weaker without (only the listener's reverb: the open by a building, a little closed).
		var lh: Dictionary = data["listen_hall"]
		expect.call("listen_hall", lh.fellow_tail_db > lh.fellow_db - 40.0 and lh.no_rooms_tail_db < lh.fellow_tail_db - 6.0)
		var e: Dictionary = data["listen_edge"]
		expect.call("listen_edge", e.fellow_db < e.stock_db - 1.0 and e.fellow_db > w.fellow_db - 0.5 + (w.stock_db - e.stock_db))
	else:
		expect.call("open", data.open.occlusion < 0.01)
		expect.call("wall", data.wall.occlusion > 0.99)
		expect.call("edge", data.edge.occlusion > 0.1 and data.edge.occlusion < 0.9)
		expect.call("car", data.car.occlusion < 0.01)
		expect.call("glass", absf(data.glass.occlusion - 0.3) < 0.01)
		expect.call("glass2", absf(data.glass2.occlusion - 0.6) < 0.01)
		expect.call("glass_wall", data.glass_wall.occlusion > 0.99)
		expect.call("curtain", absf(data.curtain.occlusion - 0.1) < 0.01 and absf(data.curtain.occlusion_highs - 0.8) < 0.01 and absf(data.glass.occlusion_highs - 0.3) < 0.01 and data.wall.occlusion_highs > 0.99)
		expect.call("moving", data.moving.max_occlusion > 0.9 and data.moving.max_step_db < 2.0)
		var ro: Dictionary = data.room_open.room
		var rc: Dictionary = data.room_closed.room
		var rr: Dictionary = data.room_roof.room
		expect.call("room_open", ro.enclosure < 0.05 and ro.send < 0.03)
		expect.call("room_closed", rc.enclosure > 0.95 and rc.rt60 > 0.2 and rc.rt60 < 2.0 and rc.predelay_msec > 5.0 and rc.predelay_msec < 40.0)
		expect.call("room_roof", rr.enclosure > 0.1 and rr.enclosure < 0.8)
		# Milestone 99: the open doorway's path (a bend of 64 degrees: -4.3 dB, its occlusion 0.36; 9.43 m long, heard
		# from the doorway's side, 5 m to the right); the closed and the shut ones as walls; then the door away (the
		# same path as the north one, mirrored) and the half-open portal (-6 dB more: 0.86).
		var po: Dictionary = data.portal_open
		expect.call("portal_open", po.portal_weight > 0.95 and absf(po.occlusion - 0.356) < 0.03 and absf(po.occlusion_highs - 0.356) < 0.03 and po.heard_from[0] > 4.5 and absf(po.heard_distance - 9.43) < 0.1)
		expect.call("portal_closed", data.portal_closed.occlusion > 0.99 and data.portal_closed.portal_weight < 0.01)
		expect.call("portal_door", data.portal_door.occlusion > 0.99 and data.portal_door.portal_weight < 0.01)
		var pd: Dictionary = data.portal_door_after
		expect.call("portal_door_after", pd.portal_weight > 0.95 and absf(pd.occlusion - 0.356) < 0.03 and pd.heard_from[2] < -4.5)
		var ph: Dictionary = data.portal_closed_after
		expect.call("portal_closed_after", ph.portal_weight > 0.95 and absf(ph.occlusion - 0.857) < 0.03)
		expect.call("portal_open_after", absf(data.portal_open_after.occlusion - po.occlusion) < 0.005)
		# Milestone 111: the listener in the open — the hall's source sent to a room reverb (a closed room's decay, RT60
		# 0.8-2.5 s, sent more than 0.3), the closet's to another (a short decay: under 0.6 s, a smaller room size), the
		# open one's to the listener's; the listener in the hall — the hall's source the listener's, the closet's still
		# its own, the open one's the listener's (in the open a source has no room of its own); source_rooms 0 —
		# every source the listener's, the room buses removed.
		var ho: Dictionary = data.rooms_hall_open
		var co: Dictionary = data.rooms_closet_open
		var oo: Dictionary = data.rooms_open3_open
		expect.call("rooms_open", ho.bus.begins_with("FellowReverbRoom") and co.bus.begins_with("FellowReverbRoom") and ho.bus != co.bus and oo.bus == "FellowReverb" and ho.bus_rt60 > 0.8 and ho.bus_rt60 < 2.5 and ho.send > 0.3 and co.bus_rt60 < 0.6 and co.bus_room_size < ho.bus_room_size and ho.enclosure > 0.8 and co.enclosure > 0.95)
		var hh: Dictionary = data.rooms_hall_hall
		var ch: Dictionary = data.rooms_closet_hall
		var oh: Dictionary = data.rooms_open3_hall
		expect.call("rooms_hall", hh.bus == "FellowReverb" and ch.bus.begins_with("FellowReverbRoom") and ch.bus_rt60 < 0.6 and oh.bus == "FellowReverb")
		var off_ok: bool = not data.rooms_stats_off.room_bus_present and data.rooms_stats_open.room_bus_present
		for n in ["hall", "closet", "open3"]:
			off_ok = off_ok and data["rooms_" + n + "_off"].bus == "FellowReverb"
		expect.call("rooms_off", off_ok)
	result["lanes"] = data
	result["failures"] = failures
	result["global"] = FellowAcoustics3D.get_global_stats()
	result["physics_engine"] = ProjectSettings.get_setting("physics/3d/physics_engine")
	print("ACOUSTICS_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("acoustics_listen.json" if listen else "acoustics_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)


# The sounds stopped and their playbacks released by the mixer before quitting (a player still playing when the game
# quits keeps its playback: stock's leak warning).
func _stop_sounds() -> void:
	for n in sources:
		sources[n].stop()
	for i in 6:
		await process_frame
		OS.delay_msec(20)


func _quit_quietly() -> void:
	await _stop_sounds()
	quit(0)
