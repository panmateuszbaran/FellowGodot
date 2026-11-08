extends Node3D
## FellowEngine's image regression suite: a small crowd of skinned characters (each its own Skeleton3D of 20 bones and
## AnimationPlayer, posed every frame by seeking). With the project setting fellow_regression/crowd_groups = N (the
## suite's "groups" group) the characters are posed in N sub-thread process groups, on worker threads (decision 0164):
## the picture must be the one without groups, bit for bit.

const COUNT := 24
const KEY_FPS := 20.0
const CLIP_LENGTH := [1.0, 0.6, 1.4]

var _names := PackedStringArray()
var _parent := PackedInt32Array()
var _head := PackedVector3Array()
var _tail := PackedVector3Array()
var _radius := PackedFloat32Array()
var _global: Array[Transform3D] = []
var _rest: Array[Transform3D] = []
var _players: Array[AnimationPlayer] = []
var _clip := PackedInt32Array()
var _phase := PackedFloat32Array()
var _groups: Array[Node3D] = []
var _t := 0.0


func _bone(p_name: String, p_parent: int, p_head: Vector3, p_tail: Vector3, p_radius: float) -> int:
	_names.append(p_name)
	_parent.append(p_parent)
	_head.append(p_head)
	_tail.append(p_tail)
	_radius.append(p_radius)
	return _names.size() - 1


func _ready() -> void:
	var hips := _bone("hips", -1, Vector3(0, 0.95, 0), Vector3(0, 1.1, 0), 0.14)
	var spine := _bone("spine", hips, Vector3(0, 1.1, 0), Vector3(0, 1.45, 0), 0.15)
	var neck := _bone("neck", spine, Vector3(0, 1.45, 0), Vector3(0, 1.58, 0), 0.06)
	_bone("head", neck, Vector3(0, 1.58, 0), Vector3(0, 1.8, 0), 0.1)
	for side in [-1.0, 1.0]:
		var sfx := "_l" if side < 0.0 else "_r"
		var ua := _bone("upper_arm" + sfx, spine, Vector3(0.17 * side, 1.44, 0), Vector3(0.45 * side, 1.44, 0), 0.05)
		var la := _bone("lower_arm" + sfx, ua, Vector3(0.45 * side, 1.44, 0), Vector3(0.7 * side, 1.44, 0), 0.04)
		_bone("hand" + sfx, la, Vector3(0.7 * side, 1.44, 0), Vector3(0.8 * side, 1.44, 0), 0.035)
		var ul := _bone("upper_leg" + sfx, hips, Vector3(0.1 * side, 0.93, 0), Vector3(0.1 * side, 0.5, 0), 0.08)
		var ll := _bone("lower_leg" + sfx, ul, Vector3(0.1 * side, 0.5, 0), Vector3(0.1 * side, 0.08, 0), 0.06)
		_bone("foot" + sfx, ll, Vector3(0.1 * side, 0.08, 0), Vector3(0.1 * side, 0.03, 0.15), 0.045)
	for i in _names.size():
		var y := (_tail[i] - _head[i]).normalized()
		var ref := Vector3.FORWARD if absf(y.dot(Vector3.FORWARD)) < 0.9 else Vector3.RIGHT
		var x := y.cross(ref).normalized()
		var g := Transform3D(Basis(x, y, x.cross(y).normalized()), _head[i])
		_global.append(g)
		_rest.append(g if _parent[i] < 0 else _global[_parent[i]].affine_inverse() * g)
	var mesh := _mesh()
	var skin := Skin.new()
	for b in _names.size():
		skin.add_named_bind(_names[b], _global[b].affine_inverse())
	var library := _clips()
	var groups := int(ProjectSettings.get_setting("fellow_regression/crowd_groups", 0))
	for g in groups:
		var gn := Node3D.new()
		gn.set_script(load("res://scripts/crowd_group.gd"))
		gn.set("crowd", self)
		gn.set("first", g * COUNT / groups)
		gn.set("last", (g + 1) * COUNT / groups)
		gn.process_thread_group = Node.PROCESS_THREAD_GROUP_SUB_THREAD
		gn.process_thread_group_order = 1 # after the main thread's group, which sets the time
		add_child(gn)
		_groups.append(gn)
	for i in COUNT:
		var root := Node3D.new()
		root.position = Vector3((i % 6 - 2.5) * 1.6, 0.0, (i / 6 - 1.5) * 1.8)
		root.rotation = Vector3(0.0, 0.4 * float(i % 5) - 0.8, 0.0)
		var sk := Skeleton3D.new()
		sk.name = "Skeleton3D"
		for b in _names.size():
			sk.add_bone(_names[b])
		for b in _names.size():
			sk.set_bone_parent(b, _parent[b])
			sk.set_bone_rest(b, _rest[b])
		sk.reset_bone_poses()
		root.add_child(sk)
		var mi := MeshInstance3D.new()
		mi.mesh = mesh
		mi.skin = skin
		sk.add_child(mi)
		mi.skeleton = NodePath("..")
		var ap := AnimationPlayer.new()
		root.add_child(ap)
		ap.root_node = NodePath("..")
		ap.add_animation_library("", library)
		ap.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
		(_groups[i * groups / COUNT] if groups > 0 else self).add_child(root)
		_clip.append(i % CLIP_LENGTH.size())
		_phase.append(0.137 * float(i))
		ap.play("clip%d" % _clip[i])
		_players.append(ap)
	apply(0, COUNT)


func _process(delta: float) -> void:
	_t += delta
	if _groups.is_empty():
		apply(0, COUNT)


## Poses characters [p_first, p_last) at the crowd's time: from _process(), or from their group's on a worker thread.
func apply(p_first: int, p_last: int) -> void:
	for i in range(p_first, p_last):
		_players[i].seek(fposmod(_t + _phase[i], float(CLIP_LENGTH[_clip[i]])), true)


func _mesh() -> ArrayMesh:
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var bones := PackedInt32Array()
	var weights := PackedFloat32Array()
	var indices := PackedInt32Array()
	const RINGS := 6
	const RADIAL := 10
	for b in _names.size():
		var g := _global[b]
		var length := (_tail[b] - _head[b]).length()
		var base := verts.size()
		for r in RINGS:
			var u := float(r) / float(RINGS - 1)
			var radius := _radius[b] * (0.35 + 0.65 * sin(PI * (0.08 + 0.84 * u)))
			for s in RADIAL:
				var a := TAU * float(s) / float(RADIAL)
				var n := Vector3(cos(a), 0.0, sin(a))
				verts.append(g * (n * radius + Vector3(0.0, u * length, 0.0)))
				normals.append((g.basis * n).normalized())
				var p := _parent[b]
				var wp := 0.0 if p < 0 else clampf(0.5 * (1.0 - u / 0.3), 0.0, 0.5)
				bones.append_array(PackedInt32Array([b, maxi(p, 0), 0, 0]))
				weights.append_array(PackedFloat32Array([1.0 - wp, wp, 0.0, 0.0]))
		for r in RINGS - 1:
			for s in RADIAL:
				var s2 := (s + 1) % RADIAL
				var i0 := base + r * RADIAL + s
				var i1 := base + r * RADIAL + s2
				var i2 := base + (r + 1) * RADIAL + s
				var i3 := base + (r + 1) * RADIAL + s2
				indices.append_array(PackedInt32Array([i0, i1, i2, i1, i3, i2]))
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = verts
	arr[Mesh.ARRAY_NORMAL] = normals
	arr[Mesh.ARRAY_BONES] = bones
	arr[Mesh.ARRAY_WEIGHTS] = weights
	arr[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.62, 0.5, 0.42)
	mat.roughness = 0.7
	mesh.surface_set_material(0, mat)
	return mesh


func _clips() -> AnimationLibrary:
	var library := AnimationLibrary.new()
	for c in CLIP_LENGTH.size():
		var anim := Animation.new()
		var length: float = CLIP_LENGTH[c]
		anim.length = length
		anim.loop_mode = Animation.LOOP_LINEAR
		var w := TAU / length
		var amp: float = [1.0, 1.6, 0.4][c]
		var keys := int(round(length * KEY_FPS))
		for b in _names.size():
			var track := anim.add_track(Animation.TYPE_ROTATION_3D)
			anim.track_set_path(track, NodePath("Skeleton3D:" + _names[b]))
			var rest_q := _rest[b].basis.get_rotation_quaternion()
			var side := -1.0 if _names[b].ends_with("_l") else 1.0
			for k in keys:
				var t := length * float(k) / float(keys)
				var e := Vector3.ZERO
				if _names[b].begins_with("upper_leg") or _names[b].begins_with("upper_arm"):
					e = Vector3(0.45 * amp * sin(w * t) * side, 0.0, 0.0)
				elif _names[b].begins_with("lower"):
					e = Vector3(0.5 * amp * maxf(0.0, sin(w * t * side + 1.0)), 0.0, 0.0)
				elif _names[b] == "spine" or _names[b] == "head":
					e = Vector3(0.04 * amp * sin(2.0 * w * t), 0.08 * amp * sin(w * t), 0.0)
				anim.rotation_track_insert_key(track, t, rest_q * Quaternion.from_euler(e))
		library.add_animation("clip%d" % c, anim)
	return library
