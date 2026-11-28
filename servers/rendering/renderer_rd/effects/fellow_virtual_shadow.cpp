/**************************************************************************/
/*  fellow_virtual_shadow.cpp                                             */
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

#include "fellow_virtual_shadow.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

static_assert(sizeof(FellowVirtualShadow::Header) == 352, "FellowVSMHeader's std430 layout");

FellowVirtualShadow::FellowVirtualShadow() {
}

FellowVirtualShadow::~FellowVirtualShadow() {
	RD *rd = RD::get_singleton();
	if (empty_framebuffer.is_valid() && rd->framebuffer_is_valid(empty_framebuffer)) {
		rd->free_rid(empty_framebuffer);
	}
	for (RID *rid : { &buffer, &request_buffer, &clear_buffer, &dummy_pool }) {
		if (rid->is_valid()) {
			rd->free_rid(*rid);
		}
	}
	if (compiled) {
		shader.version_free(shader_version);
	}
}

void FellowVirtualShadow::_compile() {
	if (compiled) {
		return;
	}
	Vector<String> modes;
	modes.push_back("\n#define MODE_CLEAR\n");
	modes.push_back("\n#define MODE_MARK\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < 2; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
	compiled = true;
}

RID FellowVirtualShadow::get_buffer() {
	if (buffer.is_null()) {
		Vector<uint8_t> init;
		init.resize(BUFFER_SIZE);
		init.fill(0);
		buffer = RD::get_singleton()->storage_buffer_create(BUFFER_SIZE, init);
		RD::get_singleton()->set_resource_name(buffer, "FellowEngine VSM Table");
	}
	return buffer;
}

RID FellowVirtualShadow::get_dummy_pool() {
	if (dummy_pool.is_null()) {
		RD::TextureFormat tf;
		tf.format = RD::DATA_FORMAT_R32_UINT;
		tf.width = 1;
		tf.height = 1;
		tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT;
		Vector<uint8_t> zero;
		zero.resize(4);
		zero.fill(0);
		Vector<Vector<uint8_t>> data;
		data.push_back(zero);
		dummy_pool = RD::get_singleton()->texture_create(tf, RD::TextureView(), data);
		RD::get_singleton()->set_resource_name(dummy_pool, "FellowEngine VSM Pool Placeholder");
	}
	return dummy_pool;
}

RID FellowVirtualShadow::get_empty_framebuffer() {
	if (empty_framebuffer.is_null() || !RD::get_singleton()->framebuffer_is_valid(empty_framebuffer)) {
		const int side = 16 * PAGE; // Fellow::VSM_REGION pages
		empty_framebuffer = RD::get_singleton()->framebuffer_create_empty(Size2i(side, side));
	}
	return empty_framebuffer;
}

void FellowVirtualShadow::upload(const Header &p_header, const uint32_t *p_table, uint32_t p_levels) {
	RID b = get_buffer();
	RD::get_singleton()->buffer_update(b, 0, sizeof(Header), &p_header);
	const uint32_t entries = MIN(p_levels, uint32_t(MAX_LEVELS)) * WINDOW * WINDOW;
	if (entries > 0) {
		RD::get_singleton()->buffer_update(b, sizeof(Header), entries * 4, p_table);
	}
}

void FellowVirtualShadow::clear_pages(RID p_pool, uint32_t p_pool_pages, const LocalVector<uint32_t> &p_pages) {
	if (p_pages.is_empty()) {
		return;
	}
	_compile();
	RD *rd = RD::get_singleton();
	if (clear_capacity < p_pages.size()) {
		if (clear_buffer.is_valid()) {
			rd->free_rid(clear_buffer);
		}
		clear_capacity = MAX(1024u, Math::nearest_power_of_2_templated(p_pages.size()));
		clear_buffer = rd->storage_buffer_create(clear_capacity * 4);
	}
	rd->buffer_update(clear_buffer, 0, p_pages.size() * 4, p_pages.ptr());

	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	ClearPush push;
	memset(&push, 0, sizeof(push));
	push.pool_pages = p_pool_pages;
	push.count = p_pages.size();
	rd->draw_command_begin_label("FellowEngine VSM Clear Pages");
	RID shader_rd = shader.version_get_shader(shader_version, 0);
	RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, pipelines[0]);
	RD::Uniform u_pool(RD::UNIFORM_TYPE_IMAGE, 0, p_pool);
	RD::Uniform u_list(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, clear_buffer);
	rd->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_pool, u_list), 0);
	rd->compute_list_set_push_constant(list, &push, sizeof(push));
	rd->compute_list_dispatch(list, PAGE / 8, PAGE / 8, p_pages.size());
	rd->compute_list_end();
	rd->draw_command_end_label();
}

void FellowVirtualShadow::mark(RID p_depth, const Size2i &p_size, const Projection &p_projection, const Callable &p_callback) {
	_compile();
	RD *rd = RD::get_singleton();
	if (request_buffer.is_null()) {
		request_buffer = rd->storage_buffer_create(REQUEST_WORDS * 4);
	}
	rd->buffer_clear(request_buffer, 0, REQUEST_WORDS * 4);

	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID nearest = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	// As the contact shadows (decision 0107) reconstruct the view's positions.
	Projection correction;
	correction.set_depth_correction(true, true, false);
	const Projection inv = (correction * p_projection).inverse();
	MarkPush push;
	memset(&push, 0, sizeof(push));
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 4; r++) {
			push.inv_projection[c * 4 + r] = inv.columns[c][r];
		}
	}
	push.size[0] = p_size.x;
	push.size[1] = p_size.y;

	rd->draw_command_begin_label("FellowEngine VSM Mark Pages");
	RID shader_rd = shader.version_get_shader(shader_version, 1);
	RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, pipelines[1]);
	RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest, p_depth }));
	RD::Uniform u_header(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, get_buffer());
	RD::Uniform u_requests(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, request_buffer);
	rd->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_depth, u_header, u_requests), 0);
	rd->compute_list_set_push_constant(list, &push, sizeof(push));
	rd->compute_list_dispatch_threads(list, p_size.x, p_size.y, 1);
	rd->compute_list_end();
	rd->draw_command_end_label();

	rd->buffer_get_data_async(request_buffer, p_callback, 0, REQUEST_WORDS * 4);
}
