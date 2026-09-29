extends CanvasLayer
## FellowEngine performance overlay (F3 toggles). Works on stock Godot too (public APIs only).
##
## Shows CPU/GPU frame time, FPS, draw calls, visible objects, primitives, VRAM, resolution, render scale and
## per-category GPU pass timings (shadows / opaque / transparent / volumetrics / post-FX / fluid).
## Per-pass timings need engine timestamps: on FellowEngine enable rendering/fellow/profiling/pass_timestamps,
## on stock start Godot with --gpu-profile. The same values are exported by the benchmark (no screen reading).
## Usage: add as an autoload (res://bench/overlay.gd) or instance it; call snapshot() to get the numbers.
## Export: F4 (or export_json()) writes the last 60 s of samples (4 per second) to user://fellow_overlay_<time>.json.

const CATEGORIES := {
	"shadows": ["Render Directional/SpotLight Shadows", "Render OmniLight Shadows", "Render GI + Render Directional/SpotLight Shadows (Parallel)", "Setup Shadows"],
	"opaque": ["Render Opaque Pass", "Render Depth Pre-Pass", "GI + Render Depth Pre-Pass (Parallel)", "Render Motion Pass", "Render Sky"],
	"transparent": ["Render 3D Transparent Pass"],
	"volumetrics": ["Update Volumetric Fog", "Render FogVolumes", "Render Fog", "Filter Fog", "Integrate Fog"],
	"screen_space": ["Process SSAO", "Process SSIL", "Process SSR", "Prepare Depth for SSAO/SSIL"],
	"postfx": ["TAA", "FSR2", "Tonemap", "Glow", "Auto exposure", "Depth of Field", "SMAA", "Resolve"],
}

var label: Label
var enabled := false
var _acc := {}
var _frames := 0
var _last := {}
var _t_last := 0
var _history: Array = []  # snapshot() dictionaries sampled at 4 Hz, last 60 s
const HISTORY_LEN := 240


func _ready() -> void:
	layer = 128
	label = Label.new()
	label.position = Vector2(12, 12)
	label.add_theme_font_size_override("font_size", 14)
	label.add_theme_color_override("font_outline_color", Color.BLACK)
	label.add_theme_constant_override("outline_size", 4)
	add_child(label)
	label.visible = enabled
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	if ProjectSettings.has_setting("rendering/fellow/profiling/pass_timestamps"):
		ProjectSettings.set_setting("rendering/fellow/profiling/pass_timestamps", true)


func _unhandled_input(e: InputEvent) -> void:
	if not (e is InputEventKey and e.pressed and not e.echo):
		return
	if e.keycode == KEY_F3:
		enabled = not enabled
		label.visible = enabled
	elif e.keycode == KEY_F4:
		var p := export_json()
		print("[FellowOverlay] exported %d samples to %s" % [_history.size(), ProjectSettings.globalize_path(p)])


func _process(_d: float) -> void:
	_accumulate_passes()
	var now := Time.get_ticks_msec()
	if now - _t_last > 250:
		_t_last = now
		var snap := snapshot()
		snap["t_ms"] = now
		_history.append(snap)
		if _history.size() > HISTORY_LEN:
			_history.pop_front()
		if enabled:
			label.text = _format(snap)


func _accumulate_passes() -> void:
	var rd := RenderingServer.get_rendering_device()
	if rd == null:
		return
	var n := rd.get_captured_timestamps_count()
	if n < 2:
		return
	var cur := {}
	for i in range(n - 1):
		var nm := rd.get_captured_timestamp_name(i)
		var dt := (rd.get_captured_timestamp_gpu_time(i + 1) - rd.get_captured_timestamp_gpu_time(i)) / 1e6
		for cat in CATEGORIES.keys():
			if nm in CATEGORIES[cat]:
				cur[cat] = cur.get(cat, 0.0) + dt
		if nm.begins_with("FellowFluid:SimBegin"):
			cur["fluid_sim"] = cur.get("fluid_sim", 0.0) + dt
	_last = cur


## Current values (also used by the benchmark / tests; no screen reading needed).
func snapshot() -> Dictionary:
	var vp := get_viewport()
	var rid := vp.get_viewport_rid()
	var d := {
		"cpu_frame_ms": RenderingServer.viewport_get_measured_render_time_cpu(rid) + RenderingServer.get_frame_setup_time_cpu(),
		"gpu_frame_ms": RenderingServer.viewport_get_measured_render_time_gpu(rid),
		"fps": Engine.get_frames_per_second(),
		"draw_calls": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME),
		"visible_objects": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_OBJECTS_IN_FRAME),
		"primitives": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME),
		"vram_mb": RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_VIDEO_MEM_USED) / 1048576.0,
		"resolution": vp.get_visible_rect().size,
		"render_scale": vp.scaling_3d_scale,
	}
	for k in _last.keys():
		d[k + "_ms"] = _last[k]
	for w in get_tree().root.find_children("*", "FellowShallowWater3D", true, false):
		d["fluid_sim_ms"] = w.get_simulation_gpu_time_ms()
		d["fluid_surface_ms"] = w.get_surface_gpu_time_ms()
	return d


## Writes the sampled history (JSON, one object per sample) and returns the user:// path.
func export_json() -> String:
	var path := "user://fellow_overlay_%s.json" % Time.get_datetime_string_from_system().replace(":", "-")
	var rows := []
	for d in _history:
		var r := {}
		for k in d.keys():
			var v = d[k]
			r[k] = [v.x, v.y] if v is Vector2 or v is Vector2i else v
		rows.append(r)
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify({"engine": Engine.get_version_info(), "adapter": RenderingServer.get_video_adapter_name(), "samples": rows}, " "))
		f.close()
	return path


func _format(d: Dictionary) -> String:
	var s := "FellowEngine overlay (F3)\n"
	s += "CPU %.2f ms   GPU %.2f ms   FPS %d\n" % [d["cpu_frame_ms"], d["gpu_frame_ms"], d["fps"]]
	s += "draws %d   objects %d   tris %.2f M\n" % [d["draw_calls"], d["visible_objects"], d["primitives"] / 1e6]
	s += "VRAM %.0f MB   %dx%d @ %.2f\n" % [d["vram_mb"], d["resolution"].x, d["resolution"].y, d["render_scale"]]
	for k in ["shadows_ms", "opaque_ms", "transparent_ms", "volumetrics_ms", "screen_space_ms", "postfx_ms", "fluid_sim_ms", "fluid_surface_ms"]:
		if d.has(k):
			s += "%s %.2f   " % [k.trim_suffix("_ms"), d[k]]
	if not d.has("opaque_ms"):
		s += "(pass timings off: FellowEngine rendering/fellow/profiling/pass_timestamps or --gpu-profile)"
	return s
