extends SceneTree
## FellowEngine milestone 82 check (decision 0190): FellowAudioDirector, FellowSoundEvent and FellowMixSnapshot —
## sounds played as a game asks for them, within a budget of voices, and the mix's states.
##
##   godot --path benchmarks/project --fixed-fps 60 --resolution 64x64 --write-movie <dir>/m.png
##       -s res://bench/audio_director_check.gd -- --out=<dir>          (Movie Maker: the audio mixed frame by frame)
##   godot --path benchmarks/project --headless --fixed-fps 60 -s res://bench/audio_director_check.gd -- --out=<dir>
##       --many=1 [--seconds=5]
##
## Lanes, each with a director of its own, the sounds tones made here (the listener the camera at the origin; all but
## the duck's on a muted bus "Sfx"): an event's four variations played 40 times without repeating one, its level and
## pitch varied within its ranges, and a sequence; an event limited to 3 instances asked for 5; an event with a 0.1 s
## cooldown asked for every frame for a second; an event that carries 50 m played at 30 and at 80 m; a budget of 8
## voices — 8 quiet loops, then 4 more important ones (the 4 quietest give their voices), 4 quieter ones (refused), a
## loud one of the first kind (the quietest of its kind gives its voice); a snapshot pushed and popped (the bus's level
## faded to -20 dB and back to where it was); a duck — a snapshot active while a line plays on the bus "Dialogue",
## the music's tone measured on the master bus before, during and after it (Goertzel's power at its frequency); a
## voice following a moving node and going on where it was when the node is freed. Milestone 85 (decision 0193):
## a virtual voice (a loop losing its voice to more important ones, resumed where it would be), a loop coming within
## its reach and leaving it, a one-shot ending unheard, the oldest instance giving way. One JSON line:
## AUDIO_DIRECTOR_CHECK {...}; exit code 1 when a lane did otherwise than expected. --many=1: 64 voices, 50 requests a
## frame at random places, the director's time (--virtual=1: the events' sounds kept as virtual voices). One JSON
## line: AUDIO_DIRECTOR_MANY {...}.

const RATE := 44100

var args := {}
var out_dir := ""
var result := {}
var root3d: Node3D
var frame := 0
var lanes := {}
var capture: AudioEffectCapture
var captured := PackedVector2Array()
var capture_start_frame := -1
var done := false
var many := false
var many_seconds := 5.0
var mover: Node3D
var mover2: Node3D


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://audio_director_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	many = _arg("many", "0") == "1"
	many_seconds = float(_arg("seconds", "5"))
	_build.call_deferred()


# A tone: a sine at p_freq Hz, p_seconds long (10 ms fades at its ends), looped when p_loop.
func _tone(p_freq: float, p_seconds: float, p_loop := false) -> AudioStreamWAV:
	var n := int(p_seconds * RATE)
	var data := PackedByteArray()
	data.resize(n * 2)
	var fade := int(0.01 * RATE)
	for i in n:
		var g := 1.0
		if not p_loop:
			g = minf(1.0, minf(float(i) / fade, float(n - 1 - i) / fade))
		var s := int(clampf(sin(TAU * p_freq * i / RATE) * 0.5 * g, -1.0, 1.0) * 32767.0)
		data.encode_s16(i * 2, s)
	var w := AudioStreamWAV.new()
	w.format = AudioStreamWAV.FORMAT_16_BITS
	w.mix_rate = RATE
	w.stereo = false
	w.data = data
	if p_loop:
		w.loop_mode = AudioStreamWAV.LOOP_FORWARD
		w.loop_begin = 0
		w.loop_end = n
	return w


func _event(streams: Array, bus := "Sfx", spatial := false) -> FellowSoundEvent:
	var e := FellowSoundEvent.new()
	var a: Array[AudioStream] = []
	for s in streams:
		a.append(s)
	e.streams = a
	e.bus = bus
	e.spatial = spatial
	return e


func _director(name: String, voices := 32) -> FellowAudioDirector:
	var d := FellowAudioDirector.new()
	d.name = name
	d.max_voices = voices
	d.seed = 7
	root3d.add_child(d)
	return d


func _add_bus(name: String, muted := false) -> void:
	if AudioServer.get_bus_index(name) >= 0:
		return
	AudioServer.add_bus()
	var i := AudioServer.bus_count - 1
	AudioServer.set_bus_name(i, name)
	AudioServer.set_bus_send(i, "Master")
	AudioServer.set_bus_mute(i, muted)


func _build() -> void:
	root3d = Node3D.new()
	get_root().add_child(root3d)
	var cam := Camera3D.new()
	root3d.add_child(cam)
	cam.current = true
	_add_bus("Sfx", true)
	_add_bus("Music")
	_add_bus("Dialogue")
	if many:
		_build_many()
		return
	capture = AudioEffectCapture.new()
	capture.buffer_length = 10.0
	AudioServer.add_bus_effect(0, capture)

	# 1. Variations: four tones, random without repeats, the level ±3 dB and the pitch ±2 semitones; and a sequence.
	var tones := [_tone(300, 0.1), _tone(400, 0.1), _tone(500, 0.1), _tone(600, 0.1)]
	var ev := _event(tones)
	ev.volume_db = -6.0
	ev.volume_random_db = 3.0
	ev.pitch_random_semitones = 2.0
	var es := _event(tones)
	es.selection = FellowSoundEvent.SELECTION_SEQUENCE
	lanes["variations"] = {"d": _director("Variations"), "random": ev, "sequence": es, "picked": [], "sequence_picked": [], "volumes": [], "pitches": []}
	# 2. Instances: at most 3 of a 1 s tone.
	var ei := _event([_tone(700, 1.0)])
	ei.max_instances = 3
	lanes["instances"] = {"d": _director("Instances"), "event": ei}
	# 3. Cooldown: 0.1 s.
	var ec := _event([_tone(800, 0.05)])
	ec.cooldown = 0.1
	lanes["cooldown"] = {"d": _director("Cooldown"), "event": ec}
	# 4. Distance: carries 50 m.
	var ed := _event([_tone(900, 0.5)], "Sfx", true)
	ed.max_distance = 50.0
	lanes["distance"] = {"d": _director("Distance"), "event": ed}
	# 5. The budget: 8 voices; quiet loops (priority 64), important ones (200).
	var loop := _tone(1000, 0.5, true)
	var low := _event([loop], "Sfx", true)
	low.priority = 64
	var high := _event([loop], "Sfx", true)
	high.priority = 200
	var bd := _director("Budget", 8)
	lanes["budget"] = {"d": bd, "low": low, "high": high, "finished": [], "ids": {}}
	bd.voice_finished.connect(func(voice: int, stolen: bool) -> void:
		lanes.budget.finished.append([voice, stolen]))
	# 6. A snapshot pushed and popped: the music bus (at -3 dB) to -20 dB more in 0.5 s and back in 0.5 s.
	var sp := FellowMixSnapshot.new()
	sp.bus_volumes = {&"Music": -20.0}
	sp.fade_in = 0.5
	sp.fade_out = 0.5
	lanes["snapshot"] = {"d": _director("Snapshot"), "snapshot": sp, "trace": {}}
	# 7. A duck: -12 dB on the music while a line plays on "Dialogue" (in 0.05 s, out in 0.4 s); the music a 220 Hz loop,
	# the line a 0.6 s tone at 1 kHz.
	var duck := FellowMixSnapshot.new()
	duck.bus_volumes = {&"Music": -12.0}
	duck.fade_in = 0.05
	duck.fade_out = 0.4
	duck.trigger_bus = &"Dialogue"
	var dd := _director("Duck")
	var snaps: Array[FellowMixSnapshot] = [duck]
	dd.snapshots = snaps
	var music := _event([_tone(220, 1.0, true)], "Music")
	music.volume_db = -6.0
	var line := _event([_tone(1000, 0.6)], "Dialogue")
	line.volume_db = -6.0
	lanes["duck"] = {"d": dd, "snapshot": duck, "music": music, "line": line, "trace": {}}
	# 8. Following a node moving at 5 m/s, then freed.
	mover = Node3D.new()
	root3d.add_child(mover)
	mover.position = Vector3(-5, 0, -10)
	var ef := _event([_tone(1100, 0.5, true)], "Sfx", true)
	lanes["follow"] = {"d": _director("Follow"), "event": ef, "max_error": 0.0}
	# Milestone 85 (decision 0193): virtual voices and the oldest instance giving way.
	# 9. Two voices: a quiet loop (virtual_voice) loses its voice to two important ones, goes on unheard, and is resumed
	# where it would be when they stop.
	var va := _event([_tone(1200, 1.0, true)], "Sfx", true)
	va.virtual_voice = true
	va.priority = 64
	var vb := _event([_tone(1300, 1.0, true)], "Sfx", true)
	vb.priority = 200
	lanes["virtual"] = {"d": _director("Virtual", 2), "a": va, "b": vb}
	# 10. A loop that carries 30 m (virtual_voice), on a node coming from 50 m to 10 m, then taken 60 m away.
	mover2 = Node3D.new()
	root3d.add_child(mover2)
	mover2.position = Vector3(3, 0, -50)
	var vc := _event([_tone(1400, 1.0, true)], "Sfx", true)
	vc.virtual_voice = true
	vc.max_distance = 30.0
	lanes["reach"] = {"d": _director("Reach"), "event": vc}
	# 11. A 0.5 s sound started 40 m away (it carries 20 m, virtual_voice): it ends unheard.
	var vd := _event([_tone(1500, 0.5)], "Sfx", true)
	vd.virtual_voice = true
	vd.max_distance = 20.0
	var lo := _director("Oneshot")
	lanes["oneshot"] = {"d": lo, "event": vd, "finished": []}
	lo.voice_finished.connect(func(voice: int, stolen: bool) -> void:
		lanes.oneshot.finished.append([voice, stolen, frame]))
	# 12. At most 2 instances, the oldest giving way (steal_oldest): played three times.
	var ve := _event([_tone(1600, 1.0)], "Sfx")
	ve.max_instances = 2
	ve.steal_oldest = true
	lanes["oldest"] = {"d": _director("Oldest"), "event": ve, "ids": []}
	physics_frame.connect(_on_frame)


func _index_of(streams: Array, s: AudioStream) -> int:
	for i in streams.size():
		if streams[i] == s:
			return i
	return -1


func _on_frame() -> void:
	if done:
		return
	frame += 1
	var t := frame / 60.0
	# 1. A variation every 3 frames (40), a sequence step every 3 frames (8).
	var lv: Dictionary = lanes.variations
	if frame >= 10 and frame < 130 and frame % 3 == 1:
		var id: int = lv.d.play(lv.random)
		var p: AudioStreamPlayer = lv.d.get_voice_player(id)
		lv.picked.append(_index_of(lv.random.streams, p.stream))
		lv.volumes.append(p.volume_db)
		lv.pitches.append(p.pitch_scale)
		if lv.sequence_picked.size() < 8:
			var sid: int = lv.d.play(lv.sequence)
			lv.sequence_picked.append(_index_of(lv.sequence.streams, (lv.d.get_voice_player(sid) as AudioStreamPlayer).stream))
	# 2. Five at once.
	var li: Dictionary = lanes.instances
	if frame == 10:
		var ids := []
		for k in 5:
			ids.append(li.d.play(li.event))
		li["ids"] = ids
		li["voices"] = li.d.get_event_voice_count(li.event)
	# 3. Every frame for a second.
	var lc: Dictionary = lanes.cooldown
	if frame >= 10 and frame < 70:
		lc.d.play(lc.event)
	# 4. At 30 and at 80 m.
	var ld: Dictionary = lanes.distance
	if frame == 10:
		ld["near"] = ld.d.play(ld.event, Vector3(0, 0, -30))
		ld["far"] = ld.d.play(ld.event, Vector3(0, 0, -80))
	# 5. The budget.
	var lb: Dictionary = lanes.budget
	if frame == 10:
		for k in 8:
			lb.ids["low_%d" % k] = lb.d.play(lb.low, Vector3(0, 0, -(20.0 + k)))
		lb["after_low"] = lb.d.get_voice_count()
	if frame == 20:
		for k in 4:
			lb.ids["high_%d" % k] = lb.d.play(lb.high, Vector3(0, 0, -30.0))
		lb["after_high"] = lb.d.get_voice_count()
	if frame == 30:
		var refused := 0
		for k in 4:
			refused += 1 if lb.d.play(lb.low, Vector3(0, 0, -40.0)) == 0 else 0
		lb["refused_far"] = refused
	if frame == 40:
		lb.ids["loud"] = lb.d.play(lb.low, Vector3(0, 0, -2.0))
	if frame == 50:
		var playing := []
		for name in lb.ids:
			if lb.d.is_playing(lb.ids[name]):
				playing.append(name)
		playing.sort()
		lb["playing"] = playing
		lb["stats"] = lb.d.get_stats()
		lb.d.stop_all(0.1)
	# 6. The snapshot: Music at -3 dB, pushed at 1 s, popped at 2 s.
	var ls: Dictionary = lanes.snapshot
	var music_bus := AudioServer.get_bus_index("Music")
	if frame == 30:
		AudioServer.set_bus_volume_db(music_bus, -3.0)
	if frame == 60:
		ls.d.push_snapshot(ls.snapshot)
	if frame == 120:
		ls.d.pop_snapshot(ls.snapshot)
	if frame in [60, 75, 90, 105, 120, 135, 150, 180]:
		ls.trace[str(frame)] = snappedf(AudioServer.get_bus_volume_db(music_bus), 0.01)
	# 7. The duck: the music from 3.5 s, the line at 4.5 s (0.6 s), the master bus captured from 3.5 s.
	var lk: Dictionary = lanes.duck
	if frame == 210:
		lk["music_id"] = lk.d.play(lk.music)
		capture.clear_buffer()
		capture_start_frame = frame
	if frame == 270:
		lk.d.play(lk.line)
	if frame >= 264 and frame <= 360 and frame % 3 == 0:
		lk.trace[str(frame)] = snappedf(AudioServer.get_bus_volume_db(music_bus), 0.01)
	if capture_start_frame >= 0:
		var n := capture.get_frames_available()
		if n > 0:
			captured.append_array(capture.get_buffer(n))
	# 8. Following.
	var lf: Dictionary = lanes.follow
	if frame == 10:
		lf["id"] = lf.d.play_on(lf.event, mover)
	if frame > 10 and frame <= 60:
		var p3: AudioStreamPlayer3D = lf.d.get_voice_player(lf.id)
		lf.max_error = maxf(lf.max_error, p3.global_position.distance_to(mover.global_position))
		if frame < 60:
			mover.position.x += 5.0 / 60.0
		else:
			# Freed (at the end of this physics step, before the director's next look): the sound stays where it was.
			lf["freed_at"] = mover.global_position
			mover.queue_free()
	if frame == 90:
		var p3: AudioStreamPlayer3D = lf.d.get_voice_player(lf.id)
		lf["after_free"] = p3.global_position.distance_to(lf.freed_at) if p3 else -1.0
		lf["still_playing"] = lf.d.is_playing(lf.id)
	# 9.
	var lvv: Dictionary = lanes.virtual
	if frame == 10:
		lvv["a_id"] = lvv.d.play(lvv.a, Vector3(0, 0, -10))
	if frame == 40:
		lvv["b_ids"] = [lvv.d.play(lvv.b, Vector3(0, 0, -5)), lvv.d.play(lvv.b, Vector3(1, 0, -5))]
	if frame == 60:
		lvv["virtual_60"] = lvv.d.is_virtual(lvv.a_id)
		lvv["playing_60"] = lvv.d.is_playing(lvv.a_id)
		lvv["voices_60"] = lvv.d.get_voice_count()
	if frame == 100:
		lvv.d.stop_event(lvv.b)
	if frame == 103:
		var p3: AudioStreamPlayer3D = lvv.d.get_voice_player(lvv.a_id)
		lvv["virtual_103"] = lvv.d.is_virtual(lvv.a_id)
		# Where it would be: started at frame 10's step, a 1 s loop.
		lvv["position_103"] = p3.get_playback_position() if p3 else -1.0
		lvv["expected_103"] = fmod((103 - 10) / 60.0, 1.0)
		lvv["stats"] = lvv.d.get_stats()
	# 10.
	var lr: Dictionary = lanes.reach
	if frame == 10:
		lr["id"] = lr.d.play_on(lr.event, mover2)
	if frame > 10 and frame <= 250:
		mover2.position.z = minf(-10.0, mover2.position.z + 10.0 / 60.0)
	if frame in [30, 120, 160, 250, 270]:
		lr[str(frame)] = lr.d.is_virtual(lr.id)
	if frame == 260:
		mover2.position.z = -60.0
	if frame == 270:
		lr["stats"] = lr.d.get_stats()
		lr["playing"] = lr.d.is_playing(lr.id)
	# 11.
	var lo: Dictionary = lanes.oneshot
	if frame == 10:
		lo["id"] = lo.d.play(lo.event, Vector3(0, 0, -40))
	if frame == 20:
		lo["virtual_20"] = lo.d.is_virtual(lo.id)
		lo["playing_20"] = lo.d.is_playing(lo.id)
	if frame == 50:
		lo["playing_50"] = lo.d.is_playing(lo.id)
		lo["stats"] = lo.d.get_stats()
	# 12.
	var lold: Dictionary = lanes.oldest
	if frame in [10, 20, 30]:
		lold.ids.append(lold.d.play(lold.event))
	if frame == 40:
		lold["playing"] = lold.ids.map(func(i): return lold.d.is_playing(i))
		lold["stats"] = lold.d.get_stats()
	if frame >= 400:
		done = true
		_finish()


# Goertzel's power of p_freq in the captured frames from p_t0 to p_t1 seconds after the capture started (dB).
func _tone_db(p_freq: float, p_t0: float, p_t1: float) -> float:
	var mix_rate := AudioServer.get_mix_rate()
	var a := int(p_t0 * mix_rate)
	var b := mini(int(p_t1 * mix_rate), captured.size())
	var w := TAU * p_freq / mix_rate
	var c := 2.0 * cos(w)
	var s1 := 0.0
	var s2 := 0.0
	for i in range(a, b):
		var x := (captured[i].x + captured[i].y) * 0.5
		var s0 := x + c * s1 - s2
		s2 = s1
		s1 = s0
	var power := s1 * s1 + s2 * s2 - c * s1 * s2
	var n := float(maxi(1, b - a))
	return 10.0 * log(maxf(power / (n * n), 1e-20)) / log(10.0)


func _finish() -> void:
	var failures := []
	var out := {}
	# 1.
	var lv: Dictionary = lanes.variations
	var repeats := 0
	for i in range(1, lv.picked.size()):
		repeats += 1 if lv.picked[i] == lv.picked[i - 1] else 0
	var used := {}
	for p in lv.picked:
		used[p] = true
	var vmin: float = lv.volumes.min()
	var vmax: float = lv.volumes.max()
	var pmin: float = lv.pitches.min()
	var pmax: float = lv.pitches.max()
	var semis := 12.0 * log(pmax / pmin) / log(2.0)
	var e1 := {"plays": lv.picked.size(), "repeats": repeats, "used": used.size(), "volume_db": [snappedf(vmin, 0.01), snappedf(vmax, 0.01)],
		"pitch": [snappedf(pmin, 0.001), snappedf(pmax, 0.001)], "pitch_span_semitones": snappedf(semis, 0.01), "sequence": lv.sequence_picked}
	e1["ok"] = lv.picked.size() == 40 and repeats == 0 and used.size() == 4 and vmin >= -9.0 and vmax <= -3.0 and vmax - vmin > 3.0 and pmin >= pow(2.0, -2.0 / 12.0) - 1e-4 and pmax <= pow(2.0, 2.0 / 12.0) + 1e-4 and semis > 2.0 and lv.sequence_picked == [0, 1, 2, 3, 0, 1, 2, 3]
	out["variations"] = e1
	# 2.
	var li: Dictionary = lanes.instances
	var st2: Dictionary = li.d.get_stats()
	out["instances"] = {"voices": li.voices, "refused": li.ids.count(0), "stats_rejected": st2.rejected_instances,
		"ok": li.voices == 3 and li.ids.count(0) == 2 and st2.rejected_instances == 2}
	# 3.
	var st3: Dictionary = lanes.cooldown.d.get_stats()
	out["cooldown"] = {"started": st3.started, "rejected": st3.rejected_cooldown, "ok": st3.started == 10 and st3.rejected_cooldown == 50}
	# 4.
	var ld: Dictionary = lanes.distance
	var st4: Dictionary = ld.d.get_stats()
	out["distance"] = {"near": ld.near != 0, "far": ld.far != 0, "culled": st4.culled, "ok": ld.near != 0 and ld.far == 0 and st4.culled == 1}
	# 5.
	var lb: Dictionary = lanes.budget
	var st5: Dictionary = lb.stats
	var stolen_signals := 0
	for f in lb.finished:
		stolen_signals += 1 if f[1] else 0
	var expected := ["high_0", "high_1", "high_2", "high_3", "loud", "low_0", "low_1", "low_2"]
	out["budget"] = {"after_low": lb.after_low, "after_high": lb.after_high, "refused_far": lb.refused_far, "playing": lb.playing,
		"stolen": st5.stolen, "rejected": st5.rejected_voices, "peak": st5.peak, "stolen_signals": stolen_signals,
		"ok": lb.after_low == 8 and lb.after_high == 8 and lb.refused_far == 4 and lb.playing == expected and st5.stolen == 5 and st5.rejected_voices == 4 and st5.peak == 8 and stolen_signals == 5}
	# 6.
	var ls: Dictionary = lanes.snapshot
	var tr: Dictionary = ls.trace
	out["snapshot"] = {"trace": tr, "ok": tr["60"] == -3.0 and absf(tr["75"] - (-3.0 - 10.0)) < 0.5 and tr["105"] == -23.0 and tr["120"] == -23.0 and absf(tr["135"] - (-13.0)) < 0.5 and tr["150"] == -3.0 and tr["180"] == -3.0}
	# 7. The music's tone: before the line (4.0-4.4 s), under it (4.65-5.0 s), after it and the release (5.6-6.0 s).
	var lk: Dictionary = lanes.duck
	var base := 210.0 / 60.0
	var before := _tone_db(220.0, 4.0 - base, 4.4 - base)
	var under := _tone_db(220.0, 4.65 - base, 5.0 - base)
	var after := _tone_db(220.0, 5.6 - base, 6.0 - base)
	var line_db := _tone_db(1000.0, 4.65 - base, 5.0 - base)
	out["duck"] = {"trace": lk.trace, "music_before_db": snappedf(before, 0.01), "music_under_db": snappedf(under, 0.01), "music_after_db": snappedf(after, 0.01),
		"ducked_by_db": snappedf(under - before, 0.01), "line_db": snappedf(line_db, 0.01), "captured_s": snappedf(captured.size() / AudioServer.get_mix_rate(), 0.01),
		"ok": absf((under - before) + 12.0) < 0.5 and absf(after - before) < 0.2 and lk.trace["264"] == -3.0 and lk.trace["276"] == -15.0 and lk.trace["300"] == -15.0 and lk.trace["345"] == -3.0 and line_db > before - 3.0}
	# 8.
	var lf: Dictionary = lanes.follow
	out["follow"] = {"max_error_m": snappedf(lf.max_error, 0.0001), "after_free_m": snappedf(lf.after_free, 0.0001), "still_playing": lf.still_playing,
		"ok": lf.max_error < 1e-4 and lf.after_free >= 0.0 and lf.after_free < 1e-4 and lf.still_playing}
	# 9.
	var lvv: Dictionary = lanes.virtual
	var st9: Dictionary = lvv.stats
	out["virtual"] = {"virtual_60": lvv.virtual_60, "playing_60": lvv.playing_60, "voices_60": lvv.voices_60, "virtual_103": lvv.virtual_103,
		"position_103": snappedf(lvv.position_103, 0.001), "expected_103": snappedf(lvv.expected_103, 0.001), "virtualized": st9.virtualized, "realized": st9.realized,
		"ok": lvv.virtual_60 and lvv.playing_60 and lvv.voices_60 == 2 and not lvv.virtual_103 and absf(lvv.position_103 - lvv.expected_103) < 0.04 and st9.virtualized == 1 and st9.realized == 1}
	# 10.
	var lr: Dictionary = lanes.reach
	var st10: Dictionary = lr.stats
	out["reach"] = {"virtual_at": {"46.8 m": lr["30"], "31.8 m": lr["120"], "25.2 m": lr["160"], "10.4 m": lr["250"], "60.1 m": lr["270"]},
		"playing": lr.playing, "culled": st10.culled, "virtualized": st10.virtualized, "realized": st10.realized,
		"ok": lr["30"] and not lr["160"] and not lr["250"] and lr["270"] and lr.playing and st10.culled == 0 and st10.virtualized == 2 and st10.realized == 1}
	# 11.
	var lo: Dictionary = lanes.oneshot
	var st11: Dictionary = lo.stats
	out["oneshot"] = {"virtual_20": lo.virtual_20, "playing_20": lo.playing_20, "playing_50": lo.playing_50, "finished": lo.finished, "realized": st11.realized,
		"ok": lo.virtual_20 and lo.playing_20 and not lo.playing_50 and lo.finished.size() == 1 and not lo.finished[0][1] and st11.realized == 0}
	# 12.
	var lold: Dictionary = lanes.oldest
	out["oldest"] = {"playing": lold.playing, "stolen": lold.stats.stolen, "rejected": lold.stats.rejected_instances,
		"ok": lold.playing == [false, true, true] and lold.stats.stolen == 1 and lold.stats.rejected_instances == 0}
	var usec := 0
	var frames := 0
	for n in lanes:
		var st: Dictionary = lanes[n].d.get_stats()
		usec += st.usec
		frames = maxi(frames, st.frames)
	for n in out:
		if not out[n].ok:
			failures.append(n)
	result["lanes"] = out
	result["failures"] = failures
	result["director_usec_per_frame"] = snappedf(float(usec) / maxf(1.0, frames), 0.01)
	result["mix_rate"] = AudioServer.get_mix_rate()
	print("AUDIO_DIRECTOR_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("audio_director_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	for n in lanes:
		lanes[n].d.stop_all()
	_quit.call_deferred(1 if not failures.is_empty() else 0)


var quit_code := 0
var quit_frames := 0
var quit_ms := 0


# Quits once the stopped sounds' playbacks are gone from the audio server (its mixing thread lets go of them: a few
# frames and, headless, where frames run faster than the audio, a quarter of a second).
func _quit(code: int) -> void:
	quit_code = code
	quit_ms = Time.get_ticks_msec()
	process_frame.connect(func() -> void:
		quit_frames += 1
		if quit_frames >= 3 and Time.get_ticks_msec() - quit_ms >= 250:
			quit(quit_code))


# --many=1: 64 voices; 50 requests a frame at random places within 100 m (a quarter loops), the rest refused, culled
# or given voices by quieter ones.
var many_director: FellowAudioDirector
var many_events := []
var many_rng := RandomNumberGenerator.new()


func _build_many() -> void:
	many_rng.seed = 3
	many_director = _director("Many", 64)
	for k in 8:
		var e := _event([_tone(300.0 + 100.0 * k, 0.3, k % 4 == 0)], "Sfx", true)
		e.priority = 64 + 16 * k
		e.max_distance = 80.0
		e.volume_random_db = 2.0
		e.pitch_random_semitones = 1.0
		e.virtual_voice = _arg("virtual", "0") == "1"  # --virtual=1: the sounds kept as virtual voices
		many_events.append(e)
	physics_frame.connect(_on_many_frame)


func _on_many_frame() -> void:
	if done:
		return
	frame += 1
	for k in 50:
		var e: FellowSoundEvent = many_events[many_rng.randi() % many_events.size()]
		var p := Vector3(many_rng.randf_range(-100, 100), 0, many_rng.randf_range(-100, 100))
		many_director.play(e, p)
	if frame == 60:
		many_director.reset_stats()
	if frame >= int(many_seconds * 60.0):
		done = true
		var st: Dictionary = many_director.get_stats()
		var r := {"stats": st, "update_us_per_frame": snappedf(float(st.usec) / maxf(1, st.frames), 0.01),
			"play_us_per_request": snappedf(float(st.play_usec) / maxf(1, (frame - 60) * 50), 0.01)}
		print("AUDIO_DIRECTOR_MANY " + JSON.stringify(r))
		var f := FileAccess.open(out_dir.path_join("audio_director_many.json"), FileAccess.WRITE)
		f.store_string(JSON.stringify(r, "  "))
		f.close()
		many_director.stop_all()
		_quit.call_deferred(0)
