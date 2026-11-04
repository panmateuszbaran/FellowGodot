/**************************************************************************/
/*  fellow_motion_model.h                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "servers/rendering/rendering_device.h"

class RDShaderFile;

// FellowEngine neural motion (docs/decisions/0143-neural-motion.md): a MotionBricks SOMA model, converted to one .fmb
// file by tools/fmb_convert.py (the weights are NVIDIA's, under the NVIDIA Open Model Agreement, and never part of this
// repository), run on its own local RenderingDevice — a GPU queue of its own, beside the renderer's. One run generates
// one batch of independent requests: four context poses and up to four target poses in, 24-64 frames of motion out.
//
// The plan (the op list in the file) is executed op by op as tools/fmb_plan.py's NumPy reference does; the inputs and
// outputs are the ONNX model's (docs of MotionBricks: global_root_values [N, 8, 5], local_poses [N, 8, 267], ...).
class FellowMotionModel : public RefCounted {
	GDCLASS(FellowMotionModel, RefCounted);

public:
	static constexpr int FRAMES = 64;
	static constexpr int SLOTS = 8;
	static constexpr int JOINTS = 30;
	static constexpr int POSE_DIM = 267; // 29 joint positions + 30 6D rotations
	static constexpr int MOTION_DIM = 366;

	struct Batch {
		int n = 0;
		// Inputs, row-major, the ONNX layout.
		LocalVector<float> global_root_values; // [n, 8, 5]
		LocalVector<int32_t> has_global_root_values; // [n, 8]
		LocalVector<float> local_root_values; // [n, 8, 4]
		LocalVector<int32_t> has_local_root_values; // [n, 8]
		LocalVector<float> local_poses; // [n, 8, 267]
		LocalVector<int32_t> has_local_poses; // [n, 8]
		LocalVector<int32_t> allowed_pred_num_tokens; // [n, 11]
		// Outputs.
		LocalVector<float> pred_global_motions; // [n, 64, 366]
		LocalVector<int32_t> pred_num_tokens; // [n]
		void resize(int p_n);
	};

private:
	struct ActInfo {
		uint32_t rows = 0;
		uint32_t cols = 0;
		uint32_t offset = 0; // floats
	};
	struct OpRec {
		uint32_t kind = 0; // 0 gemm, 1 attn, 2 ln, 3 glue
		uint32_t chunk = 0;
		uint32_t rows = 0, k = 0, nout = 0, kb = 0, ke = 0, work = 0, heads = 0;
		String tag;
	};

	Mutex mutex;
	RenderingDevice *rd = nullptr;
	Ref<RDShaderFile> shader_file;
	enum Version {
		V_GEMM16,
		V_GEMM32,
		V_GEMM64,
		V_REDUCE,
		V_ATTN,
		V_LN,
		V_GLUE,
		V_MAX,
	};
	RID shaders[V_MAX];
	RID pipelines[V_MAX];
	LocalVector<RID> weight_buffers;
	RID param_buffer;
	RID act_buffer;
	RID op_buffer;
	RID part_buffer;
	RID out_buffer; // the outputs, copied for the read-back
	HashMap<uint64_t, RID> uniform_sets; // (version << 32 | chunk) -> set
	HashMap<String, ActInfo> acts;
	LocalVector<OpRec> op_recs;
	uint32_t act_floats = 0;
	uint32_t part_floats = 0;
	int max_batch = 0;
	bool weights_f16 = true;
	bool loaded = false;
	bool profile = false;
	bool cooperative_matrix = true;
	bool coop_active = false;
	bool coop_vec4 = false; // FELLOW_MOTION_GEMM=vec4 (a diagnostic)
	String coop_note;
	int target_groups = 96;
	String path;

	// Model constants from the file.
	LocalVector<float> mean, std_dev; // the 370 statistics
	float capture_offsets[JOINTS * 9];
	float standard_offsets[JOINTS * 9];
	Dictionary meta;

	// Last run.
	Dictionary last_timings;
	double last_gpu_msec = 0.0;
	double last_cpu_msec = 0.0;

	RID _uniform_set(int p_version, uint32_t p_chunk);
	int _splits(const OpRec &p_op, int p_n, int &r_bm) const;
	void _free();

	void _prepare(const Batch &p_batch, LocalVector<float> &r_grv_n, LocalVector<float> &r_off, LocalVector<float> &r_head0);
	struct DecodeCtx {
		Batch *b = nullptr;
		const LocalVector<float> *vout = nullptr;
		const LocalVector<float> *rout = nullptr;
		const LocalVector<float> *T = nullptr;
		const LocalVector<float> *grv_n = nullptr;
		const LocalVector<float> *off = nullptr;
		const LocalVector<float> *head0 = nullptr;
		float lm[365], ls[365], gm[5], gs[5];
	};
	void _decode_one(uint32_t p_index, DecodeCtx *p_ctx);
	void _decode(Batch &p_batch, const LocalVector<float> &p_vout, const LocalVector<float> &p_rout, const LocalVector<float> &p_T, const LocalVector<float> &p_grv_n, const LocalVector<float> &p_off, const LocalVector<float> &p_head0);

protected:
	static void _bind_methods();

public:
	Error load(const String &p_path, int p_max_batch = 16);
	bool is_loaded() const { return loaded; }
	int get_max_batch() const { return max_batch; }
	Dictionary get_info() const;

	// Runs one batch (blocking: submits to the model's device and waits). Thread-safe; one run at a time.
	Error run_batch(Batch &p_batch);

	// Script access: inputs and outputs by their ONNX names, as packed arrays.
	Dictionary run(const Dictionary &p_inputs);
	PackedFloat32Array get_activation(const String &p_name, int p_count = -1);
	void set_profile(bool p_enable) { profile = p_enable; }
	bool is_profile() const { return profile; }
	void set_target_groups(int p_groups) { target_groups = MAX(1, p_groups); }
	int get_target_groups() const { return target_groups; }
	Dictionary get_last_timings() const { return last_timings; }
	void set_cooperative_matrix(bool p_enable) { cooperative_matrix = p_enable; }
	bool is_cooperative_matrix() const { return cooperative_matrix; }
	bool is_cooperative_matrix_active() const { return coop_active; }
	Dictionary get_skeleton() const { return meta.get("skeleton", Dictionary()); }
	const float *get_capture_offsets() const { return capture_offsets; }
	const float *get_standard_offsets() const { return standard_offsets; }

	FellowMotionModel();
	~FellowMotionModel();
};
