extends RefCounted
## Per-frame metric recorder + robust summary statistics.
##
## Metric definitions (all per rendered frame):
##   frame_ms        wall-clock time between consecutive main-loop iterations (what a player experiences)
##   gpu_ms          RenderingServer.viewport_get_measured_render_time_gpu() of the root viewport
##                   (GPU timestamps around the 3D+2D render of the viewport; excludes swapchain blit/present)
##   render_cpu_ms   RenderingServer.viewport_get_measured_render_time_cpu() (CPU time spent rendering the viewport)
##   setup_cpu_ms    RenderingServer.get_frame_setup_time_cpu() (CPU frame setup: culling, instance updates...)
##   process_ms      Performance.TIME_PROCESS (script/node _process time on the main thread)
##   draw_calls / primitives / objects  RenderingServer.get_rendering_info(TOTAL_*_IN_FRAME)
##   shadow_draw_calls  viewport render info (shadow pass) of the root viewport
## Note: GPU/CPU measured times are reported by the engine with a few frames of latency;
## statistics over whole sequences are unaffected by this constant offset.

const FIELDS := ["frame_ms", "gpu_ms", "render_cpu_ms", "setup_cpu_ms", "process_ms", "draw_calls", "primitives", "objects", "shadow_draw_calls", "vis_draw_calls"]

var data := {}
var _last_usec := 0
var vp_rid: RID


func _init(p_vp_rid: RID) -> void:
	vp_rid = p_vp_rid
	for f in FIELDS:
		data[f] = PackedFloat64Array()


func reset_clock() -> void:
	_last_usec = Time.get_ticks_usec()


func sample() -> void:
	var now := Time.get_ticks_usec()
	var dt := (now - _last_usec) / 1000.0
	_last_usec = now
	data["frame_ms"].append(dt)
	data["gpu_ms"].append(RenderingServer.viewport_get_measured_render_time_gpu(vp_rid))
	data["render_cpu_ms"].append(RenderingServer.viewport_get_measured_render_time_cpu(vp_rid))
	data["setup_cpu_ms"].append(RenderingServer.get_frame_setup_time_cpu())
	data["process_ms"].append(Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0)
	data["draw_calls"].append(RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME))
	data["primitives"].append(RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME))
	data["objects"].append(RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_OBJECTS_IN_FRAME))
	data["shadow_draw_calls"].append(RenderingServer.viewport_get_render_info(vp_rid, RenderingServer.VIEWPORT_RENDER_INFO_TYPE_SHADOW, RenderingServer.VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME))
	data["vis_draw_calls"].append(RenderingServer.viewport_get_render_info(vp_rid, RenderingServer.VIEWPORT_RENDER_INFO_TYPE_VISIBLE, RenderingServer.VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME))


func count() -> int:
	return data["frame_ms"].size()


static func percentile(sorted: PackedFloat64Array, p: float) -> float:
	if sorted.is_empty():
		return NAN
	var idx := p / 100.0 * (sorted.size() - 1)
	var lo := int(floor(idx))
	var hi := mini(lo + 1, sorted.size() - 1)
	return lerp(sorted[lo], sorted[hi], idx - lo)


static func summarize(arr: PackedFloat64Array) -> Dictionary:
	var n := arr.size()
	if n == 0:
		return {"n": 0}
	var s := arr.duplicate()
	s.sort()
	var total := 0.0
	for v in s:
		total += v
	var mean := total / n
	var var_acc := 0.0
	for v in s:
		var_acc += (v - mean) * (v - mean)
	var std := sqrt(var_acc / max(n - 1, 1))
	return {
		"n": n, "mean": mean, "std": std, "cv": std / mean if mean != 0.0 else 0.0,
		"min": s[0], "max": s[n - 1],
		"p50": percentile(s, 50.0), "p90": percentile(s, 90.0), "p95": percentile(s, 95.0),
		"p99": percentile(s, 99.0), "p99_9": percentile(s, 99.9),
	}


func summary() -> Dictionary:
	var out := {}
	for f in FIELDS:
		out[f] = summarize(data[f])
	var ft: PackedFloat64Array = data["frame_ms"]
	if ft.size() > 0:
		var s := ft.duplicate()
		s.sort()
		var total := 0.0
		for v in s:
			total += v
		out["avg_fps"] = 1000.0 * s.size() / total
		# "1% low": average FPS over the slowest 1% of frames (at least one frame).
		var k := maxi(1, int(ceil(s.size() * 0.01)))
		var worst := 0.0
		for i in range(s.size() - k, s.size()):
			worst += s[i]
		out["fps_1pct_low"] = 1000.0 * k / worst
		# Stutters: frames slower than max(2 x median, median + 4 ms).
		var med := percentile(s, 50.0)
		var thr: float = max(2.0 * med, med + 4.0)
		var st := 0
		for v in ft:
			if v > thr:
				st += 1
		out["stutter_frames"] = st
		out["stutter_threshold_ms"] = thr
		out["measured_seconds"] = total / 1000.0
	return out


func to_json_arrays() -> Dictionary:
	var out := {}
	for f in FIELDS:
		out[f] = Array(data[f])
	return out
