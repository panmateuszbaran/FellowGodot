extends SceneTree
## FellowEngine milestone 83 check (decision 0191): FellowFaceAnimator — a face that speaks (its mouth from the voice
## it plays, analysed ahead) and blinks.
##
##   godot --path benchmarks/project --headless --fixed-fps 60 -s res://bench/face_check.gd -- --out=<dir>
##   godot --path benchmarks/project --fixed-fps 60 --resolution 640x360 --write-movie <dir>/m.png
##       -s res://bench/face_check.gd -- --out=<dir> --listen=1     (Movie Maker: the voice played, the face following)
##   godot ... -s res://bench/face_check.gd -- --out=<dir> --many=100
##
## The voice: vowels made by formant synthesis (a 120 Hz pulse train through three resonators and the lips' radiation:
## /a/, /i/, /u/, /e/, /o/),
## an "s" (noise over 4 kHz) and silences, in one stream. Headless: the analysis read in each sound's middle (/a/ opens
## the jaw most, /i/ widens the mouth, /u/ rounds it, the "s" puts the lower lip under the teeth, silence closes it),
## a higher voice (210 Hz), the level (the same /a/ at -6, -20, -35 and -50 dB), the blinks over a minute (their number and intervals). With
## --listen=1 the voice played by an AudioStreamPlayer under Movie Maker and the face's blend shapes followed: the mouth
## leads the sound by the anticipation, its blend shapes are its channels. --many=N: N faces, the time a frame. One
## JSON line: FACE_CHECK {...} (or FACE_MANY {...}); exit code 1 when a lane did otherwise than expected.

const RATE := 48000.0
const SEGMENTS := [["sil", 0.3], ["a", 0.4], ["sil", 0.2], ["i", 0.4], ["sil", 0.2], ["u", 0.4], ["sil", 0.2],
	["e", 0.4], ["sil", 0.2], ["o", 0.4], ["sil", 0.2], ["s", 0.3], ["sil", 0.4]]
const FORMANTS := {"a": [730, 1090, 2440], "i": [270, 2290, 3010], "u": [300, 870, 2240], "e": [530, 1840, 2480],
	"o": [570, 840, 2410]}

var args := {}
var out_dir := ""
var result := {}
var root3d: Node3D
var frame := 0
var done := false
var listen := false
var many := 0
var voice: AudioStreamWAV
var face: FellowFaceAnimator
var face_mesh: MeshInstance3D
var player: AudioStreamPlayer
var trace := []
var start_frame := 30
var blink_face: FellowFaceAnimator
var blink_log := []
var blink_closed := false
var faces := []
var gaze_face: FellowFaceAnimator
var gaze_mesh: MeshInstance3D
var gaze_target: Node3D
var gaze_reads := {}
var gaze_trace := []


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://face_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	listen = _arg("listen", "0") == "1"
	many = int(_arg("many", "0"))
	_build.call_deferred()


# A vowel: a pulse train at 120 Hz (its spectrum falling 12 dB an octave) through three resonators (Klatt's) in
# cascade at its formants; or an "s": noise over 4 kHz; or silence. Peak p_peak.
func _sound(kind: String, seconds: float, peak := 0.5, f0 := 120.0) -> PackedFloat32Array:
	var n := int(seconds * RATE)
	var out := PackedFloat32Array()
	out.resize(n)
	if kind == "sil":
		return out
	var rng := RandomNumberGenerator.new()
	rng.seed = 5
	if kind == "s":
		var x1 := 0.0
		var y1 := 0.0
		var a := exp(-TAU * 4000.0 / RATE)
		for i in n:
			var x := rng.randf_range(-1.0, 1.0)
			y1 = a * (y1 + x - x1)  # one-pole high-pass
			x1 = x
			out[i] = y1
	else:
		var f: Array = FORMANTS[kind]
		var bw := [80.0, 100.0, 150.0]
		var res := []
		for k in 3:
			var c := -exp(-TAU * bw[k] / RATE)
			var b := 2.0 * exp(-PI * bw[k] / RATE) * cos(TAU * f[k] / RATE)
			res.append([1.0 - b - c, b, c, 0.0, 0.0])
		var period := int(RATE / f0)
		var g1 := 0.0
		var g2 := 0.0
		var lp := exp(-TAU * 100.0 / RATE)
		var last := 0.0
		for i in n:
			var x := 1.0 if i % period == 0 else 0.0
			g1 = (1.0 - lp) * x + lp * g1
			g2 = (1.0 - lp) * g1 + lp * g2
			var y := g2
			for r in res:
				var v: float = r[0] * y + r[1] * r[3] + r[2] * r[4]
				r[4] = r[3]
				r[3] = v
				y = v
			out[i] = y - last  # the lips' radiation (+6 dB an octave)
			last = y
	# Peak-normalised, 10 ms fades.
	var m := 0.0
	for v in out:
		m = maxf(m, absf(v))
	var fade := int(0.01 * RATE)
	for i in n:
		var g := minf(1.0, minf(float(i) / fade, float(n - 1 - i) / fade))
		out[i] = out[i] / maxf(m, 1e-9) * peak * g
	return out


func _wav(samples: PackedFloat32Array) -> AudioStreamWAV:
	var data := PackedByteArray()
	data.resize(samples.size() * 2)
	for i in samples.size():
		data.encode_s16(i * 2, int(clampf(samples[i], -1.0, 1.0) * 32767.0))
	var w := AudioStreamWAV.new()
	w.format = AudioStreamWAV.FORMAT_16_BITS
	w.mix_rate = int(RATE)
	w.data = data
	return w


func _voice() -> AudioStreamWAV:
	var all := PackedFloat32Array()
	for s in SEGMENTS:
		all.append_array(_sound(s[0], s[1]))
	return _wav(all)


func _segment_mid(index: int) -> float:
	var t := 0.0
	for k in index:
		t += SEGMENTS[k][1]
	return t + SEGMENTS[index][1] * 0.5


func _segment_start(index: int) -> float:
	var t := 0.0
	for k in index:
		t += SEGMENTS[k][1]
	return t


# A test face: a head (a sphere) and on its front a mouth, two eyes (white) with pupils (flat discs), with ARKit's blend
# shapes for the mouth, the eyelids and the gaze moving the discs' points.
func _face_mesh() -> ArrayMesh:
	var shapes := ["jawOpen", "mouthStretchLeft", "mouthStretchRight", "mouthPucker", "mouthFunnel", "mouthRollLower", "eyeBlinkLeft", "eyeBlinkRight",
		"eyeLookOutLeft", "eyeLookInLeft", "eyeLookUpLeft", "eyeLookDownLeft", "eyeLookOutRight", "eyeLookInRight", "eyeLookUpRight", "eyeLookDownRight"]
	var mesh := ArrayMesh.new()
	mesh.blend_shape_mode = Mesh.BLEND_SHAPE_MODE_NORMALIZED
	for s in shapes:
		mesh.add_blend_shape(s)
	# The head.
	var sm := SphereMesh.new()
	sm.radius = 0.5
	sm.height = 1.0
	var head: Array = sm.get_mesh_arrays()
	var head_shapes := []
	for s in shapes:
		var a := []
		a.resize(Mesh.ARRAY_MAX)
		a[Mesh.ARRAY_VERTEX] = head[Mesh.ARRAY_VERTEX]
		a[Mesh.ARRAY_NORMAL] = head[Mesh.ARRAY_NORMAL]
		a[Mesh.ARRAY_TANGENT] = head[Mesh.ARRAY_TANGENT]
		head_shapes.append(a)
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, head, head_shapes)
	var skin := StandardMaterial3D.new()
	skin.albedo_color = Color(0.86, 0.7, 0.6)
	mesh.surface_set_material(0, skin)
	# The features: discs (a ring of 24 points round a centre): 0 the mouth, 1 and 2 the eyes (the character's left at
	# +x), 3 and 4 their pupils (a little in front).
	var centers := [Vector2(0, -0.2), Vector2(0.17, 0.12), Vector2(-0.17, 0.12), Vector2(0.17, 0.12), Vector2(-0.17, 0.12)]
	var radii := [Vector2(0.12, 0.018), Vector2(0.06, 0.04), Vector2(0.06, 0.04), Vector2(0.022, 0.022), Vector2(0.022, 0.022)]
	var lift := [0.004, 0.004, 0.004, 0.02, 0.02]  # the pupils well in front: moved, they stay over the eye
	var groups := [[1, 2, Color(0.95, 0.95, 0.92)], [0, 3, 4, Color(0.2, 0.06, 0.06)]]
	for g in groups:
		var ids: Array = g.slice(0, g.size() - 1)
		var verts := PackedVector3Array()
		var norms := PackedVector3Array()
		var idx := PackedInt32Array()
		var feature := PackedInt32Array()
		for fi in ids:
			var c: Vector2 = centers[fi]
			var r: Vector2 = radii[fi]
			var base := verts.size()
			verts.append(Vector3(c.x, c.y, sqrt(0.25 - c.x * c.x - c.y * c.y) + lift[fi]))
			norms.append(Vector3(0, 0, 1))
			feature.append(fi)
			for k in 24:
				var ang := TAU * k / 24.0
				var p := c + Vector2(cos(ang) * r.x, sin(ang) * r.y)
				verts.append(Vector3(p.x, p.y, sqrt(maxf(0.0, 0.25 - p.x * p.x - p.y * p.y)) + lift[fi]))
				norms.append(Vector3(0, 0, 1))
				feature.append(fi)
			for k in 24:
				idx.append_array([base, base + 1 + (k + 1) % 24, base + 1 + k])
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = verts
		arrays[Mesh.ARRAY_NORMAL] = norms
		arrays[Mesh.ARRAY_INDEX] = idx
		var feature_shapes := []
		for s in shapes:
			var v := verts.duplicate()
			for i in v.size():
				var fi: int = feature[i]
				var c: Vector2 = centers[fi]
				var d := Vector2(v[i].x, v[i].y) - c
				var p := v[i]
				match s:
					"jawOpen":
						if fi == 0 and d.y < 0.0:
							p.y -= 0.14 * (-d.y / 0.018 + 0.5) / 1.5
						elif fi == 0:
							p.y += 0.02 * (d.y / 0.018)
					"mouthStretchLeft":
						if fi == 0 and d.x > 0.0:
							p.x += 0.05 * d.x / 0.12
					"mouthStretchRight":
						if fi == 0 and d.x < 0.0:
							p.x += 0.05 * d.x / 0.12
					"mouthPucker":
						if fi == 0:
							p.x -= d.x * 0.5
							p.z += 0.03
					"mouthFunnel":
						if fi == 0:
							p.y += d.y * 2.5
					"mouthRollLower":
						if fi == 0 and d.y < 0.0:
							p.y += 0.01
					"eyeBlinkLeft":
						if fi == 1 or fi == 3:
							p.y -= (p.y - 0.12) * 0.92
					"eyeBlinkRight":
						if fi == 2 or fi == 4:
							p.y -= (p.y - 0.12) * 0.92
					"eyeLookOutLeft":
						if fi == 3:
							p.x += 0.03
					"eyeLookInLeft":
						if fi == 3:
							p.x -= 0.03
					"eyeLookUpLeft":
						if fi == 3:
							p.y += 0.016
					"eyeLookDownLeft":
						if fi == 3:
							p.y -= 0.016
					"eyeLookOutRight":
						if fi == 4:
							p.x -= 0.03
					"eyeLookInRight":
						if fi == 4:
							p.x += 0.03
					"eyeLookUpRight":
						if fi == 4:
							p.y += 0.016
					"eyeLookDownRight":
						if fi == 4:
							p.y -= 0.016
				v[i] = p
			var a := []
			a.resize(Mesh.ARRAY_MAX)
			a[Mesh.ARRAY_VERTEX] = v
			a[Mesh.ARRAY_NORMAL] = norms
			feature_shapes.append(a)
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, feature_shapes)
		var m := StandardMaterial3D.new()
		m.albedo_color = g[g.size() - 1]
		mesh.surface_set_material(mesh.get_surface_count() - 1, m)
	return mesh


func _build() -> void:
	root3d = Node3D.new()
	get_root().add_child(root3d)
	voice = _voice()
	if many > 0:
		_build_many()
		return
	var cam := Camera3D.new()
	root3d.add_child(cam)
	cam.current = true
	cam.position = Vector3(0, 0, 1.8)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-30, 20, 0)
	root3d.add_child(light)
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.5, 0.6, 0.7)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.6, 0.6, 0.6)
	env.environment = e
	root3d.add_child(env)
	face_mesh = MeshInstance3D.new()
	face_mesh.mesh = _face_mesh()
	root3d.add_child(face_mesh)
	player = AudioStreamPlayer.new()
	player.stream = voice
	root3d.add_child(player)
	face = FellowFaceAnimator.new()
	root3d.add_child(face)
	face.mesh_path = face.get_path_to(face_mesh)
	face.voice_path = face.get_path_to(player)
	face.blinks = false
	# A second face only blinking, for the minute's blinks.
	blink_face = FellowFaceAnimator.new()
	blink_face.seed = 11
	root3d.add_child(blink_face)
	# Milestone 89: a face looking at a target (no saccades first), then with saccades and nothing to look at.
	gaze_mesh = MeshInstance3D.new()
	gaze_mesh.mesh = _face_mesh()
	gaze_mesh.position = Vector3(4, 0, 0)
	root3d.add_child(gaze_mesh)
	gaze_target = Node3D.new()
	root3d.add_child(gaze_target)
	gaze_face = FellowFaceAnimator.new()
	gaze_face.blinks = false
	gaze_face.lip_sync = false
	gaze_face.saccades = false
	gaze_face.eye_center = Vector3(0, 0.12, 0.45)
	root3d.add_child(gaze_face)
	gaze_face.mesh_path = gaze_face.get_path_to(gaze_mesh)
	gaze_face.look_target_path = gaze_face.get_path_to(gaze_target)
	physics_frame.connect(_on_frame)


func _on_frame() -> void:
	if done:
		return
	frame += 1
	if listen:
		if frame == start_frame:
			player.play()
		if frame > start_frame:
			var jaw_shape := face_mesh.find_blend_shape_by_name("jawOpen")
			trace.append({"t": (frame - start_frame) / 60.0, "jaw": face.get_channel(FellowFaceAnimator.CHANNEL_JAW_OPEN),
				"wide": face.get_channel(FellowFaceAnimator.CHANNEL_MOUTH_WIDE), "round": face.get_channel(FellowFaceAnimator.CHANNEL_MOUTH_ROUND),
				"jaw_shape": face_mesh.get_blend_shape_value(jaw_shape), "pos": player.get_playback_position()})
		if frame == start_frame + int(_segment_mid(1) * 60.0) or frame == start_frame + int(_segment_mid(3) * 60.0) or frame == start_frame + int(_segment_mid(5) * 60.0):
			if not args.has("_shots"):
				args["_shots"] = []
			args["_shots"].append(get_root().get_texture().get_image())
		if frame >= start_frame + 270:
			done = true
			_finish()
	else:
		# The gaze: the target ahead, at the character's left, above, right and below (relative to the face at x 4); then
		# saccades with nothing to look at.
		var spots := {60: ["ahead", Vector3(4, 0.12, 2.5)], 120: ["left", Vector3(5.2, 0.12, 1.0)], 180: ["up", Vector3(4, 1.0, 2.0)], 240: ["right_down", Vector3(3.6, -0.3, 2.0)]}
		if spots.has(frame):
			gaze_target.position = spots[frame][1]
		if frame - 50 in spots:
			var name: String = spots[frame - 50][0]
			var r := {}
			for k in ["look_out_left", "look_in_left", "look_up_left", "look_down_left", "look_out_right", "look_in_right", "look_up_right", "look_down_right"]:
				r[k] = snappedf(gaze_face.get_channels()[k], 0.001)
			var shape := gaze_mesh.find_blend_shape_by_name("eyeLookOutLeft")
			r["shape_out_left"] = snappedf(gaze_mesh.get_blend_shape_value(shape), 0.001)
			r["angles"] = [snappedf(gaze_face.get_gaze_angles().x, 0.01), snappedf(gaze_face.get_gaze_angles().y, 0.01)]
			gaze_reads[name] = r
		if frame == 300:
			gaze_face.look_target_path = NodePath()
			gaze_face.saccades = true
		if frame > 330 and frame <= 930:  # back from the last target first
			gaze_trace.append(gaze_face.get_gaze_angles())
		# The blinks over a minute (the frame's lid, closed past 0.9).
		var lid := blink_face.get_channel(FellowFaceAnimator.CHANNEL_BLINK_LEFT)
		if lid > 0.9 and not blink_closed:
			blink_log.append(frame / 60.0)
		blink_closed = lid > 0.9
		if frame >= 3600:
			done = true
			_finish()


func _dominant(d: Dictionary) -> String:
	var best := ""
	var v := -1.0
	for k in ["jaw_open", "mouth_wide", "mouth_round", "teeth"]:
		if d[k] > v:
			v = d[k]
			best = k
	return best


func _finish() -> void:
	var failures := []
	var out := {}
	if not listen:
		# The analysis, in each sound's middle.
		var reads := {}
		for i in SEGMENTS.size():
			var name: String = SEGMENTS[i][0]
			if name == "sil" and i != 0:
				continue
			var d := face.sample_voice(voice, _segment_mid(i))
			var r := {}
			for k in d:
				r[k] = snappedf(d[k], 0.001) if typeof(d[k]) == TYPE_FLOAT else d[k]
			r["dominant"] = _dominant(d)
			reads[name] = r
		var ok: bool = reads.a.dominant == "jaw_open" and reads.i.dominant == "mouth_wide" and reads.u.dominant == "mouth_round" and reads.s.dominant == "teeth"
		ok = ok and reads.a.jaw_open > 0.7 and reads.i.mouth_wide > 0.7 and reads.u.mouth_round > 0.7 and reads.s.teeth > 0.7
		ok = ok and reads.sil.mouth_closed > 0.99 and reads.sil.jaw_open < 0.01
		ok = ok and reads.o.mouth_round > reads.o.mouth_wide and reads.e.mouth_wide > reads.e.mouth_round
		# A higher voice (210 Hz): the same mouths.
		var high := {}
		for v in ["a", "i", "u"]:
			high[v] = _dominant(face.sample_voice(_wav(_sound(v, 0.4, 0.5, 210.0)), 0.2))
		ok = ok and high.a == "jaw_open" and high.i == "mouth_wide" and high.u == "mouth_round"
		out["analysis"] = {"reads": reads, "high_voice": high, "ok": ok}
		# The level: the same /a/ quieter.
		var levels := {}
		for db in [-6, -20, -35, -50]:
			var s := _wav(_sound("a", 0.4, pow(10.0, db / 20.0)))
			levels[str(db)] = snappedf(face.sample_voice(s, 0.2).jaw_open, 0.001)
		out["level"] = {"jaw_open": levels, "ok": levels["-6"] > 0.7 and levels["-6"] >= levels["-20"] and levels["-20"] > levels["-35"] and levels["-35"] > levels["-50"] and levels["-50"] < 0.01}
		# The blinks over a minute: about one each 2-6 s (with the blink's 0.18 s).
		var gaps := []
		for k in range(1, blink_log.size()):
			gaps.append(snappedf(blink_log[k] - blink_log[k - 1], 0.01))
		var gmin: float = gaps.min() if not gaps.is_empty() else 0.0
		var gmax: float = gaps.max() if not gaps.is_empty() else 0.0
		# The gaze.
		var gr := gaze_reads
		var jumps := 0
		var widest := 0.0
		for k in range(1, gaze_trace.size()):
			jumps += 1 if (gaze_trace[k] - gaze_trace[k - 1]).length() > 0.3 and (gaze_trace[k - 1] - gaze_trace[maxi(0, k - 2)]).length() <= 0.3 else 0
			widest = maxf(widest, maxf(absf(gaze_trace[k].x), absf(gaze_trace[k].y)))
		var gok: bool = gr.ahead.look_out_left < 0.02 and gr.ahead.look_in_left < 0.02 and gr.ahead.look_up_left < 0.02 and gr.ahead.look_down_left < 0.02
		gok = gok and gr.left.look_out_left > 0.99 and gr.left.look_in_right > 0.99 and gr.left.look_in_left == 0.0 and absf(gr.left.shape_out_left - gr.left.look_out_left) < 0.002
		gok = gok and gr.up.look_up_left > 0.99 and gr.up.look_up_right > 0.99 and gr.up.look_down_left == 0.0
		gok = gok and gr.right_down.look_in_left > 0.3 and gr.right_down.look_out_right > 0.3 and gr.right_down.look_down_left > 0.3 and gr.right_down.look_out_left == 0.0
		gok = gok and jumps >= 4 and jumps <= 30 and widest <= 1.51
		out["gaze"] = {"reads": gr, "saccades_in_10_s": jumps, "saccade_widest_deg": snappedf(widest, 0.01), "ok": gok}
		out["blinks"] = {"count": blink_log.size(), "gap_min": gmin, "gap_max": gmax,
			"ok": blink_log.size() >= 10 and blink_log.size() <= 25 and gmin >= 2.0 and gmax <= 6.3}
	else:
		# The mouth leads the sound: the jaw past half its /a/ value against the /a/'s start; the shapes are the channels.
		var a_start := _segment_start(1)
		var a_value: float = face.sample_voice(voice, _segment_mid(1)).jaw_open
		var cross := -1.0
		var shape_error := 0.0
		var closed_after := -1.0
		for s in trace:
			if cross < 0.0 and s.t > 0.1 and s.jaw > a_value * 0.5:
				cross = s.t
			shape_error = maxf(shape_error, absf(s.jaw_shape - minf(1.0, s.jaw)))
		# In each vowel's middle the channel the analysis said (the face, not the analysis).
		var mids := {}
		for i in [1, 3, 5]:
			var t := _segment_mid(i)
			for s in trace:
				if absf(s.t - t) < 0.009:
					mids[SEGMENTS[i][0]] = {"jaw": snappedf(s.jaw, 0.001), "wide": snappedf(s.wide, 0.001), "round": snappedf(s.round, 0.001)}
		var lead_ms := (a_start - cross) * 1000.0
		var ok_mids: bool = mids.size() == 3 and mids.a.jaw > mids.a.wide and mids.a.jaw > mids.a.round and mids.i.wide > mids.i.jaw * 0.8 and mids.i.wide > mids.i.round and mids.u.round > mids.u.wide
		out["listen"] = {"a_start_s": a_start, "jaw_half_s": snappedf(cross, 0.001), "mouth_lead_ms": snappedf(lead_ms, 0.1),
			"shape_error": snappedf(shape_error, 0.0001), "mids": mids,
			"ok": cross > 0.0 and lead_ms > -10.0 and lead_ms < 60.0 and shape_error < 1e-3 and ok_mids}
		if args.has("_shots"):
			var shots: Array = args["_shots"]
			var w: int = shots[0].get_width()
			var h: int = shots[0].get_height()
			var strip := Image.create(w * shots.size(), h, false, Image.FORMAT_RGBA8)
			for k in shots.size():
				var img: Image = shots[k]
				img.convert(Image.FORMAT_RGBA8)
				strip.blit_rect(img, Rect2i(0, 0, w, h), Vector2i(k * w, 0))
			strip.save_png(out_dir.path_join("face_strip.png"))
	for n in out:
		if not out[n].ok:
			failures.append(n)
	result["lanes"] = out
	result["failures"] = failures
	result["global"] = FellowFaceAnimator.get_global_stats()
	print("FACE_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("face_check.json" if not listen else "face_listen.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	if player:
		player.stop()
	_quit.call_deferred(1 if not failures.is_empty() else 0)


var quit_code := 0
var quit_frames := 0
var quit_ms := 0


func _quit(code: int) -> void:
	quit_code = code
	quit_ms = Time.get_ticks_msec()
	process_frame.connect(func() -> void:
		quit_frames += 1
		if quit_frames >= 3 and Time.get_ticks_msec() - quit_ms >= 250:
			quit(quit_code))


# --many=N: N faces, each its mesh, the voice read on a timeline (voice_time set each frame), blinking.
func _build_many() -> void:
	var mesh := _face_mesh()
	for k in many:
		var mi := MeshInstance3D.new()
		mi.mesh = mesh
		mi.position = Vector3((k % 10) * 1.2, (k / 10) * 1.2, -10)
		root3d.add_child(mi)
		var fa := FellowFaceAnimator.new()
		fa.seed = k
		fa.voice_stream = voice
		root3d.add_child(fa)
		fa.mesh_path = fa.get_path_to(mi)
		faces.append(fa)
	physics_frame.connect(_on_many_frame)


func _on_many_frame() -> void:
	if done:
		return
	frame += 1
	for k in faces.size():
		faces[k].voice_time = fmod(frame / 60.0 + k * 0.037, 4.0)
	if frame == 60:
		FellowFaceAnimator.reset_global_stats()
	if frame >= 360:
		done = true
		var st := FellowFaceAnimator.get_global_stats()
		var r := {"faces": many, "stats": st, "us_per_face_frame": snappedf(float(st.usec) / maxf(1, st.frames), 0.001),
			"ms_per_frame": snappedf(float(st.usec) / 300.0 / 1000.0, 0.0001)}
		print("FACE_MANY " + JSON.stringify(r))
		var f := FileAccess.open(out_dir.path_join("face_many.json"), FileAccess.WRITE)
		f.store_string(JSON.stringify(r, "  "))
		f.close()
		_quit.call_deferred(0)
