/**************************************************************************/
/*  fellow_clouds.cpp                                                     */
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


#include "fellow_clouds.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

static RID _fellow_clouds_texture(RD::TextureType p_type, RD::DataFormat p_format, int p_w, int p_h, int p_d) {
	RD::TextureFormat tf;
	tf.texture_type = p_type;
	tf.format = p_format;
	tf.width = p_w;
	tf.height = p_h;
	tf.depth = p_d;
	tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
	return RD::get_singleton()->texture_create(tf, RD::TextureView());
}

static void _fellow_clouds_store_matrix(const Projection &p_m, float *r_dst) {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			r_dst[i * 4 + j] = p_m.columns[i][j];
		}
	}
}

FellowClouds::FellowClouds() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_NOISE_SHAPE\n");
	modes.push_back("\n#define MODE_NOISE_DETAIL\n");
	modes.push_back("\n#define MODE_TRACE\n");
	modes.push_back("\n#define MODE_TEMPORAL\n");
	modes.push_back("\n#define MODE_COMPOSITE\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
	Vector<uint8_t> zeros;
	zeros.resize(sizeof(UBO));
	zeros.fill(0);
	ubo = RD::get_singleton()->uniform_buffer_create(sizeof(UBO), zeros);
	shape_noise = _fellow_clouds_texture(RD::TEXTURE_TYPE_3D, RD::DATA_FORMAT_R8G8B8A8_UNORM, 128, 128, 128);
	detail_noise = _fellow_clouds_texture(RD::TEXTURE_TYPE_3D, RD::DATA_FORMAT_R8G8B8A8_UNORM, 32, 32, 32);
	white = _fellow_clouds_texture(RD::TEXTURE_TYPE_2D, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, 1, 1, 1);
	RD::get_singleton()->texture_clear(white, Color(1, 1, 1, 1), 0, 1, 0, 1);

	// The noise textures, once.
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, ubo);
	for (int m = MODE_NOISE_SHAPE; m <= MODE_NOISE_DETAIL; m++) {
		RID tex = m == MODE_NOISE_SHAPE ? shape_noise : detail_noise;
		int n = m == MODE_NOISE_SHAPE ? 128 : 32;
		RID shader_rd = shader.version_get_shader(shader_version, m);
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[m]);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params), 0);
		RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 0, tex);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 1, u_dest), 1);
		RD::get_singleton()->compute_list_dispatch_threads(list, n, n, n);
		RD::get_singleton()->compute_list_end();
	}
}

void FellowClouds::_free_targets() {
	RID *rids[] = { &trace, &history[0], &history[1] };
	for (RID *r : rids) {
		if (r->is_valid()) {
			RD::get_singleton()->free_rid(*r);
			*r = RID();
		}
	}
	trace_size = Size2i();
	interleave_primed = false;
}

FellowClouds::~FellowClouds() {
	_free_targets();
	RID rids[] = { ubo, shape_noise, detail_noise, white };
	for (RID r : rids) {
		if (r.is_valid()) {
			RD::get_singleton()->free_rid(r);
		}
	}
	shader.version_free(shader_version);
}

void FellowClouds::process(const Params &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID linear_repeat = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED);
	RID linear = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID nearest = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	const Size2i ts((p_params.size.x + 1) / 2, (p_params.size.y + 1) / 2);
	bool history_valid = p_params.history_valid;
	if (ts != trace_size) {
		_free_targets();
		trace_size = ts;
		trace = _fellow_clouds_texture(RD::TEXTURE_TYPE_2D, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, ts.x, ts.y, 1);
		history[0] = _fellow_clouds_texture(RD::TEXTURE_TYPE_2D, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, ts.x, ts.y, 1);
		history[1] = _fellow_clouds_texture(RD::TEXTURE_TYPE_2D, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, ts.x, ts.y, 1);
		history_valid = false;
	}

	UBO u;
	memset(&u, 0, sizeof(UBO));
	_fellow_clouds_store_matrix(p_params.view_projection.inverse(), u.inv_view_projection);
	_fellow_clouds_store_matrix(p_params.prev_view_projection, u.prev_view_projection);
	u.camera_position[0] = p_params.camera_position.x;
	u.camera_position[1] = p_params.camera_position.y;
	u.camera_position[2] = p_params.camera_position.z;
	u.camera_position[3] = p_params.ground_height;
	const Vector3 sun = p_params.sun_direction.normalized();
	u.sun_direction[0] = sun.x;
	u.sun_direction[1] = sun.y;
	u.sun_direction[2] = sun.z;
	u.sun_direction[3] = p_params.time;
	u.sun_color[0] = p_params.sun_illuminance.r;
	u.sun_color[1] = p_params.sun_illuminance.g;
	u.sun_color[2] = p_params.sun_illuminance.b;
	u.ambient_color[0] = p_params.ambient.r;
	u.ambient_color[1] = p_params.ambient.g;
	u.ambient_color[2] = p_params.ambient.b;
	u.ambient_color[3] = p_params.ground_albedo;
	u.layer[0] = MAX(p_params.bottom_altitude, 0.0f);
	u.layer[1] = MAX(p_params.thickness, 0.05f);
	u.layer[2] = CLAMP(p_params.coverage, 0.0f, 1.0f);
	u.layer[3] = MAX(p_params.density, 0.0f);
	u.shape[0] = p_params.shape_scale;
	u.shape[1] = p_params.detail_scale;
	u.shape[2] = p_params.wind.x;
	u.shape[3] = p_params.wind.y;
	u.misc[0] = MAX(p_params.max_distance, 1.0f);
	u.misc[1] = MAX(p_params.fade_distance, 1.0f);
	u.misc[2] = history_valid ? 0.85f : 0.0f;
	u.misc[3] = float(p_params.frame & 0xffff);
	u.sizes[0] = ts.x;
	u.sizes[1] = ts.y;
	u.sizes[2] = p_params.size.x;
	u.sizes[3] = p_params.size.y;
	const bool atmosphere = p_params.atmosphere_transmittance.is_valid() && p_params.atmosphere_sky_view.is_valid();
	u.flags[0] = atmosphere ? 1 : 0;
	// Interleaved (decision 0115): one pixel of every 2 x 2 block per frame, once the history (every pixel's trace) exists.
	const bool interleaved = p_params.interleaved && history_valid && interleave_primed;
	u.flags[1] = interleaved ? 1 : 0;
	u.flags[2] = int32_t(p_params.frame & 3);
	RD::get_singleton()->buffer_update(ubo, 0, sizeof(UBO), &u);
	RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, ubo);

	RD::get_singleton()->draw_command_begin_label("FellowEngine Clouds");
	{
		RID shader_rd = shader.version_get_shader(shader_version, MODE_TRACE);
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[MODE_TRACE]);
		RD::Uniform u_shape(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ linear_repeat, shape_noise }));
		RD::Uniform u_detail(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ linear_repeat, detail_noise }));
		RD::Uniform u_t(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, Vector<RID>({ linear, atmosphere ? p_params.atmosphere_transmittance : white }));
		RD::Uniform u_sv(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 4, Vector<RID>({ linear, atmosphere ? p_params.atmosphere_sky_view : white }));
		RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 5, Vector<RID>({ nearest, p_params.depth }));
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_shape, u_detail, u_t, u_sv, u_depth), 0);
		RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 0, trace);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 1, u_dest), 1);
		if (interleaved) {
			RD::get_singleton()->compute_list_dispatch_threads(list, (ts.x + 1) / 2, (ts.y + 1) / 2, 1);
		} else {
			RD::get_singleton()->compute_list_dispatch_threads(list, ts.x, ts.y, 1);
		}
		RD::get_singleton()->compute_list_end();
	}
	if (!interleaved) {
		interleave_primed = true; // this frame traced every pixel
	}
	RID hist_prev = history[parity];
	RID hist_out = history[parity ^ 1];
	parity ^= 1;
	{
		RID shader_rd = shader.version_get_shader(shader_version, MODE_TEMPORAL);
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[MODE_TEMPORAL]);
		RD::Uniform u_tr(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest, trace }));
		RD::Uniform u_h(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ linear, hist_prev }));
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_tr, u_h), 0);
		RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 0, hist_out);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 1, u_dest), 1);
		RD::get_singleton()->compute_list_dispatch_threads(list, ts.x, ts.y, 1);
		RD::get_singleton()->compute_list_end();
	}
	{
		RID shader_rd = shader.version_get_shader(shader_version, MODE_COMPOSITE);
		RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
		RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[MODE_COMPOSITE]);
		RD::Uniform u_c(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ linear, hist_out }));
		RD::Uniform u_d(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ nearest, p_params.depth }));
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_c, u_d), 0);
		RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 0, p_params.color);
		RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 1, u_dest), 1);
		RD::get_singleton()->compute_list_dispatch_threads(list, p_params.size.x, p_params.size.y, 1);
		RD::get_singleton()->compute_list_end();
	}
	RD::get_singleton()->draw_command_end_label();
}
