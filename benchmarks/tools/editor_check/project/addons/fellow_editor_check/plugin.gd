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


func _arg(name: String, default: String) -> String:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--%s=" % name):
			return a.substr(name.length() + 3)
	return default


func _run() -> void:
	var fs := EditorInterface.get_resource_filesystem()
	var t0 := Time.get_ticks_msec()
	while (fs.is_scanning() or fs.is_importing()) and Time.get_ticks_msec() - t0 < 120000:
		await get_tree().process_frame
	await _frames(10)
	if _arg("perf", "") != "":
		await _perf(_arg("perf", ""), float(_arg("perf_seconds", "10")))
		return
	out["editor"] = Engine.get_version_info().string
	out["display"] = DisplayServer.get_name()
	_check_icons()
	_check_bvh()
	await _check_gizmos()
	await _check_handles()
	await _check_split()
	_check_monitors()
	await _check_warnings()
	out["failures"] = failures
	print("EDITOR_CHECK ", JSON.stringify(out))
	get_tree().quit(0 if failures.is_empty() else 1)


# The editor's 3D view of a scene redrawn every frame (update continuously), measured (milestone 52): the frames'
# times, the 3D viewport's CPU and GPU render times. Prints EDITOR_PERF {json}.
func _perf(scene: String, seconds: float) -> void:
	var settings := EditorInterface.get_editor_settings()
	var continuous := _arg("perf_continuous", "true") == "true"
	settings.set_setting("interface/editor/display/update_continuously", continuous)
	settings.mark_setting_changed("interface/editor/display/update_continuously")
	settings.emit_signal("settings_changed")
	EditorInterface.open_scene_from_path(scene)
	EditorInterface.set_main_screen_editor("3D")
	var vp := EditorInterface.get_editor_viewport_3d(0)
	RenderingServer.viewport_set_measure_render_time(vp.get_viewport_rid(), true)
	await _frames(int(_arg("perf_warmup", "60")))
	var deltas := []
	var cpu := 0.0
	var gpu := 0.0
	# A node named Subject with a clock ("time"): how far it ran (still in the idle editor, milestone 52).
	var subject: Node = EditorInterface.get_edited_scene_root().get_node_or_null("Subject") if EditorInterface.get_edited_scene_root() else null
	var subject_time0 = subject.get("time") if subject else null
	var drawn0 := Engine.get_frames_drawn()
	var last := Time.get_ticks_usec()
	var t_end := last + int(seconds * 1e6)
	while Time.get_ticks_usec() < t_end:
		await get_tree().process_frame
		var now := Time.get_ticks_usec()
		deltas.append((now - last) / 1000.0)
		last = now
		cpu += RenderingServer.viewport_get_measured_render_time_cpu(vp.get_viewport_rid())
		gpu += RenderingServer.viewport_get_measured_render_time_gpu(vp.get_viewport_rid())
	var sorted := deltas.duplicate()
	sorted.sort()
	var total := 0.0
	for d in deltas:
		total += d
	var n := maxi(deltas.size(), 1)
	var r := {"scene": scene, "seconds": seconds, "frames": deltas.size(), "frame_ms_mean": snappedf(total / n, 0.001),
		"frame_ms_p50": snappedf(sorted[int(sorted.size() * 0.5)] if not sorted.is_empty() else 0.0, 0.001),
		"frame_ms_p95": snappedf(sorted[mini(int(sorted.size() * 0.95), sorted.size() - 1)] if not sorted.is_empty() else 0.0, 0.001),
		"viewport_cpu_ms": snappedf(cpu / n, 0.001), "viewport_gpu_ms": snappedf(gpu / n, 0.001),
		"frames_drawn": Engine.get_frames_drawn() - drawn0, "continuous": continuous,
		"fellow_enabled": ProjectSettings.get_setting("rendering/fellow/enabled", true),
		"adapter": RenderingServer.get_video_adapter_name(), "viewport_size": var_to_str(vp.size)}
	if subject_time0 != null:
		r["subject_time_advanced"] = snappedf(float(subject.get("time")) - float(subject_time0), 0.001)
	if subject and ClassDB.class_exists("FellowEditorTools"):
		r["subject_warnings"] = Array(ClassDB.class_call_static("FellowEditorTools", "get_configuration_warnings", subject))
	if out_dir != "" and DisplayServer.get_name() != "headless":
		# The view as the measurement left it (one frame drawn for the picture).
		RenderingServer.viewport_set_measure_render_time(vp.get_viewport_rid(), true)
		await RenderingServer.frame_post_draw
		var img := vp.get_texture().get_image()
		if img != null:
			img.save_png(out_dir.path_join("perf.png"))
			r["picture"] = out_dir.path_join("perf.png")
	print("EDITOR_PERF ", JSON.stringify(r))
	settings.set_setting("interface/editor/display/update_continuously", false)
	settings.mark_setting_changed("interface/editor/display/update_continuously")
	get_tree().quit(0)


# The configuration warnings of FellowEngine's nodes (milestone 52), through FellowEditorTools: small trees, each
# subject's warnings expected to contain a phrase, or to be none ("").
func _warnings_of(n: Node) -> PackedStringArray:
	return ClassDB.class_call_static("FellowEditorTools", "get_configuration_warnings", n)


func _expect_warning(case_name: String, n: Node, phrase: String) -> void:
	var w := _warnings_of(n)
	out["warnings"][case_name] = Array(w)
	if phrase == "":
		if not w.is_empty():
			_fail("warnings, %s: expected none, got %s" % [case_name, w])
		return
	for t in w:
		if phrase in t:
			return
	_fail("warnings, %s: expected one with \"%s\", got %s" % [case_name, phrase, w])


func _check_warnings() -> void:
	out["warnings"] = {}
	if not ClassDB.class_exists("FellowEditorTools"):
		_fail("FellowEditorTools missing")
		return
	# A scene with no gas volume or burning object open (the volume's and the burnable's warnings look there).
	EditorInterface.open_scene_from_path("res://idle/Node3D.tscn")
	await _frames(5)
	var box := Node3D.new()
	box.name = "FellowWarningsCheck"
	get_tree().root.add_child(box)
	var mk := func(cls: String, parent: Node, node_name := "") -> Node:
		var n: Node = ClassDB.instantiate(cls)
		if node_name != "":
			n.name = node_name
		parent.add_child(n)
		return n
	# Gas: an emitter outside a volume, one inside; a volume nothing fills.
	_expect_warning("emitter alone", mk.call("FellowGasEmitter3D", box), "direct child of a FellowGasVolume3D")
	var volume: Node = mk.call("FellowGasVolume3D", box)
	var emitter: Node = mk.call("FellowGasEmitter3D", volume)
	_expect_warning("emitter in a volume", emitter, "")
	_expect_warning("volume with an emitter", volume, "")
	_expect_warning("volume alone", mk.call("FellowGasVolume3D", box), "Nothing fills this volume")
	# Burning: no mesh; a mesh but no gas volume in the edited scene.
	_expect_warning("burnable without a mesh", mk.call("FellowBurnable3D", box), "burns its parent MeshInstance3D")
	var mesh := MeshInstance3D.new()
	mesh.mesh = BoxMesh.new()
	box.add_child(mesh)
	var burnable: Node = mk.call("FellowBurnable3D", mesh)
	_expect_warning("burnable, no gas volume in the scene", burnable, "No FellowGasVolume3D in the scene")
	burnable.set("gas_volume_path", burnable.get_path_to(volume))
	_expect_warning("burnable of a mesh, its gas volume", burnable, "")
	var empty_mesh := MeshInstance3D.new()
	box.add_child(empty_mesh)
	_expect_warning("burnable of an empty mesh", mk.call("FellowBurnable3D", empty_mesh), "has no mesh")
	# Paths leading to the wrong node.
	var water: Node = mk.call("FellowWater3D", box, "Water")
	water.set("simulation_path", NodePath("../Water"))
	_expect_warning("water's simulation path", water, "not a FellowShallowWater3D")
	water.set("simulation_path", NodePath(""))
	water.set("sun_path", NodePath("../Nowhere"))
	_expect_warning("water's sun path", water, "leads to no node")
	var weather: Node = mk.call("FellowWeather3D", box)
	weather.set("wind_path", NodePath("../Water"))
	_expect_warning("weather's wind path", weather, "not a FellowWind3D")
	# One rain and one wind at a time; snow tracks need the rain's.
	var rain_a: Node = mk.call("FellowRain3D", box)
	var rain_b: Node = mk.call("FellowRain3D", box)
	_expect_warning("the first of two rains", rain_a, "Another FellowRain3D")
	_expect_warning("the rain", rain_b, "")
	var tracker: Node = mk.call("FellowSnowTracker3D", box)
	_expect_warning("snow tracker", tracker, "")
	rain_b.set("tracks", false)
	_expect_warning("snow tracker, tracks off", tracker, "Tracks are off")
	var wind_a: Node = mk.call("FellowWind3D", box)
	mk.call("FellowWind3D", box)
	_expect_warning("the first of two winds", wind_a, "Another FellowWind3D")
	# Streaming: a cell outside a streamer, a streamer without cells, both right.
	_expect_warning("cell alone", mk.call("FellowStreamingCell3D", box), "under a FellowWorldStreamer3D")
	_expect_warning("streamer without cells", mk.call("FellowWorldStreamer3D", box), "No FellowStreamingCell3D")
	var streamer: Node = mk.call("FellowWorldStreamer3D", box)
	var cell: Node = mk.call("FellowStreamingCell3D", streamer)
	cell.set("scene_path", "res://split.tscn")
	_expect_warning("cell in a streamer", cell, "")
	_expect_warning("streamer with a cell", streamer, "")
	cell.set("scene_path", "res://gone.tscn")
	_expect_warning("cell of a missing scene", cell, "does not exist")
	# Animation: a matcher without a database, a character without a model.
	var skeleton := Skeleton3D.new()
	box.add_child(skeleton)
	_expect_warning("matcher without a database", mk.call("FellowMotionMatcher3D", skeleton), "needs a FellowMotionDatabase")
	_expect_warning("character without a model", mk.call("FellowMotionCharacter3D", box), "Model Path is empty")
	# A stock node's warning comes through as well.
	var shape := CollisionShape3D.new()
	box.add_child(shape)
	out["warnings"]["stock CollisionShape3D"] = Array(_warnings_of(shape))
	if _warnings_of(shape).is_empty():
		_fail("warnings: a CollisionShape3D outside a body and without a shape reported none")
	await _frames(2)
	box.queue_free()
	await _frames(2)


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
		# The editor redraws only when something changes (FellowEngine's nodes no longer change every frame there,
		# milestone 52): a write to the RenderingServer asks for the frame.
		RenderingServer.viewport_set_measure_render_time(EditorInterface.get_editor_viewport_3d(0).get_viewport_rid(), true)
		await RenderingServer.frame_post_draw
		var img := EditorInterface.get_editor_viewport_3d(0).get_texture().get_image()
		if img != null:
			DirAccess.make_dir_recursive_absolute(out_dir)
			img.save_png(out_dir.path_join("gizmos.png"))
			out["picture"] = out_dir.path_join("gizmos.png")


# Handles dragged with the mouse in the editor's 3D view (milestone 52): events sent to the window, as a person's
# would be; the property changed, and undone.
func _screen_of(vp: SubViewport, cam: Camera3D, p: Vector3) -> Vector2:
	var container := vp.get_parent() as Control
	return container.get_global_rect().position + cam.unproject_position(p) * (container.size / Vector2(vp.size))


func _mouse(pos: Vector2, button: int, held: bool) -> void:
	if button < 0:
		var m := InputEventMouseMotion.new()
		m.position = pos
		m.global_position = pos
		m.button_mask = MOUSE_BUTTON_MASK_LEFT if held else 0
		Input.parse_input_event(m)
	else:
		var b := InputEventMouseButton.new()
		b.position = pos
		b.global_position = pos
		b.button_index = MOUSE_BUTTON_LEFT
		b.pressed = button == 1
		b.button_mask = MOUSE_BUTTON_MASK_LEFT if button == 1 else 0
		Input.parse_input_event(b)
	await _frames(3)


func _drag(node: Node3D, from_local: Vector3, to_local: Vector3) -> bool:
	var selection := EditorInterface.get_selection()
	selection.clear()
	selection.add_node(node)
	await _frames(10)
	var vp := EditorInterface.get_editor_viewport_3d(0)
	var cam := vp.get_camera_3d()
	var a := _screen_of(vp, cam, node.global_transform * from_local)
	var b := _screen_of(vp, cam, node.global_transform * to_local)
	var rect := (vp.get_parent() as Control).get_global_rect()
	if not rect.has_point(a) or not rect.has_point(b):
		return false
	await _mouse(a, -1, false)
	await _mouse(a, 1, true)
	for i in range(1, 6):
		await _mouse(a.lerp(b, i / 5.0), -1, true)
	await _mouse(b, 0, false)
	return true


func _undo(node: Node) -> void:
	var ur := EditorInterface.get_editor_undo_redo()
	ur.get_history_undo_redo(ur.get_object_history_id(node)).undo()
	await _frames(2)


func _check_handles() -> void:
	var root := EditorInterface.get_edited_scene_root()
	if root == null or root.name != "Gizmos":
		_fail("handles: gizmos.tscn is not open")
		return
	var results := {}
	# [node, handle (local), where it is dragged (local), property, its expected value after the drag]
	var cases := [
		["Water", Vector3(0.5, 0, 0), Vector3(1.0, 0, 0), "extent", Vector2(2.0, 1.0)],
		["ShallowWater", Vector3(0, 0, 0.5), Vector3(0, 0, 0.75), "domain_size", Vector2(1.0, 1.5)],
		["Cell", Vector3(0.4, 0.4, 0), Vector3(0.8, 0.4, 0), "bounds", AABB(Vector3(-0.4, 0, -0.4), Vector3(1.2, 0.8, 0.8))],
		["SnowTracker", Vector3(0.2, 0, 0), Vector3(0.5, 0, 0), "radius", 0.5],
	]
	for c in cases:
		var node: Node3D = root.get_node(c[0])
		var before = node.get(c[3])
		if not await _drag(node, c[1], c[2]):
			_fail("handles, %s: off the screen" % c[0])
			continue
		var after = node.get(c[3])
		var ok := false
		match typeof(c[4]):
			TYPE_VECTOR2:
				ok = (after as Vector2).distance_to(c[4]) < 0.02
			TYPE_AABB:
				ok = (after as AABB).position.distance_to(c[4].position) < 0.02 and (after as AABB).size.distance_to(c[4].size) < 0.02
			_:
				ok = absf(float(after) - float(c[4])) < 0.02
		await _undo(node)
		var undone = node.get(c[3])
		results[c[0]] = {"before": var_to_str(before), "dragged": var_to_str(after), "undone": var_to_str(undone)}
		if not ok:
			_fail("handles, %s: %s is %s after the drag, not %s" % [c[0], c[3], after, c[4]])
		if undone != before:
			_fail("handles, %s: undo left %s at %s, not %s" % [c[0], c[3], undone, before])
	out["handles"] = results
	EditorInterface.get_selection().clear()


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
