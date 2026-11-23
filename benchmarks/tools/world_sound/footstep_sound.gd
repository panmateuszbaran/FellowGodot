extends SceneTree
## FellowEngine's footsteps (milestone 181, decision 0323), headless: the synthesis rendered offline
## (FellowFootsteps3D.debug_render; 48 kHz) for cases footstep_sound.py measures and draws — four walking steps (the
## heel, the sole rolling onto the ball 80 ms later, the toe lifting) on each of the thirteen grounds; the four shoes on
## stone and wood; sneaking, walking, running and landing from a jump on stone and gravel; a light and a heavy walker;
## grass over dirt; a foot scuffing (a sneaker on wood squeaks). Each case written as interleaved 32-bit float samples
## (<out>/<case>.f32) with an index (<out>/cases.json).
##
##   godot --headless --path benchmarks/tools/world_sound -s footstep_sound.gd -- --out=<dir>
##   python benchmarks/tools/world_sound/footstep_sound.py <dir>

const RATE := 48000.0
const GROUNDS := ["stone", "wood", "metal", "tile", "carpet", "gravel", "sand", "snow", "grass", "leaves", "dirt", "mud", "water"]

var _out := ""


## Steps every p_period s from 0.1 s: the heel at p_speed, the ball 80 ms later, the toe lifting at 55 % of the period.
func _walk(p_ground, p_shoe: int, p_speed: float, p_period: float, p_count: int, p_load: float = 75.0, p_extra: Dictionary = {}) -> Array:
	var ev := []
	for i in p_count:
		var t := 0.1 + i * p_period
		var base := {"ground": p_ground, "shoe": p_shoe, "load": p_load}
		base.merge(p_extra)
		var heel := base.duplicate()
		heel.merge({"time": t, "part": "heel", "speed": p_speed})
		var ball := base.duplicate()
		ball.merge({"time": t + 0.08, "part": "ball", "speed": p_speed * 0.8})
		var lift := base.duplicate()
		lift.merge({"time": t + p_period * 0.55, "part": "lift", "speed": p_speed * 0.6})
		ev += [heel, ball, lift]
	ev.sort_custom(func(a, b): return a.time < b.time)
	return ev


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			_out = a.get_slice("=", 1)
	if _out == "":
		_out = "user://footstep_sound"
	DirAccess.make_dir_recursive_absolute(_out)
	var cases := {}
	for g in GROUNDS:
		cases["walk_" + g] = {"events": _walk(g, 0, 0.5, 0.55, 4), "seconds": 2.6, "steps": 4}
	for shoe in 4:
		for g in ["stone", "wood"]:
			cases["shoe%d_%s" % [shoe, g]] = {"events": _walk(g, shoe, 0.5, 0.55, 4), "seconds": 2.6, "steps": 4, "shoe": shoe}
	for g in ["stone", "gravel"]:
		cases["sneak_" + g] = {"events": _walk(g, 0, 0.2, 0.8, 3), "seconds": 2.6, "steps": 3}
		cases["run_" + g] = {"events": _walk(g, 0, 1.5, 0.36, 6), "seconds": 2.4, "steps": 6}
		cases["land_" + g] = {"events": [
			{"time": 0.1, "part": "heel", "ground": g, "speed": 3.5}, {"time": 0.12, "part": "heel", "ground": g, "speed": 3.3},
			{"time": 0.16, "part": "ball", "ground": g, "speed": 2.5}, {"time": 0.18, "part": "ball", "ground": g, "speed": 2.4}],
			"seconds": 1.2, "steps": 2}
	cases["light_gravel"] = {"events": _walk("gravel", 0, 0.5, 0.55, 4, 50.0), "seconds": 2.6, "steps": 4}
	cases["heavy_gravel"] = {"events": _walk("gravel", 0, 0.5, 0.55, 4, 100.0), "seconds": 2.6, "steps": 4}
	cases["grass_over_dirt"] = {"events": _walk("grass", 0, 0.5, 0.55, 4, 75.0, {"ground2": "dirt", "weight2": 0.4}), "seconds": 2.6, "steps": 4}
	for s in [[0, "wood"], [1, "wood"], [0, "gravel"]]:
		cases["scuff_%s_shoe%d" % [s[1], s[0]]] = {"events": [{"time": 0.1, "part": "scuff", "ground": s[1], "shoe": s[0], "speed": 1.2},
				{"time": 0.9, "part": "scuff", "ground": s[1], "shoe": s[0], "speed": 0.0}], "seconds": 1.4, "steps": 0}
	var index := {}
	for name in cases:
		var c: Dictionary = cases[name]
		var r: Dictionary = ClassDB.class_call_static("FellowFootsteps3D", "debug_render", c.events, c.seconds, RATE, 5)
		var s: PackedFloat32Array = r.samples
		var f := FileAccess.open(_out.path_join(name + ".f32"), FileAccess.WRITE)
		f.store_buffer(s.to_byte_array())
		f.close()
		var times := []
		for e in c.events:
			if e.part == "heel":
				times.append(e.time)
		index[name] = {"seconds": c.seconds, "steps": c.steps, "heel_times": times, "strikes": r.strikes, "grains": r.grains,
				"bubbles": r.bubbles, "usec_per_second": r.usec_per_second}
	var f := FileAccess.open(_out.path_join("cases.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify({"rate": RATE, "cases": index}, "\t"))
	f.close()
	print("footstep sound: %d cases written to %s" % [cases.size(), _out])
	quit()
