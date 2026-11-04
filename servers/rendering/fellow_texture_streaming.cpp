/**************************************************************************/
/*  fellow_texture_streaming.cpp                                          */
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

#include "fellow_texture_streaming.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/templates/sort_array.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/storage/material_storage.h"
#include "servers/rendering/storage/texture_storage.h"

#include <cmath>

FellowTextureStreaming *FellowTextureStreaming::singleton = nullptr;

FellowTextureStreaming *FellowTextureStreaming::get_singleton() {
	if (unlikely(singleton == nullptr)) {
		singleton = memnew(FellowTextureStreaming);
	}
	return singleton;
}

void FellowTextureStreaming::destroy_singleton() {
	if (singleton) {
		singleton->finish();
		memdelete(singleton);
		singleton = nullptr;
	}
}

bool FellowTextureStreaming::enabled_at_startup() {
	static int state = -1;
	if (state < 0) {
		ProjectSettings *ps = ProjectSettings::get_singleton();
		bool on = ps && ps->has_setting("rendering/fellow/streaming/textures") && bool(ps->get_setting("rendering/fellow/streaming/textures"));
		if (on && ps->has_setting("rendering/fellow/enabled") && !bool(ps->get_setting("rendering/fellow/enabled"))) {
			on = false;
		}
		state = on ? 1 : 0;
	}
	return state == 1;
}

int FellowTextureStreaming::resident_size() {
	static int size = -1;
	if (size < 0) {
		ProjectSettings *ps = ProjectSettings::get_singleton();
		size = (ps && ps->has_setting("rendering/fellow/streaming/texture_resident_size")) ? int(ps->get_setting("rendering/fellow/streaming/texture_resident_size")) : 128;
		size = CLAMP(size, 4, 16384);
	}
	return size;
}

static int _setting_int(const char *p_name, int p_default) {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps && ps->has_setting(p_name)) {
		return int(ps->get_setting(p_name));
	}
	return p_default;
}

static float _setting_float(const char *p_name, float p_default) {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps && ps->has_setting(p_name)) {
		return float(ps->get_setting(p_name));
	}
	return p_default;
}

/* UV DENSITY */

float FellowTextureStreaming::compute_uv_density(const RenderingServerTypes::SurfaceData &p_surface) {
	const uint64_t format = p_surface.format;
	if (p_surface.primitive != RSE::PRIMITIVE_TRIANGLES || !(format & RSE::ARRAY_FORMAT_TEX_UV) || !(format & RSE::ARRAY_FORMAT_VERTEX) || (format & RSE::ARRAY_FLAG_USE_2D_VERTICES)) {
		return 0.0f;
	}
	const uint32_t vcount = p_surface.vertex_count;
	if (vcount < 3) {
		return 0.0f;
	}
	uint32_t offsets[RSE::ARRAY_MAX];
	uint32_t vertex_stride = 0, normal_stride = 0, attrib_stride = 0, skin_stride = 0;
	RenderingServer::get_singleton()->mesh_surface_make_offsets_from_format(format, vcount, p_surface.index_count, offsets, vertex_stride, normal_stride, attrib_stride, skin_stride);
	const uint8_t *vr = p_surface.vertex_data.ptr();
	const uint8_t *ar = p_surface.attribute_data.ptr();
	if (vr == nullptr || ar == nullptr || uint64_t(p_surface.vertex_data.size()) < uint64_t(vertex_stride) * vcount || uint64_t(p_surface.attribute_data.size()) < uint64_t(attrib_stride) * vcount) {
		return 0.0f;
	}
	const bool compressed = format & RSE::ARRAY_FLAG_COMPRESS_ATTRIBUTES;
	const AABB aabb = p_surface.aabb;
	const Vector4 uv_scale = p_surface.uv_scale;

	auto position = [&](uint32_t i) -> Vector3 {
		const uint8_t *p = vr + uint64_t(i) * vertex_stride + offsets[RSE::ARRAY_VERTEX];
		if (compressed) {
			const uint16_t *v = reinterpret_cast<const uint16_t *>(p);
			return Vector3(v[0] / 65535.0f, v[1] / 65535.0f, v[2] / 65535.0f) * aabb.size + aabb.position;
		}
		const float *v = reinterpret_cast<const float *>(p);
		return Vector3(v[0], v[1], v[2]);
	};
	auto uv = [&](uint32_t i) -> Vector2 {
		const uint8_t *p = ar + uint64_t(i) * attrib_stride + offsets[RSE::ARRAY_TEX_UV];
		if (compressed) {
			const uint16_t *v = reinterpret_cast<const uint16_t *>(p);
			Vector2 t(v[0] / 65535.0f, v[1] / 65535.0f);
			if (!uv_scale.is_zero_approx()) {
				t = (t - Vector2(0.5f, 0.5f)) * Vector2(uv_scale.x, uv_scale.y);
			}
			return t;
		}
		const float *v = reinterpret_cast<const float *>(p);
		return Vector2(v[0], v[1]);
	};

	const uint32_t icount = p_surface.index_count;
	const bool indexed = icount > 0;
	const bool index16 = vcount < (1 << 16);
	const uint8_t *ir = p_surface.index_data.ptr();
	if (indexed && (ir == nullptr || uint64_t(p_surface.index_data.size()) < uint64_t(icount) * (index16 ? 2 : 4))) {
		return 0.0f;
	}
	auto index = [&](uint32_t i) -> uint32_t {
		if (!indexed) {
			return i;
		}
		return index16 ? uint32_t(reinterpret_cast<const uint16_t *>(ir)[i]) : reinterpret_cast<const uint32_t *>(ir)[i];
	};
	const uint32_t tris = (indexed ? icount : vcount) / 3;
	if (tris == 0) {
		return 0.0f;
	}
	const uint32_t max_samples = 8192;
	const uint32_t step = MAX(1u, tris / max_samples);

	struct Sample {
		float sigma;
		float area;
		bool operator<(const Sample &p_o) const { return sigma < p_o.sigma; }
	};
	LocalVector<Sample> samples;
	samples.reserve(MIN(tris, max_samples + 1));
	double total_area = 0.0;
	for (uint32_t t = 0; t < tris; t += step) {
		const uint32_t i0 = index(t * 3 + 0), i1 = index(t * 3 + 1), i2 = index(t * 3 + 2);
		if (i0 >= vcount || i1 >= vcount || i2 >= vcount) {
			continue;
		}
		const Vector3 p0 = position(i0);
		const Vector3 e1 = position(i1) - p0;
		const Vector3 e2 = position(i2) - p0;
		const Vector3 n = e1.cross(e2);
		const float area2 = n.length();
		const float l1 = e1.length();
		if (!(area2 > 1e-12f) || !(l1 > 1e-9f)) {
			continue;
		}
		// The triangle in its own plane: e1 along x.
		const Vector3 ux = e1 / l1;
		const Vector3 uy = (n / area2).cross(ux);
		const float ax = l1, bx = e2.dot(ux), by = e2.dot(uy); // a = (ax, 0), b = (bx, by), det = ax * by
		const float det = ax * by;
		if (!(Math::abs(det) > 1e-12f)) {
			continue;
		}
		const Vector2 t0 = uv(i0);
		const Vector2 d1 = uv(i1) - t0;
		const Vector2 d2 = uv(i2) - t0;
		// M = [d1 d2] * inverse([a b]); inverse([[ax, bx], [0, by]]) = [[by, -bx], [0, ax]] / det.
		const float m00 = (d1.x * by) / det, m01 = (-d1.x * bx + d2.x * ax) / det;
		const float m10 = (d1.y * by) / det, m11 = (-d1.y * bx + d2.y * ax) / det;
		// The largest singular value of M: sqrt of the largest eigenvalue of M^T M.
		const float p = m00 * m00 + m10 * m10;
		const float q = m00 * m01 + m10 * m11;
		const float r = m01 * m01 + m11 * m11;
		const float half = 0.5f * (p - r);
		const float lmax = 0.5f * (p + r) + Math::sqrt(half * half + q * q);
		const float sigma = Math::sqrt(MAX(lmax, 0.0f));
		if (!Math::is_finite(sigma)) {
			continue;
		}
		samples.push_back({ sigma, 0.5f * area2 });
		total_area += 0.5 * area2;
	}
	if (samples.is_empty() || !(total_area > 0.0)) {
		return 0.0f;
	}
	samples.sort();
	const double limit = total_area * 0.99;
	double acc = 0.0;
	for (const Sample &s : samples) {
		acc += s.area;
		if (acc >= limit) {
			return s.sigma;
		}
	}
	return samples[samples.size() - 1].sigma;
}

/* REGISTRATION */

void FellowTextureStreaming::register_texture(RID p_texture, const String &p_path, uint64_t p_data_offset, int p_width, int p_height, Image::Format p_format, int p_levels, int p_resident_mip) {
	ERR_FAIL_COND(p_levels < 2 || p_levels > 16 || p_resident_mip < 0 || p_resident_mip >= p_levels);
	if (!RSG::texture_storage->fellow_texture_exists(p_texture)) {
		return; // freed before the command arrived
	}
	Texture *tex = nullptr;
	Texture **existing = textures.getptr(p_texture);
	if (existing) {
		tex = *existing; // reloaded: its new tail is resident
		resident_bytes -= tex->chain_size(tex->resident_mip);
		full_bytes -= tex->chain_size(0);
	} else {
		tex = memnew(Texture);
		textures.insert(p_texture, tex);
	}
	tex->rid = p_texture;
	tex->path = p_path;
	tex->data_offset = p_data_offset;
	tex->width = p_width;
	tex->height = p_height;
	tex->format = p_format;
	tex->levels = p_levels;
	for (int i = 0; i <= p_levels; i++) {
		int w, h;
		tex->mip_offsets[i] = Image::get_image_mipmap_offset_and_dimensions(p_width, p_height, p_format, i, w, h);
	}
	tex->tail_mip = p_resident_mip;
	tex->resident_mip = p_resident_mip;
	tex->target_mip = p_resident_mip;
	tex->state = STATE_NEW;
	tex->cycles_seen = 0;
	tex->cycle_lod = 1e30f;
	tex->lod = 1e30f;
	tex->cycle_full = false;
	resident_bytes += tex->chain_size(tex->resident_mip);
	full_bytes += tex->chain_size(0);
	targets_valid = false;
}

void FellowTextureStreaming::texture_freed(RID p_texture) {
	Texture **tp = textures.getptr(p_texture);
	if (tp == nullptr) {
		return;
	}
	Texture *tex = *tp;
	if (tex->job) {
		tex->job->texture = RID(); // its result is freed when it completes
	}
	resident_bytes -= tex->chain_size(tex->resident_mip);
	full_bytes -= tex->chain_size(0);
	memdelete(tex);
	textures.erase(p_texture);
	targets_valid = false;
}

/* EVALUATION */

void FellowTextureStreaming::add_camera(const Camera &p_camera) {
	cameras.push_back(p_camera);
}

void FellowTextureStreaming::begin_frame() {
	eval_cameras = cameras;
	cameras.clear();
}

void FellowTextureStreaming::reference_material(RID p_material, float p_pixels_per_unit, float p_scale, float p_density, float p_mip_bias) {
	RID mat = p_material;
	for (int depth = 0; depth < 4 && mat.is_valid(); depth++) {
		const uint32_t version = RSG::material_storage->material_get_fellow_version(mat);
		if (version == UINT32_MAX) {
			return; // freed
		}
		MaterialInfo *mi = materials.getptr(mat);
		if (mi == nullptr || mi->version != version) {
			MaterialInfo info;
			if (!RSG::material_storage->material_get_fellow_stream_info(mat, info)) {
				return;
			}
			info.version = version;
			materials[mat] = info;
			mi = materials.getptr(mat);
		}
		for (const RID &t : mi->textures) {
			Texture **tp = textures.getptr(t);
			if (tp == nullptr) {
				continue;
			}
			Texture *tex = *tp;
			if (!mi->known_uv || (!mi->triplanar && !(p_density > 0.0f))) {
				tex->cycle_full = true; // a shader whose UVs are unknown: full resolution
				continue;
			}
			// Texels per world unit at mip 0: the texture's size times the UV units per world unit. Triplanar UVs are
			// the position times uv1_scale (local or world: the finer of the two).
			const float density = mi->triplanar ? 1.0f : p_density;
			const float scale = mi->triplanar ? MIN(p_scale, 1.0f) : p_scale;
			const float texels = float(MAX(tex->width, tex->height)) * density * mi->uv_scale / MAX(scale, 1e-6f);
			// Anisotropic filtering samples along the footprint's minor axis: off the view axis up to a level finer.
			const float lod = std::log2(MAX(texels, 1e-12f) / MAX(p_pixels_per_unit, 1e-12f)) + p_mip_bias - (mi->anisotropic ? 1.0f : 0.0f);
			tex->cycle_lod = MIN(tex->cycle_lod, lod);
		}
		mat = mi->next_pass;
	}
}

void FellowTextureStreaming::end_cycle() {
	cycles++;
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	const uint64_t unreferenced_hold = uint64_t(MAX(0.0f, _setting_float("rendering/fellow/streaming/texture_unreferenced_hold_s", 5.0f)) * 1000000.0f);
	for (KeyValue<RID, Texture *> &E : textures) {
		Texture *tex = E.value;
		tex->cycles_seen++;
		const bool referenced = tex->cycle_full || tex->cycle_lod < 1e29f;
		if (tex->state != STATE_FULL) {
			if (referenced) {
				tex->state = STATE_STREAMED;
				tex->lod = tex->cycle_full ? -100.0f : tex->cycle_lod;
				tex->unreferenced_since_usec = 0;
			} else if (tex->state == STATE_NEW) {
				if (tex->cycles_seen >= 2) {
					tex->state = STATE_FULL; // nothing in 3D uses it: 2D, sky, decals, ...
				}
			} else {
				// No longer referenced (hidden, or out of every scenario a camera sees): keep it, then release it.
				if (tex->unreferenced_since_usec == 0) {
					tex->unreferenced_since_usec = now;
				} else if (now - tex->unreferenced_since_usec >= unreferenced_hold) {
					tex->lod = 1e30f;
				}
			}
		}
		tex->cycle_lod = 1e30f;
		tex->cycle_full = false;
	}
	// Forget materials that no longer exist (their RIDs are never reused).
	if ((cycles & 63) == 0) {
		LocalVector<RID> dead;
		for (const KeyValue<RID, MaterialInfo> &E : materials) {
			if (RSG::material_storage->material_get_fellow_version(E.key) == UINT32_MAX) {
				dead.push_back(E.key);
			}
		}
		for (const RID &r : dead) {
			materials.erase(r);
		}
	}
	targets_valid = false;
}

void FellowTextureStreaming::_compute_targets() {
	const int64_t budget = int64_t(MAX(0, _setting_int("rendering/fellow/streaming/texture_budget_mb", 0))) * 1024 * 1024;
	// Pass 1: the targets without the budget's bias.
	auto target_at = [](const Texture *p_tex, float p_bias) -> int {
		switch (p_tex->state) {
			case STATE_FULL:
				return 0;
			case STATE_NEW:
				return p_tex->tail_mip;
			default:
				break;
		}
		if (p_tex->lod >= 1e29f) {
			return p_tex->tail_mip;
		}
		const float l = std::floor(p_tex->lod + p_bias);
		if (l <= 0.0f) {
			return 0;
		}
		return MIN(int(l), p_tex->tail_mip);
	};
	int64_t wanted = 0;
	for (const KeyValue<RID, Texture *> &E : textures) {
		wanted += E.value->chain_size(target_at(E.value, 0.0f));
	}
	wanted_bytes = wanted;
	float bias = 0.0f;
	int64_t total = wanted;
	if (budget > 0 && wanted > budget) {
		for (bias = 0.5f; bias <= 16.0f; bias += 0.5f) {
			total = 0;
			for (const KeyValue<RID, Texture *> &E : textures) {
				total += E.value->chain_size(target_at(E.value, bias));
			}
			if (total <= budget) {
				break;
			}
		}
		bias = MIN(bias, 16.0f);
	}
	budget_bias = bias;
	target_bytes = total;
	for (KeyValue<RID, Texture *> &E : textures) {
		E.value->target_mip = target_at(E.value, bias);
	}
	targets_valid = true;
}

/* JOBS */

void FellowTextureStreaming::_job_run(void *p_job) {
	Job *job = static_cast<Job *>(p_job);
	Ref<Image> image;
	{
		Ref<FileAccess> f = FileAccess::open(job->path, FileAccess::READ);
		if (f.is_valid()) {
			f->seek(job->file_offset);
			Vector<uint8_t> data;
			data.resize(job->size);
			const uint64_t got = f->get_buffer(data.ptrw(), job->size);
			if (got == uint64_t(job->size)) {
				image = Image::create_from_data(job->width, job->height, job->mipmaps, job->format, data);
			}
		}
	}
	if (image.is_valid() && !image->is_empty()) {
		RSG::texture_storage->texture_2d_initialize(job->new_texture, image);
		job->result.store(1, std::memory_order_release);
	} else {
		RSG::texture_storage->texture_2d_placeholder_initialize(job->new_texture);
		job->result.store(2, std::memory_order_release);
	}
}

void FellowTextureStreaming::_start_job(Texture *p_tex, int p_mip, bool p_downgrade) {
	Job *job = memnew(Job);
	job->texture = p_tex->rid;
	job->new_texture = RSG::texture_storage->texture_allocate();
	job->path = p_tex->path;
	job->file_offset = p_tex->data_offset + uint64_t(p_tex->mip_offsets[p_mip]);
	job->size = p_tex->chain_size(p_mip);
	Image::get_image_mipmap_offset_and_dimensions(p_tex->width, p_tex->height, p_tex->format, p_mip, job->width, job->height);
	job->format = p_tex->format;
	job->mipmaps = p_mip < p_tex->levels - 1;
	job->mip = p_mip;
	job->downgrade = p_downgrade;
	p_tex->job = job;
	jobs.push_back(job);
	job->task = WorkerThreadPool::get_singleton()->add_native_task(&FellowTextureStreaming::_job_run, job, false, "FellowEngine texture streaming");
}

void FellowTextureStreaming::_finish_job(Texture *p_tex, Job *p_job) {
	const int result = p_job->result.load(std::memory_order_acquire);
	if (p_tex != nullptr && result == 1) {
		RSG::texture_storage->texture_2d_fellow_stream_swap(p_tex->rid, p_job->new_texture);
		resident_bytes += p_tex->chain_size(p_job->mip) - p_tex->chain_size(p_tex->resident_mip);
		p_tex->resident_mip = p_job->mip;
		bytes_read += uint64_t(p_job->size);
		if (p_job->downgrade) {
			downgrades++;
		} else {
			loads++;
		}
	} else {
		RSG::texture_storage->texture_free(p_job->new_texture);
		if (result == 2) {
			failures++;
			if (p_tex) {
				ERR_PRINT_ONCE(vformat("FellowEngine texture streaming: could not read '%s'; the texture stays at its resident mips.", p_tex->path));
				p_tex->state = STATE_FULL;
				p_tex->tail_mip = p_tex->resident_mip; // nothing more to load
				p_tex->target_mip = p_tex->resident_mip;
			}
		}
	}
	if (p_tex) {
		p_tex->job = nullptr;
	}
}

void FellowTextureStreaming::_schedule() {
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	const int max_jobs = CLAMP(_setting_int("rendering/fellow/streaming/texture_max_jobs", 4), 1, 64);
	const int64_t upload_limit = int64_t(MAX(1, _setting_int("rendering/fellow/streaming/texture_upload_mb_per_frame", 64))) * 1024 * 1024;
	const int64_t budget = int64_t(MAX(0, _setting_int("rendering/fellow/streaming/texture_budget_mb", 0))) * 1024 * 1024;
	const uint64_t hold = uint64_t(MAX(0.0f, _setting_float("rendering/fellow/streaming/texture_release_hold_s", 2.0f)) * 1000000.0f);

	struct Candidate {
		Texture *tex;
		float key;
		bool operator<(const Candidate &p_o) const { return key < p_o.key; }
	};
	LocalVector<Candidate> up;
	LocalVector<Candidate> down;
	int64_t inflight_growth = 0;
	for (KeyValue<RID, Texture *> &E : textures) {
		Texture *tex = E.value;
		if (tex->job) {
			inflight_growth += tex->chain_size(tex->job->mip) - tex->chain_size(tex->resident_mip);
			continue;
		}
		if (tex->target_mip < tex->resident_mip) {
			tex->coarser_since_usec = 0;
			// The most needed first: the finest target, then the largest step.
			up.push_back({ tex, float(tex->target_mip) * 64.0f - float(tex->resident_mip - tex->target_mip) });
		} else if (tex->target_mip > tex->resident_mip) {
			if (tex->coarser_since_usec == 0) {
				tex->coarser_since_usec = now;
			}
			// The largest saving first.
			down.push_back({ tex, -float(tex->chain_size(tex->resident_mip) - tex->chain_size(tex->target_mip)) });
		} else {
			tex->coarser_since_usec = 0;
		}
	}
	int inflight = int(jobs.size());
	int64_t started = 0;
	const bool pressure = budget > 0 && resident_bytes + inflight_growth > budget;
	if (!down.is_empty()) {
		down.sort();
		for (const Candidate &c : down) {
			if (inflight >= max_jobs || started >= upload_limit) {
				break;
			}
			if (!pressure && now - c.tex->coarser_since_usec < hold) {
				continue;
			}
			_start_job(c.tex, c.tex->target_mip, true);
			inflight_growth += c.tex->chain_size(c.tex->target_mip) - c.tex->chain_size(c.tex->resident_mip);
			started += c.tex->chain_size(c.tex->target_mip);
			inflight++;
		}
	}
	if (!up.is_empty()) {
		up.sort();
		for (const Candidate &c : up) {
			if (inflight >= max_jobs || (started > 0 && started + c.tex->chain_size(c.tex->target_mip) > upload_limit)) {
				break;
			}
			const int64_t growth = c.tex->chain_size(c.tex->target_mip) - c.tex->chain_size(c.tex->resident_mip);
			if (budget > 0 && resident_bytes + inflight_growth + growth > budget) {
				continue; // waits for the releases
			}
			_start_job(c.tex, c.tex->target_mip, false);
			inflight_growth += growth;
			started += c.tex->chain_size(c.tex->target_mip);
			inflight++;
		}
	}
}

void FellowTextureStreaming::update() {
	// Finished jobs: swap them in.
	for (uint32_t i = 0; i < jobs.size();) {
		Job *job = jobs[i];
		if (job->result.load(std::memory_order_acquire) == 0 || !WorkerThreadPool::get_singleton()->is_task_completed(job->task)) {
			i++;
			continue;
		}
		WorkerThreadPool::get_singleton()->wait_for_task_completion(job->task);
		Texture *tex = nullptr;
		if (job->texture.is_valid()) {
			Texture **tp = textures.getptr(job->texture);
			tex = tp ? *tp : nullptr;
		}
		_finish_job(tex, job);
		memdelete(job);
		jobs.remove_at_unordered(i);
	}
	if (!textures.is_empty()) {
		if (!targets_valid) {
			_compute_targets();
		}
		_schedule();
	}
	_update_snapshot();
}

void FellowTextureStreaming::finish() {
	for (Job *job : jobs) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(job->task);
		RSG::texture_storage->texture_free(job->new_texture);
		memdelete(job);
	}
	jobs.clear();
	for (KeyValue<RID, Texture *> &E : textures) {
		memdelete(E.value);
	}
	textures.clear();
	materials.clear();
}

/* INFO */

void FellowTextureStreaming::_update_snapshot() {
	int pending = 0;
	int streamed = 0, full = 0, fresh = 0, at_full = 0, at_tail = 0;
	for (const KeyValue<RID, Texture *> &E : textures) {
		const Texture *tex = E.value;
		if (tex->target_mip != tex->resident_mip || tex->job) {
			pending++;
		}
		switch (tex->state) {
			case STATE_NEW:
				fresh++;
				break;
			case STATE_STREAMED:
				streamed++;
				break;
			case STATE_FULL:
				full++;
				break;
		}
		if (tex->resident_mip == 0) {
			at_full++;
		} else if (tex->resident_mip == tex->tail_mip) {
			at_tail++;
		}
	}
	Dictionary d;
	if (GLOBAL_GET_CACHED(bool, "rendering/fellow/streaming/texture_info_details")) {
		// Per texture (tooling): path, size, levels, resident and target level, the estimated level, state, bytes.
		static const char *state_names[3] = { "new", "streamed", "full" };
		Array list;
		for (const KeyValue<RID, Texture *> &E : textures) {
			const Texture *tex = E.value;
			Dictionary t;
			t["path"] = tex->path;
			t["size"] = Vector2i(tex->width, tex->height);
			t["levels"] = tex->levels;
			t["tail"] = tex->tail_mip;
			t["resident"] = tex->resident_mip;
			t["target"] = tex->target_mip;
			t["lod"] = tex->lod < 1e29f ? Variant(tex->lod) : Variant();
			t["state"] = state_names[tex->state];
			t["bytes"] = tex->chain_size(tex->resident_mip);
			list.push_back(t);
		}
		d["list"] = list;
	}
	d["textures"] = textures.size();
	d["streamed"] = streamed;
	d["pinned_full"] = full;
	d["new"] = fresh;
	d["resident_at_full"] = at_full;
	d["resident_at_tail"] = at_tail;
	peak_resident_bytes = MAX(peak_resident_bytes, resident_bytes);
	max_pending = MAX(max_pending, pending);
	d["resident_bytes"] = resident_bytes;
	d["peak_resident_bytes"] = peak_resident_bytes;
	d["max_pending"] = max_pending;
	d["target_bytes"] = target_bytes;
	d["wanted_bytes"] = wanted_bytes;
	d["full_bytes"] = full_bytes;
	d["budget_bias"] = budget_bias;
	d["pending"] = pending;
	d["jobs"] = jobs.size();
	d["cycles"] = cycles;
	d["loads"] = loads;
	d["downgrades"] = downgrades;
	d["failures"] = failures;
	d["bytes_read"] = bytes_read;
	d["eval_usec"] = eval_usec;
	d["eval_references"] = eval_references;
	d["materials"] = materials.size();
	// Settled: every texture at its target after at least two cycles, nothing in flight.
	d["settled"] = cycles >= 2 && pending == 0 && fresh == 0;
	MutexLock lock(info_mutex);
	info_snapshot = d;
}

Dictionary FellowTextureStreaming::get_info() const {
	MutexLock lock(info_mutex);
	return info_snapshot.duplicate();
}
