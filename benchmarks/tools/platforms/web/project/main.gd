extends Node3D
## FellowEngine's web check (milestone 50): FellowEngine's water, wind and world streaming (its loads on the web
## build's threads) in the web template; after the cells are in and 120 more frames, prints WEB_CHECK {json} to the
## browser's console.

var frames := 0
var settled_frame := -1
var t0 := 0


func _ready() -> void:
	t0 = Time.get_ticks_msec()


func _process(_delta: float) -> void:
	frames += 1
	if settled_frame < 0 and $Streamer.is_settled() and $Streamer.get_stats().get("loaded", 0) == 2:
		settled_frame = frames
	if frames == 2000 or (settled_frame >= 0 and frames == settled_frame + 120):
		_report()


func _report() -> void:
	var img := get_viewport().get_texture().get_image()
	var sum := 0.0
	var n := 0
	if img != null:
		for y in range(0, img.get_height(), 8):
			for x in range(0, img.get_width(), 8):
				var c := img.get_pixel(x, y)
				sum += 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b
				n += 1
	var stats: Dictionary = $Streamer.get_stats()
	var r := {
		"os": OS.get_name(),
		"version": Engine.get_version_info().string,
		"renderer": RenderingServer.get_current_rendering_method(),
		"driver": RenderingServer.get_current_rendering_driver_name(),
		"video_adapter": RenderingServer.get_video_adapter_name(),
		"threads": OS.has_feature("threads"),
		"frames": frames,
		"seconds": (Time.get_ticks_msec() - t0) / 1000.0,
		"cells_settled_frame": settled_frame,
		"cells_loaded": stats.get("loaded", 0),
		"cell_loads": stats.get("loads", 0),
		"cell_failures": stats.get("failures", 0),
		"luma_mean": 255.0 * sum / maxi(n, 1),
		"size": [img.get_width(), img.get_height()] if img != null else [],
		"fellow_classes": ClassDB.class_exists("FellowWater3D") and ClassDB.class_exists("FellowWorldStreamer3D"),
	}
	# On a desktop build, --shot=<file.png> keeps the picture (the web check's screenshot is the browser's).
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--shot=") and img != null:
			img.save_png(a.substr(7))
	print("WEB_CHECK ", JSON.stringify(r))
	if OS.get_name() != "Web":
		get_tree().quit()
