extends "res://bench/scene_base.gd"
## GI ARCADE (built in code, static; milestone 168): fine geometry for judging how stable diffuse global illumination is.
##
## Sponza's curtains and tree are where screen-space GI flickers most (decision 0308): surfaces that change from one
## TAA-jittered frame to the next at a pixel. This scene has the same kinds of geometry, in the cloud (Sponza's assets
## are not):
## * A colonnade (seven columns 3 m apart) in front of a gallery under an upper floor, a back wall 8 m high.
## * Folded, double-sided curtains hung in front of the columns, gathered at the bottom (red, blue, green).
## * A tree of 600 small double-sided leaves in the sunlit courtyard; a shutter of thin slats on the back wall.
## Physical sky and a shadowed sun from the courtyard's side; AgX, a fixed exposure; no SSAO, SSIL, SSR or SDFGI unless
## --set turns them on (env.sdfgi_enabled=true ...).

var sun: DirectionalLight3D
var _mats := {}


func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "gi_arcade"
	_setup_common()
	var sky := Sky.new()
	var sky_mat := PhysicalSkyMaterial.new()
	sky_mat.turbidity = 8.0
	sky_mat.mie_coefficient = 0.004
	sky.sky_material = sky_mat
	sky.radiance_size = Sky.RADIANCE_SIZE_256
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.tonemap_exposure = float(args.get("gi_exposure", "1.0"))
	env.glow_enabled = false
	env.ssao_enabled = false
	env.ssil_enabled = false
	env.ssr_enabled = false
	env.sdfgi_enabled = false
	sun = DirectionalLight3D.new()
	sun.name = "BenchSun"
	# From +z (the courtyard's side), 40 degrees up, a little from +x.
	sun.rotation_degrees = Vector3(-40.0, 25.0, 0.0)
	sun.light_energy = 2.5
	sun.light_color = Color(1.0, 0.95, 0.88)
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 60.0
	sun.light_angular_distance = 0.5
	apply_sun_overrides(sun)
	add_child(sun)

	_mat("ground", Color(0.5, 0.48, 0.44))
	_mat("stone", Color(0.72, 0.68, 0.6))
	_mat("wood", Color(0.35, 0.22, 0.12))
	_mat("red", Color(0.6, 0.05, 0.04), true)
	_mat("blue", Color(0.03, 0.25, 0.45), true)
	_mat("green", Color(0.05, 0.4, 0.1), true)
	_mat("leaf", Color(0.12, 0.3, 0.06), true)

	_box("ground", Vector3(-40, -0.2, -40), Vector3(40, 0.0, 40))
	# The back wall (z -4.3..-4), the upper floor over the gallery (y 4.8..5.4, out to z 0.5), the beam over the columns.
	_box("stone", Vector3(-10.5, 0.0, -4.3), Vector3(10.5, 8.0, -4.0))
	_box("stone", Vector3(-10.5, 4.8, -4.0), Vector3(10.5, 5.4, 0.5))
	_box("stone", Vector3(-10.5, 4.2, -0.3), Vector3(10.5, 4.8, 0.3))
	_box("stone", Vector3(-10.8, 0.0, -4.3), Vector3(-10.5, 8.0, 0.5)) # the end walls
	_box("stone", Vector3(10.5, 0.0, -4.3), Vector3(10.8, 8.0, 0.5))
	var colors := ["red", "blue", "green"]
	for i in range(7):
		var x := -9.0 + 3.0 * i
		_column(Vector3(x, 0.0, 0.0))
	for i in range(6):
		var x := -9.0 + 3.0 * i
		# Two halves per bay, tied back towards their columns.
		_curtain(colors[i % 3], Vector3(x + 0.35, 0.25, 0.45), 1.15, 3.9, 1.0, 1000 + i * 2)
		_curtain(colors[(i + 1) % 3], Vector3(x + 2.65, 0.25, 0.45), 1.15, 3.9, -1.0, 1001 + i * 2)
	# A shutter of thin slats on the back wall, in the gallery's shade.
	for k in range(14):
		var y := 1.0 + 0.12 * k
		_box("wood", Vector3(-4.6, y, -4.0), Vector3(-3.0, y + 0.03, -3.92))
	_box("wood", Vector3(-4.7, 0.95, -4.0), Vector3(-4.6, 2.75, -3.9))
	_box("wood", Vector3(-3.0, 0.95, -4.0), Vector3(-2.9, 2.75, -3.9))
	# The tree in the courtyard.
	_cylinder("wood", Vector3(0.0, 0.0, 7.0), 0.15, 3.2)
	_leaves(Vector3(0.0, 4.0, 7.0), 1.7, 600, 7)

	fixed_cameras = {
		"arcade_along": {"pos": Vector3(11.0, 1.7, 4.5), "target": Vector3(-6.0, 2.0, -0.5), "fov": 65.0, "time": 0.0,
			"purpose": "along the colonnade from the courtyard: the curtains' folds in sun and shade, the columns, the gallery behind"},
		"arcade_gallery": {"pos": Vector3(-8.0, 1.6, -3.0), "target": Vector3(6.0, 1.8, 1.0), "fov": 70.0, "time": 0.0,
			"purpose": "inside the gallery, towards the light through the curtains: their backs lit by the courtyard"},
		"arcade_tree": {"pos": Vector3(6.0, 2.0, 13.0), "target": Vector3(0.0, 3.2, 5.0), "fov": 60.0, "time": 0.0,
			"purpose": "the tree's leaves against the colonnade"},
		"arcade_shutter": {"pos": Vector3(-2.0, 1.6, -0.5), "target": Vector3(-3.8, 1.6, -4.0), "fov": 60.0, "time": 0.0,
			"purpose": "the shutter's slats in the gallery's shade"},
	}
	flythrough = CameraPath.from_array([
		[0.0, Vector3(11.0, 1.7, 4.5), Vector3(-6.0, 2.0, -0.5), 65.0],
		[8.0, Vector3(6.0, 2.0, 13.0), Vector3(0.0, 3.2, 5.0), 60.0],
		[16.0, Vector3(-2.0, 1.6, -0.5), Vector3(-3.8, 1.6, -4.0), 60.0],
		[24.0, Vector3(-8.0, 1.6, -3.0), Vector3(6.0, 1.8, 1.0), 70.0],
		[32.0, Vector3(11.0, 1.7, 4.5), Vector3(-6.0, 2.0, -0.5), 65.0],
	])
	temporal_seqs = {
		"arcade_pan": {"start": 0.0, "frames": 90, "purpose": "a slow pan along the curtains: their indirect light as they come into view",
			"path": CameraPath.from_array([[0.0, Vector3(11.0, 1.7, 4.5), Vector3(-6.0, 2.0, -0.5), 65.0], [1.5, Vector3(10.0, 1.7, 5.0), Vector3(-6.0, 2.0, 2.5), 65.0]])},
	}
	info = {"sun": {"elevation_deg": 40.0, "azimuth_deg": 25.0, "energy": sun.light_energy}}


func _mat(p_name: String, p_color: Color, p_double_sided := false) -> void:
	var m := StandardMaterial3D.new()
	m.albedo_color = p_color
	m.roughness = 0.9
	if p_double_sided:
		m.cull_mode = BaseMaterial3D.CULL_DISABLED
	_mats[p_name] = m


func _box(p_mat: String, p_from: Vector3, p_to: Vector3) -> void:
	var mi := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = p_to - p_from
	mi.mesh = bm
	mi.material_override = _mats[p_mat]
	mi.position = (p_from + p_to) * 0.5
	add_child(mi)


func _cylinder(p_mat: String, p_base: Vector3, p_radius: float, p_height: float) -> void:
	var mi := MeshInstance3D.new()
	var cm := CylinderMesh.new()
	cm.top_radius = p_radius
	cm.bottom_radius = p_radius
	cm.height = p_height
	cm.radial_segments = 24
	mi.mesh = cm
	mi.material_override = _mats[p_mat]
	mi.position = p_base + Vector3(0.0, p_height * 0.5, 0.0)
	add_child(mi)


func _column(p_base: Vector3) -> void:
	_box("stone", p_base + Vector3(-0.42, 0.0, -0.42), p_base + Vector3(0.42, 0.3, 0.42)) # the base
	_cylinder("stone", p_base + Vector3(0.0, 0.3, 0.0), 0.3, 3.65)
	_box("stone", p_base + Vector3(-0.45, 3.95, -0.45), p_base + Vector3(0.45, 4.2, 0.45)) # the capital


## A curtain p_width wide at the top, p_height long above p_anchor (its outer edge, by its column), folded (the folds
## deeper towards the bottom) and gathered towards that column; p_side +1 hangs it towards +x, -1 towards -x.
func _curtain(p_mat: String, p_anchor: Vector3, p_width: float, p_height: float, p_side: float, p_seed: int) -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = p_seed
	var folds := 7.0 + rng.randf() * 2.0
	var phase := rng.randf() * TAU
	var nu := 120
	var nv := 48
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var top := p_anchor.y + p_height
	var verts := []
	for j in range(nv + 1):
		var v := float(j) / float(nv) # 0 at the top
		# Gathered towards the column below 55 % of its length, most at 80 %, a little looser at the hem.
		var gather := smoothstep(0.35, 0.8, v) * (1.0 - 0.3 * smoothstep(0.85, 1.0, v))
		var width := p_width * (1.0 - 0.65 * gather)
		var amplitude := 0.035 + 0.09 * v
		for i in range(nu + 1):
			var u := float(i) / float(nu)
			# The anchor is the curtain's outer edge (by its column); u = 1 the inner edge, where it hangs free.
			var x := p_anchor.x + p_side * u * width
			var z := p_anchor.z + amplitude * sin(TAU * folds * u * (1.0 + 0.25 * gather) + phase) + 0.05 * v * v
			verts.append(Vector3(x, top - v * p_height, z))
	for j in range(nv):
		for i in range(nu):
			var a: Vector3 = verts[j * (nu + 1) + i]
			var b: Vector3 = verts[j * (nu + 1) + i + 1]
			var c: Vector3 = verts[(j + 1) * (nu + 1) + i]
			var d: Vector3 = verts[(j + 1) * (nu + 1) + i + 1]
			for p in [a, b, c, b, d, c]:
				st.add_vertex(p)
	st.index()
	st.generate_normals()
	var mi := MeshInstance3D.new()
	mi.mesh = st.commit()
	mi.material_override = _mats[p_mat]
	add_child(mi)


## p_count leaves (triangles about 15 cm across, both sides) scattered through a ball around p_center.
func _leaves(p_center: Vector3, p_radius: float, p_count: int, p_seed: int) -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = p_seed
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for k in range(p_count):
		var dir := Vector3(rng.randf_range(-1, 1), rng.randf_range(-1, 1), rng.randf_range(-1, 1))
		if dir.length() > 1.0:
			dir = dir.normalized()
		var c := p_center + dir * p_radius * Vector3(1.0, 0.75, 1.0)
		var n := Vector3(rng.randf_range(-1, 1), rng.randf_range(-0.3, 1), rng.randf_range(-1, 1)).normalized()
		var t := n.cross(Vector3(0.3, 0.9, 0.2).normalized()).normalized()
		var b := n.cross(t)
		var s := rng.randf_range(0.1, 0.2)
		var p0 := c + t * s
		var p1 := c - t * s * 0.5 + b * s * 0.8
		var p2 := c - t * s * 0.5 - b * s * 0.8
		st.set_normal(n)
		st.add_vertex(p0)
		st.set_normal(n)
		st.add_vertex(p1)
		st.set_normal(n)
		st.add_vertex(p2)
	var mi := MeshInstance3D.new()
	mi.mesh = st.commit()
	mi.material_override = _mats["leaf"]
	add_child(mi)
