extends SceneTree
## FellowEngine wind waves check (decision 0123): one FellowWater3D, its GPU displacement and initial spectrum written
## to files for modules/fellow_fluid/tests/waves_compare.py (NumPy: the same evolution and inverse FFT).
##
##   godot --path benchmarks/project -s res://bench/water_check.gd -- --out=<dir> [--time=12.3] [--wind=8] [--fetch=5]
##     [--res=256]
##
## Writes <out>/h0_<c>.bin (RGBA32F: h0(k), conj(h0(-k))), disp_<c>.bin (RGBA16F with mipmaps: x, y, z, foam) for each
## cascade, and info.json (the parameters the comparison needs).

var water
var frames := 0
var args := {}


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and "=" in a:
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	water = ClassDB.instantiate("FellowWater3D")
	water.time_auto = false
	water.time = float(args.get("time", "12.3"))
	water.wind_speed = float(args.get("wind", "8.0"))
	water.fetch = float(args.get("fetch", "5.0"))
	water.wave_resolution = int(args.get("res", "256"))
	root.add_child(water)


func _process(_delta: float) -> bool:
	frames += 1
	if frames < 4:
		return false
	var out: String = args.get("out", "user://water_check")
	DirAccess.make_dir_recursive_absolute(out)
	var lengths: Vector4 = water.cascade_lengths
	var n := 0
	for i in range(4):
		if lengths[i] > 0.0:
			n = i + 1
		else:
			break
	for c in range(n):
		var h0: Image = water.debug_get_spectrum_image(c)
		var f := FileAccess.open(out.path_join("h0_%d.bin" % c), FileAccess.WRITE)
		f.store_buffer(h0.get_data())
		f.close()
		var d: Image = water.debug_get_displacement_image(c)
		var g := FileAccess.open(out.path_join("disp_%d.bin" % c), FileAccess.WRITE)
		g.store_buffer(d.get_data())
		g.close()
	var info := {"time": water.time, "repeat_period": water.repeat_period, "choppiness": water.choppiness,
		"resolution": water.wave_resolution, "lengths": [lengths.x, lengths.y, lengths.z, lengths.w].slice(0, n),
		"significant_wave_height": water.get_significant_wave_height(), "gravity": 9.81}
	var fi := FileAccess.open(out.path_join("info.json"), FileAccess.WRITE)
	fi.store_string(JSON.stringify(info))
	fi.close()
	print("water_check: wrote ", n, " cascades to ", out)
	quit()
	return true
