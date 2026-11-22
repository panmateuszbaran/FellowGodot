extends SceneTree
## FellowEngine's water sound (milestone 177, decision 0319), headless: the synthesis rendered offline
## (FellowWater3D.debug_render_sound, 48 kHz mono) for cases water_sound.py measures and draws — single bubbles of
## 0.5-10 mm against Minnaert's frequency and the model's damping; splashes of bodies of 0.1-1.5 m at 1-10 m/s; a wake;
## the CPU's time a second of sound. Each case written as 32-bit float samples (<out>/<case>.f32) with an index
## (<out>/cases.json).
##
##   godot --headless --path benchmarks/tools/buoyancy -s water_sound.gd -- --out=<dir>
##   python benchmarks/tools/buoyancy/water_sound.py <dir>

const RATE := 48000.0

var _out := ""


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			_out = a.get_slice("=", 1)
	if _out == "":
		_out = "user://water_sound"
	DirAccess.make_dir_recursive_absolute(_out)
	var water: Node = ClassDB.instantiate("FellowWater3D")
	var cases := {}
	# Single bubbles.
	for r_mm in [0.5, 1.0, 2.0, 5.0, 10.0]:
		cases["bubble_%smm" % str(r_mm)] = {"events": [{"time": 0.01, "bubble": r_mm / 1000.0, "amplitude": 0.5, "rise": 0.1}],
				"seconds": 0.4, "radius": r_mm / 1000.0}
		cases["bubble_%smm_still" % str(r_mm)] = {"events": [{"time": 0.01, "bubble": r_mm / 1000.0, "amplitude": 0.5, "rise": 0.0}],
				"seconds": 0.4, "radius": r_mm / 1000.0}
	# Splashes: speed (a 0.3 m body), size (at 5 m/s).
	for v in [1.5, 3.0, 5.0, 8.0, 11.0]:
		cases["splash_0.3m_%sms" % str(v)] = {"events": [{"time": 0.05, "speed": v, "size": 0.3}], "seconds": 1.2, "speed": v, "size": 0.3}
	for z in [0.1, 0.3, 0.6, 1.0, 1.5]:
		cases["splash_%sm_5ms" % str(z)] = {"events": [{"time": 0.05, "speed": 5.0, "size": z}], "seconds": 1.2, "speed": 5.0, "size": z}
	# Debris: twenty pieces of 0.3 m at 10 m/s over 0.15 s.
	var rain := []
	var rng := RandomNumberGenerator.new()
	rng.seed = 3
	for i in 20:
		rain.append({"time": 0.05 + rng.randf() * 0.15, "speed": rng.randf_range(9.5, 11.0), "size": rng.randf_range(0.15, 0.4)})
	rain.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return a.time < b.time)
	cases["debris_20"] = {"events": rain, "seconds": 1.5}
	# A wake: rising to 1, 3, then calm.
	cases["wake"] = {"events": [{"time": 0.0, "wake": 0.3}, {"time": 1.0, "wake": 1.0}, {"time": 2.0, "wake": 3.0}, {"time": 3.0, "wake": 0.0}],
			"seconds": 3.5}
	var index := {}
	for name in cases:
		var c: Dictionary = cases[name]
		var t0 := Time.get_ticks_usec()
		var s: PackedFloat32Array = water.call("debug_render_sound", c.events, c.seconds, RATE)
		var us := Time.get_ticks_usec() - t0
		var f := FileAccess.open(_out.path_join(name + ".f32"), FileAccess.WRITE)
		f.store_buffer(s.to_byte_array())
		f.close()
		var entry := c.duplicate()
		entry.erase("events")
		entry["usec"] = us
		entry["usec_per_second"] = float(us) / float(c.seconds)
		index[name] = entry
	var f := FileAccess.open(_out.path_join("cases.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify({"rate": RATE, "cases": index}, "\t"))
	f.close()
	water.free()
	print("water sound: %d cases written to %s" % [cases.size(), _out])
	quit()
