extends SceneTree
## FellowEngine milestone 136 check (decision 0261): the Vulkan pipeline cache saved while pipelines are created on
## several threads.
##
##   godot --path benchmarks/project [--gpu-index 1] -s res://bench/pipeline_cache_stress.gd
##       -- [--threads=8] [--pipelines=150]
##
## A compute shader whose code a specialization constant changes (the driver compiles each value anew, and the cache
## grows by each pipeline); --threads threads each create --pipelines pipelines with constants no run used before. With
## rendering_device/pipeline_cache/save_chunk_size_mb small (override.cfg), the cache is saved on a worker thread
## after almost every pipeline, while the others are being created: what crashed Intel's driver
## (rendering/fellow/frame/pipeline_cache_exclusive off). One JSON line: PIPELINE_CACHE_STRESS {...}.

var args := {}
var rd: RenderingDevice
var shader: RID
var mutex := Mutex.new()
var made: Array[RID] = []
var failed := 0


func _arg(name: String, default: String) -> String:
	return args.get(name, default)


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			var kv: PackedStringArray = a.substr(2).split("=", true, 1)
			args[kv[0]] = kv[1]
	_run.call_deferred()


func _make(p_from: int, p_count: int) -> void:
	for i in p_count:
		var c := RDPipelineSpecializationConstant.new()
		c.constant_id = 0
		c.value = p_from + i
		var p := rd.compute_pipeline_create(shader, [c])
		mutex.lock()
		if p.is_valid():
			made.append(p)
		else:
			failed += 1
		mutex.unlock()


func _run() -> void:
	rd = RenderingServer.get_rendering_device()
	var src := RDShaderSource.new()
	src.language = RenderingDevice.SHADER_LANGUAGE_GLSL
	src.source_compute = """#version 450
layout(local_size_x = 64) in;
layout(constant_id = 0) const int K = 1;
layout(set = 0, binding = 0, std430) buffer Data { float v[]; } data;
void main() {
	uint i = gl_GlobalInvocationID.x;
	float x = data.v[i];
	for (int j = 0; j < 12; j++) {
		x = x * float(K % 89 + j) + sin(x + float(K % 61)) * float(K % 7 + 1);
	}
	data.v[i] = x;
}
"""
	var spirv := rd.shader_compile_spirv_from_source(src)
	if spirv.compile_error_compute != "":
		print("PIPELINE_CACHE_STRESS " + JSON.stringify({"error": spirv.compile_error_compute}))
		quit(2)
		return
	shader = rd.shader_create_from_spirv(spirv)
	var threads := int(_arg("threads", "8"))
	var per_thread := int(_arg("pipelines", "150"))
	# Constants no earlier run used (the cache starts empty or not, the driver compiles them).
	var rng := RandomNumberGenerator.new()
	rng.randomize()
	var base := rng.randi_range(1, 1 << 20) * 4096
	var t0 := Time.get_ticks_usec()
	var pool: Array[Thread] = []
	for t in threads:
		var th := Thread.new()
		th.start(_make.bind(base + t * per_thread, per_thread))
		pool.append(th)
	for th in pool:
		th.wait_to_finish()
	var seconds := (Time.get_ticks_usec() - t0) / 1000000.0
	var count := made.size()
	for p in made:
		rd.free_rid(p)
	rd.free_rid(shader)
	var result := {"pipelines": count, "failed": failed, "seconds": snappedf(seconds, 0.001),
		"per_second": snappedf(count / maxf(seconds, 0.001), 0.1), "adapter": rd.get_device_name(),
		"exclusive": ProjectSettings.get_setting("rendering/fellow/frame/pipeline_cache_exclusive", -1),
		"save_chunk_mb": ProjectSettings.get_setting("rendering/rendering_device/pipeline_cache/save_chunk_size_mb")}
	print("PIPELINE_CACHE_STRESS " + JSON.stringify(result))
	quit(0)
