extends SceneTree
## FellowEngine's editor check (milestone 50), after the editor split split.tscn into cells and saved it: the level
## loaded as a game would, its cells streamed in, every box back where split.tscn had it; the proxies baked for them
## (milestone 66) shown before, hidden after. Prints STREAM_BACK {json}.
##   godot --headless --path <copy> -s res://stream_back.gd


func _initialize() -> void:
	process_frame.connect(_run, CONNECT_ONE_SHOT)


func _run() -> void:
	var level: Node3D = load("res://split.tscn").instantiate()
	get_root().add_child(level)
	var streamer = level.get_node("Streamer")
	var proxies_shown_before := _proxies_shown(streamer)
	streamer.prefetch(Vector3.ZERO, 1000.0)
	var loaded: int = streamer.load_now()
	await process_frame
	var positions := {}
	for cell in streamer.get_children():
		if cell.has_method("get_content") and cell.get_content() != null:
			for n in cell.get_content().get_children():
				if n is MeshInstance3D:
					positions[String(n.name)] = n.global_position
	# split.tscn's boxes as the check project has them (the copy before the split is res://split_original.tscn).
	var original: Node3D = load("res://split_original.tscn").instantiate()
	var worst := 0.0
	var missing := 0
	for n in original.get_children():
		if n is MeshInstance3D:
			if positions.has(String(n.name)):
				worst = maxf(worst, (positions[String(n.name)] as Vector3).distance_to(n.position))
			else:
				missing += 1
	original.free()
	# Milestone 66: the proxies the editor baked, shown while their cells were unloaded, hidden once loaded.
	var r := {"cells_loaded": loaded, "boxes_back": positions.size(), "missing": missing, "worst_offset": worst,
		"monitor_cells_loaded": Performance.get_custom_monitor("FellowEngine/World cells loaded"),
		"proxies_shown_before": proxies_shown_before, "proxies_shown_after": _proxies_shown(streamer)}
	print("STREAM_BACK ", JSON.stringify(r))
	quit(0 if missing == 0 and positions.size() == 40 and worst < 0.001 and proxies_shown_before == loaded and r["proxies_shown_after"] == 0 else 1)


func _proxies_shown(streamer: Node) -> int:
	var shown := 0
	for cell in streamer.get_children():
		for n in cell.get_children():
			if n is MeshInstance3D and n.has_meta("_fellow_cell_proxy") and n.is_visible_in_tree():
				shown += 1
	return shown
