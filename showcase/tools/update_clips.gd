extends SceneTree
## Milestone 207: clips rebuilt in Blender put into the character's library (rooftops/character/mannequin_clips.res)
## without rebuilding the town. Each named clip is taken from <models>/Mannequin.glb as build_rooftops.gd takes them (at
## 60 fps, looping as its LOOPS say) and replaces the library's clip of that name, or is added to it.
##
##   python tools/blender/build_mannequin.py <models> fall fall_flail     (Blender's Python, or the bpy module)
##   godot --headless --path showcase -s res://tools/update_clips.gd -- --models=<models> --clips=fall,fall_flail [--check=1]
##
## --check=1 saves nothing: it prints how far each clip's rotations are from the library's (0 for a clip rebuilt from
## the same code).

const LIBRARY := "res://rooftops/character/mannequin_clips.res"


func _init() -> void:
	var models := ""
	var names: PackedStringArray = []
	var check := false
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--models="):
			models = a.get_slice("=", 1)
		elif a.begins_with("--clips="):
			names = a.get_slice("=", 1).split(",", false)
		elif a == "--check=1":
			check = true
	if models.is_empty() or names.is_empty():
		push_error("update_clips: --models=<dir> --clips=<a,b,...>")
		quit(1)
		return
	var loops: Array = (load("res://tools/build_rooftops.gd") as GDScript).get_script_constant_map()["LOOPS"]
	var doc := GLTFDocument.new()
	var st := GLTFState.new()
	if doc.append_from_file(models.path_join("Mannequin.glb"), st) != OK:
		push_error("update_clips: no Mannequin.glb in " + models)
		quit(1)
		return
	var src: Node = doc.generate_scene(st, 60.0)
	var ap: AnimationPlayer = src.find_child("AnimationPlayer", true, false)
	var built: AnimationLibrary = ap.get_animation_library(&"")
	var lib: AnimationLibrary = load(LIBRARY)
	var failed := false
	for n in names:
		if not built.has_animation(n):
			push_error("update_clips: %s is not in the model's clips" % n)
			failed = true
			continue
		var anim: Animation = built.get_animation(n).duplicate(true)
		anim.loop_mode = Animation.LOOP_LINEAR if n in loops else Animation.LOOP_NONE
		if check:
			print("update_clips: %s %s" % [n, _difference(anim, lib.get_animation(n)) if lib.has_animation(n) else "new"])
			continue
		if lib.has_animation(n):
			lib.remove_animation(n)
		lib.add_animation(n, anim)
		print("update_clips: %s %.2f s, %d tracks" % [n, anim.length, anim.get_track_count()])
	src.free()
	if not check and not failed:
		ResourceSaver.save(lib, LIBRARY, ResourceSaver.FLAG_COMPRESS) # (as build_rooftops.gd saves it)
	quit(1 if failed else 0)


## The largest angle (degrees) between the two clips' rotations, sampled every frame, over the tracks both have.
func _difference(p_a: Animation, p_b: Animation) -> String:
	var worst := 0.0
	var tracks := 0
	for i in p_a.get_track_count():
		if p_a.track_get_type(i) != Animation.TYPE_ROTATION_3D:
			continue
		var j := p_b.find_track(p_a.track_get_path(i), Animation.TYPE_ROTATION_3D)
		if j < 0:
			continue
		tracks += 1
		var t := 0.0
		while t <= minf(p_a.length, p_b.length) + 1e-4:
			var qa := p_a.rotation_track_interpolate(i, t)
			var qb := p_b.rotation_track_interpolate(j, t)
			worst = maxf(worst, rad_to_deg(qa.angle_to(qb)))
			t += 1.0 / 60.0
	return "%.2f s (library %.2f s), %d rotation tracks matched, %.3f deg at most" % [p_a.length, p_b.length, tracks, worst]
