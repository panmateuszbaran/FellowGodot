extends SceneTree
## FellowEngine's fire sound (milestone 179, decision 0321), headless: the synthesis rendered offline
## (FellowGasVolume3D.debug_render_sound; 48 kHz) for cases fire_sound.py measures and draws — flames of 0.25, 1 and 4 m
## (their roar's peak against 160 / sqrt(D) Hz, their puffing against 1.5 / sqrt(D) Hz, their loudness against their
## heat release), a campfire, a bonfire, a gas catching (the flare), crackle alone (its rate and the power law of its
## cracks), embers; the CPU's time a second of sound. Each case written as interleaved 32-bit float samples
## (<out>/<case>.f32) with an index (<out>/cases.json).
##
##   godot --headless --path benchmarks/tools/world_sound -s fire_sound.gd -- --out=<dir>
##   python benchmarks/tools/world_sound/fire_sound.py <dir>

const RATE := 48000.0

var _out := ""


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			_out = a.get_slice("=", 1)
	if _out == "":
		_out = "user://fire_sound"
	DirAccess.make_dir_recursive_absolute(_out)
	var gas: Node = ClassDB.instantiate("FellowGasVolume3D")
	var cases := {}
	# Flames alone: a heat release growing with the fire's size (about D^2.5 for pool fires: 1, 4 m and 0.25 m).
	for d in [0.25, 1.0, 4.0]:
		var p := pow(d / 0.4, 2.5)
		cases["flames_%sm" % str(d)] = {"keys": [{"time": 0.0, "power": p, "size": d}], "seconds": 12.0, "power": p, "size": d}
	cases["campfire"] = {"keys": [{"time": 0.0, "power": 1.0, "size": 0.4, "crackle": 8.0, "embers": 0.2}], "seconds": 8.0,
			"power": 1.0, "size": 0.4, "crackle": 8.0}
	cases["bonfire"] = {"keys": [{"time": 0.0, "power": 25.0, "size": 1.5, "crackle": 40.0, "embers": 0.3}], "seconds": 8.0,
			"power": 25.0, "size": 1.5, "crackle": 40.0}
	cases["flare"] = {"keys": [{"time": 0.0, "power": 0.0, "size": 0.5}, {"time": 1.0, "power": 6.0, "size": 0.8}], "seconds": 4.0,
			"ignite_at": 1.0}
	cases["crackle"] = {"keys": [{"time": 0.0, "power": 0.0, "crackle": 12.0}], "seconds": 30.0, "crackle": 12.0}
	cases["embers"] = {"keys": [{"time": 0.0, "power": 0.0, "crackle": 0.3, "embers": 1.0}], "seconds": 8.0, "crackle": 0.3}
	var index := {}
	for name in cases:
		var c: Dictionary = cases[name]
		var r: Dictionary = gas.call("debug_render_sound", c.keys, c.seconds, RATE, 7)
		var s: PackedFloat32Array = r.samples
		var f := FileAccess.open(_out.path_join(name + ".f32"), FileAccess.WRITE)
		f.store_buffer(s.to_byte_array())
		f.close()
		var entry := c.duplicate()
		entry.erase("keys")
		for k in ["cracks", "pops", "ticks", "flares", "usec_per_second"]:
			entry[k] = r[k]
		index[name] = entry
	var f := FileAccess.open(_out.path_join("cases.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify({"rate": RATE, "cases": index}, "\t"))
	f.close()
	gas.free()
	print("fire sound: %d cases written to %s" % [cases.size(), _out])
	quit()
