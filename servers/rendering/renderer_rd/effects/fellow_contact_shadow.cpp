/**************************************************************************/
/*  fellow_contact_shadow.cpp                                             */
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


#include "fellow_contact_shadow.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

FellowContactShadow::FellowContactShadow() {
	Vector<String> modes;
	modes.push_back("\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	pipeline = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, 0));
	Vector<uint8_t> zeros;
	zeros.resize(sizeof(UBO));
	zeros.fill(0);
	ubo = RD::get_singleton()->uniform_buffer_create(sizeof(UBO), zeros);
}

FellowContactShadow::~FellowContactShadow() {
	RD::get_singleton()->free_rid(ubo);
	shader.version_free(shader_version);
}

static void _fellow_cs_store_matrix(const Projection &p_m, float *r_dst) {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			r_dst[i * 4 + j] = p_m.columns[i][j];
		}
	}
}

void FellowContactShadow::process(const Params &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID nearest = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	Projection correction;
	correction.set_depth_correction(true, true, false);
	const Projection proj = correction * p_params.projection;
	UBO u;
	memset(&u, 0, sizeof(UBO));
	_fellow_cs_store_matrix(proj.inverse(), u.inv_projection);
	_fellow_cs_store_matrix(proj, u.projection);
	const Vector3 l = p_params.light_direction_view.normalized();
	u.light_direction[0] = l.x;
	u.light_direction[1] = l.y;
	u.light_direction[2] = l.z;
	u.light_direction[3] = MAX(p_params.length, 0.001f);
	u.size[0] = p_params.size.x;
	u.size[1] = p_params.size.y;
	u.frame = p_params.frame;
	u.steps = CLAMP(p_params.steps, 2, 64);
	u.thickness = MAX(p_params.thickness, 0.001f);
	RD::get_singleton()->buffer_update(ubo, 0, sizeof(UBO), &u);

	RD::get_singleton()->draw_command_begin_label("FellowEngine Contact Shadows");
	RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
	RID shader_rd = shader.version_get_shader(shader_version, 0);
	RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipeline);
	RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, ubo);
	RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest, p_params.depth }));
	RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 2, p_params.dest);
	RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_depth, u_dest), 0);
	RD::get_singleton()->compute_list_dispatch_threads(list, p_params.size.x, p_params.size.y, 1);
	RD::get_singleton()->compute_list_end();
	RD::get_singleton()->draw_command_end_label();
}
