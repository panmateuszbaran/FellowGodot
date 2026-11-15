extends SceneTree
## FellowEngine milestone 122 check (decision 0246): FellowDestructible3D's stress — the bonds between a structural
## object's pieces carry its weight; those it overloads crack, and what no longer reaches the ground falls.
##
##   godot --path benchmarks/project --headless --fixed-fps 60 -s res://bench/stress_check.gd -- --out=<dir>
##   godot --path benchmarks/project --fixed-fps 60 --resolution 1280x720 -s res://bench/stress_check.gd
##       -- --out=<dir> --pictures=1
##
## Lanes (each object prefractured, static, on the ground, 2200 kg/m³), settled with update_structure() or struck:
## * an L — a column 1.2 x 3 x 0.5 m with an arm 0.5 x 0.5 m reaching 2 m out at its top, 36 pieces: strong (5 MPa:
##   nothing cracks; the bonds' forces hold the arm's outer half and the column's top up exactly — a free body's
##   forces and moments summing to zero — and the arm's root stress near a beam's 6M/bh² = 517 kPa), weak (150 kPa:
##   the arm cracks near its root and falls, the column stands — a column 0.5 m wide topples: the arm's moment at its
##   foot is greater than at the arm's root), and without stress (strength 0: as milestone 80);
## * a pillar 0.5 x 3 x 0.5 m in 14 pieces, strong: what its pieces on the ground carry is the rest's weight;
## * a wall 4 x 3 x 0.25 m in 40 pieces with a hole blown in its foot's middle, strong and weak: the strong wall spans
##   the hole, the weak one's part over it cracks and falls;
## * a balcony — a slab 2 x 0.25 x 1.5 m held out 3 m up from a building's wall (another static body), anchored where it
##   touches it (anchor_mode Static Bodies), 24 pieces: strong (5 MPa) holds, weak (300 kPa; a beam's root stress
##   1 035 kPa) breaks off at the wall; with the lowest-point anchoring every piece of the flat slab would stand;
## * a load: a third balcony at 3 MPa holds its own weight, then 4 t at its outer edge (add_load) breaks it off;
## * the cost: a wall 8 x 4 x 0.3 m in 128 pieces, weak (20 kPa), a hole blown in its foot — the blow's stress solves.
## One JSON line: STRESS_CHECK {...}; exit code 1 when a lane did otherwise than expected.

var args := {}
var out_dir := ""
var result := {}
var root3d: Node3D
var lanes := {}
var frame := 0
var pictures := false
var done := false
var cam: Camera3D
var shots := {}
const RHO := 2200.0
const ARM := 2.0
const COL := 1.2


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	out_dir = _arg("out", "user://stress_check")
	DirAccess.make_dir_recursive_absolute(out_dir)
	pictures = _arg("pictures", "0") == "1"
	_build.call_deferred()


func _material(c: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.85
	return m


# A closed prism of a polygon (counter-clockwise in x, y) between z = -depth/2 and depth/2, Godot's front faces
# (clockwise seen from outside).
func _prism(poly: PackedVector2Array, depth: float) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var h := depth * 0.5
	var tris := Geometry2D.triangulate_polygon(poly)
	for i in range(0, tris.size(), 3):
		var a := poly[tris[i]]
		var b := poly[tris[i + 1]]
		var c := poly[tris[i + 2]]
		for v in [a, c, b]: # front, +z
			st.set_uv(v)
			st.add_vertex(Vector3(v.x, v.y, h))
		for v in [a, b, c]: # back, -z
			st.set_uv(v)
			st.add_vertex(Vector3(v.x, v.y, -h))
	for i in poly.size():
		var p := poly[i]
		var q := poly[(i + 1) % poly.size()]
		var pa := Vector3(p.x, p.y, h)
		var pb := Vector3(q.x, q.y, h)
		var pc := Vector3(q.x, q.y, -h)
		var pd := Vector3(p.x, p.y, -h)
		for v in [pa, pb, pc, pa, pc, pd]:
			st.set_uv(Vector2(v.x + v.y, v.z))
			st.add_vertex(v)
	st.generate_normals()
	return st.commit()


func _object(name: String, pos: Vector3, mesh: Mesh, pieces: int, strength: float, color: Color, anchor := FellowDestructible3D.ANCHOR_LOWEST_POINT) -> FellowDestructible3D:
	var body := StaticBody3D.new()
	body.name = name
	body.position = pos
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.set_surface_override_material(0, _material(color))
	body.add_child(mi)
	var cs := CollisionShape3D.new()
	cs.shape = mesh.create_trimesh_shape()
	body.add_child(cs)
	var d := FellowDestructible3D.new()
	d.piece_count = pieces
	d.seed = 11
	d.prefracture = true
	d.structural = true
	d.strength = strength
	d.anchor_mode = anchor
	d.density = RHO
	d.break_impulse = 1e6 # struck only by the check
	d.debris_lifetime = 0.0
	d.interior_material = _material(color.lightened(0.3))
	body.add_child(d)
	root3d.add_child(body)
	return d


func _box(size: Vector3) -> Mesh:
	var m := BoxMesh.new()
	m.size = size
	return m


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
	sun.rotation_degrees = Vector3(-45, 30, 0)
	sun.shadow_enabled = true
	root3d.add_child(sun)
	var ground := StaticBody3D.new()
	var gcs := CollisionShape3D.new()
	gcs.shape = WorldBoundaryShape3D.new()
	ground.add_child(gcs)
	var gmi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(200, 200)
	gmi.mesh = pm
	gmi.material_override = _material(Color(0.4, 0.42, 0.38))
	ground.add_child(gmi)
	root3d.add_child(ground)
	cam = Camera3D.new()
	root3d.add_child(cam)
	cam.current = true
	cam.look_at_from_position(Vector3(13, 7, 25), Vector3(13, 1.5, 0))
	FellowDestructible3D.set_max_pieces_per_frame(0) # every blow at once: its stats read after it
	var l_shape := PackedVector2Array([Vector2(0, 0), Vector2(COL, 0), Vector2(COL, 2.5), Vector2(COL + ARM, 2.5), Vector2(COL + ARM, 3.0), Vector2(0, 3.0)])
	var l_mesh := _prism(l_shape, 0.5)
	lanes["l_strong"] = {"d": _object("LStrong", Vector3(-4, 0, 0), l_mesh, 36, 5000.0, Color(0.7, 0.62, 0.55)), "data": {}}
	lanes["l_weak"] = {"d": _object("LWeak", Vector3(0.5, 0, 0), l_mesh, 36, 150.0, Color(0.62, 0.55, 0.5)), "data": {}}
	lanes["l_off"] = {"d": _object("LOff", Vector3(5.5, 0, 0), l_mesh, 36, 0.0, Color(0.6, 0.6, 0.62)), "data": {}}
	lanes["pillar"] = {"d": _object("Pillar", Vector3(13.5, 1.5, 0), _box(Vector3(0.5, 3, 0.5)), 14, 5000.0, Color(0.62, 0.62, 0.6)), "data": {}}
	lanes["wall_strong"] = {"d": _object("WallStrong", Vector3(17, 1.5, 0), _box(Vector3(4, 3, 0.25)), 40, 5000.0, Color(0.6, 0.55, 0.65)), "data": {}}
	lanes["wall_weak"] = {"d": _object("WallWeak", Vector3(23, 1.5, 0), _box(Vector3(4, 3, 0.25)), 40, 20.0, Color(0.55, 0.6, 0.55)), "data": {}}
	lanes["wall_large"] = {"d": _object("WallLarge", Vector3(15, 2.0, -8), _box(Vector3(8, 4, 0.3)), 128, 20.0, Color(0.6, 0.58, 0.5)), "data": {}}
	# Two balconies on a building's wall (a static box 0.5 m thick, 4 m tall): slabs touching its face at x = 28.
	var building := StaticBody3D.new()
	building.position = Vector3(27.75, 2.0, 0)
	var bcs := CollisionShape3D.new()
	var bshape := BoxShape3D.new()
	bshape.size = Vector3(0.5, 4, 6)
	bcs.shape = bshape
	building.add_child(bcs)
	var bmi := MeshInstance3D.new()
	bmi.mesh = _box(Vector3(0.5, 4, 6))
	bmi.set_surface_override_material(0, _material(Color(0.5, 0.5, 0.52)))
	building.add_child(bmi)
	root3d.add_child(building)
	lanes["balcony_strong"] = {"d": _object("BalconyStrong", Vector3(29, 3.125, -1.5), _box(Vector3(2, 0.25, 1.5)), 24, 5000.0, Color(0.7, 0.66, 0.58), FellowDestructible3D.ANCHOR_STATIC_BODIES), "data": {}}
	lanes["balcony_load"] = {"d": _object("BalconyLoad", Vector3(29, 3.125, 0), _box(Vector3(2, 0.25, 1.4)), 24, 3000.0, Color(0.6, 0.62, 0.56), FellowDestructible3D.ANCHOR_STATIC_BODIES), "data": {}}
	lanes["balcony_weak"] = {"d": _object("BalconyWeak", Vector3(29, 3.125, 1.5), _box(Vector3(2, 0.25, 1.5)), 24, 300.0, Color(0.66, 0.6, 0.55), FellowDestructible3D.ANCHOR_STATIC_BODIES), "data": {}}
	for n in lanes:
		var d: FellowDestructible3D = lanes[n].d
		d.broken.connect(func(pieces: Array) -> void:
			lanes[n].data["breaks"] = lanes[n].data.get("breaks", 0) + 1
			for b in pieces:
				var meshes := 0
				for c in (b as Node).get_children():
					meshes += 1 if c is MeshInstance3D else 0
				if meshes > 1:
					lanes[n].data["chunks"] = lanes[n].data.get("chunks", 0) + 1
					lanes[n].data["chunk_pieces"] = lanes[n].data.get("chunk_pieces", 0) + meshes
			if not lanes[n].has("made"):
				lanes[n]["made"] = []
			lanes[n].made.append_array(pieces))
	physics_frame.connect(_on_frame)


# A free body's balance: the pieces in `inside` (indices), the bonds between them and the rest; the sum of the forces
# on them (the bonds' and their weight) against their weight, and of the moments about their centre of mass against
# weight x span.
func _balance(d: FellowDestructible3D, inside: Array) -> Dictionary:
	var structure := d.get_structure()
	var g: Vector3 = ProjectSettings.get_setting("physics/3d/default_gravity_vector") * float(ProjectSettings.get_setting("physics/3d/default_gravity"))
	var mass := 0.0
	var com := Vector3()
	var span := 0.0
	for k in inside:
		mass += structure[k].mass
		com += structure[k].centre * structure[k].mass
	com /= maxf(mass, 1e-6)
	for k in inside:
		span = maxf(span, (structure[k].centre - com).length())
	var force := g * mass
	var moment := Vector3()
	var crossing := 0
	for bond in d.get_bonds():
		if bond.broken:
			continue
		var a_in: bool = inside.has(bond.a)
		var b_in: bool = inside.has(bond.b)
		if a_in == b_in:
			continue
		crossing += 1
		var f: Vector3 = bond.force if b_in else -bond.force
		var m: Vector3 = bond.moment if b_in else -bond.moment
		force += f
		moment += (bond.centre - com).cross(f) + m
	var weight := mass * g.length()
	return {"pieces": inside.size(), "bonds": crossing, "mass": snappedf(mass, 0.1), "force_error": snappedf(force.length() / maxf(weight, 1e-6), 1e-7),
		"moment_error": snappedf(moment.length() / maxf(weight * maxf(span, 0.1), 1e-6), 1e-7)}


func _settle_data(n: String) -> void:
	var d: FellowDestructible3D = lanes[n].d
	var data: Dictionary = lanes[n].data
	var t0 := Time.get_ticks_usec()
	d.update_structure()
	data["update_usec"] = Time.get_ticks_usec() - t0
	var st := d.get_stats()
	data["settled"] = {"standing": st.standing, "cracked": st.last_cracked, "fallen": st.last_fallen, "chunks": st.last_chunks, "bonds": st.bonds,
		"stress_usec": st.stress_usec, "stress_iterations": st.stress_iterations}
	var max_ratio := 0.0
	for bond in d.get_bonds():
		max_ratio = maxf(max_ratio, bond.ratio)
	data["max_ratio"] = snappedf(max_ratio, 0.001)


func _l_data(n: String, x0: float) -> void:
	var d: FellowDestructible3D = lanes[n].d
	var data: Dictionary = lanes[n].data
	var structure := d.get_structure()
	# The arm's outer half (its pieces' centres past the arm's middle), and the column's top third with the arm.
	var outer := []
	var upper := []
	for k in structure.size():
		var c: Vector3 = structure[k].centre
		if not structure[k].standing:
			continue
		if c.y > 2.4 and c.x > x0 + COL + ARM * 0.5:
			outer.append(k)
		if c.y > 2.0:
			upper.append(k)
	data["balance_outer"] = _balance(d, outer)
	data["balance_upper"] = _balance(d, upper)
	# The arm's root: the greatest tension in the bonds within 0.4 m of the column's face, against a beam's.
	var root_tension := 0.0
	var root_compression := 0.0
	for bond in d.get_bonds():
		var c: Vector3 = bond.centre
		if c.y > 2.4 and c.x > x0 + COL - 0.1 and c.x < x0 + COL + 0.4:
			root_tension = maxf(root_tension, bond.tension)
			root_compression = maxf(root_compression, bond.compression)
	var g := float(ProjectSettings.get_setting("physics/3d/default_gravity"))
	data["root_tension_kpa"] = snappedf(root_tension, 0.1)
	data["root_compression_kpa"] = snappedf(root_compression, 0.1)
	data["beam_root_kpa"] = snappedf(3.0 * RHO * g * ARM * ARM / 0.5 / 1000.0, 0.1)


func _on_frame() -> void:
	if done:
		return
	frame += 1
	if frame == 10:
		for n in ["l_strong", "l_weak", "l_off", "pillar", "balcony_strong", "balcony_weak", "balcony_load"]:
			_settle_data(n)
		for n in ["balcony_strong", "balcony_weak"]:
			var anchored := 0
			for p in (lanes[n].d as FellowDestructible3D).get_structure():
				anchored += 1 if p.anchored else 0
			lanes[n].data["anchored"] = anchored
			var root_tension := 0.0
			for bond in (lanes[n].d as FellowDestructible3D).get_bonds():
				if bond.centre.x < 28.5:
					root_tension = maxf(root_tension, bond.tension)
			lanes[n].data["root_tension_kpa"] = snappedf(root_tension, 0.1)
		lanes.balcony_strong.data["beam_root_kpa"] = snappedf(3.0 * RHO * float(ProjectSettings.get_setting("physics/3d/default_gravity")) * 4.0 / 0.25 / 1000.0, 0.1)
		_l_data("l_strong", -4.0)
		# The pillar: what its pieces on the ground carry (the bonds from them) against the rest's weight.
		var d: FellowDestructible3D = lanes.pillar.d
		var structure := d.get_structure()
		var free := []
		for k in structure.size():
			if structure[k].standing and not structure[k].anchored:
				free.append(k)
		lanes.pillar.data["balance_free"] = _balance(d, free)
		var max_compression := 0.0
		for bond in d.get_bonds():
			max_compression = maxf(max_compression, bond.compression)
		lanes.pillar.data["max_compression_kpa"] = snappedf(max_compression, 0.1)
		lanes.pillar.data["column_base_kpa"] = snappedf(RHO * float(ProjectSettings.get_setting("physics/3d/default_gravity")) * 3.0 / 1000.0, 0.1)
	if frame == 20:
		# 4 t at the third balcony's outer edge.
		var bl: FellowDestructible3D = lanes.balcony_load.d
		bl.add_load(Vector3(29.9, 3.25, 0), Vector3(0, -39200, 0))
		bl.update_structure()
		var st := bl.get_stats()
		lanes.balcony_load.data["loaded"] = {"cracked": st.last_cracked, "fallen": st.last_fallen, "chunks": st.last_chunks, "standing": st.standing}
	if frame == 30:
		# The walls: a hole blown in the middle of their foot.
		for n in ["wall_strong", "wall_weak", "wall_large"]:
			var w: FellowDestructible3D = lanes[n].d
			w.break_radius = 0.9
			var at: Vector3 = (w.get_parent() as Node3D).global_position
			var t0 := Time.get_ticks_usec()
			w.break_apart(Vector3(at.x, 0.45, at.z + 0.12), Vector3(0, 0, -2500))
			var st := w.get_stats()
			lanes[n].data["blow"] = {"usec": Time.get_ticks_usec() - t0, "broken": st.last_broken, "cracked": st.last_cracked, "fallen": st.last_fallen,
				"chunks": st.last_chunks, "standing": st.standing, "stress_usec": st.stress_usec, "stress_iterations": st.stress_iterations}
	if frame > 30 and lanes.has("wall_large") and is_instance_valid(lanes.wall_large.d):
		# The collapse's frames: the most a frame's stress solves took, and how many frames it went on.
		var st: Dictionary = (lanes.wall_large.d as FellowDestructible3D).get_stats()
		var data: Dictionary = lanes.wall_large.data
		data["frame_stress_usec_max"] = maxi(data.get("frame_stress_usec_max", 0), st.stress_usec)
		data["frame_stress_work_max"] = maxi(data.get("frame_stress_work_max", 0), st.stress_work)
		if st.settling:
			data["settling_frames"] = data.get("settling_frames", 0) + 1
		data["cracked_total"] = st.cracked_total
		data["fallen_total"] = st.fallen_total
	if pictures and frame in [9, 12, 45, 240]:
		shots[frame] = get_root().get_texture().get_image()
	if frame == 240:
		for n in lanes:
			var d: Variant = lanes[n].d
			lanes[n].data["standing_end"] = d.get_standing_pieces() if is_instance_valid(d) else 0
			if is_instance_valid(d):
				lanes[n].data["cracked_total"] = d.get_stats().cracked_total
				lanes[n].data["fallen_total"] = d.get_stats().fallen_total
			var rest := []
			for b in lanes[n].get("made", []):
				if is_instance_valid(b) and (b as Node).name.contains("Chunk"):
					rest.append(snappedf((b as Node3D).global_position.y, 0.01))
			lanes[n].data["chunk_rest_y"] = rest
		done = true
		_finish.call_deferred()


func _finish() -> void:
	var failures := []
	var out := {}
	for n in lanes:
		var data: Dictionary = lanes[n].data
		var ok := false
		var s: Dictionary = data.get("settled", {})
		match n:
			"l_strong":
				var bo: Dictionary = data.get("balance_outer", {})
				var bu: Dictionary = data.get("balance_upper", {})
				var ratio: float = data.get("root_tension_kpa", 0.0) / maxf(data.get("beam_root_kpa", 1.0), 1e-6)
				ok = s.get("cracked", -1) == 0 and s.get("fallen", -1) == 0 and bo.get("pieces", 0) >= 2 and bo.get("force_error", 1.0) < 1e-3 and bo.get("moment_error", 1.0) < 1e-3 \
						and bu.get("force_error", 1.0) < 1e-3 and bu.get("moment_error", 1.0) < 1e-3 and ratio > 0.5 and ratio < 4.0
				data["root_over_beam"] = snappedf(ratio, 0.01)
			"l_weak":
				ok = s.get("cracked", 0) >= 1 and s.get("chunks", 0) >= 1 and s.get("fallen", 0) >= 3 and data.get("standing_end", 0) >= 18 and data.get("chunk_rest_y", []).size() >= 1 \
						and data.chunk_rest_y.all(func(y): return y < 1.5)
			"l_off":
				ok = s.get("cracked", -1) == 0 and s.get("fallen", -1) == 0 and data.get("standing_end", 0) == 36
			"pillar":
				var bf: Dictionary = data.get("balance_free", {})
				ok = s.get("cracked", -1) == 0 and bf.get("force_error", 1.0) < 1e-3 and bf.get("moment_error", 1.0) < 1e-3 and data.get("max_compression_kpa", 0.0) > 0.0
			"wall_strong":
				var b: Dictionary = data.get("blow", {})
				ok = b.get("broken", 0) >= 1 and b.get("cracked", -1) == 0 and b.get("fallen", -1) == 0
			"wall_weak":
				var b: Dictionary = data.get("blow", {})
				ok = b.get("cracked", 0) >= 1 and data.get("fallen_total", 0) >= 20 and data.get("standing_end", 99) <= 10
			"wall_large":
				var b: Dictionary = data.get("blow", {})
				# A frame's solves within the work budget (100 000 iterations x bonds; a first round alone may pass it; the time is the machine's).
				ok = b.get("broken", 0) >= 1 and b.get("stress_usec", 0) > 0 and data.get("frame_stress_work_max", 0) <= 120000 and data.get("settling_frames", 0) >= 2 \
						and data.get("fallen_total", 0) >= 20
			"balcony_strong":
				ok = s.get("cracked", -1) == 0 and s.get("fallen", -1) == 0 and data.get("anchored", 0) >= 1 and data.get("anchored", 99) < 24 and data.get("standing_end", 0) == 24
			"balcony_load":
				var l: Dictionary = data.get("loaded", {})
				ok = s.get("cracked", -1) == 0 and s.get("fallen", -1) == 0 and l.get("cracked", 0) >= 1 and l.get("fallen", 0) >= 12
			"balcony_weak":
				ok = s.get("cracked", 0) >= 1 and s.get("fallen", 0) >= 12 and data.get("anchored", 0) >= 1 and data.get("chunk_rest_y", []).size() >= 1 \
						and data.chunk_rest_y.all(func(y): return y < 1.0)
		data["ok"] = ok
		data.erase("made")
		if not ok:
			failures.append(n)
		out[n] = data
	result["lanes"] = out
	result["failures"] = failures
	result["physics_engine"] = ProjectSettings.get_setting("physics/3d/physics_engine")
	if pictures and not shots.is_empty():
		var keys := shots.keys()
		keys.sort()
		var w: int = shots[keys[0]].get_width()
		var h: int = shots[keys[0]].get_height()
		var strip := Image.create(w * 2, h * 2, false, Image.FORMAT_RGBA8)
		for i in keys.size():
			var img: Image = shots[keys[i]]
			img.convert(Image.FORMAT_RGBA8)
			strip.blit_rect(img, Rect2i(0, 0, w, h), Vector2i((i % 2) * w, (i / 2) * h))
		strip.save_png(out_dir.path_join("stress_strip.png"))
	print("STRESS_CHECK " + JSON.stringify(result))
	var f := FileAccess.open(out_dir.path_join("stress_check.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	quit(1 if not failures.is_empty() else 0)
