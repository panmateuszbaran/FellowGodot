extends SceneTree
## Milestone 192: Mixamo's motion-captured clips, retargeted onto the mannequin by tools/blender/mixamo_retarget.py
## (<dir>/Mocap.glb and clips.json), as the player's library of mocap clips: res://rooftops/mocap/mocap_clips.res and
## res://rooftops/mocap/clips.json. Neither is committed (Mixamo's licence lets a game use its clips, not hand them out):
## the player (rooftops_player.gd) plays them in place of its own clips of the same names where they exist.
##
##   godot --headless --path showcase -s res://tools/build_mocap_clips.gd -- --in=<dir>

const OUT := "res://rooftops/mocap"


func _init() -> void:
	var src := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--in="):
			src = a.get_slice("=", 1)
	var glb := src.path_join("Mocap.glb")
	if not FileAccess.file_exists(glb):
		printerr("build_mocap_clips: no ", glb)
		quit(1)
		return
	var info: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(src.path_join("clips.json")))
	var doc := GLTFDocument.new()
	var st := GLTFState.new()
	if doc.append_from_file(glb, st) != OK:
		printerr("build_mocap_clips: cannot read ", glb)
		quit(1)
		return
	var scene: Node = doc.generate_scene(st, 60.0)
	var ap: AnimationPlayer = scene.find_child("AnimationPlayer", true, false)
	var lib: AnimationLibrary = ap.get_animation_library(&"")
	var clips: Dictionary = info.get("clips", {})
	for a in lib.get_animation_list():
		var anim := lib.get_animation(a)
		var c: Dictionary = clips.get(String(a), {})
		anim.loop_mode = Animation.LOOP_LINEAR if c.get("loop", false) else Animation.LOOP_NONE
		print("build_mocap_clips: %s %.2f s%s" % [a, anim.length, " loop" if c.get("loop", false) else ""])
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUT))
	var err := ResourceSaver.save(lib, OUT.path_join("mocap_clips.res"))
	var f := FileAccess.open(OUT.path_join("clips.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(info, " "))
	f.close()
	scene.free()
	print("build_mocap_clips: %d clips -> %s (%s)" % [lib.get_animation_list().size(), OUT, error_string(err)])
	quit(0 if err == OK else 1)
