extends Node3D
## FellowEngine's image regression suite: a face that speaks and blinks (milestone 83). Four test faces (a head with
## eyes and a mouth on ARKit's blend shapes), each a FellowFaceAnimator reading a synthesised vowel on a timeline
## (voice_time held in its middle): /a/ (the jaw open), /i/ (the mouth wide), /u/ (round); the fourth silent, blinking;
## all looking at a point up and to the right of the camera (milestone 89: the pupils on ARKit's eyeLook shapes).
## Every traversal of the face's channels settles in a few frames and the animators stop at a set physics frame (the
## blink caught closed), so the captured frame does not depend on how many frames the capture runs.

const RATE := 48000.0
const FORMANTS := {"a": [730, 1090, 2440], "i": [270, 2290, 3010], "u": [300, 870, 2240], "e": [530, 1840, 2480],
	"o": [570, 840, 2410]}

var _faces := []
var _target: Node3D
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


func _ready() -> void:
	# What they look at (milestone 89): up and to the right of the camera.
	_target = Node3D.new()
	_target.position = Vector3(-1.5, 1.2, 3.4)
	add_child(_target)
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
		fa.saccades = false
		fa.eye_center = Vector3(0, 0.12, 0.45)
		add_child(fa)
		fa.mesh_path = fa.get_path_to(mi)
		fa.look_target_path = fa.get_path_to(_target)
		_faces.append(fa)
	($Camera as Camera3D).look_at_from_position(Vector3(0, 0.1, 3.4), Vector3(0, 0, 0))


func _physics_process(_delta: float) -> void:
	_frame += 1
	if _frame == 15:
		_faces[3].blink()
	if _frame == 19:
		for fa in _faces:
			fa.process_mode = Node.PROCESS_MODE_DISABLED
