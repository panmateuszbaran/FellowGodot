/**************************************************************************/
/*  fellow_motion_model.cpp                                               */
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

#include "fellow_motion_model.h"

#include "fellow_motion.glsl.gen.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "servers/rendering/rendering_device_binds.h"
#include "servers/rendering/rendering_server.h"

namespace {

constexpr uint32_t OPW = 48;
constexpr uint32_t NONE = 0xFFFFFFFFu;
constexpr float FPS = 30.0f;
constexpr float EPS_STATS = 1e-5f;

enum GlueMode {
	GM_ROOT_SELECT = 1,
	GM_SEQ1,
	GM_NTOK_ARGMAX,
	GM_SEQ2,
	GM_MASK_COPY,
	GM_ROOT_DENSE,
	GM_REPLACE,
	GM_ROOT_TO_LOCAL,
	GM_POSE_DENSE,
	GM_POSE_SELECT,
	GM_POSE_IN,
	GM_POSE_TOKENS,
};

struct PushConstant {
	uint32_t op;
	uint32_t n;
	uint32_t splits;
	uint32_t pad;
};

uint32_t roundup(uint32_t p_x, uint32_t p_m) {
	return (p_x + p_m - 1) / p_m * p_m;
}

// The statistics' index maps (fmb_plan.py): the dual representation's 370 features.
int dual_global(int p_i) { // global representation feature -> dual index
	return p_i < 5 ? p_i : p_i + 4;
}
int dual_local(int p_i) { // local representation feature -> dual index
	return p_i < 4 ? p_i + 5 : p_i + 5;
}
int local_pose_feat(int p_c) { // the 268 pose features (hip height, positions, rotations) -> local feature
	return p_c == 0 ? 3 : p_c + 3;
}

// 3x3 row-major helpers for the 6D rotations (Gram-Schmidt, as the reference's cont6d_to_matrix).
void mat_from_6d(const float *p_c, float *r_m) {
	float x[3] = { p_c[0], p_c[1], p_c[2] };
	float lx = Math::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
	for (float &v : x) {
		v /= lx;
	}
	const float *y0 = p_c + 3;
	float z[3] = { x[1] * y0[2] - x[2] * y0[1], x[2] * y0[0] - x[0] * y0[2], x[0] * y0[1] - x[1] * y0[0] };
	float lz = Math::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
	for (float &v : z) {
		v /= lz;
	}
	float y[3] = { z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0] };
	// Columns x, y, z.
	for (int i = 0; i < 3; i++) {
		r_m[i * 3 + 0] = x[i];
		r_m[i * 3 + 1] = y[i];
		r_m[i * 3 + 2] = z[i];
	}
}

void mat_to_6d(const float *p_m, float *r_c) {
	for (int i = 0; i < 3; i++) {
		r_c[i] = p_m[i * 3 + 0];
		r_c[3 + i] = p_m[i * 3 + 1];
	}
}

// r = a * b * c^T
void mat_abct(const float *p_a, const float *p_b, const float *p_c, float *r) {
	float ab[9];
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			ab[i * 3 + j] = p_a[i * 3 + 0] * p_b[0 * 3 + j] + p_a[i * 3 + 1] * p_b[1 * 3 + j] + p_a[i * 3 + 2] * p_b[2 * 3 + j];
		}
	}
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			r[i * 3 + j] = ab[i * 3 + 0] * p_c[j * 3 + 0] + ab[i * 3 + 1] * p_c[j * 3 + 1] + ab[i * 3 + 2] * p_c[j * 3 + 2];
		}
	}
}

void retarget_6d(float *p_c6, const float *p_from, const float *p_to) {
	for (int j = 0; j < FellowMotionModel::JOINTS; j++) {
		float m[9], o[9];
		mat_from_6d(p_c6 + j * 6, m);
		mat_abct(m, p_from + j * 9, p_to + j * 9, o);
		mat_to_6d(o, p_c6 + j * 6);
	}
}

} // namespace

void FellowMotionModel::Batch::resize(int p_n) {
	n = p_n;
	global_root_values.resize(p_n * SLOTS * 5);
	has_global_root_values.resize(p_n * SLOTS);
	local_root_values.resize(p_n * SLOTS * 4);
	has_local_root_values.resize(p_n * SLOTS);
	local_poses.resize(p_n * SLOTS * POSE_DIM);
	has_local_poses.resize(p_n * SLOTS);
	allowed_pred_num_tokens.resize(p_n * 11);
	pred_global_motions.resize(p_n * FRAMES * MOTION_DIM);
	pred_num_tokens.resize(p_n);
}

void FellowMotionModel::_free() {
	loaded = false;
	if (!rd) {
		return;
	}
	rd->make_current();
	for (KeyValue<uint64_t, RID> &kv : uniform_sets) {
		if (rd->uniform_set_is_valid(kv.value)) {
			rd->free_rid(kv.value);
		}
	}
	uniform_sets.clear();
	for (RID &b : weight_buffers) {
		rd->free_rid(b);
	}
	weight_buffers.clear();
	for (RID *b : { &param_buffer, &act_buffer, &op_buffer, &part_buffer }) {
		if (b->is_valid()) {
			rd->free_rid(*b);
			*b = RID();
		}
	}
	for (int i = 0; i < V_MAX; i++) {
		if (pipelines[i].is_valid()) {
			rd->free_rid(pipelines[i]);
			pipelines[i] = RID();
		}
		if (shaders[i].is_valid()) {
			rd->free_rid(shaders[i]);
			shaders[i] = RID();
		}
	}
	memdelete(rd);
	rd = nullptr;
}

RID FellowMotionModel::_uniform_set(int p_version, uint32_t p_chunk) {
	uint64_t key = (uint64_t(p_version) << 32) | p_chunk;
	if (RID *s = uniform_sets.getptr(key)) {
		return *s;
	}
	Vector<RD::Uniform> u;
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, act_buffer));
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, weight_buffers[MIN(p_chunk, weight_buffers.size() - 1)]));
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, param_buffer));
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 3, op_buffer));
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 4, part_buffer));
	RID s = rd->uniform_set_create(u, shaders[p_version], 0);
	uniform_sets[key] = s;
	return s;
}

// Rows per workgroup (16/32/64) and the split-K count that fills the GPU when the batch is small.
int FellowMotionModel::_splits(const OpRec &p_op, int p_n, int &r_bm) const {
	uint32_t m = p_op.rows * p_n;
	r_bm = m <= 16 ? 16 : (m <= 32 ? 32 : 64);
	uint32_t groups = ((p_op.nout + 63) / 64) * ((m + r_bm - 1) / r_bm);
	uint32_t ktiles = (p_op.ke - p_op.kb + 31) / 32;
	int s = 1;
	if (groups < uint32_t(target_groups)) {
		s = int((target_groups + groups - 1) / groups);
		s = MIN(s, int(MAX(1u, ktiles / 4)));
		s = MIN(s, 16);
	}
	return MAX(s, 1);
}

Error FellowMotionModel::load(const String &p_path, int p_max_batch) {
	MutexLock lock(mutex);
	_free();
	path = p_path;
	max_batch = CLAMP(p_max_batch, 1, 256);

	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_FILE_CANT_OPEN, "FellowMotionModel: cannot open " + p_path + ".");
	uint8_t magic[4];
	f->get_buffer(magic, 4);
	ERR_FAIL_COND_V_MSG(memcmp(magic, "FMB1", 4) != 0, ERR_FILE_UNRECOGNIZED, "FellowMotionModel: not an .fmb file: " + p_path + ".");
	uint32_t version = f->get_32();
	ERR_FAIL_COND_V_MSG(version != 1, ERR_FILE_UNRECOGNIZED, "FellowMotionModel: unsupported .fmb version.");
	uint32_t jlen = f->get_32();
	Vector<uint8_t> js;
	js.resize(jlen);
	f->get_buffer(js.ptrw(), jlen);
	JSON json;
	ERR_FAIL_COND_V_MSG(json.parse(String::utf8((const char *)js.ptr(), jlen)) != OK, ERR_PARSE_ERROR, "FellowMotionModel: bad header.");
	Dictionary head = json.get_data();
	meta = head["meta"];
	weights_f16 = String(head["weight_type"]) == "f16";

	// Statistics and rig offsets.
	Array mn = meta["mean"];
	Array sd = meta["std"];
	ERR_FAIL_COND_V(mn.size() != 370 || sd.size() != 370, ERR_FILE_CORRUPT);
	mean.resize(370);
	std_dev.resize(370);
	for (int i = 0; i < 370; i++) {
		mean[i] = float(mn[i]);
		std_dev[i] = float(sd[i]);
	}
	Array co = meta["capture_offsets"];
	Array so = meta["standard_offsets"];
	ERR_FAIL_COND_V(co.size() != JOINTS * 9 || so.size() != JOINTS * 9, ERR_FILE_CORRUPT);
	for (int i = 0; i < JOINTS * 9; i++) {
		capture_offsets[i] = float(co[i]);
		standard_offsets[i] = float(so[i]);
	}

	// Activations: one buffer, every tensor sized for the largest batch; views share their base.
	acts.clear();
	Dictionary ad = head["acts"];
	act_floats = 0;
	for (const KeyValue<Variant, Variant> &kv : ad) {
		Dictionary a = kv.value;
		ActInfo ai;
		ai.rows = int(a["rows"]);
		ai.cols = int(a["cols"]);
		if (a["view_of"].get_type() == Variant::NIL) {
			ai.offset = act_floats;
			act_floats += roundup(ai.rows * ai.cols * max_batch, 64);
		} else {
			ai.offset = acts[String(a["view_of"])].offset;
		}
		acts[String(kv.key)] = ai;
	}
	Dictionary mats = head["mats"];
	Dictionary params = head["params"];
	auto A = [&](const Variant &p_name) -> const ActInfo & {
		return acts[String(p_name)];
	};
	auto P = [&](const Variant &p_name) -> uint32_t {
		if (p_name.get_type() == Variant::NIL) {
			return NONE;
		}
		return uint32_t(int(Dictionary(params[p_name])["offset"]));
	};
	uint32_t t_off = acts["meta.T"].offset;

	// The op table.
	Array ops = head["ops"];
	LocalVector<uint32_t> table;
	table.resize(ops.size() * OPW);
	memset(table.ptr(), 0, table.size() * sizeof(uint32_t));
	op_recs.clear();
	op_recs.resize(ops.size());
	for (int i = 0; i < ops.size(); i++) {
		Dictionary o = ops[i];
		uint32_t *v = table.ptr() + i * OPW;
		OpRec &rec = op_recs[i];
		rec.tag = o["tag"];
		String kind = o["op"];
		if (kind == "gemm") {
			rec.kind = 0;
			const ActInfo &a = A(o["a"]);
			const ActInfo &out = A(o["out"]);
			Dictionary m = mats[o["w"]];
			Array amap = o["amap"], omap = o["omap"], conv = o["conv"], kr = o["kr"];
			rec.rows = int(o["rows"]);
			rec.k = int(o["k"]);
			rec.nout = int(o["n"]);
			rec.kb = int(kr[0]);
			rec.ke = int(kr[1]);
			rec.chunk = int(m["chunk"]);
			v[2] = rec.rows;
			v[3] = rec.k;
			v[4] = rec.nout;
			v[5] = a.offset;
			v[6] = a.rows;
			v[7] = a.cols;
			v[8] = int(amap[0]);
			v[9] = int(amap[1]);
			v[10] = int(amap[2]);
			v[11] = int(m["offset"]);
			v[12] = int(m["cols"]);
			v[13] = P(o["b"]);
			v[14] = out.offset;
			v[15] = out.rows;
			v[16] = out.cols;
			v[17] = int(o["oc"]);
			v[18] = int(omap[0]);
			v[19] = int(omap[1]);
			v[20] = int(omap[2]);
			if (o["res"].get_type() != Variant::NIL) {
				const ActInfo &r = A(o["res"]);
				v[21] = r.offset;
				v[22] = r.rows;
				v[23] = r.cols;
			} else {
				v[21] = NONE;
			}
			v[24] = int(conv[0]);
			v[25] = int(conv[1]);
			v[26] = int(conv[2]);
			v[27] = rec.kb;
			v[28] = rec.ke;
			v[29] = int(o["aprelu"]);
			v[30] = int(o["act"]);
			v[31] = int(o["mask"]);
			v[32] = t_off;
		} else if (kind == "attn") {
			rec.kind = 1;
			rec.rows = int(o["rows"]);
			rec.heads = int(o["heads"]);
			v[2] = rec.rows;
			v[3] = int(o["d"]);
			v[4] = rec.heads;
			v[5] = A(o["qkv"]).offset;
			v[6] = A(o["out"]).offset;
			int ms = int(o["mask_start"]);
			v[7] = ms < 0 ? NONE : uint32_t(ms);
			v[8] = t_off;
		} else if (kind == "ln") {
			rec.kind = 2;
			rec.rows = int(o["rows"]);
			v[2] = rec.rows;
			v[3] = int(o["d"]);
			v[4] = A(o["x"]).offset;
			v[5] = P(o["g"]);
			v[6] = P(o["b"]);
		} else {
			rec.kind = 3;
			String mode = o["mode"];
			v[46] = t_off;
			if (mode == "root_select") {
				v[1] = GM_ROOT_SELECT;
				v[2] = A(o["emb"]).offset;
				v[3] = A(o["flags"]).offset;
				v[4] = P(o["no"]);
				rec.work = 8 * 384;
			} else if (mode == "seq1") {
				v[1] = GM_SEQ1;
				v[2] = A(o["fe"]).offset;
				v[3] = A(o["pf"]).offset;
				v[4] = A(o["s"]).offset;
				v[5] = P(o["inpos"]);
				v[6] = P(o["ntok"]);
				rec.work = 9 * 512;
			} else if (mode == "ntok_argmax") {
				v[1] = GM_NTOK_ARGMAX;
				v[2] = A(o["logit"]).offset;
				v[3] = A(o["allowed"]).offset;
				v[4] = A(o["T"]).offset;
				rec.work = 1;
			} else if (mode == "seq2") {
				v[1] = GM_SEQ2;
				v[2] = A(o["pf"]).offset;
				v[3] = A(o["s"]).offset;
				v[4] = P(o["mid"]);
				v[5] = P(o["pos"]);
				rec.work = 25 * 512;
			} else if (mode == "mask_copy") {
				v[1] = GM_MASK_COPY;
				const ActInfo &src = A(o["src"]);
				const ActInfo &dst = A(o["dst"]);
				v[2] = src.offset;
				v[3] = src.rows;
				v[4] = int(o["src_off"]);
				v[5] = dst.offset;
				v[6] = int(o["rows"]);
				v[7] = dst.cols;
				v[8] = int(o["mask"]);
				rec.work = v[6] * v[7];
			} else if (mode == "root_dense") {
				v[1] = GM_ROOT_DENSE;
				v[2] = A(o["fe"]).offset;
				v[3] = A(o["flags"]).offset;
				v[4] = A(o["grv"]).offset;
				v[5] = P(o["nofr"]);
				v[6] = A(o["df"]).offset;
				v[7] = A(o["dg"]).offset;
				v[8] = A(o["dh"]).offset;
				rec.work = 64 * 512;
			} else if (mode == "replace") {
				v[1] = GM_REPLACE;
				const ActInfo &h = A(o["h"]);
				v[2] = h.offset;
				v[3] = A(o["src"]).offset;
				v[4] = A(o["has"]).offset;
				v[5] = h.cols;
				rec.work = 64 * h.cols;
			} else if (mode == "root_to_local") {
				v[1] = GM_ROOT_TO_LOCAL;
				v[2] = A(o["src"]).offset;
				v[3] = A(o["lrv"]).offset;
				v[4] = A(o["flags"]).offset;
				v[5] = A(o["dst"]).offset;
				v[6] = P(o["stats"]);
				rec.work = 1;
			} else if (mode == "pose_dense") {
				v[1] = GM_POSE_DENSE;
				v[2] = A(o["lp"]).offset;
				v[3] = A(o["flags"]).offset;
				v[4] = A(o["pc"]).offset;
				v[5] = A(o["ph"]).offset;
				rec.work = 64 * 268;
			} else if (mode == "pose_select") {
				v[1] = GM_POSE_SELECT;
				v[2] = A(o["pce"]).offset;
				v[3] = A(o["ph"]).offset;
				v[4] = A(o["dst"]).offset;
				v[5] = P(o["c"]);
				rec.work = 64 * 160;
			} else if (mode == "pose_in") {
				v[1] = GM_POSE_IN;
				v[2] = A(o["s"]).offset;
				v[3] = P(o["ntok"]);
				v[4] = P(o["pos"]);
				rec.work = 16 * 1024;
			} else if (mode == "pose_tokens") {
				v[1] = GM_POSE_TOKENS;
				v[2] = A(o["logit"]).offset;
				v[3] = A(o["dst"]).offset;
				v[4] = P(o["codebook"]);
				rec.work = 16 * 160;
			} else {
				ERR_FAIL_V_MSG(ERR_FILE_CORRUPT, "FellowMotionModel: unknown glue mode " + mode + ".");
			}
			v[47] = rec.work;
		}
		v[0] = rec.kind;
	}

	// The split-K scratch: the largest over the ops and batch sizes.
	part_floats = 64;
	for (const OpRec &op : op_recs) {
		if (op.kind != 0) {
			continue;
		}
		for (int n = 1; n <= max_batch; n++) {
			int bm;
			int s = _splits(op, n, bm);
			if (s > 1) {
				part_floats = MAX(part_floats, uint32_t(s) * op.rows * n * op.nout);
			}
		}
	}

	// The device and the shaders.
	rd = RenderingServer::get_singleton()->create_local_rendering_device();
	ERR_FAIL_NULL_V_MSG(rd, ERR_UNAVAILABLE, "FellowMotionModel requires a RenderingDevice (Forward+ or Mobile renderer).");
	shader_file.instantiate();
	Error err = shader_file->parse_versions_from_text(fellow_motion_shader_glsl);
	if (err != OK) {
		shader_file->print_errors("fellow_motion");
		_free();
		return err;
	}
	const char *names_h[V_MAX] = { "gemm_h16", "gemm_h32", "gemm_h64", "reduce", "attn", "ln", "glue" };
	const char *names_f[V_MAX] = { "gemm_f16", "gemm_f32", "gemm_f64", "reduce", "attn", "ln", "glue" };
	for (int i = 0; i < V_MAX; i++) {
		const char *nm = weights_f16 ? names_h[i] : names_f[i];
		shaders[i] = rd->shader_create_from_spirv(shader_file->get_spirv_stages(nm), String("FellowMotion ") + nm);
		if (shaders[i].is_null()) {
			_free();
			ERR_FAIL_V_MSG(ERR_CANT_CREATE, String("FellowMotionModel: shader ") + nm + " failed.");
		}
		pipelines[i] = rd->compute_pipeline_create(shaders[i]);
	}

	// Weights: one storage buffer per section (each under 128 MB), then the params.
	Array sections = head["sections"];
	for (int i = 0; i < sections.size(); i++) {
		Dictionary s = sections[i];
		uint64_t off = uint64_t(int64_t(s["offset"]));
		uint32_t bytes = uint32_t(int64_t(s["bytes"]));
		Vector<uint8_t> data;
		data.resize(bytes);
		f->seek(off);
		f->get_buffer(data.ptrw(), bytes);
		RID b = rd->storage_buffer_create(bytes, data.span());
		if (i + 1 < sections.size()) {
			weight_buffers.push_back(b);
		} else {
			param_buffer = b;
		}
	}
	// The weight sections are under 96 MB (the converter's chunks); keep the activations under 128 MB too, the smallest
	// storage buffer range among the drivers this runs on.
	if (uint64_t(act_floats) * 4 > (uint64_t(128) << 20) || uint64_t(part_floats) * 4 > (uint64_t(128) << 20)) {
		WARN_PRINT("FellowMotionModel: the activations exceed 128 MB at this max_batch; some drivers may not bind them.");
	}
	act_buffer = rd->storage_buffer_create(act_floats * 4);
	part_buffer = rd->storage_buffer_create(part_floats * 4);
	op_buffer = rd->storage_buffer_create(table.size() * 4, Span<uint8_t>((const uint8_t *)table.ptr(), table.size() * 4));
	rd->set_resource_name(act_buffer, "FellowMotion activations");
	loaded = true;
	return OK;
}

Dictionary FellowMotionModel::get_info() const {
	Dictionary d;
	d["path"] = path;
	d["loaded"] = loaded;
	d["weights"] = weights_f16 ? "f16" : "f32";
	d["ops"] = int(op_recs.size());
	d["max_batch"] = max_batch;
	d["activation_mb"] = act_floats * 4.0 / 1e6;
	d["split_scratch_mb"] = part_floats * 4.0 / 1e6;
	d["notice"] = meta.get("notice", "");
	if (rd) {
		d["device"] = rd->get_device_name();
	}
	return d;
}

void FellowMotionModel::_prepare(const Batch &p_b, LocalVector<float> &r_grv_n, LocalVector<float> &r_off, LocalVector<float> &r_head0) {
	const int n = p_b.n;
	LocalVector<float> grv, lrv, lp, flags, allowed;
	grv.resize(n * SLOTS * 5);
	lrv.resize(n * SLOTS * 4);
	lp.resize(n * SLOTS * 268);
	flags.resize(n * SLOTS * 4);
	allowed.resize(n * 12);
	r_off.resize(n * 2);
	r_head0.resize(n);
	auto gstd = [&](int i) { return Math::sqrt(std_dev[dual_global(i)] * std_dev[dual_global(i)] + EPS_STATS); };
	auto lstd = [&](int i) { return Math::sqrt(std_dev[dual_local(i)] * std_dev[dual_local(i)] + EPS_STATS); };
	for (int b = 0; b < n; b++) {
		const float *g0 = p_b.global_root_values.ptr() + b * SLOTS * 5;
		float ox = g0[0], oz = g0[2];
		r_off[b * 2 + 0] = ox;
		r_off[b * 2 + 1] = oz;
		r_head0[b] = Math::atan2(g0[4], g0[3]);
		for (int s = 0; s < SLOTS; s++) {
			const float *gi = p_b.global_root_values.ptr() + (b * SLOTS + s) * 5;
			float g[5] = { gi[0] - ox, gi[1], gi[2] - oz, gi[3], gi[4] };
			for (int k = 0; k < 5; k++) {
				grv[(b * SLOTS + s) * 5 + k] = (g[k] - mean[dual_global(k)]) / gstd(k);
			}
			for (int k = 0; k < 4; k++) {
				lrv[(b * SLOTS + s) * 4 + k] = (p_b.local_root_values[(b * SLOTS + s) * 4 + k] - mean[dual_local(k)]) / lstd(k);
			}
			float pose[POSE_DIM];
			memcpy(pose, p_b.local_poses.ptr() + (b * SLOTS + s) * POSE_DIM, sizeof(pose));
			retarget_6d(pose + 87, capture_offsets, standard_offsets);
			float *o = lp.ptr() + (b * SLOTS + s) * 268;
			for (int c = 0; c < 268; c++) {
				int li = local_pose_feat(c);
				float x = c == 0 ? g[1] : pose[c - 1];
				o[c] = (x - mean[dual_local(li)]) / lstd(li);
			}
			float hg = float(p_b.has_global_root_values[b * SLOTS + s]);
			float hl = float(p_b.has_local_root_values[b * SLOTS + s]);
			float hp = float(p_b.has_local_poses[b * SLOTS + s]);
			float *fl = flags.ptr() + (b * SLOTS + s) * 4;
			fl[0] = hg;
			fl[1] = hl;
			fl[2] = hp;
			fl[3] = MAX(MAX(hg, hl), hp);
		}
		for (int k = 0; k < 12; k++) {
			allowed[b * 12 + k] = k < 11 ? float(p_b.allowed_pred_num_tokens[b * 11 + k]) : 0.0f;
		}
	}
	r_grv_n = grv;
	auto up = [&](const char *p_name, const LocalVector<float> &p_data) {
		rd->buffer_update(act_buffer, acts[p_name].offset * 4, p_data.size() * 4, p_data.ptr());
	};
	up("in.grv", grv);
	up("in.lrv", lrv);
	up("in.lp", lp);
	up("in.flags", flags);
	up("in.allowed", allowed);
}

void FellowMotionModel::_decode(Batch &p_b, const LocalVector<float> &p_vout, const LocalVector<float> &p_rout, const LocalVector<float> &p_T, const LocalVector<float> &p_grv_n, const LocalVector<float> &p_off, const LocalVector<float> &p_head0) {
	const int n = p_b.n;
	float lm[365], ls[365], gm[5], gs[5];
	for (int i = 0; i < 365; i++) {
		lm[i] = mean[dual_local(i)];
		ls[i] = Math::sqrt(std_dev[dual_local(i)] * std_dev[dual_local(i)] + EPS_STATS);
	}
	for (int i = 0; i < 5; i++) {
		gm[i] = mean[dual_global(i)];
		gs[i] = Math::sqrt(std_dev[dual_global(i)] * std_dev[dual_global(i)] + EPS_STATS);
	}
	for (int b = 0; b < n; b++) {
		int T = int(p_T[b * 4]);
		p_b.pred_num_tokens[b] = T;
		float *out = p_b.pred_global_motions.ptr() + b * FRAMES * MOTION_DIM;
		float ang = 0.0f, px = 0.0f, pz = 0.0f;
		for (int f = 0; f < FRAMES; f++) {
			const float *v = p_vout.ptr() + (b * FRAMES + f) * 365;
			float loc[365];
			for (int c = 0; c < 365; c++) {
				loc[c] = v[c] * ls[c] + lm[c];
			}
			float a2 = Math::atan2(Math::sin(ang * 0.5f), Math::cos(ang * 0.5f)) * 2.0f;
			float *o = out + f * MOTION_DIM;
			o[0] = px;
			o[1] = loc[3];
			o[2] = pz;
			o[3] = Math::cos(a2);
			o[4] = Math::sin(a2);
			memcpy(o + 5, loc + 4, 361 * sizeof(float));
			ang += loc[0] / FPS;
			px += loc[1] / FPS;
			pz += loc[2] / FPS;
		}
		// The root module's root where the pose module misses the target height (final_root_pred_mode).
		float e_pose = 0.0f, e_root = 0.0f, wsum = 0.0f;
		for (int i = 0; i < 4; i++) {
			int fr = (T - 1) * 4 + i;
			float w = float(p_b.has_global_root_values[b * SLOTS + 4 + i]);
			float th = p_grv_n[(b * SLOTS + 4 + i) * 5 + 1] * gs[1] + gm[1];
			float hr = p_rout[(b * FRAMES + fr) * 5 + 1] * gs[1] + gm[1];
			e_pose += Math::abs(out[fr * MOTION_DIM + 1] - th) * w;
			e_root += Math::abs(hr - th) * w;
			wsum += w;
		}
		if (wsum > 0.0f) {
			e_pose /= wsum;
			e_root /= wsum;
			if (e_root < e_pose && e_pose > 0.05f) {
				for (int f = 0; f < FRAMES; f++) {
					for (int k = 0; k < 5; k++) {
						out[f * MOTION_DIM + k] = p_rout[(b * FRAMES + f) * 5 + k] * gs[k] + gm[k];
					}
				}
			}
		}
		for (int f = 0; f < FRAMES; f++) {
			float *o = out + f * MOTION_DIM;
			float a = Math::atan2(o[4], o[3]) + p_head0[b];
			o[3] = Math::cos(a);
			o[4] = Math::sin(a);
			o[0] += p_off[b * 2 + 0];
			o[2] += p_off[b * 2 + 1];
			retarget_6d(o + 92, standard_offsets, capture_offsets);
		}
	}
}

Error FellowMotionModel::run_batch(Batch &p_batch) {
	MutexLock lock(mutex);
	ERR_FAIL_COND_V_MSG(!loaded, ERR_UNCONFIGURED, "FellowMotionModel: no model loaded.");
	const int n = p_batch.n;
	ERR_FAIL_COND_V_MSG(n < 1 || n > max_batch, ERR_INVALID_PARAMETER, "FellowMotionModel: batch size outside 1..max_batch.");
	uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	rd->make_current();

	LocalVector<float> grv_n, off, head0;
	_prepare(p_batch, grv_n, off, head0);

	PushConstant pcs = {};
	pcs.n = uint32_t(n);
	if (profile) {
		rd->capture_timestamp("start");
	}
	for (uint32_t i = 0; i < op_recs.size(); i++) {
		const OpRec &op = op_recs[i];
		pcs.op = i;
		pcs.splits = 1;
		int version = V_GLUE;
		uint32_t gx = 1, gy = 1, gz = 1;
		switch (op.kind) {
			case 0: {
				int bm;
				int s = _splits(op, n, bm);
				pcs.splits = uint32_t(s);
				version = bm == 16 ? V_GEMM16 : (bm == 32 ? V_GEMM32 : V_GEMM64);
				gx = (op.nout + 63) / 64;
				gy = (op.rows * n + bm - 1) / bm;
				gz = s;
			} break;
			case 1:
				version = V_ATTN;
				gx = n * op.heads;
				break;
			case 2:
				version = V_LN;
				gx = n * op.rows;
				break;
			default:
				version = V_GLUE;
				gx = (n * op.work + 255) / 256;
				break;
		}
		RD::ComputeListID cl = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(cl, pipelines[version]);
		rd->compute_list_bind_uniform_set(cl, _uniform_set(version, op.kind == 0 ? op.chunk : 0), 0);
		rd->compute_list_set_push_constant(cl, &pcs, sizeof(pcs));
		rd->compute_list_dispatch(cl, gx, gy, gz);
		rd->compute_list_end();
		if (op.kind == 0 && pcs.splits > 1) {
			cl = rd->compute_list_begin();
			rd->compute_list_bind_compute_pipeline(cl, pipelines[V_REDUCE]);
			rd->compute_list_bind_uniform_set(cl, _uniform_set(V_REDUCE, 0), 0);
			rd->compute_list_set_push_constant(cl, &pcs, sizeof(pcs));
			rd->compute_list_dispatch(cl, (op.rows * n * op.nout + 255) / 256, 1, 1);
			rd->compute_list_end();
		}
		if (profile) {
			rd->capture_timestamp(op.tag);
		}
	}
	uint64_t t1 = OS::get_singleton()->get_ticks_usec();
	rd->submit();
	rd->sync();
	uint64_t t2 = OS::get_singleton()->get_ticks_usec();

	auto read = [&](const char *p_name, int p_per_req) {
		LocalVector<float> v;
		v.resize(n * p_per_req);
		Vector<uint8_t> d = rd->buffer_get_data(act_buffer, acts[p_name].offset * 4, n * p_per_req * 4);
		memcpy(v.ptr(), d.ptr(), v.size() * 4);
		return v;
	};
	LocalVector<float> vout = read("v.out", FRAMES * 365);
	LocalVector<float> rout = read("r.out", FRAMES * 5);
	LocalVector<float> tk = read("meta.T", 4);
	_decode(p_batch, vout, rout, tk, grv_n, off, head0);
	uint64_t t3 = OS::get_singleton()->get_ticks_usec();

	last_timings = Dictionary();
	last_timings["record_msec"] = (t1 - t0) / 1000.0;
	last_timings["submit_sync_msec"] = (t2 - t1) / 1000.0;
	last_timings["read_decode_msec"] = (t3 - t2) / 1000.0;
	last_timings["total_msec"] = (t3 - t0) / 1000.0;
	if (profile) {
		uint32_t cnt = rd->get_captured_timestamps_count();
		if (cnt > 1) {
			Dictionary per;
			uint64_t first = rd->get_captured_timestamp_gpu_time(0);
			uint64_t prev = first;
			for (uint32_t i = 1; i < cnt; i++) {
				uint64_t tt = rd->get_captured_timestamp_gpu_time(i);
				String name = rd->get_captured_timestamp_name(i);
				double dt = (tt - prev) / 1e6; // ns -> ms
				per[name] = double(per.get(name, 0.0)) + dt;
				prev = tt;
			}
			last_timings["gpu_msec"] = (prev - first) / 1e6;
			last_timings["ops"] = per;
		}
	}
	return OK;
}

Dictionary FellowMotionModel::run(const Dictionary &p_inputs) {
	Dictionary out;
	PackedFloat32Array grv = p_inputs.get("global_root_values", PackedFloat32Array());
	int n = grv.size() / (SLOTS * 5);
	ERR_FAIL_COND_V_MSG(n < 1 || grv.size() != n * SLOTS * 5, out, "FellowMotionModel.run: global_root_values must hold N x 8 x 5 floats.");
	Batch b;
	b.resize(n);
	auto fl = [&](const char *p_name, LocalVector<float> &r_dst) -> bool {
		PackedFloat32Array a = p_inputs.get(p_name, PackedFloat32Array());
		ERR_FAIL_COND_V_MSG(a.size() != int(r_dst.size()), false, String("FellowMotionModel.run: wrong size of ") + p_name + ".");
		memcpy(r_dst.ptr(), a.ptr(), r_dst.size() * 4);
		return true;
	};
	auto in = [&](const char *p_name, LocalVector<int32_t> &r_dst) -> bool {
		PackedInt32Array a = p_inputs.get(p_name, PackedInt32Array());
		ERR_FAIL_COND_V_MSG(a.size() != int(r_dst.size()), false, String("FellowMotionModel.run: wrong size of ") + p_name + ".");
		memcpy(r_dst.ptr(), a.ptr(), r_dst.size() * 4);
		return true;
	};
	if (!fl("global_root_values", b.global_root_values) || !fl("local_root_values", b.local_root_values) || !fl("local_poses", b.local_poses) ||
			!in("has_global_root_values", b.has_global_root_values) || !in("has_local_root_values", b.has_local_root_values) ||
			!in("has_local_poses", b.has_local_poses) || !in("allowed_pred_num_tokens", b.allowed_pred_num_tokens)) {
		return out;
	}
	if (run_batch(b) != OK) {
		return out;
	}
	PackedFloat32Array m;
	m.resize(b.pred_global_motions.size());
	memcpy(m.ptrw(), b.pred_global_motions.ptr(), m.size() * 4);
	PackedInt32Array t;
	t.resize(n);
	memcpy(t.ptrw(), b.pred_num_tokens.ptr(), n * 4);
	out["pred_global_motions"] = m;
	out["pred_num_tokens"] = t;
	return out;
}

PackedFloat32Array FellowMotionModel::get_activation(const String &p_name, int p_count) {
	MutexLock lock(mutex);
	PackedFloat32Array r;
	ERR_FAIL_COND_V(!loaded || !acts.has(p_name), r);
	const ActInfo &a = acts[p_name];
	int n = p_count < 0 ? max_batch : MIN(p_count, max_batch);
	rd->make_current();
	Vector<uint8_t> d = rd->buffer_get_data(act_buffer, a.offset * 4, n * a.rows * a.cols * 4);
	r.resize(n * a.rows * a.cols);
	memcpy(r.ptrw(), d.ptr(), r.size() * 4);
	return r;
}

void FellowMotionModel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load", "path", "max_batch"), &FellowMotionModel::load, DEFVAL(16));
	ClassDB::bind_method(D_METHOD("is_loaded"), &FellowMotionModel::is_loaded);
	ClassDB::bind_method(D_METHOD("get_max_batch"), &FellowMotionModel::get_max_batch);
	ClassDB::bind_method(D_METHOD("get_info"), &FellowMotionModel::get_info);
	ClassDB::bind_method(D_METHOD("run", "inputs"), &FellowMotionModel::run);
	ClassDB::bind_method(D_METHOD("get_activation", "name", "count"), &FellowMotionModel::get_activation, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("set_profile", "enable"), &FellowMotionModel::set_profile);
	ClassDB::bind_method(D_METHOD("is_profile"), &FellowMotionModel::is_profile);
	ClassDB::bind_method(D_METHOD("set_target_groups", "groups"), &FellowMotionModel::set_target_groups);
	ClassDB::bind_method(D_METHOD("get_target_groups"), &FellowMotionModel::get_target_groups);
	ClassDB::bind_method(D_METHOD("get_last_timings"), &FellowMotionModel::get_last_timings);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "profile"), "set_profile", "is_profile");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "target_groups"), "set_target_groups", "get_target_groups");
}

FellowMotionModel::FellowMotionModel() {
	memset(capture_offsets, 0, sizeof(capture_offsets));
	memset(standard_offsets, 0, sizeof(standard_offsets));
}

FellowMotionModel::~FellowMotionModel() {
	MutexLock lock(mutex);
	_free();
}
