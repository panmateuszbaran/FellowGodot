extends SceneTree
## Packs this project's files into a .pck for the web check (a template build cannot export):
##   godot --headless --path benchmarks/tools/platforms/web/project -s res://make_pck.gd -- --out=<file.pck>


func _initialize() -> void:
	var out := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.substr(6)
	var packer := PCKPacker.new()
	var err := packer.pck_start(out)
	var files := []
	_collect("res://", files)
	for f in files:
		err = err if err != OK else packer.add_file(f, ProjectSettings.globalize_path(f))
	# The global script class cache an export writes (no class_name scripts here).
	var cache := ConfigFile.new()
	cache.set_value("", "list", [])
	cache.save("user://global_script_class_cache.cfg")
	err = err if err != OK else packer.add_file("res://.godot/global_script_class_cache.cfg", OS.get_user_data_dir().path_join("global_script_class_cache.cfg"))
	err = err if err != OK else packer.flush()
	print("MAKE_PCK ", JSON.stringify({"out": out, "files": files, "error": err}))
	quit(0 if err == OK else 1)


func _collect(dir: String, files: Array) -> void:
	for f in DirAccess.get_files_at(dir):
		if f != "make_pck.gd" and not f.ends_with(".uid"):
			files.append(dir.path_join(f))
	for d in DirAccess.get_directories_at(dir):
		if not d.begins_with("."):
			_collect(dir.path_join(d), files)
