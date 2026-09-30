extends Node3D
## Loads the imported glTF scenes, renders a few frames, saves a screenshot and quits (exit code 0 = OK).

func _ready() -> void:
	var ok := true
	for p in ["res://models/suzanne.glb", "res://models/embedded_texture.gltf"]:
		var ps: PackedScene = load(p)
		if ps == null:
			push_error("VERIFY: could not load %s" % p)
			ok = false
			continue
		var inst := ps.instantiate()
		add_child(inst)
		print("VERIFY: loaded %s (%d nodes)" % [p, inst.get_child_count()])
	var cam := Camera3D.new()
	cam.position = Vector3(0, 0.5, 4)
	add_child(cam)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-45, 30, 0)
	add_child(sun)
	for i in 10:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw
	var img := get_viewport().get_texture().get_image()
	if img:
		img.save_png("user://verify.png")
		print("VERIFY: rendered %dx%d on %s" % [img.get_width(), img.get_height(), RenderingServer.get_video_adapter_name()])
	print("VERIFY: engine %s fellow=%s" % [Engine.get_version_info().string, ProjectSettings.has_setting("rendering/fellow/enabled")])
	print("VERIFY: %s" % ("OK" if ok else "FAILED"))
	get_tree().quit(0 if ok else 1)
