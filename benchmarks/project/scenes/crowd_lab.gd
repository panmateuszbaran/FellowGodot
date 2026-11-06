extends "res://bench/scene_base.gd"
## CROWD LAB (built in code, deterministic, driven only through set_time()): skinned characters.
##
## No other benchmark scene has skeletal animation. Games draw crowds of animated characters, and every one of them
## costs work every frame on the main thread (its AnimationPlayer evaluates its clip's tracks and poses the
## Skeleton3D, the skeleton computes its bones' global transforms and hands them to the RenderingServer), on the render
## thread (each skeleton's bone buffer, each deformed instance's bounds) and on the GPU (a skinning dispatch per
## instance and surface, double-buffered for motion vectors, then the depth, colour and shadow passes of meshes that
## are new every frame).
##
## CROWD characters (--crowd=N, default 256), each a procedural humanoid of 52 bones (spine, head, arms with five
## three-bone fingers, legs) and 4992 vertices / 8736 triangles, all sharing one mesh and one Skin. Each has its own
## Skeleton3D and AnimationPlayer playing one of four looping clips (walk, run, idle, wave; every bone has a rotation
## track) at its own phase, and walks along its own lane. Players use the manual process callback: set_time() seeks
## each to its time with an update, which evaluates and applies the clip exactly as the idle process would.
## --crowd_blend=1: instead, an AnimationTree per character blends idle, walk and run (a BlendSpace1D whose position
## set_time() moves), advanced by set_time()'s step: two clips with fractional weights per character.
## --crowd_groups=N (milestone 60): the characters split into N process thread groups (Node.PROCESS_THREAD_GROUP_SUB_THREAD,
## crowd_group.gd), each seeking its characters in its own _process() on a worker thread; their skeletons update on the
## same thread (deferred thread-group notifications). The same poses as without groups.

const CROWD := 256
const SPACING := 2.4
const RINGS := 8
const RADIAL := 12
const KEY_FPS := 20.0

var sun: DirectionalLight3D
var crowd := 256
var blend := false # --crowd_blend=1: an AnimationTree per character blending idle, walk and run (BlendSpace1D)
var _trees: Array[AnimationTree] = []
var _last_t: PackedFloat32Array = PackedFloat32Array()
var _chars: Array[Node3D] = []
var _players: Array[AnimationPlayer] = []
var _clip_of: PackedInt32Array = PackedInt32Array()
var _phase: PackedFloat32Array = PackedFloat32Array()
var _lane: PackedFloat32Array = PackedFloat32Array()
var _start: PackedFloat32Array = PackedFloat32Array()
var _dir: PackedFloat32Array = PackedFloat32Array()
var _area := 60.0
var groups := 0 # --crowd_groups=N: the characters processed in N sub-thread process groups
var _groups: Array[Node3D] = []
var _t := 0.0

# Bone table: name, parent index, head and tail (skeleton space, rest pose), radius.
var _bone_names: PackedStringArray = PackedStringArray()
var _bone_parent: PackedInt32Array = PackedInt32Array()
var _bone_head: PackedVector3Array = PackedVector3Array()
var _bone_tail: PackedVector3Array = PackedVector3Array()
var _bone_radius: PackedFloat32Array = PackedFloat32Array()
var _bone_global: Array[Transform3D] = []
var _bone_rest: Array[Transform3D] = []

var _mesh: ArrayMesh
var _skin: Skin
var _library: AnimationLibrary
const CLIPS := ["walk", "run", "idle", "wave"]
const CLIP_LENGTH := [1.0, 0.6, 3.0, 1.2]
const CLIP_SPEED := [1.4, 3.6, 0.0, 0.0] # metres per second along the lane


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "crowd"
	crowd = int(args.get("crowd", str(CROWD)))
	blend = str(args.get("crowd_blend", "0")) == "1"
	groups = int(args.get("crowd_groups", "0"))
	_area = maxf(20.0, sqrt(float(crowd)) * SPACING * 1.6)
	_setup_common()
	_setup_environment()
	_define_bones()
	_build_mesh()
	_build_clips()
	_build_ground()
	_build_crowd()
	_build_lights()
	_define_cameras()
	set_time(0.0)
	info = {"stats": scene_stats(self), "characters": crowd, "bones": _bone_names.size(),
		"vertices_per_character": _bone_names.size() * RINGS * RADIAL,
		"notes": "Skinned crowd: one AnimationPlayer + Skeleton3D per character (52 bones, 4992 vertices), manual seek per frame, sun with 4 cascades."}


func _setup_environment() -> void:
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.ssao_enabled = true
	env.glow_enabled = true
	cam_attr.auto_exposure_enabled = false


func _add_bone(p_name: String, p_parent: int, p_head: Vector3, p_tail: Vector3, p_radius: float) -> int:
	_bone_names.append(p_name)
	_bone_parent.append(p_parent)
	_bone_head.append(p_head)
	_bone_tail.append(p_tail)
	_bone_radius.append(p_radius)
	return _bone_names.size() - 1


func _define_bones() -> void:
	var hips := _add_bone("hips", -1, Vector3(0, 0.95, 0), Vector3(0, 1.05, 0), 0.14)
	var s1 := _add_bone("spine1", hips, Vector3(0, 1.05, 0), Vector3(0, 1.2, 0), 0.13)
	var s2 := _add_bone("spine2", s1, Vector3(0, 1.2, 0), Vector3(0, 1.35, 0), 0.14)
	var s3 := _add_bone("spine3", s2, Vector3(0, 1.35, 0), Vector3(0, 1.48, 0), 0.15)
	var neck := _add_bone("neck", s3, Vector3(0, 1.48, 0), Vector3(0, 1.58, 0), 0.06)
	_add_bone("head", neck, Vector3(0, 1.58, 0), Vector3(0, 1.8, 0), 0.1)
	for side in [-1.0, 1.0]:
		var sfx := "_l" if side < 0.0 else "_r"
		var cl := _add_bone("clavicle" + sfx, s3, Vector3(0.03 * side, 1.45, 0), Vector3(0.17 * side, 1.46, 0), 0.05)
		var ua := _add_bone("upper_arm" + sfx, cl, Vector3(0.17 * side, 1.46, 0), Vector3(0.45 * side, 1.46, 0), 0.05)
		var la := _add_bone("lower_arm" + sfx, ua, Vector3(0.45 * side, 1.46, 0), Vector3(0.7 * side, 1.46, 0), 0.04)
		var hand := _add_bone("hand" + sfx, la, Vector3(0.7 * side, 1.46, 0), Vector3(0.8 * side, 1.46, 0), 0.035)
		for f in 5:
			var z := (float(f) - 2.0) * 0.016
			var parent := hand
			var x0 := 0.8
			for p in 3:
				var x1 := x0 + 0.03 - 0.005 * p
				parent = _add_bone("finger%d_%d%s" % [f, p, sfx], parent, Vector3(x0 * side, 1.46, z), Vector3(x1 * side, 1.46, z), 0.011)
				x0 = x1
	for side in [-1.0, 1.0]:
		var sfx := "_l" if side < 0.0 else "_r"
		var ul := _add_bone("upper_leg" + sfx, hips, Vector3(0.1 * side, 0.93, 0), Vector3(0.1 * side, 0.5, 0), 0.08)
		var ll := _add_bone("lower_leg" + sfx, ul, Vector3(0.1 * side, 0.5, 0), Vector3(0.1 * side, 0.08, 0), 0.06)
		var foot := _add_bone("foot" + sfx, ll, Vector3(0.1 * side, 0.08, 0), Vector3(0.1 * side, 0.03, 0.12), 0.045)
		_add_bone("toe" + sfx, foot, Vector3(0.1 * side, 0.03, 0.12), Vector3(0.1 * side, 0.03, 0.2), 0.03)
	# Rest transforms: each bone at its head, its +Y towards its tail.
	for i in _bone_names.size():
		var y := (_bone_tail[i] - _bone_head[i]).normalized()
		var ref := Vector3.FORWARD if absf(y.dot(Vector3.FORWARD)) < 0.9 else Vector3.RIGHT
		var x := y.cross(ref).normalized()
		var z := x.cross(y).normalized()
		var g := Transform3D(Basis(x, y, z), _bone_head[i])
		_bone_global.append(g)
		var p := _bone_parent[i]
		_bone_rest.append(g if p < 0 else _bone_global[p].affine_inverse() * g)


func _build_mesh() -> void:
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var uvs := PackedVector2Array()
	var bones := PackedInt32Array()
	var weights := PackedFloat32Array()
	var indices := PackedInt32Array()
	for b in _bone_names.size():
		var g := _bone_global[b]
		var length := (_bone_tail[b] - _bone_head[b]).length()
		var base := verts.size()
		for r in RINGS:
			var u := float(r) / float(RINGS - 1)
			var radius := _bone_radius[b] * (0.35 + 0.65 * sin(PI * (0.08 + 0.84 * u)))
			for s in RADIAL:
				var a := TAU * float(s) / float(RADIAL)
				var local_n := Vector3(cos(a), 0.0, sin(a))
				verts.append(g * (local_n * radius + Vector3(0.0, u * length, 0.0)))
				normals.append((g.basis * local_n).normalized())
				uvs.append(Vector2(float(s) / float(RADIAL), u))
				var p := _bone_parent[b]
				var w_parent := 0.0 if p < 0 else clampf(0.5 * (1.0 - u / 0.3), 0.0, 0.5)
				bones.append_array(PackedInt32Array([b, maxi(p, 0), 0, 0]))
				weights.append_array(PackedFloat32Array([1.0 - w_parent, w_parent, 0.0, 0.0]))
		for r in RINGS - 1:
			for s in RADIAL:
				var s2 := (s + 1) % RADIAL
				var i0 := base + r * RADIAL + s
				var i1 := base + r * RADIAL + s2
				var i2 := base + (r + 1) * RADIAL + s
				var i3 := base + (r + 1) * RADIAL + s2
				# Clockwise seen from outside (Godot's front faces).
				indices.append_array(PackedInt32Array([i0, i1, i2, i1, i3, i2]))
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = verts
	arr[Mesh.ARRAY_NORMAL] = normals
	arr[Mesh.ARRAY_TEX_UV] = uvs
	arr[Mesh.ARRAY_BONES] = bones
	arr[Mesh.ARRAY_WEIGHTS] = weights
	arr[Mesh.ARRAY_INDEX] = indices
	_mesh = ArrayMesh.new()
	_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.62, 0.5, 0.42)
	mat.roughness = 0.7
	_mesh.surface_set_material(0, mat)
	_skin = Skin.new()
	for b in _bone_names.size():
		_skin.add_named_bind(_bone_names[b], _bone_global[b].affine_inverse())


func _swing(p_clip: int, p_bone: String, t: float) -> Vector3:
	# Rotation (Euler XYZ, radians) added to the rest pose of p_bone at t seconds into clip p_clip.
	var w: float = TAU / float(CLIP_LENGTH[p_clip])
	var side := -1.0 if p_bone.ends_with("_l") else 1.0
	var amp: float = [1.0, 1.6, 0.15, 0.3][p_clip]
	if p_clip == 3 and p_bone.ends_with("_r"):
		# Wave: the right arm raised and waving.
		if p_bone.begins_with("upper_arm"):
			return Vector3(0.0, 0.0, 1.9 + 0.1 * sin(w * t))
		if p_bone.begins_with("lower_arm"):
			return Vector3(0.0, 0.0, 0.6 + 0.45 * sin(w * t))
		if p_bone.begins_with("hand"):
			return Vector3(0.0, 0.0, 0.3 * sin(w * t + 0.5))
	if p_bone.begins_with("upper_leg"):
		return Vector3(0.45 * amp * sin(w * t) * side, 0.0, 0.0)
	if p_bone.begins_with("lower_leg"):
		return Vector3(0.55 * amp * maxf(0.0, sin(w * t * side + 1.0)), 0.0, 0.0)
	if p_bone.begins_with("foot"):
		return Vector3(0.2 * amp * sin(w * t * side + 2.0), 0.0, 0.0)
	if p_bone.begins_with("toe"):
		return Vector3(0.15 * amp * maxf(0.0, sin(w * t * side + 2.5)), 0.0, 0.0)
	if p_bone.begins_with("upper_arm"):
		return Vector3(0.35 * amp * sin(w * t + PI) * side, 0.0, -0.9)
	if p_bone.begins_with("lower_arm"):
		return Vector3(0.0, 0.25 * amp * (1.0 + sin(w * t + PI)), 0.0)
	if p_bone.begins_with("clavicle") or p_bone.begins_with("hand"):
		return Vector3(0.05 * amp * sin(w * t), 0.0, 0.03 * sin(w * t + side))
	if p_bone.begins_with("finger"):
		var p := float(p_bone.substr(8, 1).to_int())
		return Vector3(0.0, 0.0, (0.25 + 0.15 * sin(w * t + p)) * side)
	if p_bone.begins_with("spine"):
		return Vector3(0.03 * amp * sin(2.0 * w * t), 0.06 * amp * sin(w * t), 0.0)
	if p_bone == "neck" or p_bone == "head":
		return Vector3(0.05 * sin(w * t * 0.5), 0.08 * sin(w * t * 0.25), 0.0)
	if p_bone == "hips":
		return Vector3(0.0, 0.08 * amp * sin(w * t), 0.04 * amp * sin(w * t))
	return Vector3.ZERO


func _build_clips() -> void:
	_library = AnimationLibrary.new()
	for c in CLIPS.size():
		var anim := Animation.new()
		var length: float = CLIP_LENGTH[c]
		anim.length = length
		anim.loop_mode = Animation.LOOP_LINEAR
		var keys := int(round(length * KEY_FPS))
		for b in _bone_names.size():
			var track := anim.add_track(Animation.TYPE_ROTATION_3D)
			anim.track_set_path(track, NodePath("Skeleton3D:" + _bone_names[b]))
			var rest_q := _bone_rest[b].basis.get_rotation_quaternion()
			for k in keys:
				var t := length * float(k) / float(keys)
				var e := _swing(c, _bone_names[b], t)
				anim.rotation_track_insert_key(track, t, rest_q * Quaternion.from_euler(e))
		_library.add_animation(CLIPS[c], anim)


func _build_ground() -> void:
	var plane := PlaneMesh.new()
	plane.size = Vector2(_area * 2.5, _area * 2.5)
	var mi := MeshInstance3D.new()
	mi.mesh = plane
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(0.42, 0.43, 0.4)
	m.roughness = 0.9
	mi.material_override = m
	add_child(mi)


func _build_crowd() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 23
	var side := int(ceil(sqrt(float(crowd))))
	for g in groups:
		var gn := Node3D.new()
		gn.name = "group_%d" % g
		gn.set_script(load("res://scenes/crowd_group.gd"))
		gn.set("crowd", self)
		gn.set("first", g * crowd / groups)
		gn.set("last", (g + 1) * crowd / groups)
		gn.process_thread_group = Node.PROCESS_THREAD_GROUP_SUB_THREAD
		gn.process_thread_group_order = 1 # after the main thread's group, which sets the time
		add_child(gn)
		_groups.append(gn)
	for i in crowd:
		var parent: Node = _groups[i * groups / crowd] if groups > 0 else self
		var root := Node3D.new()
		root.name = "character_%d" % i
		var sk := Skeleton3D.new()
		sk.name = "Skeleton3D"
		for b in _bone_names.size():
			sk.add_bone(_bone_names[b])
		for b in _bone_names.size():
			sk.set_bone_parent(b, _bone_parent[b])
			sk.set_bone_rest(b, _bone_rest[b])
		sk.reset_bone_poses()
		root.add_child(sk)
		var mi := MeshInstance3D.new()
		mi.mesh = _mesh
		mi.skin = _skin
		sk.add_child(mi)
		mi.skeleton = NodePath("..")
		var clip := i % CLIPS.size()
		var phase := rng.randf_range(0.0, 3.0)
		if blend:
			# Locomotion as games do it: idle, walk and run blended by speed; two clips weigh in at once.
			var at := AnimationTree.new()
			at.name = "AnimationTree"
			root.add_child(at)
			at.root_node = NodePath("..")
			at.add_animation_library("", _library)
			var bs := AnimationNodeBlendSpace1D.new()
			bs.min_space = 0.0
			bs.max_space = 2.0
			var names := ["idle", "walk", "run"]
			for j in 3:
				var an := AnimationNodeAnimation.new()
				an.animation = names[j]
				bs.add_blend_point(an, float(j))
			at.tree_root = bs
			at.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
			parent.add_child(root)
			at.active = true
			_trees.append(at)
			_last_t.append(-phase)
		else:
			var ap := AnimationPlayer.new()
			ap.name = "AnimationPlayer"
			root.add_child(ap)
			ap.root_node = NodePath("..")
			ap.add_animation_library("", _library)
			ap.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
			parent.add_child(root)
			ap.play(CLIPS[clip])
			_players.append(ap)
		_chars.append(root)
		_clip_of.append(clip)
		_phase.append(phase)
		var gx := float(i % side) - float(side - 1) * 0.5
		var gz := float(i / side) - float(side - 1) * 0.5
		_lane.append(gz * SPACING)
		_start.append(gx * SPACING + rng.randf_range(-0.5, 0.5))
		_dir.append(1.0 if (i / side) % 2 == 0 else -1.0)


func _build_lights() -> void:
	sun = DirectionalLight3D.new()
	sun.name = "sun"
	sun.light_energy = 2.0
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 80.0
	sun.light_angular_distance = 0.5
	apply_sun_overrides(sun)
	add_child(sun)
	sun.rotation_degrees = Vector3(-55.0, -30.0, 0.0)


func _define_cameras() -> void:
	var h := _area * 0.5
	fixed_cameras = {
		"crowd_aerial": {"pos": Vector3(-h, h * 0.8, h), "target": Vector3(0, 0, 0), "fov": 60.0, "time": 2.0, "purpose": "every character in view, small on screen"},
		"crowd_street": {"pos": Vector3(1.2, 1.7, h * 0.45), "target": Vector3(0, 1.2, -h), "fov": 65.0, "time": 2.0, "purpose": "eye level inside the crowd: near characters large, far ones small"},
		"crowd_close": {"pos": Vector3(0.6, 1.5, 3.0), "target": Vector3(0, 1.1, 0), "fov": 50.0, "time": 2.0, "purpose": "a few characters filling the screen"},
	}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(-h, h * 0.8, h), Vector3(0, 0, 0), 60.0],
		[8.0, Vector3(h * 0.6, h * 0.35, h * 0.6), Vector3(0, 0.5, 0), 60.0],
		[16.0, Vector3(1.2, 1.7, h * 0.45), Vector3(0, 1.2, -h), 65.0],
		[22.0, Vector3(0.6, 1.5, 3.0), Vector3(0, 1.1, 0), 50.0],
		[30.0, Vector3(h, h * 0.5, -h), Vector3(0, 0, 0), 60.0],
	])
	temporal_seqs = {
		"crowd_pan": {"start": 3.0, "frames": 60, "purpose": "pan across the crowd at eye level, characters walking",
			"path": CameraPath.from_array([[0.0, Vector3(-6, 1.7, 8), Vector3(-2, 1.2, -6), 60.0], [1.0, Vector3(6, 1.7, 8), Vector3(2, 1.2, -6), 60.0]])},
	}


func set_time(t: float) -> void:
	_t = t
	if _groups.is_empty():
		apply_time(0, _chars.size())


## Poses characters [p_from, p_to) at the time set_time() gave: from set_time() itself, or from the characters' process
## group on its worker thread (--crowd_groups).
func apply_time(p_from: int, p_to: int) -> void:
	var t := _t
	var span := _area * 1.2
	for i in range(p_from, p_to):
		var clip := _clip_of[i]
		var speed: float = CLIP_SPEED[clip] if not blend else 1.4
		var x := fposmod(_start[i] + _dir[i] * speed * t + span * 0.5, span) - span * 0.5
		var root := _chars[i]
		root.position = Vector3(x, 0.0, _lane[i])
		root.rotation = Vector3(0.0, PI * 0.5 * _dir[i], 0.0)
		if blend:
			var at := _trees[i]
			at.set("parameters/blend_position", 1.0 + sin(0.3 * t + _phase[i] * 2.0))
			at.advance(t - _last_t[i])
			_last_t[i] = t
		else:
			var ap := _players[i]
			ap.seek(fposmod(t + _phase[i], float(CLIP_LENGTH[clip])), true)
