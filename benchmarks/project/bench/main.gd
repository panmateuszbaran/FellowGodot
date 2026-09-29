extends Node
## FellowBench entry point. Behaviour is driven by user CLI args (after "--"):
##   --scene=<pbr|sponza|bistro_exterior|bistro_interior|fluid>
##   --mode=<perf|capture|temporal|reference|list>
##   --out=<dir>              output directory (created if needed)
##   --tag=<label>            free label stored in results (e.g. stock, fellow)
##   --run=<n>                run index (stored in results)
##   --warmup=<s>             perf warmup seconds (default 10; at least one full flythrough pass)
##   --measure=<s>            perf measurement seconds (default 30; whole flythrough passes only)
##   --percam=<frames>        perf: additionally measure each fixed camera statically for N frames (0 = off)
##   --preset=<name>          graphics preset (bench/presets.gd), default "high"
##   --set=<k=v,...>          explicit preset overrides (exported to results)
##   --fellow=<k=v,...>       FellowEngine toggles under rendering/fellow/* (no-op on stock)
##   --cameras=<a,b>          restrict capture/reference to these fixed cameras
##   --seqs=<a,b>             restrict temporal sequences
##   --settle=<frames>        frames rendered before a still capture (default 96, fills TAA history)
##   --hdr=1                  capture: also write linear pre-tonemap EXR (linear tonemapper, no glow, HDR 2D)
##   --jitter=<n>             reference: sub-pixel jittered renders per output frame (default 16)
##   --profile=1              perf: collect per-pass GPU timestamps (run godot with --gpu-profile). Changes GPU
##                            scheduling (timestamps are barriers) -> reported separately from normal perf runs.
## Run with:  godot --path benchmarks/project --fixed-fps 60 --disable-vsync --resolution 1920x1080 --position 0,0 -- --scene=pbr --mode=perf --out=...

const Presets = preload("res://bench/presets.gd")
const Metrics = preload("res://bench/metrics.gd")
const SCENES := {
	"pbr": "res://scenes/pbr_lab.gd",
	"sponza": "res://scenes/sponza.gd",
	"bistro_exterior": "res://scenes/bistro.gd",
	"bistro_interior": "res://scenes/bistro.gd",
	"fluid": "res://scenes/fluid_lab.gd",
}
const DT := 1.0 / 60.0

var args := {}
var scene  # scene_base.gd instance
var preset := {}
var fellow := {}
var result := {}
var out_dir := ""
var vp: Viewport
var frame_counter := 0
var t_start_usec := 0


func _ready() -> void:
	t_start_usec = Time.get_ticks_usec()
	args = _parse_args(OS.get_cmdline_user_args())
	# Watchdog: never hang a benchmark session (e.g. an asset that fails to load mid-run).
	var wd := float(args.get("watchdog", "1800"))
	get_tree().create_timer(wd, true, false, true).timeout.connect(func(): _fail("watchdog timeout after %.0f s" % wd))
	vp = get_viewport()
	# Benchmark rules: no vsync, no FPS cap (both are also set in project.godot / CLI).
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	out_dir = args.get("out", OS.get_user_data_dir().path_join("results"))
	DirAccess.make_dir_recursive_absolute(out_dir)
	var scene_key: String = args.get("scene", "pbr")
	if not SCENES.has(scene_key):
		_fail("unknown scene " + scene_key)
		return
	result = {
		"schema": 1,
		"scene": scene_key,
		"mode": args.get("mode", "perf"),
		"tag": args.get("tag", ""),
		"run": int(args.get("run", "0")),
		"cmdline_user_args": Array(OS.get_cmdline_user_args()),
		"cmdline_args": Array(OS.get_cmdline_args()),
		"engine": Engine.get_version_info(),
		"engine_build": {"debug_build": OS.is_debug_build(), "has_editor": OS.has_feature("editor"), "template_release": OS.has_feature("template_release")},
		"adapter": {
			"name": RenderingServer.get_video_adapter_name(),
			"vendor": RenderingServer.get_video_adapter_vendor(),
			"api_version": RenderingServer.get_video_adapter_api_version(),
			"type": RenderingServer.get_video_adapter_type(),
			"driver": OS.get_video_adapter_driver_info(),
			"rendering_driver": RenderingServer.get_current_rendering_driver_name(),
			"rendering_method": RenderingServer.get_current_rendering_method(),
		},
		"os": {"name": OS.get_name(), "version": OS.get_version(), "cpu": OS.get_processor_name(), "cpu_count": OS.get_processor_count()},
	}
	fellow = Presets.apply_fellow(args.get("fellow", ""))
	result["fellow"] = fellow

	_log("building scene " + scene_key)
	var scr = load(SCENES[scene_key])
	if scr == null or not scr.can_instantiate():
		_fail("scene script failed to load: " + SCENES[scene_key])
		return
	scene = scr.new()
	scene.name = "BenchScene"
	add_child(scene)
	var t_build := Time.get_ticks_usec()
	await scene.build(args.merged({"scene": scene_key}))
	if scene.load_error != "":
		_fail(scene.load_error)
		return
	_log("scene built in %.0f ms" % ((Time.get_ticks_usec() - t_build) / 1000.0))
	preset = Presets.build(args.get("preset", "high"), Presets.parse_overrides(args.get("set", "")))
	Presets.apply(preset, vp, scene.env, scene.cam_attr)
	RenderingServer.viewport_set_measure_render_time(vp.get_viewport_rid(), true)
	await get_tree().process_frame
	await _post_draw()
	result["load_time_ms"] = (Time.get_ticks_usec() - t_build) / 1000.0
	result["startup_to_first_frame_ms"] = (Time.get_ticks_usec() - t_start_usec) / 1000.0
	result["preset"] = preset
	result["effective_settings"] = Presets.effective(vp, scene.env, scene.cam_attr)
	result["scene_desc"] = scene.describe()
	var want := _parse_res(args.get("res", ""))
	var got := Vector2i(vp.get_visible_rect().size)
	result["resolution"] = [got.x, got.y]
	if want != Vector2i.ZERO and want != got:
		result["valid"] = false
		result["invalid_reason"] = "viewport size %s != requested %s" % [got, want]
	else:
		result["valid"] = true

	_log("starting mode " + str(result["mode"]))
	match result["mode"]:
		"perf":
			await _run_perf()
		"capture":
			await _run_capture()
		"temporal":
			await _run_temporal(false)
		"reference":
			await _run_reference()
		"list":
			pass
		_:
			_fail("unknown mode " + str(result["mode"]))
			return
	_finish()


# ---------------------------------------------------------------- perf

func _pipeline_compilations() -> Dictionary:
	return {
		"canvas": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_CANVAS),
		"mesh": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_MESH),
		"surface": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_SURFACE),
		"draw": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_DRAW),
		"specialization": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_PIPELINE_COMPILATIONS_SPECIALIZATION),
	}


func _mem() -> Dictionary:
	return {
		"video_mem_used": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_VIDEO_MEM_USED),
		"texture_mem_used": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TEXTURE_MEM_USED),
		"buffer_mem_used": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_BUFFER_MEM_USED),
		"static_mem": OS.get_static_memory_usage(),
		"static_mem_peak": OS.get_static_memory_peak_usage(),
	}


func _pose_at(t: float) -> void:
	scene.set_time(t)
	scene.apply_path_sample(scene.flythrough.sample(t))


func _run_perf() -> void:
	var warmup_s := float(args.get("warmup", "10"))
	var measure_s := float(args.get("measure", "30"))
	var dur: float = scene.flythrough.duration()
	var frames_per_pass := int(ceil(dur / DT)) + 1
	var profile: bool = args.get("profile", "0") == "1"
	var rd := RenderingServer.get_rendering_device()

	# 1) Warmup: shader/pipeline warmup by visiting every fixed camera, then flythrough passes until
	#    >= warmup_s wall-clock seconds AND at least one complete pass.
	var pc0 := _pipeline_compilations()
	var w0 := Time.get_ticks_usec()
	for k in scene.fixed_cameras.keys():
		var c: Dictionary = scene.fixed_cameras[k]
		scene.set_time(c.get("time", 0.0))
		scene.set_camera_pose(c["pos"], c["target"], c.get("fov", 60.0))
		for i in range(20):
			await get_tree().process_frame
	var warm_passes := 0
	while true:
		for f in range(frames_per_pass):
			_pose_at(f * DT)
			await get_tree().process_frame
		warm_passes += 1
		if (Time.get_ticks_usec() - w0) / 1e6 >= warmup_s:
			break
	var pc1 := _pipeline_compilations()
	result["warmup"] = {"seconds": (Time.get_ticks_usec() - w0) / 1e6, "passes": warm_passes, "pipeline_compilations": _dict_sub(pc1, pc0)}

	# 2) Measurement: whole passes until >= measure_s.
	var m := Metrics.new(vp.get_viewport_rid())
	var pass_list := []
	var pass_frames := []
	var gpu_passes := {}
	var gpu_pass_frames := 0
	var last_ts_frame := -1
	var m0 := Time.get_ticks_usec()
	var mem_peak := _mem()
	await get_tree().process_frame
	m.reset_clock()
	var passes := 0
	while true:
		for f in range(frames_per_pass):
			_pose_at(f * DT)
			await get_tree().process_frame
			m.sample()
			pass_frames.append(f)
			if profile and rd:
				var fr := rd.get_captured_timestamps_frame()
				if fr != last_ts_frame:
					last_ts_frame = fr
					_accumulate_timestamps(rd, gpu_passes)
					gpu_pass_frames += 1
		passes += 1
		var mm := _mem()
		for k in mm.keys():
			mem_peak[k] = max(mem_peak[k], mm[k])
		if (Time.get_ticks_usec() - m0) / 1e6 >= measure_s:
			break
	var pc2 := _pipeline_compilations()
	result["measurement"] = {
		"seconds": (Time.get_ticks_usec() - m0) / 1e6,
		"passes": passes,
		"frames_per_pass": frames_per_pass,
		"pipeline_compilations": _dict_sub(pc2, pc1),
		"profile_timestamps": profile,
	}
	result["summary"] = m.summary()
	result["frames"] = m.to_json_arrays()
	result["frames"]["path_frame"] = pass_frames
	result["memory_peak"] = mem_peak
	if profile:
		var avg := {}
		for k in gpu_passes.keys():
			avg[k] = gpu_passes[k] / max(gpu_pass_frames, 1)
		result["gpu_passes_ms"] = avg
		result["gpu_passes_frames"] = gpu_pass_frames

	# 3) Optional static per-camera measurement.
	var percam := int(args.get("percam", "0"))
	if percam > 0:
		var cams := {}
		for k in scene.fixed_cameras.keys():
			var c: Dictionary = scene.fixed_cameras[k]
			scene.set_time(c.get("time", 0.0))
			scene.set_camera_pose(c["pos"], c["target"], c.get("fov", 60.0))
			for i in range(60):
				await get_tree().process_frame
			var mc := Metrics.new(vp.get_viewport_rid())
			var cam_gpu := {}
			var cam_gpu_frames := 0
			mc.reset_clock()
			for i in range(percam):
				await get_tree().process_frame
				mc.sample()
				if profile and rd:
					var fr := rd.get_captured_timestamps_frame()
					if fr != last_ts_frame:
						last_ts_frame = fr
						_accumulate_timestamps(rd, cam_gpu)
						cam_gpu_frames += 1
			var entry := {"summary": mc.summary()}
			if profile:
				var a := {}
				for kk in cam_gpu.keys():
					a[kk] = cam_gpu[kk] / max(cam_gpu_frames, 1)
				entry["gpu_passes_ms"] = a
			cams[k] = entry
		result["per_camera"] = cams


func _accumulate_timestamps(rd: RenderingDevice, acc: Dictionary) -> void:
	var n := rd.get_captured_timestamps_count()
	if n < 2:
		return
	for i in range(n - 1):
		var nm := rd.get_captured_timestamp_name(i)
		if nm.begins_with("<") or nm.begins_with(">"):
			continue
		var dt_ms := (rd.get_captured_timestamp_gpu_time(i + 1) - rd.get_captured_timestamp_gpu_time(i)) / 1e6
		acc[nm] = acc.get(nm, 0.0) + dt_ms
	acc["__total__"] = acc.get("__total__", 0.0) + (rd.get_captured_timestamp_gpu_time(n - 1) - rd.get_captured_timestamp_gpu_time(0)) / 1e6


func _dict_sub(a: Dictionary, b: Dictionary) -> Dictionary:
	var o := {}
	for k in a.keys():
		o[k] = a[k] - b.get(k, 0)
	return o


# ---------------------------------------------------------------- captures

func _selected(names: Array, filter_key: String) -> Array:
	var f: String = args.get(filter_key, "")
	if f.is_empty():
		return names
	var allow := f.split(",", false)
	return names.filter(func(n): return n in allow)


func _save_frame(path: String, hdr: bool = false) -> void:
	var img := vp.get_texture().get_image()
	if img == null:
		push_error("[FellowBench] could not read back viewport image for " + path)
		return
	if hdr:
		img.save_exr(path, false)
	else:
		if img.get_format() != Image.FORMAT_RGB8:
			img.convert(Image.FORMAT_RGB8)
		img.save_png(path)


func _file_stem() -> String:
	var r: Array = result["resolution"]
	return "%s_%dx%d" % [result["scene"], r[0], r[1]]


func _run_capture() -> void:
	var settle := int(args.get("settle", "96"))
	var files := []
	var stem := _file_stem()
	var cams := _selected(scene.fixed_cameras.keys(), "cameras")
	for k in cams:
		var c: Dictionary = scene.fixed_cameras[k]
		scene.set_time(c.get("time", 0.0))
		scene.set_camera_pose(c["pos"], c["target"], c.get("fov", 60.0))
		for i in range(settle):
			await get_tree().process_frame
		await _post_draw()
		var p := out_dir.path_join("%s_%s.png" % [stem, k])
		_save_frame(p)
		files.append(p.get_file())
	# Optional: frames sampled along the flythrough (to review camera paths / coverage).
	var pathshots := int(args.get("pathshots", "0"))
	if pathshots > 0 and scene.flythrough:
		var dur: float = scene.flythrough.duration()
		for i in range(pathshots):
			var t := dur * i / pathshots
			for f in range(30):
				_pose_at(t)
				await get_tree().process_frame
			await _post_draw()
			var p := out_dir.path_join("%s_path%02d.png" % [stem, i])
			_save_frame(p)
			files.append(p.get_file())
	if args.get("hdr", "0") == "1":
		# Linear, pre-tonemap HDR: HDR 2D viewport + linear tonemapper at exposure 1, glow off.
		vp.use_hdr_2d = true
		scene.env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
		scene.env.tonemap_exposure = 1.0
		scene.env.glow_enabled = false
		scene.env.adjustment_enabled = false
		for k in cams:
			var c: Dictionary = scene.fixed_cameras[k]
			scene.set_time(c.get("time", 0.0))
			scene.set_camera_pose(c["pos"], c["target"], c.get("fov", 60.0))
			for i in range(settle):
				await get_tree().process_frame
			await _post_draw()
			var p := out_dir.path_join("%s_%s.exr" % [stem, k])
			_save_frame(p, true)
			files.append(p.get_file())
	result["files"] = files


## Temporal sequences and their ground truth must see the same exposure: auto exposure adapts per rendered
## frame, and reference mode renders many sub-frames per output frame, so it is disabled for both modes.
func _freeze_exposure() -> void:
	if scene.cam_attr and scene.cam_attr.auto_exposure_enabled:
		scene.cam_attr.auto_exposure_enabled = false
		result["auto_exposure_disabled_for_mode"] = true


func _run_temporal(jittered_reference: bool) -> void:
	_freeze_exposure()
	var files := {}
	var stem := _file_stem()
	var pre_roll := int(args.get("preroll", "32"))
	for sname in _selected(scene.temporal_seqs.keys(), "seqs"):
		var s: Dictionary = scene.temporal_seqs[sname]
		var path = s["path"]
		var start: float = s["start"]
		var nframes: int = s["frames"]
		var lst := []
		# Pre-roll: run the sequence backwards in time so TAA history is "in motion" when capture starts.
		for i in range(pre_roll):
			var t := start + (i - pre_roll) * DT
			scene.set_time(t)
			scene.apply_path_sample(path.sample(t - start))
			await get_tree().process_frame
		for f in range(nframes):
			var t := start + f * DT
			scene.set_time(t)
			scene.apply_path_sample(path.sample(t - start))
			await get_tree().process_frame
			await _post_draw()
			var p := out_dir.path_join("%s_seq-%s_f%03d.png" % [stem, sname, f])
			_save_frame(p)
			lst.append(p.get_file())
		files[sname] = lst
	result["files"] = files


## Ground-truth-style reference: every output frame = average of N sub-pixel jittered renders
## (averaging is done offline by benchmarks/tools/accumulate.py). Intended with preset "reference_ssaa"
## (2x2 SSAA + 8x MSAA, TAA off). Scene time is frozen per output frame while jittering.
func _run_reference() -> void:
	_freeze_exposure()
	var jit := int(args.get("jitter", "16"))
	var files := {}
	var stem := _file_stem()
	var h: float = result["resolution"][1]
	var targets := []
	for k in _selected(scene.fixed_cameras.keys(), "cameras"):
		var c: Dictionary = scene.fixed_cameras[k]
		targets.append({"name": "cam-" + k, "time": c.get("time", 0.0), "pose": [c["pos"], c["target"], c.get("fov", 60.0)]})
	if args.get("seqs", "") != "":
		for sname in _selected(scene.temporal_seqs.keys(), "seqs"):
			var s: Dictionary = scene.temporal_seqs[sname]
			var step := int(args.get("seq_step", "1"))
			for f in range(0, s["frames"], step):
				targets.append({"name": "seq-%s_f%03d" % [sname, f], "time": s["start"] + f * DT, "seq": s, "seq_t": f * DT})
	for tg in targets:
		scene.set_time(tg["time"])
		if tg.has("seq"):
			scene.apply_path_sample(tg["seq"]["path"].sample(tg["seq_t"]))
		else:
			scene.set_camera_pose(tg["pose"][0], tg["pose"][1], tg["pose"][2])
		var fov: float = scene.camera.fov
		var near: float = scene.camera.near
		var far: float = scene.camera.far
		var xf: Transform3D = scene.camera.global_transform
		var size := 2.0 * near * tan(deg_to_rad(fov) * 0.5)
		var lst := []
		for j in range(jit):
			var jx := _halton(j + 1, 2) - 0.5
			var jy := _halton(j + 1, 3) - 0.5
			scene.camera.set_frustum(size, Vector2(jx, jy) * (size / h), near, far)
			scene.camera.global_transform = xf
			for i in range(3):  # let shadows/SS effects settle on the new projection
				await get_tree().process_frame
			await _post_draw()
			var p := out_dir.path_join("%s_ref-%s_j%02d.png" % [stem, tg["name"], j])
			_save_frame(p)
			lst.append(p.get_file())
		scene.camera.set_perspective(fov, near, far)
		files[tg["name"]] = lst
	result["files"] = files
	result["jitter_samples"] = jit


func _halton(i: int, b: int) -> float:
	var f := 1.0
	var r := 0.0
	while i > 0:
		f /= b
		r += f * (i % b)
		i = i / b
	return r


# ---------------------------------------------------------------- output

func _finish() -> void:
	result["total_runtime_s"] = (Time.get_ticks_usec() - t_start_usec) / 1e6
	var r: Array = result.get("resolution", [0, 0])
	var fname := "%s_%s_%s_%dx%d_run%d.json" % [result["scene"], result["mode"], result.get("tag", "x"), r[0], r[1], result["run"]]
	var f := FileAccess.open(out_dir.path_join(fname), FileAccess.WRITE)
	f.store_string(JSON.stringify(result, "", false))
	f.close()
	print("[FellowBench] wrote ", out_dir.path_join(fname))
	get_tree().quit(0 if result.get("valid", false) else 3)


func _fail(msg: String) -> void:
	push_error("[FellowBench] " + msg)
	result["valid"] = false
	result["error"] = msg
	var f := FileAccess.open(out_dir.path_join("error_%s.json" % args.get("scene", "x")), FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(result))
		f.close()
	get_tree().quit(2)


## frame_post_draw is never emitted by the headless display server (used for script CI only).
func _post_draw() -> void:
	if DisplayServer.get_name() == "headless":
		await get_tree().process_frame
	else:
		await RenderingServer.frame_post_draw


func _log(msg: String) -> void:
	print("[FellowBench %8.3f s] %s" % [(Time.get_ticks_usec() - t_start_usec) / 1e6, msg])


func _parse_args(a: PackedStringArray) -> Dictionary:
	var d := {}
	for s in a:
		if s.begins_with("--"):
			var kv := s.substr(2).split("=", true, 1)
			d[kv[0]] = kv[1] if kv.size() > 1 else "1"
	return d


func _parse_res(s: String) -> Vector2i:
	var p := s.split("x")
	if p.size() == 2:
		return Vector2i(p[0].to_int(), p[1].to_int())
	return Vector2i.ZERO
