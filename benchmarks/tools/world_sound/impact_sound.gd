extends SceneTree
## FellowEngine's contact sound (milestone 180, decision 0322), headless: bodies' modal synthesis rendered offline
## (FellowContactSound3D.debug_render; 48 kHz) for cases impact_sound.py measures and draws — a crate, a plank struck
## at its end and its middle, a steel pipe and bar (their lowest modes against a beam's theory), a stone, a glass
## bottle, a rubber ball, a steel sheet, each struck in the hand (ringing free: the material's decay) and dropped on
## stone (resting after); the crate dropped at 0.5-8 m/s (louder and brighter); the crate sliding at 0.3-3 m/s, the
## pipe rolling. Each case written as interleaved 32-bit float samples (<out>/<case>.f32) with an index
## (<out>/cases.json: the modes, the contact's durations, the clicks, the CPU's time).
##
##   godot --headless --path benchmarks/tools/world_sound -s impact_sound.gd -- --out=<dir>
##   python benchmarks/tools/world_sound/impact_sound.py <dir>

const RATE := 48000.0
enum { WOOD, METAL, STONE, GLASS, PLASTIC, RUBBER, CERAMIC }

var _out := ""


func _body(p_shape: Shape3D, p_material: int, p_wall: float, p_mass: float) -> Node:
	var body := RigidBody3D.new()
	body.mass = p_mass
	var cs := CollisionShape3D.new()
	cs.shape = p_shape
	body.add_child(cs)
	var snd: Node = ClassDB.instantiate("FellowContactSound3D")
	snd.set("material", p_material)
	snd.set("wall_thickness", p_wall)
	body.add_child(snd)
	return snd


func _box(p_size: Vector3) -> BoxShape3D:
	var b := BoxShape3D.new()
	b.size = p_size
	return b


func _cyl(p_radius: float, p_height: float) -> CylinderShape3D:
	var c := CylinderShape3D.new()
	c.radius = p_radius
	c.height = p_height
	return c


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			_out = a.get_slice("=", 1)
	if _out == "":
		_out = "user://impact_sound"
	DirAccess.make_dir_recursive_absolute(_out)
	var objects := {
		"crate": [_box(Vector3(0.6, 0.6, 0.6)), WOOD, 0.02, 20.0],
		"plank": [_box(Vector3(1.2, 0.03, 0.15)), WOOD, 0.0, 3.2],
		"pipe": [_cyl(0.03, 1.0), METAL, 0.003, 4.2],
		"bar": [_box(Vector3(1.0, 0.03, 0.03)), METAL, 0.0, 7.1],
		"stone": [_box(Vector3(0.12, 0.1, 0.08)), STONE, 0.0, 2.5],
		"bottle": [_cyl(0.04, 0.3), GLASS, 0.003, 0.5],
		"ball": [_cyl(0.1, 0.2), RUBBER, 0.0, 2.0],
		"sheet": [_box(Vector3(1.0, 0.002, 0.6)), METAL, 0.0, 9.4],
	}
	var nodes := {}
	for k in objects:
		var o: Array = objects[k]
		nodes[k] = _body(o[0], o[1], o[2], o[3])
	var cases := {}
	# Struck in the hand at 2 m/s by stone (ringing free) and dropped on stone at 3 m/s (resting after).
	for k in objects:
		cases["held_" + k] = [k, [{"time": 0.05, "kind": "impact", "speed": 2.0, "other": STONE, "held": true}], 3.0]
		cases["drop_" + k] = [k, [{"time": 0.05, "kind": "impact", "speed": 3.0, "other": STONE}, {"time": 0.06, "kind": "rest"}], 1.5]
	# The plank struck at its end and in its middle (its long axis x).
	cases["plank_end"] = ["plank", [{"time": 0.05, "kind": "impact", "speed": 2.0, "other": STONE, "held": true, "position": Vector3(-0.58, 0.0, 0.0)}], 2.0]
	cases["plank_middle"] = ["plank", [{"time": 0.05, "kind": "impact", "speed": 2.0, "other": STONE, "held": true, "position": Vector3(0.0, 0.0, 0.0)}], 2.0]
	# The crate dropped faster and faster.
	for v in [0.5, 1.0, 2.0, 4.0, 8.0]:
		cases["speed_%s" % str(v)] = ["crate", [{"time": 0.05, "kind": "impact", "speed": v, "other": STONE}, {"time": 0.06, "kind": "rest"}], 1.0]
	# The crate struck by metal and by rubber (the contact's hardness).
	cases["crate_by_metal"] = ["crate", [{"time": 0.05, "kind": "impact", "speed": 2.0, "other": METAL, "held": true}], 1.5]
	cases["crate_by_rubber"] = ["crate", [{"time": 0.05, "kind": "impact", "speed": 2.0, "other": RUBBER, "held": true}], 1.5]
	# Sliding, rolling.
	for v in [0.3, 1.0, 3.0]:
		cases["slide_%s" % str(v)] = ["crate", [{"time": 0.0, "kind": "slide", "speed": v, "other": STONE}], 3.0]
	cases["roll_pipe"] = ["pipe", [{"time": 0.0, "kind": "roll", "speed": 1.0, "other": STONE}], 3.0]
	var index := {}
	for name in cases:
		var c: Array = cases[name]
		var r: Dictionary = nodes[c[0]].call("debug_render", c[1], c[2], RATE, 3)
		var s: PackedFloat32Array = r.samples
		var f := FileAccess.open(_out.path_join(name + ".f32"), FileAccess.WRITE)
		f.store_buffer(s.to_byte_array())
		f.close()
		var freqs := []
		var decays := []
		for m in r.modes:
			freqs.append(m.frequency)
			decays.append(m.decay)
		index[name] = {"object": c[0], "events": c[1], "seconds": c[2], "modes_hz": freqs, "decays": decays,
				"contact_durations": Array(r.contact_durations), "clicks": Array(r.clicks), "usec_per_second": r.usec_per_second}
	var f := FileAccess.open(_out.path_join("cases.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify({"rate": RATE, "cases": index}, "\t"))
	f.close()
	for k in nodes:
		nodes[k].get_parent().free()
	print("impact sound: %d cases written to %s" % [cases.size(), _out])
	quit()
