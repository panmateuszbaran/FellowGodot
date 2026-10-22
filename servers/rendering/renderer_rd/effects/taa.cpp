/**************************************************************************/
/*  taa.cpp                                                               */
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

#include "taa.h"

#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/renderer_compositor.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/renderer_rd/effects/copy_effects.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

TAA::TAA() {
	Vector<String> taa_modes;
	taa_modes.push_back("\n#define MODE_TAA_RESOLVE"); // Stock.
	taa_modes.push_back("\n#define MODE_TAA_RESOLVE\n#define FELLOW_TAA_V2\n"); // FellowEngine resolve v2.
	taa_modes.push_back("\n#define MODE_TAA_RESOLVE\n#define FELLOW_TAA_WRITE_VELOCITY\n"); // Stock resolve, ping-pong.
	taa_modes.push_back("\n#define MODE_TAA_RESOLVE\n#define FELLOW_TAA_V2\n#define FELLOW_TAA_WRITE_VELOCITY\n");
	for (int i = 0; i < 4; i++) {
		taa_modes.push_back(taa_modes[i] + "\n#define GROUP_SIZE 16\n#define FELLOW_TAA_STRIDED_LOAD\n");
	}
	for (int i = 0; i < 4; i++) {
		// Decision 0075: stock's 8 x 8 workgroups, the 10 x 10 tile loaded by all 64 invocations in turn (at most two
		// each) instead of four each by 25 of them.
		taa_modes.push_back(taa_modes[i] + "\n#define FELLOW_TAA_STRIDED_LOAD\n");
	}
	taa_shader.initialize(taa_modes, Fellow::taa_shared_velocity() ? "\n#define FELLOW_TAA_SHARED_VELOCITY\n" : "");
	shader_version = taa_shader.version_create();
	for (int i = 0; i < VARIANT_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(taa_shader.version_get_shader(shader_version, i));
	}
}

TAA::~TAA() {
	taa_shader.version_free(shader_version);
}

void TAA::resolve(RID p_frame, RID p_temp, RID p_depth, RID p_velocity, RID p_prev_velocity, RID p_history, Size2 p_resolution, float p_z_near, float p_z_far, uint32_t p_variant, RID p_velocity_out) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);

	RID shader = taa_shader.version_get_shader(shader_version, p_variant);
	ERR_FAIL_COND(shader.is_null());

	RID default_sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	float base_variance = 1.1f;
	float base_variance_min = 0.75f;
	float base_variance_max = 1.00f;
	float variance_scale = 1080.0f / p_resolution.height; // 1080p taken as baseline for calculation, as this is most commonly used resolution

	TAAResolvePushConstant push_constant;
	memset(&push_constant, 0, sizeof(TAAResolvePushConstant));
	push_constant.resolution_width = p_resolution.width;
	push_constant.resolution_height = p_resolution.height;
	push_constant.disocclusion_threshold = 2.5f; // If velocity changes by less than this amount of texels we can retain the accumulation buffer.
	push_constant.variance_dynamic = CLAMP(base_variance * variance_scale, base_variance_min, base_variance_max); // Variance dynamically scales based on resolution
	push_constant.fellow_gamma = Fellow::taa_v2_gamma();
	push_constant.fellow_feedback_min = Fellow::taa_v2_feedback_min();
	push_constant.fellow_feedback_max = Fellow::taa_v2_feedback_max();
	push_constant.fellow_antiflicker = Fellow::taa_v2_antiflicker();
	push_constant.fellow_velocity_dilation = Fellow::taa_v2_velocity_dilation() ? 1.0f : 0.0f;
	push_constant.fellow_motion_soften = Fellow::taa_v2_motion_soften();
	push_constant.fellow_soften_contrast = MAX(0.01f, Fellow::taa_v2_soften_contrast());

	RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[p_variant]);

	RD::Uniform u_frame_source(RD::UNIFORM_TYPE_IMAGE, 0, { p_frame });
	RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, { default_sampler, p_depth });
	RD::Uniform u_velocity(RD::UNIFORM_TYPE_IMAGE, 2, { p_velocity });
	RD::Uniform u_prev_velocity(RD::UNIFORM_TYPE_IMAGE, 3, { p_prev_velocity });
	RD::Uniform u_history(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 4, { default_sampler, p_history });
	RD::Uniform u_frame_dest(RD::UNIFORM_TYPE_IMAGE, 5, { p_temp });

	if (p_variant & VARIANT_FELLOW_WRITE_VELOCITY) {
		RD::Uniform u_velocity_out(RD::UNIFORM_TYPE_IMAGE, 6, { p_velocity_out });
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_frame_source, u_depth, u_velocity, u_prev_velocity, u_history, u_frame_dest, u_velocity_out), 0);
	} else {
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_frame_source, u_depth, u_velocity, u_prev_velocity, u_history, u_frame_dest), 0);
	}
	RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(TAAResolvePushConstant));
	RD::get_singleton()->compute_list_dispatch_threads(compute_list, p_resolution.width, p_resolution.height, 1);
	RD::get_singleton()->compute_list_end();
}

void TAA::process(Ref<RenderSceneBuffersRD> p_render_buffers, RD::DataFormat p_format, float p_z_near, float p_z_far, bool p_direct_output) {
	CopyEffects *copy_effects = CopyEffects::get_singleton();

	uint32_t variant = Fellow::taa_resolve_v2() ? VARIANT_FELLOW_V2 : 0;
	if (Fellow::taa_tile16()) {
		variant |= VARIANT_FELLOW_TILE16;
	} else if (Fellow::taa_strided_load()) {
		variant |= VARIANT_FELLOW_STRIDED8; // decision 0075
	}
	if (Fellow::taa_history_pingpong()) {
		process_pingpong(p_render_buffers, p_format, p_z_near, p_z_far, variant | VARIANT_FELLOW_WRITE_VELOCITY, p_direct_output && p_render_buffers->get_view_count() == 1);
		return;
	}

	uint32_t view_count = p_render_buffers->get_view_count();
	Size2i internal_size = p_render_buffers->get_internal_size();
	Size2i target_size = p_render_buffers->get_target_size();

	bool just_allocated = false;
	if (!p_render_buffers->has_texture(SNAME("taa"), SNAME("history"))) {
		uint32_t usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;

		p_render_buffers->create_texture(SNAME("taa"), SNAME("history"), p_format, usage_bits);
		p_render_buffers->create_texture(SNAME("taa"), SNAME("temp"), p_format, usage_bits);

		p_render_buffers->create_texture(SNAME("taa"), SNAME("prev_velocity"), RD::DATA_FORMAT_R16G16_SFLOAT, usage_bits);

		just_allocated = true;
	}

	RD::get_singleton()->draw_command_begin_label("TAA");

	for (uint32_t v = 0; v < view_count; v++) {
		// Get our (cached) slices
		RID internal_texture = p_render_buffers->get_internal_texture(v);
		RID velocity_buffer = p_render_buffers->get_velocity_buffer(false, v);
		RID taa_history = p_render_buffers->get_texture_slice(SNAME("taa"), SNAME("history"), v, 0);
		RID taa_prev_velocity = p_render_buffers->get_texture_slice(SNAME("taa"), SNAME("prev_velocity"), v, 0);

		if (!just_allocated) {
			RID depth_texture = p_render_buffers->get_depth_texture(v);
			RID taa_temp = p_render_buffers->get_texture_slice(SNAME("taa"), SNAME("temp"), v, 0);
			resolve(internal_texture, taa_temp, depth_texture, velocity_buffer, taa_prev_velocity, taa_history, Size2(internal_size.x, internal_size.y), p_z_near, p_z_far, variant);
			copy_effects->copy_to_rect(taa_temp, internal_texture, Rect2(0, 0, internal_size.x, internal_size.y));
		}

		copy_effects->copy_to_rect(internal_texture, taa_history, Rect2(0, 0, internal_size.x, internal_size.y));
		copy_effects->copy_to_rect(velocity_buffer, taa_prev_velocity, Rect2(0, 0, target_size.x, target_size.y));
	}

	RD::get_singleton()->draw_command_end_label();
}

// FellowEngine: history ping-pong. The stock path resolves into "temp", then copies temp -> internal,
// internal -> history and velocity -> prev_velocity (3 full-resolution copies). Here the resolve writes the
// new history and next frame's velocity directly into the "other" half of a ping-pong pair, leaving a single
// copy (history -> internal) that the rest of the post-processing chain needs.
void TAA::process_pingpong(Ref<RenderSceneBuffersRD> p_render_buffers, RD::DataFormat p_format, float p_z_near, float p_z_far, uint32_t p_variant, bool p_direct_output) {
	CopyEffects *copy_effects = CopyEffects::get_singleton();

	uint32_t view_count = p_render_buffers->get_view_count();
	Size2i internal_size = p_render_buffers->get_internal_size();
	Size2i target_size = p_render_buffers->get_target_size();

	Ref<FellowPingPongState> state;
	if (p_render_buffers->has_custom_data(SNAME("fellow_taa"))) {
		state = p_render_buffers->get_custom_data(SNAME("fellow_taa"));
	} else {
		state.instantiate();
		p_render_buffers->set_custom_data(SNAME("fellow_taa"), state);
	}

	if (!p_render_buffers->has_texture(SNAME("fellow_taa"), SNAME("history0"))) {
		uint32_t usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
		p_render_buffers->create_texture(SNAME("fellow_taa"), SNAME("history0"), p_format, usage_bits);
		p_render_buffers->create_texture(SNAME("fellow_taa"), SNAME("history1"), p_format, usage_bits);
		p_render_buffers->create_texture(SNAME("fellow_taa"), SNAME("velocity0"), RD::DATA_FORMAT_R16G16_SFLOAT, usage_bits);
		p_render_buffers->create_texture(SNAME("fellow_taa"), SNAME("velocity1"), RD::DATA_FORMAT_R16G16_SFLOAT, usage_bits);
		state->valid = false;
	}

	RD::get_singleton()->draw_command_begin_label("TAA (FellowEngine ping-pong)");

	const StringName hist_names[2] = { SNAME("history0"), SNAME("history1") };
	const StringName vel_names[2] = { SNAME("velocity0"), SNAME("velocity1") };
	uint32_t read = state->parity;
	uint32_t write = 1 - read;

	for (uint32_t v = 0; v < view_count; v++) {
		RID internal_texture = p_render_buffers->get_internal_texture(v);
		RID velocity_buffer = p_render_buffers->get_velocity_buffer(false, v);
		RID hist_read = p_render_buffers->get_texture_slice(SNAME("fellow_taa"), hist_names[read], v, 0);
		RID hist_write = p_render_buffers->get_texture_slice(SNAME("fellow_taa"), hist_names[write], v, 0);
		RID vel_read = p_render_buffers->get_texture_slice(SNAME("fellow_taa"), vel_names[read], v, 0);
		RID vel_write = p_render_buffers->get_texture_slice(SNAME("fellow_taa"), vel_names[write], v, 0);

		if (state->valid) {
			RID depth_texture = p_render_buffers->get_depth_texture(v);
			resolve(internal_texture, hist_write, depth_texture, velocity_buffer, vel_read, hist_read, Size2(internal_size.x, internal_size.y), p_z_near, p_z_far, p_variant, vel_write);
			if (p_direct_output) {
				// FellowEngine postfx/taa_direct_output: the post-processing chain reads hist_write itself.
				p_render_buffers->fellow_post_source = hist_write;
				p_render_buffers->fellow_post_source_frame = RSG::rasterizer->get_frame_number();
			} else {
				copy_effects->copy_to_rect(hist_write, internal_texture, Rect2(0, 0, internal_size.x, internal_size.y));
			}
		} else {
			// First frame after (re)allocation: seed history and velocity like the stock path does.
			copy_effects->copy_to_rect(internal_texture, hist_write, Rect2(0, 0, internal_size.x, internal_size.y));
			copy_effects->copy_to_rect(velocity_buffer, vel_write, Rect2(0, 0, target_size.x, target_size.y));
		}
	}
	state->valid = true;
	state->parity = write;

	RD::get_singleton()->draw_command_end_label();
}
