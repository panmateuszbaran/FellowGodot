extends Node3D
## FellowEngine's image regression suite: a face that speaks and blinks (milestone 83). Four test faces (a head with
## eyes and a mouth on ARKit's blend shapes), each a FellowFaceAnimator reading a synthesised vowel on a timeline
## (voice_time held in its middle): /a/ (the jaw open), /i/ (the mouth wide), /u/ (round); the fourth silent, blinking.
## Every traversal of the face's channels settles in a few frames and the animators stop at a set physics frame (the
## blink caught closed), so the captured frame does not depend on how many frames the capture runs.

const RATE := 48000.0
const FORMANTS := {"a": [730, 1090, 2440], "i": [270, 2290, 3010], "u": [300, 870, 2240], "e": [530, 1840, 2480],
	"o": [570, 840, 2410]}

var _faces := []
var _frame := 0


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


# A test face: a head (a sphere), two eyes and a mouth (flat discs on its front), with ARKit's blend shapes for the
# mouth and the eyes moving the discs' points.
func _face_mesh() -> ArrayMesh:
	var shapes := ["jawOpen", "mouthStretchLeft", "mouthStretchRight", "mouthPucker", "mouthFunnel", "mouthRollLower", "eyeBlinkLeft", "eyeBlinkRight"]
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
	# The features: discs (a ring of 24 points round a centre) — the mouth at (0, -0.2), the eyes at (±0.17, 0.12).
	var verts := PackedVector3Array()
	var norms := PackedVector3Array()
	var idx := PackedInt32Array()
	var feature := PackedInt32Array()  # 0 mouth, 1 left eye (+x), 2 right eye
	var centers := [Vector2(0, -0.2), Vector2(0.17, 0.12), Vector2(-0.17, 0.12)]
	var radii := [Vector2(0.12, 0.018), Vector2(0.06, 0.04), Vector2(0.06, 0.04)]
	for fi in 3:
		var c: Vector2 = centers[fi]
		var r: Vector2 = radii[fi]
		var base := verts.size()
		verts.append(Vector3(c.x, c.y, sqrt(0.25 - c.x * c.x - c.y * c.y) + 0.004))
		norms.append(Vector3(0, 0, 1))
		feature.append(fi)
		for k in 24:
			var ang := TAU * k / 24.0
			var p := c + Vector2(cos(ang) * r.x, sin(ang) * r.y)
			verts.append(Vector3(p.x, p.y, sqrt(maxf(0.0, 0.25 - p.x * p.x - p.y * p.y)) + 0.004))
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
					if fi == 1:
						p.y -= d.y * 0.92
				"eyeBlinkRight":
					if fi == 2:
						p.y -= d.y * 0.92
			v[i] = p
		var a := []
		a.resize(Mesh.ARRAY_MAX)
		a[Mesh.ARRAY_VERTEX] = v
		a[Mesh.ARRAY_NORMAL] = norms
		feature_shapes.append(a)
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, feature_shapes)
	var dark := StandardMaterial3D.new()
	dark.albedo_color = Color(0.25, 0.08, 0.08)
	mesh.surface_set_material(1, dark)
	return mesh


func _ready() -> void:
	var mesh := _face_mesh()
	var kinds := ["a", "i", "u", "sil"]
	for k in 4:
		var mi := MeshInstance3D.new()
		mi.mesh = mesh
		mi.position = Vector3(-1.65 + 1.1 * k, 0, 0)
		add_child(mi)
		var fa := FellowFaceAnimator.new()
		fa.blinks = false
		fa.voice_stream = _wav(_sound(kinds[k], 0.4))
		fa.voice_time = 0.2
		add_child(fa)
		fa.mesh_path = fa.get_path_to(mi)
		_faces.append(fa)
	($Camera as Camera3D).look_at_from_position(Vector3(0, 0.1, 3.4), Vector3(0, 0, 0))


func _physics_process(_delta: float) -> void:
	_frame += 1
	if _frame == 15:
		_faces[3].blink()
	if _frame == 19:
		for fa in _faces:
			fa.process_mode = Node.PROCESS_MODE_DISABLED
