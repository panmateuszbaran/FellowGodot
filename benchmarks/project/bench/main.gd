extends Node
## FellowBench entry point. Behaviour is driven by user CLI args (after "--"):
##   --scene=<pbr|sponza|bistro_exterior|bistro_interior|fluid|stress|lights|blend>
##   --mode=<perf|capture|temporal|reference|list|geodump>
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
##   --pipeline_settle=<n>    temporal: before each sequence, render its first pose until n consecutive frames compile
##                            no pipeline (default 0: off; at most 20 s). The ubershader draws while a specialized
##                            pipeline compiles, so a sequence started earlier can depend on the compile threads' timing.
##   --temporal_warm=1        temporal: run each sequence once (pre-roll and frames, nothing saved) before the captured
##                            run, so that the pipelines its frames need (new lights or materials coming into view) are
##                            compiled before it, not while it is drawn with the ubershader
##   --hdr=1                  capture: also write linear pre-tonemap EXR (linear tonemapper, no glow, HDR 2D)
##   --jitter=<n>             reference: sub-pixel jittered renders per output frame (default 16)
##   --ablate=<a+b>           sizing only (items separated by + or ,): hide omni / spot / sun lights, probes, decals, or pos_shadows (see _ablate)
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
	"stress": "res://scenes/stress_lab.gd",
	"lights": "res://scenes/lights_lab.gd",
	"blend": "res://scenes/blend_lab.gd",
	"city": "res://scenes/city_lab.gd",
	"foliage": "res://scenes/foliage_lab.gd",
	"crowd": "res://scenes/crowd_lab.gd",
	"particles": "res://scenes/particles_lab.gd",
	"physics": "res://scenes/physics_lab.gd",
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
var watchdog_s := 1800.0
var finished := false


var pace_usec := 0 # --max_fps with --fixed-fps (where Engine.max_fps does nothing): the main thread waits here
var pace_next := 0


func _process(_delta: float) -> void:
	if not finished and (Time.get_ticks_usec() - t_start_usec) / 1e6 > watchdog_s:
		finished = true
		_fail("watchdog timeout after %.0f s (wall clock)" % watchdog_s)
	if pace_usec > 0:
		var now := Time.get_ticks_usec()
		if pace_next > now:
			while Time.get_ticks_usec() < pace_next:
				pass
			pace_next += pace_usec
		else:
			pace_next = now + pace_usec


func _ready() -> void:
	t_start_usec = Time.get_ticks_usec()
	args = _parse_args(OS.get_cmdline_user_args())
	# Watchdog (wall-clock): never hang a benchmark session. SceneTree timers are not usable for this because
	# --fixed-fps advances game time by 1/60 s per frame regardless of real time.
	watchdog_s = float(args.get("watchdog", "1800"))
	vp = get_viewport()
	# Benchmark rules: no vsync, no FPS cap (both are also set in project.godot / CLI).
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	# --max_fps=N: a frame rate cap for diagnostics only (presentation at a given rate); never in a measurement. The
	# runner passes --fixed-fps, with which the engine's own cap does nothing: the main thread waits in _process().
	Engine.max_fps = 0
	var cap_fps := int(args.get("max_fps", "0"))
	pace_usec = 1000000 / cap_fps if cap_fps > 0 else 0
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
	result["scene_built_ms"] = (Time.get_ticks_usec() - t_build) / 1000.0
	var fs_built := _fellow_stats()
	if not fs_built.is_empty():
		result["fellow_stats_scene_built"] = fs_built
	if args.has("ablate"):
		result["ablate"] = _ablate(str(args["ablate"]).replace("+", ",").split(",", false))
		_log("ablate: " + str(result["ablate"]))
	preset = Presets.build(args.get("preset", "high"), Presets.parse_overrides(args.get("set", "")))
	Presets.apply(preset, vp, scene.env, scene.cam_attr)
	RenderingServer.viewport_set_measure_render_time(vp.get_viewport_rid(), true)
	await get_tree().process_frame
	await _post_draw()
	result["load_time_ms"] = (Time.get_ticks_usec() - t_build) / 1000.0
	result["startup_to_first_frame_ms"] = (Time.get_ticks_usec() - t_start_usec) / 1000.0
	var fs_first := _fellow_stats()
	if not fs_first.is_empty():
		# Process totals after the first frame (the cost_* counters: where loading spent its time).
		result["fellow_stats_first_frame"] = fs_first
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
		"repeat":
			await _run_repeat()
		"temporal":
			await _run_temporal(false)
		"reference":
			await _run_reference()
		"physics":
			await _run_physics()
		"list":
			pass
		"geodump":
			_run_geodump()
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
	if args.has("pass_frames"):
		# Functional tests on slow devices (lavapipe) only: the first N frames of the path per pass.
		frames_per_pass = mini(frames_per_pass, int(args["pass_frames"]))
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
	# --rs_metrics=render_thread: see metrics.gd (for --render-thread separate; not with --ab, whose blocks read the
	# metrics of the frame just sampled).
	var m := Metrics.new(vp.get_viewport_rid(), str(args.get("rs_metrics", "")) == "render_thread")
	var pass_list := []
	var pass_frames := []
	var gpu_passes := {}
	var gpu_pass_frames := 0
	var last_ts_frame := -1
	var m0 := Time.get_ticks_usec()
	var mem_peak := _mem()
	var has_extra: bool = scene.has_method("sample_extra")
	var extra := {}
	var fellow_frames := {}
	await get_tree().process_frame
	var fs0 := _fellow_stats()
	var fs_prev := fs0
	# Per-frame FellowEngine counters only with --fellow_frames=1: reading them builds a dictionary of ~80 counters on
	# the main thread every frame, a cost stock builds (no counters) never pay.
	var per_frame_fellow: bool = not fs0.is_empty() and str(args.get("fellow_frames", "0")) == "1"
	# --fellow_frames_keys=a,b,... (or a+b+..., inside the runner's comma-separated --builds): the counters recorded per
	# frame (their increase in the frame); the shadow cache's by default.
	var per_frame_keys := PackedStringArray(["shadow_cache_cascades_rebuilt", "shadow_cache_cascades_fallback", "shadow_cache_cascades_cached"])
	if args.has("fellow_frames_keys"):
		per_frame_keys = str(args["fellow_frames_keys"]).replace("+", ",").split(",", false)
	# In-process A/B of runtime FellowEngine settings (--ab=name:k=v;k=v|name2:k=v...): the configurations take turns
	# in blocks of --ab_block frames along the path, rotating by one block per pass, so after (configurations x
	# --ab_repeats) passes each one has run every block of the path the same number of times, in the same process
	# (same thread placement, clocks and temperature). The first --ab_skip frames of a block are not counted (the CPU
	# and GPU times are reported a few frames late).
	var ab := _parse_ab(str(args.get("ab", "")))
	var ab_block := int(args.get("ab_block", "30"))
	var ab_skip := int(args.get("ab_skip", "4"))
	# --ab_warm=1: one unmeasured rotation first (configurations that change shaders compile their pipelines there).
	var ab_warm_passes := ab.size() * int(args.get("ab_warm", "0"))
	var ab_passes := ab.size() * int(args.get("ab_repeats", "2")) + ab_warm_passes
	var ab_sums := []
	var ab_ts := []
	var ab_ts_frames := []
	var ab_current := -1
	for c in ab.size():
		ab_sums.append({})
		ab_ts.append({})
		ab_ts_frames.append(0)
	m.reset_clock()
	var passes := 0
	while true:
		for f in range(frames_per_pass):
			if not ab.is_empty() and f % ab_block == 0:
				ab_current = (f / ab_block + passes) % ab.size()
				for k in ab[ab_current]["settings"].keys():
					ProjectSettings.set_setting(k, ab[ab_current]["settings"][k])
			_pose_at(f * DT)
			await get_tree().process_frame
			m.sample()
			pass_frames.append(f)
			if not ab.is_empty() and f % ab_block >= ab_skip and passes >= ab_warm_passes:
				var key := "%d:%d" % [passes - ab_warm_passes, f / ab_block]
				if not ab_sums[ab_current].has(key):
					ab_sums[ab_current][key] = {"n": 0, "frame_ms": 0.0, "render_cpu_ms": 0.0, "gpu_ms": 0.0, "setup_cpu_ms": 0.0}
				var acc: Dictionary = ab_sums[ab_current][key]
				acc["n"] += 1
				for metric in ["frame_ms", "render_cpu_ms", "gpu_ms", "setup_cpu_ms"]:
					var arr: PackedFloat64Array = m.data[metric]
					acc[metric] += arr[arr.size() - 1]
			if per_frame_fellow:
				# Per-frame FellowEngine counters (e.g. shadow cache rebuilds) to correlate with frame times.
				var fs := _fellow_stats()
				for k in per_frame_keys:
					if not fellow_frames.has(k):
						fellow_frames[k] = PackedInt32Array()
					fellow_frames[k].append(int(fs.get(k, 0)) - int(fs_prev.get(k, 0)))
				fs_prev = fs
			if has_extra:
				var ex: Dictionary = scene.sample_extra()
				for k in ex.keys():
					if not extra.has(k):
						extra[k] = PackedFloat64Array()
					extra[k].append(float(ex[k]))
			if profile and rd:
				var fr := rd.get_captured_timestamps_frame()
				if fr != last_ts_frame:
					last_ts_frame = fr
					if ab.is_empty():
						_accumulate_timestamps(rd, gpu_passes)
						gpu_pass_frames += 1
					elif f % ab_block >= ab_skip and passes >= ab_warm_passes:
						# Per configuration (the captured frame is a few frames old: inside the block after the skip).
						_accumulate_timestamps(rd, ab_ts[ab_current])
						ab_ts_frames[ab_current] += 1
		passes += 1
		var mm := _mem()
		for k in mm.keys():
			mem_peak[k] = max(mem_peak[k], mm[k])
		if ab.is_empty() and (Time.get_ticks_usec() - m0) / 1e6 >= measure_s:
			break
		if not ab.is_empty() and passes >= ab_passes:
			break
	var pc2 := _pipeline_compilations()
	if not ab.is_empty():
		# Per configuration: the mean of every (pass, block) it ran, keyed "pass:block" (block = position on the path).
		var abr := {"block": ab_block, "skip": ab_skip, "passes": passes - ab_warm_passes, "warm_passes": ab_warm_passes, "configs": [], "blocks": {}}
		for c in ab.size():
			var name: String = ab[c]["name"]
			abr["configs"].append({"name": name, "settings": ab[c]["settings"]})
			var blocks := {}
			for key in ab_sums[c].keys():
				var acc: Dictionary = ab_sums[c][key]
				var o := {}
				for metric in ["frame_ms", "render_cpu_ms", "gpu_ms", "setup_cpu_ms"]:
					o[metric] = acc[metric] / max(acc["n"], 1)
				blocks[key] = o
			abr["blocks"][name] = blocks
			if profile:
				var avg := {}
				for k in ab_ts[c].keys():
					avg[k] = ab_ts[c][k] / max(ab_ts_frames[c], 1)
				if not abr.has("passes_ms"):
					abr["passes_ms"] = {}
				abr["passes_ms"][name] = avg
		result["ab"] = abr
	result["measurement"] = {
		"seconds": (Time.get_ticks_usec() - m0) / 1e6,
		"passes": passes,
		"frames_per_pass": frames_per_pass,
		"pipeline_compilations": _dict_sub(pc2, pc1),
		"profile_timestamps": profile,
	}
	m.merge_rs()
	result["rs_metrics"] = "render_thread" if m.rs_async else "main_thread"
	result["summary"] = m.summary()
	result["frames"] = m.to_json_arrays()
	result["frames"]["path_frame"] = pass_frames
	if not fs0.is_empty():
		result["fellow_stats"] = _dict_sub(_fellow_stats(), fs0)
		result["frames"]["fellow"] = {}
		for k in fellow_frames.keys():
			result["frames"]["fellow"][k] = Array(fellow_frames[k])
	result["memory_peak"] = mem_peak
	if has_extra:
		var es := {}
		for k in extra.keys():
			var arr: PackedFloat64Array = extra[k]
			# Negative values mean "no timestamp available yet" (first frames): excluded.
			var valid := PackedFloat64Array()
			for v in arr:
				if v >= 0.0:
					valid.append(v)
			es[k] = Metrics.summarize(valid)
		result["extra_metrics"] = es
		result["frames"]["extra"] = {}
		for k in extra.keys():
			result["frames"]["extra"][k] = Array(extra[k])
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
			var cfs0 := _fellow_stats()
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
			if not cfs0.is_empty():
				entry["fellow_stats"] = _dict_sub(_fellow_stats(), cfs0)
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
		# Render-thread CPU time between the same markers (microseconds), reported as "cpu: <marker>".
		var cpu_ms := (rd.get_captured_timestamp_cpu_time(i + 1) - rd.get_captured_timestamp_cpu_time(i)) / 1e3
		acc["cpu: " + nm] = acc.get("cpu: " + nm, 0.0) + cpu_ms
	acc["__total__"] = acc.get("__total__", 0.0) + (rd.get_captured_timestamp_gpu_time(n - 1) - rd.get_captured_timestamp_gpu_time(0)) / 1e6
	acc["cpu: __total__"] = acc.get("cpu: __total__", 0.0) + (rd.get_captured_timestamp_cpu_time(n - 1) - rd.get_captured_timestamp_cpu_time(0)) / 1e3


## FellowEngine counters (RenderingServer.fellow_get_stats); empty on stock Godot.
func _parse_ab(spec: String) -> Array:
	# "name:k=v;k=v|name2:k=v" -> [{name, settings: {rendering/fellow/k: v}}]; every configuration sets every key that
	# any of them sets (keys a configuration does not mention get their start-up value).
	var out := []
	if spec.is_empty():
		return out
	var keys := {}
	for part in spec.split("|", false):
		var nv := part.split(":", true, 1)
		var settings := {}
		if nv.size() == 2:
			for kv in nv[1].split(";", false):
				var p := kv.split("=", false, 1)
				if p.size() == 2:
					# Keys are relative to rendering/fellow/; a full path (rendering/...) is taken as is.
					var k := p[0].strip_edges()
					var key := k if k.begins_with("rendering/") else "rendering/fellow/" + k
					settings[key] = Presets.parse_value(p[1].strip_edges())
					keys[key] = true
		out.append({"name": nv[0].strip_edges(), "settings": settings})
	for c in out:
		for key in keys.keys():
			if not c["settings"].has(key):
				c["settings"][key] = ProjectSettings.get_setting(key)
	return out


func _fellow_stats() -> Dictionary:
	if RenderingServer.has_method("fellow_get_stats"):
		return RenderingServer.call("fellow_get_stats")
	return {}


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


## Milestone 17: the Physics Lab's case simulated for --ticks physics ticks (default 600, after --physics_warm ticks,
## default 0: a case's whole evolution is the measurement), one tick per frame (--fixed-fps 60, one physics step per
## frame). Per tick: the frame's time and, where the build has them, the FellowEngine phase counters (sync,
## physics_process, step, end; their sum is physics_ms; Performance's physics time is a per-second maximum, not per
## tick); every --hash_every ticks (default 30) the SHA-256 of
## every moving body's transform (and the queries' results): two builds of one engine simulate the same when their
## hashes agree tick for tick.
func _run_physics() -> void:
	if not scene.has_method("physics_state_bytes"):
		_fail("mode physics needs the physics scene")
		return
	var ticks := int(args.get("ticks", "600"))
	var warm := int(args.get("physics_warm", "0"))
	var hash_every := int(args.get("hash_every", "30"))
	for i in warm:
		await get_tree().physics_frame
	var keys := ["physics_ticks", "physics_sync_usec", "physics_process_usec", "physics_step_usec", "physics_end_usec"]
	var per_tick := {"physics_ms": [], "frame_ms": [], "sync_ms": [], "process_ms": [], "step_ms": [], "end_ms": []}
	var hashes := []
	var prev := _fellow_stats()
	var last := Time.get_ticks_usec()
	var t0 := last
	for i in ticks:
		await get_tree().process_frame # one frame: one physics tick, then the frame's processing and drawing
		var now := Time.get_ticks_usec()
		per_tick["frame_ms"].append((now - last) / 1000.0)
		last = now
		var fs := _fellow_stats()
		if fs.has("physics_ticks"):
			var d := {}
			for k in ["sync", "process", "step", "end"]:
				d[k] = (fs.get("physics_%s_usec" % k, 0) - prev.get("physics_%s_usec" % k, 0)) / 1000.0
				per_tick[k + "_ms"].append(d[k])
			per_tick["physics_ms"].append(d["sync"] + d["process"] + d["step"] + d["end"])
			prev = fs
		if hash_every > 0 and (i + 1) % hash_every == 0:
			var ctx := HashingContext.new()
			ctx.start(HashingContext.HASH_SHA256)
			ctx.update(scene.physics_state_bytes())
			hashes.append([i + 1, ctx.finish().hex_encode()])
			last = Time.get_ticks_usec() # the hash is not the frame's
	var wall := (Time.get_ticks_usec() - t0) / 1e6
	var summary := {}
	for k in per_tick.keys():
		var a: Array = per_tick[k]
		if a.is_empty():
			continue
		var s := a.duplicate()
		s.sort()
		var total := 0.0
		for x in a:
			total += x
		summary[k] = {"mean": total / a.size(), "p50": s[s.size() / 2], "p95": s[int(s.size() * 0.95)], "max": s[s.size() - 1], "total": total}
	result["physics"] = {"ticks": ticks, "warm": warm, "wall_s": wall, "summary": summary, "per_tick": per_tick, "hashes": hashes,
		"counters": scene.physics_counters(), "engine": ProjectSettings.get_setting("physics/3d/physics_engine"),
		"threads": OS.get_processor_count()}
	_log("physics: %d ticks, frame %.3f ms, physics %.3f ms/tick (step %.3f, sync %.3f, process %.3f), final hash %s" % [ticks,
		summary["frame_ms"]["mean"], summary.get("physics_ms", {}).get("mean", -1.0), summary.get("step_ms", {}).get("mean", -1.0), summary.get("sync_ms", {}).get("mean", -1.0),
		summary.get("process_ms", {}).get("mean", -1.0), hashes[-1][1] if hashes.size() else "-"])


## Diagnostic (lavapipe determinism): one fixed camera, then --repeat frames saved one after another.
func _run_repeat() -> void:
	var settle := int(args.get("settle", "30"))
	var n := int(args.get("repeat", "8"))
	var stem := _file_stem()
	var k: String = _selected(scene.fixed_cameras.keys(), "cameras")[0]
	var c: Dictionary = scene.fixed_cameras[k]
	scene.set_time(c.get("time", 0.0))
	scene.set_camera_pose(c["pos"], c["target"], c.get("fov", 60.0))
	for i in range(settle):
		await get_tree().process_frame
	var files := []
	var per_frame := []
	var keys := str(args.get("keys", "pipeline_ubershader_draws")).split("+", false)
	var prev := _fellow_stats()
	for r in range(n):
		await get_tree().process_frame
		await _post_draw()
		var p := out_dir.path_join("%s_%s_r%02d.png" % [stem, k, r])
		_save_frame(p)
		files.append(p.get_file())
		var now := _fellow_stats()
		var row := []
		for key in keys:
			row.append(now.get(key, 0) - prev.get(key, 0))
		per_frame.append(row)
		prev = now
	print("[repeat] keys ", keys, " per frame ", per_frame)
	result["files"] = files


func _run_capture() -> void:
	# --freeze_exposure=1: same exposure handling as the temporal/reference modes (auto exposure off), so the captures
	# can be compared with the accumulated ground truth. Default captures keep auto exposure (what a player sees).
	if args.get("freeze_exposure", "0") == "1":
		_freeze_exposure()
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
	var pipeline_settle := int(args.get("pipeline_settle", "0"))
	var compile_info := {}
	for sname in _selected(scene.temporal_seqs.keys(), "seqs"):
		var s: Dictionary = scene.temporal_seqs[sname]
		var path = s["path"]
		var start: float = s["start"]
		var nframes: int = s["frames"]
		var lst := []
		var settle_frames := 0
		if pipeline_settle > 0:
			# Render the sequence's first pose until no pipeline has compiled for pipeline_settle frames.
			var t0 := start - pre_roll * DT
			scene.set_time(t0)
			scene.apply_path_sample(path.sample(t0 - start))
			var quiet := 0
			var last := _pipeline_compilations()
			var deadline := Time.get_ticks_msec() + 20000
			while quiet < pipeline_settle and Time.get_ticks_msec() < deadline:
				await get_tree().process_frame
				settle_frames += 1
				var now := _pipeline_compilations()
				quiet = quiet + 1 if now == last else 0
				last = now
		if args.get("temporal_warm", "0") == "1":
			for i in range(pre_roll + nframes):
				var tw := start + (i - pre_roll) * DT
				scene.set_time(tw)
				scene.apply_path_sample(path.sample(tw - start))
				await get_tree().process_frame
			# Let the last compilations finish before the captured run.
			var quiet_w := 0
			var last_w := _pipeline_compilations()
			var deadline_w := Time.get_ticks_msec() + 20000
			while quiet_w < 30 and Time.get_ticks_msec() < deadline_w:
				await get_tree().process_frame
				var now_w := _pipeline_compilations()
				quiet_w = quiet_w + 1 if now_w == last_w else 0
				last_w = now_w
		# TAA's jitter phase (the frame number modulo 16) aligned before the pre-roll: the waits above end after a number of
		# frames that depends on when compilations finish, and two runs would otherwise capture with different jitter.
		# --align_jitter=0: off.
		if args.get("align_jitter", "1") == "1":
			while Engine.get_frames_drawn() % 16 != 0:
				await get_tree().process_frame
		var pc_start := _pipeline_compilations()
		var frames_at_preroll := Engine.get_frames_drawn()
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
		compile_info[sname] = {"settle_frames": settle_frames, "frames_drawn_at_preroll": frames_at_preroll, "during_sequence": _dict_sub(_pipeline_compilations(), pc_start)}
	result["files"] = files
	result["temporal_pipeline_compilations"] = compile_info


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
			# Let shadows / screen-space effects (SSIL and SSR reproject the previous frame) settle on the new
			# projection: more frames after a pose change (first jitter), fewer between sub-pixel jitters.
			var settle := int(args.get("ref_settle_first", "3")) if j == 0 else int(args.get("ref_settle", "3"))
			for i in range(settle):
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


# ---------------------------------------------------------------- ablation (bottleneck sizing only)

## Removes parts of the scene for a sizing profile (never used for image comparisons): "omni", "spot", "sun" hide those
## lights, "pos_shadows" disables the shadows of omni and spot lights, "probes" hides reflection probes, "decals" hides
## decals. Returns the number of nodes changed per item (recorded in the results).
func _ablate(items: PackedStringArray) -> Dictionary:
	var changed := {}
	for it in items:
		changed[it] = 0
	var stack: Array = [scene]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		for c in n.get_children():
			stack.append(c)
		if n is OmniLight3D and changed.has("omni") or n is SpotLight3D and changed.has("spot") or n is DirectionalLight3D and changed.has("sun") or n is ReflectionProbe and changed.has("probes") or n is Decal and changed.has("decals"):
			var key := "omni" if n is OmniLight3D else "spot" if n is SpotLight3D else "sun" if n is DirectionalLight3D else "probes" if n is ReflectionProbe else "decals"
			if (n as Node3D).visible:
				(n as Node3D).visible = false
				changed[key] += 1
		elif (n is OmniLight3D or n is SpotLight3D) and changed.has("pos_shadows") and (n as Light3D).shadow_enabled:
			(n as Light3D).shadow_enabled = false
			changed["pos_shadows"] += 1
	return changed


# ---------------------------------------------------------------- geometry dump (offline analysis)

## Writes the scene's mesh geometry in world space for offline analysis (benchmarks/tools/occlusion_geodump.py):
## <out>/<scene>_geodump_pos.f32 (float32 x, y, z per vertex, world space), _idx.u32 (int32, 3 per triangle,
## surface-local vertex indices) and, in the result json, per instance surface: base_vertex, vertex_count,
## first_index, triangle count, material transparency; the visible omni and spot lights; the camera flythrough sampled
## every 0.5 s, the fixed cameras and the camera's near/far. Mesh instances only (MultiMesh instances are listed with
## their instance count but not expanded).
func _run_geodump() -> void:
	var pos := PackedByteArray()
	var idx := PackedByteArray()
	var surfaces := []
	var multimeshes := []
	var lights := []
	var stack: Array = [scene]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		for c in n.get_children():
			stack.append(c)
		if n is MultiMeshInstance3D and (n as MultiMeshInstance3D).multimesh:
			multimeshes.append({"name": str(n.get_path()), "instances": (n as MultiMeshInstance3D).multimesh.instance_count})
		if (n is OmniLight3D or n is SpotLight3D) and (n as Light3D).is_visible_in_tree():
			var lx := (n as Light3D).global_transform
			var ld := {"name": str(n.get_path()), "type": "omni" if n is OmniLight3D else "spot", "position": [lx.origin.x, lx.origin.y, lx.origin.z], "direction": [-lx.basis.z.x, -lx.basis.z.y, -lx.basis.z.z], "energy": (n as Light3D).light_energy, "shadow": (n as Light3D).shadow_enabled, "size": (n as Light3D).light_size}
			if n is OmniLight3D:
				ld["range"] = (n as OmniLight3D).omni_range
			else:
				ld["range"] = (n as SpotLight3D).spot_range
				ld["angle"] = (n as SpotLight3D).spot_angle
			lights.append(ld)
		if not (n is MeshInstance3D) or (n as MeshInstance3D).mesh == null or not (n as MeshInstance3D).is_visible_in_tree():
			continue
		var mi := n as MeshInstance3D
		var xf := mi.global_transform
		for si in mi.mesh.get_surface_count():
			# Only ArrayMesh exposes the primitive type; PrimitiveMesh surfaces are always triangles.
			if mi.mesh is ArrayMesh and (mi.mesh as ArrayMesh).surface_get_primitive_type(si) != Mesh.PRIMITIVE_TRIANGLES:
				continue
			var arrays := mi.mesh.surface_get_arrays(si)
			var v: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
			var ind = arrays[Mesh.ARRAY_INDEX]
			if v.is_empty():
				continue
			# Bulk operations only (Bistro has millions of vertices).
			var base := pos.size() / 12
			var first := idx.size() / 4
			pos.append_array((xf * v).to_byte_array())
			if ind == null or (ind as PackedInt32Array).is_empty():
				var seq := PackedInt32Array()
				seq.resize(v.size())
				for k in v.size():
					seq[k] = k
				idx.append_array(seq.to_byte_array())
			else:
				idx.append_array((ind as PackedInt32Array).to_byte_array())
			var m := mi.get_active_material(si)
			var transparency := -1 # BaseMaterial3D.Transparency; -1: no material or not a BaseMaterial3D.
			if m is BaseMaterial3D:
				transparency = (m as BaseMaterial3D).transparency
			var blend := transparency == BaseMaterial3D.TRANSPARENCY_ALPHA or transparency == BaseMaterial3D.TRANSPARENCY_ALPHA_DEPTH_PRE_PASS
			surfaces.append({"name": str(mi.get_path()), "surface": si, "base_vertex": base, "vertex_count": v.size(), "first_index": first, "tris": (idx.size() / 4 - first) / 3, "blend": blend, "transparency": transparency, "material": m.get_class() if m else ""})
	var stem := out_dir.path_join("%s_geodump" % result["scene"])
	var f := FileAccess.open(stem + "_pos.f32", FileAccess.WRITE)
	f.store_buffer(pos)
	f.close()
	f = FileAccess.open(stem + "_idx.u32", FileAccess.WRITE)
	f.store_buffer(idx)
	f.close()
	var fly := []
	var t := 0.0
	while t <= scene.flythrough.duration() + 1e-6:
		var d: Dictionary = scene.flythrough.sample(t)
		var cx: Transform3D = d["xform"]
		fly.append({"t": t, "origin": [cx.origin.x, cx.origin.y, cx.origin.z], "forward": [-cx.basis.z.x, -cx.basis.z.y, -cx.basis.z.z], "up": [cx.basis.y.x, cx.basis.y.y, cx.basis.y.z], "fov": d["fov"]})
		t += 0.5
	result["geodump"] = {"surfaces": surfaces, "multimeshes": multimeshes, "vertices": pos.size() / 12, "triangles": idx.size() / 12, "flythrough": fly, "fixed_cameras": scene.describe()["fixed_cameras"], "near": scene.camera.near, "far": scene.camera.far, "lights": lights}
	_log("geodump: %d surfaces, %d vertices, %d triangles, %d positional lights" % [surfaces.size(), pos.size() / 12, idx.size() / 12, lights.size()])


# ---------------------------------------------------------------- output

func _finish() -> void:
	finished = true
	result["total_runtime_s"] = (Time.get_ticks_usec() - t_start_usec) / 1e6
	var fst := _fellow_stats()
	if not fst.is_empty():
		result["fellow_stats_process_total"] = fst
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
