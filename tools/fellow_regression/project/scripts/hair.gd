extends Node3D
## FellowEngine's image regression suite: FellowHair3D (milestone 124). A head (a sphere on a neck, its scalp painted
## dark) with hair 0.32 m long falling onto the shoulders (a capsule it collides with), seen from the front and a side;
## the head turns a little as the capture runs, so the strands swing.


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.65
	return m


var head: Node3D
var t := 0.0


func _ready() -> void:
	head = Node3D.new()
	head.position = Vector3(0, 1.6, 0)
	add_child(head)
	var skull := MeshInstance3D.new()
	var sm := SphereMesh.new()
	sm.radius = 0.1
	sm.height = 0.23
	skull.mesh = sm
	var scalp := ShaderMaterial.new()
	scalp.shader = Shader.new()
	scalp.shader.code = "shader_type spatial;\nvarying vec3 n;\nvoid vertex() { n = NORMAL; }\nvoid fragment() { float h = smoothstep(-0.2, 0.05, n.y) * (1.0 - smoothstep(0.55, 0.75, -n.z)); ALBEDO = mix(vec3(0.78, 0.6, 0.5), vec3(0.06, 0.04, 0.03), h); ROUGHNESS = 0.6; }"
	skull.set_surface_override_material(0, scalp)
	head.add_child(skull)
	var neck := MeshInstance3D.new()
	var cm := CylinderMesh.new()
	cm.top_radius = 0.05
	cm.bottom_radius = 0.06
	cm.height = 0.2
	neck.mesh = cm
	neck.position = Vector3(0, -0.17, 0)
	neck.set_surface_override_material(0, _material(Color(0.78, 0.6, 0.5)))
	head.add_child(neck)
	var shoulders := MeshInstance3D.new()
	var bm := CapsuleMesh.new()
	bm.radius = 0.11
	bm.height = 0.5
	shoulders.mesh = bm
	shoulders.rotation_degrees = Vector3(0, 0, 90)
	shoulders.position = Vector3(0, 1.35, 0)
	shoulders.set_surface_override_material(0, _material(Color(0.2, 0.25, 0.35)))
	add_child(shoulders)
	var body := StaticBody3D.new()
	var bcs := CollisionShape3D.new()
	var cap := CapsuleShape3D.new()
	cap.radius = 0.11
	cap.height = 0.5
	bcs.shape = cap
	bcs.rotation_degrees = Vector3(0, 0, 90)
	bcs.position = Vector3(0, 1.35, 0)
	body.add_child(bcs)
	add_child(body)
	var hair := FellowHair3D.new()
	hair.scalp_center = Vector3(0, 0.012, 0)
	hair.strand_count = 8000
	head.add_child(hair)
	hair.colliders = [hair.get_path_to(bcs)]


func _physics_process(delta: float) -> void:
	t += delta
	head.rotation.y = deg_to_rad(25.0) * sin(t * 2.0)
