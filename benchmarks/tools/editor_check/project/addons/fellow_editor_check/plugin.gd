@tool
extends EditorPlugin
## FellowEngine's editor check (milestone 50). Run by benchmarks/tools/editor_check/run_editor_check.py, which opens a
## copy of this project in the editor: once the first import is done it checks the fellow_editor module — the class
## icons, the .bvh importer, the gizmos of FellowEngine's nodes (and a picture of them where the editor renders), the
## "Split into Streaming Cells" tool run through its menu item and dialog (the cells' content shown in the editor, not
## saved with the level), and the Performance monitors — prints one line, EDITOR_CHECK {json}, and quits (exit code 1 on a failure).
##   godot --editor --path <copy> -- --out=<directory for the picture>

const ICON_CLASSES := ["FellowBurnable3D", "FellowGasEmitter3D", "FellowGasVolume3D", "FellowLightning3D",
	"FellowRain3D", "FellowShallowWater3D", "FellowSnowTracker3D", "FellowWater3D", "FellowWeather3D", "FellowWind3D",
	"FellowHLOD3D", "FellowImpostorBaker", "FellowStreamingCell3D", "FellowWorldStreamer3D", "FellowMotionCharacter3D",
	"FellowMotionDatabase", "FellowMotionMatcher3D"]
const MONITORS := ["FellowEngine/Texture streaming resident", "FellowEngine/Texture streaming wanted",
	"FellowEngine/Texture streaming pending", "FellowEngine/World cells loaded", "FellowEngine/World cells loading",
	"FellowEngine/Motion matching time", "FellowEngine/Motion matching updates"]
const SPLIT_MENU := "Split into Streaming Cells (FellowEngine)..."

var out := {}
var failures := []
var out_dir := ""


func _enter_tree() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out_dir = a.substr(6)
	_run.call_deferred()


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _fail(what: String) -> void:
	failures.append(what)
	printerr("EDITOR_CHECK failure: ", what)


func _run() -> void:
	var fs := EditorInterface.get_resource_filesystem()
	var t0 := Time.get_ticks_msec()
	while (fs.is_scanning() or fs.is_importing()) and Time.get_ticks_msec() - t0 < 120000:
		await get_tree().process_frame
	await _frames(10)
	out["editor"] = Engine.get_version_info().string
	out["display"] = DisplayServer.get_name()
	_check_icons()
	_check_bvh()
	await _check_gizmos()
	await _check_split()
	_check_monitors()
	out["failures"] = failures
	print("EDITOR_CHECK ", JSON.stringify(out))
	get_tree().quit(0 if failures.is_empty() else 1)


func _check_icons() -> void:
	var theme := EditorInterface.get_editor_theme()
	var missing := []
	for c in ICON_CLASSES:
		if not theme.has_icon(c, "EditorIcons"):
			missing.append(c)
	out["icons"] = ICON_CLASSES.size() - missing.size()
	if not missing.is_empty():
		_fail("icons missing: %s" % [missing])


func _check_bvh() -> void:
	var anim = load("res://walk.bvh")
	if not anim is Animation:
		_fail("walk.bvh did not import as an Animation: %s" % [anim])
		return
	var bvh := {"tracks": anim.get_track_count(), "length": anim.length}
	# The hips' position at the first frame: 90 BVH units at the default preset's 0.01 → 0.9 m.
	for t in anim.get_track_count():
		if anim.track_get_type(t) == Animation.TYPE_POSITION_3D and String(anim.track_get_path(t)).contains("Hips"):
			bvh["hips_start"] = var_to_str(anim.position_track_interpolate(t, 0.0))
			if absf(anim.position_track_interpolate(t, 0.0).y - 0.9) > 0.001:
				_fail("walk.bvh: the hips start at %s, not 0.9 m up" % [anim.position_track_interpolate(t, 0.0)])
	var paths := []
	for t in mini(anim.get_track_count(), 3):
		paths.append(String(anim.track_get_path(t)))
	bvh["first_tracks"] = paths
	var import_cfg := ConfigFile.new()
	if import_cfg.load("res://walk.bvh.import") == OK:
		bvh["importer"] = import_cfg.get_value("remap", "importer", "")
	out["bvh"] = bvh
	if bvh.get("importer", "") != "fellow.bvh":
		_fail("walk.bvh imported by %s" % [bvh.get("importer", "nothing")])


func _check_gizmos() -> void:
	EditorInterface.open_scene_from_path("res://gizmos.tscn")
	await _frames(20)
	var root := EditorInterface.get_edited_scene_root()
	if root == null or root.name != "Gizmos":
		_fail("gizmos.tscn did not open")
		return
	var selection := EditorInterface.get_selection()
	selection.clear()
	selection.add_node(root.get_node("GasVolume"))
	await _frames(10)
	var gizmos := {}
	for n in root.get_children():
		var count := 0
		for g in n.get_gizmos():
			if g is EditorNode3DGizmo and g.get_plugin() != null and g.get_plugin().get_class() == "FellowGizmoPlugin":
				count += 1
		gizmos[n.get_class()] = count
		if count != 1:
			_fail("%s has %d FellowEngine gizmos" % [n.get_class(), count])
	out["gizmos"] = gizmos
	if out_dir != "" and DisplayServer.get_name() != "headless":
		await RenderingServer.frame_post_draw
		var img := EditorInterface.get_editor_viewport_3d(0).get_texture().get_image()
		if img != null:
			DirAccess.make_dir_recursive_absolute(out_dir)
			img.save_png(out_dir.path_join("gizmos.png"))
			out["picture"] = out_dir.path_join("gizmos.png")


func _find_menu(node: Node, text: String) -> PopupMenu:
	if node is PopupMenu:
		for i in node.item_count:
			if node.get_item_text(i) == text:
				return node
	for c in node.get_children(true):
		var found := _find_menu(c, text)
		if found != null:
			return found
	return null


func _find_dialog(node: Node, title: String) -> AcceptDialog:
	if node is AcceptDialog and node.title == title:
		return node
	for c in node.get_children(true):
		var found := _find_dialog(c, title)
		if found != null:
			return found
	return null


func _check_split() -> void:
	EditorInterface.open_scene_from_path("res://split.tscn")
	await _frames(20)
	var root := EditorInterface.get_edited_scene_root()
	if root == null or root.name != "Level":
		_fail("split.tscn did not open")
		return
	var streamer := root.get_node("Streamer")
	var selection := EditorInterface.get_selection()
	selection.clear()
	selection.add_node(streamer)
	await _frames(2)
	var base := EditorInterface.get_base_control()
	var menu := _find_menu(base, SPLIT_MENU)
	if menu == null:
		_fail("no \"%s\" menu item" % SPLIT_MENU)
		return
	for i in menu.item_count:
		if menu.get_item_text(i) == SPLIT_MENU:
			menu.index_pressed.emit(i)
	await _frames(3)
	var dialog := _find_dialog(base, "Split into Streaming Cells")
	if dialog == null or not dialog.visible:
		_fail("the split dialog did not open")
		return
	var directory := ""
	for e in dialog.find_children("*", "LineEdit", true, false):
		if e.text.begins_with("res://"):
			directory = e.text
			e.text = "res://split_cells"
	for s in dialog.find_children("*", "SpinBox", true, false):
		s.value = 64
	out["split_default_directory"] = directory
	var t0 := Time.get_ticks_usec()
	dialog.confirmed.emit()
	dialog.hide()
	var split_ms := (Time.get_ticks_usec() - t0) / 1000.0
	await _frames(5)
	var report := ""
	for d in base.find_children("*", "AcceptDialog", true, false):
		if d.visible and d.dialog_text.contains("streaming cells made"):
			report = d.dialog_text
			d.hide()
	var cells := []
	for c in streamer.get_children():
		if c.get_class() == "FellowStreamingCell3D":
			cells.append(c)
	var unowned := 0
	for c in cells:
		if c.owner != root:
			unowned += 1
	var left := []
	for c in root.get_children():
		left.append(String(c.name))
	var files := DirAccess.get_files_at("res://split_cells")
	var split := {"cells": cells.size(), "files": files.size(), "left_in_level": left, "unowned_cells": unowned,
		"report": report, "split_ms": split_ms}
	if cells.is_empty() or files.size() != cells.size() or unowned != 0 or left.size() != 2 or report == "":
		_fail("split: %s" % [split])
	# In the editor the cells show their content (internal children, not saved): every box still in view.
	await _frames(5)
	var preview_boxes := 0
	for c in cells:
		for i in range(c.get_child_count(false), c.get_child_count(true)):
			for n in c.get_child(i, true).get_children():
				if n is MeshInstance3D:
					preview_boxes += 1
	split["preview_boxes"] = preview_boxes
	if preview_boxes != 40:
		_fail("split: %d of 40 boxes shown in the editor" % preview_boxes)
	# Saved without them: stream_back.gd then loads the level as a game would (run_editor_check.py runs it).
	EditorInterface.save_scene()
	await _frames(3)
	var saved := FileAccess.get_file_as_string("res://split.tscn")
	split["saved_boxes"] = saved.count("type=\"MeshInstance3D\"")
	split["saved_cells"] = saved.count("type=\"FellowStreamingCell3D\"")
	if split["saved_boxes"] != 0 or split["saved_cells"] != cells.size():
		_fail("split: the saved level has %d boxes and %d cells" % [split["saved_boxes"], split["saved_cells"]])
	out["split"] = split


func _check_monitors() -> void:
	var names := []
	for n in Performance.get_custom_monitor_names():
		names.append(String(n))
	var values := {}
	for m in MONITORS:
		if names.has(m):
			values[m] = Performance.get_custom_monitor(m)
		else:
			_fail("no Performance monitor %s" % m)
	out["monitors"] = values
