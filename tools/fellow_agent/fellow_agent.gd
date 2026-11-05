extends SceneTree
## FellowEngine's agent toolkit (milestone 51): the in-engine half of tools/fellow_agent/fellow_agent.py, which runs it
## as `godot --path <project> -s <this file> -- --cmd=<command> ...` and reads the one line it prints,
## `FELLOW_AGENT {json}`. Commands:
##   capture  a scene rendered from its camera or from framed views, PNGs and statistics per view
##   describe a scene's node tree as data: classes, transforms, bounds, the properties that differ from the defaults
##   check    every scene and script of the project (or those given) loaded, instantiated, run a few frames
##   run      a scene run for a while: frame times, errors, a final picture
## Errors and warnings are caught through a Logger (file, line, message, the script backtrace), per scene.

const VIEW_DIRECTIONS := {
	"iso": Vector3(1.0, 0.8, 1.0),
	"front": Vector3(0, 0.15, 1),
	"back": Vector3(0, 0.15, -1),
	"left": Vector3(-1, 0.15, 0),
	"right": Vector3(1, 0.15, 0),
	"top": Vector3(0, 1, 0),
}
const SKIP_PROPERTIES := ["script", "owner", "name", "unique_name_in_owner", "scene_file_path", "transform",
	"global_transform", "position", "rotation", "rotation_degrees", "quaternion", "basis", "scale", "global_position",
	"global_rotation", "global_rotation_degrees", "global_basis", "global_scale", "process_mode", "editor_description",
	"metadata/_edit_lock_", "metadata/_edit_group_", "rotation_edit_mode", "rotation_order", "top_level",
	"size", "offset_left", "offset_top", "offset_right", "offset_bottom", "anchor_left", "anchor_top", "anchor_right",
	"anchor_bottom", "layout_mode", "anchors_preset", "skew", "visible"]


class CaptureLogger extends Logger:
	var mutex := Mutex.new()
	var entries: Array = []
	var dropped := 0

	func _log_error(function: String, file: String, line: int, code: String, rationale: String, _editor_notify: bool, error_type: int, script_backtraces: Array[ScriptBacktrace]) -> void:
		var kinds := ["error", "warning", "script_error", "shader_error"]
		var e := {
			"type": kinds[error_type] if error_type >= 0 and error_type < kinds.size() else "error",
			"message": rationale if rationale != "" else code,
			"function": function,
			"file": file,
			"line": line,
		}
		if rationale != "" and code != "":
			e["condition"] = code
		var frames := []
		for bt in script_backtraces:
			if bt == null or bt.is_empty():
				continue
			for i in mini(bt.get_frame_count(), 8):
				frames.append("%s:%d %s()" % [bt.get_frame_file(i), bt.get_frame_line(i), bt.get_frame_function(i)])
		if not frames.is_empty():
			e["script_backtrace"] = frames
			# The script's own place is what an agent fixes.
			var top: String = frames[0]
			e["script_location"] = top.split(" ")[0]
		mutex.lock()
		if entries.size() < 500:
			entries.append(e)
		else:
			dropped += 1
		mutex.unlock()

	func _log_message(_message: String, _error: bool) -> void:
		pass

	func take() -> Array:
		mutex.lock()
		var out := entries
		entries = []
		mutex.unlock()
		return out


var args := {}
var logger := CaptureLogger.new()
var out_dir := ""


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--"):
			var kv := a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1] if kv.size() > 1 else "true"
	out_dir = args.get("out", "")
	if out_dir != "":
		DirAccess.make_dir_recursive_absolute(out_dir)
	OS.add_logger(logger)
	# A project in low processor mode draws only when something changes: the frames this script waits for would not
	# come for a still scene (milestone 52).
	OS.low_processor_usage_mode = false
	process_frame.connect(_main, CONNECT_ONE_SHOT)


func _finish(result: Dictionary, code: int) -> void:
	result["engine"] = Engine.get_version_info().string
	result["renderer"] = RenderingServer.get_current_rendering_method()
	result["driver"] = RenderingServer.get_current_rendering_driver_name()
	result["adapter"] = RenderingServer.get_video_adapter_name()
	result["debug_build"] = OS.is_debug_build()
	if logger.dropped > 0:
		result["errors_dropped"] = logger.dropped
	OS.remove_logger(logger)
	print("FELLOW_AGENT ", JSON.stringify(result, "", false))
	quit(code)


func _main() -> void:
	match args.get("cmd", ""):
		"capture":
			await _capture()
		"describe":
			_describe()
		"check":
			await _check()
		"run":
			await _run()
		_:
			_finish({"ok": false, "error": "unknown --cmd (capture, describe, check, run)"}, 2)


# ------------------------------------------------------------------ helpers

func _load_scene(path: String) -> Dictionary:
	var r := {"path": path}
	var t0 := Time.get_ticks_usec()
	if not ResourceLoader.exists(path):
		r["error"] = "no such resource"
		return r
	var res = ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_REUSE)
	r["load_ms"] = (Time.get_ticks_usec() - t0) / 1000.0
	var node: Node = null
	if res is PackedScene:
		node = res.instantiate()
	elif res is Script and res.can_instantiate() and ClassDB.is_parent_class(res.get_instance_base_type(), "Node"):
		# A script that builds its scene in code (a node script): an instance of it.
		node = res.new()
	else:
		r["error"] = "not a scene: %s" % [res]
		return r
	if node == null:
		r["error"] = "the scene did not instantiate"
		return r
	r["node"] = node
	return r


func _frames(n: int) -> void:
	for i in n:
		await process_frame


func _walk(node: Node, out: Array) -> void:
	out.append(node)
	for c in node.get_children():
		_walk(c, out)


func _scene_summary(root: Node) -> Dictionary:
	var nodes := []
	_walk(root, nodes)
	var classes := {}
	var scripts := {}
	var cameras := 0
	var lights := 0
	var meshes := 0
	for n in nodes:
		var c: String = n.get_class()
		classes[c] = classes.get(c, 0) + 1
		if n.get_script() != null:
			scripts[str(n.get_script().resource_path)] = true
		if n is Camera3D or n is Camera2D:
			cameras += 1
		if n is Light3D:
			lights += 1
		if n is MeshInstance3D or n is MultiMeshInstance3D:
			meshes += 1
	var top := classes.keys()
	top.sort_custom(func(a, b): return classes[a] > classes[b])
	var class_counts := {}
	for c in top.slice(0, 20):
		class_counts[c] = classes[c]
	return {"nodes": nodes.size(), "classes": class_counts, "scripts": scripts.keys(), "cameras": cameras,
		"lights": lights, "mesh_instances": meshes}


func _visual_bounds(root: Node) -> AABB:
	var nodes := []
	_walk(root, nodes)
	var have := false
	var box := AABB()
	for n in nodes:
		# Geometry only: a light's bounds are its range (a directional light's thousands of metres), a probe's its
		# extent (found framing Sponza from 12 km, milestone 51).
		if n is GeometryInstance3D and n.is_visible_in_tree():
			var b: AABB = n.global_transform * n.get_aabb()
			# Things as large as the sky (a world boundary, a sea to the horizon) would frame nothing.
			if b.size.length() > 20000.0 or not b.size.is_finite():
				continue
			box = b if not have else box.merge(b)
			have = true
	if not have:
		for n in nodes:
			if n is Node3D:
				var p: Vector3 = n.global_position
				box = AABB(p, Vector3.ZERO) if not have else box.expand(p)
				have = true
	if not have or box.size.length() < 0.01:
		box = AABB(box.position - Vector3.ONE, Vector3.ONE * 2.0)
	return box


func _has_class(root: Node, cls: String) -> bool:
	var nodes := []
	_walk(root, nodes)
	for n in nodes:
		if n.is_class(cls):
			return true
	return false


func _add_preview_lighting(root: Node) -> Array:
	# As the editor's viewport does when a scene has none: a sun and a sky, so an unlit scene is not black.
	var added := []
	if not _has_class(root, "DirectionalLight3D"):
		var sun := DirectionalLight3D.new()
		sun.name = "_FellowAgentSun"
		sun.shadow_enabled = true
		sun.rotation = Vector3(deg_to_rad(-50.0), deg_to_rad(-30.0), 0.0)
		get_root().add_child(sun)
		added.append("sun")
	if not _has_class(root, "WorldEnvironment") and get_root().world_3d.environment == null:
		var env := Environment.new()
		env.background_mode = Environment.BG_SKY
		var sky := Sky.new()
		sky.sky_material = ProceduralSkyMaterial.new()
		env.sky = sky
		env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
		env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
		var we := WorldEnvironment.new()
		we.name = "_FellowAgentEnvironment"
		we.environment = env
		get_root().add_child(we)
		added.append("environment")
	return added


func _place_camera(cam: Camera3D, box: AABB, view: String) -> void:
	# Looks at the box's centre from the view's direction, as near as fits every corner in the frame (and a margin).
	var dir: Vector3 = VIEW_DIRECTIONS.get(view, VIEW_DIRECTIONS["iso"]).normalized()
	cam.fov = 50.0
	cam.keep_aspect = Camera3D.KEEP_HEIGHT
	var centre := box.get_center()
	var up := Vector3.UP if absf(dir.y) < 0.99 else Vector3(0, 0, -1)
	var basis := Basis.looking_at(-dir, up)
	var size := get_root().get_visible_rect().size
	var aspect := size.x / maxf(size.y, 1.0)
	var tv := tan(deg_to_rad(cam.fov * 0.5))
	var th := tv * aspect
	var dist := 0.0
	for i in 8:
		var p := box.get_endpoint(i) - centre
		var local := basis.inverse() * p # x right, y up, z towards the camera
		dist = maxf(dist, local.z + absf(local.x) / th)
		dist = maxf(dist, local.z + absf(local.y) / tv)
	dist = maxf(dist * 1.08, 0.1)
	var radius := box.size.length() * 0.5
	cam.near = maxf((dist - radius) * 0.5, 0.01)
	cam.far = dist + radius * 4.0 + 10.0
	cam.global_transform = Transform3D(basis, centre + dir * dist)


func _image_stats(img: Image) -> Dictionary:
	if img == null or img.is_empty():
		return {}
	var small: Image = img.duplicate()
	small.convert(Image.FORMAT_RGB8)
	var w := small.get_width()
	var h := small.get_height()
	var step := maxi(1, int(sqrt(float(w * h) / 40000.0)))
	var n := 0
	var s := 0.0
	var s2 := 0.0
	var lo := 1.0
	var hi := 0.0
	var black := 0
	var white := 0
	for y in range(0, h, step):
		for x in range(0, w, step):
			var c := small.get_pixel(x, y)
			var l := 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b
			s += l
			s2 += l * l
			lo = minf(lo, l)
			hi = maxf(hi, l)
			if l < 2.0 / 255.0:
				black += 1
			if l > 253.0 / 255.0:
				white += 1
			n += 1
	var mean := s / n
	var spread := sqrt(maxf(s2 / n - mean * mean, 0.0))
	return {"size": [w, h], "luma_mean": snappedf(mean * 255.0, 0.01), "luma_spread": snappedf(spread * 255.0, 0.01),
		"luma_min": snappedf(lo * 255.0, 0.01), "luma_max": snappedf(hi * 255.0, 0.01),
		"black_fraction": snappedf(float(black) / n, 0.0001), "white_fraction": snappedf(float(white) / n, 0.0001),
		"blank": spread * 255.0 < 1.0}


func _set_scene_visible(root: Node, visible: bool) -> void:
	if root is Node3D or root is CanvasItem:
		root.visible = visible
	else:
		for c in root.get_children():
			if c is Node3D or c is CanvasItem:
				c.visible = visible


func _coverage(a: Image, b: Image) -> float:
	if a == null or b == null or a.get_size() != b.get_size():
		return -1.0
	var ia: Image = a.duplicate()
	var ib: Image = b.duplicate()
	ia.convert(Image.FORMAT_RGB8)
	ib.convert(Image.FORMAT_RGB8)
	var w := ia.get_width()
	var h := ia.get_height()
	var step := maxi(1, int(sqrt(float(w * h) / 40000.0)))
	var n := 0
	var changed := 0
	for y in range(0, h, step):
		for x in range(0, w, step):
			var ca := ia.get_pixel(x, y)
			var cb := ib.get_pixel(x, y)
			if maxf(absf(ca.r - cb.r), maxf(absf(ca.g - cb.g), absf(ca.b - cb.b))) > 8.0 / 255.0:
				changed += 1
			n += 1
	return snappedf(float(changed) / maxi(n, 1), 0.0001)


func _render_stats() -> Dictionary:
	var vp := get_root().get_viewport_rid()
	var d := {
		"draw_calls": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME),
		"objects": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_OBJECTS_IN_FRAME),
		"primitives": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME),
		"video_memory_mib": snappedf(RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_VIDEO_MEM_USED) / 1048576.0, 0.1),
		"render_cpu_ms": snappedf(RenderingServer.viewport_get_measured_render_time_cpu(vp), 0.001),
		"render_gpu_ms": snappedf(RenderingServer.viewport_get_measured_render_time_gpu(vp), 0.001),
	}
	var fellow := {}
	for m in Performance.get_custom_monitor_names():
		if String(m).begins_with("FellowEngine/"):
			fellow[String(m).substr(13)] = Performance.get_custom_monitor(m)
	if not fellow.is_empty():
		d["fellow_monitors"] = fellow
	return d


func _save(img: Image, file: String) -> String:
	if out_dir == "" or img == null:
		return ""
	var p := out_dir.path_join(file)
	img.save_png(p)
	return p


func _sheet(images: Array) -> Image:
	# The views side by side (two columns), each scaled to at most 640 pixels wide: one picture to look at.
	var cols := 2 if images.size() > 1 else 1
	var rows := ceili(images.size() / float(cols))
	var cw := 640
	var first: Image = images[0]
	var ch := int(round(first.get_height() * cw / float(first.get_width())))
	var sheet := Image.create_empty(cw * cols, ch * rows, false, Image.FORMAT_RGB8)
	sheet.fill(Color(0.1, 0.1, 0.1))
	for i in images.size():
		var im: Image = images[i].duplicate()
		im.convert(Image.FORMAT_RGB8)
		im.resize(cw, ch, Image.INTERPOLATE_BILINEAR)
		sheet.blit_rect(im, Rect2i(0, 0, cw, ch), Vector2i((i % cols) * cw, (i / cols) * ch))
	return sheet


# ------------------------------------------------------------------ capture

func _capture() -> void:
	var scene_path: String = args.get("scene", "")
	var settle := int(args.get("frames", "30"))
	var result := {"command": "capture", "scene": scene_path}
	var loaded := _load_scene(scene_path)
	result["load"] = {"ms": loaded.get("load_ms", 0.0)}
	if loaded.has("error"):
		result["ok"] = false
		result["error"] = loaded["error"]
		result["errors"] = logger.take()
		_finish(result, 1)
		return
	var root: Node = loaded["node"]
	get_root().add_child(root)
	RenderingServer.viewport_set_measure_render_time(get_root().get_viewport_rid(), true)
	# Frames for the scene to build itself (scripts that add their content in _ready or over a few frames) before
	# it is summarised, framed and lit.
	await _frames(int(args.get("build_frames", "10")))
	result["summary"] = _scene_summary(root)
	var is_3d := root is Node3D or _has_class(root, "VisualInstance3D")
	var views: Array = []
	var requested: String = args.get("views", "auto")
	var scene_cam: Camera3D = null
	if args.has("camera"):
		scene_cam = root.get_node_or_null(NodePath(args["camera"])) as Camera3D
		if scene_cam == null:
			logger.entries.append({"type": "error", "message": "no Camera3D at %s" % args["camera"], "file": "fellow_agent", "line": 0})
	if scene_cam == null and is_3d:
		scene_cam = get_root().get_camera_3d()
	if requested == "auto":
		views = ["screen"] if not is_3d else (["camera"] if scene_cam != null else ["iso"])
	else:
		views = requested.split(",", false)
	var lighting: String = args.get("preview_lighting", "auto")
	if is_3d and (lighting == "on" or lighting == "auto"):
		result["preview_lighting_added"] = _add_preview_lighting(root)
	var box := _visual_bounds(root) if is_3d else AABB()
	if is_3d:
		result["bounds"] = {"position": var_to_str(box.position), "size": var_to_str(box.size)}
	var agent_cam: Camera3D = null
	var images := []
	var view_results := []
	for v in views:
		var vr := {"view": v}
		if is_3d and v != "camera":
			if agent_cam == null:
				agent_cam = Camera3D.new()
				agent_cam.name = "_FellowAgentCamera"
				get_root().add_child(agent_cam)
			_place_camera(agent_cam, box, v)
			agent_cam.make_current()
			vr["camera"] = {"position": var_to_str(agent_cam.global_position), "fov": agent_cam.fov}
		elif is_3d and scene_cam != null:
			scene_cam.make_current()
			vr["camera"] = {"path": str(root.get_path_to(scene_cam)), "position": var_to_str(scene_cam.global_position)}
		await _frames(settle)
		await RenderingServer.frame_post_draw
		var img := get_root().get_texture().get_image()
		vr["image"] = _save(img, "view_%s.png" % v)
		vr["stats"] = _image_stats(img)
		vr["render"] = _render_stats()
		if is_3d and img != null and args.get("coverage", "true") == "true":
			# The same view with the scene hidden: the share of pixels the scene changes (0: the camera does not see
			# it — too far, behind it, or nothing drawn).
			_set_scene_visible(root, false)
			await _frames(3)
			await RenderingServer.frame_post_draw
			var empty := get_root().get_texture().get_image()
			_set_scene_visible(root, true)
			vr["scene_coverage"] = _coverage(img, empty)
			if vr["scene_coverage"] < 0.01:
				vr["warning"] = "the scene covers %.2f %% of the picture: the camera may not see it" % (vr["scene_coverage"] * 100.0)
			await _frames(3)
		view_results.append(vr)
		if img != null:
			images.append(img)
	result["views"] = view_results
	if images.size() > 1:
		result["sheet"] = _save(_sheet(images), "sheet.png")
	result["errors"] = logger.take()
	result["ok"] = true
	_finish(result, 0)


# ------------------------------------------------------------------ describe

var _defaults := {}


func _default_of(cls: String) -> Object:
	if not _defaults.has(cls):
		var o: Object = null
		if ClassDB.can_instantiate(cls):
			o = ClassDB.instantiate(cls)
		_defaults[cls] = o
	return _defaults[cls]


func _short(v) -> Variant:
	if v is Object:
		if v == null:
			return null
		if v is Resource:
			var r: Resource = v
			return "%s(%s)" % [r.get_class(), r.resource_path if r.resource_path != "" and not r.resource_path.contains("::") else "embedded"]
		return v.get_class()
	var s := var_to_str(v)
	return s if s.length() <= 160 else s.substr(0, 157) + "..."


func _describe_node(n: Node, root: Node, depth: int, max_depth: int, budget: Array) -> Dictionary:
	budget[0] -= 1
	var d := {"name": String(n.name), "class": n.get_class()}
	if n != root and n.owner != root and n.owner != null and n.owner != n:
		d["from_instance"] = true
	if n.scene_file_path != "" and n != root:
		d["instance_of"] = n.scene_file_path
	if n.get_script() != null:
		d["script"] = n.get_script().resource_path
	if n is Node3D:
		d["position"] = var_to_str(n.position)
		if n.rotation != Vector3.ZERO:
			d["rotation_degrees"] = var_to_str(n.rotation_degrees)
		if n.scale != Vector3.ONE:
			d["scale"] = var_to_str(n.scale)
		if not n.visible:
			d["visible"] = false
	elif n is Node2D:
		d["position"] = var_to_str(n.position)
		if n.rotation != 0.0:
			d["rotation_degrees"] = n.rotation_degrees
		if not n.visible:
			d["visible"] = false
	elif n is Control:
		d["rect"] = var_to_str(Rect2(n.position, n.size))
		if not n.visible:
			d["visible"] = false
	if n is VisualInstance3D:
		d["aabb"] = var_to_str(n.get_aabb())
	var def := _default_of(n.get_class())
	var props := {}
	for p in n.get_property_list():
		var pname: String = p["name"]
		if not (int(p["usage"]) & PROPERTY_USAGE_STORAGE) or pname in SKIP_PROPERTIES or pname.begins_with("metadata/"):
			continue
		var v = n.get(pname)
		if def != null and pname in def:
			var dv = def.get(pname)
			if typeof(v) == typeof(dv) and v == dv:
				continue
		elif v == null:
			continue
		props[pname] = _short(v)
	if not props.is_empty():
		d["properties"] = props
	if n.get_child_count() > 0:
		if depth >= max_depth or budget[0] <= 0:
			d["children_not_listed"] = n.get_child_count()
		else:
			var kids := []
			for c in n.get_children():
				if budget[0] <= 0:
					d["children_not_listed"] = n.get_child_count() - kids.size()
					break
				kids.append(_describe_node(c, root, depth + 1, max_depth, budget))
			d["children"] = kids
	return d


func _describe() -> void:
	var scene_path: String = args.get("scene", "")
	var result := {"command": "describe", "scene": scene_path}
	var loaded := _load_scene(scene_path)
	if loaded.has("error"):
		result["ok"] = false
		result["error"] = loaded["error"]
		result["errors"] = logger.take()
		_finish(result, 1)
		return
	var root: Node = loaded["node"]
	var budget := [int(args.get("max_nodes", "400"))]
	result["tree"] = _describe_node(root, root, 0, int(args.get("depth", "12")), budget)
	result["summary"] = _scene_summary(root)
	result["dependencies"] = ResourceLoader.get_dependencies(scene_path)
	root.free()
	for o in _defaults.values():
		if o != null and not o is RefCounted:
			o.free()
	result["errors"] = logger.take()
	result["ok"] = true
	_finish(result, 0)


# ------------------------------------------------------------------ check

func _collect(dir: String, exts: Array, out: Array) -> void:
	for f in DirAccess.get_files_at(dir):
		if f.get_extension() in exts:
			out.append(dir.path_join(f))
	for d in DirAccess.get_directories_at(dir):
		if not d.begins_with(".") and d != "addons" or (d == "addons" and args.get("addons", "false") == "true"):
			_collect(dir.path_join(d), exts, out)


# Every node's configuration warnings (the Scene dock's warning icons), once the scene is in the tree and has run:
# FellowEditorTools, in editor builds (milestone 52). They are reported, and do not fail the check.
func _configuration_warnings(root: Node) -> Array:
	if not ClassDB.class_exists("FellowEditorTools"):
		return []
	var found := []
	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		var w: PackedStringArray = ClassDB.class_call_static("FellowEditorTools", "get_configuration_warnings", n)
		for t in w:
			found.append({"node": String(root.get_path_to(n)), "class": n.get_class(), "warning": t})
		for c in n.get_children():
			stack.append(c)
	return found


func _check() -> void:
	var frames := int(args.get("frames", "5"))
	var result := {"command": "check"}
	var scenes := []
	var scripts := []
	if args.has("scenes"):
		scenes = Array(String(args["scenes"]).split(",", false))
	else:
		_collect("res://", ["tscn", "scn"], scenes)
		_collect("res://", ["gd"], scripts)
	logger.take() # what the start printed
	var script_results := []
	for s in scripts:
		var r := {"path": s}
		var res = ResourceLoader.load(s, "", ResourceLoader.CACHE_MODE_REUSE)
		var errs := logger.take()
		r["ok"] = res is Script and res.can_instantiate() and errs.filter(func(e): return e["type"] != "warning").is_empty()
		if not errs.is_empty():
			r["errors"] = errs
		script_results.append(r)
	var scene_results := []
	for s in scenes:
		var r := {"path": s}
		var missing := []
		for dep in ResourceLoader.get_dependencies(s):
			var p: String = dep.get_slice("::", 2) if dep.contains("::") else dep
			if p != "" and not ResourceLoader.exists(p) and not FileAccess.file_exists(p):
				missing.append(p)
		if not missing.is_empty():
			r["missing_dependencies"] = missing
		var loaded := _load_scene(s)
		r["load_ms"] = loaded.get("load_ms", 0.0)
		if loaded.has("error"):
			r["error"] = loaded["error"]
		else:
			var root: Node = loaded["node"]
			get_root().add_child(root)
			await _frames(frames)
			r["nodes"] = _scene_summary(root)["nodes"]
			var warnings := _configuration_warnings(root)
			if not warnings.is_empty():
				r["configuration_warnings"] = warnings
			root.queue_free()
			await _frames(1)
		var errs := logger.take()
		if not errs.is_empty():
			r["errors"] = errs
		r["ok"] = not r.has("error") and missing.is_empty() and errs.filter(func(e): return e["type"] != "warning").is_empty()
		scene_results.append(r)
	result["scripts"] = script_results
	result["scenes"] = scene_results
	var bad_scripts := script_results.filter(func(r): return not r["ok"]).size()
	var bad_scenes := scene_results.filter(func(r): return not r["ok"]).size()
	var warned := 0
	for r in scene_results:
		warned += r.get("configuration_warnings", []).size()
	result["summary"] = {"scripts": script_results.size(), "scripts_failed": bad_scripts, "scenes": scene_results.size(),
		"scenes_failed": bad_scenes, "configuration_warnings": warned}
	if not ClassDB.class_exists("FellowEditorTools"):
		result["hint_configuration_warnings"] = "not available: a release template (an editor build reports them)"
	result["ok"] = bad_scripts == 0 and bad_scenes == 0
	_finish(result, 0 if result["ok"] else 1)


# ------------------------------------------------------------------ run

func _percentile(sorted: Array, q: float) -> float:
	if sorted.is_empty():
		return 0.0
	return sorted[mini(int(ceil(q * sorted.size())) - 1, sorted.size() - 1)]


func _run() -> void:
	var scene_path: String = args.get("scene", "")
	var seconds := float(args.get("seconds", "5"))
	var warmup := int(args.get("warmup", "30"))
	var result := {"command": "run", "scene": scene_path}
	var loaded := _load_scene(scene_path)
	if loaded.has("error"):
		result["ok"] = false
		result["error"] = loaded["error"]
		result["errors"] = logger.take()
		_finish(result, 1)
		return
	var root: Node = loaded["node"]
	get_root().add_child(root)
	RenderingServer.viewport_set_measure_render_time(get_root().get_viewport_rid(), true)
	await _frames(int(args.get("build_frames", "10")))
	# A 3D scene without a camera of its own is run from the iso view, lit as capture lights it.
	if (root is Node3D or _has_class(root, "VisualInstance3D")) and get_root().get_camera_3d() == null:
		if args.get("preview_lighting", "auto") != "off":
			result["preview_lighting_added"] = _add_preview_lighting(root)
		var cam := Camera3D.new()
		cam.name = "_FellowAgentCamera"
		get_root().add_child(cam)
		_place_camera(cam, _visual_bounds(root), "iso")
		cam.make_current()
		result["camera"] = {"view": "iso", "position": var_to_str(cam.global_position)}
	await _frames(warmup)
	var frame_ms := []
	var gpu_ms := []
	var t_end := Time.get_ticks_usec() + int(seconds * 1e6)
	var last := Time.get_ticks_usec()
	while Time.get_ticks_usec() < t_end:
		await process_frame
		var now := Time.get_ticks_usec()
		frame_ms.append((now - last) / 1000.0)
		last = now
		gpu_ms.append(RenderingServer.viewport_get_measured_render_time_gpu(get_root().get_viewport_rid()))
	var sorted := frame_ms.duplicate()
	sorted.sort()
	var total := 0.0
	for f in frame_ms:
		total += f
	var gsum := 0.0
	for g in gpu_ms:
		gsum += g
	result["frames"] = frame_ms.size()
	result["frame_ms"] = {"mean": snappedf(total / maxi(frame_ms.size(), 1), 0.001), "p50": snappedf(_percentile(sorted, 0.5), 0.001),
		"p95": snappedf(_percentile(sorted, 0.95), 0.001), "p99": snappedf(_percentile(sorted, 0.99), 0.001),
		"max": snappedf(sorted[-1] if not sorted.is_empty() else 0.0, 0.001)}
	result["gpu_ms_mean"] = snappedf(gsum / maxi(gpu_ms.size(), 1), 0.001)
	result["render"] = _render_stats()
	result["summary"] = _scene_summary(root)
	if args.get("picture", "true") == "true":
		await RenderingServer.frame_post_draw
		var img := get_root().get_texture().get_image()
		result["image"] = _save(img, "run_end.png")
		result["image_stats"] = _image_stats(img)
	result["errors"] = logger.take()
	result["ok"] = true
	_finish(result, 0)
