# FellowEngine — run a FellowMotionModel on a case directory (raw little-endian arrays, one file per ONNX input) and
# write the outputs and a few activations back, for tests/fmb_parity_gpu.py.
#
#   godot --path <any project> --rendering-driver vulkan --script fmb_gpu_run.gd -- --fmb=<model.fmb> \
#       --case=<dir> --out=<dir> [--max_batch=16] [--repeat=1] [--profile] [--batch=N] [--target_groups=96] [--coop=0]
extends SceneTree

const F32 := ["global_root_values", "local_root_values", "local_poses"]
const I32 := ["has_global_root_values", "has_local_root_values", "has_local_poses", "allowed_pred_num_tokens"]
const ACTS := ["meta.T", "r.out", "c.lrv", "v.out", "p.logit", "v.x"]

var done := false


func _arg(name: String, def: String) -> String:
	for a in OS.get_cmdline_user_args():
		if a == "--" + name:
			return "1"
		if a.begins_with("--" + name + "="):
			return a.substr(name.length() + 3)
	return def


func _read(path: String) -> PackedByteArray:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		push_error("cannot read " + path)
		return PackedByteArray()
	return f.get_buffer(f.get_length())


func _write(path: String, data: PackedByteArray) -> void:
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_buffer(data)


func _process(_delta: float) -> bool:
	if done:
		return true
	done = true
	var model := FellowMotionModel.new()
	var max_batch := int(_arg("max_batch", "16"))
	model.cooperative_matrix = _arg("coop", "1") != "0"
	var t0 := Time.get_ticks_usec()
	var err := model.load(_arg("fmb", ""), max_batch)
	print("load: ", err, " ", (Time.get_ticks_usec() - t0) / 1000.0, " ms ", model.get_info())
	if err != OK:
		quit(1)
		return true
	model.target_groups = int(_arg("target_groups", "96"))
	model.profile = _arg("profile", "") != ""
	var case_dir := _arg("case", "")
	var out_dir := _arg("out", "")
	var inputs := {}
	for k in F32:
		inputs[k] = _read(case_dir + "/" + k + ".bin").to_float32_array()
	for k in I32:
		inputs[k] = _read(case_dir + "/" + k + ".bin").to_int32_array()
	# Optionally tile the case up to a batch size (timings at N).
	var want := int(_arg("batch", "0"))
	if want > 0:
		var n0: int = inputs["global_root_values"].size() / 40
		for k in inputs:
			var src = inputs[k]
			var per: int = src.size() / n0
			var dst = src.duplicate()
			dst.resize(per * want)
			for b in range(want):
				for j in range(per):
					dst[b * per + j] = src[(b % n0) * per + j]
			inputs[k] = dst
	var repeat := int(_arg("repeat", "1"))
	var res := {}
	var times := []
	for i in range(repeat):
		var t1 := Time.get_ticks_usec()
		res = model.run(inputs)
		times.append((Time.get_ticks_usec() - t1) / 1000.0)
	print("run ms: ", times)
	print("timings: ", model.get_last_timings())
	if res.is_empty():
		quit(2)
		return true
	DirAccess.make_dir_recursive_absolute(out_dir)
	_write(out_dir + "/pred_global_motions.bin", PackedFloat32Array(res["pred_global_motions"]).to_byte_array())
	_write(out_dir + "/pred_num_tokens.bin", PackedInt32Array(res["pred_num_tokens"]).to_byte_array())
	var n: int = res["pred_num_tokens"].size()
	for a in ACTS:
		_write(out_dir + "/act_" + a + ".bin", model.get_activation(a, n).to_byte_array())
	var f := FileAccess.open(out_dir + "/timings.json", FileAccess.WRITE)
	f.store_string(JSON.stringify({"runs_ms": times, "last": model.get_last_timings(), "info": model.get_info()}))
	quit(0)
	return true
