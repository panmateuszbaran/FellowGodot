extends SceneTree
## FellowEngine milestone 79 check (decision 0187): FellowVehicleEffects3D — skid marks, tyre smoke, engine and tyre
## sounds.
##
##   godot --path benchmarks/project --headless --fixed-fps 60 -s res://bench/vehicle_fx_check.gd -- --out=<dir>
##   godot --path benchmarks/project --fixed-fps 60 --resolution 64x64 --write-movie <dir>/m.png
##       -s res://bench/vehicle_fx_check.gd -- --out=<dir> --listen=1          (the sound measured, Movie Maker's audio)
##   godot --path benchmarks/project --fixed-fps 60 --resolution 1280x720 -s res://bench/vehicle_fx_check.gd
##       -- --out=<dir> --pictures=1
##   godot ... -s res://bench/vehicle_fx_check.gd -- --out=<dir> --many=32 [--seconds=6]
##
## Lanes, each a car (as bench/vehicle_check.gd's) with a FellowVehicleEffects3D: cruising straight at 50 km/h (no
## marks, no smoke), a handbrake turn at 50 km/h, a burnout (the traction control off, full throttle from rest), a
## stop from 80 km/h with the wheels locked (the anti-lock brakes off) and one with them (fewer marks). One JSON line:
## VEHICLE_FX_CHECK {...}; exit code 1 when a lane did otherwise than expected. --listen=1: one car, the master bus
## captured — the engine's tone at its firing frequency at idle and at 70 km/h, the tyres' squeal in a slide. --many=N: N cars
## sliding in circles, the effects' time a frame. One JSON line: VEHICLE_FX_MANY {...}.

const RUN_SECONDS := 14.0

var args := {}
var out_dir := ""
var result := {}
var root3d: Node3D
var lanes: Array = []
var frame := 0
var listen := false
var pictures := false
var many := 0
var many_seconds := 6.0
var done := false
var cam: Camera3D
var capture: AudioEffectCapture
var shots := {}


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://vehicle_fx_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	listen = _arg("listen", "0") == "1"
	pictures = _arg("pictures", "0") == "1"
	many = int(_arg("many", "0"))
	many_seconds = float(_arg("seconds", "6"))
	_build.call_deferred()


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.8
	return m


func _box(pos: Vector3, size: Vector3, color := Color(0.6, 0.6, 0.58)) -> StaticBody3D:
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
	mi.material_override = _material(color)
	b.add_child(mi)
	root3d.add_child(b)
	return b


func _car(pos: Vector3, color: Color) -> FellowVehicle3D:
	var car := FellowVehicle3D.new()
	car.center_of_mass_mode = RigidBody3D.CENTER_OF_MASS_MODE_CUSTOM
	car.center_of_mass = Vector3(0, 0.45, 0.1)
	car.position = pos
	var cs := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = Vector3(1.8, 0.55, 4.2)
	cs.shape = shape
	cs.position = Vector3(0, 0.62, 0)
	car.add_child(cs)
	var body := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = shape.size
	body.mesh = bm
	body.material_override = _material(color)
	body.position = cs.position
	car.add_child(body)
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
			w.position = Vector3(x, 0.34, z)
			w.steering = z < 0.0
			w.driven = z > 0.0
			w.handbrake = z > 0.0
			var wm := MeshInstance3D.new()
			var cyl := CylinderMesh.new()
			cyl.top_radius = 0.34
			cyl.bottom_radius = 0.34
			cyl.height = 0.24
			wm.mesh = cyl
			wm.material_override = tyre
			wm.rotation_degrees = Vector3(0, 0, 90)
			w.add_child(wm)
			car.add_child(w)
	var fx := FellowVehicleEffects3D.new()
	fx.name = "Effects"
	car.add_child(fx)
	root3d.add_child(car)
	return car


func _fx(car: FellowVehicle3D) -> FellowVehicleEffects3D:
	return car.get_node("Effects")


func _lane(name: String, car: FellowVehicle3D) -> Dictionary:
	var lane := {"name": name, "car": car, "t": 0.0, "phase": 0, "data": {}}
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
	_box(Vector3(0, -0.5, -400), Vector3(2000, 1, 2000), Color(0.42, 0.42, 0.4))
	cam = Camera3D.new()
	root3d.add_child(cam)
	cam.current = true
	cam.look_at_from_position(Vector3(0, 40, 40), Vector3(0, 0, -40))
	if many > 0:
		var cols := int(ceil(sqrt(float(many))))
		for i in many:
			var c := _car(Vector3(float(i % cols) * 50.0, 0.1, -float(i / cols) * 50.0), Color.from_hsv(float(i) / many, 0.6, 0.8))
			c.throttle = 0.8
			c.steering = 0.6
			c.traction_control = false
			lanes.append({"name": "many", "car": c})
		physics_frame.connect(_on_frame)
		return
	if listen:
		_lane("listen", _car(Vector3(0, 0.1, 0), Color(0.8, 0.2, 0.15)))
		capture = AudioEffectCapture.new()
		capture.buffer_length = 2.0
		AudioServer.add_bus_effect(0, capture)
		physics_frame.connect(_on_frame)
		return
	_lane("cruise", _car(Vector3(0, 0.1, 0), Color(0.3, 0.5, 0.8)))
	_lane("handbrake", _car(Vector3(15, 0.1, 0), Color(0.9, 0.5, 0.2)))
	var burn := _car(Vector3(30, 0.1, 0), Color(0.8, 0.2, 0.15))
	burn.traction_control = false
	_lane("burnout", burn)
	var locked := _car(Vector3(45, 0.1, 0), Color(0.2, 0.7, 0.3))
	locked.anti_lock_brakes = false
	_lane("locked", locked)
	_lane("abs", _car(Vector3(60, 0.1, 0), Color(0.6, 0.3, 0.7)))
	if pictures:
		cam.look_at_from_position(Vector3(30, 26, 22), Vector3(30, 0, -50))
	physics_frame.connect(_on_frame)


func _cruise(car: FellowVehicle3D, kmh: float) -> void:
	var err := kmh - car.get_speed_kmh()
	car.throttle = clampf(err * 0.15, 0.0, 1.0)
	car.brake = clampf(-err * 0.1, 0.0, 1.0)


func _drive(lane: Dictionary, dt: float) -> void:
	var car: FellowVehicle3D = lane.car
	lane.t += dt
	var t: float = lane.t
	var d: Dictionary = lane.data
	var st := _fx(car).get_stats()
	d["max_smoking"] = maxi(d.get("max_smoking", 0), st.smoking)
	match lane.name:
		"cruise":
			_cruise(car, 50.0)
		"handbrake":
			if t < 6.0:
				_cruise(car, 50.0)
			else:
				car.throttle = 0.0
				car.steering = 0.6
				car.handbrake = t < 7.5
				if t > 6.0 and t < 9.0:
					d["smoking_frames"] = d.get("smoking_frames", 0) + (1 if st.smoking > 0 else 0)
		"burnout":
			car.throttle = 1.0 if t < 3.0 else 0.0
			car.brake = 0.0 if t < 3.0 else 1.0
			if t < 3.0:
				d["smoking_frames"] = d.get("smoking_frames", 0) + (1 if st.smoking > 0 else 0)
		"locked", "abs":
			if lane.phase == 0:
				car.throttle = 1.0
				if car.get_speed_kmh() >= 80.0:
					lane.phase = 1
					d["before"] = st.segments_laid
			elif lane.phase == 1:
				car.throttle = 0.0
				car.brake = 1.0
				if car.get_speed_kmh() < 1.0:
					lane.phase = 2
					d["while_braking"] = st.segments_laid - d.before
	d["segments"] = st.segments_laid
	d["mark_low"] = snappedf(st.mark_low, 0.001)
	d["mark_high"] = snappedf(st.mark_high, 0.001)


var listen_windows := []
var listen_t := 0.0
var listen_buf: PackedFloat32Array = []
var listen_rpm := 0.0
var listen_rpm_n := 0


# The power of one frequency in the samples (Goertzel's algorithm).
func _goertzel(x: PackedFloat32Array, f: float, rate: float) -> float:
	var w := TAU * f / rate
	var c := 2.0 * cos(w)
	var s1 := 0.0
	var s2 := 0.0
	for v in x:
		var s := v + c * s1 - s2
		s2 = s1
		s1 = s
	return (s1 * s1 + s2 * s2 - c * s1 * s2) / float(x.size() * x.size())


# The share of the energy within a band (by two one-pole filters).
func _band_share(x: PackedFloat32Array, lo: float, hi: float, rate: float) -> float:
	var a_lo := 1.0 - exp(-TAU * lo / rate)
	var a_hi := 1.0 - exp(-TAU * hi / rate)
	var l1 := 0.0
	var l2 := 0.0
	var band := 0.0
	var total := 0.0
	for v in x:
		l1 += (v - l1) * a_hi
		l2 += (v - l2) * a_lo
		var b := l1 - l2
		band += b * b
		total += v * v
	return band / maxf(total, 1e-12)


func _listen_tick(lane: Dictionary, dt: float) -> void:
	var car: FellowVehicle3D = lane.car
	lane.t += dt
	var t: float = lane.t
	cam.look_at_from_position(car.global_position + Vector3(6, 2, 4), car.global_position + Vector3(0, 0.6, 0))
	var avail := capture.get_frames_available()
	var buf := capture.get_buffer(avail)
	# 0-3 s standing (the handbrake on: the engine at idle), 3-11 s at 70 km/h, then a handbrake slide; each measured
	# over its last 1-1.5 s.
	var windows := [["idle", 1.5, 3.0], ["fast", 9.5, 11.0], ["slide", 12.0, 13.0]]
	if t < 3.0:
		car.handbrake = true
	elif t < 11.0:
		car.handbrake = false
		_cruise(car, 70.0)
	else:
		car.throttle = 0.0
		car.steering = 0.6
		car.handbrake = t < 13.0
	for w in windows:
		if t > w[1] and t <= w[2]:
			for f in buf:
				listen_buf.append((f.x + f.y) * 0.5)
			listen_rpm += car.get_engine_rpm()
			listen_rpm_n += 1
		elif t > w[2] and t - dt <= w[2]:
			var rate := AudioServer.get_mix_rate()
			var rpm := listen_rpm / maxf(1.0, listen_rpm_n)
			var f0 := rpm / 60.0 * 4.0 * 0.5
			var p0 := _goertzel(listen_buf, f0, rate)
			var off := (_goertzel(listen_buf, f0 * 0.8, rate) + _goertzel(listen_buf, f0 * 1.25, rate)) * 0.5
			var rms := 0.0
			for v in listen_buf:
				rms += v * v
			rms = sqrt(rms / maxf(1.0, listen_buf.size()))
			lane.data[w[0]] = {"rpm": snappedf(rpm, 1.0), "firing_hz": snappedf(f0, 0.1), "tone_over_off_db": snappedf(10.0 * log(p0 / maxf(off, 1e-20)) / log(10.0), 0.1),
				"level_db": snappedf(linear_to_db(maxf(rms, 1e-9)), 0.01), "squeal_share": snappedf(_band_share(listen_buf, 700.0, 1700.0, rate), 0.0001)}
			listen_buf = PackedFloat32Array()
			listen_rpm = 0.0
			listen_rpm_n = 0
	if t > 13.2:
		done = true
		_finish.call_deferred()


func _on_frame() -> void:
	if done:
		return
	frame += 1
	var dt := 1.0 / 60.0
	if many > 0:
		_many_tick()
		return
	if listen:
		_listen_tick(lanes[0], dt)
		return
	for lane in lanes:
		_drive(lane, dt)
	if pictures and frame % 60 == 0 and frame / 60 in [4, 7, 9, 13]:
		shots[frame / 60] = get_root().get_texture().get_image()
	if frame >= int(RUN_SECONDS * 60.0):
		done = true
		_finish.call_deferred()


var many_usec: PackedInt64Array = []
var many_synth: PackedInt64Array = []
var many_last := {}


func _many_tick() -> void:
	if frame == 60:
		FellowVehicleEffects3D.reset_global_stats()
	if frame < 60:
		return
	if frame >= 60 + int(many_seconds * 60.0):
		done = true
		var st := FellowVehicleEffects3D.get_global_stats()
		var n := float(frame - 60)
		var segs := 0
		for l in lanes:
			segs += _fx(l.car).get_stats().mark_segments
		var res := {"cars": many, "frames": int(n), "stats": st,
			"effects_ms_per_frame": snappedf(float(st.usec) / n / 1000.0, 0.001),
			"synth_ms_per_frame": snappedf(float(st.synth_usec) / n / 1000.0, 0.001),
			"mark_segments": segs, "physics_engine": ProjectSettings.get_setting("physics/3d/physics_engine")}
		print("VEHICLE_FX_MANY " + JSON.stringify(res))
		var f := FileAccess.open(out_dir.path_join("vehicle_fx_many.json"), FileAccess.WRITE)
		f.store_string(JSON.stringify(res, "  "))
		f.close()
		await _stop_sounds()
		quit(0)


func _finish() -> void:
	await _stop_sounds()
	var failures := []
	var out := {}
	for lane in lanes:
		var d: Dictionary = lane.data
		var ok := true
		match lane.name:
			"cruise":
				ok = d.get("segments", 1) == 0 and d.get("max_smoking", 1) == 0
			"handbrake":
				ok = d.get("segments", 0) > 10 and d.get("smoking_frames", 0) > 30 and absf(d.get("mark_low", 1.0) - 0.02) < 0.03 and d.get("mark_high", 1.0) < 0.06
			"burnout":
				ok = d.get("segments", 0) > 5 and d.get("smoking_frames", 0) > 30
			"locked":
				ok = d.get("while_braking", 0) > 20
			"abs":
				ok = true # compared with "locked" below
			"listen":
				ok = d.has("idle") and d.has("fast") and d.has("slide") and d.idle.tone_over_off_db > 6.0 and d.fast.tone_over_off_db > 6.0 and d.fast.level_db > d.idle.level_db and d.slide.squeal_share > d.fast.squeal_share * 2.0
		d["ok"] = ok
		if not ok:
			failures.append(lane.name)
		out[lane.name] = d
	if out.has("abs") and out.has("locked"):
		# With the anti-lock brakes, fewer marks than with the wheels locked.
		var fewer: bool = out.abs.get("while_braking", 0) < out.locked.get("while_braking", 0)
		out.abs["ok"] = fewer
		if not fewer:
			failures.append("abs")
	result["lanes"] = out
	result["failures"] = failures
	result["global"] = FellowVehicleEffects3D.get_global_stats()
	result["physics_engine"] = ProjectSettings.get_setting("physics/3d/physics_engine")
	if pictures:
		var keys := shots.keys()
		keys.sort()
		if not keys.is_empty():
			var w: int = shots[keys[0]].get_width()
			var h: int = shots[keys[0]].get_height()
			var strip := Image.create(w * 2, h * 2, false, Image.FORMAT_RGBA8)
			for i in keys.size():
				var img: Image = shots[keys[i]]
				img.convert(Image.FORMAT_RGBA8)
				strip.blit_rect(img, Rect2i(0, 0, w, h), Vector2i((i % 2) * w, (i / 2) * h))
			strip.save_png(out_dir.path_join("vehicle_fx_strip.png"))
	print("VEHICLE_FX_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("vehicle_fx_listen.json" if listen else "vehicle_fx_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)


# The cars' sounds stopped and their playbacks released by the mixer before quitting (a player still playing when the
# game quits keeps its playback: stock's leak warning).
func _stop_sounds() -> void:
	for lane in lanes:
		_fx(lane.car).sound = false
	for i in 6:
		await process_frame
		OS.delay_msec(20)
