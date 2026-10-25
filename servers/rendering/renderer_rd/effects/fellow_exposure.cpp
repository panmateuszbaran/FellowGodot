/**************************************************************************/
/*  fellow_exposure.cpp                                                   */
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


#include "fellow_exposure.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

FellowExposure::FellowExposure() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_BUILD\n");
	modes.push_back("\n#define MODE_GRID_BLUR\n");
	modes.push_back("\n#define MODE_TILE_BLUR\n");
	modes.push_back("\n#define MODE_ADAPT\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
	Vector<uint8_t> zeros;
	zeros.resize(BINS * 4);
	zeros.fill(0);
	histogram_buffer = RD::get_singleton()->storage_buffer_create(BINS * 4, zeros);
}

FellowExposure::~FellowExposure() {
	_free_local();
	if (histogram_buffer.is_valid()) {
		RD::get_singleton()->free_rid(histogram_buffer);
	}
	shader.version_free(shader_version);
}

void FellowExposure::_free_local() {
	RID *textures[] = { &grid, &grid_blurred, &tile_mean, &tile_tmp, &tile_blurred };
	for (RID *t : textures) {
		if (t->is_valid()) {
			RD::get_singleton()->free_rid(*t);
			*t = RID();
		}
	}
	cells = Size2i();
}

Vector2 FellowExposure::get_grid_scale(const Size2i &p_size) const {
	if (cells.x <= 0 || cells.y <= 0) {
		return Vector2(1, 1);
	}
	return Vector2(float(p_size.x) / float(cells.x * TILE), float(p_size.y) / float(cells.y * TILE));
}

void FellowExposure::process(const Params &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID nearest = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	const Size2i new_cells((p_params.size.x + TILE - 1) / TILE, (p_params.size.y + TILE - 1) / TILE);
	// The grid's textures are made for the histogram alone too (its build pass binds them, without writing them).
	if ((p_params.local || p_params.histogram) && new_cells != cells) {
		_free_local();
		cells = new_cells;
		RD::TextureFormat tf;
		tf.texture_type = RD::TEXTURE_TYPE_3D;
		tf.format = RD::DATA_FORMAT_R16G16_SFLOAT;
		tf.width = cells.x;
		tf.height = cells.y;
		tf.depth = SLICES;
		tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		grid = RD::get_singleton()->texture_create(tf, RD::TextureView());
		grid_blurred = RD::get_singleton()->texture_create(tf, RD::TextureView());
		RD::TextureFormat tf2;
		tf2.texture_type = RD::TEXTURE_TYPE_2D;
		tf2.format = RD::DATA_FORMAT_R16_SFLOAT;
		tf2.width = cells.x;
		tf2.height = cells.y;
		tf2.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		tile_mean = RD::get_singleton()->texture_create(tf2, RD::TextureView());
		tile_tmp = RD::get_singleton()->texture_create(tf2, RD::TextureView());
		tile_blurred = RD::get_singleton()->texture_create(tf2, RD::TextureView());
	}
	const bool local = p_params.local && grid.is_valid();
	const bool hist = p_params.histogram && p_params.current.is_valid();
	if (!local && !hist) {
		return;
	}

	PushConstant push;
	memset(&push, 0, sizeof(PushConstant));
	push.size[0] = p_params.size.x;
	push.size[1] = p_params.size.y;
	push.cells[0] = new_cells.x;
	push.cells[1] = new_cells.y;
	push.luminance_multiplier = p_params.luminance_multiplier;
	push.low_percent = CLAMP(p_params.low_percent, 0.0f, 1.0f);
	push.high_percent = CLAMP(p_params.high_percent, push.low_percent, 1.0f);
	push.speed_up = MAX(p_params.speed_up, 0.0f);
	push.speed_down = MAX(p_params.speed_down, 0.0f);
	push.dt = MAX(p_params.dt, 0.0f);
	push.min_luminance = p_params.min_luminance;
	push.max_luminance = MAX(p_params.max_luminance, p_params.min_luminance);
	push.flags = (hist ? 1 : 0) | (local ? 2 : 0) | (p_params.immediate ? 4 : 0);
	const float sigma = MAX(0.5f, p_params.blur_size * float(new_cells.x) / 6.0f);
	push.blur_sigma = sigma;
	push.blur_radius = CLAMP(int(Math::ceil(3.0f * sigma)), 1, 64);

	RD::get_singleton()->draw_command_begin_label("FellowEngine Exposure");

	{
		RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, MODE_BUILD);
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_BUILD]);
		RD::Uniform u_source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest, p_params.source }));
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 0, u_source), 0);
		RD::Uniform u_hist(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, Vector<RID>({ histogram_buffer }));
		RD::Uniform u_grid(RD::UNIFORM_TYPE_IMAGE, 1, Vector<RID>({ grid }));
		RD::Uniform u_mean(RD::UNIFORM_TYPE_IMAGE, 2, Vector<RID>({ tile_mean }));
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 1, u_hist, u_grid, u_mean), 1);
		RD::get_singleton()->compute_list_set_push_constant(compute_list, &push, sizeof(PushConstant));
		RD::get_singleton()->compute_list_dispatch(compute_list, new_cells.x, new_cells.y, 1);
		RD::get_singleton()->compute_list_end();
	}

	if (local) {
		{
			RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
			RID shader_rd = shader.version_get_shader(shader_version, MODE_GRID_BLUR);
			RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_GRID_BLUR]);
			RD::Uniform u_in(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest, grid }));
			RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 0, u_in), 0);
			RD::Uniform u_out(RD::UNIFORM_TYPE_IMAGE, 0, Vector<RID>({ grid_blurred }));
			RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 1, u_out), 1);
			RD::get_singleton()->compute_list_set_push_constant(compute_list, &push, sizeof(PushConstant));
			RD::get_singleton()->compute_list_dispatch_threads(compute_list, new_cells.x, new_cells.y, SLICES);
			RD::get_singleton()->compute_list_end();
		}
		for (int axis = 0; axis < 2; axis++) {
			push.blur_axis = axis;
			RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
			RID shader_rd = shader.version_get_shader(shader_version, MODE_TILE_BLUR);
			RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_TILE_BLUR]);
			RD::Uniform u_in(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest, axis == 0 ? tile_mean : tile_tmp }));
			RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 0, u_in), 0);
			RD::Uniform u_out(RD::UNIFORM_TYPE_IMAGE, 0, Vector<RID>({ axis == 0 ? tile_tmp : tile_blurred }));
			RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 1, u_out), 1);
			RD::get_singleton()->compute_list_set_push_constant(compute_list, &push, sizeof(PushConstant));
			RD::get_singleton()->compute_list_dispatch_threads(compute_list, new_cells.x, new_cells.y, 1);
			RD::get_singleton()->compute_list_end();
		}
	}

	if (hist) {
		RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
		RID shader_rd = shader.version_get_shader(shader_version, MODE_ADAPT);
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_ADAPT]);
		RD::Uniform u_hist(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, Vector<RID>({ histogram_buffer }));
		RD::Uniform u_current(RD::UNIFORM_TYPE_IMAGE, 1, Vector<RID>({ p_params.current }));
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rd, 0, u_hist, u_current), 0);
		RD::get_singleton()->compute_list_set_push_constant(compute_list, &push, sizeof(PushConstant));
		RD::get_singleton()->compute_list_dispatch(compute_list, 1, 1, 1);
		RD::get_singleton()->compute_list_end();
	}

	RD::get_singleton()->draw_command_end_label();
}
