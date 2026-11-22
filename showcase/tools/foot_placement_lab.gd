extends Node3D
## FellowEngine's foot placement in pictures (milestone 174, decision 0316): the rooftops' mannequin, its clips made on
## flat ground, standing across a 25° roof and stepping up a stair, without FellowFootPlacement3D and with it. Four
## cameras on the legs: SlopeOff, SlopeOn, StairsOff, StairsOn.
##
##   python tools/fellow_agent/fellow_agent.py capture --project showcase --scene res://tools/foot_placement_lab.gd \
##       --views camera --camera SlopeOn --frames 60 --size 800x800

const SLOPE := 25.0
const STRIDE_TIME := 0.55 # s into the walk: both feet down, the front one flat, the back one on its toes
const PLAYER := "res://rooftops/player.tscn"


func _ready() -> void:
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var sm := ProceduralSkyMaterial.new()
	sm.sky_top_color = Color(0.33, 0.5, 0.75)
	sm.sky_horizon_color = Color(0.72, 0.78, 0.85)
	sm.ground_horizon_color = Color(0.6, 0.55, 0.5)
	sky.sky_material = sm
	e.sky = sky
	e.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	e.ambient_light_energy = 0.8
	e.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.environment = e
	add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-38.0, -62.0, 0.0)
	sun.shadow_enabled = true
	sun.light_energy = 1.6
	add_child(sun)
	var tiles := StandardMaterial3D.new()
	tiles.albedo_color = Color(0.62, 0.3, 0.2)
	tiles.roughness = 0.85
	var stone := StandardMaterial3D.new()
	stone.albedo_color = Color(0.62, 0.6, 0.56)
	stone.roughness = 0.9

	# A strip of roof sloping up to the left of the picture (+x), the mannequins standing across it, facing the camera,
	# seen edge on (the strip's top is the ground's line).
	var roof := Basis(Vector3.BACK, deg_to_rad(SLOPE))
	_solid(Vector3(9.0, 0.3, 0.7), Transform3D(roof, roof * Vector3(0, -0.15, 0)), tiles)
	var a := _mannequin(Vector3(-1.2, 0.0, 0.0), 0.0, false, "idle", 0.0)
	var b := _mannequin(Vector3(1.2, 0.0, 0.0), 0.0, true, "idle", 0.0)
	_camera("SlopeOff", a + Vector3(0.0, 0.5, -2.4), a + Vector3(0.0, 0.45, 0.0))
	_camera("SlopeOn", b + Vector3(0.0, 0.5, -2.4), b + Vector3(0.0, 0.45, 0.0))

	# Garden stairs (0.15 m by 0.34 m) rising the way the mannequins face (-z), the mannequins in mid-stride on them, seen
	# from the side (a strip, edge on).
	for k in 2:
		var z0 := float(k) * 4.0
		_solid(Vector3(0.7, 0.3, 6.0), Transform3D(Basis(), Vector3(40.0, -0.15, z0 + 3.0)), stone)
		for i in 8:
			var h := 0.15 * float(i + 1)
			_solid(Vector3(0.7, h, 0.34), Transform3D(Basis(), Vector3(40.0, h * 0.5, z0 - 0.34 * float(i) - 0.17 + 0.68)), stone)
	var c := _mannequin(Vector3(40.0, 0.0, 0.45), 0.0, false, "walk", STRIDE_TIME)
	var d := _mannequin(Vector3(40.0, 0.0, 4.45), 0.0, true, "walk", STRIDE_TIME)
	_camera("StairsOff", c + Vector3(2.6, 0.55, -0.2), c + Vector3(0.0, 0.5, -0.2))
	_camera("StairsOn", d + Vector3(2.6, 0.55, -0.2), d + Vector3(0.0, 0.5, -0.2))


func _camera(p_name: String, p_from: Vector3, p_at: Vector3) -> void:
	var cam := Camera3D.new()
	cam.name = p_name
	cam.fov = 36.0
	add_child(cam)
	cam.look_at_from_position(p_from, p_at)
	cam.current = p_name == "SlopeOff"


func _solid(p_size: Vector3, p_xform: Transform3D, p_material: Material) -> void:
	var body := StaticBody3D.new()
	var cs := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = p_size
	cs.shape = shape
	body.add_child(cs)
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = p_size
	mi.mesh = bm
	mi.material_override = p_material
	body.add_child(mi)
	body.transform = p_xform
	add_child(body)


## The rooftops' player, its script and traversal left out: its clip played in place (frozen at p_time when it is the
## walk), set on the ground under it, its FellowFootPlacement3D on or off.
func _mannequin(p_at: Vector3, p_yaw: float, p_place: bool, p_clip: String, p_time: float) -> Vector3:
	var p: Node3D = (load(PLAYER) as PackedScene).instantiate()
	p.set_script(null)
	for n in ["Traversal", "AnimationTree", "LookTarget"]:
		var c := p.get_node_or_null(n)
		if c:
			p.remove_child(c)
			c.free()
	var skel: Skeleton3D = p.get_node("Model/Mannequin/Skeleton3D")
	for n in ["TraversalPose", "HeadLook"]:
		var c := skel.get_node_or_null(n)
		if c:
			skel.remove_child(c)
			c.free()
	(skel.get_node("FootLock") as SkeletonModifier3D).influence = 0.0
	(skel.get_node("FootPlacement") as SkeletonModifier3D).influence = 1.0 if p_place else 0.0
	add_child(p)
	# On the ground under its origin (as its capsule would stand).
	var q := PhysicsRayQueryParameters3D.create(p_at + Vector3(0, 5, 0), p_at - Vector3(0, 5, 0), 1)
	var hit := get_world_3d().direct_space_state.intersect_ray(q)
	p.global_position = (hit.position as Vector3) if not hit.is_empty() else p_at
	p.rotation.y = p_yaw
	var ap: AnimationPlayer = p.get_node("AnimationPlayer")
	ap.root_motion_track = NodePath("Mannequin/Skeleton3D:Root")
	ap.play(p_clip)
	ap.seek(p_time, true)
	if p_clip == "walk":
		ap.speed_scale = 0.0
	return p.global_position
