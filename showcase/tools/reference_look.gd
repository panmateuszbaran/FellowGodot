extends SceneTree
## The look beside the owner's reference video (milestone 158): the rooftops scene's reference shot (ReferenceShot,
## decision 0297: the video's performance through the video's camera, at the replica of its house) rendered at the
## video's moments, each picture after the lighting has settled, set beside the video's frames.
##
##   godot --path showcase --rendering-driver vulkan --resolution 1280x720 -s res://tools/reference_look.gd -- \
##       --out=<dir> --frames=10,45,70,84,93
##
## (--frames: the video's frames, 1-based, at 24 fps; --settle=<n>: frames drawn before each picture, 24.) Writes
## <dir>/e<frame>.png; with --ref=<dir> (the video's frames as f001.png, f002.png, ...: not in the repository) also
## <dir>/sheet.png, each frame beside the video's.

func _init() -> void:
	var out := "user://reference_look"
	var frames := PackedInt32Array([10, 45, 70, 84, 93])
	var ref := ""
	var settle := 24
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.get_slice("=", 1)
		elif a.begins_with("--ref="):
			ref = a.get_slice("=", 1)
		elif a.begins_with("--settle="):
			settle = int(a.get_slice("=", 1))
		elif a.begins_with("--frames="):
			frames = PackedInt32Array()
			for v in a.get_slice("=", 1).split(","):
				frames.append(int(v))
	DirAccess.make_dir_recursive_absolute(out)
	var sc: Node = (load("res://rooftops/rooftops.tscn") as PackedScene).instantiate()
	root.add_child(sc)
	await process_frame
	var demo: Node = sc.get_node("Demo")
	# The demo's own staging of the shot (its sun, its sky, the player and the square's people hidden).
	demo.set("_player", sc.get_node("Player"))
	demo.set("_cam", sc.get_node("CameraRig/Camera3D"))
	demo.set("_reference", sc.get_node("ReferenceShot"))
	demo.call("_show_reference", true)
	var ap: AnimationPlayer = sc.get_node("ReferenceShot/AnimationPlayer")
	var tile := Vector2i(640, 360)
	var sheet := Image.create_empty(tile.x * 2, tile.y * frames.size(), false, Image.FORMAT_RGB8)
	var row := 0
	for f in frames:
		ap.seek((f - 1) / 24.0, true)
		ap.pause()
		for k in settle:
			await process_frame
		await RenderingServer.frame_post_draw
		var img := root.get_texture().get_image()
		img.save_png("%s/e%03d.png" % [out, f])
		print("reference_look: frame %d" % f)
		if ref != "":
			var r := Image.load_from_file("%s/f%03d.png" % [ref, f])
			if r:
				r.convert(Image.FORMAT_RGB8)
				r.resize(tile.x, tile.y, Image.INTERPOLATE_BILINEAR)
				sheet.blit_rect(r, Rect2i(Vector2i(), tile), Vector2i(0, row * tile.y))
			img.convert(Image.FORMAT_RGB8)
			img.resize(tile.x, tile.y, Image.INTERPOLATE_BILINEAR)
			sheet.blit_rect(img, Rect2i(Vector2i(), tile), Vector2i(tile.x, row * tile.y))
		row += 1
	if ref != "":
		sheet.save_png("%s/sheet.png" % out)
	quit(0)
