extends SceneTree
## FellowEngine's weather sound (milestone 178, decision 0320), headless: the syntheses rendered offline
## (FellowRain3D / FellowWind3D .debug_render_sound, FellowLightning3D.debug_render_thunder; 48 kHz stereo) for cases
## weather_sound.py measures and draws — rain on the ground at three strengths, on water, under a roof; wind from 3 to
## 20 m/s and a gust; thunder from 300 m to 3 km, to the right and ahead; the CPU's time a second of sound. Each case
## written as interleaved 32-bit float samples (<out>/<case>.f32) with an index (<out>/cases.json).
##
##   godot --headless --path benchmarks/tools/world_sound -s weather_sound.gd -- --out=<dir>
##   python benchmarks/tools/world_sound/weather_sound.py <dir>

const RATE := 48000.0

var _out := ""


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			_out = a.get_slice("=", 1)
	if _out == "":
		_out = "user://weather_sound"
	DirAccess.make_dir_recursive_absolute(_out)
	var rain: Node = ClassDB.instantiate("FellowRain3D")
	var wind: Node = ClassDB.instantiate("FellowWind3D")
	var lightning: Node = ClassDB.instantiate("FellowLightning3D")
	var cases := {}
	for r in [0.2, 0.5, 1.0]:
		cases["rain_ground_%s" % str(r)] = {"node": rain, "events": [{"time": 0.0, "rain": r, "water": 0.0, "shelter": 0.0}], "seconds": 4.0,
				"rain": r, "water": 0.0, "shelter": 0.0}
	cases["rain_water_0.5"] = {"node": rain, "events": [{"time": 0.0, "rain": 0.5, "water": 1.0, "shelter": 0.0}], "seconds": 4.0,
			"rain": 0.5, "water": 1.0, "shelter": 0.0}
	cases["rain_roof_0.5"] = {"node": rain, "events": [{"time": 0.0, "rain": 0.5, "water": 0.0, "shelter": 1.0, "roof": 2.5}], "seconds": 4.0,
			"rain": 0.5, "water": 0.0, "shelter": 1.0}
	for v in [3.0, 8.0, 14.0, 20.0]:
		cases["wind_%sms" % str(v)] = {"node": wind, "events": [{"time": 0.0, "speed": v}], "seconds": 4.0, "speed": v}
	cases["wind_gust"] = {"node": wind, "events": [{"time": 0.0, "speed": 6.0}, {"time": 1.5, "speed": 16.0}, {"time": 3.0, "speed": 6.0}],
			"seconds": 4.5}
	for d in [300.0, 800.0, 1500.0, 3000.0]:
		cases["thunder_%dm" % int(d)] = {"node": lightning, "thunder": true, "distance": d, "azimuth": 0.0, "seconds": d / 343.0 + 9.0}
	cases["thunder_800m_ahead"] = {"node": lightning, "thunder": true, "distance": 800.0, "azimuth": -90.0, "seconds": 800.0 / 343.0 + 9.0}
	var index := {}
	for name in cases:
		var c: Dictionary = cases[name]
		var t0 := Time.get_ticks_usec()
		var s: PackedFloat32Array
		if c.has("thunder"):
			s = c.node.call("debug_render_thunder", c.distance, c.azimuth, c.seconds, RATE, 3)
		else:
			s = c.node.call("debug_render_sound", c.events, c.seconds, RATE)
		var us := Time.get_ticks_usec() - t0
		var f := FileAccess.open(_out.path_join(name + ".f32"), FileAccess.WRITE)
		f.store_buffer(s.to_byte_array())
		f.close()
		var entry := c.duplicate()
		entry.erase("events")
		entry.erase("node")
		entry["usec"] = us
		entry["usec_per_second"] = float(us) / float(c.seconds)
		index[name] = entry
	var f := FileAccess.open(_out.path_join("cases.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify({"rate": RATE, "cases": index}, "\t"))
	f.close()
	rain.free()
	wind.free()
	lightning.free()
	print("weather sound: %d cases written to %s" % [cases.size(), _out])
	quit()
