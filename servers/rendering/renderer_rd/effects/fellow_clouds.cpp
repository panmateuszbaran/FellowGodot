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

#include "fellow_camera_relative.h"
#include "fellow_weather.h"

#include "core/object/callable_mp.h"
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
	modes.push_back("\n#define MODE_SHADOW\n");
	modes.push_back("\n#define MODE_PROBE\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
	Vector<uint8_t> zeros;
	zeros.resize(sizeof(UBO));
	zeros.fill(0);
	ubo = RD::get_singleton()->uniform_buffer_create(sizeof(UBO), zeros);
	shadow_ubo = RD::get_singleton()->uniform_buffer_create(sizeof(UBO), zeros);
	probe_ubo = RD::get_singleton()->uniform_buffer_create(sizeof(UBO), zeros);
	probe_buffer = RD::get_singleton()->storage_buffer_create(16);
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
	RID rids[] = { ubo, shadow_ubo, shadow_maps[0], shadow_maps[1], probe_ubo, probe_buffer, shape_noise, detail_noise, white };
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
	// Milestone 103 (decision 0211): the view's directions relative to the camera, the reprojection and the flash
	// taken there on the CPU; the camera on top of the planet, its x and z the clouds' field position (km).
	_fellow_clouds_store_matrix(fellow_inverse_view_projection_relative(p_params.view_projection, p_params.camera_position), u.inv_view_projection);
	_fellow_clouds_store_matrix(fellow_view_projection_relative(p_params.prev_view_projection, p_params.camera_position), u.prev_view_projection);
	u.camera_position[0] = p_params.camera_position.x;
	u.camera_position[1] = p_params.camera_position.y;
	u.camera_position[2] = p_params.camera_position.z;
	u.camera_position[3] = p_params.ground_height;
	u.field[0] = p_params.camera_position.x * 0.001;
	u.field[1] = p_params.camera_position.z * 0.001;
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
	if (p_params.flash) {
		u.flash[0] = p_params.flash_position.x - p_params.camera_position.x;
		u.flash[1] = p_params.flash_position.y - p_params.camera_position.y;
		u.flash[2] = p_params.flash_position.z - p_params.camera_position.z;
		u.flash[3] = MAX(p_params.flash_radius, 1.0f);
		u.flash_color[0] = p_params.flash_color.r;
		u.flash_color[1] = p_params.flash_color.g;
		u.flash_color[2] = p_params.flash_color.b;
		u.flash_color[3] = 1.0f;
	}
	u.precip[0] = CLAMP(p_params.precipitation, 0.0f, 1.0f);
	u.precip[1] = p_params.snow ? 1.0f : 0.0f;
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

RID FellowClouds::process_shadow(const ShadowParams &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL_V(uniform_set_cache, RID());
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL_V(material_storage, RID());
	RID linear_repeat = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED);
	// Centred on the camera, in whole texels (the map does not swim as the camera moves).
	const float texel = SHADOW_EXTENT / float(SHADOW_SIZE);
	const Vector2 shadow_origin = Vector2(Math::floor(p_params.camera_position.x / texel + 0.5f) * texel, Math::floor(p_params.camera_position.z / texel + 0.5f) * texel);
	if (shadow_maps[0].is_null()) {
		for (int i = 0; i < 2; i++) {
			shadow_maps[i] = _fellow_clouds_texture(RD::TEXTURE_TYPE_2D, RD::DATA_FORMAT_R16_SFLOAT, SHADOW_SIZE, SHADOW_SIZE, 1);
			RD::get_singleton()->texture_clear(shadow_maps[i], Color(1, 1, 1, 1), 0, 1, 0, 1); // the first frame in sunlight
			shadow_origins[i] = shadow_origin;
		}
	}
	const uint32_t write = shadow_parity;
	shadow_parity ^= 1;
	shadow_origins[write] = shadow_origin;
	shadow_read_origin = shadow_origins[write ^ 1];

	UBO u;
	memset(&u, 0, sizeof(UBO));
	const Vector3 sun = p_params.sun_direction.normalized();
	u.sun_direction[0] = sun.x;
	u.sun_direction[1] = sun.y;
	u.sun_direction[2] = sun.z;
	u.sun_direction[3] = p_params.time;
	u.layer[0] = MAX(p_params.bottom_altitude, 0.0f);
	u.layer[1] = MAX(p_params.thickness, 0.05f);
	u.layer[2] = CLAMP(p_params.coverage, 0.0f, 1.0f);
	u.layer[3] = MAX(p_params.density, 0.0f);
	u.shape[0] = p_params.shape_scale;
	u.shape[1] = p_params.detail_scale;
	u.shape[2] = p_params.wind.x;
	u.shape[3] = p_params.wind.y;
	u.sizes[0] = SHADOW_SIZE;
	u.sizes[1] = SHADOW_SIZE;
	u.shadow[0] = shadow_origin.x;
	u.shadow[1] = shadow_origin.y;
	u.shadow[2] = SHADOW_EXTENT;
	u.shadow[3] = p_params.ground_height;
	u.field[0] = shadow_origin.x * 0.001;
	u.field[1] = shadow_origin.y * 0.001;
	RD::get_singleton()->buffer_update(shadow_ubo, 0, sizeof(UBO), &u);

	RD::get_singleton()->draw_command_begin_label("FellowEngine Cloud Shadows");
	RID shader_rd = shader.version_get_shader(shader_version, MODE_SHADOW);
	RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[MODE_SHADOW]);
	RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, shadow_ubo);
	RD::Uniform u_shape(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ linear_repeat, shape_noise }));
	RD::Uniform u_detail(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ linear_repeat, detail_noise }));
	RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_shape, u_detail), 0);
	RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 0, shadow_maps[write]);
	RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 1, u_dest), 1);
	RD::get_singleton()->compute_list_dispatch_threads(list, SHADOW_SIZE, SHADOW_SIZE, 1);
	RD::get_singleton()->compute_list_end();
	RD::get_singleton()->draw_command_end_label();
	return shadow_maps[write ^ 1];
}

void FellowClouds::_probe_readback(const Vector<uint8_t> &p_data) {
	if (p_data.size() < 16) {
		return;
	}
	const float *v = reinterpret_cast<const float *>(p_data.ptr());
	if (FellowWeather *weather = FellowWeather::get_singleton_if_exists()) {
		weather->set_rain_here(CLAMP(v[0], 0.0f, 1.0f));
	}
}

void FellowClouds::process_probe(const ShadowParams &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID linear_repeat = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED);
	UBO u;
	memset(&u, 0, sizeof(UBO));
	u.sun_direction[3] = p_params.time;
	u.layer[0] = MAX(p_params.bottom_altitude, 0.0f);
	u.layer[1] = MAX(p_params.thickness, 0.05f);
	u.layer[2] = CLAMP(p_params.coverage, 0.0f, 1.0f);
	u.layer[3] = MAX(p_params.density, 0.0f);
	u.shape[0] = p_params.shape_scale;
	u.shape[1] = p_params.detail_scale;
	u.shape[2] = p_params.wind.x;
	u.shape[3] = p_params.wind.y;
	u.shadow[0] = p_params.camera_position.x;
	u.shadow[1] = p_params.camera_position.z;
	u.field[0] = p_params.camera_position.x * 0.001;
	u.field[1] = p_params.camera_position.z * 0.001;
	RD::get_singleton()->buffer_update(probe_ubo, 0, sizeof(UBO), &u);

	RD::get_singleton()->draw_command_begin_label("FellowEngine Showers Probe");
	RID shader_rd = shader.version_get_shader(shader_version, MODE_PROBE);
	RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipelines[MODE_PROBE]);
	RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, probe_ubo);
	RD::Uniform u_shape(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ linear_repeat, shape_noise }));
	RD::Uniform u_detail(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ linear_repeat, detail_noise }));
	RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_shape, u_detail), 0);
	RD::Uniform u_dest(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, probe_buffer);
	RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 1, u_dest), 1);
	RD::get_singleton()->compute_list_dispatch(list, 1, 1, 1);
	RD::get_singleton()->compute_list_end();
	RD::get_singleton()->draw_command_end_label();
	RD::get_singleton()->buffer_get_data_async(probe_buffer, callable_mp_static(&FellowClouds::_probe_readback), 0, 16);
}
