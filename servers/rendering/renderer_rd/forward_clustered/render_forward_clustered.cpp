/**************************************************************************/
/*  render_forward_clustered.cpp                                          */
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

#include "render_forward_clustered.h"

#include "core/object/callable_mp.h"

#include "core/config/project_settings.h"
#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/renderer_rd/environment/fog.h"
#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/light_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/particles_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server_default.h"
#include "servers/rendering/storage/ltc_lut.gen.h"


using namespace RendererSceneRenderImplementation;

#define PRELOAD_PIPELINES_ON_SURFACE_CACHE_CONSTRUCTION 1

#define FADE_ALPHA_PASS_THRESHOLD 0.999

void RenderForwardClustered::RenderBufferDataForwardClustered::ensure_specular() {
	ERR_FAIL_NULL(render_buffers);

	if (!render_buffers->has_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_SPECULAR)) {
		bool msaa = render_buffers->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED;
		render_buffers->create_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_SPECULAR, get_specular_format(), get_specular_usage_bits(msaa, false, render_buffers->get_can_be_storage()));
		if (msaa) {
			render_buffers->create_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_SPECULAR_MSAA, get_specular_format(), get_specular_usage_bits(false, msaa, render_buffers->get_can_be_storage()), render_buffers->get_texture_samples());
		}
	}
}

void RenderForwardClustered::RenderBufferDataForwardClustered::ensure_normal_roughness_texture() {
	ERR_FAIL_NULL(render_buffers);

	if (!render_buffers->has_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS)) {
		bool msaa = render_buffers->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED;
		render_buffers->create_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS, get_normal_roughness_format(), get_normal_roughness_usage_bits(msaa, false, render_buffers->get_can_be_storage()));
		if (msaa) {
			render_buffers->create_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS_MSAA, get_normal_roughness_format(), get_normal_roughness_usage_bits(false, msaa, render_buffers->get_can_be_storage()), render_buffers->get_texture_samples());
		}
	}
}

void RenderForwardClustered::RenderBufferDataForwardClustered::ensure_voxelgi() {
	ERR_FAIL_NULL(render_buffers);

	if (!render_buffers->has_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_VOXEL_GI)) {
		bool msaa = render_buffers->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED;
		render_buffers->create_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_VOXEL_GI, get_voxelgi_format(), get_voxelgi_usage_bits(msaa, false, render_buffers->get_can_be_storage()));
		if (msaa) {
			render_buffers->create_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_VOXEL_GI_MSAA, get_voxelgi_format(), get_voxelgi_usage_bits(false, msaa, render_buffers->get_can_be_storage()), render_buffers->get_texture_samples());
		}
	}
}

void RenderForwardClustered::RenderBufferDataForwardClustered::ensure_fsr2(RendererRD::FSR2Effect *p_effect) {
	if (fsr2_context == nullptr) {
		fsr2_context = p_effect->create_context(render_buffers->get_internal_size(), render_buffers->get_target_size());
	}
}

#ifdef METAL_MFXTEMPORAL_ENABLED
bool RenderForwardClustered::RenderBufferDataForwardClustered::ensure_mfx_temporal(RendererRD::MFXTemporalEffect *p_effect) {
	if (mfx_temporal_context == nullptr) {
		RendererRD::MFXTemporalEffect::CreateParams params;
		params.input_size = render_buffers->get_internal_size();
		params.output_size = render_buffers->get_target_size();
		params.input_format = render_buffers->get_base_data_format();
		params.depth_format = render_buffers->get_depth_format(false, false, render_buffers->get_can_be_storage());
		params.motion_format = render_buffers->get_velocity_format();
		params.reactive_format = render_buffers->get_base_data_format(); // Reactive is derived from input.
		params.output_format = render_buffers->get_base_data_format();
		params.motion_vector_scale = render_buffers->get_internal_size();
		mfx_temporal_context = p_effect->create_context(params);
		return true;
	}
	return false;
}
#endif

void RenderForwardClustered::RenderBufferDataForwardClustered::free_data() {
	// JIC, should already have been cleared
	if (render_buffers) {
		render_buffers->clear_context(RB_SCOPE_FORWARD_CLUSTERED);
		render_buffers->clear_context(RB_SCOPE_SSDS);
		render_buffers->clear_context(RB_SCOPE_SSIL);
		render_buffers->clear_context(RB_SCOPE_SSAO);
		render_buffers->clear_context(RB_SCOPE_SSR);
	}

	if (cluster_builder) {
		memdelete(cluster_builder);
		cluster_builder = nullptr;
	}

	if (fsr2_context) {
		memdelete(fsr2_context);
		fsr2_context = nullptr;
	}

#ifdef METAL_MFXTEMPORAL_ENABLED
	if (mfx_temporal_context) {
		memdelete(mfx_temporal_context);
		mfx_temporal_context = nullptr;
	}
#endif

	if (!render_sdfgi_uniform_set.is_null() && RD::get_singleton()->uniform_set_is_valid(render_sdfgi_uniform_set)) {
		RD::get_singleton()->free_rid(render_sdfgi_uniform_set);
	}
}

void RenderForwardClustered::RenderBufferDataForwardClustered::configure(RenderSceneBuffersRD *p_render_buffers) {
	if (render_buffers) {
		// JIC
		free_data();
	}

	render_buffers = p_render_buffers;
	ERR_FAIL_NULL(render_buffers);

	if (cluster_builder == nullptr) {
		cluster_builder = memnew(ClusterBuilderRD);
	}
	cluster_builder->set_shared(RenderForwardClustered::get_singleton()->get_cluster_builder_shared());

	RID sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	cluster_builder->setup(p_render_buffers->get_internal_size(), p_render_buffers->get_max_cluster_elements(), p_render_buffers->get_depth_texture(), sampler, p_render_buffers->get_internal_texture());
}

RID RenderForwardClustered::RenderBufferDataForwardClustered::get_color_only_fb() {
	ERR_FAIL_NULL_V(render_buffers, RID());

	bool use_msaa = render_buffers->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED;

	RID color = use_msaa ? render_buffers->get_texture(RB_SCOPE_BUFFERS, RB_TEX_COLOR_MSAA) : render_buffers->get_internal_texture();
	RID depth = use_msaa ? render_buffers->get_texture(RB_SCOPE_BUFFERS, RB_TEX_DEPTH_MSAA) : render_buffers->get_depth_texture();

	if (render_buffers->has_texture(RB_SCOPE_VRS, RB_TEXTURE)) {
		RID vrs_texture = render_buffers->get_texture(RB_SCOPE_VRS, RB_TEXTURE);
		return FramebufferCacheRD::get_singleton()->get_cache_multiview(render_buffers->get_view_count(), color, depth, vrs_texture);
	} else {
		return FramebufferCacheRD::get_singleton()->get_cache_multiview(render_buffers->get_view_count(), color, depth);
	}
}

RID RenderForwardClustered::RenderBufferDataForwardClustered::get_color_pass_fb(uint32_t p_color_pass_flags) {
	ERR_FAIL_NULL_V(render_buffers, RID());
	bool use_msaa = render_buffers->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED;

	int v_count = (p_color_pass_flags & COLOR_PASS_FLAG_MULTIVIEW) ? render_buffers->get_view_count() : 1;
	RID color = use_msaa ? render_buffers->get_texture(RB_SCOPE_BUFFERS, RB_TEX_COLOR_MSAA) : render_buffers->get_internal_texture();

	RID specular;
	if (p_color_pass_flags & COLOR_PASS_FLAG_SEPARATE_SPECULAR) {
		ensure_specular();
		specular = render_buffers->get_texture(RB_SCOPE_FORWARD_CLUSTERED, use_msaa ? RB_TEX_SPECULAR_MSAA : RB_TEX_SPECULAR);
	}

	RID velocity_buffer;
	if (p_color_pass_flags & COLOR_PASS_FLAG_MOTION_VECTORS) {
		render_buffers->ensure_velocity();
		velocity_buffer = render_buffers->get_velocity_buffer(use_msaa);
	}

	RID depth = use_msaa ? render_buffers->get_texture(RB_SCOPE_BUFFERS, RB_TEX_DEPTH_MSAA) : render_buffers->get_depth_texture();

	if (render_buffers->has_texture(RB_SCOPE_VRS, RB_TEXTURE)) {
		RID vrs_texture = render_buffers->get_texture(RB_SCOPE_VRS, RB_TEXTURE);
		return FramebufferCacheRD::get_singleton()->get_cache_multiview(v_count, color, specular, velocity_buffer, depth, vrs_texture);
	} else {
		return FramebufferCacheRD::get_singleton()->get_cache_multiview(v_count, color, specular, velocity_buffer, depth);
	}
}

RID RenderForwardClustered::RenderBufferDataForwardClustered::get_depth_fb(DepthFrameBufferType p_type) {
	ERR_FAIL_NULL_V(render_buffers, RID());
	bool use_msaa = render_buffers->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED;

	RID depth = use_msaa ? render_buffers->get_texture(RB_SCOPE_BUFFERS, RB_TEX_DEPTH_MSAA) : render_buffers->get_depth_texture();

	switch (p_type) {
		case DEPTH_FB: {
			return FramebufferCacheRD::get_singleton()->get_cache_multiview(render_buffers->get_view_count(), depth);
		} break;
		case DEPTH_FB_ROUGHNESS: {
			ensure_normal_roughness_texture();

			RID normal_roughness_buffer = render_buffers->get_texture(RB_SCOPE_FORWARD_CLUSTERED, use_msaa ? RB_TEX_NORMAL_ROUGHNESS_MSAA : RB_TEX_NORMAL_ROUGHNESS);

			return FramebufferCacheRD::get_singleton()->get_cache_multiview(render_buffers->get_view_count(), depth, normal_roughness_buffer);
		} break;
		case DEPTH_FB_ROUGHNESS_VOXELGI: {
			ensure_normal_roughness_texture();
			ensure_voxelgi();

			RID normal_roughness_buffer = render_buffers->get_texture(RB_SCOPE_FORWARD_CLUSTERED, use_msaa ? RB_TEX_NORMAL_ROUGHNESS_MSAA : RB_TEX_NORMAL_ROUGHNESS);
			RID voxelgi_buffer = render_buffers->get_texture(RB_SCOPE_FORWARD_CLUSTERED, use_msaa ? RB_TEX_VOXEL_GI_MSAA : RB_TEX_VOXEL_GI);

			return FramebufferCacheRD::get_singleton()->get_cache_multiview(render_buffers->get_view_count(), depth, normal_roughness_buffer, voxelgi_buffer);
		} break;
		default: {
			ERR_FAIL_V(RID());
		} break;
	}
}

RID RenderForwardClustered::RenderBufferDataForwardClustered::get_specular_only_fb() {
	bool use_msaa = render_buffers->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED;

	RID specular = render_buffers->get_texture(RB_SCOPE_FORWARD_CLUSTERED, use_msaa ? RB_TEX_SPECULAR_MSAA : RB_TEX_SPECULAR);

	return FramebufferCacheRD::get_singleton()->get_cache_multiview(render_buffers->get_view_count(), specular);
}

RID RenderForwardClustered::RenderBufferDataForwardClustered::get_velocity_only_fb() {
	bool use_msaa = render_buffers->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED;

	RID velocity = render_buffers->get_texture(RB_SCOPE_BUFFERS, use_msaa ? RB_TEX_VELOCITY_MSAA : RB_TEX_VELOCITY);

	return FramebufferCacheRD::get_singleton()->get_cache_multiview(render_buffers->get_view_count(), velocity);
}

RD::DataFormat RenderForwardClustered::RenderBufferDataForwardClustered::get_specular_format() {
	return RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
}

uint32_t RenderForwardClustered::RenderBufferDataForwardClustered::get_specular_usage_bits(bool p_resolve, bool p_msaa, bool p_storage) {
	return RenderSceneBuffersRD::get_color_usage_bits(p_resolve, p_msaa, p_storage);
}

RD::DataFormat RenderForwardClustered::RenderBufferDataForwardClustered::get_normal_roughness_format() {
	return RD::DATA_FORMAT_R8G8B8A8_UNORM;
}

uint32_t RenderForwardClustered::RenderBufferDataForwardClustered::get_normal_roughness_usage_bits(bool p_resolve, bool p_msaa, bool p_storage) {
	return RenderSceneBuffersRD::get_color_usage_bits(p_resolve, p_msaa, p_storage);
}

RD::DataFormat RenderForwardClustered::RenderBufferDataForwardClustered::get_voxelgi_format() {
	return RD::DATA_FORMAT_R8G8_UINT;
}

uint32_t RenderForwardClustered::RenderBufferDataForwardClustered::get_voxelgi_usage_bits(bool p_resolve, bool p_msaa, bool p_storage) {
	return RenderSceneBuffersRD::get_color_usage_bits(p_resolve, p_msaa, p_storage);
}

void RenderForwardClustered::setup_render_buffer_data(Ref<RenderSceneBuffersRD> p_render_buffers) {
	Ref<RenderBufferDataForwardClustered> data;
	data.instantiate();
	p_render_buffers->set_custom_data(RB_SCOPE_FORWARD_CLUSTERED, data);

	Ref<RendererRD::GI::RenderBuffersGI> rbgi;
	rbgi.instantiate();
	p_render_buffers->set_custom_data(RB_SCOPE_GI, rbgi);
}

bool RenderForwardClustered::free(RID p_rid) {
	if (RendererSceneRenderRD::free(p_rid)) {
		return true;
	}
	return false;
}

void RenderForwardClustered::update() {
	RendererSceneRenderRD::update();
	_update_global_pipeline_data_requirements_from_project();
	_update_global_pipeline_data_requirements_from_light_storage();
}

/// RENDERING ///

// FellowEngine parallel recording (decision 0018): the job of the segment the calling thread records, if any
// (a RenderForwardClustered::FellowRecordJob).
static thread_local void *fellow_tl_record = nullptr;

template <RenderForwardClustered::PassMode p_pass_mode, uint32_t p_color_pass_flags>
void RenderForwardClustered::_render_list_template(RenderingDevice::DrawListID p_draw_list, RenderingDevice::FramebufferFormatID p_framebuffer_Format, RenderListParameters *p_params, uint32_t p_from_element, uint32_t p_to_element) {
	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	RendererRD::ParticlesStorage *particles_storage = RendererRD::ParticlesStorage::get_singleton();
	RD::DrawListID draw_list = p_draw_list;
	RD::FramebufferFormatID framebuffer_format = p_framebuffer_Format;

	//global scope bindings
	RD::get_singleton()->draw_list_bind_uniform_set(draw_list, render_base_uniform_set, SCENE_UNIFORM_SET);
	RD::get_singleton()->draw_list_bind_uniform_set(draw_list, p_params->render_pass_uniform_set, RENDER_PASS_UNIFORM_SET);
	RD::get_singleton()->draw_list_bind_uniform_set(draw_list, scene_shader.default_vec4_xform_uniform_set, TRANSFORMS_UNIFORM_SET);

	RID prev_material_uniform_set;

	RID prev_vertex_array_rd;
	RID prev_index_array_rd;
	RID prev_xforms_uniform_set;

	SceneShaderForwardClustered::ShaderData *shader = nullptr;
	SceneShaderForwardClustered::ShaderData *prev_shader = nullptr;
	SceneShaderForwardClustered::ShaderData::PipelineKey pipeline_key;
	uint32_t pipeline_hash = 0;
	uint32_t prev_pipeline_hash = 0;

	bool shadow_pass = (p_pass_mode == PASS_MODE_SHADOW) || (p_pass_mode == PASS_MODE_SHADOW_DP);

	SceneState::PushConstant push_constant;

	if constexpr (p_pass_mode == PASS_MODE_DEPTH_MATERIAL) {
		push_constant.uv_offset = Math::make_half_float(p_params->uv_offset.y) << 16;
		push_constant.uv_offset |= Math::make_half_float(p_params->uv_offset.x);
	} else {
		push_constant.uv_offset = 0;
	}

	bool should_request_redraw = false;

	// FellowEngine parallel recording: set while this thread records a segment of a forked draw list.
	FellowRecordJob *const fellow_record = static_cast<FellowRecordJob *>(fellow_tl_record);
	void *fellow_va_surface = nullptr; // the last mesh surface's vertex array lookup (segments: no lock per draw)
	uint64_t fellow_va_input_mask = 0;
	bool fellow_va_motion_vectors = false;
	bool fellow_va_point_size = false;
	RID fellow_va_rd;
	RD::VertexFormatID fellow_va_format = 0;

	for (uint32_t i = p_from_element; i < p_to_element; i++) {
		if (fellow_record != nullptr && fellow_record->abort.load(std::memory_order_relaxed)) {
			return; // Another segment needs a pipeline that is not compiled yet: the list is recorded again serially.
		}
		const GeometryInstanceSurfaceDataCache *surf = p_params->elements[i];
		const RenderElementInfo &element_info = p_params->element_info[i];

		if (p_pass_mode == PASS_MODE_COLOR && surf->color_pass_inclusion_mask && (p_color_pass_flags & surf->color_pass_inclusion_mask) == 0) {
			// Some surfaces can be repeated in multiple render lists. We exclude them from being rendered on the color pass based on the
			// features supported by the pass compared to the exclusion mask.
			continue;
		}

		if (surf->owner->instance_count == 0) {
			continue;
		}

		push_constant.base_index = i + p_params->element_offset;

		RID material_uniform_set;
		void *mesh_surface;

		if (shadow_pass || p_pass_mode == PASS_MODE_DEPTH) { //regular depth pass can use these too
			material_uniform_set = surf->material_uniform_set_shadow;
			shader = surf->shader_shadow;
			mesh_surface = surf->surface_shadow;

		} else {
#ifdef DEBUG_ENABLED
			if (unlikely(get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_LIGHTING)) {
				material_uniform_set = scene_shader.default_material_uniform_set;
				shader = scene_shader.default_material_shader_ptr;
			} else if (unlikely(get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_OVERDRAW)) {
				material_uniform_set = scene_shader.overdraw_material_uniform_set;
				shader = scene_shader.overdraw_material_shader_ptr;
			} else if (unlikely(get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_PSSM_SPLITS)) {
				material_uniform_set = scene_shader.debug_shadow_splits_material_uniform_set;
				shader = scene_shader.debug_shadow_splits_material_shader_ptr;
			} else {
#endif
				material_uniform_set = surf->material_uniform_set;
				shader = surf->shader;
				surf->material->set_as_used();
#ifdef DEBUG_ENABLED
			}
#endif
			mesh_surface = surf->surface;
		}

		if (!mesh_surface) {
			continue;
		}

		//request a redraw if one of the shaders uses TIME
		if (shader->uses_time) {
			should_request_redraw = true;
		}

		// Determine the cull variant.
		SceneShaderForwardClustered::ShaderData::CullVariant cull_variant = SceneShaderForwardClustered::ShaderData::CULL_VARIANT_MAX;
		if constexpr (p_pass_mode == PASS_MODE_DEPTH_MATERIAL || p_pass_mode == PASS_MODE_SDF) {
			cull_variant = SceneShaderForwardClustered::ShaderData::CULL_VARIANT_DOUBLE_SIDED;
		} else {
			if constexpr (p_pass_mode == PASS_MODE_SHADOW || p_pass_mode == PASS_MODE_SHADOW_DP) {
				if (surf->flags & GeometryInstanceSurfaceDataCache::FLAG_USES_DOUBLE_SIDED_SHADOWS) {
					cull_variant = SceneShaderForwardClustered::ShaderData::CULL_VARIANT_DOUBLE_SIDED;
				}
			}

			if (cull_variant == SceneShaderForwardClustered::ShaderData::CULL_VARIANT_MAX) {
				bool mirror = surf->owner->mirror;
				if (p_params->reverse_cull) {
					mirror = !mirror;
				}

				cull_variant = mirror ? SceneShaderForwardClustered::ShaderData::CULL_VARIANT_REVERSED : SceneShaderForwardClustered::ShaderData::CULL_VARIANT_NORMAL;
			}
		}

		pipeline_key.primitive_type = surf->primitive;

		RID xforms_uniform_set = surf->owner->transforms_uniform_set;

		SceneShaderForwardClustered::ShaderSpecialization pipeline_specialization = p_params->base_specialization;
		pipeline_specialization.fellow_persistent_instances = fellow_persistent.active; // the lists' format this frame (decision 0022)
		pipeline_specialization.multimesh = bool(surf->owner->base_flags & INSTANCE_DATA_FLAG_MULTIMESH);
		pipeline_specialization.multimesh_format_2d = bool(surf->owner->base_flags & INSTANCE_DATA_FLAG_MULTIMESH_FORMAT_2D);
		pipeline_specialization.multimesh_has_color = bool(surf->owner->base_flags & INSTANCE_DATA_FLAG_MULTIMESH_HAS_COLOR);
		pipeline_specialization.multimesh_has_custom_data = bool(surf->owner->base_flags & INSTANCE_DATA_FLAG_MULTIMESH_HAS_CUSTOM_DATA);

		if constexpr (p_pass_mode == PASS_MODE_COLOR) {
			pipeline_specialization.use_light_soft_shadows = element_info.uses_softshadow;
			pipeline_specialization.use_light_projector = element_info.uses_projector;
			pipeline_specialization.use_directional_soft_shadows = p_params->use_directional_soft_shadow;
		}

		pipeline_key.color_pass_flags = 0;

		switch (p_pass_mode) {
			case PASS_MODE_COLOR: {
				if (element_info.uses_lightmap) {
					pipeline_key.color_pass_flags |= SceneShaderForwardClustered::PIPELINE_COLOR_PASS_FLAG_LIGHTMAP;
				} else {
					pipeline_specialization.use_forward_gi = element_info.uses_forward_gi;
				}

				if constexpr ((p_color_pass_flags & COLOR_PASS_FLAG_SEPARATE_SPECULAR) != 0) {
					pipeline_key.color_pass_flags |= SceneShaderForwardClustered::PIPELINE_COLOR_PASS_FLAG_SEPARATE_SPECULAR;
				}

				if constexpr ((p_color_pass_flags & COLOR_PASS_FLAG_MOTION_VECTORS) != 0) {
					pipeline_key.color_pass_flags |= SceneShaderForwardClustered::PIPELINE_COLOR_PASS_FLAG_MOTION_VECTORS;
				}

				if constexpr ((p_color_pass_flags & COLOR_PASS_FLAG_TRANSPARENT) != 0) {
					pipeline_key.color_pass_flags |= SceneShaderForwardClustered::PIPELINE_COLOR_PASS_FLAG_TRANSPARENT;
				}

				if constexpr ((p_color_pass_flags & COLOR_PASS_FLAG_MULTIVIEW) != 0) {
					pipeline_key.color_pass_flags |= SceneShaderForwardClustered::PIPELINE_COLOR_PASS_FLAG_MULTIVIEW;
				}

				pipeline_key.version = SceneShaderForwardClustered::PIPELINE_VERSION_COLOR_PASS;
			} break;
			case PASS_MODE_SHADOW:
			case PASS_MODE_DEPTH: {
				pipeline_key.version = p_params->view_count > 1 ? SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS_MULTIVIEW : SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS;
			} break;
			case PASS_MODE_SHADOW_DP: {
				ERR_FAIL_COND_MSG(p_params->view_count > 1, "Multiview not supported for shadow DP pass");
				pipeline_key.version = SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS_DP;
			} break;
			case PASS_MODE_DEPTH_NORMAL_ROUGHNESS: {
				pipeline_key.version = p_params->view_count > 1 ? SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_MULTIVIEW : SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS;
			} break;
			case PASS_MODE_DEPTH_NORMAL_ROUGHNESS_VOXEL_GI: {
				pipeline_key.version = p_params->view_count > 1 ? SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_AND_VOXEL_GI_MULTIVIEW : SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_AND_VOXEL_GI;
			} break;
			case PASS_MODE_DEPTH_MATERIAL: {
				ERR_FAIL_COND_MSG(p_params->view_count > 1, "Multiview not supported for material pass");
				pipeline_key.version = SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS_WITH_MATERIAL;
			} break;
			case PASS_MODE_SDF: {
				// Note, SDF is prepared in world space, this shouldn't be a multiview buffer even when stereoscopic rendering is used.
				ERR_FAIL_COND_MSG(p_params->view_count > 1, "Multiview not supported for SDF pass");
				pipeline_key.version = SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS_WITH_SDF;
			} break;
		}

		pipeline_key.framebuffer_format_id = framebuffer_format;
		pipeline_key.wireframe = p_params->force_wireframe;
		pipeline_key.ubershader = 0;

		bool emulate_point_size = shader->uses_point_size && scene_shader.emulate_point_size;

		const RD::PolygonCullMode cull_mode = shader->get_cull_mode_from_cull_variant(cull_variant);
		RID vertex_array_rd;
		RID index_array_rd;
		RID pipeline_rd;
		const uint32_t ubershader_iterations = 2;
		bool pipeline_valid = false;
		while (pipeline_key.ubershader < ubershader_iterations) {
			// Skeleton and blend shape.
			RD::VertexFormatID vertex_format = -1;
			bool pipeline_motion_vectors = pipeline_key.color_pass_flags & SceneShaderForwardClustered::PIPELINE_COLOR_PASS_FLAG_MOTION_VECTORS;
			uint64_t input_mask = shader->get_vertex_input_mask(pipeline_key.version, pipeline_key.color_pass_flags, pipeline_key.ubershader);
			if (surf->owner->mesh_instance.is_valid()) {
				mesh_storage->mesh_instance_surface_get_vertex_arrays_and_format(surf->owner->mesh_instance, surf->surface_index, input_mask, pipeline_motion_vectors, emulate_point_size, vertex_array_rd, vertex_format);
			} else if (fellow_record != nullptr) {
				// Same result as the lookup (the surface's versions do not change while lists are recorded), without its
				// lock when consecutive elements use the same surface.
				if (mesh_surface != fellow_va_surface || input_mask != fellow_va_input_mask || pipeline_motion_vectors != fellow_va_motion_vectors || emulate_point_size != fellow_va_point_size) {
					mesh_storage->mesh_surface_get_vertex_arrays_and_format(mesh_surface, input_mask, pipeline_motion_vectors, emulate_point_size, fellow_va_rd, fellow_va_format);
					fellow_va_surface = mesh_surface;
					fellow_va_input_mask = input_mask;
					fellow_va_motion_vectors = pipeline_motion_vectors;
					fellow_va_point_size = emulate_point_size;
				}
				vertex_array_rd = fellow_va_rd;
				vertex_format = fellow_va_format;
			} else {
				mesh_storage->mesh_surface_get_vertex_arrays_and_format(mesh_surface, input_mask, pipeline_motion_vectors, emulate_point_size, vertex_array_rd, vertex_format);
			}

			pipeline_key.vertex_format_id = vertex_format;

			if (pipeline_key.ubershader) {
				pipeline_key.shader_specialization = {};
				pipeline_key.cull_mode = RD::POLYGON_CULL_DISABLED;
			} else {
				pipeline_key.shader_specialization = pipeline_specialization;
				pipeline_key.cull_mode = cull_mode;
			}

			pipeline_hash = pipeline_key.hash();

			if (shader != prev_shader || pipeline_hash != prev_pipeline_hash) {
				RSE::PipelineSource pipeline_source = pipeline_key.ubershader ? RSE::PIPELINE_SOURCE_DRAW : RSE::PIPELINE_SOURCE_SPECIALIZATION;
				if (fellow_record != nullptr) {
					// Parallel recording: pipelines already in the map only. Otherwise the list is recorded again serially,
					// which requests (and for the ubershader, waits for) the compilation as stock does.
					if (!shader->pipeline_hash_map.fellow_find_pipeline(pipeline_hash, pipeline_rd)) {
						fellow_record->abort.store(true, std::memory_order_relaxed);
						return;
					}
				} else {
					pipeline_rd = shader->pipeline_hash_map.get_pipeline(pipeline_key, pipeline_hash, pipeline_key.ubershader, pipeline_source);
				}

				if (pipeline_rd.is_valid()) {
					pipeline_valid = true;
					prev_shader = shader;
					prev_pipeline_hash = pipeline_hash;
					break;
				} else {
					pipeline_key.ubershader++;
				}
			} else {
				// The same pipeline is bound already.
				pipeline_valid = true;
				break;
			}
		}

		if (pipeline_valid) {
			if (!emulate_point_size) {
				index_array_rd = mesh_storage->mesh_surface_get_index_array(mesh_surface, element_info.lod_index);
			} else {
				index_array_rd = RID();
			}

			if (prev_vertex_array_rd != vertex_array_rd) {
				RD::get_singleton()->draw_list_bind_vertex_array(draw_list, vertex_array_rd);
				prev_vertex_array_rd = vertex_array_rd;
			}

			if (prev_index_array_rd != index_array_rd) {
				if (index_array_rd.is_valid()) {
					RD::get_singleton()->draw_list_bind_index_array(draw_list, index_array_rd);
				}
				prev_index_array_rd = index_array_rd;
			}

			if (!pipeline_rd.is_null()) {
				RD::get_singleton()->draw_list_bind_render_pipeline(draw_list, pipeline_rd);
			}

			if (xforms_uniform_set.is_valid() && prev_xforms_uniform_set != xforms_uniform_set) {
				RD::get_singleton()->draw_list_bind_uniform_set(draw_list, xforms_uniform_set, TRANSFORMS_UNIFORM_SET);
				prev_xforms_uniform_set = xforms_uniform_set;
			}

			if (material_uniform_set != prev_material_uniform_set) {
				// Update uniform set.
				if (material_uniform_set.is_valid() && (fellow_record != nullptr ? RD::get_singleton()->fellow_uniform_set_is_valid_unlocked(material_uniform_set) : RD::get_singleton()->uniform_set_is_valid(material_uniform_set))) { // Material may not have a uniform set.
					RD::get_singleton()->draw_list_bind_uniform_set(draw_list, material_uniform_set, MATERIAL_UNIFORM_SET);
				}

				prev_material_uniform_set = material_uniform_set;
			}

			if (surf->owner->base_flags & INSTANCE_DATA_FLAG_PARTICLES) {
				particles_storage->particles_get_instance_buffer_motion_vectors_offsets(surf->owner->data->base, push_constant.multimesh_motion_vectors_current_offset, push_constant.multimesh_motion_vectors_previous_offset);
			} else if (surf->owner->base_flags & INSTANCE_DATA_FLAG_MULTIMESH) {
				mesh_storage->_multimesh_get_motion_vectors_offsets(surf->owner->data->base, push_constant.multimesh_motion_vectors_current_offset, push_constant.multimesh_motion_vectors_previous_offset);
			} else {
				push_constant.multimesh_motion_vectors_current_offset = 0;
				push_constant.multimesh_motion_vectors_previous_offset = 0;
			}

			uint32_t instance_count = surf->owner->instance_count > 1 ? surf->owner->instance_count : element_info.repeat;
			if (surf->flags & GeometryInstanceSurfaceDataCache::FLAG_USES_PARTICLE_TRAILS) {
				instance_count /= surf->owner->trail_steps;
			}

			bool indirect = bool(surf->owner->base_flags & INSTANCE_DATA_FLAG_MULTIMESH_INDIRECT);

			const FellowMeshClusters *fellow_clusters = nullptr;
			uint32_t fellow_occlusion_run = UINT32_MAX;
			if (!emulate_point_size && !indirect) {
				// Shaders that can move vertices (VERTEX/POSITION writes, custom matrices, grow, billboards) may leave the
				// cluster bounds, which are computed from the mesh data: such elements are drawn whole.
				const bool fellow_static_vertices = !(shader->uses_vertex || shader->uses_position || shader->writes_modelview_or_projection || shader->uses_z_clip_scale || shader->uses_particle_trails);
				if (p_params->fellow_cluster_plane_count > 0 && fellow_static_vertices && instance_count == 1 && index_array_rd.is_valid() && !surf->owner->mesh_instance.is_valid() && !(surf->owner->base_flags & (INSTANCE_DATA_FLAG_MULTIMESH | INSTANCE_DATA_FLAG_PARTICLES))) {
					fellow_clusters = mesh_storage->fellow_mesh_surface_get_clusters(mesh_surface);
					if (fellow_clusters && (element_info.lod_index >= fellow_clusters->levels.size() || (fellow_clusters->preserves_order && !Fellow::mesh_clusters_blended()))) {
						fellow_clusters = nullptr;
					}
				}
				if constexpr (p_pass_mode == PASS_MODE_COLOR) {
					// Occlusion culling (decision 0026): a run the GPU tested is drawn with the count of its visible
					// elements, read from the compacted elements (the top bit of the element index).
					if (fellow_clusters == nullptr && p_params->fellow_occlusion_runs != nullptr && index_array_rd.is_valid() && instance_count == element_info.repeat) {
						fellow_occlusion_run = p_params->fellow_occlusion_runs[i];
						if (fellow_occlusion_run != UINT32_MAX) {
							push_constant.base_index |= 0x80000000u;
						}
					}
				}
			}

			size_t push_constant_size = 0;
			if (pipeline_key.ubershader) {
				push_constant_size = sizeof(SceneState::PushConstant);
				push_constant.ubershader.specialization = pipeline_specialization;
				push_constant.ubershader.constants = {};
				push_constant.ubershader.constants.cull_mode = cull_mode;
			} else {
				push_constant_size = sizeof(SceneState::PushConstant) - sizeof(SceneState::PushConstantUbershader);
			}

			RD::get_singleton()->draw_list_set_push_constant(draw_list, &push_constant, push_constant_size);

			if (emulate_point_size) {
				if (indirect) {
					WARN_PRINT("Indirect draws are not supported when emulating point size.");
				}
				RD::get_singleton()->draw_list_draw(draw_list, false, mesh_storage->mesh_surface_get_vertex_count(mesh_surface), instance_count * 6);
			} else if (indirect) {
				RD::get_singleton()->draw_list_draw_indirect(draw_list, index_array_rd.is_valid(), mesh_storage->_multimesh_get_command_buffer_rd_rid(surf->owner->data->base), surf->surface_index * sizeof(uint32_t) * mesh_storage->INDIRECT_MULTIMESH_COMMAND_STRIDE, 1, 0);
			} else if (fellow_occlusion_run != UINT32_MAX) {
				RD::get_singleton()->draw_list_draw_indirect(draw_list, true, p_params->fellow_occlusion_args, fellow_occlusion_run * 5 * sizeof(uint32_t), 1, 5 * sizeof(uint32_t));
				Fellow::stats().occlusion_cull_indirect_draws.increment();
			} else {
				if (fellow_clusters) {
					// Draw only the clusters that intersect the view volume (planes moved into the surface's local space).
					const FellowMeshClusters::Level &level = fellow_clusters->levels[element_info.lod_index];
					const Transform3D &xf = surf->owner->transform;
					Plane local_planes[8];
					const int plane_count = MIN(p_params->fellow_cluster_plane_count, 8);
					for (int p = 0; p < plane_count; p++) {
						const Plane &wp = p_params->fellow_cluster_planes[p];
						local_planes[p] = Plane(xf.basis.xform_inv(wp.normal), wp.d - wp.normal.dot(xf.origin));
					}
					thread_local LocalVector<uint32_t> fellow_ranges;
					fellow_ranges.clear();
					const uint32_t drawn = fellow_clusters->cull(element_info.lod_index, local_planes, plane_count, fellow_ranges);
					if (fellow_ranges.size() > 0) {
						// The element's own index array (bound above) is in cluster order.
						for (uint32_t r = 0; r < fellow_ranges.size(); r += 2) {
							RD::get_singleton()->draw_list_draw_indexed_range(draw_list, fellow_ranges[r], fellow_ranges[r + 1], 1);
						}
					}
					Fellow::stats().geometry_cluster_draws.add(fellow_ranges.size() / 2);
					Fellow::stats().geometry_cluster_triangles_drawn.add(drawn / 3);
					Fellow::stats().geometry_cluster_triangles_culled.add((level.index_count - drawn) / 3);
				} else {
					RD::get_singleton()->draw_list_draw(draw_list, index_array_rd.is_valid(), instance_count);
				}
			}
		}

		i += element_info.repeat - 1; //skip equal elements
	}

	// Make the actual redraw request
	if (should_request_redraw) {
		if (fellow_record != nullptr) {
			fellow_record->redraw.store(true, std::memory_order_relaxed); // requested by the render thread at the join
		} else {
			RenderingServerDefault::redraw_request();
		}
	}
}

void RenderForwardClustered::_render_list(RenderingDevice::DrawListID p_draw_list, RenderingDevice::FramebufferFormatID p_framebuffer_Format, RenderListParameters *p_params, uint32_t p_from_element, uint32_t p_to_element) {
	if (Fellow::cpu_parallel_recording() && _fellow_render_list_parallel(p_draw_list, p_framebuffer_Format, p_params, p_from_element, p_to_element)) {
		return;
	}
	_render_list_serial(p_draw_list, p_framebuffer_Format, p_params, p_from_element, p_to_element);
}

// Parallel recording: lists from this many draws (runs of repeated elements count once), at least this many draws
// per segment (and at most cpu/recording_segments_per_thread segments per thread). City Lab's instanced lists have thousands of elements in a few hundred draws: they stay serial.
static constexpr uint32_t FELLOW_RECORD_MIN_DRAWS = 384;
static constexpr uint32_t FELLOW_RECORD_SEGMENT_DRAWS = 64;

bool RenderForwardClustered::_fellow_render_list_parallel(RenderingDevice::DrawListID p_draw_list, RenderingDevice::FramebufferFormatID p_framebuffer_Format, RenderListParameters *p_params, uint32_t p_from_element, uint32_t p_to_element) {
	if (p_to_element <= p_from_element || p_to_element - p_from_element < FELLOW_RECORD_MIN_DRAWS || fellow_parallel == nullptr || p_params->element_info == nullptr) {
		return false;
	}
	const uint32_t helpers = MIN((uint32_t)Fellow::cpu_worker_tasks(), (uint32_t)WorkerThreadPool::get_singleton()->get_thread_count());
	if (helpers == 0) {
		return false;
	}

	// The runs (the serial loop draws a run of repeated elements at once). Segments start at run starts: the serial loop
	// visits every run start (from inside a run it either draws the rest of the run or steps to the next element), so
	// each segment records exactly the draws the serial loop records for its elements.
	FellowRecordJob &job = fellow_record;
	LocalVector<uint32_t> &runs = job.runs;
	runs.clear();
	const RenderElementInfo *element_info = p_params->element_info;
	for (uint32_t i = p_from_element; i < p_to_element; i += MAX(1u, uint32_t(element_info[i].repeat))) {
		runs.push_back(i);
	}
	const uint32_t draws = runs.size();
	if (draws < FELLOW_RECORD_MIN_DRAWS) {
		return false;
	}
	// More segments than threads balance the load (the threads take segments as they finish theirs: an efficiency core
	// records slower than a performance core); the replay joins consecutive segments again.
	const uint32_t per_thread = (uint32_t)CLAMP(Fellow::cpu_recording_segments_per_thread(), 1, 8);
	const uint32_t segments = MIN(draws / FELLOW_RECORD_SEGMENT_DRAWS, (helpers + 1) * per_thread);
	if (segments < 2) {
		return false;
	}
	job.starts.clear();
	for (uint32_t k = 0; k < segments; k++) {
		job.starts.push_back(runs[uint32_t(uint64_t(draws) * k / segments)]);
	}
	job.starts.push_back(p_to_element);
	if (!RD::get_singleton()->fellow_draw_list_fork(p_draw_list, segments)) {
		return false;
	}
	job.draw_list = p_draw_list;
	job.framebuffer_format = p_framebuffer_Format;
	job.params = p_params;
	job.redraw.store(false, std::memory_order_relaxed);
	job.abort.store(false, std::memory_order_relaxed);
	_fellow_parallel_for(segments, &RenderForwardClustered::_fellow_record_chunk);
	const bool abort = job.abort.load(std::memory_order_relaxed);
	RD::get_singleton()->fellow_draw_list_join(abort);
	if (abort) {
		Fellow::stats().cpu_record_fallbacks.increment();
		return false; // Recorded again serially by the caller.
	}
	if (job.redraw.load(std::memory_order_relaxed)) {
		RenderingServerDefault::redraw_request();
	}
	Fellow::stats().cpu_record_lists.increment();
	Fellow::stats().cpu_record_segments.add(segments);
	return true;
}

void RenderForwardClustered::_fellow_record_chunk(void *p_self, uint32_t p_index) {
	RenderForwardClustered *self = static_cast<RenderForwardClustered *>(p_self);
	FellowRecordJob &job = self->fellow_record;
	RD::get_singleton()->fellow_draw_list_segment_begin(p_index);
	fellow_tl_record = &job;
	self->_render_list_serial(job.draw_list, job.framebuffer_format, job.params, job.starts[p_index], job.starts[p_index + 1]);
	fellow_tl_record = nullptr;
	RD::get_singleton()->fellow_draw_list_segment_end();
}

void RenderForwardClustered::_render_list_serial(RenderingDevice::DrawListID p_draw_list, RenderingDevice::FramebufferFormatID p_framebuffer_Format, RenderListParameters *p_params, uint32_t p_from_element, uint32_t p_to_element) {
	//use template for faster performance (pass mode comparisons are inlined)

	switch (p_params->pass_mode) {
#define VALID_FLAG_COMBINATION(f) \
	case f: { \
		_render_list_template<PASS_MODE_COLOR, f>(p_draw_list, p_framebuffer_Format, p_params, p_from_element, p_to_element); \
	} break;

		case PASS_MODE_COLOR: {
			switch (p_params->color_pass_flags) {
				VALID_FLAG_COMBINATION(0);
				VALID_FLAG_COMBINATION(COLOR_PASS_FLAG_TRANSPARENT);
				VALID_FLAG_COMBINATION(COLOR_PASS_FLAG_TRANSPARENT | COLOR_PASS_FLAG_MULTIVIEW);
				VALID_FLAG_COMBINATION(COLOR_PASS_FLAG_TRANSPARENT | COLOR_PASS_FLAG_MOTION_VECTORS);
				VALID_FLAG_COMBINATION(COLOR_PASS_FLAG_SEPARATE_SPECULAR);
				VALID_FLAG_COMBINATION(COLOR_PASS_FLAG_SEPARATE_SPECULAR | COLOR_PASS_FLAG_MULTIVIEW);
				VALID_FLAG_COMBINATION(COLOR_PASS_FLAG_SEPARATE_SPECULAR | COLOR_PASS_FLAG_MOTION_VECTORS);
				VALID_FLAG_COMBINATION(COLOR_PASS_FLAG_MULTIVIEW);
				VALID_FLAG_COMBINATION(COLOR_PASS_FLAG_MULTIVIEW | COLOR_PASS_FLAG_MOTION_VECTORS);
				VALID_FLAG_COMBINATION(COLOR_PASS_FLAG_MOTION_VECTORS);
				VALID_FLAG_COMBINATION(COLOR_PASS_FLAG_SEPARATE_SPECULAR | COLOR_PASS_FLAG_MULTIVIEW | COLOR_PASS_FLAG_MOTION_VECTORS);
				VALID_FLAG_COMBINATION(COLOR_PASS_FLAG_TRANSPARENT | COLOR_PASS_FLAG_MULTIVIEW | COLOR_PASS_FLAG_MOTION_VECTORS);
				default: {
					ERR_FAIL_MSG("Invalid color pass flag combination " + itos(p_params->color_pass_flags));
				}
			}

		} break;
		case PASS_MODE_SHADOW: {
			_render_list_template<PASS_MODE_SHADOW>(p_draw_list, p_framebuffer_Format, p_params, p_from_element, p_to_element);
		} break;
		case PASS_MODE_SHADOW_DP: {
			_render_list_template<PASS_MODE_SHADOW_DP>(p_draw_list, p_framebuffer_Format, p_params, p_from_element, p_to_element);
		} break;
		case PASS_MODE_DEPTH: {
			_render_list_template<PASS_MODE_DEPTH>(p_draw_list, p_framebuffer_Format, p_params, p_from_element, p_to_element);
		} break;
		case PASS_MODE_DEPTH_NORMAL_ROUGHNESS: {
			_render_list_template<PASS_MODE_DEPTH_NORMAL_ROUGHNESS>(p_draw_list, p_framebuffer_Format, p_params, p_from_element, p_to_element);
		} break;
		case PASS_MODE_DEPTH_NORMAL_ROUGHNESS_VOXEL_GI: {
			_render_list_template<PASS_MODE_DEPTH_NORMAL_ROUGHNESS_VOXEL_GI>(p_draw_list, p_framebuffer_Format, p_params, p_from_element, p_to_element);
		} break;
		case PASS_MODE_DEPTH_MATERIAL: {
			_render_list_template<PASS_MODE_DEPTH_MATERIAL>(p_draw_list, p_framebuffer_Format, p_params, p_from_element, p_to_element);
		} break;
		case PASS_MODE_SDF: {
			_render_list_template<PASS_MODE_SDF>(p_draw_list, p_framebuffer_Format, p_params, p_from_element, p_to_element);
		} break;
		default: {
			// Unknown pass mode.
		} break;
	}
}

void RenderForwardClustered::_render_list_with_draw_list(RenderListParameters *p_params, RID p_framebuffer, BitField<RD::DrawFlags> p_draw_flags, const Vector<Color> &p_clear_color_values, float p_clear_depth_value, uint32_t p_clear_stencil_value, const Rect2 &p_region) {
	RD::FramebufferFormatID fb_format = RD::get_singleton()->framebuffer_get_format(p_framebuffer);
	p_params->framebuffer_format = fb_format;

	RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(p_framebuffer, p_draw_flags, p_clear_color_values, p_clear_depth_value, p_clear_stencil_value, p_region);
	_render_list(draw_list, fb_format, p_params, 0, p_params->element_count);
	RD::get_singleton()->draw_list_end();
}

uint32_t RenderForwardClustered::_setup_environment(const RenderDataRD *p_render_data, bool p_no_fog, const Size2i &p_screen_size, const Size2 &p_viewport_size, const Color &p_default_bg_color, bool p_opaque_render_buffers, bool p_apply_alpha_multiplier, bool p_pancake_shadows) {
	RendererRD::LightStorage *light_storage = RendererRD::LightStorage::get_singleton();

	Ref<RenderSceneBuffersRD> rd = p_render_data->render_buffers;
	RID env = is_environment(p_render_data->environment) ? p_render_data->environment : RID();
	RID reflection_probe_instance = p_render_data->reflection_probe.is_valid() ? light_storage->reflection_probe_instance_get_probe(p_render_data->reflection_probe) : RID();

	// May do this earlier in RenderSceneRenderRD::render_scene
	uint32_t uniform_buffer_index = scene_state.used_uniform_buffer_count;
	++scene_state.used_uniform_buffer_count;

	if (uniform_buffer_index >= scene_state.uniform_buffers.size()) {
		uint32_t from = scene_state.uniform_buffers.size();
		scene_state.uniform_buffers.resize(uniform_buffer_index + 1);
		for (uint32_t i = from; i < scene_state.uniform_buffers.size(); i++) {
			scene_state.uniform_buffers[i] = p_render_data->scene_data->create_uniform_buffer();
		}
	}

	float luminance_multiplier = rd.is_valid() ? rd->get_luminance_multiplier() : 1.0;

	p_render_data->scene_data->update_ubo(scene_state.uniform_buffers[uniform_buffer_index], get_debug_draw_mode(), env, reflection_probe_instance, p_render_data->camera_attributes, p_pancake_shadows, p_screen_size, p_viewport_size, p_default_bg_color, luminance_multiplier, p_opaque_render_buffers, p_apply_alpha_multiplier);

	// now do implementation UBO

	scene_state.ubo.cluster_shift = Math::get_shift_from_power_of_2(p_render_data->cluster_size);
	scene_state.ubo.max_cluster_element_count_div_32 = p_render_data->cluster_max_elements / 32;
	{
		uint32_t cluster_screen_width = Math::division_round_up((uint32_t)p_screen_size.width, p_render_data->cluster_size);
		uint32_t cluster_screen_height = Math::division_round_up((uint32_t)p_screen_size.height, p_render_data->cluster_size);
		scene_state.ubo.cluster_type_size = cluster_screen_width * cluster_screen_height * (scene_state.ubo.max_cluster_element_count_div_32 + 32);
		scene_state.ubo.cluster_width = cluster_screen_width;
	}

	scene_state.ubo.gi_upscale_for_msaa = false;
	scene_state.ubo.volumetric_fog_enabled = false;

	if (rd.is_valid()) {
		if (rd->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED) {
			scene_state.ubo.gi_upscale_for_msaa = true;
		}

		if (rd->has_custom_data(RB_SCOPE_FOG)) {
			Ref<RendererRD::Fog::VolumetricFog> fog = rd->get_custom_data(RB_SCOPE_FOG);

			scene_state.ubo.volumetric_fog_enabled = true;
			float fog_end = fog->length;
			if (fog_end > 0.0) {
				scene_state.ubo.volumetric_fog_inv_length = 1.0 / fog_end;
			} else {
				scene_state.ubo.volumetric_fog_inv_length = 1.0;
			}

			float fog_detail_spread = fog->spread; //reverse lookup
			if (fog_detail_spread > 0.0) {
				scene_state.ubo.volumetric_fog_detail_spread = 1.0 / fog_detail_spread;
			} else {
				scene_state.ubo.volumetric_fog_detail_spread = 1.0;
			}
		}
	}

	if (get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_UNSHADED) {
		scene_state.ubo.ss_effects_flags = 0;
	} else if (p_render_data->reflection_probe.is_null() && is_environment(p_render_data->environment)) {
		scene_state.ubo.ssao_ao_affect = environment_get_ssao_ao_channel_affect(p_render_data->environment);
		scene_state.ubo.ssao_light_affect = environment_get_ssao_direct_light_affect(p_render_data->environment);
		uint32_t ss_flags = 0;
		if (p_opaque_render_buffers) {
			ss_flags |= environment_get_ssao_enabled(p_render_data->environment) ? (1 << 0) : 0;
			ss_flags |= environment_get_ssil_enabled(p_render_data->environment) ? (1 << 1) : 0;
			ss_flags |= environment_get_ssr_enabled(p_render_data->environment) ? (1 << 2) : 0;

			if (rd.is_valid()) {
				Ref<RenderBufferDataForwardClustered> rb_data;
				if (rd->has_custom_data(RB_SCOPE_FORWARD_CLUSTERED)) {
					rb_data = rd->get_custom_data(RB_SCOPE_FORWARD_CLUSTERED);
					ss_flags |= (rb_data.is_valid() && !rb_data->ss_effects_data.ssr.half_size) ? (1 << 3) : 0;
				}
			}
		}
		scene_state.ubo.ss_effects_flags = ss_flags;
	} else {
		scene_state.ubo.ss_effects_flags = 0;
	}

	if (uniform_buffer_index >= scene_state.implementation_uniform_buffers.size()) {
		uint32_t from = scene_state.implementation_uniform_buffers.size();
		scene_state.implementation_uniform_buffers.resize(uniform_buffer_index + 1);
		for (uint32_t i = from; i < scene_state.implementation_uniform_buffers.size(); i++) {
			scene_state.implementation_uniform_buffers[i] = RD::get_singleton()->uniform_buffer_create(sizeof(SceneState::UBO));
		}
	}

	RD::get_singleton()->buffer_update(scene_state.implementation_uniform_buffers[uniform_buffer_index], 0, sizeof(SceneState::UBO), &scene_state.ubo);

	return uniform_buffer_index;
}

void RenderForwardClustered::SceneState::grow_instance_buffer(RenderListType p_render_list, uint32_t p_req_element_count, bool p_append) {
	if (p_req_element_count > 0) {
		if (!p_append && instance_buffer[p_render_list].fellow_set_memory((uint8_t)Fellow::cpu_instance_buffer_memory())) {
			// FellowEngine (cpu/instance_buffer_memory, decision 0021): the buffers are created again in the other memory
			// (only where the list starts over, as when they grow).
			curr_gpu_ptr[p_render_list] = nullptr;
		}
		if (instance_buffer[p_render_list].get_size(0u) < p_req_element_count * sizeof(SceneState::InstanceData)) {
			instance_buffer[p_render_list].uninit();
			uint32_t new_size = Math::nearest_power_of_2_templated(MAX(uint64_t(INSTANCE_DATA_BUFFER_MIN_SIZE), p_req_element_count));
			instance_buffer[p_render_list].set_storage_size(0u, new_size * sizeof(SceneState::InstanceData));
			curr_gpu_ptr[p_render_list] = nullptr;
		}

		const bool must_remap = instance_buffer[p_render_list].prepare_for_map(p_append);
		if (must_remap) {
			curr_gpu_ptr[p_render_list] = nullptr;
		}
	}
}

void RenderForwardClustered::_fellow_parallel_for(uint32_t p_count, FellowParallelFor::Func p_func, uint32_t p_max_helpers) {
	fellow_parallel->set_linger_usec((uint64_t)Fellow::cpu_hot_workers_usec());
	fellow_parallel->run(p_count, p_func, this, MIN(p_max_helpers, (uint32_t)Fellow::cpu_worker_tasks()));
}

_FORCE_INLINE_ void RenderForwardClustered::_fellow_make_instance_data(const GeometryInstanceSurfaceDataCache *surface, const GeometryInstanceForwardClustered *inst, SceneState::InstanceData &instance_data, bool p_full) const {
	// Stock's per-element instance data (moved out of _fill_instance_data's loop; also used by its worker ranges).
	// p_full false: only the fields before prev_transform (FELLOW_SHADOW_INSTANCE_BYTES; see cpu_compact_shadow_instances()).
	if (likely(inst->store_transform_cache)) {
		RendererRD::MaterialStorage::store_transform_transposed_3x4(inst->transform, instance_data.transform);
		if (p_full) {
			RendererRD::MaterialStorage::store_transform_transposed_3x4(inst->prev_transform, instance_data.prev_transform);
		}

#ifdef REAL_T_IS_DOUBLE
		// Split the origin into two components, the float approximation and the missing precision.
		// In the shader we will combine these back together to restore the lost precision.
		RendererRD::MaterialStorage::split_double(inst->transform.origin.x, &instance_data.transform[3], &instance_data.model_precision[0]);
		RendererRD::MaterialStorage::split_double(inst->transform.origin.y, &instance_data.transform[7], &instance_data.model_precision[1]);
		RendererRD::MaterialStorage::split_double(inst->transform.origin.z, &instance_data.transform[11], &instance_data.model_precision[2]);
		RendererRD::MaterialStorage::split_double(inst->prev_transform.origin.x, &instance_data.prev_transform[3], &instance_data.prev_model_precision[0]);
		RendererRD::MaterialStorage::split_double(inst->prev_transform.origin.y, &instance_data.prev_transform[7], &instance_data.prev_model_precision[1]);
		RendererRD::MaterialStorage::split_double(inst->prev_transform.origin.z, &instance_data.prev_transform[11], &instance_data.prev_model_precision[2]);
#endif
	} else {
		RendererRD::MaterialStorage::store_transform_transposed_3x4(Transform3D(), instance_data.transform);
		if (p_full) {
			RendererRD::MaterialStorage::store_transform_transposed_3x4(Transform3D(), instance_data.prev_transform);
		}
#ifdef REAL_T_IS_DOUBLE
		memset(instance_data.model_precision, 0, sizeof(instance_data.model_precision));
		memset(instance_data.prev_model_precision, 0, sizeof(instance_data.prev_model_precision));
#endif
	}

	instance_data.flags = inst->flags_cache;
	instance_data.gi_offset = inst->gi_offset_cache;
	instance_data.layer_mask = inst->layer_mask;
	instance_data.instance_uniforms_ofs = uint32_t(inst->shader_uniforms_offset);
	if (p_full) {
		instance_data.set_lightmap_uv_scale(inst->lightmap_uv_scale);
	}

	AABB surface_aabb = AABB(Vector3(0.0, 0.0, 0.0), Vector3(1.0, 1.0, 1.0));
	uint64_t format = RendererRD::MeshStorage::get_singleton()->mesh_surface_get_format(surface->surface);
	Vector4 uv_scale = Vector4(0.0, 0.0, 0.0, 0.0);

	if (format & RSE::ARRAY_FLAG_COMPRESS_ATTRIBUTES) {
		surface_aabb = RendererRD::MeshStorage::get_singleton()->mesh_surface_get_aabb(surface->surface);
		uv_scale = RendererRD::MeshStorage::get_singleton()->mesh_surface_get_uv_scale(surface->surface);
	}

	instance_data.set_compressed_aabb(surface_aabb);
	instance_data.set_uv_scale(uv_scale);
}

// Persistent instance data (decision 0022).

uint32_t RenderForwardClustered::_fellow_slot_alloc() {
	FellowPersistent &p = fellow_persistent;
	uint32_t slot;
	if (!p.free_slots.is_empty()) {
		slot = p.free_slots[p.free_slots.size() - 1];
		p.free_slots.resize(p.free_slots.size() - 1);
	} else {
		slot = p.slot_count++;
	}
	if (slot < p.valid.size()) {
		p.valid[slot] = 0;
	}
	return slot;
}

void RenderForwardClustered::_fellow_slot_free(uint32_t p_slot) {
	FellowPersistent &p = fellow_persistent;
	if (p_slot == UINT32_MAX) {
		return;
	}
	if (p_slot < p.valid.size()) {
		p.valid[p_slot] = 0;
	}
	p.free_slots.push_back(p_slot);
}

bool RenderForwardClustered::_fellow_persistent_begin() {
	// Render thread, before a list's instance data is written (no list's workers running).
	FellowPersistent &p = fellow_persistent;
	const uint64_t frame = RSG::rasterizer->get_frame_number();
	const bool new_frame = frame != p.frame;
	if (new_frame) {
		p.frame = frame;
		p.active = Fellow::cpu_persistent_instances();
	}
	if (p.active) {
		// A new buffer (slots handed out since the last list) is mapped by _fellow_persistent_reserve().
		if (!_fellow_persistent_reserve() && new_frame) {
			_fellow_persistent_map();
		}
	}
	return p.active;
}

bool RenderForwardClustered::_fellow_persistent_reserve() {
	// Room for every slot handed out so far; true if a new buffer was created (and mapped).
	FellowPersistent &p = fellow_persistent;
	const uint32_t needed = MAX(p.slot_count, 1024u);
	if (p.buffer.is_valid() && p.capacity >= needed) {
		return false;
	}
	const uint32_t capacity = Math::next_power_of_2(needed);
	RenderingDevice *rd = RD::get_singleton();
	if (p.buffer.is_valid()) {
		rd->free_rid(p.buffer); // freed once the frames using it are done
	}
	p.buffer = rd->storage_buffer_create(capacity * sizeof(SceneState::InstanceData), Vector<uint8_t>(), BitField<RD::StorageBufferUsage>(), RD::BUFFER_CREATION_DYNAMIC_PERSISTENT_BIT);
	p.capacity = capacity;
	const uint32_t old_size = p.valid.size();
	p.mirror.resize(capacity);
	p.valid.resize(capacity);
	for (uint32_t i = old_size; i < capacity; i++) {
		p.valid[i] = 0;
	}
	for (uint32_t m = 0; m < FellowPersistent::MAPS; m++) {
		p.written[m].resize(capacity);
		p.written_count[m].store(0, std::memory_order_relaxed);
		p.region_ptr[m] = nullptr;
		p.region_map[m] = 0;
	}
	p.region_count = 0;
	p.map_seq = 0;
	// Its regions are new: each gets every valid entry when first mapped.
	_fellow_persistent_map();
	return true;
}

void RenderForwardClustered::_fellow_persistent_map() {
	// Render thread, once per frame (and when a new buffer is created): the frame's region of the buffer, brought up to
	// date. The region was last mapped some maps ago (as many as there are frames in flight); the entries written since
	// then are written into it from the CPU copy. A region not seen before gets every valid entry.
	FellowPersistent &p = fellow_persistent;
	p.region = RD::get_singleton()->buffer_persistent_map_advance(p.buffer);
	const uint64_t map = p.map_seq++;
	int r = -1;
	for (uint32_t i = 0; i < p.region_count; i++) {
		if (p.region_ptr[i] == p.region) {
			r = int(i);
			break;
		}
	}
	const SceneState::InstanceData *mirror = p.mirror.ptr();
	SceneState::InstanceData *dst = reinterpret_cast<SceneState::InstanceData *>(p.region);
	uint32_t count = 0;
	if (r < 0 || map - p.region_map[r] > FellowPersistent::MAPS) {
		if (r < 0) {
			ERR_FAIL_COND_MSG(p.region_count >= FellowPersistent::MAPS, "FellowEngine persistent instance data: more regions than expected.");
			r = int(p.region_count++);
			p.region_ptr[r] = p.region;
		}
		for (uint32_t slot = 0; slot < p.slot_count; slot++) {
			if (p.valid[slot]) {
				dst[slot] = mirror[slot];
				count++;
			}
		}
	} else {
		for (uint64_t m = p.region_map[r] + 1; m < map; m++) {
			const uint32_t list = uint32_t(m % FellowPersistent::MAPS);
			const uint32_t n = p.written_count[list].load(std::memory_order_relaxed);
			const uint32_t *slots = p.written[list].ptr();
			for (uint32_t i = 0; i < n; i++) {
				const uint32_t slot = slots[i];
				if (p.valid[slot]) {
					dst[slot] = mirror[slot];
					count++;
				}
			}
		}
	}
	p.region_map[r] = map;
	p.written_count[map % FellowPersistent::MAPS].store(0, std::memory_order_relaxed);
	p.needs_flush = count > 0;
	Fellow::stats().cpu_persistent_catchup.add(count);
	Fellow::stats().cpu_persistent_maps.increment();
}

void RenderForwardClustered::_fellow_persistent_check(GeometryInstanceSurfaceDataCache *p_surface, const GeometryInstanceForwardClustered *p_inst, uint64_t p_frame) {
	// Any thread. The entry is current if it was built from the instance's current version (the instance's setters and
	// the previous transform's update change the version; a new mesh or material makes new surface caches, with new
	// entries): the common case only reads, so the surface's cache line stays shared between the threads that use it.
	FellowPersistent &p = fellow_persistent;
	const uint32_t slot = p_surface->fellow_slot;
	const uint64_t version = p_inst->fellow_data_version;
	if (p_surface->fellow_seen_version.load(std::memory_order_relaxed) == version && p.valid[slot]) {
		return;
	}
	// Changed: the first element of the frame that meets it rebuilds the entry (an exchange of the frame number).
	if (p_surface->fellow_checked_frame.exchange(p_frame, std::memory_order_relaxed) == p_frame) {
		return;
	}
	p_surface->fellow_seen_version.store(version, std::memory_order_relaxed);
	SceneState::InstanceData data;
	_fellow_make_instance_data(p_surface, p_inst, data, true);
	// Set per element, not kept in the entry (the shader reads them from the element).
	data.flags = 0;
	data.gi_offset = 0;
	// Stock leaves these uninitialised (never read).
	data.compressed_aabb_position[3] = 0.0f;
	data.compressed_aabb_size[3] = 0.0f;
	if (p.valid[slot] && memcmp(&p.mirror[slot], &data, sizeof(SceneState::InstanceData)) == 0) {
		return;
	}
	p.mirror[slot] = data;
	p.valid[slot] = 1;
	// Into this frame's region (the GPU reads it when the frame's draws run), and noted for the regions of the next
	// frames.
	memcpy(p.region + size_t(slot) * sizeof(SceneState::InstanceData), &data, sizeof(SceneState::InstanceData));
	const uint32_t list = uint32_t((p.map_seq - 1) % FellowPersistent::MAPS);
	p.written[list][p.written_count[list].fetch_add(1, std::memory_order_relaxed)] = slot;
	Fellow::stats().cpu_persistent_uploads.increment();
}

void RenderForwardClustered::_fellow_persistent_flush() {
	// Render thread, before draws that read the entries are recorded: non-coherent memory is flushed (a no-op where the
	// memory is coherent, as stock's buffer_flush()).
	FellowPersistent &p = fellow_persistent;
	if (p.needs_flush || p.written_count[(p.map_seq - 1) % FellowPersistent::MAPS].load(std::memory_order_relaxed) > 0) {
		RD::get_singleton()->buffer_flush(p.buffer);
		p.needs_flush = false;
	}
}

void RenderForwardClustered::_fellow_persistent_free() {
	FellowPersistent &p = fellow_persistent;
	if (p.buffer.is_valid()) {
		RD::get_singleton()->free_rid(p.buffer);
		p.buffer = RID();
	}
	p.region = nullptr;
}

// Occlusion statistics (decision 0026).

void RenderForwardClustered::_fellow_occlusion_free() {
	FellowOcclusion &o = fellow_occlusion;
	RenderingDevice *rd = RD::get_singleton();
	for (RID &level : o.hzb_levels) {
		if (level.is_valid()) {
			rd->free_rid(level);
		}
	}
	o.hzb_levels.clear();
	if (o.hzb.is_valid()) {
		rd->free_rid(o.hzb);
		o.hzb = RID();
	}
	if (o.element_buffer.is_valid()) {
		rd->free_rid(o.element_buffer);
		o.element_buffer = RID();
	}
	if (o.counters.is_valid()) {
		rd->free_rid(o.counters);
		o.counters = RID();
	}
	if (o.visible_by_slot.is_valid()) {
		rd->free_rid(o.visible_by_slot);
		o.visible_by_slot = RID();
	}
	if (o.hzb_version.is_valid()) {
		o.hzb_shader.version_free(o.hzb_version);
		o.hzb_version = RID();
	}
	if (o.test_version.is_valid()) {
		o.test_shader.version_free(o.test_version);
		o.test_version = RID();
	}
	for (RID *buffer : { &o.slot_bounds, &o.runs, &o.args, &o.compacted, &o.cull_counters }) {
		if (buffer->is_valid()) {
			rd->free_rid(*buffer);
			*buffer = RID();
		}
	}
	if (o.cull_version.is_valid()) {
		o.cull_shader.version_free(o.cull_version);
		o.cull_version = RID();
	}
	if (fellow_vrs.counters.is_valid()) {
		rd->free_rid(fellow_vrs.counters);
		fellow_vrs.counters = RID();
	}
	if (fellow_vrs.version.is_valid()) {
		fellow_vrs.shader.version_free(fellow_vrs.version);
		fellow_vrs.version = RID();
	}
}

void RenderForwardClustered::_fellow_occlusion_readback(const Vector<uint8_t> &p_data) {
	RenderForwardClustered *self = get_singleton();
	if (self == nullptr) {
		return;
	}
	FellowOcclusion &o = self->fellow_occlusion;
	const uint32_t n = p_data.size() / sizeof(uint32_t);
	const uint32_t *v = reinterpret_cast<const uint32_t *>(p_data.ptr());
	o.visible_cpu.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		o.visible_cpu[i] = v[i] != 0 ? 1 : 0;
	}
}

void RenderForwardClustered::_fellow_occlusion_filter(RenderListType p_list) {
	// Debug (wrong images): leave out the elements the last readback found hidden.
	FellowOcclusion &o = fellow_occlusion;
	RenderList &rl = render_list[p_list];
	if (p_list == RENDER_LIST_OPAQUE) {
		o.candidates = rl.elements;
		o.filtered = true;
	}
	const uint32_t n = o.visible_cpu.size();
	uint32_t kept = 0;
	for (uint32_t i = 0; i < rl.elements.size(); i++) {
		GeometryInstanceSurfaceDataCache *surface = rl.elements[i];
		const uint32_t slot = surface->fellow_slot;
		const bool multi = surface->owner->base_flags & (INSTANCE_DATA_FLAG_MULTIMESH | INSTANCE_DATA_FLAG_PARTICLES);
		if (multi || slot >= n || o.visible_cpu[slot]) {
			rl.elements[kept++] = surface;
		}
	}
	Fellow::stats().occlusion_culled.add(rl.elements.size() - kept);
	rl.elements.resize(kept);
	rl.element_info.resize(kept);
}

void RenderForwardClustered::_fellow_occlusion_init() {
	FellowOcclusion &o = fellow_occlusion;
	if (o.hzb_version.is_valid()) {
		return;
	}
	RenderingDevice *rd = RD::get_singleton();
	Vector<String> modes;
	modes.push_back("\n#define MODE_FROM_DEPTH\n");
	modes.push_back("\n");
	o.hzb_shader.initialize(modes);
	o.hzb_version = o.hzb_shader.version_create();
	for (int i = 0; i < 2; i++) {
		o.hzb_pipelines[i] = rd->compute_pipeline_create(o.hzb_shader.version_get_shader(o.hzb_version, i));
	}
	Vector<String> test_modes;
	test_modes.push_back("\n");
	o.test_shader.initialize(test_modes);
	o.test_version = o.test_shader.version_create();
	o.test_pipeline = rd->compute_pipeline_create(o.test_shader.version_get_shader(o.test_version, 0));
	o.counters = rd->storage_buffer_create(16);
	// Culling: the list elements and the persistent instance data entries are read from the frame's region of their
	// dynamic buffers.
	Vector<String> cull_modes;
	cull_modes.push_back("\n#define MODE_THREAD_PER_RUN\n");
	cull_modes.push_back("\n");
	Vector<uint64_t> dynamic_buffers;
	dynamic_buffers.push_back(ShaderRD::DynamicBuffer::encode(0, 2));
	dynamic_buffers.push_back(ShaderRD::DynamicBuffer::encode(0, 3));
	o.cull_shader.initialize(cull_modes, String(), Vector<RD::PipelineImmutableSampler>(), dynamic_buffers);
	o.cull_version = o.cull_shader.version_create();
	for (int i = 0; i < 2; i++) {
		o.cull_pipelines[i] = rd->compute_pipeline_create(o.cull_shader.version_get_shader(o.cull_version, i));
	}
	o.cull_counters = rd->storage_buffer_create(16);
}

bool RenderForwardClustered::_fellow_occlusion_pyramid(const Ref<RenderSceneBuffersRD> &p_render_buffers) {
	// The depth pyramid of the depth buffer as it is now.
	FellowOcclusion &o = fellow_occlusion;
	RenderingDevice *rd = RD::get_singleton();
	_fellow_occlusion_init();
	const Size2i screen = p_render_buffers->get_internal_size();
	if (screen.x <= 0 || screen.y <= 0) {
		return false;
	}
	// Level 0 covers 2 x 2 pixels per texel and has power-of-two sides, so that every level is exactly half the previous
	// one (texture levels round down); texels past the screen repeat its edge (the pyramid takes the minimum, so they change
	// nothing).
	const Size2i size0(Math::next_power_of_2((uint32_t)(screen.x + 1) / 2), Math::next_power_of_2((uint32_t)(screen.y + 1) / 2));
	if (o.hzb.is_null() || o.hzb_size != size0) {
		for (RID &level : o.hzb_levels) {
			rd->free_rid(level);
		}
		o.hzb_levels.clear();
		if (o.hzb.is_valid()) {
			rd->free_rid(o.hzb);
		}
		uint32_t levels = 1;
		for (int d = MAX(size0.x, size0.y); d > 1; d /= 2) {
			levels++;
		}
		RD::TextureFormat tf;
		tf.format = RD::DATA_FORMAT_R32_SFLOAT;
		tf.width = size0.x;
		tf.height = size0.y;
		tf.mipmaps = levels;
		tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
		o.hzb = rd->texture_create(tf, RD::TextureView());
		for (uint32_t m = 0; m < levels; m++) {
			o.hzb_levels.push_back(rd->texture_create_shared_from_slice(RD::TextureView(), o.hzb, 0, m));
		}
		o.hzb_size = size0;
	}

	RID sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	UniformSetCacheRD *cache = UniformSetCacheRD::get_singleton();
	RD::ComputeListID list = rd->compute_list_begin();
	Size2i source = screen;
	for (uint32_t m = 0; m < o.hzb_levels.size(); m++) {
		const Size2i dest = m == 0 ? size0 : Size2i(MAX(1, source.x / 2), MAX(1, source.y / 2));
		const int mode = m == 0 ? 0 : 1;
		RID shader = o.hzb_shader.version_get_shader(o.hzb_version, mode);
		rd->compute_list_bind_compute_pipeline(list, o.hzb_pipelines[mode]);
		if (m == 0) {
			RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ sampler, p_render_buffers->get_depth_texture(0) }));
			rd->compute_list_bind_uniform_set(list, cache->get_cache(shader, 0, u_depth), 0);
		} else {
			RD::Uniform u_source(RD::UNIFORM_TYPE_IMAGE, 0, o.hzb_levels[m - 1]);
			rd->compute_list_bind_uniform_set(list, cache->get_cache(shader, 0, u_source), 0);
		}
		RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 0, o.hzb_levels[m]);
		rd->compute_list_bind_uniform_set(list, cache->get_cache(shader, 1, u_dest), 1);
		int32_t push[4] = { source.x, source.y, dest.x, dest.y };
		rd->compute_list_set_push_constant(list, push, sizeof(push));
		rd->compute_list_dispatch_threads(list, dest.x, dest.y, 1);
		rd->compute_list_add_barrier(list);
		source = dest;
	}
	rd->compute_list_end();
	return true;
}

void RenderForwardClustered::_fellow_occlusion_set_slot_bounds(uint32_t p_slot, void *p_mesh_surface) {
	// The surface's local bounding box for its slot (uploaded with the next culled frame).
	FellowOcclusion &o = fellow_occlusion;
	if (p_slot == UINT32_MAX) {
		return;
	}
	if (o.slot_bounds_cpu.size() < (p_slot + 1) * 8) {
		const uint32_t old_size = o.slot_bounds_cpu.size();
		o.slot_bounds_cpu.resize(Math::next_power_of_2((p_slot + 1) * 8));
		for (uint32_t i = old_size; i < o.slot_bounds_cpu.size(); i++) {
			o.slot_bounds_cpu[i] = 0.0f;
		}
	}
	float *b = o.slot_bounds_cpu.ptr() + p_slot * 8;
	if (p_mesh_surface != nullptr) {
		const AABB aabb = RendererRD::MeshStorage::get_singleton()->mesh_surface_get_aabb(p_mesh_surface);
		b[0] = aabb.position.x;
		b[1] = aabb.position.y;
		b[2] = aabb.position.z;
		b[3] = 1.0f;
		b[4] = aabb.position.x + aabb.size.x;
		b[5] = aabb.position.y + aabb.size.y;
		b[6] = aabb.position.z + aabb.size.z;
		b[7] = 0.0f;
	} else {
		for (int i = 0; i < 8; i++) {
			b[i] = 0.0f;
		}
	}
	o.bounds_dirty_from = MIN(o.bounds_dirty_from, p_slot);
	o.bounds_dirty_to = MAX(o.bounds_dirty_to, p_slot + 1);
}

void RenderForwardClustered::_fellow_occlusion_cull_readback(const Vector<uint8_t> &p_data) {
	if (p_data.size() < 16) {
		return;
	}
	const uint32_t *c = reinterpret_cast<const uint32_t *>(p_data.ptr());
	Fellow::stats().occlusion_cull_elements.add(c[0]);
	Fellow::stats().occlusion_cull_hidden.add(c[1]);
	Fellow::stats().occlusion_cull_indices.add(c[2]);
	Fellow::stats().occlusion_cull_hidden_indices.add(c[3]);
}

bool RenderForwardClustered::_fellow_occlusion_cull(RenderDataRD *p_render_data, uint32_t p_color_pass_flags) {
	// Occlusion culling of the opaque colour pass (decision 0026), after the full depth pre-pass. Returns true when the
	// pass's runs were tested (fellow_occlusion.run_of_element then marks the runs drawn indirectly).
	FellowOcclusion &o = fellow_occlusion;
	RenderingDevice *rd = RD::get_singleton();
	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	const RenderList &rl = render_list[RENDER_LIST_OPAQUE];
	const uint32_t count = rl.elements.size();
	if (count == 0 || !fellow_persistent.active || fellow_persistent.buffer.is_null()) {
		return false;
	}

	// The runs the colour pass draws (as _render_list_template() walks the list), tested where every element can be
	// bounded by its surface's box under its instance's transform and nothing but the depth test decides its pixels.
	o.run_data.clear();
	o.long_runs.clear();
	o.run_of_element.resize(count);
	memset(o.run_of_element.ptr(), 0xFF, count * sizeof(uint32_t));
	static constexpr uint32_t LONG_RUN = 16; // elements; longer runs get a workgroup each
	const uint64_t uncullable_owner = INSTANCE_DATA_FLAG_MULTIMESH | INSTANCE_DATA_FLAG_PARTICLES;
	uint32_t short_count = 0;
	for (uint32_t i = 0; i < count;) {
		const GeometryInstanceSurfaceDataCache *surf = rl.elements[i];
		const RenderElementInfo &info = rl.element_info[i];
		const uint32_t n = MAX(1u, uint32_t(info.repeat));
		const GeometryInstanceForwardClustered *inst = surf->owner;
		const SceneShaderForwardClustered::ShaderData *shader = surf->shader;
		bool cullable = surf->surface != nullptr && shader != nullptr && inst->instance_count == 1 && !inst->mesh_instance.is_valid() && !(inst->base_flags & uncullable_owner);
		if (cullable && surf->color_pass_inclusion_mask && (p_color_pass_flags & surf->color_pass_inclusion_mask) == 0) {
			cullable = false;
		}
		if (cullable) {
			cullable = !(shader->uses_vertex || shader->uses_position || shader->writes_modelview_or_projection || shader->uses_z_clip_scale || shader->uses_particle_trails || shader->uses_world_coordinates || shader->uses_point_size || shader->fellow_writes_depth || shader->fellow_uses_instance_id || shader->stencil_enabled || shader->depth_test != SceneShaderForwardClustered::ShaderData::DEPTH_TEST_ENABLED) && !(surf->flags & GeometryInstanceSurfaceDataCache::FLAG_USES_STENCIL);
		}
		uint32_t index_count = 0;
		if (cullable) {
			index_count = mesh_storage->fellow_mesh_surface_get_index_count(surf->surface, info.lod_index);
			cullable = index_count > 0;
		}
		if (cullable) {
			if (n > LONG_RUN) {
				o.run_of_element[i] = 0x80000000u | o.long_runs.size(); // fixed below
				o.long_runs.push_back(i);
			} else {
				o.run_of_element[i] = short_count++;
				o.run_data.push_back(i);
				o.run_data.push_back(n);
				o.run_data.push_back(index_count);
				o.run_data.push_back(0);
			}
		}
		i += n;
	}
	for (uint32_t k = 0; k < o.long_runs.size(); k++) {
		const uint32_t i = o.long_runs[k];
		const RenderElementInfo &info = rl.element_info[i];
		o.run_of_element[i] = short_count + k;
		o.run_data.push_back(i);
		o.run_data.push_back(MAX(1u, uint32_t(info.repeat)));
		o.run_data.push_back(mesh_storage->fellow_mesh_surface_get_index_count(rl.elements[i]->surface, info.lod_index));
		o.run_data.push_back(0);
	}
	const uint32_t run_count = o.run_data.size() / 4;
	if (run_count == 0) {
		return false;
	}

	Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	if (!_fellow_occlusion_pyramid(rb)) {
		return false;
	}

	// Buffers.
	if (o.run_capacity < run_count) {
		for (RID *buffer : { &o.runs, &o.args }) {
			if (buffer->is_valid()) {
				rd->free_rid(*buffer);
			}
		}
		o.run_capacity = Math::next_power_of_2(MAX(run_count, 1024u));
		o.runs = rd->storage_buffer_create(o.run_capacity * 4 * sizeof(uint32_t));
		o.args = rd->storage_buffer_create(o.run_capacity * 5 * sizeof(uint32_t), Span<uint8_t>(), RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
	}
	if (o.compacted_capacity < count) {
		if (o.compacted.is_valid()) {
			rd->free_rid(o.compacted);
		}
		o.compacted_capacity = Math::next_power_of_2(MAX(count, 1024u));
		o.compacted = rd->storage_buffer_create(o.compacted_capacity * 4 * sizeof(uint32_t));
	}
	const uint32_t slot_capacity = o.slot_bounds_cpu.size() / 8;
	if (o.slot_bounds_capacity < slot_capacity || o.slot_bounds.is_null()) {
		if (o.slot_bounds.is_valid()) {
			rd->free_rid(o.slot_bounds);
		}
		o.slot_bounds_capacity = MAX(slot_capacity, 1024u);
		if (o.slot_bounds_cpu.size() < o.slot_bounds_capacity * 8) {
			const uint32_t old_size = o.slot_bounds_cpu.size();
			o.slot_bounds_cpu.resize(o.slot_bounds_capacity * 8);
			for (uint32_t i = old_size; i < o.slot_bounds_cpu.size(); i++) {
				o.slot_bounds_cpu[i] = 0.0f;
			}
		}
		o.slot_bounds = rd->storage_buffer_create(o.slot_bounds_capacity * 8 * sizeof(float), Span<uint8_t>((const uint8_t *)o.slot_bounds_cpu.ptr(), o.slot_bounds_capacity * 8 * sizeof(float)));
		Fellow::stats().occlusion_cull_slot_uploads.add(o.slot_bounds_capacity);
		o.bounds_dirty_from = UINT32_MAX;
		o.bounds_dirty_to = 0;
	} else if (o.bounds_dirty_from < o.bounds_dirty_to) {
		rd->buffer_update(o.slot_bounds, o.bounds_dirty_from * 8 * sizeof(float), (o.bounds_dirty_to - o.bounds_dirty_from) * 8 * sizeof(float), o.slot_bounds_cpu.ptr() + o.bounds_dirty_from * 8);
		Fellow::stats().occlusion_cull_slot_uploads.add(o.bounds_dirty_to - o.bounds_dirty_from);
		o.bounds_dirty_from = UINT32_MAX;
		o.bounds_dirty_to = 0;
	}
	rd->buffer_update(o.runs, 0, run_count * 4 * sizeof(uint32_t), o.run_data.ptr());
	rd->buffer_clear(o.cull_counters, 0, 16);

	// The test and the compaction.
	struct {
		float view_projection[16];
		float screen_size[2];
		uint32_t levels;
		uint32_t run_count;
		uint32_t run_offset;
		uint32_t dispatch_width;
		uint32_t slot_vec4s;
		uint32_t pad;
	} push;
	const Projection view_projection = p_render_data->scene_data->get_cam_projection() * Projection(p_render_data->scene_data->cam_transform.affine_inverse());
	RendererRD::MaterialStorage::store_camera(view_projection, push.view_projection);
	const Size2i screen = rb->get_internal_size();
	push.screen_size[0] = screen.x;
	push.screen_size[1] = screen.y;
	push.levels = o.hzb_levels.size();
	push.slot_vec4s = sizeof(SceneState::InstanceData) / 16;
	push.pad = 0;
	RID sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RD::Uniform u_hzb(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ sampler, o.hzb }));
	RD::Uniform u_runs(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, o.runs);
	RD::Uniform u_source(RD::UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC, 2, scene_state.instance_buffer[RENDER_LIST_OPAQUE]._get(0u));
	RD::Uniform u_slots(RD::UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC, 3, fellow_persistent.buffer);
	RD::Uniform u_bounds(RD::UNIFORM_TYPE_STORAGE_BUFFER, 4, o.slot_bounds);
	RD::Uniform u_compacted(RD::UNIFORM_TYPE_STORAGE_BUFFER, 5, o.compacted);
	RD::Uniform u_args(RD::UNIFORM_TYPE_STORAGE_BUFFER, 6, o.args);
	RD::Uniform u_counters(RD::UNIFORM_TYPE_STORAGE_BUFFER, 7, o.cull_counters);
	UniformSetCacheRD *cache = UniformSetCacheRD::get_singleton();
	RD::ComputeListID list = rd->compute_list_begin();
	const uint32_t long_count = o.long_runs.size();
	for (int mode = 0; mode < 2; mode++) {
		const uint32_t mode_runs = mode == 0 ? short_count : long_count;
		if (mode_runs == 0) {
			continue;
		}
		RID shader = o.cull_shader.version_get_shader(o.cull_version, mode);
		rd->compute_list_bind_compute_pipeline(list, o.cull_pipelines[mode]);
		rd->compute_list_bind_uniform_set(list, cache->get_cache(shader, 0, u_hzb, u_runs, u_source, u_slots, u_bounds, u_compacted, u_args, u_counters), 0);
		push.run_count = mode_runs;
		push.run_offset = mode == 0 ? 0 : short_count;
		if (mode == 0) {
			push.dispatch_width = 0;
			rd->compute_list_set_push_constant(list, &push, sizeof(push));
			rd->compute_list_dispatch_threads(list, mode_runs, 1, 1);
		} else {
			const uint32_t width = MIN(mode_runs, 65535u);
			push.dispatch_width = width;
			rd->compute_list_set_push_constant(list, &push, sizeof(push));
			rd->compute_list_dispatch(list, width, (mode_runs + width - 1) / width, 1);
		}
	}
	rd->compute_list_end();
	rd->buffer_get_data_async(o.cull_counters, callable_mp_static(&RenderForwardClustered::_fellow_occlusion_cull_readback), 0, 16);

	Fellow::stats().occlusion_cull_passes.increment();
	Fellow::stats().occlusion_cull_runs.add(run_count);
	return true;
}

bool RenderForwardClustered::_fellow_adaptive_vrs_target(const Ref<RenderSceneBuffersRD> &p_render_buffers) const {
	// The render buffers got an attachment for adaptive shading (decision 0027): a viewport without a VRS mode of its own.
	if (p_render_buffers.is_null() || !p_render_buffers->has_texture(RB_SCOPE_VRS, RB_TEXTURE) || p_render_buffers->get_view_count() != 1) {
		return false;
	}
	RID render_target = p_render_buffers->get_render_target();
	return render_target.is_valid() && RendererRD::TextureStorage::get_singleton()->render_target_get_vrs_mode(render_target) == RSE::VIEWPORT_VRS_DISABLED && RD::get_singleton()->vrs_get_format() == RD::DATA_FORMAT_R8_UINT;
}

void RenderForwardClustered::_fellow_adaptive_vrs_readback(const Vector<uint8_t> &p_data) {
	if (p_data.size() < 16) {
		return;
	}
	const uint32_t *c = reinterpret_cast<const uint32_t *>(p_data.ptr());
	Fellow::stats().vrs_tiles.add(c[0]);
	Fellow::stats().vrs_tiles_coarse.add(c[1]);
	Fellow::stats().vrs_pixels.add(c[2]);
	Fellow::stats().vrs_fragments.add(c[3]);
}

void RenderForwardClustered::_fellow_adaptive_vrs_dispatch(const Ref<RenderSceneBuffersRD> &p_render_buffers, int p_mode) {
	// p_mode 0: rates from the final image (estimate, then dilate into the attachment); 2: every tile at full rate.
	FellowVRS &v = fellow_vrs;
	RenderingDevice *rd = RD::get_singleton();
	RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();
	if (v.version.is_null()) {
		Vector<String> modes;
		modes.push_back("\n#define MODE_ESTIMATE\n");
		modes.push_back("\n#define MODE_DILATE\n");
		modes.push_back("\n#define MODE_CLEAR\n");
		v.shader.initialize(modes);
		v.version = v.shader.version_create();
		for (int i = 0; i < 3; i++) {
			v.pipelines[i] = rd->compute_pipeline_create(v.shader.version_get_shader(v.version, i));
		}
		v.counters = rd->storage_buffer_create(16);
	}
	const Size2i texel = rd->vrs_get_texel_size();
	if (texel.x <= 0 || texel.y <= 0 || texel.x > 32 || texel.y > 32) {
		return;
	}
	const Size2i internal_size = p_render_buffers->get_internal_size();
	const Size2i tiles((internal_size.x + texel.x - 1) / texel.x, (internal_size.y + texel.y - 1) / texel.y);
	RID shading_rates = p_render_buffers->get_texture(RB_SCOPE_VRS, RB_TEXTURE);
	struct {
		int32_t tile_size[2];
		int32_t tiles[2];
		int32_t screen_size[2];
		float source_scale[2];
		int32_t source_size[2];
		float threshold;
		uint32_t max_log2;
		uint32_t linear_source;
		uint32_t pad[3];
	} push = {};
	push.tile_size[0] = texel.x;
	push.tile_size[1] = texel.y;
	push.tiles[0] = tiles.x;
	push.tiles[1] = tiles.y;
	push.screen_size[0] = internal_size.x;
	push.screen_size[1] = internal_size.y;
	push.threshold = Fellow::adaptive_vrs_threshold();
	push.max_log2 = Fellow::adaptive_vrs_max_rate() >= 1 ? 2 : 1;
	UniformSetCacheRD *cache = UniformSetCacheRD::get_singleton();
	RD::Uniform u_rates_out(RD::UNIFORM_TYPE_IMAGE, 2, shading_rates);
	RD::ComputeListID list = rd->compute_list_begin();
	if (p_mode == 2) {
		RID shader = v.shader.version_get_shader(v.version, 2);
		rd->compute_list_bind_compute_pipeline(list, v.pipelines[2]);
		rd->compute_list_bind_uniform_set(list, cache->get_cache(shader, 0, u_rates_out), 0);
		rd->compute_list_set_push_constant(list, &push, sizeof(push));
		rd->compute_list_dispatch_threads(list, tiles.x, tiles.y, 1);
		rd->compute_list_end();
		return;
	}
	RID render_target = p_render_buffers->get_render_target();
	RID source = texture_storage->render_target_get_rd_texture(render_target);
	const Size2i source_size = texture_storage->render_target_get_size(render_target);
	if (source.is_null() || source_size.x <= 0 || source_size.y <= 0) {
		rd->compute_list_end();
		return;
	}
	push.source_size[0] = source_size.x;
	push.source_size[1] = source_size.y;
	push.source_scale[0] = float(source_size.x) / float(internal_size.x);
	push.source_scale[1] = float(source_size.y) / float(internal_size.y);
	push.linear_source = texture_storage->render_target_is_using_hdr(render_target) ? 1 : 0;
	if (!p_render_buffers->has_texture(SNAME("fellow_vrs"), SNAME("tile_rates"))) {
		p_render_buffers->create_texture(SNAME("fellow_vrs"), SNAME("tile_rates"), RD::DATA_FORMAT_R8_UINT, RD::TEXTURE_USAGE_STORAGE_BIT, RD::TEXTURE_SAMPLES_1, tiles);
	}
	RID tile_rates = p_render_buffers->get_texture(SNAME("fellow_vrs"), SNAME("tile_rates"));
	RID sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RD::Uniform u_source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ sampler, source }));
	RD::Uniform u_tile_rates(RD::UNIFORM_TYPE_IMAGE, 1, tile_rates);
	RD::Uniform u_counters(RD::UNIFORM_TYPE_STORAGE_BUFFER, 3, v.counters);
	rd->buffer_clear(v.counters, 0, 16);
	RID shader = v.shader.version_get_shader(v.version, 0);
	rd->compute_list_bind_compute_pipeline(list, v.pipelines[0]);
	rd->compute_list_bind_uniform_set(list, cache->get_cache(shader, 0, u_source, u_tile_rates), 0);
	rd->compute_list_set_push_constant(list, &push, sizeof(push));
	rd->compute_list_dispatch(list, tiles.x, tiles.y, 1);
	rd->compute_list_add_barrier(list);
	shader = v.shader.version_get_shader(v.version, 1);
	rd->compute_list_bind_compute_pipeline(list, v.pipelines[1]);
	rd->compute_list_bind_uniform_set(list, cache->get_cache(shader, 0, u_tile_rates, u_rates_out, u_counters), 0);
	rd->compute_list_set_push_constant(list, &push, sizeof(push));
	rd->compute_list_dispatch_threads(list, tiles.x, tiles.y, 1);
	rd->compute_list_end();
	rd->buffer_get_data_async(v.counters, callable_mp_static(&RenderForwardClustered::_fellow_adaptive_vrs_readback), 0, 16);
	Fellow::stats().vrs_frames.increment();
}

void RenderForwardClustered::_fellow_adaptive_vrs_begin(const Ref<RenderSceneBuffersRD> &p_render_buffers) {
	// The attachment's first frame (new render buffers): every tile at full rate until the first final image.
	if (!_fellow_adaptive_vrs_target(p_render_buffers) || p_render_buffers->has_texture(SNAME("fellow_vrs"), SNAME("tile_rates"))) {
		return;
	}
	const Size2i texel = RD::get_singleton()->vrs_get_texel_size();
	if (texel.x <= 0 || texel.y <= 0) {
		return;
	}
	const Size2i internal_size = p_render_buffers->get_internal_size();
	p_render_buffers->create_texture(SNAME("fellow_vrs"), SNAME("tile_rates"), RD::DATA_FORMAT_R8_UINT, RD::TEXTURE_USAGE_STORAGE_BIT, RD::TEXTURE_SAMPLES_1, Size2i((internal_size.x + texel.x - 1) / texel.x, (internal_size.y + texel.y - 1) / texel.y));
	_fellow_adaptive_vrs_dispatch(p_render_buffers, 2);
}

void RenderForwardClustered::_fellow_adaptive_vrs_update(const Ref<RenderSceneBuffersRD> &p_render_buffers) {
	// After the frame's final image: the next frame's shading rates.
	if (!_fellow_adaptive_vrs_target(p_render_buffers)) {
		return;
	}
	_fellow_adaptive_vrs_dispatch(p_render_buffers, Fellow::adaptive_vrs_threshold() > 0.0f ? 0 : 2);
}

void RenderForwardClustered::_fellow_occlusion_stats(RenderDataRD *p_render_data) {
	FellowOcclusion &o = fellow_occlusion;
	RenderingDevice *rd = RD::get_singleton();
	Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	if (rb.is_null()) {
		return;
	}
	const bool approx = o.filtered; // the opaque list was filtered this frame: test the unfiltered one
	const LocalVector<GeometryInstanceSurfaceDataCache *> &tested = approx ? o.candidates : render_list[RENDER_LIST_OPAQUE].elements;
	const uint32_t count = tested.size();
	if (count == 0) {
		return;
	}
	if (!_fellow_occlusion_pyramid(rb)) {
		return;
	}
	const Size2i screen = rb->get_internal_size();
	RID sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	UniformSetCacheRD *cache = UniformSetCacheRD::get_singleton();
	RD::ComputeListID list;

	// The elements: world AABB (the instance's) and how many indices their surface draws at its full detail.
	o.elements.resize(count * 8);
	float *e = o.elements.ptr();
	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	uint32_t max_slot = 0;
	for (uint32_t i = 0; i < count; i++) {
		const GeometryInstanceSurfaceDataCache *surface = tested[i];
		const AABB &aabb = surface->owner->transformed_aabb;
		const uint32_t weight = mesh_storage->mesh_surface_get_vertices_drawn_count(surface->surface);
		float *f = e + i * 8;
		f[0] = aabb.position.x;
		f[1] = aabb.position.y;
		f[2] = aabb.position.z;
		memcpy(&f[3], &weight, sizeof(uint32_t));
		f[4] = aabb.position.x + aabb.size.x;
		f[5] = aabb.position.y + aabb.size.y;
		f[6] = aabb.position.z + aabb.size.z;
		const uint32_t slot = surface->fellow_slot;
		memcpy(&f[7], &slot, sizeof(uint32_t));
		max_slot = MAX(max_slot, slot);
	}
	if (o.visible_capacity <= max_slot) {
		if (o.visible_by_slot.is_valid()) {
			rd->free_rid(o.visible_by_slot);
		}
		o.visible_capacity = Math::next_power_of_2(MAX(max_slot + 1, 1024u));
		LocalVector<uint32_t> ones;
		ones.resize(o.visible_capacity);
		for (uint32_t &v : ones) {
			v = 1;
		}
		o.visible_by_slot = rd->storage_buffer_create(o.visible_capacity * sizeof(uint32_t), Span<uint8_t>((const uint8_t *)ones.ptr(), o.visible_capacity * sizeof(uint32_t)));
	}
	if (o.element_capacity < count) {
		if (o.element_buffer.is_valid()) {
			rd->free_rid(o.element_buffer);
		}
		o.element_capacity = Math::next_power_of_2(MAX(count, 1024u));
		o.element_buffer = rd->storage_buffer_create(o.element_capacity * 8 * sizeof(float));
	}
	rd->buffer_update(o.element_buffer, 0, count * 8 * sizeof(float), e);
	const uint32_t zeros[4] = {};
	rd->buffer_update(o.counters, 0, sizeof(zeros), zeros);

	struct {
		float view_projection[16];
		uint32_t screen_size[2];
		uint32_t count;
		uint32_t levels;
	} push;
	const Projection view_projection = p_render_data->scene_data->get_cam_projection() * Projection(p_render_data->scene_data->cam_transform.affine_inverse());
	RendererRD::MaterialStorage::store_camera(view_projection, push.view_projection);
	push.screen_size[0] = screen.x;
	push.screen_size[1] = screen.y;
	push.count = count;
	push.levels = o.hzb_levels.size();
	RID shader = o.test_shader.version_get_shader(o.test_version, 0);
	RD::Uniform u_hzb(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ sampler, o.hzb }));
	RD::Uniform u_elements(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, o.element_buffer);
	RD::Uniform u_counters(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, o.counters);
	RD::Uniform u_visible(RD::UNIFORM_TYPE_STORAGE_BUFFER, 3, o.visible_by_slot);
	list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, o.test_pipeline);
	rd->compute_list_bind_uniform_set(list, cache->get_cache(shader, 0, u_hzb, u_elements, u_counters, u_visible), 0);
	rd->compute_list_set_push_constant(list, &push, sizeof(push));
	rd->compute_list_dispatch_threads(list, count, 1, 1);
	rd->compute_list_end();

	if (approx) {
		// No wait: the visibility comes back in a later frame (and is applied to the frames after that).
		rd->buffer_get_data_async(o.visible_by_slot, callable_mp_static(&RenderForwardClustered::_fellow_occlusion_readback), 0, (max_slot + 1) * sizeof(uint32_t));
		return;
	}

	// Read back (debug: waits for the GPU).
	const Vector<uint8_t> result = rd->buffer_get_data(o.counters);
	if (result.size() >= 16) {
		const uint32_t *c = reinterpret_cast<const uint32_t *>(result.ptr());
		Fellow::stats().occlusion_tested.add(c[0]);
		Fellow::stats().occlusion_weight.add(c[1]);
		Fellow::stats().occlusion_hidden.add(c[2]);
		Fellow::stats().occlusion_hidden_weight.add(c[3]);
	}
}

uint32_t RenderForwardClustered::_fellow_verify_elements(RenderListType p_render_list, uint32_t p_from, uint32_t p_size, const uint32_t *p_flags) {
	// Persistent instance data: stock's instance data of each element, compared with its entry as the GPU reads it (in
	// this frame's region of the persistent buffer) and its element.
	RenderList *rl = &render_list[p_render_list];
	const FellowElement *elements = reinterpret_cast<const FellowElement *>(scene_state.curr_gpu_ptr[p_render_list]);
	const FellowPersistent &p = fellow_persistent;
	uint32_t mismatches = 0;
	for (uint32_t k = 0; k < p_size; k++) {
		const uint32_t i = p_from + k;
		const GeometryInstanceSurfaceDataCache *surface = rl->elements[i];
		const GeometryInstanceForwardClustered *inst = surface->owner;
		SceneState::InstanceData expected;
		_fellow_make_instance_data(surface, inst, expected, true);
		expected.flags = p_flags ? p_flags[k] : inst->flags_cache;
		expected.compressed_aabb_position[3] = 0.0f;
		expected.compressed_aabb_size[3] = 0.0f;
		const FellowElement element = elements[i];
		if (element.slot != surface->fellow_slot || element.slot >= p.capacity || !p.valid[element.slot]) {
			mismatches++;
			continue;
		}
		SceneState::InstanceData actual;
		memcpy(&actual, p.region + size_t(element.slot) * sizeof(SceneState::InstanceData), sizeof(SceneState::InstanceData));
		actual.flags = element.flags;
		actual.gi_offset = element.gi_offset;
		if (memcmp(&expected, &actual, sizeof(SceneState::InstanceData)) != 0) {
			mismatches++;
		}
	}
	return mismatches;
}

uint32_t RenderForwardClustered::_fellow_verify_sort(RenderListType p_render_list, uint32_t p_from, uint32_t p_size, LocalVector<GeometryInstanceSurfaceDataCache *> &p_unsorted) {
	// Stock's sort of the same elements by the keys the surfaces hold, compared with the list.
	SortArray<GeometryInstanceSurfaceDataCache *, RenderList::SortByKey> sorter;
	sorter.sort(p_unsorted.ptr(), p_size);
	uint32_t mismatches = 0;
	const GeometryInstanceSurfaceDataCache *const *e = render_list[p_render_list].elements.ptr() + p_from;
	for (uint32_t i = 0; i < p_size; i++) {
		if (e[i] != p_unsorted[i]) {
			mismatches++;
		}
	}
	return mismatches;
}

uint32_t RenderForwardClustered::_fellow_verify_instances(RenderListType p_render_list, uint32_t p_from, uint32_t p_size, const uint32_t *p_flags, bool p_compact) {
	// Stock's instance data, run lengths and element values for the list range, compared with what was written. The
	// last component of the compressed AABB position and size is never written (stock leaves it uninitialised).
	RenderList *rl = &render_list[p_render_list];
	const SceneState::InstanceData *gpu = scene_state.curr_gpu_ptr[p_render_list];
	uint32_t mismatches = 0;
	uint32_t repeats = 0;
	const GeometryInstanceSurfaceDataCache *prev_surface = nullptr;
	LocalVector<uint32_t> expected_repeat;
	expected_repeat.resize(p_size);
	for (uint32_t k = 0; k < p_size; k++) {
		const uint32_t i = p_from + k;
		const GeometryInstanceSurfaceDataCache *surface = rl->elements[i];
		const GeometryInstanceForwardClustered *inst = surface->owner;
		SceneState::InstanceData expected;
		_fellow_make_instance_data(surface, inst, expected, true);
		const uint32_t flags = p_flags ? p_flags[k] : inst->flags_cache;
		expected.flags = flags;
		expected.compressed_aabb_position[3] = gpu[i].compressed_aabb_position[3];
		expected.compressed_aabb_size[3] = gpu[i].compressed_aabb_size[3];
		if (memcmp(&expected, &gpu[i], p_compact ? FELLOW_SHADOW_INSTANCE_BYTES : sizeof(SceneState::InstanceData)) != 0) {
			mismatches++;
		}
		if (rl->element_info[i].value != uint32_t(surface->sort.sort_key1 & 0xFFF) && p_flags == nullptr) {
			mismatches++;
		}
		const bool cant_repeat = flags & INSTANCE_DATA_FLAG_MULTIMESH || inst->mesh_instance.is_valid();
		if (prev_surface != nullptr && !cant_repeat && prev_surface->sort.sort_key1 == surface->sort.sort_key1 && prev_surface->sort.sort_key2 == surface->sort.sort_key2 && inst->mirror == prev_surface->owner->mirror && repeats < RenderElementInfo::MAX_REPEATS) {
			repeats++;
		} else {
			for (uint32_t j = 1; j <= repeats; j++) {
				expected_repeat[k - j] = j;
			}
			repeats = 1;
		}
		prev_surface = cant_repeat ? nullptr : surface;
	}
	for (uint32_t j = 1; j <= repeats; j++) {
		expected_repeat[p_size - j] = j;
	}
	for (uint32_t k = 0; k < p_size; k++) {
		if (rl->element_info[p_from + k].repeat != expected_repeat[k]) {
			mismatches++;
		}
	}
	return mismatches;
}

void RenderForwardClustered::_fellow_verify_count(uint32_t p_mismatches) {
	Fellow::stats().cpu_verify_checks.increment();
	if (p_mismatches) {
		Fellow::stats().cpu_verify_mismatches.add(p_mismatches);
		WARN_PRINT_ONCE(vformat("FellowEngine CPU list verification: %d mismatches against stock's results.", p_mismatches));
	}
}

void RenderForwardClustered::_fill_instance_data(RenderListType p_render_list, int *p_render_info, uint32_t p_offset, int32_t p_max_elements, bool p_update_buffer) {
	RenderList *rl = &render_list[p_render_list];
	uint32_t element_total = p_max_elements >= 0 ? uint32_t(p_max_elements) : rl->elements.size();

	rl->element_info.resize(p_offset + element_total);

	// If p_offset == 0, grow_instance_buffer resets and increment the buffer.
	// If this behavior ever changes, _render_shadow_begin may need to change.
	scene_state.grow_instance_buffer(p_render_list, p_offset + element_total, p_offset != 0u);
	if (!scene_state.curr_gpu_ptr[p_render_list] && element_total > 0u) {
		// The old buffer was replaced for another larger one. We must start copying from scratch.
		element_total += p_offset;
		p_offset = 0u;
		scene_state.curr_gpu_ptr[p_render_list] = reinterpret_cast<SceneState::InstanceData *>(scene_state.instance_buffer[p_render_list].map_raw_for_upload(0u));
	}

	if (p_render_info) {
		p_render_info[RSE::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME] += element_total;
	}

	// Persistent instance data (decision 0022): 16-byte elements, the surfaces' entries checked and the changed ones
	// uploaded before the list's draws are recorded.
	const bool persistent = _fellow_persistent_begin();
	if (persistent) {
		Fellow::stats().cpu_persistent_elements.add(element_total);
	}

	if (Fellow::cpu_parallel_instance_data() && element_total >= FELLOW_PARALLEL_LIST_MIN) {
		// Worker ranges write the instance data and each element's "same draw as the previous element" test; the run
		// lengths (instancing repeats) and the draw count follow in one pass, as stock computes them.
		fellow_instance_job.render_list = p_render_list;
		fellow_instance_job.offset = p_offset;
		fellow_instance_job.total = element_total;
		fellow_instance_job.chunk_size = FELLOW_PARALLEL_LIST_CHUNK;
		fellow_instance_joinable.resize(element_total);
		const uint32_t chunk_count = (element_total + FELLOW_PARALLEL_LIST_CHUNK - 1) / FELLOW_PARALLEL_LIST_CHUNK;
		fellow_run_chunks.resize(chunk_count);
		for (uint32_t c = 0; c < chunk_count; c++) {
			fellow_run_chunks[c].from = c * FELLOW_PARALLEL_LIST_CHUNK;
			fellow_run_chunks[c].to = MIN(element_total, (c + 1) * FELLOW_PARALLEL_LIST_CHUNK);
		}
		if (Fellow::cpu_debug_instance_scratch()) {
			fellow_debug_scratch[p_render_list].resize(p_offset + element_total);
		}
		_fellow_parallel_for(chunk_count, &_fellow_chunk_thunk<&RenderForwardClustered::_fellow_fill_instance_data_chunk>);

		if (element_total <= RenderElementInfo::MAX_REPEATS) {
			fellow_run_info = rl->element_info.ptr() + p_offset;
			_fellow_runs(element_total, UINT32_MAX);
			if (p_render_info) {
				for (uint32_t c = 0; c < chunk_count; c++) {
					p_render_info[RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME] += fellow_run_chunks[c].starts;
				}
			}
		} else {
			// Runs can reach MAX_REPEATS: stock's counting loop.
			const uint8_t *joinable = fellow_instance_joinable.ptr();
			uint32_t repeats = 0;
			for (uint32_t i = 0; i < element_total; i++) {
				if (joinable[i] && repeats < RenderElementInfo::MAX_REPEATS) {
					repeats++;
				} else {
					if (repeats > 0) {
						for (uint32_t j = 1; j <= repeats; j++) {
							rl->element_info[p_offset + i - j].repeat = j;
						}
					}
					repeats = 1;
					if (p_render_info) {
						p_render_info[RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME]++;
					}
				}
			}
			if (repeats > 0) {
				for (uint32_t j = 1; j <= repeats; j++) {
					rl->element_info[p_offset + element_total - j].repeat = j;
				}
			}
		}

		if (Fellow::cpu_verify()) {
			_fellow_verify_count(persistent ? _fellow_verify_elements(p_render_list, p_offset, element_total, nullptr) : _fellow_verify_instances(p_render_list, p_offset, element_total, nullptr, false));
		}
		if (p_update_buffer && element_total > 0u) {
			RenderingDevice::get_singleton()->buffer_flush(scene_state.instance_buffer[p_render_list]._get(0u));
		}
		return;
	}

	uint32_t repeats = 0;
	GeometryInstanceSurfaceDataCache *prev_surface = nullptr;
	for (uint32_t i = 0; i < element_total; i++) {
		GeometryInstanceSurfaceDataCache *surface = rl->elements[i + p_offset];
		GeometryInstanceForwardClustered *inst = surface->owner;

		uint32_t element_flags;
		if (persistent) {
			_fellow_persistent_check(surface, inst, fellow_persistent.frame);
			FellowElement &element = reinterpret_cast<FellowElement *>(scene_state.curr_gpu_ptr[p_render_list])[i + p_offset];
			element = { surface->fellow_slot, inst->flags_cache, inst->gi_offset_cache, 0 };
			element_flags = inst->flags_cache;
		} else {
			SceneState::InstanceData instance_data;

			_fellow_make_instance_data(surface, inst, instance_data, true);

			scene_state.curr_gpu_ptr[p_render_list][i + p_offset] = instance_data;
			element_flags = instance_data.flags;
		}

		const bool cant_repeat = element_flags & INSTANCE_DATA_FLAG_MULTIMESH || inst->mesh_instance.is_valid();

		if (prev_surface != nullptr && !cant_repeat && prev_surface->sort.sort_key1 == surface->sort.sort_key1 && prev_surface->sort.sort_key2 == surface->sort.sort_key2 && inst->mirror == prev_surface->owner->mirror && repeats < RenderElementInfo::MAX_REPEATS) {
			//this element is the same as the previous one, count repeats to draw it using instancing
			repeats++;
		} else {
			if (repeats > 0) {
				for (uint32_t j = 1; j <= repeats; j++) {
					rl->element_info[p_offset + i - j].repeat = j;
				}
			}
			repeats = 1;
			if (p_render_info) {
				p_render_info[RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME]++;
			}
		}

		RenderElementInfo &element_info = rl->element_info[p_offset + i];

		element_info.value = uint32_t(surface->sort.sort_key1 & 0xFFF);

		if (cant_repeat) {
			prev_surface = nullptr;
		} else {
			prev_surface = surface;
		}
	}

	if (repeats > 0) {
		for (uint32_t j = 1; j <= repeats; j++) {
			rl->element_info[p_offset + element_total - j].repeat = j;
		}
	}

	if (p_update_buffer && element_total > 0u) {
		RenderingDevice::get_singleton()->buffer_flush(scene_state.instance_buffer[p_render_list]._get(0u));
	}
}

void RenderForwardClustered::_fellow_sort_by_key(RenderListType p_render_list, uint32_t p_from, uint32_t p_size) {
	RenderList &rl = render_list[p_render_list];
	if (Fellow::cpu_verify() && p_size > 1) {
		LocalVector<GeometryInstanceSurfaceDataCache *> unsorted;
		unsorted.resize(p_size);
		memcpy(unsorted.ptr(), rl.elements.ptr() + p_from, p_size * sizeof(GeometryInstanceSurfaceDataCache *));
		_fellow_sort_by_key_impl(p_render_list, p_from, p_size);
		_fellow_verify_count(_fellow_verify_sort(p_render_list, p_from, p_size, unsorted));
		return;
	}
	_fellow_sort_by_key_impl(p_render_list, p_from, p_size);
}

void RenderForwardClustered::_fellow_sort_by_key_impl(RenderListType p_render_list, uint32_t p_from, uint32_t p_size) {
	RenderList &rl = render_list[p_render_list];
	if (!(Fellow::cpu_parallel_list_sort() && p_size >= FELLOW_PARALLEL_SORT_MIN)) {
		rl.fellow_sort_by_key_range(p_from, p_size);
		return;
	}

	// Stock's introsort on the records, with its independent parts sorted by worker threads (FellowParallelSort,
	// servers/rendering/fellow_sort.h): the same steps on the same subranges, the same order, ties included.
	const bool markers = Fellow::list_markers();
	if (markers) {
		RENDER_TIMESTAMP("FellowEngine Sort Gather");
	}
	GeometryInstanceSurfaceDataCache **e = rl.elements.ptr() + p_from;
	if (!rl.fellow_take_fill_records(p_from, p_size)) {
		rl.fellow_sort_records.resize(p_size);
		RenderList::FellowSortRecord *gather = rl.fellow_sort_records.ptr();
		const bool prefetch = Fellow::cpu_prefetch();
		for (uint32_t i = 0; i < p_size; i++) {
			if (prefetch && i + 2 * FELLOW_PREFETCH_DISTANCE < p_size) {
				fellow_prefetch(e[i + 2 * FELLOW_PREFETCH_DISTANCE]);
			}
			gather[i].key2 = e[i]->sort.sort_key2;
			gather[i].key1 = e[i]->sort.sort_key1;
			gather[i].surface = e[i];
		}
	}
	RenderList::FellowSortRecord *records = rl.fellow_sort_records.ptr();

	if (markers) {
		RENDER_TIMESTAMP("FellowEngine Sort Partitions");
	}
	// Each finished leaf writes its part of the list (while its records are in the cache).
	fellow_list_sort.clear();
	fellow_list_sort.add(records, int64_t(p_size), e);
	fellow_parallel->set_linger_usec((uint64_t)Fellow::cpu_hot_workers_usec());
	fellow_list_sort.run_partitions(*fellow_parallel, (uint32_t)Fellow::cpu_worker_tasks(), MAX(int64_t(FELLOW_PARALLEL_SORT_GRAIN), int64_t(p_size) / 16), Fellow::cpu_parallel_partition() ? (int64_t)MAX(1024, Fellow::cpu_parallel_partition_min()) : 0);
	if (markers) {
		RENDER_TIMESTAMP("FellowEngine Sort Leaves");
	}
	fellow_list_sort.run_leaves(*fellow_parallel, (uint32_t)Fellow::cpu_worker_tasks(), &_fellow_sort_write_back);
}

void RenderForwardClustered::_fellow_sort_write_back(void *p_elements, const RenderList::FellowSortRecord *p_records, int64_t p_first, int64_t p_last) {
	GeometryInstanceSurfaceDataCache **e = static_cast<GeometryInstanceSurfaceDataCache **>(p_elements);
	for (int64_t i = p_first; i < p_last; i++) {
		e[i] = p_records[i].surface;
	}
}

void RenderForwardClustered::_fellow_prefetch_element(GeometryInstanceSurfaceDataCache *const *p_elements, uint32_t p_index, uint32_t p_end) {
	// Surface caches two distances ahead, their instances and mesh surfaces one distance ahead (no effect on results).
	if (p_index + 2 * FELLOW_PREFETCH_DISTANCE < p_end) {
		fellow_prefetch(p_elements[p_index + 2 * FELLOW_PREFETCH_DISTANCE]);
	}
	if (p_index + FELLOW_PREFETCH_DISTANCE < p_end) {
		const GeometryInstanceSurfaceDataCache *s = p_elements[p_index + FELLOW_PREFETCH_DISTANCE];
		const uint8_t *inst = reinterpret_cast<const uint8_t *>(s->owner);
		for (uint32_t line = 0; line < FELLOW_PREFETCH_INSTANCE_LINES; line++) {
			fellow_prefetch(inst + line * 64);
		}
		fellow_prefetch(s->surface);
	}
}

void RenderForwardClustered::_fellow_fill_instance_data_chunk(uint32_t p_chunk, void *p_unused) {
	const FellowInstanceDataJob &job = fellow_instance_job;
	RenderList *rl = &render_list[job.render_list];
	// Debug (cpu/debug_instance_scratch): system memory instead of the GPU buffer, to measure the cost of the writes.
	SceneState::InstanceData *gpu = Fellow::cpu_debug_instance_scratch() && !fellow_persistent.active ? fellow_debug_scratch[job.render_list].ptr() : scene_state.curr_gpu_ptr[job.render_list];
	FellowElement *elements = reinterpret_cast<FellowElement *>(scene_state.curr_gpu_ptr[job.render_list]);
	const bool persistent = fellow_persistent.active;
	const uint64_t frame = fellow_persistent.frame;
	uint8_t *joinable = fellow_instance_joinable.ptr();
	const uint32_t from = job.offset + p_chunk * job.chunk_size;
	const uint32_t to = MIN(from + job.chunk_size, job.offset + job.total);
	const bool prefetch = Fellow::cpu_prefetch();
	for (uint32_t i = from; i < to; i++) {
		GeometryInstanceSurfaceDataCache *surface = rl->elements[i];
		GeometryInstanceForwardClustered *inst = surface->owner;
		if (prefetch) {
			_fellow_prefetch_element(rl->elements.ptr(), i, to);
		}

		uint32_t element_flags;
		if (persistent) {
			_fellow_persistent_check(surface, inst, frame);
			elements[i] = { surface->fellow_slot, inst->flags_cache, inst->gi_offset_cache, 0 };
			element_flags = inst->flags_cache;
		} else {
			SceneState::InstanceData instance_data;
			_fellow_make_instance_data(surface, inst, instance_data, true);
			gpu[i] = instance_data;
			element_flags = instance_data.flags;
		}

		// Stock's repeat test against the previous element (none before the first element of the range filled).
		const bool cant_repeat = element_flags & INSTANCE_DATA_FLAG_MULTIMESH || inst->mesh_instance.is_valid();
		bool join = false;
		if (i > job.offset && !cant_repeat) {
			const GeometryInstanceSurfaceDataCache *prev_surface = rl->elements[i - 1];
			const GeometryInstanceForwardClustered *prev_inst = prev_surface->owner;
			const bool prev_cant_repeat = prev_inst->flags_cache & INSTANCE_DATA_FLAG_MULTIMESH || prev_inst->mesh_instance.is_valid();
			join = !prev_cant_repeat && prev_surface->sort.sort_key1 == surface->sort.sort_key1 && prev_surface->sort.sort_key2 == surface->sort.sort_key2 && inst->mirror == prev_inst->mirror;
		}
		joinable[i - job.offset] = join;

		rl->element_info[i].value = uint32_t(surface->sort.sort_key1 & 0xFFF);
	}
	_fellow_run_scan(p_chunk);
}

void RenderForwardClustered::_fellow_run_scan(uint32_t p_chunk) {
	FellowRunChunk &chunk = fellow_run_chunks[p_chunk];
	const uint8_t *link = fellow_instance_joinable.ptr();
	chunk.first_start = UINT32_MAX;
	chunk.starts = 0;
	for (uint32_t k = chunk.from; k < chunk.to; k++) {
		if (!link[k]) {
			if (chunk.first_start == UINT32_MAX) {
				chunk.first_start = k;
			}
			chunk.starts++;
		}
	}
}

void RenderForwardClustered::_fellow_runs(uint32_t p_end, uint32_t p_max_helpers) {
	// The end of the run each range ends in: the first run start after the range (ranges are in list order).
	uint32_t next = p_end;
	for (int64_t c = int64_t(fellow_run_chunks.size()) - 1; c >= 0; c--) {
		FellowRunChunk &chunk = fellow_run_chunks[c];
		chunk.next = next;
		if (chunk.first_start != UINT32_MAX) {
			next = chunk.first_start;
		}
	}
	_fellow_parallel_for(fellow_run_chunks.size(), &_fellow_chunk_thunk<&RenderForwardClustered::_fellow_runs_chunk>, p_max_helpers);
}

void RenderForwardClustered::_fellow_runs_chunk(uint32_t p_chunk, void *p_unused) {
	const FellowRunChunk &chunk = fellow_run_chunks[p_chunk];
	const uint8_t *link = fellow_instance_joinable.ptr();
	RenderElementInfo *info = fellow_run_info;
	uint32_t end = chunk.next;
	for (int64_t k = int64_t(chunk.to) - 1; k >= int64_t(chunk.from); k--) {
		info[k].repeat = end - uint32_t(k);
		if (!link[k]) {
			end = uint32_t(k);
		}
	}
}

_FORCE_INLINE_ static uint32_t _indices_to_primitives(RSE::PrimitiveType p_primitive, uint32_t p_indices) {
	static const uint32_t divisor[RSE::PRIMITIVE_MAX] = { 1, 2, 1, 3, 1 };
	static const uint32_t subtractor[RSE::PRIMITIVE_MAX] = { 0, 0, 1, 0, 2 };
	return (p_indices - subtractor[p_primitive]) / divisor[p_primitive];
}
RenderForwardClustered::FellowFillParams RenderForwardClustered::_fellow_fill_params(RenderListType p_render_list, const RenderDataRD *p_render_data, PassMode p_pass_mode, bool p_using_sdfgi, bool p_using_opaque_gi, bool p_using_motion_pass, uint64_t p_frame) {
	// What stock's _fill_render_list computes before its loop and what the loop reads from the render data.
	FellowFillParams params;
	Plane near_plane = Plane(-p_render_data->scene_data->cam_transform.basis.get_column(Vector3::AXIS_Z), p_render_data->scene_data->cam_transform.origin);
	near_plane.d += p_render_data->scene_data->cam_projection.get_z_near();
	float z_max = p_render_data->scene_data->cam_projection.get_z_far() - p_render_data->scene_data->cam_projection.get_z_near();
	params.render_list = p_render_list;
	params.render_data = p_render_data;
	params.pass_mode = p_pass_mode;
	params.using_sdfgi = p_using_sdfgi;
	params.using_opaque_gi = p_using_opaque_gi;
	params.using_motion_pass = p_using_motion_pass;
	params.frame = p_frame;
	params.near_plane = near_plane;
	params.z_max = z_max;
	params.instances = p_render_data->instances;
	params.cam_orthogonal = p_render_data->scene_data->cam_orthogonal;
	params.cam_origin = p_render_data->scene_data->cam_transform.origin;
	params.main_cam_origin = p_render_data->scene_data->main_cam_transform.origin;
	params.lod_distance_multiplier = p_render_data->scene_data->lod_distance_multiplier;
	params.fellow_lod_distance_bias = p_render_data->scene_data->fellow_lod_distance_bias;
	params.fellow_lod_ortho_extent = p_render_data->scene_data->fellow_lod_ortho_extent;
	params.screen_mesh_lod_threshold = p_render_data->scene_data->screen_mesh_lod_threshold;
	params.count_primitives = p_render_data->render_info != nullptr;
	params.render_info = p_render_data->render_info;
	return params;
}

void RenderForwardClustered::_fill_render_list(RenderListType p_render_list, const RenderDataRD *p_render_data, PassMode p_pass_mode, bool p_using_sdfgi, bool p_using_opaque_gi, bool p_using_motion_pass, bool p_append) {
	uint64_t frame = RSG::rasterizer->get_frame_number();

	if (p_render_list == RENDER_LIST_OPAQUE) {
		scene_state.used_sss = false;
		scene_state.used_screen_texture = false;
		scene_state.used_normal_texture = false;
		scene_state.used_depth_texture = false;
		scene_state.used_lightmap = false;
		scene_state.used_opaque_stencil = false;
	}
	uint32_t lightmap_captures_used = 0;

	RenderList *rl = &render_list[p_render_list];
	_update_dirty_geometry_instances();
	rl->fellow_sort_records_count = 0; // set again by the parallel fill when it takes the sort records

	if (!p_append) {
		rl->clear();
		if (p_render_list == RENDER_LIST_OPAQUE) {
			// Opaque fills motion and alpha lists.
			render_list[RENDER_LIST_MOTION].clear();
			render_list[RENDER_LIST_ALPHA].clear();
		}
	}

	//fill list

	const FellowFillParams params = _fellow_fill_params(p_render_list, p_render_data, p_pass_mode, p_using_sdfgi, p_using_opaque_gi, p_using_motion_pass, frame);

	const uint32_t instance_count = (uint32_t)p_render_data->instances->size();
	if (!(Fellow::cpu_parallel_list_fill() && instance_count >= FELLOW_PARALLEL_LIST_MIN && _fellow_fill_render_list_parallel(params, instance_count, lightmap_captures_used))) {
		FellowFillOutput out;
		out.main = &rl->elements;
		out.alpha = &render_list[RENDER_LIST_ALPHA].elements;
		out.motion = &render_list[RENDER_LIST_MOTION].elements;
		out.lightmap_captures_used = &lightmap_captures_used;
		_fill_render_list_range<false>(params, 0, instance_count, out);
		_fellow_fill_apply_output(params, out);
	}

	if (p_render_list == RENDER_LIST_OPAQUE && lightmap_captures_used) {
		RD::get_singleton()->buffer_update(scene_state.lightmap_capture_buffer, 0, sizeof(LightmapCaptureData) * lightmap_captures_used, scene_state.lightmap_captures);
	}
}

template <bool t_records>
void RenderForwardClustered::_fill_render_list_range(const FellowFillParams &p_params, uint32_t p_from, uint32_t p_to, FellowFillOutput &r_out) {
	// Stock's _fill_render_list loop, for the instances [p_from, p_to). The lists, the scene flags and the primitive
	// counter it updated directly go through r_out; the caller applies them (in list order for worker ranges).
	// t_records (shadow passes only, decision 0016's batched shadow lists): the instances and surfaces are not written;
	// each element becomes a record of the sort keys and instance flags this pass gives it, and the instances whose
	// previous transform must be reset are listed (the same values, kept per pass so passes can be filled at once).
	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	const RenderListType p_render_list = p_params.render_list;
	const PassMode p_pass_mode = p_params.pass_mode;
	const bool p_using_sdfgi = p_params.using_sdfgi;
	const bool p_using_opaque_gi = p_params.using_opaque_gi;
	const bool p_using_motion_pass = p_params.using_motion_pass;
	const uint64_t frame = p_params.frame;
	const Plane &near_plane = p_params.near_plane;
	const float z_max = p_params.z_max;
	// Locals the compiler can keep in registers (the loop writes surfaces through pointers it cannot tell apart).
	LocalVector<GeometryInstanceSurfaceDataCache *> &out_main = *r_out.main;
	LocalVector<GeometryInstanceSurfaceDataCache *> &out_alpha = *r_out.alpha;
	LocalVector<GeometryInstanceSurfaceDataCache *> &out_motion = *r_out.motion;
	uint64_t primitives = 0;
	LocalVector<FellowShadowRecord> *sort_records = r_out.sort_records;

	const bool fellow_prefetch_on = Fellow::cpu_prefetch();
	for (uint32_t i = p_from; i < p_to; i++) {
		GeometryInstanceForwardClustered *inst = static_cast<GeometryInstanceForwardClustered *>((*p_params.instances)[i]);
		if (fellow_prefetch_on) {
			// FellowEngine (decision 0016): load the instances a few iterations ahead (no effect on results).
			if (i + FELLOW_PREFETCH_DISTANCE < p_to) {
				const uint8_t *ahead = reinterpret_cast<const uint8_t *>((*p_params.instances)[i + FELLOW_PREFETCH_DISTANCE]);
				for (uint32_t line = 0; line < FELLOW_PREFETCH_INSTANCE_LINES; line++) {
					fellow_prefetch(ahead + line * 64);
				}
			}
			if (i + FELLOW_PREFETCH_DISTANCE / 2 < p_to) {
				const GeometryInstanceForwardClustered *soon = static_cast<const GeometryInstanceForwardClustered *>((*p_params.instances)[i + FELLOW_PREFETCH_DISTANCE / 2]);
				if (soon->surface_caches) {
					fellow_prefetch(soon->surface_caches);
				}
			}
		}

		Vector3 center = inst->transform.origin;
		float inst_depth;
		if (p_params.cam_orthogonal) {
			if (inst->use_aabb_center) {
				center = inst->transformed_aabb.get_support(-near_plane.normal);
			}
			inst_depth = near_plane.distance_to(center) - inst->sorting_offset;
		} else {
			if (inst->use_aabb_center) {
				center = inst->transformed_aabb.position + (inst->transformed_aabb.size * 0.5);
			}
			inst_depth = p_params.cam_origin.distance_to(center) - inst->sorting_offset;
		}
		if constexpr (!t_records) {
			inst->depth = inst_depth;
		}
		uint32_t depth_layer = CLAMP(int(inst_depth * 16 / z_max), 0, 15);

		uint32_t flags = inst->base_flags; //fill flags if appropriate

		if (inst->non_uniform_scale) {
			flags |= INSTANCE_DATA_FLAGS_NON_UNIFORM_SCALE;
		}
		bool uses_lightmap = false;
		bool uses_gi = false;
		bool uses_motion = false;
		float fade_alpha = 1.0;

		if (inst->fade_near || inst->fade_far) {
			float fade_dist = inst->transformed_aabb.get_center().distance_to(p_params.cam_origin);
			// Use `smoothstep()` to make opacity changes more gradual and less noticeable to the player.
			if (inst->fade_far && fade_dist > inst->fade_far_begin) {
				fade_alpha = Math::smoothstep(0.0f, 1.0f, 1.0f - (fade_dist - inst->fade_far_begin) / (inst->fade_far_end - inst->fade_far_begin));
			} else if (inst->fade_near && fade_dist < inst->fade_near_end) {
				fade_alpha = Math::smoothstep(0.0f, 1.0f, (fade_dist - inst->fade_near_begin) / (inst->fade_near_end - inst->fade_near_begin));
			}
		}

		fade_alpha *= inst->force_alpha * inst->parent_fade_alpha;

		flags = (flags & ~INSTANCE_DATA_FLAGS_FADE_MASK) | (uint32_t(fade_alpha * 255.0) << INSTANCE_DATA_FLAGS_FADE_SHIFT);

		if (p_render_list == RENDER_LIST_OPAQUE) {
			// Setup GI
			if (inst->lightmap_instance.is_valid()) {
				// find index of the lightmap_instance of the instance being rendered
				int32_t lightmap_cull_index = -1;
				for (uint32_t j = 0; j < scene_state.lightmaps_used; j++) {
					if (scene_state.lightmap_ids[j] == inst->lightmap_instance) {
						lightmap_cull_index = j;
						break;
					}
				}
				if (lightmap_cull_index >= 0) {
					inst->gi_offset_cache = inst->lightmap_slice_index << 16;
					inst->gi_offset_cache |= lightmap_cull_index;
					flags |= INSTANCE_DATA_FLAG_USE_LIGHTMAP;
					if (scene_state.lightmap_has_sh[lightmap_cull_index]) {
						flags |= INSTANCE_DATA_FLAG_USE_SH_LIGHTMAP;
					}
					uses_lightmap = true;
				} else {
					inst->gi_offset_cache = 0xFFFFFFFF;
				}

			} else if (inst->lightmap_sh) {
				if (r_out.lightmap_captures_used == nullptr) {
					// Worker range: the capture slot depends on the instances before this one; assigned afterwards.
					r_out.deferred_captures->push_back(inst);
				} else if (*r_out.lightmap_captures_used < scene_state.max_lightmap_captures) {
					const Color *src_capture = inst->lightmap_sh->sh;
					LightmapCaptureData &lcd = scene_state.lightmap_captures[*r_out.lightmap_captures_used];
					for (int j = 0; j < 9; j++) {
						lcd.sh[j * 4 + 0] = src_capture[j].r;
						lcd.sh[j * 4 + 1] = src_capture[j].g;
						lcd.sh[j * 4 + 2] = src_capture[j].b;
						lcd.sh[j * 4 + 3] = src_capture[j].a;
					}
					flags |= INSTANCE_DATA_FLAG_USE_LIGHTMAP_CAPTURE;
					inst->gi_offset_cache = *r_out.lightmap_captures_used;
					(*r_out.lightmap_captures_used)++;
					uses_lightmap = true;
				}

			} else {
				if (p_using_opaque_gi) {
					flags |= INSTANCE_DATA_FLAG_USE_GI_BUFFERS;
				}

				if (inst->voxel_gi_instances[0].is_valid()) {
					uint32_t probe0_index = 0xFFFF;
					uint32_t probe1_index = 0xFFFF;

					for (uint32_t j = 0; j < scene_state.voxelgis_used; j++) {
						if (scene_state.voxelgi_ids[j] == inst->voxel_gi_instances[0]) {
							probe0_index = j;
						} else if (scene_state.voxelgi_ids[j] == inst->voxel_gi_instances[1]) {
							probe1_index = j;
						}
					}

					if (probe0_index == 0xFFFF && probe1_index != 0xFFFF) {
						//0 must always exist if a probe exists
						SWAP(probe0_index, probe1_index);
					}

					inst->gi_offset_cache = probe0_index | (probe1_index << 16);
					flags |= INSTANCE_DATA_FLAG_USE_VOXEL_GI;
					uses_gi = true;
				} else {
					if (p_using_sdfgi && inst->can_sdfgi) {
						flags |= INSTANCE_DATA_FLAG_USE_SDFGI;
						uses_gi = true;
					}
					inst->gi_offset_cache = 0xFFFFFFFF;
				}
			}
			if (p_pass_mode == PASS_MODE_DEPTH_NORMAL_ROUGHNESS || p_pass_mode == PASS_MODE_DEPTH_NORMAL_ROUGHNESS_VOXEL_GI || p_pass_mode == PASS_MODE_COLOR) {
				bool transform_changed = inst->transform_status == GeometryInstanceForwardClustered::TransformStatus::MOVED;
				bool has_mesh_instance = inst->mesh_instance.is_valid();
				bool uses_particles = inst->base_flags & INSTANCE_DATA_FLAG_PARTICLES;
				bool is_multimesh_with_motion = !uses_particles && (inst->base_flags & INSTANCE_DATA_FLAG_MULTIMESH) && mesh_storage->_multimesh_uses_motion_vectors_offsets(inst->data->base);
				bool is_dynamic = transform_changed || has_mesh_instance || uses_particles || is_multimesh_with_motion;
				if (p_pass_mode == PASS_MODE_COLOR && p_using_motion_pass) {
					uses_motion = is_dynamic;
					if (is_dynamic && fellow_taa_motion_pass) {
						flags |= INSTANCE_DATA_FLAGS_DYNAMIC;
					}
				} else if (is_dynamic) {
					flags |= INSTANCE_DATA_FLAGS_DYNAMIC;
				}
			}
		}
		if constexpr (!t_records) {
			inst->flags_cache = flags;
		}

		GeometryInstanceSurfaceDataCache *surf = inst->surface_caches;

		float lod_distance = 0.0;

		if (p_params.cam_orthogonal || p_params.fellow_lod_ortho_extent > 0.0) {
			lod_distance = 1.0;
		} else {
			Vector3 aabb_min = inst->transformed_aabb.position;
			Vector3 aabb_max = inst->transformed_aabb.position + inst->transformed_aabb.size;
			Vector3 camera_position = p_params.main_cam_origin;
			Vector3 surface_distance = Vector3(0.0, 0.0, 0.0).max(aabb_min - camera_position).max(camera_position - aabb_max);

			lod_distance = surface_distance.length();
			if (p_params.fellow_lod_distance_bias > 0.0) {
				lod_distance = MAX(0.0, lod_distance - p_params.fellow_lod_distance_bias);
			}
		}

		if (unlikely(inst->transform_status != GeometryInstanceForwardClustered::TransformStatus::NONE && frame > inst->prev_transform_change_frame && inst->prev_transform_change_frame)) {
			if constexpr (t_records) {
				r_out.transform_updates->push_back(inst);
			} else {
				inst->prev_transform = inst->transform;
				inst->fellow_data_version++;
				inst->transform_status = GeometryInstanceForwardClustered::TransformStatus::NONE;
			}
		}

		while (surf) {
			decltype(surf->sort) record_sort;
			if constexpr (t_records) {
				record_sort = surf->sort;
			}
			decltype(surf->sort) &sort = t_records ? record_sort : surf->sort;
			bool record_add = false;
			sort.uses_forward_gi = 0;
			sort.uses_lightmap = 0;

			// LOD
			if (p_params.screen_mesh_lod_threshold > 0.0 && mesh_storage->mesh_surface_has_lod(surf->surface)) {
				uint32_t indices = 0;
				sort.lod_index = mesh_storage->mesh_surface_get_lod(surf->surface, inst->lod_model_scale * inst->lod_bias, lod_distance * p_params.lod_distance_multiplier, p_params.screen_mesh_lod_threshold, indices);
				if (p_params.count_primitives) {
					indices = _indices_to_primitives(surf->primitive, indices);
					primitives += indices; // Added to the visible (opaque list) or shadow counter by the caller.
				}
			} else {
				sort.lod_index = 0;
				if (p_params.count_primitives) {
					// This does not include primitives rendered via indirect draw calls.
					uint32_t to_draw = mesh_storage->mesh_surface_get_vertices_drawn_count(surf->surface);
					to_draw = _indices_to_primitives(surf->primitive, to_draw);
					to_draw *= inst->instance_count;
					primitives += to_draw; // Added to the visible (opaque list) or shadow counter by the caller.
				}
			}

			// ADD Element
			if (p_pass_mode == PASS_MODE_COLOR) {
#ifdef DEBUG_ENABLED
				bool force_alpha = unlikely(get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_OVERDRAW);
#else
				bool force_alpha = false;
#endif

				if (fade_alpha < FADE_ALPHA_PASS_THRESHOLD) {
					force_alpha = true;
				}

				if (!force_alpha && (surf->flags & (GeometryInstanceSurfaceDataCache::FLAG_PASS_DEPTH | GeometryInstanceSurfaceDataCache::FLAG_PASS_OPAQUE))) {
					record_add = true;
					if constexpr (!t_records) {
						out_main.push_back(surf);
					}
				}

				if (force_alpha || (surf->flags & GeometryInstanceSurfaceDataCache::FLAG_PASS_ALPHA)) {
					surf->color_pass_inclusion_mask = COLOR_PASS_FLAG_TRANSPARENT;
					out_alpha.push_back(surf);
					if (uses_gi) {
						sort.uses_forward_gi = 1;
					}
				} else if (p_using_motion_pass && (uses_motion || (surf->flags & GeometryInstanceSurfaceDataCache::FLAG_USES_MOTION_VECTOR))) {
					surf->color_pass_inclusion_mask = COLOR_PASS_FLAG_MOTION_VECTORS;
					out_motion.push_back(surf);
				} else {
					surf->color_pass_inclusion_mask = 0;
				}

				if (uses_lightmap) {
					sort.uses_lightmap = 1;
					r_out.used_lightmap = true;
				}

				if (surf->flags & GeometryInstanceSurfaceDataCache::FLAG_USES_SUBSURFACE_SCATTERING) {
					r_out.used_sss = true;
				}
				if (surf->flags & GeometryInstanceSurfaceDataCache::FLAG_USES_SCREEN_TEXTURE) {
					r_out.used_screen_texture = true;
				}
				if (surf->flags & GeometryInstanceSurfaceDataCache::FLAG_USES_NORMAL_TEXTURE) {
					r_out.used_normal_texture = true;
				}
				if (surf->flags & GeometryInstanceSurfaceDataCache::FLAG_USES_DEPTH_TEXTURE) {
					r_out.used_depth_texture = true;
				}
				if ((surf->flags & GeometryInstanceSurfaceDataCache::FLAG_USES_STENCIL) && !force_alpha && (surf->flags & (GeometryInstanceSurfaceDataCache::FLAG_PASS_DEPTH | GeometryInstanceSurfaceDataCache::FLAG_PASS_OPAQUE))) {
					r_out.used_opaque_stencil = true;
				}
			} else if (p_pass_mode == PASS_MODE_SHADOW || p_pass_mode == PASS_MODE_SHADOW_DP) {
				if (surf->flags & GeometryInstanceSurfaceDataCache::FLAG_PASS_SHADOW) {
					record_add = true;
					if constexpr (!t_records) {
						out_main.push_back(surf);
					}
				}
			} else if (p_pass_mode == PASS_MODE_DEPTH_MATERIAL) {
				if (surf->flags & (GeometryInstanceSurfaceDataCache::FLAG_PASS_DEPTH | GeometryInstanceSurfaceDataCache::FLAG_PASS_OPAQUE | GeometryInstanceSurfaceDataCache::FLAG_PASS_ALPHA)) {
					record_add = true;
					if constexpr (!t_records) {
						out_main.push_back(surf);
					}
				}
			} else {
				if (surf->flags & (GeometryInstanceSurfaceDataCache::FLAG_PASS_DEPTH | GeometryInstanceSurfaceDataCache::FLAG_PASS_OPAQUE)) {
					record_add = true;
					if constexpr (!t_records) {
						out_main.push_back(surf);
					}
				}
			}

			sort.depth_layer = depth_layer;
			if constexpr (t_records) {
				if (record_add) {
					r_out.records->push_back({ sort.sort_key2, sort.sort_key1, surf, flags });
				}
			} else {
				if (record_add && sort_records) {
					// The main list's sort keys, final for this frame, taken while the surface is in this thread's
					// cache (the sort then needs no pass over the surfaces).
					sort_records->push_back({ sort.sort_key2, sort.sort_key1, surf, 0 });
				}
			}

			surf = surf->next;
		}
	}

	r_out.primitives += primitives;
}

void RenderForwardClustered::_fellow_fill_apply_output(const FellowFillParams &p_params, const FellowFillOutput &p_out) {
	if (p_out.used_sss) {
		scene_state.used_sss = true;
	}
	if (p_out.used_screen_texture) {
		scene_state.used_screen_texture = true;
	}
	if (p_out.used_normal_texture) {
		scene_state.used_normal_texture = true;
	}
	if (p_out.used_depth_texture) {
		scene_state.used_depth_texture = true;
	}
	if (p_out.used_lightmap) {
		scene_state.used_lightmap = true;
	}
	if (p_out.used_opaque_stencil) {
		scene_state.used_opaque_stencil = true;
	}
	if (p_params.render_info) {
		// Stock adds each surface's count to the int counter; the sum modulo 2^32 is the same.
		const uint32_t primitives = uint32_t(p_out.primitives);
		if (p_params.render_list == RENDER_LIST_OPAQUE) { //opaque
			p_params.render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_VISIBLE][RSE::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] += primitives;
		} else if (p_params.render_list == RENDER_LIST_SECONDARY) { //shadow
			p_params.render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_SHADOW][RSE::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] += primitives;
		}
	}
}

void RenderForwardClustered::_fellow_fill_render_list_chunk(uint32_t p_chunk, void *p_unused) {
	const uint32_t from = p_chunk * fellow_fill_chunk_size;
	const uint32_t to = MIN(from + fellow_fill_chunk_size, fellow_fill_count);
	_fill_render_list_range<false>(fellow_fill_params, from, to, fellow_fill_chunks[p_chunk].out);
}

bool RenderForwardClustered::_fellow_fill_render_list_parallel(const FellowFillParams &p_params, uint32_t p_count, uint32_t &r_lightmap_captures_used) {
	// Each worker fills a contiguous range of instances into its own lists. Every instance appears once in the list
	// being filled, and the loop body writes only to that instance and its surfaces, so the ranges are independent.
	// Joined in range order, the lists hold the elements in stock's order.
	const uint32_t chunk_size = FELLOW_PARALLEL_LIST_CHUNK;
	const uint32_t chunk_count = (p_count + chunk_size - 1) / chunk_size;
	// With the compact sort, the main list's sort records are taken during the fill (see _fellow_sort_by_key()).
	const bool gather_keys = Fellow::cpu_compact_list_sort();
	if (fellow_fill_chunks.size() < chunk_count) {
		fellow_fill_chunks.resize(chunk_count);
	}
	for (uint32_t c = 0; c < chunk_count; c++) {
		FellowFillChunk &chunk = fellow_fill_chunks[c];
		chunk.main.clear();
		chunk.alpha.clear();
		chunk.motion.clear();
		chunk.deferred_captures.clear();
		chunk.out = FellowFillOutput();
		chunk.out.main = &chunk.main;
		chunk.out.alpha = &chunk.alpha;
		chunk.out.motion = &chunk.motion;
		chunk.out.deferred_captures = &chunk.deferred_captures;
		chunk.sort_records.clear();
		chunk.out.sort_records = gather_keys ? &chunk.sort_records : nullptr;
	}
	fellow_fill_params = p_params;
	fellow_fill_count = p_count;
	fellow_fill_chunk_size = chunk_size;

	_fellow_parallel_for(chunk_count, &_fellow_chunk_thunk<&RenderForwardClustered::_fellow_fill_render_list_chunk>);

	// Join: each chunk's elements copied to their place in range order (by worker tasks), counters summed.
	RenderList *rl = &render_list[p_params.render_list];
	const uint32_t main_from = rl->elements.size();
	bool any_deferred_captures = false;
	FellowFillOutput total;
	uint32_t main_at = main_from;
	uint32_t alpha_at = render_list[RENDER_LIST_ALPHA].elements.size();
	uint32_t motion_at = render_list[RENDER_LIST_MOTION].elements.size();
	uint32_t record_at = 0;
	for (uint32_t c = 0; c < chunk_count; c++) {
		FellowFillChunk &chunk = fellow_fill_chunks[c];
		any_deferred_captures = any_deferred_captures || !chunk.deferred_captures.is_empty();
		chunk.main_at = main_at;
		chunk.alpha_at = alpha_at;
		chunk.motion_at = motion_at;
		chunk.record_at = record_at;
		main_at += chunk.main.size();
		alpha_at += chunk.alpha.size();
		motion_at += chunk.motion.size();
		record_at += chunk.sort_records.size();
		total.primitives += chunk.out.primitives;
		total.used_sss = total.used_sss || chunk.out.used_sss;
		total.used_screen_texture = total.used_screen_texture || chunk.out.used_screen_texture;
		total.used_normal_texture = total.used_normal_texture || chunk.out.used_normal_texture;
		total.used_depth_texture = total.used_depth_texture || chunk.out.used_depth_texture;
		total.used_lightmap = total.used_lightmap || chunk.out.used_lightmap;
		total.used_opaque_stencil = total.used_opaque_stencil || chunk.out.used_opaque_stencil;
	}
	rl->elements.resize(main_at);
	render_list[RENDER_LIST_ALPHA].elements.resize(alpha_at);
	render_list[RENDER_LIST_MOTION].elements.resize(motion_at);
	// The sort records taken during the fill, in list order (not when capture slots are given out afterwards: that
	// changes the surfaces' keys after the fill).
	rl->fellow_sort_records_count = 0;
	fellow_join_records = gather_keys && !any_deferred_captures && record_at == main_at - main_from;
	if (fellow_join_records) {
		rl->fellow_sort_records.resize(record_at);
	}
	fellow_join_list = p_params.render_list;
	_fellow_parallel_for(chunk_count, &_fellow_chunk_thunk<&RenderForwardClustered::_fellow_fill_join_chunk>);
	if (fellow_join_records) {
		rl->fellow_sort_records_from = main_from;
		rl->fellow_sort_records_count = record_at;
	}

	// Lightmap capture slots, in list order: what stock's loop does for these instances, after the rest of their fill
	// (it only adds the capture flag to the instance flags and marks its surfaces as lightmapped, both part of what the
	// sort and the instance data read later).
	for (uint32_t c = 0; c < chunk_count; c++) {
		for (GeometryInstanceForwardClustered *inst : fellow_fill_chunks[c].deferred_captures) {
			if (r_lightmap_captures_used < scene_state.max_lightmap_captures) {
				const Color *src_capture = inst->lightmap_sh->sh;
				LightmapCaptureData &lcd = scene_state.lightmap_captures[r_lightmap_captures_used];
				for (int j = 0; j < 9; j++) {
					lcd.sh[j * 4 + 0] = src_capture[j].r;
					lcd.sh[j * 4 + 1] = src_capture[j].g;
					lcd.sh[j * 4 + 2] = src_capture[j].b;
					lcd.sh[j * 4 + 3] = src_capture[j].a;
				}
				inst->flags_cache |= INSTANCE_DATA_FLAG_USE_LIGHTMAP_CAPTURE;
				inst->gi_offset_cache = r_lightmap_captures_used;
				r_lightmap_captures_used++;
				if (p_params.pass_mode == PASS_MODE_COLOR) {
					for (GeometryInstanceSurfaceDataCache *surf = inst->surface_caches; surf; surf = surf->next) {
						surf->sort.uses_lightmap = 1;
					}
					total.used_lightmap = true;
				}
			}
		}
	}

	_fellow_fill_apply_output(p_params, total);
	return true;
}

void RenderForwardClustered::_fellow_fill_join_chunk(uint32_t p_chunk, void *p_unused) {
	const FellowFillChunk &chunk = fellow_fill_chunks[p_chunk];
	RenderList *rl = &render_list[fellow_join_list];
	if (!chunk.main.is_empty()) {
		memcpy(rl->elements.ptr() + chunk.main_at, chunk.main.ptr(), chunk.main.size() * sizeof(GeometryInstanceSurfaceDataCache *));
	}
	if (!chunk.alpha.is_empty()) {
		memcpy(render_list[RENDER_LIST_ALPHA].elements.ptr() + chunk.alpha_at, chunk.alpha.ptr(), chunk.alpha.size() * sizeof(GeometryInstanceSurfaceDataCache *));
	}
	if (!chunk.motion.is_empty()) {
		memcpy(render_list[RENDER_LIST_MOTION].elements.ptr() + chunk.motion_at, chunk.motion.ptr(), chunk.motion.size() * sizeof(GeometryInstanceSurfaceDataCache *));
	}
	if (fellow_join_records) {
		RenderList::FellowSortRecord *out = rl->fellow_sort_records.ptr() + chunk.record_at;
		const FellowShadowRecord *in = chunk.sort_records.ptr();
		for (uint32_t i = 0; i < chunk.sort_records.size(); i++) {
			out[i].key2 = in[i].key2;
			out[i].key1 = in[i].key1;
			out[i].surface = in[i].surface;
		}
	}
}

void RenderForwardClustered::_setup_voxelgis(const PagedArray<RID> &p_voxelgis) {
	scene_state.voxelgis_used = MIN(p_voxelgis.size(), uint32_t(MAX_VOXEL_GI_INSTANCESS));
	for (uint32_t i = 0; i < scene_state.voxelgis_used; i++) {
		scene_state.voxelgi_ids[i] = p_voxelgis[i];
	}
}

void RenderForwardClustered::_setup_lightmaps(const RenderDataRD *p_render_data, const PagedArray<RID> &p_lightmaps, const Transform3D &p_cam_transform) {
	RendererRD::LightStorage *light_storage = RendererRD::LightStorage::get_singleton();

	scene_state.lightmaps_used = 0;
	for (int i = 0; i < (int)p_lightmaps.size(); i++) {
		if (i >= (int)scene_state.max_lightmaps) {
			break;
		}

		RID lightmap = light_storage->lightmap_instance_get_lightmap(p_lightmaps[i]);

		// Transform (for directional lightmaps).
		Basis to_lm = light_storage->lightmap_instance_get_transform(p_lightmaps[i]).basis.inverse() * p_cam_transform.basis;
		to_lm = to_lm.inverse().transposed(); //will transform normals
		RendererRD::MaterialStorage::store_transform_3x3(to_lm, scene_state.lightmaps[i].normal_xform);

		// Light texture size.
		Vector2i lightmap_size = light_storage->lightmap_get_light_texture_size(lightmap);
		scene_state.lightmaps[i].texture_size[0] = lightmap_size[0];
		scene_state.lightmaps[i].texture_size[1] = lightmap_size[1];

		// Exposure.
		scene_state.lightmaps[i].exposure_normalization = 1.0;
		scene_state.lightmaps[i].flags = light_storage->lightmap_get_shadowmask_mode(lightmap);
		if (p_render_data->camera_attributes.is_valid()) {
			float baked_exposure = light_storage->lightmap_get_baked_exposure_normalization(lightmap);
			float enf = RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_render_data->camera_attributes);
			scene_state.lightmaps[i].exposure_normalization = enf / baked_exposure;
		}

		scene_state.lightmap_ids[i] = p_lightmaps[i];
		scene_state.lightmap_has_sh[i] = light_storage->lightmap_uses_spherical_harmonics(lightmap);

		scene_state.lightmaps_used++;
	}
	if (scene_state.lightmaps_used > 0) {
		RD::get_singleton()->buffer_update(scene_state.lightmap_buffer, 0, sizeof(LightmapData) * scene_state.lightmaps_used, scene_state.lightmaps);
	}
}

/* SDFGI */

void RenderForwardClustered::_update_sdfgi(RenderDataRD *p_render_data) {
	if (p_render_data->sdfgi_update_data == nullptr) {
		return;
	}

	Ref<RenderSceneBuffersRD> rb;
	if (p_render_data && p_render_data->render_buffers.is_valid()) {
		rb = p_render_data->render_buffers;
	}

	if (rb.is_valid() && rb->has_custom_data(RB_SCOPE_SDFGI)) {
		RENDER_TIMESTAMP("Render SDFGI");
		Ref<RendererRD::GI::SDFGI> sdfgi = rb->get_custom_data(RB_SCOPE_SDFGI);
		float exposure_normalization = 1.0;

		if (p_render_data->camera_attributes.is_valid()) {
			exposure_normalization = RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_render_data->camera_attributes);
		}
		for (int i = 0; i < p_render_data->render_sdfgi_region_count; i++) {
			sdfgi->render_region(rb, p_render_data->render_sdfgi_regions[i].region, p_render_data->render_sdfgi_regions[i].instances, exposure_normalization);
		}
		if (p_render_data->sdfgi_update_data->update_static) {
			sdfgi->render_static_lights(p_render_data, rb, p_render_data->sdfgi_update_data->static_cascade_count, p_render_data->sdfgi_update_data->static_cascade_indices, p_render_data->sdfgi_update_data->static_positional_lights);
		}
	}
}

/* Debug */

void RenderForwardClustered::_debug_draw_cluster(Ref<RenderSceneBuffersRD> p_render_buffers) {
	if (p_render_buffers.is_valid() && current_cluster_builder != nullptr) {
		RSE::ViewportDebugDraw dd = get_debug_draw_mode();

		if (dd == RSE::VIEWPORT_DEBUG_DRAW_CLUSTER_OMNI_LIGHTS || dd == RSE::VIEWPORT_DEBUG_DRAW_CLUSTER_SPOT_LIGHTS || dd == RSE::VIEWPORT_DEBUG_DRAW_CLUSTER_AREA_LIGHTS || dd == RSE::VIEWPORT_DEBUG_DRAW_CLUSTER_DECALS || dd == RSE::VIEWPORT_DEBUG_DRAW_CLUSTER_REFLECTION_PROBES) {
			ClusterBuilderRD::ElementType elem_type = ClusterBuilderRD::ELEMENT_TYPE_MAX;
			switch (dd) {
				case RSE::VIEWPORT_DEBUG_DRAW_CLUSTER_OMNI_LIGHTS:
					elem_type = ClusterBuilderRD::ELEMENT_TYPE_OMNI_LIGHT;
					break;
				case RSE::VIEWPORT_DEBUG_DRAW_CLUSTER_SPOT_LIGHTS:
					elem_type = ClusterBuilderRD::ELEMENT_TYPE_SPOT_LIGHT;
					break;
				case RSE::VIEWPORT_DEBUG_DRAW_CLUSTER_AREA_LIGHTS:
					elem_type = ClusterBuilderRD::ELEMENT_TYPE_AREA_LIGHT;
					break;
				case RSE::VIEWPORT_DEBUG_DRAW_CLUSTER_DECALS:
					elem_type = ClusterBuilderRD::ELEMENT_TYPE_DECAL;
					break;
				case RSE::VIEWPORT_DEBUG_DRAW_CLUSTER_REFLECTION_PROBES:
					elem_type = ClusterBuilderRD::ELEMENT_TYPE_REFLECTION_PROBE;
					break;
				default: {
				}
			}
			current_cluster_builder->debug(elem_type);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
// FOG SHADER

void RenderForwardClustered::_update_volumetric_fog(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_environment, const Projection &p_cam_projection, const Transform3D &p_cam_transform, const Transform3D &p_prev_cam_inv_transform, RID p_shadow_atlas, int p_directional_light_count, bool p_use_directional_shadows, int p_positional_light_count, int p_voxel_gi_count, const PagedArray<RID> &p_fog_volumes) {
	ERR_FAIL_COND(p_render_buffers.is_null());

	Ref<RenderBufferDataForwardClustered> rb_data = p_render_buffers->get_custom_data(RB_SCOPE_FORWARD_CLUSTERED);
	ERR_FAIL_COND(rb_data.is_null());

	ERR_FAIL_COND(!p_render_buffers->has_custom_data(RB_SCOPE_GI));
	Ref<RendererRD::GI::RenderBuffersGI> rbgi = p_render_buffers->get_custom_data(RB_SCOPE_GI);

	Ref<RendererRD::GI::SDFGI> sdfgi;
	if (p_render_buffers->has_custom_data(RB_SCOPE_SDFGI)) {
		sdfgi = p_render_buffers->get_custom_data(RB_SCOPE_SDFGI);
	}

	Size2i size = p_render_buffers->get_internal_size();
	float ratio = float(size.x) / float((size.x + size.y) / 2);
	uint32_t target_width = uint32_t(float(get_volumetric_fog_size()) * ratio);
	uint32_t target_height = uint32_t(float(get_volumetric_fog_size()) / ratio);

	if (p_render_buffers->has_custom_data(RB_SCOPE_FOG)) {
		Ref<RendererRD::Fog::VolumetricFog> fog = p_render_buffers->get_custom_data(RB_SCOPE_FOG);
		//validate
		if (p_environment.is_null() || !environment_get_volumetric_fog_enabled(p_environment) || fog->width != target_width || fog->height != target_height || fog->depth != get_volumetric_fog_depth()) {
			p_render_buffers->set_custom_data(RB_SCOPE_FOG, Ref<RenderBufferCustomDataRD>());
		}
	}

	if (p_environment.is_null() || !environment_get_volumetric_fog_enabled(p_environment)) {
		//no reason to enable or update, bye
		return;
	}

	if (p_environment.is_valid() && environment_get_volumetric_fog_enabled(p_environment) && !p_render_buffers->has_custom_data(RB_SCOPE_FOG)) {
		//required volumetric fog but not existing, create
		Ref<RendererRD::Fog::VolumetricFog> fog;

		fog.instantiate();
		fog->init(Vector3i(target_width, target_height, get_volumetric_fog_depth()), sky.sky_shader.default_shader_rd);

		p_render_buffers->set_custom_data(RB_SCOPE_FOG, fog);
	}

	if (p_render_buffers->has_custom_data(RB_SCOPE_FOG)) {
		Ref<RendererRD::Fog::VolumetricFog> fog = p_render_buffers->get_custom_data(RB_SCOPE_FOG);

		RendererRD::Fog::VolumetricFogSettings settings;
		settings.rb_size = size;
		settings.time = time;
		settings.is_using_radiance_octmap_array = is_using_radiance_octmap_array();
		settings.max_cluster_elements = RendererRD::LightStorage::get_singleton()->get_max_cluster_elements();
		settings.volumetric_fog_filter_active = get_volumetric_fog_filter_active();

		settings.shadow_sampler = shadow_sampler;
		settings.shadow_atlas_depth = RendererRD::LightStorage::get_singleton()->owns_shadow_atlas(p_shadow_atlas) ? RendererRD::LightStorage::get_singleton()->shadow_atlas_get_texture(p_shadow_atlas) : RID();
		settings.voxel_gi_buffer = rbgi->get_voxel_gi_buffer();
		settings.omni_light_buffer = RendererRD::LightStorage::get_singleton()->get_omni_light_buffer();
		settings.spot_light_buffer = RendererRD::LightStorage::get_singleton()->get_spot_light_buffer();
		settings.area_light_buffer = RendererRD::LightStorage::get_singleton()->get_area_light_buffer();
		settings.area_light_atlas = RendererRD::TextureStorage::get_singleton()->area_light_atlas_get_texture();
		settings.directional_shadow_depth = RendererRD::LightStorage::get_singleton()->directional_shadow_get_texture();
		settings.directional_light_buffer = RendererRD::LightStorage::get_singleton()->get_directional_light_buffer();

		settings.vfog = fog;
		settings.cluster_builder = rb_data->cluster_builder;
		settings.rbgi = rbgi;
		settings.sdfgi = sdfgi;
		settings.env = p_environment;
		settings.sky = &sky;
		settings.gi = &gi;

		RendererRD::Fog::get_singleton()->volumetric_fog_update(settings, p_cam_projection, p_cam_transform, p_prev_cam_inv_transform, p_shadow_atlas, p_directional_light_count, p_use_directional_shadows, p_positional_light_count, p_voxel_gi_count, p_fog_volumes);
	}
}

/* Lighting */

void RenderForwardClustered::setup_added_reflection_probe(const Transform3D &p_transform, const Vector3 &p_half_size) {
	if (current_cluster_builder != nullptr) {
		current_cluster_builder->add_box(ClusterBuilderRD::BOX_TYPE_REFLECTION_PROBE, p_transform, p_half_size);
	}
}

void RenderForwardClustered::setup_added_light(const RSE::LightType p_type, const Transform3D &p_transform, float p_radius, float p_spot_aperture, const Vector2 &p_area_size) {
	if (current_cluster_builder != nullptr) {
		ClusterBuilderRD::LightType type;
		if (p_type == RSE::LIGHT_SPOT) {
			type = ClusterBuilderRD::LIGHT_TYPE_SPOT;
		} else if (p_type == RSE::LIGHT_OMNI) {
			type = ClusterBuilderRD::LIGHT_TYPE_OMNI;
		} else {
			type = ClusterBuilderRD::LIGHT_TYPE_AREA;
		}

		current_cluster_builder->add_light(type, p_transform, p_radius, p_spot_aperture, p_area_size);
	}
}

void RenderForwardClustered::setup_added_decal(const Transform3D &p_transform, const Vector3 &p_half_size) {
	if (current_cluster_builder != nullptr) {
		current_cluster_builder->add_box(ClusterBuilderRD::BOX_TYPE_DECAL, p_transform, p_half_size);
	}
}

/* Render scene */

void RenderForwardClustered::_process_ssao(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_environment, const RID *p_normal_buffers, const Projection *p_projections) {
	ERR_FAIL_NULL(ss_effects);
	ERR_FAIL_COND(p_render_buffers.is_null());
	ERR_FAIL_COND(p_environment.is_null());

	Ref<RenderBufferDataForwardClustered> rb_data = p_render_buffers->get_custom_data(RB_SCOPE_FORWARD_CLUSTERED);
	ERR_FAIL_COND(rb_data.is_null());

	RENDER_TIMESTAMP("Process SSAO");

	RendererRD::SSEffects::SSAOSettings settings;
	settings.radius = environment_get_ssao_radius(p_environment);
	settings.intensity = environment_get_ssao_intensity(p_environment);
	settings.power = environment_get_ssao_power(p_environment);
	settings.detail = environment_get_ssao_detail(p_environment);
	settings.horizon = environment_get_ssao_horizon(p_environment);
	settings.sharpness = environment_get_ssao_sharpness(p_environment);
	settings.full_screen_size = p_render_buffers->get_internal_size();

	ss_effects->ssao_allocate_buffers(p_render_buffers, rb_data->ss_effects_data.ssao, settings);

	for (uint32_t v = 0; v < p_render_buffers->get_view_count(); v++) {
		ss_effects->generate_ssao(p_render_buffers, rb_data->ss_effects_data.ssao, v, p_normal_buffers[v], p_projections[v], settings);
	}
}

void RenderForwardClustered::_process_ssil(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_environment, const RID *p_normal_buffers, const Projection *p_projections, const Transform3D &p_transform) {
	ERR_FAIL_NULL(ss_effects);
	ERR_FAIL_COND(p_render_buffers.is_null());
	ERR_FAIL_COND(p_environment.is_null());

	Ref<RenderBufferDataForwardClustered> rb_data = p_render_buffers->get_custom_data(RB_SCOPE_FORWARD_CLUSTERED);
	ERR_FAIL_COND(rb_data.is_null());

	RENDER_TIMESTAMP("Process SSIL");

	RendererRD::SSEffects::SSILSettings settings;
	settings.radius = environment_get_ssil_radius(p_environment);
	settings.intensity = environment_get_ssil_intensity(p_environment);
	settings.sharpness = environment_get_ssil_sharpness(p_environment);
	settings.normal_rejection = environment_get_ssil_normal_rejection(p_environment);
	settings.full_screen_size = p_render_buffers->get_internal_size();

	ss_effects->ssil_allocate_buffers(p_render_buffers, rb_data->ss_effects_data.ssil, settings);

	Transform3D transform = p_transform;
	transform.set_origin(Vector3(0.0, 0.0, 0.0));

	for (uint32_t v = 0; v < p_render_buffers->get_view_count(); v++) {
		Projection correction;
		correction.set_depth_correction(true);
		Projection projection = correction * p_projections[v];
		Projection last_frame_projection = rb_data->ss_effects_data.ssil_last_frame_projections[v] * Projection(rb_data->ss_effects_data.ssil_last_frame_transform.affine_inverse()) * Projection(transform) * projection.inverse();

		ss_effects->screen_space_indirect_lighting(p_render_buffers, rb_data->ss_effects_data.ssil, v, p_normal_buffers[v], p_projections[v], last_frame_projection, settings);

		rb_data->ss_effects_data.ssil_last_frame_projections[v] = projection;
	}
	rb_data->ss_effects_data.ssil_last_frame_transform = transform;
}

void RenderForwardClustered::_process_ssr(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_environment, const RID *p_normal_slices, const Projection *p_projections, const Vector3 *p_eye_offsets, const Transform3D &p_transform) {
	ERR_FAIL_NULL(ss_effects);
	ERR_FAIL_COND(p_render_buffers.is_null());

	Ref<RenderBufferDataForwardClustered> rb_data = p_render_buffers->get_custom_data(RB_SCOPE_FORWARD_CLUSTERED);
	ERR_FAIL_COND(rb_data.is_null());

	RENDER_TIMESTAMP("Process SSR");

	ss_effects->ssr_allocate_buffers(p_render_buffers, rb_data->ss_effects_data.ssr, p_render_buffers->get_base_data_format());

	Projection reprojections[RendererSceneRender::MAX_RENDER_VIEWS];

	for (uint32_t v = 0; v < p_render_buffers->get_view_count(); v++) {
		Projection correction;
		correction.set_depth_correction(true);

		Projection projection = correction * p_projections[v];
		reprojections[v] = rb_data->ss_effects_data.ssr_last_frame_projections[v] * Projection(rb_data->ss_effects_data.ssr_last_frame_transform.affine_inverse()) * Projection(p_transform) * projection.inverse();

		rb_data->ss_effects_data.ssr_last_frame_projections[v] = projection;
	}
	rb_data->ss_effects_data.ssr_last_frame_transform = p_transform;

	ss_effects->screen_space_reflection(p_render_buffers, rb_data->ss_effects_data.ssr, p_normal_slices, environment_get_ssr_max_steps(p_environment), environment_get_ssr_fade_in(p_environment), environment_get_ssr_fade_out(p_environment), environment_get_ssr_depth_tolerance(p_environment), p_projections, reprojections, p_eye_offsets, *copy_effects);
}

void RenderForwardClustered::_copy_framebuffer_to_ss_effects(Ref<RenderSceneBuffersRD> p_render_buffers, bool p_use_ssil, bool p_use_ssr) {
	ERR_FAIL_NULL(ss_effects);
	ERR_FAIL_COND(p_render_buffers.is_null());

	ss_effects->copy_internal_texture_to_last_frame(p_render_buffers, *copy_effects);
}

void RenderForwardClustered::_fellow_process_ss_effects(RenderDataRD *p_render_data, const Ref<RenderSceneBuffersRD> &rb, bool p_use_ssao, bool p_use_ssil, bool p_use_ssr, const RID *p_normal_roughness_slices) {
	// Stock's screen-space effects block of _pre_opaque_render(), unchanged (moved so that async compute, decision
	// 0015, can record it before the shadows).
	// Note, in multiview we're allocating buffers for each eye/view we're rendering.
	// This should allow most of the processing to happen in parallel even if we're doing
	// drawcalls per eye/view. It will all sync up at the barrier.

	if (p_use_ssil || p_use_ssr) {
		ss_effects->allocate_last_frame_buffer(rb, p_use_ssil, p_use_ssr);
	}

	if (p_use_ssao || p_use_ssil) {
		RENDER_TIMESTAMP("Prepare Depth for SSAO/SSIL");
		// Convert our depth buffer data to linear data in
		for (uint32_t v = 0; v < rb->get_view_count(); v++) {
			ss_effects->downsample_depth(rb, v, p_render_data->scene_data->view_projection[v]);
		}

		if (p_use_ssao) {
			_process_ssao(rb, p_render_data->environment, p_normal_roughness_slices, p_render_data->scene_data->view_projection);
		}

		if (p_use_ssil) {
			_process_ssil(rb, p_render_data->environment, p_normal_roughness_slices, p_render_data->scene_data->view_projection, p_render_data->scene_data->cam_transform);
		}
	}

	if (p_use_ssr) {
		_process_ssr(rb, p_render_data->environment, p_normal_roughness_slices, p_render_data->scene_data->view_projection, p_render_data->scene_data->view_eye_offset, p_render_data->scene_data->cam_transform);
	}
}

void RenderForwardClustered::_pre_opaque_render(RenderDataRD *p_render_data, bool p_use_ssao, bool p_use_ssil, bool p_use_ssr, bool p_use_gi, const RID *p_normal_roughness_slices, RID p_voxel_gi_buffer) {
	// Render shadows while GI is rendering, due to how barriers are handled, this should happen at the same time
	RendererRD::LightStorage *light_storage = RendererRD::LightStorage::get_singleton();
	RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();

	Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	Ref<RenderBufferDataForwardClustered> rb_data;
	if (rb.is_valid() && rb->has_custom_data(RB_SCOPE_FORWARD_CLUSTERED)) {
		// Our forward clustered custom data buffer will only be available when we're rendering our normal view.
		// This will not be available when rendering reflection probes.
		rb_data = rb->get_custom_data(RB_SCOPE_FORWARD_CLUSTERED);
	}

	// FellowEngine async compute (decision 0015): the screen-space effects only need the depth pre-pass. Recorded first
	// and submitted to the second queue, they run while the main queue renders the shadows, the light clusters and the
	// volumetric fog; the work recorded after this function (the opaque pass reads their results) waits for them. Not
	// with GI (it reads the same normal buffers and runs between the shadows).
	const bool fellow_async = rb_data.is_valid() && ss_effects && (p_use_ssao || p_use_ssil || p_use_ssr) && !(rb.is_valid() && p_use_gi) && RD::get_singleton()->fellow_async_compute_begin();
	if (fellow_async) {
		_fellow_process_ss_effects(p_render_data, rb, p_use_ssao, p_use_ssil, p_use_ssr, p_normal_roughness_slices);
		RD::get_singleton()->fellow_async_compute_end();
	}

	RENDER_TIMESTAMP("Setup Shadows");

	if (rb.is_valid() && p_use_gi && rb->has_custom_data(RB_SCOPE_SDFGI)) {
		Ref<RendererRD::GI::SDFGI> sdfgi = rb->get_custom_data(RB_SCOPE_SDFGI);
		sdfgi->store_probes();
	}

	Size2i viewport_size = Size2i(1, 1);
	if (rb.is_valid()) {
		viewport_size = rb->get_internal_size();
	}

	p_render_data->cube_shadows.clear();
	p_render_data->shadows.clear();
	p_render_data->directional_shadows.clear();

	float lod_distance_multiplier = p_render_data->scene_data->cam_projection.get_lod_multiplier();
	{
		for (int i = 0; i < p_render_data->render_shadow_count; i++) {
			RID li = p_render_data->render_shadows[i].light;
			RID base = light_storage->light_instance_get_base_light(li);

			if (light_storage->light_get_type(base) == RSE::LIGHT_DIRECTIONAL) {
				p_render_data->directional_shadows.push_back(i);
			} else if (light_storage->light_get_type(base) == RSE::LIGHT_OMNI && light_storage->light_omni_get_shadow_mode(base) == RSE::LIGHT_OMNI_SHADOW_CUBE) {
				p_render_data->cube_shadows.push_back(i);
			} else {
				p_render_data->shadows.push_back(i);
			}
		}

		if (p_render_data->cube_shadows.size()) {
			RENDER_TIMESTAMP("Render OmniLight Shadows");
			// Cube shadows are rendered in their own way.
			for (const int &index : p_render_data->cube_shadows) {
				_render_shadow_pass(p_render_data->render_shadows[index].light, p_render_data->shadow_atlas, p_render_data->render_shadows[index].pass, p_render_data->render_shadows[index].instances, lod_distance_multiplier, p_render_data->scene_data->screen_mesh_lod_threshold, true, true, true, p_render_data->render_info, viewport_size, p_render_data->scene_data->cam_transform);
			}
		}

		if (p_render_data->directional_shadows.size()) {
			//open the pass for directional shadows
			light_storage->update_directional_shadow_atlas();
			// FellowEngine directional shadow cache: cascades served from the cache overwrite their whole rect (or keep
			// it, when unchanged), so the atlas only needs clearing if some cascade renders the stock way.
			bool fellow_all_cached = true;
			bool fellow_any_cached = false;
			for (const int &index : p_render_data->directional_shadows) {
				if (p_render_data->render_shadows[index].fellow.cache_id < 0) {
					fellow_all_cached = false;
				} else {
					fellow_any_cached = true;
				}
			}
			fellow_dir_clear_stock_rects = false;
			if (!fellow_all_cached) {
				// With no cascade from the cache there is nothing to keep: one clear of the whole atlas, as stock (a
				// clear of the whole attachment is cheaper than clears of parts of it).
				if (fellow_any_cached && Fellow::directional_shadow_cache() && light_storage->fellow_directional_shadow_grid_full()) {
					// The lights' rects tile the atlas: each stock cascade clears its own rect (same content as clearing
					// the whole atlas), the cached ones keep theirs.
					fellow_dir_clear_stock_rects = true;
				} else {
					RD::get_singleton()->draw_list_begin(light_storage->direction_shadow_get_fb(), RD::DRAW_CLEAR_DEPTH, Vector<Color>(), 0.0f);
					RD::get_singleton()->draw_list_end();
					fellow_shadow_cache->atlas_invalidate_all();
					fellow_dir_atlas_dirty_all = true;
				}
			}
		}
	}

	// Render GI

	bool render_shadows = p_render_data->directional_shadows.size() || p_render_data->shadows.size();
	bool render_gi = rb.is_valid() && p_use_gi;

	if (render_shadows && render_gi) {
		RENDER_TIMESTAMP("Render GI + Render Directional/SpotLight Shadows (Parallel)");
	} else if (render_shadows) {
		RENDER_TIMESTAMP("Render Directional/SpotLight Shadows");
	} else if (render_gi) {
		RENDER_TIMESTAMP("Render GI");
	}

	//prepare shadow rendering
	if (render_shadows) {
		_render_shadow_begin();

		//render directional shadows
		for (uint32_t i = 0; i < p_render_data->directional_shadows.size(); i++) {
			_render_shadow_pass(p_render_data->render_shadows[p_render_data->directional_shadows[i]].light, p_render_data->shadow_atlas, p_render_data->render_shadows[p_render_data->directional_shadows[i]].pass, p_render_data->render_shadows[p_render_data->directional_shadows[i]].instances, lod_distance_multiplier, p_render_data->scene_data->screen_mesh_lod_threshold, false, i == p_render_data->directional_shadows.size() - 1, false, p_render_data->render_info, viewport_size, p_render_data->scene_data->cam_transform, &p_render_data->render_shadows[p_render_data->directional_shadows[i]].fellow);
		}
		//render positional shadows
		for (uint32_t i = 0; i < p_render_data->shadows.size(); i++) {
			_render_shadow_pass(p_render_data->render_shadows[p_render_data->shadows[i]].light, p_render_data->shadow_atlas, p_render_data->render_shadows[p_render_data->shadows[i]].pass, p_render_data->render_shadows[p_render_data->shadows[i]].instances, lod_distance_multiplier, p_render_data->scene_data->screen_mesh_lod_threshold, i == 0, i == p_render_data->shadows.size() - 1, true, p_render_data->render_info, viewport_size, p_render_data->scene_data->cam_transform);
		}

		_render_shadow_process();
	}

	if (render_gi) {
		gi.process_gi(rb, p_normal_roughness_slices, p_voxel_gi_buffer, p_render_data->environment, p_render_data->scene_data->view_count, p_render_data->scene_data->view_projection, p_render_data->scene_data->view_eye_offset, p_render_data->scene_data->cam_transform, *p_render_data->voxel_gi_instances);
	}

	if (render_shadows) {
		_render_shadow_end();
	}

	// FellowEngine: min/max window pyramids of the shadow atlases for the shadow filters' early-outs, brought up to date
	// over the atlas texels written since their last update (none in a static view with the shadow cache, none for a
	// positional atlas whose shadows were not re-rendered).
	p_render_data->scene_data->fellow_shadow_max_mip = false;
	p_render_data->scene_data->fellow_positional_shadow_max_mip = false;
	if (fellow_shadow_max_mip) {
		if (Fellow::shadow_directional_max_mip() || Fellow::shadow_positional_max_mip()) {
			RENDER_TIMESTAMP("FellowEngine Shadow Pyramids");
		}
		if (Fellow::shadow_directional_max_mip() && render_shadows && p_render_data->directional_shadows.size() > 0 && light_storage->directional_shadow_get_texture().is_valid()) {
			const int atlas_size = light_storage->directional_shadow_get_size();
			const RID atlas_texture = light_storage->directional_shadow_get_texture();
			const Size2i size(atlas_size, atlas_size);
			const bool use_16_bits = light_storage->directional_shadow_get_use_16_bits();
			const bool full = fellow_dir_atlas_dirty_all || !fellow_shadow_max_mip->is_valid(RID());
			if (Fellow::shadow_max_mip_visible_region()) {
				// Only the part of each cascade this camera can look up is computed from the atlas (exact for any
				// choice: the other windows hold (0, 1), a bound of any content). A cascade whose texels were not
				// written keeps its windows, unless this camera needs more of it than was computed.
				if (full) {
					fellow_dir_built.clear();
				}
				LocalVector<Rect2i> dirty;
				if (full) {
					dirty.push_back(Rect2i(Point2i(), size));
				} else {
					for (const Rect2i &d : fellow_dir_atlas_dirty) {
						dirty.push_back(d);
					}
				}
				LocalVector<Rect2i> valid;
				for (const FellowDirCascade &c : fellow_dir_cascades) {
					const Rect2i needed = _fellow_dir_needed_rect(c, p_render_data->scene_data);
					bool written = full;
					for (const Rect2i &d : fellow_dir_atlas_dirty) {
						written = written || d.intersects(c.rect);
					}
					int64_t built_index = -1;
					for (uint32_t k = 0; k < fellow_dir_built.size(); k++) {
						if (fellow_dir_built[k].first == c.rect) {
							built_index = k;
						}
					}
					Rect2i computed = needed;
					if (!written) {
						if (built_index >= 0 && fellow_dir_built[built_index].second.encloses(needed)) {
							valid.push_back(fellow_dir_built[built_index].second); // unchanged, enough of it computed
							continue;
						}
						if (built_index >= 0) {
							computed = needed.merge(fellow_dir_built[built_index].second);
						}
						dirty.push_back(c.rect); // unchanged texels, but more of them needed now
					}
					valid.push_back(computed);
					if (built_index >= 0) {
						fellow_dir_built[built_index].second = computed;
					} else {
						fellow_dir_built.push_back(Pair<Rect2i, Rect2i>(c.rect, computed));
					}
				}
				// Written texels outside every cascade of this camera (another light's or view's): computed whole.
				for (const Rect2i &d : fellow_dir_atlas_dirty) {
					bool covered = false;
					for (const FellowDirCascade &c : fellow_dir_cascades) {
						covered = covered || c.rect.encloses(d);
					}
					if (!covered) {
						valid.push_back(d);
					}
				}
				// Cascade rects whose pyramid an earlier camera computed and that this camera did not render: they lie
				// inside a merged rebuild region only by chance; keep what was computed for them.
				for (const Pair<Rect2i, Rect2i> &b : fellow_dir_built) {
					bool listed = false;
					for (const FellowDirCascade &c : fellow_dir_cascades) {
						listed = listed || c.rect == b.first;
					}
					if (!listed) {
						valid.push_back(b.second);
					}
				}
				if (full) {
					fellow_shadow_max_mip->build(RID(), atlas_texture, size, Rect2i(Point2i(), size), use_16_bits, &valid);
				} else {
					fellow_shadow_max_mip->build_rects(RID(), atlas_texture, size, dirty, use_16_bits, &valid);
				}
			} else {
				fellow_dir_built.clear();
				if (full) {
					fellow_shadow_max_mip->build(RID(), atlas_texture, size, Rect2i(Point2i(), size), use_16_bits);
				} else {
					fellow_shadow_max_mip->build_rects(RID(), atlas_texture, size, fellow_dir_atlas_dirty, use_16_bits);
				}
			}
			p_render_data->scene_data->fellow_shadow_max_mip = fellow_shadow_max_mip->is_valid(RID());
		} else {
			fellow_shadow_max_mip->invalidate(RID());
		}

		const RID pos_atlas = p_render_data->shadow_atlas;
		if (pos_atlas.is_valid() && light_storage->owns_shadow_atlas(pos_atlas)) {
			FellowAtlasDirty &d = fellow_pos_atlas_dirty[pos_atlas];
			const int atlas_size = light_storage->shadow_atlas_get_size(pos_atlas);
			const RID atlas_texture = light_storage->shadow_atlas_get_texture(pos_atlas);
			if (Fellow::shadow_positional_max_mip() && atlas_size > 0 && atlas_texture.is_valid()) {
				const Size2i size(atlas_size, atlas_size);
				const bool use_16_bits = light_storage->shadow_atlas_get_use_16_bits(pos_atlas);
				if (d.all || !fellow_shadow_max_mip->is_valid(pos_atlas)) {
					fellow_shadow_max_mip->build(pos_atlas, atlas_texture, size, Rect2i(Point2i(), size), use_16_bits);
				} else {
					fellow_shadow_max_mip->build_rects(pos_atlas, atlas_texture, size, d.rects, use_16_bits);
				}
				p_render_data->scene_data->fellow_positional_shadow_max_mip = fellow_shadow_max_mip->is_valid(pos_atlas);
			} else {
				fellow_shadow_max_mip->invalidate(pos_atlas);
			}
			d.rects.clear();
			d.all = false;
		}
		// Atlases that were freed (their viewports are gone).
		fellow_shadow_max_mip->free_unused([&](RID p_atlas) { return light_storage->owns_shadow_atlas(p_atlas); });
		LocalVector<RID> gone;
		for (const KeyValue<RID, FellowAtlasDirty> &kv : fellow_pos_atlas_dirty) {
			if (!light_storage->owns_shadow_atlas(kv.key)) {
				gone.push_back(kv.key);
			}
		}
		for (const RID &k : gone) {
			fellow_pos_atlas_dirty.erase(k);
		}
	}
	fellow_dir_atlas_dirty.clear();
	fellow_dir_atlas_dirty_all = false;
	fellow_dir_cascades.clear();

	if (rb_data.is_valid() && ss_effects && !fellow_async) {
		_fellow_process_ss_effects(p_render_data, rb, p_use_ssao, p_use_ssil, p_use_ssr, p_normal_roughness_slices);
	}

	RENDER_TIMESTAMP("Pre Opaque Render");

	if (current_cluster_builder) {
		// Note: when rendering stereoscopic (multiview) we are using our combined frustum projection to create
		// our cluster data. We use reprojection in the shader to adjust for our left/right eye.
		// This only works as we don't filter our cluster by depth buffer.
		// If we ever make this optimization we should make it optional and only use it in mono.
		// What we win by filtering out a few lights, we loose by having to do the work double for stereo.
		// FellowEngine depth slices (fellow_settings.h): mono only, since the cluster depths are those of the combined
		// (center) view.
		const bool fellow_log_depth = Fellow::light_cluster_depth_slices() && p_render_data->scene_data->view_count == 1;
		current_cluster_builder->begin(p_render_data->scene_data->cam_transform, p_render_data->scene_data->cam_projection, !p_render_data->reflection_probe.is_valid(), fellow_log_depth);
		p_render_data->scene_data->fellow_cluster_log_depth = current_cluster_builder->fellow_get_log_depth();
	}

	bool using_shadows = true;

	if (p_render_data->reflection_probe.is_valid()) {
		if (!RSG::light_storage->reflection_probe_renders_shadows(light_storage->reflection_probe_instance_get_probe(p_render_data->reflection_probe))) {
			using_shadows = false;
		}
	} else {
		//do not render reflections when rendering a reflection probe
		light_storage->update_reflection_probe_buffer(p_render_data, *p_render_data->reflection_probes, p_render_data->scene_data->cam_transform.affine_inverse(), p_render_data->environment);
	}

	uint32_t directional_light_count = 0;
	uint32_t positional_light_count = 0;
	light_storage->update_light_buffers(p_render_data, *p_render_data->lights, p_render_data->scene_data->cam_transform, p_render_data->shadow_atlas, using_shadows, directional_light_count, positional_light_count, p_render_data->directional_light_soft_shadows);
	texture_storage->update_decal_buffer(*p_render_data->decals, p_render_data->scene_data->cam_transform);

	p_render_data->directional_light_count = directional_light_count;

	if (current_cluster_builder) {
		current_cluster_builder->bake_cluster();
	}

	if (rb_data.is_valid()) {
		RENDER_TIMESTAMP("Update Volumetric Fog");
		bool directional_shadows = RendererRD::LightStorage::get_singleton()->has_directional_shadows(directional_light_count);
		_update_volumetric_fog(rb, p_render_data->environment, p_render_data->scene_data->cam_projection, p_render_data->scene_data->cam_transform, p_render_data->scene_data->prev_cam_transform.affine_inverse(), p_render_data->shadow_atlas, directional_light_count, directional_shadows, positional_light_count, p_render_data->voxel_gi_count, *p_render_data->fog_volumes);
	}

	if (fellow_async) {
		RD::get_singleton()->fellow_async_compute_join();
	}
}

void RenderForwardClustered::_process_sss(Ref<RenderSceneBuffersRD> p_render_buffers, const Projection &p_camera) {
	ERR_FAIL_COND(p_render_buffers.is_null());

	Size2i internal_size = p_render_buffers->get_internal_size();
	bool can_use_effects = internal_size.x >= 8 && internal_size.y >= 8;

	if (!can_use_effects) {
		//just copy
		return;
	}

	p_render_buffers->allocate_blur_textures();

	for (uint32_t v = 0; v < p_render_buffers->get_view_count(); v++) {
		RID internal_texture = p_render_buffers->get_internal_texture(v);
		RID depth_texture = p_render_buffers->get_depth_texture(v);
		ss_effects->sub_surface_scattering(p_render_buffers, internal_texture, depth_texture, p_camera, internal_size);
	}
}

void RenderForwardClustered::_render_scene(RenderDataRD *p_render_data, const Color &p_default_bg_color) {
	scene_state.used_uniform_buffer_count = 0;

	RendererRD::LightStorage *light_storage = RendererRD::LightStorage::get_singleton();

	ERR_FAIL_NULL(p_render_data);

	Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	ERR_FAIL_COND(rb.is_null());
	Ref<RenderBufferDataForwardClustered> rb_data;
	if (rb->has_custom_data(RB_SCOPE_FORWARD_CLUSTERED)) {
		// Our forward clustered custom data buffer will only be available when we're rendering our normal view.
		// This will not be available when rendering reflection probes.
		rb_data = rb->get_custom_data(RB_SCOPE_FORWARD_CLUSTERED);
	}
	bool is_reflection_probe = p_render_data->reflection_probe.is_valid();

	static const int texture_multisamples[RSE::VIEWPORT_MSAA_MAX] = { 1, 2, 4, 8 };

	//first of all, make a new render pass
	//fill up ubo

	RENDER_TIMESTAMP("Prepare 3D Scene");

	// get info about our rendering effects
	bool ce_needs_motion_vectors = _compositor_effects_has_flag(p_render_data, RSE::COMPOSITOR_EFFECT_FLAG_NEEDS_MOTION_VECTORS);
	bool ce_needs_normal_roughness = _compositor_effects_has_flag(p_render_data, RSE::COMPOSITOR_EFFECT_FLAG_NEEDS_ROUGHNESS);
	bool ce_needs_separate_specular = _compositor_effects_has_flag(p_render_data, RSE::COMPOSITOR_EFFECT_FLAG_NEEDS_SEPARATE_SPECULAR);

	// sdfgi first
	_update_sdfgi(p_render_data);

	// assign render indices to voxel_gi_instances
	for (uint32_t i = 0; i < (uint32_t)p_render_data->voxel_gi_instances->size(); i++) {
		RID voxel_gi_instance = (*p_render_data->voxel_gi_instances)[i];
		gi.voxel_gi_instance_set_render_index(voxel_gi_instance, i);
	}

	// obtain cluster builder
	if (light_storage->owns_reflection_probe_instance(p_render_data->reflection_probe)) {
		current_cluster_builder = light_storage->reflection_probe_instance_get_cluster_builder(p_render_data->reflection_probe, &cluster_builder_shared);

		if (p_render_data->camera_attributes.is_valid()) {
			light_storage->reflection_probe_set_baked_exposure(light_storage->reflection_probe_instance_get_probe(p_render_data->reflection_probe), RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_render_data->camera_attributes));
		}
	} else if (rb_data.is_valid()) {
		current_cluster_builder = rb_data->cluster_builder;

		p_render_data->voxel_gi_count = 0;

		if (rb->has_custom_data(RB_SCOPE_SDFGI)) {
			Ref<RendererRD::GI::SDFGI> sdfgi = rb->get_custom_data(RB_SCOPE_SDFGI);
			if (sdfgi.is_valid()) {
				sdfgi->update_cascades();
				sdfgi->pre_process_gi(p_render_data->scene_data->cam_transform, p_render_data);
				sdfgi->update_light();
			}
		}

		gi.setup_voxel_gi_instances(p_render_data, p_render_data->render_buffers, p_render_data->scene_data->cam_transform, *p_render_data->voxel_gi_instances, p_render_data->voxel_gi_count);
	} else {
		ERR_PRINT("No render buffer nor reflection atlas, bug"); // Should never happen!
		current_cluster_builder = nullptr;
		return; // No point in continuing, we'll just crash.
	}

	ERR_FAIL_NULL(current_cluster_builder);

	p_render_data->cluster_buffer = current_cluster_builder->get_cluster_buffer();
	p_render_data->cluster_size = current_cluster_builder->get_cluster_size();
	p_render_data->cluster_max_elements = current_cluster_builder->get_max_cluster_elements();

	_update_vrs(rb);
	_fellow_adaptive_vrs_begin(rb);

	RENDER_TIMESTAMP("Setup 3D Scene");

	bool using_debug_mvs = get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_MOTION_VECTORS;
	bool using_taa = rb->get_use_taa();

	enum {
		SCALE_NONE,
		SCALE_FSR2,
		SCALE_MFX,
	} scale_type = SCALE_NONE;

	switch (rb->get_scaling_3d_mode()) {
		case RSE::VIEWPORT_SCALING_3D_MODE_FSR2:
			scale_type = SCALE_FSR2;
			break;
		case RSE::VIEWPORT_SCALING_3D_MODE_METALFX_TEMPORAL:
#ifdef METAL_MFXTEMPORAL_ENABLED
			scale_type = SCALE_MFX;
#else
			scale_type = SCALE_NONE;
#endif
			break;
		default:
			break;
	}

	bool using_upscaling = scale_type != SCALE_NONE;

	// check if we need motion vectors
	bool motion_vectors_required;
	if (using_debug_mvs) {
		motion_vectors_required = true;
	} else if (ce_needs_motion_vectors) {
		motion_vectors_required = true;
	} else if (!is_reflection_probe && using_taa) {
		motion_vectors_required = true;
	} else if (!is_reflection_probe && using_upscaling) {
		motion_vectors_required = true;
	} else {
		motion_vectors_required = false;
	}

	//p_render_data->scene_data->subsurface_scatter_width = subsurface_scatter_size;
	p_render_data->scene_data->calculate_motion_vectors = motion_vectors_required;
	p_render_data->scene_data->directional_light_count = 0;
	p_render_data->scene_data->opaque_prepass_threshold = 0.99f;

	Size2i screen_size;
	RID color_framebuffer;
	RID color_only_framebuffer;
	RID depth_framebuffer;
	RendererRD::MaterialStorage::Samplers samplers;

	PassMode depth_pass_mode = PASS_MODE_DEPTH;
	uint32_t color_pass_flags = 0;
	Vector<Color> depth_pass_clear;
	bool using_separate_specular = false;
	bool using_ssr = false;
	bool using_sdfgi = false;
	bool using_voxelgi = false;
	bool reverse_cull = p_render_data->scene_data->cam_transform.basis.determinant() < 0;
	bool using_ssil = !is_reflection_probe && p_render_data->environment.is_valid() && environment_get_ssil_enabled(p_render_data->environment);
	// FellowEngine (decision 0013): TAA takes the upscalers' motion-pass path. Static surfaces are drawn without
	// motion vectors and their velocity comes from the depth buffer (MotionVectorsStore::process_fellow_taa); surfaces
	// that move or animate are drawn in the motion pass with the motion-vector shaders.
	fellow_taa_motion_pass = rb_data.is_valid() && !using_upscaling && !is_reflection_probe && using_taa && rb->get_view_count() == 1 && rb->get_msaa_3d() == RSE::VIEWPORT_MSAA_DISABLED && rb->get_can_be_storage() && p_render_data->scene_data->calculate_motion_vectors && Fellow::taa_camera_motion_vectors();
	bool using_motion_pass = rb_data.is_valid() && (using_upscaling || fellow_taa_motion_pass);

	if (is_reflection_probe) {
		uint32_t resolution = light_storage->reflection_probe_instance_get_resolution(p_render_data->reflection_probe);
		screen_size.x = resolution;
		screen_size.y = resolution;

		color_framebuffer = light_storage->reflection_probe_instance_get_framebuffer(p_render_data->reflection_probe, p_render_data->reflection_probe_pass);
		color_only_framebuffer = color_framebuffer;
		depth_framebuffer = light_storage->reflection_probe_instance_get_depth_framebuffer(p_render_data->reflection_probe, p_render_data->reflection_probe_pass);

		if (light_storage->reflection_probe_is_interior(light_storage->reflection_probe_instance_get_probe(p_render_data->reflection_probe))) {
			p_render_data->environment = RID(); //no environment on interiors
		}

		reverse_cull = true; // for some reason our views are inverted
		samplers = RendererRD::MaterialStorage::get_singleton()->samplers_rd_get_default();

		// Indicate pipelines for reflection probes are required.
		global_pipeline_data_required.use_reflection_probes = true;
	} else {
		screen_size = rb->get_internal_size();

		if (p_render_data->scene_data->calculate_motion_vectors) {
			color_pass_flags |= COLOR_PASS_FLAG_MOTION_VECTORS;
			scene_shader.enable_advanced_shader_group();

			// Indicate pipelines for motion vectors are required.
			global_pipeline_data_required.use_motion_vectors = true;
		}

		if (p_render_data->voxel_gi_instances->size() > 0) {
			using_voxelgi = true;
		}

		if (p_render_data->environment.is_valid()) {
			if (environment_get_sdfgi_enabled(p_render_data->environment) && get_debug_draw_mode() != RSE::VIEWPORT_DEBUG_DRAW_UNSHADED) {
				using_sdfgi = true;
			}
			if (environment_get_ssr_enabled(p_render_data->environment)) {
				if (!p_render_data->transparent_bg) {
					using_ssr = true;
				} else {
					WARN_PRINT_ONCE("Screen-space reflections are not supported in viewports with a transparent background. Disabling SSR in transparent viewport.");
				}
			}
		}

		if (p_render_data->scene_data->view_count > 1) {
			color_pass_flags |= COLOR_PASS_FLAG_MULTIVIEW;
			// Try enabling here in case is_xr_enabled() returns false.
			scene_shader.shader.enable_group(SceneShaderForwardClustered::SHADER_GROUP_MULTIVIEW);

			// Indicate pipelines for multiview are required.
			global_pipeline_data_required.use_multiview = true;
		}

		color_framebuffer = rb_data->get_color_pass_fb(color_pass_flags);
		color_only_framebuffer = rb_data->get_color_only_fb();
		samplers = rb->get_samplers();
	}

	p_render_data->scene_data->emissive_exposure_normalization = -1.0;

	RD::get_singleton()->draw_command_begin_label("Render Setup");

	_setup_lightmaps(p_render_data, *p_render_data->lightmaps, p_render_data->scene_data->cam_transform);
	_setup_voxelgis(*p_render_data->voxel_gi_instances);
	uint32_t depth_prepass_uniform_buffer_index = _setup_environment(p_render_data, is_reflection_probe, screen_size, screen_size, p_default_bg_color, false);

	// May have changed due to the above (light buffer enlarged, as an example).
	_update_render_base_uniform_set();

	// FellowEngine mesh clusters: start deferred builds, apply finished ones (never waits; once per frame).
	RendererRD::MeshStorage::get_singleton()->fellow_update_clusters();

	// FellowEngine mesh clusters: view volume of the depth pre-pass and the opaque pass (the projection the GPU uses,
	// TAA jitter included).
	LocalVector<Plane> fellow_cluster_planes;
	if (Fellow::mesh_clusters() && p_render_data->scene_data->view_count == 1) {
		Projection jitter;
		jitter.add_jitter_offset(p_render_data->scene_data->taa_jitter);
		const Vector<Plane> planes = (jitter * p_render_data->scene_data->cam_projection).get_projection_planes(p_render_data->scene_data->cam_transform);
		for (const Plane &plane : planes) {
			fellow_cluster_planes.push_back(plane);
		}
	}

	const bool fellow_markers = Fellow::list_markers();
	if (fellow_markers) {
		RENDER_TIMESTAMP("FellowEngine Opaque List Fill");
	}
	_fill_render_list(RENDER_LIST_OPAQUE, p_render_data, PASS_MODE_COLOR, using_sdfgi, using_sdfgi || using_voxelgi, using_motion_pass);
	if (fellow_markers) {
		RENDER_TIMESTAMP("FellowEngine Opaque List Sort");
	}
	if (Fellow::cpu_compact_list_sort()) {
		_fellow_sort_by_key(RENDER_LIST_OPAQUE, 0, render_list[RENDER_LIST_OPAQUE].elements.size());
		_fellow_sort_by_key(RENDER_LIST_MOTION, 0, render_list[RENDER_LIST_MOTION].elements.size());
	} else {
		render_list[RENDER_LIST_OPAQUE].sort_by_key();
		render_list[RENDER_LIST_MOTION].sort_by_key();
	}
	render_list[RENDER_LIST_ALPHA].sort_by_reverse_depth_and_priority();

	fellow_occlusion.filtered = false;
	if (Fellow::debug_occlusion_cull_approx() && !is_reflection_probe && p_render_data->render_buffers.is_valid()) {
		_fellow_occlusion_filter(RENDER_LIST_OPAQUE);
		_fellow_occlusion_filter(RENDER_LIST_MOTION);
	}

	int *render_info = p_render_data->render_info ? p_render_data->render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_VISIBLE] : (int *)nullptr;
	if (fellow_markers) {
		RENDER_TIMESTAMP("FellowEngine Opaque Instance Data");
	}
	_fill_instance_data(RENDER_LIST_OPAQUE, render_info);
	_fill_instance_data(RENDER_LIST_MOTION, render_info);
	_fill_instance_data(RENDER_LIST_ALPHA, render_info);
	if (fellow_markers) {
		RENDER_TIMESTAMP("FellowEngine Setup 3D Scene (rest)");
	}

	RD::get_singleton()->draw_command_end_label();

	if (!is_reflection_probe) {
		if (using_voxelgi) {
			depth_pass_mode = PASS_MODE_DEPTH_NORMAL_ROUGHNESS_VOXEL_GI;
		} else if (p_render_data->environment.is_valid()) {
			if (using_ssr ||
					using_sdfgi ||
					environment_get_ssao_enabled(p_render_data->environment) ||
					using_ssil ||
					ce_needs_normal_roughness ||
					get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_NORMAL_BUFFER ||
					scene_state.used_normal_texture) {
				depth_pass_mode = PASS_MODE_DEPTH_NORMAL_ROUGHNESS;
			}
		} else if (get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_NORMAL_BUFFER || scene_state.used_normal_texture) {
			depth_pass_mode = PASS_MODE_DEPTH_NORMAL_ROUGHNESS;
		}

		switch (depth_pass_mode) {
			case PASS_MODE_DEPTH: {
				depth_framebuffer = rb_data->get_depth_fb();
			} break;
			case PASS_MODE_DEPTH_NORMAL_ROUGHNESS: {
				depth_framebuffer = rb_data->get_depth_fb(RenderBufferDataForwardClustered::DEPTH_FB_ROUGHNESS);
				depth_pass_clear.push_back(Color(0, 0, 0, 0));
			} break;
			case PASS_MODE_DEPTH_NORMAL_ROUGHNESS_VOXEL_GI: {
				depth_framebuffer = rb_data->get_depth_fb(RenderBufferDataForwardClustered::DEPTH_FB_ROUGHNESS_VOXELGI);
				depth_pass_clear.push_back(Color(0, 0, 0, 0));
				depth_pass_clear.push_back(Color(0, 0, 0, 0));
			} break;
			default: {
			};
		}
	}

	bool using_sss = rb_data.is_valid() && !is_reflection_probe && scene_state.used_sss && ss_effects->sss_get_quality() != RSE::SUB_SURFACE_SCATTERING_QUALITY_DISABLED;

	if (using_sss && p_render_data->transparent_bg) {
		WARN_PRINT_ONCE("Sub-surface scattering is not supported in viewports with a transparent background. Disabling SSS in transparent viewport.");
		using_sss = false;
	}

	if ((using_sss || ce_needs_separate_specular) && !using_separate_specular) {
		using_separate_specular = true;
		color_pass_flags |= COLOR_PASS_FLAG_SEPARATE_SPECULAR;
		color_framebuffer = rb_data->get_color_pass_fb(color_pass_flags);
	}

	// Ensure this is allocated so we don't get a stutter the first time an object with SSS appears on screen.
	if (global_surface_data.sss_used && !is_reflection_probe) {
		rb_data->ensure_specular();
	}

	if (global_surface_data.normal_texture_used && !is_reflection_probe) {
		rb_data->ensure_normal_roughness_texture();
	}

	if (using_sss || using_separate_specular || scene_state.used_lightmap || using_voxelgi || global_surface_data.sss_used) {
		scene_shader.enable_advanced_shader_group(p_render_data->scene_data->view_count > 1);
	}

	// Update the global pipeline requirements with all the features found to be in use in this scene.
	if (depth_pass_mode == PASS_MODE_DEPTH_NORMAL_ROUGHNESS || global_surface_data.normal_texture_used) {
		global_pipeline_data_required.use_normal_and_roughness = true;
	}

	if (scene_state.used_lightmap || scene_state.lightmaps_used > 0) {
		global_pipeline_data_required.use_lightmaps = true;
	}

	if (using_voxelgi) {
		global_pipeline_data_required.use_voxelgi = true;
	}

	if (using_separate_specular || global_surface_data.sss_used) {
		global_pipeline_data_required.use_separate_specular = true;
	}

	// Update the compiled pipelines if any of the requirements have changed.
	_update_dirty_geometry_pipelines();

	RID radiance_texture;
	bool draw_sky = false;
	bool draw_sky_fog_only = false;
	// We invert luminance_multiplier for sky so that we can combine it with exposure value.
	float sky_luminance_multiplier = 1.0 / rb->get_luminance_multiplier();
	float sky_brightness_multiplier = 1.0;

	Color clear_color;
	bool load_color = false;

	if (get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_OVERDRAW) {
		clear_color = Color(0, 0, 0, 1); //in overdraw mode, BG should always be black
	} else if (is_environment(p_render_data->environment)) {
		RSE::EnvironmentBG bg_mode = environment_get_background(p_render_data->environment);
		float bg_energy_multiplier = environment_get_bg_energy_multiplier(p_render_data->environment);
		bg_energy_multiplier *= environment_get_bg_intensity(p_render_data->environment);
		RSE::EnvironmentReflectionSource reflection_source = environment_get_reflection_source(p_render_data->environment);

		if (p_render_data->camera_attributes.is_valid()) {
			bg_energy_multiplier *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(p_render_data->camera_attributes);
		}

		switch (bg_mode) {
			case RSE::ENV_BG_CLEAR_COLOR: {
				clear_color = p_default_bg_color;
				clear_color.r *= bg_energy_multiplier;
				clear_color.g *= bg_energy_multiplier;
				clear_color.b *= bg_energy_multiplier;
				if (!p_render_data->transparent_bg && (rb->has_custom_data(RB_SCOPE_FOG) || environment_get_fog_enabled(p_render_data->environment))) {
					draw_sky_fog_only = true;
					RendererRD::MaterialStorage::get_singleton()->material_set_param(sky.sky_scene_state.fog_material, "clear_color", Variant(clear_color.srgb_to_linear()));
				}
			} break;
			case RSE::ENV_BG_COLOR: {
				clear_color = environment_get_bg_color(p_render_data->environment);
				clear_color.r *= bg_energy_multiplier;
				clear_color.g *= bg_energy_multiplier;
				clear_color.b *= bg_energy_multiplier;
				if (!p_render_data->transparent_bg && (rb->has_custom_data(RB_SCOPE_FOG) || environment_get_fog_enabled(p_render_data->environment))) {
					draw_sky_fog_only = true;
					RendererRD::MaterialStorage::get_singleton()->material_set_param(sky.sky_scene_state.fog_material, "clear_color", Variant(clear_color.srgb_to_linear()));
				}
			} break;
			case RSE::ENV_BG_SKY: {
				draw_sky = !p_render_data->transparent_bg;
			} break;
			case RSE::ENV_BG_CANVAS: {
				if (!is_reflection_probe) {
					RID texture = RendererRD::TextureStorage::get_singleton()->render_target_get_rd_texture(rb->get_render_target());
					bool convert_to_linear = !RendererRD::TextureStorage::get_singleton()->render_target_is_using_hdr(rb->get_render_target());
					copy_effects->copy_to_fb_rect(texture, color_only_framebuffer, Rect2i(), false, false, false, false, RID(), false, false, convert_to_linear);
				}
				load_color = true;
			} break;
			case RSE::ENV_BG_KEEP: {
				load_color = true;
			} break;
			case RSE::ENV_BG_CAMERA_FEED: {
			} break;
			default: {
			}
		}

		// setup sky if used for ambient, reflections, or background
		if (draw_sky || draw_sky_fog_only || (reflection_source == RSE::ENV_REFLECTION_SOURCE_BG && bg_mode == RSE::ENV_BG_SKY) || reflection_source == RSE::ENV_REFLECTION_SOURCE_SKY || environment_get_ambient_source(p_render_data->environment) == RSE::ENV_AMBIENT_SOURCE_SKY) {
			RENDER_TIMESTAMP("Setup Sky");
			RD::get_singleton()->draw_command_begin_label("Setup Sky");

			// Setup our sky render information for this frame/viewport
			sky.setup_sky(p_render_data, screen_size);

			sky_brightness_multiplier *= bg_energy_multiplier;

			RID sky_rid = environment_get_sky(p_render_data->environment);
			if (sky_rid.is_valid()) {
				sky.update_radiance_buffers(rb, p_render_data->environment, p_render_data->scene_data->cam_transform.origin, time, sky_luminance_multiplier, sky_brightness_multiplier);
				radiance_texture = sky.sky_get_radiance_texture_rd(sky_rid);
			} else {
				// do not try to draw sky if invalid
				draw_sky = false;
			}

			if (draw_sky || draw_sky_fog_only) {
				// update sky half/quarter res buffers (if required)
				sky.update_res_buffers(rb, p_render_data->environment, time, sky_luminance_multiplier, sky_brightness_multiplier);
			}

			RD::get_singleton()->draw_command_end_label();
		}
	} else {
		clear_color = p_default_bg_color;
	}

	RSE::ViewportMSAA msaa = rb->get_msaa_3d();
	bool use_msaa = msaa != RSE::VIEWPORT_MSAA_DISABLED;

	bool ce_pre_opaque_resolved_color = use_msaa && _compositor_effects_has_flag(p_render_data, RSE::COMPOSITOR_EFFECT_FLAG_ACCESS_RESOLVED_COLOR, RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_PRE_OPAQUE);
	bool ce_post_opaque_resolved_color = use_msaa && _compositor_effects_has_flag(p_render_data, RSE::COMPOSITOR_EFFECT_FLAG_ACCESS_RESOLVED_COLOR, RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_OPAQUE);
	bool ce_pre_transparent_resolved_color = use_msaa && _compositor_effects_has_flag(p_render_data, RSE::COMPOSITOR_EFFECT_FLAG_ACCESS_RESOLVED_COLOR, RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_PRE_TRANSPARENT);

	bool ce_pre_opaque_resolved_depth = use_msaa && _compositor_effects_has_flag(p_render_data, RSE::COMPOSITOR_EFFECT_FLAG_ACCESS_RESOLVED_DEPTH, RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_PRE_OPAQUE);
	bool ce_post_opaque_resolved_depth = use_msaa && _compositor_effects_has_flag(p_render_data, RSE::COMPOSITOR_EFFECT_FLAG_ACCESS_RESOLVED_DEPTH, RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_OPAQUE);
	bool ce_pre_transparent_resolved_depth = use_msaa && _compositor_effects_has_flag(p_render_data, RSE::COMPOSITOR_EFFECT_FLAG_ACCESS_RESOLVED_DEPTH, RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_PRE_TRANSPARENT);

	bool debug_voxelgis = get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_VOXEL_GI_ALBEDO || get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_VOXEL_GI_LIGHTING || get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_VOXEL_GI_EMISSION;
	bool debug_sdfgi_probes = get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_SDFGI_PROBES;
	bool force_depth_pre_pass = scene_state.used_opaque_stencil;
	bool depth_pre_pass = (force_depth_pre_pass || bool(GLOBAL_GET_CACHED(bool, "rendering/driver/depth_prepass/enable"))) && depth_framebuffer.is_valid();

	SceneShaderForwardClustered::ShaderSpecialization base_specialization = scene_shader.default_specialization;
	base_specialization.use_depth_fog = p_render_data->environment.is_valid() && environment_get_fog_mode(p_render_data->environment) == RSE::EnvironmentFogMode::ENV_FOG_MODE_DEPTH;
	base_specialization.fellow_pcss_blend_band = Fellow::shadow_pcss_blend_band();
	base_specialization.fellow_directional_max_mip = Fellow::shadow_directional_max_mip();
	base_specialization.fellow_positional_max_mip = Fellow::shadow_positional_max_mip();
	base_specialization.fellow_zero_attenuation_skip = Fellow::light_zero_attenuation_skip();
	base_specialization.fellow_skip_backfacing_positional = Fellow::shadow_skip_backfacing_positional();
	base_specialization.fellow_skip_backfacing_directional = Fellow::shadow_skip_backfacing();
	base_specialization.fellow_skip_unblended_cascade = Fellow::shadow_skip_unblended_cascade();
	base_specialization.fellow_cluster_log_depth = Fellow::light_cluster_depth_slices();
	base_specialization.fellow_blended_alpha_zero_skip = Fellow::blended_alpha_zero_skip();

	bool using_ssao = depth_pre_pass && !is_reflection_probe && p_render_data->environment.is_valid() && environment_get_ssao_enabled(p_render_data->environment);

	if (depth_pre_pass) { //depth pre pass
		bool needs_pre_resolve = _needs_post_prepass_render(p_render_data, using_sdfgi || using_voxelgi);
		if (needs_pre_resolve) {
			RENDER_TIMESTAMP("GI + Render Depth Pre-Pass (Parallel)");
		} else {
			RENDER_TIMESTAMP("Render Depth Pre-Pass");
		}
		if (needs_pre_resolve) {
			//pre clear the depth framebuffer, as AMD (and maybe others?) use compute for it, and barrier other compute shaders.
			RD::get_singleton()->draw_list_begin(depth_framebuffer, RD::DRAW_CLEAR_ALL, depth_pass_clear, 0.0f);
			RD::get_singleton()->draw_list_end();
			//start compute processes here, so they run at the same time as depth pre-pass
			_post_prepass_render(p_render_data, using_sdfgi || using_voxelgi);
		}

		RD::get_singleton()->draw_command_begin_label("Render Depth Pre-Pass");

		RID rp_uniform_set = _setup_render_pass_uniform_set(RENDER_LIST_OPAQUE, nullptr, RID(), samplers, depth_prepass_uniform_buffer_index);

		bool finish_depth = using_ssao || using_ssil || using_sdfgi || using_voxelgi || ce_pre_opaque_resolved_depth || ce_post_opaque_resolved_depth;
		RenderListParameters render_list_params(render_list[RENDER_LIST_OPAQUE].elements.ptr(), render_list[RENDER_LIST_OPAQUE].element_info.ptr(), render_list[RENDER_LIST_OPAQUE].elements.size(), reverse_cull, depth_pass_mode, 0, rb_data.is_null(), p_render_data->directional_light_soft_shadows, rp_uniform_set, get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_WIREFRAME, Vector2(), p_render_data->scene_data->lod_distance_multiplier, p_render_data->scene_data->screen_mesh_lod_threshold, p_render_data->scene_data->view_count, 0, base_specialization);
		render_list_params.fellow_cluster_planes = fellow_cluster_planes.ptr();
		render_list_params.fellow_cluster_plane_count = fellow_cluster_planes.size();
		_render_list_with_draw_list(&render_list_params, depth_framebuffer, RD::DrawFlags(needs_pre_resolve ? RD::DRAW_DEFAULT_ALL : RD::DRAW_CLEAR_ALL), depth_pass_clear, 0.0f, 0u, p_render_data->render_region);

		RD::get_singleton()->draw_command_end_label();

		if (use_msaa) {
			RENDER_TIMESTAMP("Resolve Depth Pre-Pass (MSAA)");
			RD::get_singleton()->draw_command_begin_label("Resolve Depth Pre-Pass (MSAA)");
			if (depth_pass_mode == PASS_MODE_DEPTH_NORMAL_ROUGHNESS || depth_pass_mode == PASS_MODE_DEPTH_NORMAL_ROUGHNESS_VOXEL_GI) {
				for (uint32_t v = 0; v < rb->get_view_count(); v++) {
					resolve_effects->resolve_gi(rb->get_depth_msaa(v), rb_data->get_normal_roughness_msaa(v), using_voxelgi ? rb_data->get_voxelgi_msaa(v) : RID(), rb->get_depth_texture(v), rb_data->get_normal_roughness(v), using_voxelgi ? rb_data->get_voxelgi(v) : RID(), rb->get_internal_size(), texture_multisamples[msaa]);
				}
			} else if (finish_depth) {
				for (uint32_t v = 0; v < rb->get_view_count(); v++) {
					resolve_effects->resolve_depth(rb->get_depth_msaa(v), rb->get_depth_texture(v), rb->get_internal_size(), texture_multisamples[msaa]);
				}
			}
			RD::get_singleton()->draw_command_end_label();
		}
	}

	if (depth_pre_pass && (Fellow::debug_occlusion_stats() || fellow_occlusion.filtered) && !is_reflection_probe) {
		_fellow_occlusion_stats(p_render_data);
	}

	{
		if (ce_pre_opaque_resolved_color) {
			// We haven't rendered color data yet so...
			WARN_PRINT_ONCE("Pre opaque rendering effects can't access resolved color buffers.");
		}

		if (ce_pre_opaque_resolved_depth && !depth_pre_pass) {
			// We haven't rendered depth data yet so...
			WARN_PRINT_ONCE("Pre opaque rendering effects can't access resolved depth buffers.");
		}

		RENDER_TIMESTAMP("Process Pre Opaque Compositor Effects");
		_process_compositor_effects(RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_PRE_OPAQUE, p_render_data);
	}

	RID normal_roughness_views[RendererSceneRender::MAX_RENDER_VIEWS];
	if (rb_data.is_valid() && rb_data->has_normal_roughness()) {
		for (uint32_t v = 0; v < rb->get_view_count(); v++) {
			normal_roughness_views[v] = rb_data->get_normal_roughness(v);
		}
	}
	_pre_opaque_render(p_render_data, using_ssao, using_ssil, using_ssr, using_sdfgi || using_voxelgi, normal_roughness_views, rb_data.is_valid() && rb_data->has_voxelgi() ? rb_data->get_voxelgi() : RID());

	if (current_cluster_builder) {
		base_specialization.cluster_has_area_light = current_cluster_builder->get_cluster_count_by_type(ClusterBuilderRD::ELEMENT_TYPE_AREA_LIGHT) != 0;
	}

	RENDER_TIMESTAMP("Render Opaque Pass");

	RD::get_singleton()->draw_command_begin_label("Render Opaque Pass");

	p_render_data->scene_data->directional_light_count = p_render_data->directional_light_count;
	p_render_data->scene_data->opaque_prepass_threshold = 0.0f;

	// Shadow pass can change the base uniform set samplers.
	_update_render_base_uniform_set();

	// The upscalers' motion pass also zeroes the opaque pass alpha (pass_alpha_multiplier); stock's TAA path does not.
	uint32_t opaque_pass_uniform_buffer_index = _setup_environment(p_render_data, is_reflection_probe, screen_size, screen_size, p_default_bg_color, true, using_motion_pass && !fellow_taa_motion_pass);

	// FellowEngine occlusion culling (decision 0026): the opaque colour pass's runs tested against the full pre-pass's depth.
	bool fellow_occlusion_culled = false;
	if (Fellow::occlusion_culling() && depth_pre_pass && !use_msaa && !is_reflection_probe && rb->get_view_count() == 1 && get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_DISABLED && !fellow_occlusion.filtered) {
		fellow_occlusion_culled = _fellow_occlusion_cull(p_render_data, using_motion_pass ? (color_pass_flags & ~uint32_t(COLOR_PASS_FLAG_MOTION_VECTORS)) : color_pass_flags);
	}
	fellow_occlusion.pass_active = fellow_occlusion_culled;
	RID rp_uniform_set = _setup_render_pass_uniform_set(RENDER_LIST_OPAQUE, p_render_data, radiance_texture, samplers, opaque_pass_uniform_buffer_index, true);
	fellow_occlusion.pass_active = false;

	{
		bool render_motion_pass = !render_list[RENDER_LIST_MOTION].elements.is_empty();

		{
			Vector<Color> c;
			if (!load_color) {
				Color cc = clear_color.srgb_to_linear();
				if (using_separate_specular || rb_data.is_valid()) {
					// Effects that rely on separate specular, like subsurface scattering, must clear the alpha to zero.
					cc.a = 0;
				}
				c.push_back(cc);

				if (rb_data.is_valid()) {
					c.push_back(Color(0, 0, 0, 0)); // Separate specular.
					c.push_back(Color(0, 0, 0, 0)); // Motion vector. Pushed to the clear color vector even if the framebuffer isn't bound.
				}
			}

			uint32_t opaque_color_pass_flags = using_motion_pass ? (color_pass_flags & ~uint32_t(COLOR_PASS_FLAG_MOTION_VECTORS)) : color_pass_flags;
			RID opaque_framebuffer = using_motion_pass ? rb_data->get_color_pass_fb(opaque_color_pass_flags) : color_framebuffer;
			RenderListParameters render_list_params(render_list[RENDER_LIST_OPAQUE].elements.ptr(), render_list[RENDER_LIST_OPAQUE].element_info.ptr(), render_list[RENDER_LIST_OPAQUE].elements.size(), reverse_cull, PASS_MODE_COLOR, opaque_color_pass_flags, rb_data.is_null(), p_render_data->directional_light_soft_shadows, rp_uniform_set, get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_WIREFRAME, Vector2(), p_render_data->scene_data->lod_distance_multiplier, p_render_data->scene_data->screen_mesh_lod_threshold, p_render_data->scene_data->view_count, 0, base_specialization);
			render_list_params.fellow_cluster_planes = fellow_cluster_planes.ptr();
			render_list_params.fellow_cluster_plane_count = fellow_cluster_planes.size();
			if (fellow_occlusion_culled) {
				render_list_params.fellow_occlusion_runs = fellow_occlusion.run_of_element.ptr();
				render_list_params.fellow_occlusion_args = fellow_occlusion.args;
			}
			_render_list_with_draw_list(&render_list_params, opaque_framebuffer, RD::DrawFlags(load_color ? RD::DRAW_DEFAULT_ALL : RD::DRAW_CLEAR_COLOR_ALL) | (depth_pre_pass ? RD::DRAW_DEFAULT_ALL : RD::DRAW_CLEAR_DEPTH), c, 0.0f, 0u, p_render_data->render_region);
		}

		RD::get_singleton()->draw_command_end_label();

		if (using_motion_pass) {
			if (scale_type == SCALE_MFX) {
				motion_vectors_store->process(rb,
						p_render_data->scene_data->cam_projection, p_render_data->scene_data->cam_transform,
						p_render_data->scene_data->prev_cam_projection, p_render_data->scene_data->prev_cam_transform);
			} else if (fellow_taa_motion_pass) {
				RENDER_TIMESTAMP("FellowEngine Camera Motion Vectors");
				if (motion_vectors_store == nullptr) {
					motion_vectors_store = memnew(RendererRD::MotionVectorsStore); // Stock creates it only for MetalFX.
				}
				motion_vectors_store->process_fellow_taa(rb,
						p_render_data->scene_data->cam_projection, p_render_data->scene_data->cam_transform, p_render_data->scene_data->taa_jitter,
						p_render_data->scene_data->prev_cam_projection, p_render_data->scene_data->prev_cam_transform);
			} else {
				Vector<Color> motion_vector_clear_colors;
				motion_vector_clear_colors.push_back(Color(-1, -1, 0, 0));
				RD::get_singleton()->draw_list_begin(rb_data->get_velocity_only_fb(), RD::DRAW_CLEAR_ALL, motion_vector_clear_colors);
				RD::get_singleton()->draw_list_end();
			}
		}

		if (render_motion_pass) {
			RD::get_singleton()->draw_command_begin_label("Render Motion Pass");

			RENDER_TIMESTAMP("Render Motion Pass");

			rp_uniform_set = _setup_render_pass_uniform_set(RENDER_LIST_MOTION, p_render_data, radiance_texture, samplers, opaque_pass_uniform_buffer_index, true);

			RenderListParameters render_list_params(render_list[RENDER_LIST_MOTION].elements.ptr(), render_list[RENDER_LIST_MOTION].element_info.ptr(), render_list[RENDER_LIST_MOTION].elements.size(), reverse_cull, PASS_MODE_COLOR, color_pass_flags, rb_data.is_null(), p_render_data->directional_light_soft_shadows, rp_uniform_set, get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_WIREFRAME, Vector2(), p_render_data->scene_data->lod_distance_multiplier, p_render_data->scene_data->screen_mesh_lod_threshold, p_render_data->scene_data->view_count, 0, base_specialization);
			render_list_params.fellow_cluster_planes = fellow_cluster_planes.ptr();
			render_list_params.fellow_cluster_plane_count = fellow_cluster_planes.size();
			_render_list_with_draw_list(&render_list_params, color_framebuffer);

			RD::get_singleton()->draw_command_end_label();
		}
	}

	{
		if (ce_post_opaque_resolved_color) {
			for (uint32_t v = 0; v < rb->get_view_count(); v++) {
				RD::get_singleton()->texture_resolve_multisample(rb->get_color_msaa(v), rb->get_internal_texture(v));
			}
		}

		if (ce_post_opaque_resolved_depth) {
			for (uint32_t v = 0; v < rb->get_view_count(); v++) {
				resolve_effects->resolve_depth(rb->get_depth_msaa(v), rb->get_depth_texture(v), rb->get_internal_size(), texture_multisamples[msaa]);
			}
		}

		RENDER_TIMESTAMP("Process Post Opaque Compositor Effects");
		_process_compositor_effects(RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_OPAQUE, p_render_data);
	}

	if (debug_voxelgis) {
		Projection dc;
		dc.set_depth_correction(true);
		Projection cm = (dc * p_render_data->scene_data->cam_projection) * Projection(p_render_data->scene_data->cam_transform.affine_inverse());
		RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(color_only_framebuffer);
		RD::get_singleton()->draw_command_begin_label("Debug VoxelGIs");
		for (int i = 0; i < (int)p_render_data->voxel_gi_instances->size(); i++) {
			gi.debug_voxel_gi((*p_render_data->voxel_gi_instances)[i], draw_list, color_only_framebuffer, cm, get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_VOXEL_GI_LIGHTING, get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_VOXEL_GI_EMISSION, 1.0);
		}
		RD::get_singleton()->draw_command_end_label();
		RD::get_singleton()->draw_list_end();
	}

	if (debug_sdfgi_probes) {
		Projection dc;
		dc.set_depth_correction(true);
		Projection cms[RendererSceneRender::MAX_RENDER_VIEWS];
		for (uint32_t v = 0; v < p_render_data->scene_data->view_count; v++) {
			cms[v] = (dc * p_render_data->scene_data->view_projection[v]) * Projection(p_render_data->scene_data->cam_transform.affine_inverse());
		}
		_debug_sdfgi_probes(rb, color_only_framebuffer, p_render_data->scene_data->view_count, cms);
	}

	if (draw_sky || draw_sky_fog_only) {
		RENDER_TIMESTAMP("Render Sky");

		RD::get_singleton()->draw_command_begin_label("Draw Sky");
		RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(color_only_framebuffer, RD::DRAW_DEFAULT_ALL, Vector<Color>(), 1.0f, 0u, p_render_data->render_region);

		sky.draw_sky(draw_list, rb, p_render_data->environment, color_only_framebuffer, time, sky_luminance_multiplier, sky_brightness_multiplier);

		RD::get_singleton()->draw_list_end();
		RD::get_singleton()->draw_command_end_label();
	}

	if (use_msaa) {
		RENDER_TIMESTAMP("Resolve MSAA");

		if (scene_state.used_screen_texture || using_separate_specular || ce_pre_transparent_resolved_color) {
			for (uint32_t v = 0; v < rb->get_view_count(); v++) {
				RD::get_singleton()->texture_resolve_multisample(rb->get_color_msaa(v), rb->get_internal_texture(v));
			}
			if (using_separate_specular) {
				for (uint32_t v = 0; v < rb->get_view_count(); v++) {
					RD::get_singleton()->texture_resolve_multisample(rb_data->get_specular_msaa(v), rb_data->get_specular(v));
				}
			}
		}

		if (scene_state.used_depth_texture || scene_state.used_normal_texture || using_separate_specular || ce_needs_normal_roughness || ce_pre_transparent_resolved_depth) {
			for (uint32_t v = 0; v < rb->get_view_count(); v++) {
				resolve_effects->resolve_depth(rb->get_depth_msaa(v), rb->get_depth_texture(v), rb->get_internal_size(), texture_multisamples[msaa]);
			}
		}
	}

	{
		RENDER_TIMESTAMP("Process Post Sky Compositor Effects");
		// Don't need to check for depth or color resolve here, we've already triggered it.
		_process_compositor_effects(RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_SKY, p_render_data);
	}

	if (using_separate_specular) {
		if (using_sss) {
			RENDER_TIMESTAMP("Sub-Surface Scattering");
			RD::get_singleton()->draw_command_begin_label("Process Sub-Surface Scattering");
			_process_sss(rb, p_render_data->scene_data->cam_projection);
			RD::get_singleton()->draw_command_end_label();
		}

		{
			//just mix specular back
			RENDER_TIMESTAMP("Merge Specular");
			copy_effects->merge_specular(color_only_framebuffer, rb_data->get_specular(), !use_msaa ? RID() : rb->get_internal_texture(), RID(), p_render_data->scene_data->view_count);
		}
	}

	if (using_separate_specular && is_environment(p_render_data->environment) && (environment_get_background(p_render_data->environment) == RSE::ENV_BG_CANVAS)) {
		// Canvas background mode does not clear the color buffer, but copies over it. If screen-space specular effects are enabled and the background is blank,
		// this results in ghosting due to the separate specular buffer copy. Need to explicitly clear the specular buffer once we're done with it to fix it.
		RENDER_TIMESTAMP("Clear Separate Specular (Canvas Background Mode)");
		Vector<Color> blank_clear_color;
		blank_clear_color.push_back(Color(0.0, 0.0, 0.0));
		RD::get_singleton()->draw_list_begin(rb_data->get_specular_only_fb(), RD::DRAW_CLEAR_ALL, blank_clear_color);
		RD::get_singleton()->draw_list_end();
	}

	if (rb_data.is_valid() && using_upscaling) {
		// Make sure the upscaled texture is initialized, but not necessarily filled, before running screen copies
		// so it properly detect if a dedicated copy texture should be used.
		rb->ensure_upscaled();
	}

	if (scene_state.used_screen_texture || global_surface_data.screen_texture_used) {
		RENDER_TIMESTAMP("Copy Screen Texture");

		_render_buffers_ensure_screen_texture(p_render_data);

		if (scene_state.used_screen_texture) {
			// Copy screen texture to backbuffer so we can read from it
			_render_buffers_copy_screen_texture(p_render_data);
		}
	}

	if (scene_state.used_depth_texture || global_surface_data.depth_texture_used) {
		RENDER_TIMESTAMP("Copy Depth Texture");

		_render_buffers_ensure_depth_texture(p_render_data);

		if (scene_state.used_depth_texture) {
			// Copy depth texture to backbuffer so we can read from it
			_render_buffers_copy_depth_texture(p_render_data);
		}
	}

	{
		if (using_separate_specular) {
			// Our specular will be combined back in (and effects, subsurface scattering and/or ssr applied),
			// so if we've requested this, we need another copy.
			// Fairly unlikely scenario though.

			if (ce_pre_transparent_resolved_color) {
				for (uint32_t v = 0; v < rb->get_view_count(); v++) {
					RD::get_singleton()->texture_resolve_multisample(rb->get_color_msaa(v), rb->get_internal_texture(v));
				}
			}

			if (ce_pre_transparent_resolved_depth) {
				for (uint32_t v = 0; v < rb->get_view_count(); v++) {
					resolve_effects->resolve_depth(rb->get_depth_msaa(v), rb->get_depth_texture(v), rb->get_internal_size(), texture_multisamples[msaa]);
				}
			}
		}

		RENDER_TIMESTAMP("Process Pre Transparent Compositor Effects");
		_process_compositor_effects(RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_PRE_TRANSPARENT, p_render_data);
	}

	RENDER_TIMESTAMP("Render 3D Transparent Pass");

	RD::get_singleton()->draw_command_begin_label("Render 3D Transparent Pass");

	uint32_t transparent_pass_uniform_buffer_index = _setup_environment(p_render_data, is_reflection_probe, screen_size, screen_size, p_default_bg_color, false);

	rp_uniform_set = _setup_render_pass_uniform_set(RENDER_LIST_ALPHA, p_render_data, radiance_texture, samplers, transparent_pass_uniform_buffer_index, true);

	{
		uint32_t transparent_color_pass_flags = (color_pass_flags | uint32_t(COLOR_PASS_FLAG_TRANSPARENT)) & ~uint32_t(COLOR_PASS_FLAG_SEPARATE_SPECULAR);
		// Motion vectors should not be overwritten by transparent objects.
		transparent_color_pass_flags &= ~uint32_t(COLOR_PASS_FLAG_MOTION_VECTORS);

		RID alpha_framebuffer = rb_data.is_valid() ? rb_data->get_color_pass_fb(transparent_color_pass_flags) : color_only_framebuffer;
		RenderListParameters render_list_params(render_list[RENDER_LIST_ALPHA].elements.ptr(), render_list[RENDER_LIST_ALPHA].element_info.ptr(), render_list[RENDER_LIST_ALPHA].elements.size(), reverse_cull, PASS_MODE_COLOR, transparent_color_pass_flags, rb_data.is_null(), p_render_data->directional_light_soft_shadows, rp_uniform_set, get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_WIREFRAME, Vector2(), p_render_data->scene_data->lod_distance_multiplier, p_render_data->scene_data->screen_mesh_lod_threshold, p_render_data->scene_data->view_count, 0, base_specialization);
		render_list_params.fellow_cluster_planes = fellow_cluster_planes.ptr();
		render_list_params.fellow_cluster_plane_count = fellow_cluster_planes.size();
		_render_list_with_draw_list(&render_list_params, alpha_framebuffer, RD::DRAW_DEFAULT_ALL, Vector<Color>(), 0.0f, 0u, p_render_data->render_region);
	}

	RD::get_singleton()->draw_command_end_label();

	RENDER_TIMESTAMP("Resolve");

	RD::get_singleton()->draw_command_begin_label("Resolve");

	if (rb_data.is_valid() && use_msaa) {
		bool resolve_velocity_buffer = (using_taa || using_upscaling || ce_needs_motion_vectors) && rb->has_velocity_buffer(true);
		for (uint32_t v = 0; v < rb->get_view_count(); v++) {
			RD::get_singleton()->texture_resolve_multisample(rb->get_color_msaa(v), rb->get_internal_texture(v));
			resolve_effects->resolve_depth(rb->get_depth_msaa(v), rb->get_depth_texture(v), rb->get_internal_size(), texture_multisamples[msaa]);

			if (resolve_velocity_buffer) {
				RD::get_singleton()->texture_resolve_multisample(rb->get_velocity_buffer(true, v), rb->get_velocity_buffer(false, v));
			}
		}
	}

	RD::get_singleton()->draw_command_end_label();

	RD::get_singleton()->draw_command_begin_label("Copy Framebuffer for SSIL/SSR");
	if (using_ssil || using_ssr) {
		RENDER_TIMESTAMP("Copy Final Framebuffer (SSIL/SSR)");
		_copy_framebuffer_to_ss_effects(rb, using_ssil, using_ssr);
	}
	RD::get_singleton()->draw_command_end_label();

	{
		RENDER_TIMESTAMP("Process Post Transparent Compositor Effects");
		_process_compositor_effects(RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_TRANSPARENT, p_render_data);
	}

	if (rb_data.is_valid() && (using_upscaling || using_taa)) {
		if (scale_type == SCALE_FSR2) {
			rb_data->ensure_fsr2(fsr2_effect);

			RID exposure;
			if (RSG::camera_attributes->camera_attributes_uses_auto_exposure(p_render_data->camera_attributes)) {
				exposure = luminance->get_current_luminance_buffer(rb);
			}

			RD::get_singleton()->draw_command_begin_label("FSR2");
			RENDER_TIMESTAMP("FSR2");

			for (uint32_t v = 0; v < rb->get_view_count(); v++) {
				real_t fov = p_render_data->scene_data->cam_projection.get_fov();
				real_t aspect = p_render_data->scene_data->cam_projection.get_aspect();
				real_t fovy = p_render_data->scene_data->cam_projection.get_fovy(fov, 1.0 / aspect);
				Vector2 jitter = p_render_data->scene_data->taa_jitter * Vector2(rb->get_internal_size()) * 0.5f;
				RendererRD::FSR2Effect::Parameters params;
				params.context = rb_data->get_fsr2_context();
				params.internal_size = rb->get_internal_size();
				params.sharpness = CLAMP(1.0f - (rb->get_fsr_sharpness() / 2.0f), 0.0f, 1.0f);
				params.color = rb->get_internal_texture(v);
				params.depth = rb->get_depth_texture(v);
				params.velocity = rb->get_velocity_buffer(false, v);
				params.reactive = rb->get_internal_texture_reactive(v);
				params.exposure = exposure;
				params.output = rb->get_upscaled_texture(v);
				params.z_near = p_render_data->scene_data->z_near;
				params.z_far = p_render_data->scene_data->z_far;
				params.fovy = fovy;
				params.jitter = jitter;
				params.delta_time = float(time_step);
				params.reset_accumulation = false; // FIXME: The engine does not provide a way to reset the accumulation.

				Projection correction;
				correction.set_depth_correction(true, true, false);

				const Projection &prev_proj = p_render_data->scene_data->prev_cam_projection;
				const Projection &cur_proj = p_render_data->scene_data->cam_projection;
				const Transform3D &prev_transform = p_render_data->scene_data->prev_cam_transform;
				const Transform3D &cur_transform = p_render_data->scene_data->cam_transform;
				params.reprojection = (correction * prev_proj) * prev_transform.affine_inverse() * cur_transform * (correction * cur_proj).inverse();

				fsr2_effect->upscale(params);
			}

			RD::get_singleton()->draw_command_end_label();
		} else if (scale_type == SCALE_MFX) {
#ifdef METAL_MFXTEMPORAL_ENABLED
			bool reset = rb_data->ensure_mfx_temporal(mfx_temporal_effect);

			RID exposure;
			if (RSG::camera_attributes->camera_attributes_uses_auto_exposure(p_render_data->camera_attributes)) {
				exposure = luminance->get_current_luminance_buffer(rb);
			}

			RD::get_singleton()->draw_command_begin_label("MetalFX Temporal");
			// Scale to ±0.5.
			Vector2 jitter = p_render_data->scene_data->taa_jitter * 0.5f;
			jitter *= Vector2(1.0, -1.0); // Flip y-axis as bottom left is origin.

			for (uint32_t v = 0; v < rb->get_view_count(); v++) {
				RendererRD::MFXTemporalEffect::Params params;
				params.src = rb->get_internal_texture(v);
				params.depth = rb->get_depth_texture(v);
				params.motion = rb->get_velocity_buffer(false, v);
				params.exposure = exposure;
				params.dst = rb->get_upscaled_texture(v);
				params.jitter_offset = jitter;
				params.reset = reset;

				mfx_temporal_effect->process(rb_data->get_mfx_temporal_context(), params);
			}

			RD::get_singleton()->draw_command_end_label();
#endif
		} else if (using_taa) {
			RD::get_singleton()->draw_command_begin_label("TAA");
			RENDER_TIMESTAMP("TAA");
			taa->process(rb, rb->get_base_data_format(), p_render_data->scene_data->z_near, p_render_data->scene_data->z_far);
			RD::get_singleton()->draw_command_end_label();
		}
	}

	if (rb_data.is_valid()) {
		_debug_draw_cluster(rb);

		RENDER_TIMESTAMP("Tonemap");

		_render_buffers_post_process_and_tonemap(p_render_data);

		_fellow_adaptive_vrs_update(rb);
	}

	if (rb_data.is_valid()) {
		_render_buffers_debug_draw(p_render_data);

		if (get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_SDFGI && rb->has_custom_data(RB_SCOPE_SDFGI)) {
			Ref<RendererRD::GI::SDFGI> sdfgi = rb->get_custom_data(RB_SCOPE_SDFGI);
			Vector<RID> view_rids;

			// SDFGI renders at internal resolution, need to check if our debug correctly supports outputting upscaled.
			Size2i size = rb->get_internal_size();
			RID source_texture = rb->get_internal_texture();
			for (uint32_t v = 0; v < rb->get_view_count(); v++) {
				view_rids.push_back(rb->get_internal_texture(v));
			}

			sdfgi->debug_draw(p_render_data->scene_data->view_count, p_render_data->scene_data->view_projection, p_render_data->scene_data->cam_transform, size.x, size.y, rb->get_render_target(), source_texture, view_rids);
		}
	}
}

void RenderForwardClustered::_render_buffers_debug_draw(const RenderDataRD *p_render_data) {
	RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();

	Ref<RenderSceneBuffersRD> rb = p_render_data->render_buffers;
	ERR_FAIL_COND(rb.is_null());

	Ref<RenderBufferDataForwardClustered> rb_data = rb->get_custom_data(RB_SCOPE_FORWARD_CLUSTERED);
	ERR_FAIL_COND(rb_data.is_null());

	RendererSceneRenderRD::_render_buffers_debug_draw(p_render_data);

	RID render_target = rb->get_render_target();

	if (get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_SSAO && rb->has_texture(RB_SCOPE_SSAO, RB_FINAL)) {
		RID final = rb->get_texture_slice(RB_SCOPE_SSAO, RB_FINAL, 0, 0);
		Size2i rtsize = texture_storage->render_target_get_size(render_target);
		copy_effects->copy_to_fb_rect(final, texture_storage->render_target_get_rd_framebuffer(render_target), Rect2(Vector2(), rtsize), false, true);
	}

	if (get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_SSIL && rb->has_texture(RB_SCOPE_SSIL, RB_FINAL)) {
		RID final = rb->get_texture_slice(RB_SCOPE_SSIL, RB_FINAL, 0, 0);
		Size2i rtsize = texture_storage->render_target_get_size(render_target);
		copy_effects->copy_to_fb_rect(final, texture_storage->render_target_get_rd_framebuffer(render_target), Rect2(Vector2(), rtsize), false, false);
	}

	if (get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_GI_BUFFER && rb->has_texture(RB_SCOPE_GI, RB_TEX_AMBIENT)) {
		Size2i rtsize = texture_storage->render_target_get_size(render_target);
		RID ambient_texture = rb->get_texture(RB_SCOPE_GI, RB_TEX_AMBIENT);
		RID reflection_texture = rb->get_texture(RB_SCOPE_GI, RB_TEX_REFLECTION);
		copy_effects->copy_to_fb_rect(ambient_texture, texture_storage->render_target_get_rd_framebuffer(render_target), Rect2(Vector2(), rtsize), false, false, false, true, reflection_texture, rb->get_view_count() > 1);
	}
}

void RenderForwardClustered::_render_shadow_pass(RID p_light, RID p_shadow_atlas, int p_pass, const PagedArray<RenderGeometryInstance *> &p_instances, float p_lod_distance_multiplier, float p_screen_mesh_lod_threshold, bool p_open_pass, bool p_close_pass, bool p_clear_region, RenderingServerTypes::RenderInfo *p_render_info, const Size2i &p_viewport_size, const Transform3D &p_main_cam_transform, const RendererSceneRender::FellowShadowCacheOp *p_fellow_op) {
	RendererRD::LightStorage *light_storage = RendererRD::LightStorage::get_singleton();

	ERR_FAIL_COND(!light_storage->owns_light_instance(p_light));

	RID base = light_storage->light_instance_get_base_light(p_light);

	Rect2i atlas_rect;
	uint32_t atlas_size = 1;
	RID atlas_fb;

	bool reverse_cull_face = light_storage->light_get_reverse_cull_face_mode(base);
	bool using_dual_paraboloid = false;
	bool using_dual_paraboloid_flip = false;
	Vector2i dual_paraboloid_offset;
	RID render_fb;
	RID render_texture;
	float zfar;

	bool use_pancake = false;
	bool render_cubemap = false;
	bool finalize_cubemap = false;

	bool flip_y = false;

	Projection light_projection;
	Transform3D light_transform;

	if (light_storage->light_get_type(base) == RSE::LIGHT_DIRECTIONAL) {
		//set pssm stuff
		uint64_t last_scene_shadow_pass = light_storage->light_instance_get_shadow_pass(p_light);
		if (last_scene_shadow_pass != get_scene_pass()) {
			light_storage->light_instance_set_directional_rect(p_light, light_storage->get_directional_shadow_rect());
			light_storage->directional_shadow_increase_current_light();
			light_storage->light_instance_set_shadow_pass(p_light, get_scene_pass());
		}

		use_pancake = light_storage->light_get_param(base, RSE::LIGHT_PARAM_SHADOW_PANCAKE_SIZE) > 0;
		light_projection = light_storage->light_instance_get_shadow_camera(p_light, p_pass);
		light_transform = light_storage->light_instance_get_shadow_transform(p_light, p_pass);

		atlas_rect = light_storage->light_instance_get_directional_rect(p_light);

		if (light_storage->light_directional_get_shadow_mode(base) == RSE::LIGHT_DIRECTIONAL_SHADOW_PARALLEL_4_SPLITS) {
			atlas_rect.size.width /= 2;
			atlas_rect.size.height /= 2;

			if (p_pass == 1) {
				atlas_rect.position.x += atlas_rect.size.width;
			} else if (p_pass == 2) {
				atlas_rect.position.y += atlas_rect.size.height;
			} else if (p_pass == 3) {
				atlas_rect.position += atlas_rect.size;
			}
		} else if (light_storage->light_directional_get_shadow_mode(base) == RSE::LIGHT_DIRECTIONAL_SHADOW_PARALLEL_2_SPLITS) {
			atlas_rect.size.height /= 2;

			if (p_pass == 0) {
			} else {
				atlas_rect.position.y += atlas_rect.size.height;
			}
		}

		float directional_shadow_size = light_storage->directional_shadow_get_size();
		Rect2 atlas_rect_norm = atlas_rect;
		atlas_rect_norm.position /= directional_shadow_size;
		atlas_rect_norm.size /= directional_shadow_size;
		light_storage->light_instance_set_directional_shadow_atlas_rect(p_light, p_pass, atlas_rect_norm);
		fellow_dir_cascades.push_back({ atlas_rect, light_projection, light_transform, light_storage->light_instance_get_directional_shadow_split(p_light, p_pass) });

		zfar = RSG::light_storage->light_get_param(base, RSE::LIGHT_PARAM_RANGE);

		render_fb = light_storage->direction_shadow_get_fb();
		render_texture = RID();
		flip_y = true;

	} else {
		//set from shadow atlas

		ERR_FAIL_COND(!light_storage->owns_shadow_atlas(p_shadow_atlas));
		ERR_FAIL_COND(!light_storage->shadow_atlas_owns_light_instance(p_shadow_atlas, p_light));

		RSG::light_storage->shadow_atlas_update(p_shadow_atlas);

		uint32_t key = light_storage->shadow_atlas_get_light_instance_key(p_shadow_atlas, p_light);

		uint32_t quadrant = (key >> RendererRD::LightStorage::QUADRANT_SHIFT) & 0x3;
		uint32_t shadow = key & RendererRD::LightStorage::SHADOW_INDEX_MASK;
		uint32_t subdivision = light_storage->shadow_atlas_get_quadrant_subdivision(p_shadow_atlas, quadrant);

		ERR_FAIL_INDEX((int)shadow, light_storage->shadow_atlas_get_quadrant_shadow_size(p_shadow_atlas, quadrant));

		uint32_t shadow_atlas_size = light_storage->shadow_atlas_get_size(p_shadow_atlas);
		uint32_t quadrant_size = shadow_atlas_size >> 1;

		atlas_rect.position.x = (quadrant & 1) * quadrant_size;
		atlas_rect.position.y = (quadrant >> 1) * quadrant_size;

		uint32_t shadow_size = (quadrant_size / subdivision);
		atlas_rect.position.x += (shadow % subdivision) * shadow_size;
		atlas_rect.position.y += (shadow / subdivision) * shadow_size;

		atlas_rect.size.width = shadow_size;
		atlas_rect.size.height = shadow_size;

		// FellowEngine positional shadow pyramid: this light's slot is written (an omni light also writes the next slot,
		// its second paraboloid).
		{
			FellowAtlasDirty &d = fellow_pos_atlas_dirty[p_shadow_atlas];
			d.merge(atlas_rect);
			if (light_storage->light_get_type(base) == RSE::LIGHT_OMNI) {
				const bool wrap_dp = (shadow + 1) % subdivision == 0;
				const Vector2i dp_offset = wrap_dp ? Vector2i(1 - subdivision, 1) : Vector2i(1, 0);
				d.merge(Rect2i(atlas_rect.position + dp_offset * atlas_rect.size, atlas_rect.size));
			}
		}

		zfar = light_storage->light_get_param(base, RSE::LIGHT_PARAM_RANGE);

		if (light_storage->light_get_type(base) == RSE::LIGHT_OMNI) {
			bool wrap = (shadow + 1) % subdivision == 0;
			dual_paraboloid_offset = wrap ? Vector2i(1 - subdivision, 1) : Vector2i(1, 0);

			if (light_storage->light_omni_get_shadow_mode(base) == RSE::LIGHT_OMNI_SHADOW_CUBE) {
				render_texture = light_storage->get_cubemap(shadow_size / 2);
				render_fb = light_storage->get_cubemap_fb(shadow_size / 2, p_pass);

				light_projection = light_storage->light_instance_get_shadow_camera(p_light, p_pass);
				light_transform = light_storage->light_instance_get_shadow_transform(p_light, p_pass);
				render_cubemap = true;
				finalize_cubemap = p_pass == 5;
				atlas_fb = light_storage->shadow_atlas_get_fb(p_shadow_atlas);

				atlas_size = shadow_atlas_size;

				if (p_pass == 0) {
					_render_shadow_begin();
				}

			} else {
				atlas_rect.position.x += 1;
				atlas_rect.position.y += 1;
				atlas_rect.size.x -= 2;
				atlas_rect.size.y -= 2;

				atlas_rect.position += p_pass * atlas_rect.size * dual_paraboloid_offset;

				light_projection = light_storage->light_instance_get_shadow_camera(p_light, 0);
				light_transform = light_storage->light_instance_get_shadow_transform(p_light, 0);

				using_dual_paraboloid = true;
				using_dual_paraboloid_flip = p_pass == 1;
				render_fb = light_storage->shadow_atlas_get_fb(p_shadow_atlas);
				flip_y = true;
			}

		} else if (light_storage->light_get_type(base) == RSE::LIGHT_SPOT) {
			light_projection = light_storage->light_instance_get_shadow_camera(p_light, 0);
			light_transform = light_storage->light_instance_get_shadow_transform(p_light, 0);

			render_fb = light_storage->shadow_atlas_get_fb(p_shadow_atlas);

			flip_y = true;
		} else if (light_storage->light_get_type(base) == RSE::LIGHT_AREA) {
			Vector2 area_size = light_storage->light_area_get_size(base);

			zfar = light_storage->light_get_param(base, RSE::LIGHT_PARAM_RANGE) + area_size.length() / 2.0;

			light_transform = light_storage->light_instance_get_shadow_transform(p_light, 0);

			light_projection = light_storage->light_instance_get_shadow_camera(p_light, 0);

			render_fb = light_storage->shadow_atlas_get_fb(p_shadow_atlas);

			flip_y = true;

			using_dual_paraboloid = true;
		}
	}

	if (render_cubemap) {
		//rendering to cubemap
		_render_shadow_append(render_fb, p_instances, light_projection, light_transform, zfar, 0, 0, reverse_cull_face, false, false, use_pancake, p_lod_distance_multiplier, p_screen_mesh_lod_threshold, Rect2(), false, true, true, true, p_render_info, p_viewport_size, p_main_cam_transform);
		if (finalize_cubemap) {
			_render_shadow_process();
			_render_shadow_end();
			//reblit
			Rect2 atlas_rect_norm = atlas_rect;
			atlas_rect_norm.position /= float(atlas_size);
			atlas_rect_norm.size /= float(atlas_size);
			copy_effects->copy_cubemap_to_dp(render_texture, atlas_fb, atlas_rect_norm, atlas_rect.size, light_projection.get_z_near(), zfar, false);
			atlas_rect_norm.position += Vector2(dual_paraboloid_offset) * atlas_rect_norm.size;
			copy_effects->copy_cubemap_to_dp(render_texture, atlas_fb, atlas_rect_norm, atlas_rect.size, light_projection.get_z_near(), zfar, true);

			//restore transform so it can be properly used
			light_storage->light_instance_set_shadow_transform(p_light, Projection(), light_storage->light_instance_get_base_transform(p_light), zfar, 0, 0, 0);
		}

	} else if (p_fellow_op != nullptr && p_fellow_op->cache_id >= 0 && light_storage->light_get_type(base) == RSE::LIGHT_DIRECTIONAL) {
		// FellowEngine directional shadow cache (docs/decisions/0004-directional-shadow-cache.md).
		const bool use_16_bits = light_storage->directional_shadow_get_use_16_bits();
		RID cache_fb = fellow_shadow_cache->get_framebuffer(p_fellow_op->cache_id, p_pass, p_fellow_op->cache_size, RendererRD::LightStorage::get_shadow_atlas_depth_format(use_16_bits), RendererRD::LightStorage::get_shadow_atlas_depth_usage_bits());
		if (p_fellow_op->rebuild) {
			// Static casters of the whole cache volume, same pipeline state as the cascade (only the camera differs).
			float cache_lod_multiplier = p_lod_distance_multiplier;
			float cache_lod_threshold = p_screen_mesh_lod_threshold;
			float cache_lod_ortho_extent = 0.0;
			if (p_fellow_op->lod_texel_threshold > 0.0 && p_screen_mesh_lod_threshold > 0.0) {
				// Orthographic LOD: "screen size" = error / cache width; accept errors below lod_texel_threshold texels.
				cache_lod_ortho_extent = 2.0 / p_fellow_op->cache_projection.columns[0][0];
				cache_lod_multiplier = cache_lod_ortho_extent;
				cache_lod_threshold = p_fellow_op->lod_texel_threshold / float(p_fellow_op->cache_size);
			}
			// Counted separately: the scene cull's cost gate compares the static casters' cost with the copy's.
			RenderingServerTypes::RenderInfo cache_info;
			// Its counters are read right after the append: built at once (after the passes batched before it).
			_fellow_shadow_flush();
			fellow_shadow_hold++;
			_render_shadow_append(cache_fb, p_fellow_op->cache_instances, p_fellow_op->cache_projection, p_fellow_op->cache_transform, zfar, 0, 0, reverse_cull_face, false, false, use_pancake, cache_lod_multiplier, cache_lod_threshold, Rect2i(0, 0, p_fellow_op->cache_size, p_fellow_op->cache_size), flip_y, true, false, false, &cache_info, p_viewport_size, p_main_cam_transform, p_fellow_op->lod_distance_bias, cache_lod_ortho_extent);
			fellow_shadow_hold--;
			if (p_render_info) {
				for (int t = 0; t < RSE::VIEWPORT_RENDER_INFO_TYPE_MAX; t++) {
					for (int k = 0; k < RSE::VIEWPORT_RENDER_INFO_MAX; k++) {
						p_render_info->info[t][k] += cache_info.info[t][k];
					}
				}
			}
			const int *shadow_info = cache_info.info[RSE::VIEWPORT_RENDER_INFO_TYPE_SHADOW];
			fellow_shadow_cache->set_static_cost(p_fellow_op->cache_id, p_pass, p_fellow_op->build_frame, (uint64_t)MAX(0, shadow_info[RSE::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME]), (uint32_t)MAX(0, shadow_info[RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME]));
			fellow_shadow_cache->set_content_valid(p_fellow_op->cache_id, p_pass);
		}
		RID cache_texture = fellow_shadow_cache->get_valid_texture(p_fellow_op->cache_id, p_pass);

		SceneState::ShadowPass copy_pass;
		copy_pass.fellow_copy = cache_texture.is_valid(); // content lost (atlas format changed): skip, rebuilt next frame
		copy_pass.element_from = 0;
		copy_pass.element_count = 0;
		copy_pass.pass_mode = PASS_MODE_SHADOW;
		copy_pass.lod_distance_multiplier = 0.0;
		copy_pass.screen_mesh_lod_threshold = 0.0;
		copy_pass.framebuffer = render_fb;
		copy_pass.rect = atlas_rect;
		copy_pass.clear_depth = false;
		copy_pass.flip_cull = false;
		copy_pass.uniform_buffer_index = 0;
		copy_pass.fellow_source = cache_texture;
		copy_pass.fellow_source_offset = p_fellow_op->copy_offset;
		copy_pass.fellow_scale = p_fellow_op->remap_scale;
		copy_pass.fellow_bias = p_fellow_op->remap_bias;
		// Skip the copy if the rect still holds exactly this content (static view, no dynamic casters now or before).
		const RID atlas_texture = light_storage->directional_shadow_get_texture();
		const bool unchanged = copy_pass.fellow_copy && !p_fellow_op->rebuild && p_instances.size() == 0 && fellow_shadow_cache->atlas_rect_holds(atlas_texture, atlas_rect, cache_texture, copy_pass.fellow_source_offset, copy_pass.fellow_scale, copy_pass.fellow_bias);
		if (copy_pass.fellow_copy && !unchanged) {
			scene_state.shadow_passes.push_back(copy_pass);
			_fellow_mark_dir_atlas_dirty(atlas_rect);
		}
		if (unchanged) {
			Fellow::stats().shadow_cache_copies_skipped.increment();
		}
		if (copy_pass.fellow_copy) {
			fellow_shadow_cache->atlas_rect_set(atlas_texture, atlas_rect, cache_texture, copy_pass.fellow_source_offset, copy_pass.fellow_scale, copy_pass.fellow_bias, p_instances.size() > 0);
		} else {
			fellow_shadow_cache->atlas_rect_set(atlas_texture, atlas_rect, RID(), Vector2i(), 0.0, 0.0, true);
		}

		if (p_instances.size() > 0) {
			// Dynamic casters on top, exactly like the stock pass (no clear).
			_render_shadow_append(render_fb, p_instances, light_projection, light_transform, zfar, 0, 0, reverse_cull_face, false, false, use_pancake, p_lod_distance_multiplier, p_screen_mesh_lod_threshold, atlas_rect, flip_y, false, false, p_close_pass, p_render_info, p_viewport_size, p_main_cam_transform);
			_fellow_mark_dir_atlas_dirty(atlas_rect);
		}
	} else {
		bool clear_region = p_clear_region;
		if (light_storage->light_get_type(base) == RSE::LIGHT_DIRECTIONAL) {
			_fellow_mark_dir_atlas_dirty(atlas_rect);
			if (fellow_dir_clear_stock_rects) {
				// The atlas was not cleared this frame: clear this cascade's rect (to the same 0) and forget what the
				// cache had put there.
				clear_region = true;
				fellow_shadow_cache->atlas_rect_set(light_storage->directional_shadow_get_texture(), atlas_rect, RID(), Vector2i(), 0.0, 0.0, true);
			}
		}
		//render shadow
		_render_shadow_append(render_fb, p_instances, light_projection, light_transform, zfar, 0, 0, reverse_cull_face, using_dual_paraboloid, using_dual_paraboloid_flip, use_pancake, p_lod_distance_multiplier, p_screen_mesh_lod_threshold, atlas_rect, flip_y, clear_region, p_open_pass, p_close_pass, p_render_info, p_viewport_size, p_main_cam_transform);
	}
}

Rect2i RenderForwardClustered::_fellow_dir_needed_rect(const FellowDirCascade &p_cascade, const RenderSceneDataRD *p_scene_data) const {
	// The scene shader looks cascade i up for surfaces at view depth below its split (its own range and the blend into
	// it from the previous cascade), within the camera frustum: the frustum from the eye to that depth, projected into
	// the cascade (bounding box), plus a margin for the filter boxes and their windows around the edges. Orthographic
	// cameras and multiview: the whole cascade.
	const Rect2i whole = p_cascade.rect;
	const Projection &cam = p_scene_data->cam_projection;
	if (p_scene_data->view_count != 1 || cam.is_orthogonal() || p_cascade.split <= 0.0) {
		return whole;
	}
	Vector3 corners[8];
	if (!cam.get_endpoints(Transform3D(), corners)) {
		return whole;
	}
	const real_t z_far = -corners[0].z;
	if (z_far <= 0.0) {
		return whole;
	}
	const Projection to_shadow = p_cascade.projection * Projection(p_cascade.transform.affine_inverse() * p_scene_data->cam_transform);
	Vector2 lo(1e20, 1e20);
	Vector2 hi(-1e20, -1e20);
	for (int i = 0; i < 5; i++) {
		// The eye and the four far corners scaled to the split depth (corners 0-3 are on the far plane).
		const Vector3 v = i == 0 ? Vector3() : corners[i - 1] * (MIN(p_cascade.split, z_far) / z_far);
		const Vector3 q = to_shadow.xform(v);
		const Vector2 uv(q.x * 0.5 + 0.5, q.y * 0.5 + 0.5);
		lo = lo.min(uv);
		hi = hi.max(uv);
	}
	const int margin = MAX(32, whole.size.x / 16);
	Rect2i r;
	r.position = Point2i(int(Math::floor(lo.x * whole.size.x)), int(Math::floor(lo.y * whole.size.y))) + whole.position - Point2i(margin, margin);
	const Point2i end = Point2i(int(Math::ceil(hi.x * whole.size.x)), int(Math::ceil(hi.y * whole.size.y))) + whole.position + Point2i(margin, margin);
	r.size = end - r.position;
	r = r.intersection(whole);
	return r;
}

void RenderForwardClustered::fellow_directional_shadow_cache_free(int64_t p_cache_id) {
	if (fellow_shadow_cache) {
		fellow_shadow_cache->free_cache(p_cache_id);
	}
}

bool RenderForwardClustered::fellow_directional_shadow_cache_take_lost(int64_t p_cache_id) {
	return fellow_shadow_cache && fellow_shadow_cache->take_lost(p_cache_id);
}

bool RenderForwardClustered::fellow_directional_shadow_cache_get_static_cost(int64_t p_cache_id, int p_cascade, uint64_t p_build_frame, uint64_t &r_primitives, uint32_t &r_draws) const {
	return fellow_shadow_cache && fellow_shadow_cache->get_static_cost(p_cache_id, p_cascade, p_build_frame, r_primitives, r_draws);
}

void RenderForwardClustered::_fellow_shadow_fill_chunk(uint32_t p_chunk, void *p_unused) {
	FellowShadowChunk &chunk = fellow_shadow_chunks[p_chunk];
	FellowFillOutput out;
	out.main = &chunk.unused;
	out.alpha = &chunk.unused;
	out.motion = &chunk.unused;
	out.records = &chunk.records;
	out.transform_updates = &chunk.transform_updates;
	_fill_render_list_range<true>(fellow_shadow_entries[chunk.entry].params, chunk.from, chunk.to, out);
	chunk.primitives = out.primitives;
}

void RenderForwardClustered::_fellow_shadow_gather_chunk(uint32_t p_chunk, void *p_unused) {
	const FellowShadowChunk &chunk = fellow_shadow_chunks[p_chunk];
	if (!chunk.records.is_empty()) {
		memcpy(fellow_shadow_entries[chunk.entry].records.ptr() + chunk.offset, chunk.records.ptr(), chunk.records.size() * sizeof(FellowShadowRecord));
	}
}

void RenderForwardClustered::_fellow_shadow_sort_entry(uint32_t p_entry, void *p_unused) {
	// Stock sorts the pass's elements by the keys its fill gave them: the same introsort on the same keys.
	FellowShadowEntry &entry = fellow_shadow_entries[p_entry];
	SortArray<FellowShadowRecord, FellowShadowRecordByKey> sorter;
	sorter.sort(entry.records.ptr(), entry.size);
}

void RenderForwardClustered::_fellow_shadow_instance_chunk(uint32_t p_item, void *p_unused) {
	const FellowShadowItem &item = fellow_shadow_items[p_item];
	const FellowShadowEntry &entry = fellow_shadow_entries[item.entry];
	RenderList *rl = &render_list[RENDER_LIST_SECONDARY];
	SceneState::InstanceData *gpu = Fellow::cpu_debug_instance_scratch() && !fellow_persistent.active ? fellow_debug_scratch[RENDER_LIST_SECONDARY].ptr() : scene_state.curr_gpu_ptr[RENDER_LIST_SECONDARY];
	FellowElement *elements = reinterpret_cast<FellowElement *>(scene_state.curr_gpu_ptr[RENDER_LIST_SECONDARY]);
	const bool persistent = fellow_persistent.active;
	const uint64_t frame = fellow_persistent.frame;
	uint8_t *link = fellow_instance_joinable.ptr();
	const FellowShadowRecord *records = entry.records.ptr();
	const bool prefetch = Fellow::cpu_prefetch();
	const bool compact = fellow_shadow_compact;
	for (uint32_t r = item.from; r < item.to; r++) {
		const FellowShadowRecord &rec = records[r];
		GeometryInstanceSurfaceDataCache *surface = rec.surface;
		GeometryInstanceForwardClustered *inst = surface->owner;
		if (prefetch && r + FELLOW_PREFETCH_DISTANCE < item.to) {
			const uint8_t *ahead = reinterpret_cast<const uint8_t *>(records[r + FELLOW_PREFETCH_DISTANCE].surface->owner);
			for (uint32_t line = 0; line < FELLOW_PREFETCH_INSTANCE_LINES; line++) {
				fellow_prefetch(ahead + line * 64);
			}
			fellow_prefetch(records[r + FELLOW_PREFETCH_DISTANCE].surface->surface);
		}
		const uint32_t i = entry.from + r;
		rl->elements[i] = surface;

		// Stock's instance data, with the flags this pass gave the instance (stock reads them back from the instance).
		if (persistent) {
			_fellow_persistent_check(surface, inst, frame);
			elements[i] = { surface->fellow_slot, rec.flags, inst->gi_offset_cache, 0 };
		} else {
			SceneState::InstanceData instance_data;
			_fellow_make_instance_data(surface, inst, instance_data, !compact);
			instance_data.flags = rec.flags;
			if (compact) {
				memcpy(&gpu[i], &instance_data, FELLOW_SHADOW_INSTANCE_BYTES);
			} else {
				gpu[i] = instance_data;
			}
		}
		rl->element_info[i].value = uint32_t(rec.key1 & 0xFFF);

		// Stock's repeat test against the element before it (a pass's first element starts a run).
		const bool cant_repeat = rec.flags & INSTANCE_DATA_FLAG_MULTIMESH || inst->mesh_instance.is_valid();
		bool join = false;
		if (r > 0 && !cant_repeat) {
			const FellowShadowRecord &prev = records[r - 1];
			const GeometryInstanceForwardClustered *prev_inst = prev.surface->owner;
			const bool prev_cant_repeat = prev.flags & INSTANCE_DATA_FLAG_MULTIMESH || prev_inst->mesh_instance.is_valid();
			join = !prev_cant_repeat && prev.key1 == rec.key1 && prev.key2 == rec.key2 && inst->mirror == prev_inst->mirror;
		}
		link[i] = join;
	}
	_fellow_run_scan(p_item);
}

void RenderForwardClustered::_fellow_shadow_flush() {
	// Batched shadow lists (docs/decisions/0016-parallel-render-lists.md). The passes appended since the last flush are
	// built together, as stock builds them one after the other: each pass's instances are filled (record mode: the keys
	// and flags the pass gives each element, without writing the instances and surfaces, so passes sharing instances
	// can be filled at once), sorted by stock's introsort, appended to the secondary list in pass order, and turned into
	// instance data. The instance buffer is grown pass by pass as stock grows it. When stock would have to replace the
	// buffer in the middle of the passes (it then rewrites and re-counts everything from element 0 with the keys and
	// flags the instances hold by then; first frames, or when the shadow lists outgrow the buffer), the passes are built
	// stock's way, one after the other.
	const uint32_t entry_count = fellow_shadow_entry_count;
	fellow_shadow_entry_count = 0;
	if (entry_count == 0) {
		return;
	}
	_update_dirty_geometry_instances();
	RenderList *rl = &render_list[RENDER_LIST_SECONDARY];
	Fellow::stats().cpu_shadow_batches.increment();
	Fellow::stats().cpu_shadow_batch_passes.add(entry_count);

	const bool markers = Fellow::list_markers();
	if (markers) {
		RENDER_TIMESTAMP("FellowEngine Shadow Lists Fill");
	}

	// Fill. Worker tasks only when the passes have enough instances between them (posting and waking workers costs more
	// than a few hundred instances' lists: m4_f46_gpu_ab, PBR Lab).
	uint32_t total_instances = 0;
	for (uint32_t e = 0; e < entry_count; e++) {
		total_instances += (uint32_t)fellow_shadow_entries[e].params.instances->size();
	}
	const uint32_t helpers = total_instances >= FELLOW_PARALLEL_LIST_MIN ? UINT32_MAX : 0;
	fellow_shadow_compact = Fellow::cpu_compact_shadow_instances();
	uint32_t chunk_count = 0;
	for (uint32_t e = 0; e < entry_count; e++) {
		FellowShadowEntry &entry = fellow_shadow_entries[e];
		const uint32_t n = (uint32_t)entry.params.instances->size();
		entry.chunk_first = chunk_count;
		entry.chunk_count = (n + FELLOW_PARALLEL_LIST_CHUNK - 1) / FELLOW_PARALLEL_LIST_CHUNK;
		chunk_count += entry.chunk_count;
		if (fellow_shadow_chunks.size() < chunk_count) {
			fellow_shadow_chunks.resize(chunk_count);
		}
		for (uint32_t c = 0; c < entry.chunk_count; c++) {
			FellowShadowChunk &chunk = fellow_shadow_chunks[entry.chunk_first + c];
			chunk.entry = e;
			chunk.from = c * FELLOW_PARALLEL_LIST_CHUNK;
			chunk.to = MIN(n, chunk.from + FELLOW_PARALLEL_LIST_CHUNK);
			chunk.records.clear();
			chunk.transform_updates.clear();
			chunk.primitives = 0;
		}
	}
	_fellow_parallel_for(chunk_count, &_fellow_chunk_thunk<&RenderForwardClustered::_fellow_shadow_fill_chunk>, helpers);

	// Each pass's elements in fill order, its place in the secondary list, its primitive count.
	uint32_t total = rl->elements.size();
	for (uint32_t e = 0; e < entry_count; e++) {
		FellowShadowEntry &entry = fellow_shadow_entries[e];
		uint32_t size = 0;
		entry.primitives = 0;
		for (uint32_t c = 0; c < entry.chunk_count; c++) {
			size += fellow_shadow_chunks[entry.chunk_first + c].records.size();
			entry.primitives += fellow_shadow_chunks[entry.chunk_first + c].primitives;
		}
		entry.from = total;
		entry.size = size;
		total += size;
	}

	// Would stock replace the instance buffer in the middle (a pass after the first growing it)?
	{
		// Stock's _fill_instance_data per pass: grow_instance_buffer (replaced if too small; remapped when the pass
		// starts the list), then, with no mapping and elements to write, it maps the buffer and, if the pass does not
		// start the list, rewrites everything from element 0.
		const uint64_t stride = sizeof(SceneState::InstanceData);
		uint64_t capacity = scene_state.instance_buffer[RENDER_LIST_SECONDARY].get_size(0u) / stride;
		bool mapped = scene_state.curr_gpu_ptr[RENDER_LIST_SECONDARY] != nullptr;
		bool restart = false;
		for (uint32_t e = 0; e < entry_count && !restart; e++) {
			const FellowShadowEntry &entry = fellow_shadow_entries[e];
			const uint64_t required = uint64_t(entry.from) + entry.size;
			if (required > 0) {
				if (capacity < required) {
					capacity = Math::nearest_power_of_2_templated(MAX(uint64_t(INSTANCE_DATA_BUFFER_MIN_SIZE), required));
					mapped = false;
				}
				if (entry.from == 0u) {
					mapped = false; // prepare_for_map(false) always asks for a new mapping
				}
			}
			if (!mapped && entry.size > 0u) {
				restart = entry.from != 0u;
				mapped = true;
			}
		}
		if (restart) {
			Fellow::stats().cpu_shadow_batch_fallbacks.increment();
			for (uint32_t e = 0; e < entry_count; e++) {
				FellowShadowEntry &entry = fellow_shadow_entries[e];
				FellowFillOutput out;
				out.main = &rl->elements;
				out.alpha = &render_list[RENDER_LIST_ALPHA].elements;
				out.motion = &render_list[RENDER_LIST_MOTION].elements;
				uint32_t unused_captures = 0;
				out.lightmap_captures_used = &unused_captures;
				const uint32_t from = rl->elements.size();
				_fill_render_list_range<false>(entry.params, 0, (uint32_t)entry.params.instances->size(), out);
				_fellow_fill_apply_output(entry.params, out);
				const uint32_t size = rl->elements.size() - from;
				if (Fellow::cpu_compact_list_sort()) {
					_fellow_sort_by_key(RENDER_LIST_SECONDARY, from, size);
				} else {
					rl->sort_by_key_range(from, size);
				}
				_fill_instance_data(RENDER_LIST_SECONDARY, entry.render_info ? entry.render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_SHADOW] : (int *)nullptr, from, size, false);
				scene_state.shadow_passes[entry.shadow_pass].element_from = from;
				scene_state.shadow_passes[entry.shadow_pass].element_count = size;
			}
			return;
		}
	}

	// Previous transforms reset by the fill (stock: by the first pass whose fill meets the instance; the same values).
	const uint64_t frame = RSG::rasterizer->get_frame_number();
	for (uint32_t c = 0; c < chunk_count; c++) {
		for (GeometryInstanceForwardClustered *inst : fellow_shadow_chunks[c].transform_updates) {
			if (inst->transform_status != GeometryInstanceForwardClustered::TransformStatus::NONE && frame > inst->prev_transform_change_frame && inst->prev_transform_change_frame) {
				inst->prev_transform = inst->transform;
				inst->fellow_data_version++;
				inst->transform_status = GeometryInstanceForwardClustered::TransformStatus::NONE;
			}
		}
	}

	for (uint32_t e = 0; e < entry_count; e++) {
		FellowShadowEntry &entry = fellow_shadow_entries[e];
		entry.records.resize(entry.size);
		uint32_t at = 0;
		for (uint32_t c = 0; c < entry.chunk_count; c++) {
			FellowShadowChunk &chunk = fellow_shadow_chunks[entry.chunk_first + c];
			chunk.offset = at;
			at += chunk.records.size();
		}
		if (entry.render_info) {
			entry.render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_SHADOW][RSE::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] += uint32_t(entry.primitives);
		}
		scene_state.shadow_passes[entry.shadow_pass].element_from = entry.from;
		scene_state.shadow_passes[entry.shadow_pass].element_count = entry.size;
	}

	// Each pass's records in fill order (chunks copied in parallel).
	_fellow_parallel_for(chunk_count, &_fellow_chunk_thunk<&RenderForwardClustered::_fellow_shadow_gather_chunk>, helpers);

	// Sort.
	if (markers) {
		RENDER_TIMESTAMP("FellowEngine Shadow Lists Sort");
	}
	if (Fellow::cpu_parallel_list_sort()) {
		// All passes' records in one parallel sort (a large cascade is split across the workers).
		fellow_shadow_sort.clear();
		for (uint32_t e = 0; e < entry_count; e++) {
			fellow_shadow_sort.add(fellow_shadow_entries[e].records.ptr(), int64_t(fellow_shadow_entries[e].size));
		}
		const int64_t sort_total = fellow_shadow_sort.get_total();
		fellow_parallel->set_linger_usec((uint64_t)Fellow::cpu_hot_workers_usec());
		fellow_shadow_sort.run_partitions(*fellow_parallel, MIN(helpers, (uint32_t)Fellow::cpu_worker_tasks()), MAX(int64_t(FELLOW_PARALLEL_SORT_GRAIN), sort_total / 16), Fellow::cpu_parallel_partition() ? (int64_t)MAX(1024, Fellow::cpu_parallel_partition_min()) : 0);
		if (markers) {
			RENDER_TIMESTAMP("FellowEngine Shadow Lists Sort Leaves");
		}
		fellow_shadow_sort.run_leaves(*fellow_parallel, MIN(helpers, (uint32_t)Fellow::cpu_worker_tasks()));
	} else {
		_fellow_parallel_for(entry_count, &_fellow_chunk_thunk<&RenderForwardClustered::_fellow_shadow_sort_entry>, helpers);
	}

	// The instance buffer, grown and mapped pass by pass as stock's _fill_instance_data does it.
	if (markers) {
		RENDER_TIMESTAMP("FellowEngine Shadow Lists Instance Data");
	}
	rl->elements.resize(total);
	rl->element_info.resize(total);
	fellow_instance_joinable.resize(total);
	for (uint32_t e = 0; e < entry_count; e++) {
		const FellowShadowEntry &entry = fellow_shadow_entries[e];
		scene_state.grow_instance_buffer(RENDER_LIST_SECONDARY, entry.from + entry.size, entry.from != 0u);
		if (!scene_state.curr_gpu_ptr[RENDER_LIST_SECONDARY] && entry.size > 0u) {
			// Only a pass starting the list (from 0) can get here (the check above); stock maps the buffer here.
			DEV_ASSERT(entry.from == 0u);
			scene_state.curr_gpu_ptr[RENDER_LIST_SECONDARY] = reinterpret_cast<SceneState::InstanceData *>(scene_state.instance_buffer[RENDER_LIST_SECONDARY].map_raw_for_upload(0u));
		}
	}

	// Instance data (persistent instance data, decision 0022: 16-byte elements, the surfaces' entries checked).
	const bool persistent = _fellow_persistent_begin();
	if (persistent) {
		Fellow::stats().cpu_persistent_elements.add(total - fellow_shadow_entries[0].from);
	}
	fellow_shadow_items.clear();
	for (uint32_t e = 0; e < entry_count; e++) {
		const FellowShadowEntry &entry = fellow_shadow_entries[e];
		for (uint32_t from = 0; from < entry.size; from += FELLOW_PARALLEL_LIST_CHUNK) {
			fellow_shadow_items.push_back({ e, from, MIN(entry.size, from + FELLOW_PARALLEL_LIST_CHUNK) });
		}
	}
	fellow_run_chunks.resize(fellow_shadow_items.size());
	for (uint32_t k = 0; k < fellow_shadow_items.size(); k++) {
		const FellowShadowItem &item = fellow_shadow_items[k];
		fellow_run_chunks[k].from = fellow_shadow_entries[item.entry].from + item.from;
		fellow_run_chunks[k].to = fellow_shadow_entries[item.entry].from + item.to;
	}
	if (Fellow::cpu_debug_instance_scratch()) {
		fellow_debug_scratch[RENDER_LIST_SECONDARY].resize(render_list[RENDER_LIST_SECONDARY].elements.size());
	}
	_fellow_parallel_for(fellow_shadow_items.size(), &_fellow_chunk_thunk<&RenderForwardClustered::_fellow_shadow_instance_chunk>, helpers);

	// Runs and counters, per pass as stock counts them (a pass's first element always starts a run).
	if (markers) {
		RENDER_TIMESTAMP("FellowEngine Shadow Lists Runs");
	}
	if (total - fellow_shadow_entries[0].from <= RenderElementInfo::MAX_REPEATS) {
		fellow_run_info = rl->element_info.ptr();
		_fellow_runs(total, helpers);
		for (uint32_t k = 0; k < fellow_shadow_items.size(); k++) {
			const FellowShadowEntry &entry = fellow_shadow_entries[fellow_shadow_items[k].entry];
			if (entry.render_info) {
				entry.render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_SHADOW][RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME] += fellow_run_chunks[k].starts;
			}
		}
		for (uint32_t e = 0; e < entry_count; e++) {
			const FellowShadowEntry &entry = fellow_shadow_entries[e];
			if (entry.render_info) {
				entry.render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_SHADOW][RSE::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME] += entry.size;
			}
		}
	} else {
		const uint8_t *link = fellow_instance_joinable.ptr();
		for (uint32_t e = 0; e < entry_count; e++) {
			const FellowShadowEntry &entry = fellow_shadow_entries[e];
			int *info = entry.render_info ? entry.render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_SHADOW] : (int *)nullptr;
			const uint32_t range_from = entry.from;
			const uint32_t range_to = entry.from + entry.size;
			if (info) {
				info[RSE::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME] += range_to - range_from;
			}
			uint32_t repeats = 0;
			for (uint32_t i = range_from; i < range_to; i++) {
				if (i > range_from && link[i] && repeats < RenderElementInfo::MAX_REPEATS) {
					repeats++;
				} else {
					if (repeats > 0) {
						for (uint32_t j = 1; j <= repeats; j++) {
							rl->element_info[i - j].repeat = j;
						}
					}
					repeats = 1;
					if (info) {
						info[RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME]++;
					}
				}
			}
			if (repeats > 0) {
				for (uint32_t j = 1; j <= repeats; j++) {
					rl->element_info[range_to - j].repeat = j;
				}
			}
		}
	}

	if (Fellow::cpu_verify()) {
		// Stock's lists, pass by pass: its fill (into a scratch list; it writes the instances' and surfaces' per-pass
		// state as stock does), its sort, then instance data and runs compared with the batched results.
		for (uint32_t e = 0; e < entry_count; e++) {
			const FellowShadowEntry &entry = fellow_shadow_entries[e];
			LocalVector<GeometryInstanceSurfaceDataCache *> scratch;
			LocalVector<GeometryInstanceSurfaceDataCache *> unused;
			FellowFillOutput out;
			out.main = &scratch;
			out.alpha = &unused;
			out.motion = &unused;
			uint32_t unused_captures = 0;
			out.lightmap_captures_used = &unused_captures;
			_fill_render_list_range<false>(entry.params, 0, (uint32_t)entry.params.instances->size(), out);
			uint32_t mismatches = scratch.size() != entry.size ? 1 : 0;
			if (mismatches == 0) {
				mismatches += _fellow_verify_sort(RENDER_LIST_SECONDARY, entry.from, entry.size, scratch);
				LocalVector<uint32_t> flags;
				flags.resize(entry.size);
				for (uint32_t k = 0; k < entry.size; k++) {
					flags[k] = entry.records[k].flags;
					if (flags[k] != rl->elements[entry.from + k]->owner->flags_cache || rl->element_info[entry.from + k].value != uint32_t(rl->elements[entry.from + k]->sort.sort_key1 & 0xFFF)) {
						mismatches++; // the record's flags and key bits against what stock's fill just wrote
					}
				}
				mismatches += persistent ? _fellow_verify_elements(RENDER_LIST_SECONDARY, entry.from, entry.size, flags.ptr()) : _fellow_verify_instances(RENDER_LIST_SECONDARY, entry.from, entry.size, flags.ptr(), fellow_shadow_compact);
			}
			_fellow_verify_count(mismatches);
		}
	}
}

void RenderForwardClustered::_render_shadow_begin() {
	scene_state.shadow_passes.clear();
	fellow_shadow_entry_count = 0;
	fellow_shadow_batching = Fellow::cpu_batched_shadow_lists();
	RD::get_singleton()->draw_command_begin_label("Shadow Setup");
	_update_render_base_uniform_set();

	render_list[RENDER_LIST_SECONDARY].clear();
	// No need to reset scene_state.curr_gpu_ptr or scene_state.instance_buffer[RENDER_LIST_SECONDARY]
	// because _fill_instance_data will do that if it detects p_offset == 0u.
}

void RenderForwardClustered::_render_shadow_append(RID p_framebuffer, const PagedArray<RenderGeometryInstance *> &p_instances, const Projection &p_projection, const Transform3D &p_transform, float p_zfar, float p_bias, float p_normal_bias, bool p_reverse_cull_face, bool p_use_dp, bool p_use_dp_flip, bool p_use_pancake, float p_lod_distance_multiplier, float p_screen_mesh_lod_threshold, const Rect2i &p_rect, bool p_flip_y, bool p_clear_region, bool p_begin, bool p_end, RenderingServerTypes::RenderInfo *p_render_info, const Size2i &p_viewport_size, const Transform3D &p_main_cam_transform, float p_fellow_lod_distance_bias, float p_fellow_lod_ortho_extent) {
	SceneState::ShadowPass shadow_pass;

	RenderSceneDataRD scene_data;
	scene_data.flip_y = !p_flip_y; // Q: Why is this inverted? Do we assume flip in shadow logic?
	scene_data.cam_projection = p_projection;
	scene_data.cam_transform = p_transform;
	scene_data.view_projection[0] = p_projection;
	scene_data.z_far = p_zfar;
	scene_data.z_near = 0.0;
	scene_data.lod_distance_multiplier = p_lod_distance_multiplier;
	scene_data.dual_paraboloid_side = p_use_dp_flip ? -1 : 1;
	scene_data.opaque_prepass_threshold = 0.1f;
	scene_data.time = time;
	scene_data.time_step = time_step;
	scene_data.main_cam_transform = p_main_cam_transform;
	scene_data.fellow_lod_distance_bias = p_fellow_lod_distance_bias;
	scene_data.fellow_lod_ortho_extent = p_fellow_lod_ortho_extent;
	scene_data.shadow_pass = true;

	RenderDataRD render_data;
	render_data.scene_data = &scene_data;
	render_data.cluster_size = 1;
	render_data.cluster_max_elements = 32;
	render_data.instances = &p_instances;
	render_data.render_info = p_render_info;

	Size2i screen_size = RD::get_singleton()->framebuffer_get_size(p_framebuffer);
	Size2i viewport_size = p_rect.size;
	if (viewport_size == Size2()) {
		viewport_size = screen_size;
	}
	uint32_t uniform_buffer_index = _setup_environment(&render_data, true, screen_size, viewport_size, Color(), false, false, p_use_pancake);

	if (get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_DISABLE_LOD) {
		scene_data.screen_mesh_lod_threshold = 0.0;
	} else {
		scene_data.screen_mesh_lod_threshold = p_screen_mesh_lod_threshold;
	}

	PassMode pass_mode = p_use_dp ? PASS_MODE_SHADOW_DP : PASS_MODE_SHADOW;

	uint32_t render_list_from = 0;
	uint32_t render_list_size = 0;
	int fellow_entry = -1;
	if (fellow_shadow_batching && fellow_shadow_hold == 0) {
		// Batched shadow lists (decision 0016): this pass's list is built with the others in _fellow_shadow_flush().
		if (fellow_shadow_entries.size() <= fellow_shadow_entry_count) {
			fellow_shadow_entries.resize(fellow_shadow_entry_count + 1);
		}
		fellow_entry = (int)fellow_shadow_entry_count++;
		FellowShadowEntry &entry = fellow_shadow_entries[fellow_entry];
		entry.params = _fellow_fill_params(RENDER_LIST_SECONDARY, &render_data, pass_mode, false, false, false, RSG::rasterizer->get_frame_number());
		entry.params.render_data = nullptr; // not used in record mode (gone by the flush)
		entry.render_info = p_render_info;
	} else {
		render_list_from = render_list[RENDER_LIST_SECONDARY].elements.size();
		_fill_render_list(RENDER_LIST_SECONDARY, &render_data, pass_mode, false, false, false, true);
		render_list_size = render_list[RENDER_LIST_SECONDARY].elements.size() - render_list_from;
		if (Fellow::cpu_compact_list_sort()) {
			_fellow_sort_by_key(RENDER_LIST_SECONDARY, render_list_from, render_list_size);
		} else {
			render_list[RENDER_LIST_SECONDARY].sort_by_key_range(render_list_from, render_list_size);
		}
		_fill_instance_data(RENDER_LIST_SECONDARY, p_render_info ? p_render_info->info[RSE::VIEWPORT_RENDER_INFO_TYPE_SHADOW] : (int *)nullptr, render_list_from, render_list_size, false);
	}

	{
		//regular forward for now
		bool flip_cull = p_use_dp_flip;
		if (p_flip_y) {
			flip_cull = !flip_cull;
		}

		if (p_reverse_cull_face) {
			flip_cull = !flip_cull;
		}

		shadow_pass.element_from = render_list_from;
		shadow_pass.element_count = render_list_size;
		shadow_pass.flip_cull = flip_cull;
		shadow_pass.pass_mode = pass_mode;

		shadow_pass.rp_uniform_set = RID(); //will be filled later when instance buffer is complete
		shadow_pass.screen_mesh_lod_threshold = scene_data.screen_mesh_lod_threshold;
		shadow_pass.lod_distance_multiplier = scene_data.lod_distance_multiplier;

		shadow_pass.framebuffer = p_framebuffer;
		shadow_pass.clear_depth = p_begin || p_clear_region;
		shadow_pass.rect = p_rect;

		shadow_pass.uniform_buffer_index = uniform_buffer_index;

		shadow_pass.fellow_cluster_plane_count = 0;
		if (Fellow::mesh_clusters() && !p_use_dp) {
			// Casters in front of a pancaked shadow camera are flattened onto its near plane, not clipped: keep the near
			// plane out of the cluster cull then.
			const Vector<Plane> planes = p_projection.get_projection_planes(p_transform);
			for (int i = 0; i < planes.size(); i++) {
				if (i == Projection::PLANE_NEAR && p_use_pancake) {
					continue;
				}
				shadow_pass.fellow_cluster_planes[shadow_pass.fellow_cluster_plane_count++] = planes[i];
			}
		}

		if (fellow_entry >= 0) {
			fellow_shadow_entries[fellow_entry].shadow_pass = scene_state.shadow_passes.size();
		}
		scene_state.shadow_passes.push_back(shadow_pass);
	}
}

void RenderForwardClustered::_render_shadow_process() {
	if (fellow_shadow_entry_count > 0 && Fellow::list_markers()) {
		RENDER_TIMESTAMP("FellowEngine Shadow Lists");
	}
	_fellow_shadow_flush();
	if (Fellow::list_markers()) {
		RENDER_TIMESTAMP("FellowEngine Shadow Process");
	}
	fellow_shadow_batching = false;
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (scene_state.instance_buffer[RENDER_LIST_SECONDARY].get_size(0u) > 0u) {
		rd->buffer_flush(scene_state.instance_buffer[RENDER_LIST_SECONDARY]._get(0u));
	}

	//render shadows one after the other, so this can be done un-barriered and the driver can optimize (as well as allow us to run compute at the same time)

	for (uint32_t i = 0; i < scene_state.shadow_passes.size(); i++) {
		//render passes need to be configured after instance buffer is done, since they need the latest version
		SceneState::ShadowPass &shadow_pass = scene_state.shadow_passes[i];
		if (shadow_pass.fellow_copy) {
			continue;
		}
		shadow_pass.rp_uniform_set = _setup_render_pass_uniform_set(RENDER_LIST_SECONDARY, nullptr, RID(), RendererRD::MaterialStorage::get_singleton()->samplers_rd_get_default(), shadow_pass.uniform_buffer_index, false);
	}

	RD::get_singleton()->draw_command_end_label();
}
void RenderForwardClustered::_render_shadow_end() {
	RD::get_singleton()->draw_command_begin_label("Shadow Render");

	for (SceneState::ShadowPass &shadow_pass : scene_state.shadow_passes) {
		if (shadow_pass.fellow_copy) {
			fellow_shadow_cache->copy(shadow_pass.fellow_source, shadow_pass.framebuffer, shadow_pass.rect, shadow_pass.fellow_source_offset, shadow_pass.fellow_scale, shadow_pass.fellow_bias);
			continue;
		}
		RenderListParameters render_list_parameters(render_list[RENDER_LIST_SECONDARY].elements.ptr() + shadow_pass.element_from, render_list[RENDER_LIST_SECONDARY].element_info.ptr() + shadow_pass.element_from, shadow_pass.element_count, shadow_pass.flip_cull, shadow_pass.pass_mode, 0, true, false, shadow_pass.rp_uniform_set, false, Vector2(), shadow_pass.lod_distance_multiplier, shadow_pass.screen_mesh_lod_threshold, 1, shadow_pass.element_from);
		render_list_parameters.fellow_cluster_planes = shadow_pass.fellow_cluster_planes;
		render_list_parameters.fellow_cluster_plane_count = shadow_pass.fellow_cluster_plane_count;
		_render_list_with_draw_list(&render_list_parameters, shadow_pass.framebuffer, shadow_pass.clear_depth ? RD::DRAW_CLEAR_DEPTH : RD::DRAW_DEFAULT_ALL, Vector<Color>(), 0.0f, 0, shadow_pass.rect);
	}

	RD::get_singleton()->draw_command_end_label();
}

void RenderForwardClustered::_render_particle_collider_heightfield(RID p_fb, const Transform3D &p_cam_transform, const Projection &p_cam_projection, const PagedArray<RenderGeometryInstance *> &p_instances) {
	RENDER_TIMESTAMP("Setup GPUParticlesCollisionHeightField3D");

	RD::get_singleton()->draw_command_begin_label("Render Collider Heightfield");

	RenderSceneDataRD scene_data;
	scene_data.flip_y = true;
	scene_data.cam_projection = p_cam_projection;
	scene_data.cam_transform = p_cam_transform;
	scene_data.view_projection[0] = p_cam_projection;
	scene_data.z_near = 0.0;
	scene_data.z_far = p_cam_projection.get_z_far();
	scene_data.dual_paraboloid_side = 0;
	scene_data.opaque_prepass_threshold = 0.0;
	scene_data.time = time;
	scene_data.time_step = time_step;
	scene_data.main_cam_transform = p_cam_transform;
	scene_data.shadow_pass = true; // Not a shadow pass, but should be treated like one.

	RenderDataRD render_data;
	render_data.scene_data = &scene_data;
	render_data.cluster_size = 1;
	render_data.cluster_max_elements = 32;
	render_data.instances = &p_instances;

	_update_render_base_uniform_set();

	Size2i screen_size = RD::get_singleton()->framebuffer_get_size(p_fb);
	uint32_t uniform_buffer_index = _setup_environment(&render_data, true, screen_size, screen_size, Color(), false, false, false);

	PassMode pass_mode = PASS_MODE_SHADOW;

	_fill_render_list(RENDER_LIST_SECONDARY, &render_data, pass_mode);
	render_list[RENDER_LIST_SECONDARY].sort_by_key();
	_fill_instance_data(RENDER_LIST_SECONDARY);

	RID rp_uniform_set = _setup_render_pass_uniform_set(RENDER_LIST_SECONDARY, nullptr, RID(), RendererRD::MaterialStorage::get_singleton()->samplers_rd_get_default(), uniform_buffer_index);

	RENDER_TIMESTAMP("Render Collider Heightfield");

	{
		//regular forward for now
		RenderListParameters render_list_params(render_list[RENDER_LIST_SECONDARY].elements.ptr(), render_list[RENDER_LIST_SECONDARY].element_info.ptr(), render_list[RENDER_LIST_SECONDARY].elements.size(), false, pass_mode, 0, true, false, rp_uniform_set);
		_render_list_with_draw_list(&render_list_params, p_fb, RD::DRAW_CLEAR_ALL);
	}
	RD::get_singleton()->draw_command_end_label();
}

void RenderForwardClustered::_render_material(const Transform3D &p_cam_transform, const Projection &p_cam_projection, bool p_cam_orthogonal, const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region, float p_exposure_normalization) {
	RENDER_TIMESTAMP("Setup Rendering 3D Material");

	RD::get_singleton()->draw_command_begin_label("Render 3D Material");

	RenderSceneDataRD scene_data;
	scene_data.cam_projection = p_cam_projection;
	scene_data.cam_transform = p_cam_transform;
	scene_data.view_projection[0] = p_cam_projection;
	scene_data.dual_paraboloid_side = 0;
	scene_data.material_uv2_mode = false;
	scene_data.opaque_prepass_threshold = 0.0f;
	scene_data.emissive_exposure_normalization = p_exposure_normalization;
	scene_data.time = time;
	scene_data.time_step = time_step;
	scene_data.main_cam_transform = p_cam_transform;

	RenderDataRD render_data;
	render_data.scene_data = &scene_data;
	render_data.cluster_size = 1;
	render_data.cluster_max_elements = 32;
	render_data.instances = &p_instances;

	scene_shader.enable_advanced_shader_group();

	_update_render_base_uniform_set();

	Size2i screen_size = RD::get_singleton()->framebuffer_get_size(p_framebuffer);
	Size2i viewport_size = p_region.size;
	if (viewport_size == Size2()) {
		viewport_size = screen_size;
	}
	uint32_t uniform_buffer_index = _setup_environment(&render_data, true, screen_size, viewport_size, Color());

	PassMode pass_mode = PASS_MODE_DEPTH_MATERIAL;
	_fill_render_list(RENDER_LIST_SECONDARY, &render_data, pass_mode);
	render_list[RENDER_LIST_SECONDARY].sort_by_key();
	_fill_instance_data(RENDER_LIST_SECONDARY);

	RID rp_uniform_set = _setup_render_pass_uniform_set(RENDER_LIST_SECONDARY, nullptr, RID(), RendererRD::MaterialStorage::get_singleton()->samplers_rd_get_default(), uniform_buffer_index);

	RENDER_TIMESTAMP("Render 3D Material");

	{
		RenderListParameters render_list_params(render_list[RENDER_LIST_SECONDARY].elements.ptr(), render_list[RENDER_LIST_SECONDARY].element_info.ptr(), render_list[RENDER_LIST_SECONDARY].elements.size(), true, pass_mode, 0, true, false, rp_uniform_set);
		//regular forward for now
		Vector<Color> clear = {
			Color(0, 0, 0, 0),
			Color(0, 0, 0, 0),
			Color(0, 0, 0, 0),
			Color(0, 0, 0, 0),
			Color(0, 0, 0, 0)
		};

		RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(p_framebuffer, RD::DRAW_CLEAR_ALL, clear, 0.0f, 0, p_region);
		_render_list(draw_list, RD::get_singleton()->framebuffer_get_format(p_framebuffer), &render_list_params, 0, render_list_params.element_count);
		RD::get_singleton()->draw_list_end();
	}

	RD::get_singleton()->draw_command_end_label();
}

void RenderForwardClustered::_render_uv2(const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region) {
	RENDER_TIMESTAMP("Setup Rendering UV2");

	RD::get_singleton()->draw_command_begin_label("Render UV2");

	RenderSceneDataRD scene_data;
	scene_data.dual_paraboloid_side = 0;
	scene_data.material_uv2_mode = true;
	scene_data.opaque_prepass_threshold = 0.0;
	scene_data.emissive_exposure_normalization = -1.0;

	RenderDataRD render_data;
	render_data.scene_data = &scene_data;
	render_data.cluster_size = 1;
	render_data.cluster_max_elements = 32;
	render_data.instances = &p_instances;

	scene_shader.enable_advanced_shader_group();

	_update_render_base_uniform_set();

	Size2i screen_size = RD::get_singleton()->framebuffer_get_size(p_framebuffer);
	Size2i viewport_size = p_region.size;
	if (viewport_size == Size2()) {
		viewport_size = screen_size;
	}
	uint32_t uniform_buffer_index = _setup_environment(&render_data, true, screen_size, viewport_size, Color());

	PassMode pass_mode = PASS_MODE_DEPTH_MATERIAL;
	_fill_render_list(RENDER_LIST_SECONDARY, &render_data, pass_mode);
	render_list[RENDER_LIST_SECONDARY].sort_by_key();
	_fill_instance_data(RENDER_LIST_SECONDARY);

	RID rp_uniform_set = _setup_render_pass_uniform_set(RENDER_LIST_SECONDARY, nullptr, RID(), RendererRD::MaterialStorage::get_singleton()->samplers_rd_get_default(), uniform_buffer_index);

	RENDER_TIMESTAMP("Render 3D Material");

	{
		RenderListParameters render_list_params(render_list[RENDER_LIST_SECONDARY].elements.ptr(), render_list[RENDER_LIST_SECONDARY].element_info.ptr(), render_list[RENDER_LIST_SECONDARY].elements.size(), true, pass_mode, 0, true, false, rp_uniform_set, true);
		//regular forward for now
		Vector<Color> clear = {
			Color(0, 0, 0, 0),
			Color(0, 0, 0, 0),
			Color(0, 0, 0, 0),
			Color(0, 0, 0, 0),
			Color(0, 0, 0, 0)
		};
		RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(p_framebuffer, RD::DRAW_CLEAR_ALL, clear, 0.0f, 0, p_region);

		const int uv_offset_count = 9;
		static const Vector2 uv_offsets[uv_offset_count] = {
			Vector2(-1, 1),
			Vector2(1, 1),
			Vector2(1, -1),
			Vector2(-1, -1),
			Vector2(-1, 0),
			Vector2(1, 0),
			Vector2(0, -1),
			Vector2(0, 1),
			Vector2(0, 0),

		};

		for (int i = 0; i < uv_offset_count; i++) {
			Vector2 ofs = uv_offsets[i];
			ofs.x /= p_region.size.width;
			ofs.y /= p_region.size.height;
			render_list_params.uv_offset = ofs;
			_render_list(draw_list, RD::get_singleton()->framebuffer_get_format(p_framebuffer), &render_list_params, 0, render_list_params.element_count); //first wireframe, for pseudo conservative
		}
		render_list_params.uv_offset = Vector2();
		render_list_params.force_wireframe = false;
		_render_list(draw_list, RD::get_singleton()->framebuffer_get_format(p_framebuffer), &render_list_params, 0, render_list_params.element_count); //second regular triangles

		RD::get_singleton()->draw_list_end();
	}

	RD::get_singleton()->draw_command_end_label();
}

void RenderForwardClustered::_render_sdfgi(Ref<RenderSceneBuffersRD> p_render_buffers, const Vector3i &p_from, const Vector3i &p_size, const AABB &p_bounds, const PagedArray<RenderGeometryInstance *> &p_instances, const RID &p_albedo_texture, const RID &p_emission_texture, const RID &p_emission_aniso_texture, const RID &p_geom_facing_texture, float p_exposure_normalization) {
	RENDER_TIMESTAMP("Render SDFGI");

	RD::get_singleton()->draw_command_begin_label("Render SDFGI Voxel");

	RenderSceneDataRD scene_data;

	RenderDataRD render_data;
	render_data.scene_data = &scene_data;
	render_data.cluster_size = 1;
	render_data.cluster_max_elements = 32;
	render_data.instances = &p_instances;

	_update_render_base_uniform_set();

	// Indicate pipelines for SDFGI are required.
	global_pipeline_data_required.use_sdfgi = true;

	PassMode pass_mode = PASS_MODE_SDF;
	_fill_render_list(RENDER_LIST_SECONDARY, &render_data, pass_mode);
	render_list[RENDER_LIST_SECONDARY].sort_by_key();
	_fill_instance_data(RENDER_LIST_SECONDARY);

	Vector3 half_size = p_bounds.size * 0.5;
	Vector3 center = p_bounds.position + half_size;

	//print_line("re-render " + p_from + " - " + p_size + " bounds " + p_bounds);
	for (int i = 0; i < 3; i++) {
		scene_state.ubo.sdf_offset[i] = p_from[i];
		scene_state.ubo.sdf_size[i] = p_size[i];
	}

	for (int i = 0; i < 3; i++) {
		Vector3 axis;
		axis[i] = 1.0;
		Vector3 up, right;
		int right_axis = (i + 1) % 3;
		int up_axis = (i + 2) % 3;
		up[up_axis] = 1.0;
		right[right_axis] = 1.0;

		Size2i fb_size;
		fb_size.x = p_size[right_axis];
		fb_size.y = p_size[up_axis];

		scene_data.cam_transform.origin = center + axis * half_size;
		scene_data.cam_transform.basis.set_column(0, right);
		scene_data.cam_transform.basis.set_column(1, up);
		scene_data.cam_transform.basis.set_column(2, axis);

		//print_line("pass: " + itos(i) + " xform " + scene_data.cam_transform);

		float h_size = half_size[right_axis];
		float v_size = half_size[up_axis];
		float d_size = half_size[i] * 2.0;
		scene_data.cam_projection.set_orthogonal(-h_size, h_size, -v_size, v_size, 0, d_size);
		//print_line("pass: " + itos(i) + " cam hsize: " + rtos(h_size) + " vsize: " + rtos(v_size) + " dsize " + rtos(d_size));

		Transform3D to_bounds;
		to_bounds.origin = p_bounds.position;
		to_bounds.basis.scale(p_bounds.size);

		RendererRD::MaterialStorage::store_transform(to_bounds.affine_inverse() * scene_data.cam_transform, scene_state.ubo.sdf_to_bounds);

		scene_data.emissive_exposure_normalization = p_exposure_normalization;
		uint32_t uniform_buffer_index = _setup_environment(&render_data, true, fb_size, fb_size, Color());

		RID rp_uniform_set = _setup_sdfgi_render_pass_uniform_set(p_albedo_texture, p_emission_texture, p_emission_aniso_texture, p_geom_facing_texture, RendererRD::MaterialStorage::get_singleton()->samplers_rd_get_default(), uniform_buffer_index);

		HashMap<Size2i, RID>::Iterator E = sdfgi_framebuffer_size_cache.find(fb_size);
		if (!E) {
			RID fb = RD::get_singleton()->framebuffer_create_empty(fb_size);
			E = sdfgi_framebuffer_size_cache.insert(fb_size, fb);
		}

		RenderListParameters render_list_params(render_list[RENDER_LIST_SECONDARY].elements.ptr(), render_list[RENDER_LIST_SECONDARY].element_info.ptr(), render_list[RENDER_LIST_SECONDARY].elements.size(), true, pass_mode, 0, true, false, rp_uniform_set, false);
		_render_list_with_draw_list(&render_list_params, E->value);
	}

	RD::get_singleton()->draw_command_end_label();
}

void RenderForwardClustered::base_uniforms_changed() {
	if (!render_base_uniform_set.is_null() && RD::get_singleton()->uniform_set_is_valid(render_base_uniform_set)) {
		RD::get_singleton()->free_rid(render_base_uniform_set);
	}
	render_base_uniform_set = RID();
}

void RenderForwardClustered::_update_render_base_uniform_set() {
	RendererRD::LightStorage *light_storage = RendererRD::LightStorage::get_singleton();

	if (render_base_uniform_set.is_null() || !RD::get_singleton()->uniform_set_is_valid(render_base_uniform_set) || (lightmap_texture_array_version != light_storage->lightmap_array_get_version())) {
		if (render_base_uniform_set.is_valid() && RD::get_singleton()->uniform_set_is_valid(render_base_uniform_set)) {
			RD::get_singleton()->free_rid(render_base_uniform_set);
		}

		lightmap_texture_array_version = light_storage->lightmap_array_get_version();

		Vector<RD::Uniform> uniforms;

		{
			RD::Uniform u;
			u.binding = 2;
			u.uniform_type = RD::UNIFORM_TYPE_SAMPLER;
			u.append_id(scene_shader.shadow_sampler);
			uniforms.push_back(u);
		}

		{
			RD::Uniform u;
			u.binding = 3;
			u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			u.append_id(RendererRD::LightStorage::get_singleton()->get_omni_light_buffer());
			uniforms.push_back(u);
		}
		{
			RD::Uniform u;
			u.binding = 4;
			u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			u.append_id(RendererRD::LightStorage::get_singleton()->get_spot_light_buffer());
			uniforms.push_back(u);
		}
		{
			RD::Uniform u;
			u.binding = 5;
			u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			u.append_id(RendererRD::LightStorage::get_singleton()->get_area_light_buffer());
			uniforms.push_back(u);
		}

		{
			RD::Uniform u;
			u.binding = 6;
			u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			u.append_id(RendererRD::LightStorage::get_singleton()->get_reflection_probe_buffer());
			uniforms.push_back(u);
		}
		{
			RD::Uniform u;
			u.binding = 7;
			u.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
			u.append_id(RendererRD::LightStorage::get_singleton()->get_directional_light_buffer());
			uniforms.push_back(u);
		}
		{
			RD::Uniform u;
			u.binding = 8;
			u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			u.append_id(scene_state.lightmap_buffer);
			uniforms.push_back(u);
		}
		{
			RD::Uniform u;
			u.binding = 9;
			u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			u.append_id(scene_state.lightmap_capture_buffer);
			uniforms.push_back(u);
		}
		{
			RD::Uniform u;
			u.binding = 10;
			u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
			RID decal_atlas = RendererRD::TextureStorage::get_singleton()->decal_atlas_get_texture();
			u.append_id(decal_atlas);
			uniforms.push_back(u);
		}
		{
			RD::Uniform u;
			u.binding = 11;
			u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
			RID decal_atlas = RendererRD::TextureStorage::get_singleton()->decal_atlas_get_texture_srgb();
			u.append_id(decal_atlas);
			uniforms.push_back(u);
		}
		{
			RD::Uniform u;
			u.binding = 12;
			u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			u.append_id(RendererRD::TextureStorage::get_singleton()->get_decal_buffer());
			uniforms.push_back(u);
		}

		{
			RD::Uniform u;
			u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
			u.binding = 13;
			u.append_id(RendererRD::MaterialStorage::get_singleton()->global_shader_uniforms_get_storage_buffer());
			uniforms.push_back(u);
		}

		{
			RD::Uniform u;
			u.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
			u.binding = 14;
			u.append_id(sdfgi_get_ubo());
			uniforms.push_back(u);
		}

		{
			RD::Uniform u;
			u.binding = 15;
			u.uniform_type = RD::UNIFORM_TYPE_SAMPLER;
			u.append_id(RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CanvasItemTextureFilter::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, RSE::CanvasItemTextureRepeat::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED));
			uniforms.push_back(u);
		}

		{
			RD::Uniform u;
			u.binding = 16;
			u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
			u.append_id(best_fit_normal.texture);
			uniforms.push_back(u);
		}

		{
			RD::Uniform u;
			u.binding = 17;
			u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
			u.append_id(dfg_lut.texture);
			uniforms.push_back(u);
		}

		{ // Lookup-table for Area Lights - Linearly transformed cosines (LTC)
			if (ltc.lut1_texture.is_null() || ltc.lut2_texture.is_null()) {
				Ref<Image> lut1_image;
				int dimensions = LTC_LUT_DIMENSIONS;
				int lut1_bytes = 4 * dimensions * dimensions;
				size_t lut1_size = lut1_bytes * 4; // float

				Vector<uint8_t> lut1_data;
				lut1_data.resize(lut1_size);

				memcpy(lut1_data.ptrw(), LTC_LUT1, lut1_size);
				lut1_image = Image::create_from_data(dimensions, dimensions, false, Image::FORMAT_RGBAF, lut1_data);

				ltc.lut1_texture = RS::get_singleton()->texture_2d_create(lut1_image);

				int lut2_bytes = 4 * dimensions * dimensions;
				size_t lut2_size = lut2_bytes * 4;

				Ref<Image> lut2_image;
				Vector<uint8_t> lut2_data;
				lut2_data.resize(lut2_size);

				memcpy(lut2_data.ptrw(), LTC_LUT2, lut2_size);
				lut2_image = Image::create_from_data(dimensions, dimensions, false, Image::FORMAT_RGBAF, lut2_data);

				ltc.lut2_texture = RS::get_singleton()->texture_2d_create(lut2_image);
			}
		}

		{
			RD::Uniform u;
			u.binding = 18;
			u.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
			u.append_id(RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED));
			u.append_id(RendererRD::TextureStorage::get_singleton()->texture_get_rd_texture(ltc.lut1_texture));
			uniforms.push_back(u);
		}

		{
			RD::Uniform u;
			u.binding = 19;
			u.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
			u.append_id(RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED));
			u.append_id(RendererRD::TextureStorage::get_singleton()->texture_get_rd_texture(ltc.lut2_texture));
			uniforms.push_back(u);
		}
		{
			RD::Uniform u;
			u.binding = 20;
			u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
			RID area_light_atlas = RendererRD::TextureStorage::get_singleton()->area_light_atlas_get_texture();
			u.append_id(area_light_atlas);
			uniforms.push_back(u);
		}

		render_base_uniform_set = RD::get_singleton()->uniform_set_create(uniforms, scene_shader.default_shader_rd, SCENE_UNIFORM_SET);
	}
}

RID RenderForwardClustered::_setup_render_pass_uniform_set(RenderListType p_render_list, const RenderDataRD *p_render_data, RID p_radiance_texture, const RendererRD::MaterialStorage::Samplers &p_samplers, uint32_t p_uniform_buffer_index, bool p_use_directional_shadow_atlas) {
	RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();
	RendererRD::LightStorage *light_storage = RendererRD::LightStorage::get_singleton();

	bool is_multiview = false;

	Ref<RenderSceneBuffersRD> rb; // handy for not having to fully type out p_render_data->render_buffers all the time...
	Ref<RenderBufferDataForwardClustered> rb_data;
	if (p_render_data && p_render_data->render_buffers.is_valid()) {
		rb = p_render_data->render_buffers;
		is_multiview = rb->get_view_count() > 1;
		if (rb->has_custom_data(RB_SCOPE_FORWARD_CLUSTERED)) {
			// Our forward clustered custom data buffer will only be available when we're rendering our normal view.
			// This will not be available when rendering reflection probes.
			rb_data = rb->get_custom_data(RB_SCOPE_FORWARD_CLUSTERED);
		}
	}

	//default render buffer and scene state uniform set

	// Persistent instance data (decision 0022): the entries queued by the lists filled since the last flush are uploaded
	// before the draws that use this uniform set are recorded (one dispatch for all of them).
	if (fellow_persistent.active) {
		_fellow_persistent_flush();
	}

	thread_local LocalVector<RD::Uniform> uniforms;
	uniforms.clear();

	{
		RD::Uniform u;
		u.binding = 0;
		u.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
		u.append_id(scene_state.uniform_buffers[p_uniform_buffer_index]);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 1;
		u.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
		u.append_id(scene_state.implementation_uniform_buffers[p_uniform_buffer_index]);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 2;
		u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC;
		if (scene_state.instance_buffer[p_render_list].get_size(0u) == 0u) {
			// Any buffer will do since it's not used, so just create one.
			// We can't use scene_shader.default_vec4_xform_buffer because it's not dynamic.
			scene_state.instance_buffer[p_render_list].set_storage_size(0u, INSTANCE_DATA_BUFFER_MIN_SIZE * sizeof(SceneState::InstanceData));
			scene_state.instance_buffer[p_render_list].prepare_for_upload();
		}
		RID instance_buffer = scene_state.instance_buffer[p_render_list]._get(0u);
		u.append_id(instance_buffer);
		uniforms.push_back(u);
	}
	{
		// FellowEngine persistent instance data (decision 0022): the list elements (the same per-frame buffer) and the
		// surfaces' entries; read only with persistent instance data.
		RD::Uniform u;
		u.binding = 39;
		u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC;
		u.append_id(scene_state.instance_buffer[p_render_list]._get(0u));
		uniforms.push_back(u);
	}
	{
		// The persistent buffer's region for this frame (a dynamic buffer, as the per-frame ones); any dynamic buffer
		// when there is none (not read).
		RD::Uniform u;
		u.binding = 40;
		u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC;
		u.append_id(fellow_persistent.buffer.is_valid() ? fellow_persistent.buffer : scene_state.instance_buffer[p_render_list]._get(0u));
		uniforms.push_back(u);
	}
	{
		// Occlusion culling (decision 0026): the opaque colour pass's compacted elements; any storage buffer otherwise (not
		// read).
		RD::Uniform u;
		u.binding = 41;
		u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
		u.append_id(p_render_list == RENDER_LIST_OPAQUE && fellow_occlusion.pass_active ? fellow_occlusion.compacted : scene_shader.default_vec4_xform_buffer);
		uniforms.push_back(u);
	}
	{
		RID radiance_texture;
		if (p_radiance_texture.is_valid()) {
			radiance_texture = p_radiance_texture;
		} else {
			radiance_texture = texture_storage->texture_rd_get_default(is_using_radiance_octmap_array() ? RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK : RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		}
		RD::Uniform u;
		u.binding = 3;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		u.append_id(radiance_texture);
		uniforms.push_back(u);
	}
	{
		RID ref_texture = (p_render_data && p_render_data->reflection_atlas.is_valid()) ? light_storage->reflection_atlas_get_texture(p_render_data->reflection_atlas) : RID();
		RD::Uniform u;
		u.binding = 4;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		if (ref_texture.is_valid()) {
			u.append_id(ref_texture);
		} else {
			u.append_id(texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK));
		}
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 5;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID texture;
		if (p_render_data && p_render_data->shadow_atlas.is_valid()) {
			texture = RendererRD::LightStorage::get_singleton()->shadow_atlas_get_texture(p_render_data->shadow_atlas);
		}
		if (!texture.is_valid()) {
			texture = texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_DEPTH);
		}
		u.append_id(texture);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 6;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		if (p_use_directional_shadow_atlas && RendererRD::LightStorage::get_singleton()->directional_shadow_get_texture().is_valid()) {
			u.append_id(RendererRD::LightStorage::get_singleton()->directional_shadow_get_texture());
		} else {
			u.append_id(texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_DEPTH));
		}
		uniforms.push_back(u);
	}
	{
		Vector<RID> textures;
		textures.resize(scene_state.max_lightmaps * 2);

		RID default_tex = texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_WHITE);
		for (uint32_t i = 0; i < scene_state.max_lightmaps * 2; i++) {
			uint32_t current_lightmap_index = i < scene_state.max_lightmaps ? i : i - scene_state.max_lightmaps;

			if (p_render_data && current_lightmap_index < p_render_data->lightmaps->size()) {
				RID base = light_storage->lightmap_instance_get_lightmap((*p_render_data->lightmaps)[current_lightmap_index]);
				RID texture;

				if (i < scene_state.max_lightmaps) {
					// Lightmap
					texture = light_storage->lightmap_get_texture(base);
				} else {
					// Shadowmask
					texture = light_storage->shadowmask_get_texture(base);
				}

				if (texture.is_valid()) {
					RID rd_texture = texture_storage->texture_get_rd_texture(texture);
					textures.write[i] = rd_texture;
					continue;
				}
			}

			textures.write[i] = default_tex;
		}
		RD::Uniform u(RD::UNIFORM_TYPE_TEXTURE, 7, textures);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 8;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID default_tex = texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_3D_WHITE);
		for (int i = 0; i < MAX_VOXEL_GI_INSTANCESS; i++) {
			if (p_render_data && i < (int)p_render_data->voxel_gi_instances->size()) {
				RID tex = gi.voxel_gi_instance_get_texture((*p_render_data->voxel_gi_instances)[i]);
				if (!tex.is_valid()) {
					tex = default_tex;
				}
				u.append_id(tex);
			} else {
				u.append_id(default_tex);
			}
		}

		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 9;
		u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
		RID cb = (p_render_data && p_render_data->cluster_buffer.is_valid()) ? p_render_data->cluster_buffer : scene_shader.default_vec4_xform_buffer;
		u.append_id(cb);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 10;
		u.uniform_type = RD::UNIFORM_TYPE_SAMPLER;
		RID sampler;
		switch (decals_get_filter()) {
			case RSE::DECAL_FILTER_NEAREST: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::DECAL_FILTER_LINEAR: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::DECAL_FILTER_NEAREST_MIPMAPS: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::DECAL_FILTER_LINEAR_MIPMAPS: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::DECAL_FILTER_NEAREST_MIPMAPS_ANISOTROPIC: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::DECAL_FILTER_LINEAR_MIPMAPS_ANISOTROPIC: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
		}

		u.append_id(sampler);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 11;
		u.uniform_type = RD::UNIFORM_TYPE_SAMPLER;
		RID sampler;
		switch (light_projectors_get_filter()) {
			case RSE::LIGHT_PROJECTOR_FILTER_NEAREST: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::LIGHT_PROJECTOR_FILTER_LINEAR: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::LIGHT_PROJECTOR_FILTER_NEAREST_MIPMAPS: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::LIGHT_PROJECTOR_FILTER_LINEAR_MIPMAPS: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::LIGHT_PROJECTOR_FILTER_NEAREST_MIPMAPS_ANISOTROPIC: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::LIGHT_PROJECTOR_FILTER_LINEAR_MIPMAPS_ANISOTROPIC: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
		}

		u.append_id(sampler);
		uniforms.push_back(u);
	}

	p_samplers.append_uniforms(uniforms, 12);

	{
		RD::Uniform u;
		u.binding = 24;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID texture;
		if (rb.is_valid() && rb->has_texture(RB_SCOPE_BUFFERS, RB_TEX_BACK_DEPTH)) {
			texture = rb->get_texture(RB_SCOPE_BUFFERS, RB_TEX_BACK_DEPTH);
		} else {
			texture = texture_storage->texture_rd_get_default(is_multiview ? RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_DEPTH : RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_DEPTH);
		}
		u.append_id(texture);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 25;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID bbt = rb_data.is_valid() ? rb->get_back_buffer_texture() : RID();
		RID texture = bbt.is_valid() ? bbt : texture_storage->texture_rd_get_default(is_multiview ? RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK : RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		u.append_id(texture);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 26;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID texture = rb_data.is_valid() && rb_data->has_normal_roughness() ? rb_data->get_normal_roughness() : texture_storage->texture_rd_get_default(is_multiview ? RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_NORMAL : RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_NORMAL);
		u.append_id(texture);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 27;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID aot = rb.is_valid() && rb->has_texture(RB_SCOPE_SSAO, RB_FINAL) ? rb->get_texture(RB_SCOPE_SSAO, RB_FINAL) : RID();
		RID texture = aot.is_valid() ? aot : texture_storage->texture_rd_get_default(is_multiview ? RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK : RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		u.append_id(texture);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 28;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID texture = rb_data.is_valid() && rb->has_texture(RB_SCOPE_GI, RB_TEX_AMBIENT) ? rb->get_texture(RB_SCOPE_GI, RB_TEX_AMBIENT) : texture_storage->texture_rd_get_default(is_multiview ? RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK : RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		u.append_id(texture);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 29;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID texture = rb_data.is_valid() && rb->has_texture(RB_SCOPE_GI, RB_TEX_REFLECTION) ? rb->get_texture(RB_SCOPE_GI, RB_TEX_REFLECTION) : texture_storage->texture_rd_get_default(is_multiview ? RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK : RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		u.append_id(texture);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 30;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID t;
		if (rb.is_valid() && rb->has_custom_data(RB_SCOPE_SDFGI)) {
			Ref<RendererRD::GI::SDFGI> sdfgi = rb->get_custom_data(RB_SCOPE_SDFGI);
			t = sdfgi->lightprobe_texture;
		}
		if (t.is_null()) {
			t = texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_WHITE);
		}
		u.append_id(t);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 31;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID t;
		if (rb.is_valid() && rb->has_custom_data(RB_SCOPE_SDFGI)) {
			Ref<RendererRD::GI::SDFGI> sdfgi = rb->get_custom_data(RB_SCOPE_SDFGI);
			t = sdfgi->occlusion_texture;
		}
		if (t.is_null()) {
			t = texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_3D_WHITE);
		}
		u.append_id(t);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 32;
		u.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
		RID voxel_gi;
		if (rb.is_valid() && rb->has_custom_data(RB_SCOPE_GI)) {
			Ref<RendererRD::GI::RenderBuffersGI> rbgi = rb->get_custom_data(RB_SCOPE_GI);
			voxel_gi = rbgi->get_voxel_gi_buffer();
		}
		u.append_id(voxel_gi.is_valid() ? voxel_gi : render_buffers_get_default_voxel_gi_buffer());
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 33;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID vfog;
		if (rb_data.is_valid() && rb->has_custom_data(RB_SCOPE_FOG)) {
			Ref<RendererRD::Fog::VolumetricFog> fog = rb->get_custom_data(RB_SCOPE_FOG);
			vfog = fog->fog_map;
			if (vfog.is_null()) {
				vfog = texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_3D_WHITE);
			}
		} else {
			vfog = texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_3D_WHITE);
		}
		u.append_id(vfog);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 34;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID ssil = rb.is_valid() && rb->has_texture(RB_SCOPE_SSIL, RB_FINAL) ? rb->get_texture(RB_SCOPE_SSIL, RB_FINAL) : RID();
		RID texture = ssil.is_valid() ? ssil : texture_storage->texture_rd_get_default(is_multiview ? RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK : RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		u.append_id(texture);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 35;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;

		RID ssr;
		if (rb_data.is_valid()) {
			if (rb_data->ss_effects_data.ssr.half_size) {
				if (rb->has_texture(RB_SCOPE_SSR, RB_FINAL)) {
					ssr = rb->get_texture(RB_SCOPE_SSR, RB_FINAL);
				}
			} else {
				if (rb->has_texture(RB_SCOPE_SSR, RB_SSR)) {
					ssr = rb->get_texture(RB_SCOPE_SSR, RB_SSR);
				}
			}
		}

		RID texture = ssr.is_valid() ? ssr : texture_storage->texture_rd_get_default(is_multiview ? RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK : RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		u.append_id(texture);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 36;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;

		RID ssr_mip_level = (rb_data.is_valid() && !rb_data->ss_effects_data.ssr.half_size && rb->has_texture(RB_SCOPE_SSR, RB_MIP_LEVEL)) ? rb->get_texture(RB_SCOPE_SSR, RB_MIP_LEVEL) : RID();
		RID texture = ssr_mip_level.is_valid() ? ssr_mip_level : texture_storage->texture_rd_get_default(is_multiview ? RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK : RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		u.append_id(texture);
		uniforms.push_back(u);
	}
	{
		// FellowEngine directional shadow max pyramid (sampled only when SCENE_DATA_FLAGS_FELLOW_SHADOW_MAX_MIP is set).
		RD::Uniform u;
		u.binding = 37;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID texture = (fellow_shadow_max_mip && fellow_shadow_max_mip->get_texture(RID()).is_valid()) ? fellow_shadow_max_mip->get_texture(RID()) : texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_WHITE);
		u.append_id(texture);
		uniforms.push_back(u);
	}
	{
		// FellowEngine positional shadow min/max pyramid of this render's shadow atlas (sampled only when
		// SCENE_DATA_FLAGS_FELLOW_POSITIONAL_SHADOW_MAX_MIP is set).
		RD::Uniform u;
		u.binding = 38;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID pos_texture;
		if (fellow_shadow_max_mip && p_render_data && p_render_data->shadow_atlas.is_valid()) {
			pos_texture = fellow_shadow_max_mip->get_texture(p_render_data->shadow_atlas);
		}
		u.append_id(pos_texture.is_valid() ? pos_texture : texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_WHITE));
		uniforms.push_back(u);
	}

	return UniformSetCacheRD::get_singleton()->get_cache_vec(scene_shader.default_shader_rd, RENDER_PASS_UNIFORM_SET, uniforms);
}

RID RenderForwardClustered::_setup_sdfgi_render_pass_uniform_set(RID p_albedo_texture, RID p_emission_texture, RID p_emission_aniso_texture, RID p_geom_facing_texture, const RendererRD::MaterialStorage::Samplers &p_samplers, uint32_t p_uniform_buffer_index) {
	RendererRD::TextureStorage *texture_storage = RendererRD::TextureStorage::get_singleton();
	if (fellow_persistent.active) {
		_fellow_persistent_flush(); // see _setup_render_pass_uniform_set()
	}
	thread_local LocalVector<RD::Uniform> uniforms;
	uniforms.clear();

	{
		RD::Uniform u;
		u.binding = 0;
		u.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
		u.append_id(scene_state.uniform_buffers[p_uniform_buffer_index]);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 1;
		u.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
		u.append_id(scene_state.implementation_uniform_buffers[p_uniform_buffer_index]);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 2;
		u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC;
		if (scene_state.instance_buffer[RENDER_LIST_SECONDARY].get_size(0u) == 0u) {
			// Any buffer will do since it's not used, so just create one.
			// We can't use scene_shader.default_vec4_xform_buffer because it's not dynamic.
			scene_state.instance_buffer[RENDER_LIST_SECONDARY].set_storage_size(0u, INSTANCE_DATA_BUFFER_MIN_SIZE * sizeof(SceneState::InstanceData));
			scene_state.instance_buffer[RENDER_LIST_SECONDARY].prepare_for_upload();
		}
		RID instance_buffer = scene_state.instance_buffer[RENDER_LIST_SECONDARY]._get(0u);
		u.append_id(instance_buffer);
		uniforms.push_back(u);
	}
	{
		// FellowEngine persistent instance data (decision 0022): the list elements (the same per-frame buffer) and the
		// surfaces' entries; read only with persistent instance data.
		RD::Uniform u;
		u.binding = 39;
		u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC;
		u.append_id(scene_state.instance_buffer[RENDER_LIST_SECONDARY]._get(0u));
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 40;
		u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC;
		u.append_id(fellow_persistent.buffer.is_valid() ? fellow_persistent.buffer : scene_state.instance_buffer[RENDER_LIST_SECONDARY]._get(0u));
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.binding = 41;
		u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
		u.append_id(scene_shader.default_vec4_xform_buffer);
		uniforms.push_back(u);
	}
	{
		// No radiance texture.
		RID radiance_texture = texture_storage->texture_rd_get_default(is_using_radiance_octmap_array() ? RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK : RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
		RD::Uniform u;
		u.binding = 3;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		u.append_id(radiance_texture);
		uniforms.push_back(u);
	}

	{
		// No reflection atlas.
		RID ref_texture = texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_BLACK);
		RD::Uniform u;
		u.binding = 4;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		u.append_id(ref_texture);
		uniforms.push_back(u);
	}

	{
		// No shadow atlas.
		RD::Uniform u;
		u.binding = 5;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID texture = texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_DEPTH);
		u.append_id(texture);
		uniforms.push_back(u);
	}

	{
		// No directional shadow atlas.
		RD::Uniform u;
		u.binding = 6;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
		RID texture = texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_DEPTH);
		u.append_id(texture);
		uniforms.push_back(u);
	}

	{
		// No Lightmaps
		RD::Uniform u;
		u.binding = 7;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;

		RID default_tex = texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_2D_ARRAY_WHITE);
		for (uint32_t i = 0; i < scene_state.max_lightmaps * 2; i++) {
			u.append_id(default_tex);
		}

		uniforms.push_back(u);
	}

	{
		// No VoxelGIs
		RD::Uniform u;
		u.binding = 8;
		u.uniform_type = RD::UNIFORM_TYPE_TEXTURE;

		RID default_tex = texture_storage->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_3D_WHITE);
		for (int i = 0; i < MAX_VOXEL_GI_INSTANCESS; i++) {
			u.append_id(default_tex);
		}

		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 9;
		u.uniform_type = RD::UNIFORM_TYPE_STORAGE_BUFFER;
		RID cb = scene_shader.default_vec4_xform_buffer;
		u.append_id(cb);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 10;
		u.uniform_type = RD::UNIFORM_TYPE_SAMPLER;
		RID sampler;
		switch (decals_get_filter()) {
			case RSE::DECAL_FILTER_NEAREST: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::DECAL_FILTER_LINEAR: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::DECAL_FILTER_NEAREST_MIPMAPS: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::DECAL_FILTER_LINEAR_MIPMAPS: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::DECAL_FILTER_NEAREST_MIPMAPS_ANISOTROPIC: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::DECAL_FILTER_LINEAR_MIPMAPS_ANISOTROPIC: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
		}

		u.append_id(sampler);
		uniforms.push_back(u);
	}

	{
		RD::Uniform u;
		u.binding = 11;
		u.uniform_type = RD::UNIFORM_TYPE_SAMPLER;
		RID sampler;
		switch (light_projectors_get_filter()) {
			case RSE::LIGHT_PROJECTOR_FILTER_NEAREST: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::LIGHT_PROJECTOR_FILTER_LINEAR: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::LIGHT_PROJECTOR_FILTER_NEAREST_MIPMAPS: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::LIGHT_PROJECTOR_FILTER_LINEAR_MIPMAPS: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::LIGHT_PROJECTOR_FILTER_NEAREST_MIPMAPS_ANISOTROPIC: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST_WITH_MIPMAPS_ANISOTROPIC, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
			case RSE::LIGHT_PROJECTOR_FILTER_LINEAR_MIPMAPS_ANISOTROPIC: {
				sampler = p_samplers.get_sampler(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
			} break;
		}

		u.append_id(sampler);
		uniforms.push_back(u);
	}

	p_samplers.append_uniforms(uniforms, 12);

	// actual sdfgi stuff

	{
		RD::Uniform u;
		u.uniform_type = RD::UNIFORM_TYPE_IMAGE;
		u.binding = 24;
		u.append_id(p_albedo_texture);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.uniform_type = RD::UNIFORM_TYPE_IMAGE;
		u.binding = 25;
		u.append_id(p_emission_texture);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.uniform_type = RD::UNIFORM_TYPE_IMAGE;
		u.binding = 26;
		u.append_id(p_emission_aniso_texture);
		uniforms.push_back(u);
	}
	{
		RD::Uniform u;
		u.uniform_type = RD::UNIFORM_TYPE_IMAGE;
		u.binding = 27;
		u.append_id(p_geom_facing_texture);
		uniforms.push_back(u);
	}

	if (scene_shader.default_shader_sdfgi_rd.is_null()) {
		// The variant for SDF from the default material should only be retrieved when SDFGI is required.
		ERR_FAIL_NULL_V(scene_shader.default_material_shader_ptr, RID());
		scene_shader.enable_advanced_shader_group();
		scene_shader.default_shader_sdfgi_rd = scene_shader.default_material_shader_ptr->get_shader_variant(SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS_WITH_SDF, 0, true);
		ERR_FAIL_COND_V(scene_shader.default_shader_sdfgi_rd.is_null(), RID());
	}

	return UniformSetCacheRD::get_singleton()->get_cache_vec(scene_shader.default_shader_sdfgi_rd, RENDER_PASS_UNIFORM_SET, uniforms);
}

RID RenderForwardClustered::_render_buffers_get_normal_texture(Ref<RenderSceneBuffersRD> p_render_buffers) {
	Ref<RenderBufferDataForwardClustered> rb_data = p_render_buffers->get_custom_data(RB_SCOPE_FORWARD_CLUSTERED);

	return rb_data->get_normal_roughness();
}

RID RenderForwardClustered::_render_buffers_get_velocity_texture(Ref<RenderSceneBuffersRD> p_render_buffers) {
	return p_render_buffers->get_velocity_buffer(false);
}

void RenderForwardClustered::environment_set_ssao_quality(RSE::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) {
	ERR_FAIL_NULL(ss_effects);
	ERR_FAIL_COND(p_quality < RSE::EnvironmentSSAOQuality::ENV_SSAO_QUALITY_VERY_LOW || p_quality > RSE::EnvironmentSSAOQuality::ENV_SSAO_QUALITY_ULTRA);
	ss_effects->ssao_set_quality(p_quality, p_half_size, p_adaptive_target, p_blur_passes, p_fadeout_from, p_fadeout_to);
}

void RenderForwardClustered::environment_set_ssil_quality(RSE::EnvironmentSSILQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) {
	ERR_FAIL_NULL(ss_effects);
	ERR_FAIL_COND(p_quality < RSE::EnvironmentSSILQuality::ENV_SSIL_QUALITY_VERY_LOW || p_quality > RSE::EnvironmentSSILQuality::ENV_SSIL_QUALITY_ULTRA);
	ss_effects->ssil_set_quality(p_quality, p_half_size, p_adaptive_target, p_blur_passes, p_fadeout_from, p_fadeout_to);
}

void RenderForwardClustered::environment_set_ssr_half_size(bool p_half_size) {
	ERR_FAIL_NULL(ss_effects);
	ss_effects->ssr_set_half_size(p_half_size);
}

void RenderForwardClustered::environment_set_ssr_roughness_quality(RSE::EnvironmentSSRRoughnessQuality p_quality) {
	WARN_PRINT_ONCE("environment_set_ssr_roughness_quality has been deprecated and no longer does anything.");
}

void RenderForwardClustered::sub_surface_scattering_set_quality(RSE::SubSurfaceScatteringQuality p_quality) {
	ERR_FAIL_NULL(ss_effects);
	ERR_FAIL_COND(p_quality < RSE::SubSurfaceScatteringQuality::SUB_SURFACE_SCATTERING_QUALITY_DISABLED || p_quality > RSE::SubSurfaceScatteringQuality::SUB_SURFACE_SCATTERING_QUALITY_HIGH);
	ss_effects->sss_set_quality(p_quality);
}

void RenderForwardClustered::sub_surface_scattering_set_scale(float p_scale, float p_depth_scale) {
	ERR_FAIL_NULL(ss_effects);
	ss_effects->sss_set_scale(p_scale, p_depth_scale);
}

RenderForwardClustered *RenderForwardClustered::singleton = nullptr;

void RenderForwardClustered::sdfgi_update(const Ref<RenderSceneBuffers> &p_render_buffers, RID p_environment, const Vector3 &p_world_position) {
	Ref<RenderSceneBuffersRD> rb = p_render_buffers;
	ERR_FAIL_COND(rb.is_null());
	Ref<RendererRD::GI::SDFGI> sdfgi;
	if (rb->has_custom_data(RB_SCOPE_SDFGI)) {
		sdfgi = rb->get_custom_data(RB_SCOPE_SDFGI);
	}

	bool needs_sdfgi = p_environment.is_valid() && environment_get_sdfgi_enabled(p_environment);
	bool needs_reset = sdfgi.is_valid() ? sdfgi->version != gi.sdfgi_current_version : false;

	if (!needs_sdfgi || needs_reset) {
		if (sdfgi.is_valid()) {
			// delete it
			sdfgi.unref();
			rb->set_custom_data(RB_SCOPE_SDFGI, sdfgi);
		}

		if (!needs_sdfgi) {
			return;
		}
	}

	// Ensure advanced shaders are available if SDFGI is used.
	// Call here as this is the first entry point for SDFGI.
	scene_shader.enable_advanced_shader_group();

	static const uint32_t history_frames_to_converge[RSE::ENV_SDFGI_CONVERGE_MAX] = { 5, 10, 15, 20, 25, 30 };
	uint32_t requested_history_size = history_frames_to_converge[gi.sdfgi_frames_to_converge];

	if (sdfgi.is_valid() && (sdfgi->num_cascades != environment_get_sdfgi_cascades(p_environment) || sdfgi->min_cell_size != environment_get_sdfgi_min_cell_size(p_environment) || requested_history_size != sdfgi->history_size || sdfgi->uses_occlusion != environment_get_sdfgi_use_occlusion(p_environment) || sdfgi->y_scale_mode != environment_get_sdfgi_y_scale(p_environment))) {
		//configuration changed, erase
		sdfgi.unref();
		rb->set_custom_data(RB_SCOPE_SDFGI, sdfgi);
	}

	if (sdfgi.is_null()) {
		// re-create
		sdfgi = gi.create_sdfgi(p_environment, p_world_position, requested_history_size);
		rb->set_custom_data(RB_SCOPE_SDFGI, sdfgi);
	} else {
		//check for updates
		sdfgi->update(p_environment, p_world_position);
	}
}

int RenderForwardClustered::sdfgi_get_pending_region_count(const Ref<RenderSceneBuffers> &p_render_buffers) const {
	Ref<RenderSceneBuffersRD> rb = p_render_buffers;
	ERR_FAIL_COND_V(rb.is_null(), 0);

	if (!rb->has_custom_data(RB_SCOPE_SDFGI)) {
		return 0;
	}
	Ref<RendererRD::GI::SDFGI> sdfgi = rb->get_custom_data(RB_SCOPE_SDFGI);

	int dirty_count = 0;
	for (const RendererRD::GI::SDFGI::Cascade &c : sdfgi->cascades) {
		if (c.dirty_regions == RendererRD::GI::SDFGI::Cascade::DIRTY_ALL) {
			dirty_count++;
		} else {
			for (int j = 0; j < 3; j++) {
				if (c.dirty_regions[j] != 0) {
					dirty_count++;
				}
			}
		}
	}

	return dirty_count;
}

AABB RenderForwardClustered::sdfgi_get_pending_region_bounds(const Ref<RenderSceneBuffers> &p_render_buffers, int p_region) const {
	AABB bounds;
	Vector3i from;
	Vector3i size;

	Ref<RenderSceneBuffersRD> rb = p_render_buffers;
	ERR_FAIL_COND_V(rb.is_null(), AABB());
	Ref<RendererRD::GI::SDFGI> sdfgi = rb->get_custom_data(RB_SCOPE_SDFGI);
	ERR_FAIL_COND_V(sdfgi.is_null(), AABB());

	int c = sdfgi->get_pending_region_data(p_region, from, size, bounds);
	ERR_FAIL_COND_V(c == -1, AABB());
	return bounds;
}

uint32_t RenderForwardClustered::sdfgi_get_pending_region_cascade(const Ref<RenderSceneBuffers> &p_render_buffers, int p_region) const {
	AABB bounds;
	Vector3i from;
	Vector3i size;

	Ref<RenderSceneBuffersRD> rb = p_render_buffers;
	ERR_FAIL_COND_V(rb.is_null(), -1);
	Ref<RendererRD::GI::SDFGI> sdfgi = rb->get_custom_data(RB_SCOPE_SDFGI);
	ERR_FAIL_COND_V(sdfgi.is_null(), -1);

	return sdfgi->get_pending_region_data(p_region, from, size, bounds);
}

void RenderForwardClustered::GeometryInstanceForwardClustered::_mark_dirty() {
	if (dirty_list_element.in_list()) {
		return;
	}

	//clear surface caches
	GeometryInstanceSurfaceDataCache *surf = surface_caches;

	while (surf) {
		GeometryInstanceSurfaceDataCache *next = surf->next;
		RenderForwardClustered::get_singleton()->_fellow_slot_free(surf->fellow_slot);
		RenderForwardClustered::get_singleton()->geometry_instance_surface_alloc.free(surf);
		surf = next;
	}

	surface_caches = nullptr;

	RenderForwardClustered::get_singleton()->geometry_instance_dirty_list.add(&dirty_list_element);
}

void RenderForwardClustered::_update_global_pipeline_data_requirements_from_project() {
	const int msaa_3d_mode = GLOBAL_GET_CACHED(int, "rendering/anti_aliasing/quality/msaa_3d");
	const bool directional_shadow_16_bits = GLOBAL_GET_CACHED(bool, "rendering/lights_and_shadows/directional_shadow/16_bits");
	const bool positional_shadow_16_bits = GLOBAL_GET_CACHED(bool, "rendering/lights_and_shadows/positional_shadow/atlas_16_bits");
	global_pipeline_data_required.use_16_bit_shadows = directional_shadow_16_bits || positional_shadow_16_bits;
	global_pipeline_data_required.use_32_bit_shadows = !directional_shadow_16_bits || !positional_shadow_16_bits;
	global_pipeline_data_required.texture_samples = RenderSceneBuffersRD::msaa_to_samples(RSE::ViewportMSAA(msaa_3d_mode));
}

void RenderForwardClustered::_update_global_pipeline_data_requirements_from_light_storage() {
	RendererRD::LightStorage *light_storage = RendererRD::LightStorage::get_singleton();
	global_pipeline_data_required.use_shadow_cubemaps = light_storage->get_shadow_cubemaps_used();
	global_pipeline_data_required.use_shadow_dual_paraboloid = light_storage->get_shadow_dual_paraboloid_used();
}

void RenderForwardClustered::_geometry_instance_add_surface_with_material(GeometryInstanceForwardClustered *ginstance, uint32_t p_surface, SceneShaderForwardClustered::MaterialData *p_material, uint32_t p_material_id, uint32_t p_shader_id, RID p_mesh) {
	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	uint32_t flags = 0;

	if (p_material->shader_data->uses_sss) {
		flags |= GeometryInstanceSurfaceDataCache::FLAG_USES_SUBSURFACE_SCATTERING;
		global_surface_data.sss_used = true;
	}

	if (p_material->shader_data->uses_screen_texture) {
		flags |= GeometryInstanceSurfaceDataCache::FLAG_USES_SCREEN_TEXTURE;
		global_surface_data.screen_texture_used = true;
	}

	if (p_material->shader_data->uses_depth_texture) {
		flags |= GeometryInstanceSurfaceDataCache::FLAG_USES_DEPTH_TEXTURE;
		global_surface_data.depth_texture_used = true;
	}

	if (p_material->shader_data->uses_normal_texture) {
		flags |= GeometryInstanceSurfaceDataCache::FLAG_USES_NORMAL_TEXTURE;
		global_surface_data.normal_texture_used = true;
	}

	if (ginstance->data->cast_double_sided_shadows) {
		flags |= GeometryInstanceSurfaceDataCache::FLAG_USES_DOUBLE_SIDED_SHADOWS;
	}

	if (p_material->shader_data->stencil_enabled) {
		flags |= GeometryInstanceSurfaceDataCache::FLAG_USES_STENCIL;
	}

	if (p_material->shader_data->uses_alpha_pass()) {
		flags |= GeometryInstanceSurfaceDataCache::FLAG_PASS_ALPHA;
		if (p_material->shader_data->uses_depth_in_alpha_pass()) {
			flags |= GeometryInstanceSurfaceDataCache::FLAG_PASS_DEPTH;
			flags |= GeometryInstanceSurfaceDataCache::FLAG_PASS_SHADOW;
		}
	} else {
		flags |= GeometryInstanceSurfaceDataCache::FLAG_PASS_OPAQUE;
		flags |= GeometryInstanceSurfaceDataCache::FLAG_PASS_DEPTH;
		flags |= GeometryInstanceSurfaceDataCache::FLAG_PASS_SHADOW;
	}

	if (p_material->shader_data->uses_particle_trails) {
		flags |= GeometryInstanceSurfaceDataCache::FLAG_USES_PARTICLE_TRAILS;
	}

	if (p_material->shader_data->is_animated()) {
		flags |= GeometryInstanceSurfaceDataCache::FLAG_USES_MOTION_VECTOR;
	}

	if (p_material->shader_data->stencil_enabled) {
		if (p_material->shader_data->stencil_flags & SceneShaderForwardClustered::ShaderData::STENCIL_FLAG_READ) {
			// Stencil materials which read from the stencil buffer must be in the alpha pass.
			// This is critical to preserve compatibility once we'll have the compositor.
			if (!(flags & GeometryInstanceSurfaceDataCache::FLAG_PASS_ALPHA)) {
				String shader_path = p_material->shader_data->path.is_empty() ? "" : "(" + p_material->shader_data->path + ")";
				ERR_PRINT_ED(vformat("Attempting to use a shader %s that reads stencil but is not in the alpha queue. Ensure the material uses alpha blending or has depth_draw disabled or depth_test disabled.", shader_path));
			}
		}
	}

	SceneShaderForwardClustered::MaterialData *material_shadow = nullptr;
	void *surface_shadow = nullptr;
	if (p_material->shader_data->uses_shared_shadow_material()) {
		flags |= GeometryInstanceSurfaceDataCache::FLAG_USES_SHARED_SHADOW_MATERIAL;
		material_shadow = static_cast<SceneShaderForwardClustered::MaterialData *>(RendererRD::MaterialStorage::get_singleton()->material_get_data(scene_shader.default_material, RendererRD::MaterialStorage::SHADER_TYPE_3D));

		RID shadow_mesh = mesh_storage->mesh_get_shadow_mesh(p_mesh);
		if (shadow_mesh.is_valid()) {
			surface_shadow = mesh_storage->mesh_get_surface(shadow_mesh, p_surface);
		}
	} else {
		material_shadow = p_material;
	}

	GeometryInstanceSurfaceDataCache *sdcache = geometry_instance_surface_alloc.alloc();

	sdcache->flags = flags;
	sdcache->fellow_slot = _fellow_slot_alloc();
	sdcache->fellow_seen_version.store(0, std::memory_order_relaxed);
	sdcache->fellow_checked_frame.store(0, std::memory_order_relaxed);

	sdcache->shader = p_material->shader_data;
	sdcache->material = p_material;
	sdcache->material_uniform_set = p_material->uniform_set;
	sdcache->surface = mesh_storage->mesh_get_surface(p_mesh, p_surface);
	sdcache->primitive = mesh_storage->mesh_surface_get_primitive(sdcache->surface);
	_fellow_occlusion_set_slot_bounds(sdcache->fellow_slot, sdcache->surface);
	sdcache->surface_index = p_surface;

	if (ginstance->data->dirty_dependencies) {
		RSG::utilities->base_update_dependency(p_mesh, &ginstance->data->dependency_tracker);
	}

	//shadow
	sdcache->shader_shadow = material_shadow->shader_data;
	sdcache->material_uniform_set_shadow = material_shadow->uniform_set;

	sdcache->surface_shadow = surface_shadow ? surface_shadow : sdcache->surface;

	sdcache->owner = ginstance;

	sdcache->next = ginstance->surface_caches;
	ginstance->surface_caches = sdcache;

	//sortkey

	sdcache->sort.sort_key1 = 0;
	sdcache->sort.sort_key2 = 0;

	sdcache->sort.surface_index = p_surface;
	sdcache->sort.material_id_hi = (p_material_id & 0xFF000000) >> 24;
	sdcache->sort.material_id_lo = (p_material_id & 0x00FFFFFF);
	sdcache->sort.shader_id = p_shader_id;
	sdcache->sort.geometry_id = p_mesh.get_local_index(); //only meshes can repeat anyway
	sdcache->sort.uses_forward_gi = ginstance->can_sdfgi;
	sdcache->sort.priority = p_material->priority;
	sdcache->sort.uses_projector = ginstance->using_projectors;
	sdcache->sort.uses_softshadow = ginstance->using_softshadows;

	uint64_t format = RendererRD::MeshStorage::get_singleton()->mesh_surface_get_format(sdcache->surface);
	if (p_material->shader_data->uses_tangent && !p_material->shader_data->writes_tangent && !(format & RSE::ARRAY_FORMAT_TANGENT)) {
		String shader_path = p_material->shader_data->path.is_empty() ? "" : "(" + p_material->shader_data->path + ")";
		String mesh_path = mesh_storage->mesh_get_path(p_mesh).is_empty() ? "" : "(" + mesh_storage->mesh_get_path(p_mesh) + ")";
		WARN_PRINT_ED(vformat("Attempting to use a shader %s that requires tangents with a mesh %s that doesn't contain tangents. Ensure that meshes are imported with the 'ensure_tangents' option. If creating your own meshes, add an `ARRAY_TANGENT` array (when using ArrayMesh) or call `generate_tangents()` (when using SurfaceTool).", shader_path, mesh_path));
	}

#if PRELOAD_PIPELINES_ON_SURFACE_CACHE_CONSTRUCTION
	if (!sdcache->compilation_dirty_element.in_list()) {
		geometry_surface_compilation_dirty_list.add(&sdcache->compilation_dirty_element);
	}

	if (!sdcache->compilation_all_element.in_list()) {
		geometry_surface_compilation_all_list.add(&sdcache->compilation_all_element);
	}
#endif
}

void RenderForwardClustered::_geometry_instance_add_surface_with_material_chain(GeometryInstanceForwardClustered *ginstance, uint32_t p_surface, SceneShaderForwardClustered::MaterialData *p_material, RID p_mat_src, RID p_mesh) {
	SceneShaderForwardClustered::MaterialData *material = p_material;
	RendererRD::MaterialStorage *material_storage = RendererRD::MaterialStorage::get_singleton();

	_geometry_instance_add_surface_with_material(ginstance, p_surface, material, p_mat_src.get_local_index(), material_storage->material_get_shader_id(p_mat_src), p_mesh);

	while (material->next_pass.is_valid()) {
		RID next_pass = material->next_pass;
		material = static_cast<SceneShaderForwardClustered::MaterialData *>(material_storage->material_get_data(next_pass, RendererRD::MaterialStorage::SHADER_TYPE_3D));
		if (!material || !material->shader_data->is_valid()) {
			break;
		}
		if (ginstance->data->dirty_dependencies) {
			material_storage->material_update_dependency(next_pass, &ginstance->data->dependency_tracker);
		}
		_geometry_instance_add_surface_with_material(ginstance, p_surface, material, next_pass.get_local_index(), material_storage->material_get_shader_id(next_pass), p_mesh);
	}
}

// FellowEngine mesh clusters (docs/decisions/0007-mesh-clusters.md): a clustered surface's index buffers are put in
// cluster order the first time it is set up for drawing with materials that never blend it and never move its
// vertices; with a blending material (whose result depends on the triangle order inside a draw) or a vertex-moving
// one (the clusters would not be drawn) it keeps stock's order. Shadow meshes are only drawn in depth and shadow
// passes and are always put in cluster order.
void RenderForwardClustered::_fellow_decide_clusters(GeometryInstanceForwardClustered *ginstance, uint32_t p_surface, SceneShaderForwardClustered::MaterialData *p_material, RID p_mesh) {
	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	RendererRD::MaterialStorage *material_storage = RendererRD::MaterialStorage::get_singleton();
	void *surface = mesh_storage->mesh_get_surface(p_mesh, p_surface);
	if (surface == nullptr) {
		return;
	}
	const RendererRD::MeshStorage::FellowClusterState state = mesh_storage->fellow_mesh_surface_get_cluster_state(surface);
	RID shadow_mesh = mesh_storage->mesh_get_shadow_mesh(p_mesh);
	void *shadow_surface = shadow_mesh.is_valid() ? mesh_storage->mesh_get_surface(shadow_mesh, p_surface) : nullptr;
	if (shadow_surface && shadow_surface != surface && mesh_storage->fellow_mesh_surface_get_cluster_state(shadow_surface) == RendererRD::MeshStorage::FELLOW_CLUSTERS_PENDING) {
		mesh_storage->fellow_mesh_surface_decide_clusters(shadow_surface, true);
	}
	if (state == RendererRD::MeshStorage::FELLOW_CLUSTERS_NONE) {
		return;
	}
	bool blends = false;
	bool moves_vertices = false;
	auto check = [&](SceneShaderForwardClustered::MaterialData *p_mat) {
		while (p_mat && p_mat->shader_data && p_mat->shader_data->is_valid()) {
			const SceneShaderForwardClustered::ShaderData *sd = p_mat->shader_data;
			blends = blends || sd->uses_alpha_pass();
			moves_vertices = moves_vertices || sd->uses_vertex || sd->uses_position || sd->writes_modelview_or_projection || sd->uses_z_clip_scale || sd->uses_particle_trails;
			if (p_mat->next_pass.is_null()) {
				break;
			}
			p_mat = static_cast<SceneShaderForwardClustered::MaterialData *>(material_storage->material_get_data(p_mat->next_pass, RendererRD::MaterialStorage::SHADER_TYPE_3D));
		}
	};
	check(p_material);
	if (ginstance->data->material_overlay.is_valid()) {
		check(static_cast<SceneShaderForwardClustered::MaterialData *>(material_storage->material_get_data(ginstance->data->material_overlay, RendererRD::MaterialStorage::SHADER_TYPE_3D)));
	}
	if (state == RendererRD::MeshStorage::FELLOW_CLUSTERS_PENDING) {
		mesh_storage->fellow_mesh_surface_decide_clusters(surface, !blends && !moves_vertices);
	} else if (blends && !mesh_storage->fellow_mesh_surface_get_clusters(surface)->preserves_order) {
		Fellow::stats().geometry_cluster_blended_after_activation.increment();
		WARN_PRINT_ONCE("FellowEngine mesh clusters: a mesh surface already in cluster order is now drawn with a blending material; its triangles blend in cluster order, not in the mesh's order. Assign blending materials before the mesh is first drawn, or disable rendering/fellow/geometry/mesh_clusters.");
	}
}

void RenderForwardClustered::_geometry_instance_add_surface(GeometryInstanceForwardClustered *ginstance, uint32_t p_surface, RID p_material, RID p_mesh) {
	RendererRD::MaterialStorage *material_storage = RendererRD::MaterialStorage::get_singleton();
	RID m_src;

	m_src = ginstance->data->material_override.is_valid() ? ginstance->data->material_override : p_material;

	SceneShaderForwardClustered::MaterialData *material = nullptr;

	if (m_src.is_valid()) {
		material = static_cast<SceneShaderForwardClustered::MaterialData *>(material_storage->material_get_data(m_src, RendererRD::MaterialStorage::SHADER_TYPE_3D));
		if (!material || !material->shader_data->is_valid()) {
			material = nullptr;
		}
	}

	if (material) {
		if (ginstance->data->dirty_dependencies) {
			material_storage->material_update_dependency(m_src, &ginstance->data->dependency_tracker);
		}
	} else {
		material = static_cast<SceneShaderForwardClustered::MaterialData *>(material_storage->material_get_data(scene_shader.default_material, RendererRD::MaterialStorage::SHADER_TYPE_3D));
		m_src = scene_shader.default_material;
	}

	ERR_FAIL_NULL(material);

	_fellow_decide_clusters(ginstance, p_surface, material, p_mesh);

	_geometry_instance_add_surface_with_material_chain(ginstance, p_surface, material, m_src, p_mesh);

	if (ginstance->data->material_overlay.is_valid()) {
		m_src = ginstance->data->material_overlay;

		material = static_cast<SceneShaderForwardClustered::MaterialData *>(material_storage->material_get_data(m_src, RendererRD::MaterialStorage::SHADER_TYPE_3D));
		if (material && material->shader_data->is_valid()) {
			if (ginstance->data->dirty_dependencies) {
				material_storage->material_update_dependency(m_src, &ginstance->data->dependency_tracker);
			}

			_geometry_instance_add_surface_with_material_chain(ginstance, p_surface, material, m_src, p_mesh);
		}
	}
}

void RenderForwardClustered::_geometry_instance_update(RenderGeometryInstance *p_geometry_instance) {
	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	RendererRD::ParticlesStorage *particles_storage = RendererRD::ParticlesStorage::get_singleton();
	GeometryInstanceForwardClustered *ginstance = static_cast<GeometryInstanceForwardClustered *>(p_geometry_instance);

	if (ginstance->data->dirty_dependencies) {
		ginstance->data->dependency_tracker.update_begin();
	}

	//add geometry for drawing
	switch (ginstance->data->base_type) {
		case RSE::INSTANCE_MESH: {
			const RID *materials = nullptr;
			uint32_t surface_count;
			RID mesh = ginstance->data->base;

			materials = mesh_storage->mesh_get_surface_count_and_materials(mesh, surface_count);
			if (materials) {
				//if no materials, no surfaces.
				const RID *inst_materials = ginstance->data->surface_materials.ptr();
				uint32_t surf_mat_count = ginstance->data->surface_materials.size();

				for (uint32_t j = 0; j < surface_count; j++) {
					RID material = (j < surf_mat_count && inst_materials[j].is_valid()) ? inst_materials[j] : materials[j];
					_geometry_instance_add_surface(ginstance, j, material, mesh);
				}
			}

			ginstance->instance_count = 1;

		} break;

		case RSE::INSTANCE_MULTIMESH: {
			RID mesh = mesh_storage->multimesh_get_mesh(ginstance->data->base);
			if (mesh.is_valid()) {
				const RID *materials = nullptr;
				uint32_t surface_count;

				materials = mesh_storage->mesh_get_surface_count_and_materials(mesh, surface_count);
				if (materials) {
					for (uint32_t j = 0; j < surface_count; j++) {
						_geometry_instance_add_surface(ginstance, j, materials[j], mesh);
					}
				}

				ginstance->instance_count = mesh_storage->multimesh_get_instances_to_draw(ginstance->data->base);
			}

		} break;
#if 0
		case RSE::INSTANCE_IMMEDIATE: {
			RasterizerStorageGLES3::Immediate *immediate = storage->immediate_owner.get_or_null(inst->base);
			ERR_CONTINUE(!immediate);

			_add_geometry(immediate, inst, nullptr, -1, p_depth_pass, p_shadow_pass);

		} break;
#endif
		case RSE::INSTANCE_PARTICLES: {
			int draw_passes = particles_storage->particles_get_draw_passes(ginstance->data->base);

			for (int j = 0; j < draw_passes; j++) {
				RID mesh = particles_storage->particles_get_draw_pass_mesh(ginstance->data->base, j);
				if (!mesh.is_valid()) {
					continue;
				}

				const RID *materials = nullptr;
				uint32_t surface_count;

				materials = mesh_storage->mesh_get_surface_count_and_materials(mesh, surface_count);
				if (materials) {
					for (uint32_t k = 0; k < surface_count; k++) {
						_geometry_instance_add_surface(ginstance, k, materials[k], mesh);
					}
				}
			}

			ginstance->instance_count = particles_storage->particles_get_amount(ginstance->data->base, ginstance->trail_steps);

		} break;

		default: {
		}
	}

	//Fill push constant

	ginstance->base_flags = 0;

	bool store_transform = true;
	if (ginstance->data->base_type == RSE::INSTANCE_MULTIMESH) {
		ginstance->base_flags |= INSTANCE_DATA_FLAG_MULTIMESH;

		if (mesh_storage->multimesh_get_transform_format(ginstance->data->base) == RSE::MULTIMESH_TRANSFORM_2D) {
			ginstance->base_flags |= INSTANCE_DATA_FLAG_MULTIMESH_FORMAT_2D;
		}
		if (mesh_storage->multimesh_uses_colors(ginstance->data->base)) {
			ginstance->base_flags |= INSTANCE_DATA_FLAG_MULTIMESH_HAS_COLOR;
		}
		if (mesh_storage->multimesh_uses_custom_data(ginstance->data->base)) {
			ginstance->base_flags |= INSTANCE_DATA_FLAG_MULTIMESH_HAS_CUSTOM_DATA;
		}
		if (mesh_storage->multimesh_uses_indirect(ginstance->data->base)) {
			ginstance->base_flags |= INSTANCE_DATA_FLAG_MULTIMESH_INDIRECT;
		}

		ginstance->transforms_uniform_set = mesh_storage->multimesh_get_3d_uniform_set(ginstance->data->base, scene_shader.default_shader_rd, TRANSFORMS_UNIFORM_SET);

	} else if (ginstance->data->base_type == RSE::INSTANCE_PARTICLES) {
		ginstance->base_flags |= INSTANCE_DATA_FLAG_PARTICLES;
		ginstance->base_flags |= INSTANCE_DATA_FLAG_MULTIMESH;

		ginstance->base_flags |= INSTANCE_DATA_FLAG_MULTIMESH_HAS_COLOR;
		ginstance->base_flags |= INSTANCE_DATA_FLAG_MULTIMESH_HAS_CUSTOM_DATA;

		//for particles, stride is the trail size
		ginstance->base_flags |= (ginstance->trail_steps << INSTANCE_DATA_FLAGS_PARTICLE_TRAIL_SHIFT);

		if (!particles_storage->particles_is_using_local_coords(ginstance->data->base)) {
			store_transform = false;
		}
		ginstance->transforms_uniform_set = particles_storage->particles_get_instance_buffer_uniform_set(ginstance->data->base, scene_shader.default_shader_rd, TRANSFORMS_UNIFORM_SET);

		if (particles_storage->particles_get_frame_counter(ginstance->data->base) == 0) {
			// Particles haven't been cleared or updated, update once now to ensure they are ready to render.
			particles_storage->update_particles();
		}

		if (ginstance->data->dirty_dependencies) {
			particles_storage->particles_update_dependency(ginstance->data->base, &ginstance->data->dependency_tracker);
		}
	} else if (ginstance->data->base_type == RSE::INSTANCE_MESH) {
		if (mesh_storage->skeleton_is_valid(ginstance->data->skeleton)) {
			ginstance->transforms_uniform_set = mesh_storage->skeleton_get_3d_uniform_set(ginstance->data->skeleton, scene_shader.default_shader_rd, TRANSFORMS_UNIFORM_SET);
			if (ginstance->data->dirty_dependencies) {
				mesh_storage->skeleton_update_dependency(ginstance->data->skeleton, &ginstance->data->dependency_tracker);
			}
		} else {
			ginstance->transforms_uniform_set = RID();
		}
	}

	ginstance->store_transform_cache = store_transform;
	ginstance->fellow_data_version++;
	ginstance->can_sdfgi = false;

	if (!RendererRD::LightStorage::get_singleton()->lightmap_instance_is_valid(ginstance->lightmap_instance)) {
		if (ginstance->voxel_gi_instances[0].is_null() && (ginstance->data->use_baked_light || ginstance->data->use_dynamic_gi)) {
			ginstance->can_sdfgi = true;
		}
	}

	if (ginstance->data->dirty_dependencies) {
		ginstance->data->dependency_tracker.update_end();
		ginstance->data->dirty_dependencies = false;
	}

	ginstance->dirty_list_element.remove_from_list();
}

static RD::FramebufferFormatID _get_color_framebuffer_format_for_pipeline(RD::DataFormat p_color_format, bool p_can_be_storage, RD::TextureSamples p_samples, bool p_specular, bool p_velocity, uint32_t p_view_count) {
	const bool multisampling = p_samples > RD::TEXTURE_SAMPLES_1;
	RD::AttachmentFormat attachment;
	attachment.samples = p_samples;

	RD::AttachmentFormat unused_attachment;
	unused_attachment.usage_flags = RD::AttachmentFormat::UNUSED_ATTACHMENT;

	thread_local Vector<RD::AttachmentFormat> attachments;
	attachments.clear();

	// Color attachment.
	attachment.format = p_color_format;
	attachment.usage_flags = RenderSceneBuffersRD::get_color_usage_bits(false, multisampling, p_can_be_storage);
	attachments.push_back(attachment);

	if (p_specular) {
		attachment.format = RenderForwardClustered::RenderBufferDataForwardClustered::get_specular_format();
		attachment.usage_flags = RenderForwardClustered::RenderBufferDataForwardClustered::get_specular_usage_bits(false, multisampling, p_can_be_storage);
		attachments.push_back(attachment);
	} else {
		attachments.push_back(unused_attachment);
	}

	if (p_velocity) {
		attachment.format = RenderSceneBuffersRD::get_velocity_format();
		attachment.usage_flags = RenderSceneBuffersRD::get_velocity_usage_bits(false, multisampling, p_can_be_storage);
		attachments.push_back(attachment);
	} else {
		attachments.push_back(unused_attachment);
	}

	// Depth attachment.
	attachment.format = RenderSceneBuffersRD::get_depth_format(false, multisampling, p_can_be_storage);
	attachment.usage_flags = RenderSceneBuffersRD::get_depth_usage_bits(false, multisampling, p_can_be_storage);
	attachments.push_back(attachment);

	thread_local Vector<RD::FramebufferPass> passes;
	passes.resize(1);
	passes.ptrw()[0].color_attachments.resize(attachments.size() - 1);

	int *color_attachments = passes.ptrw()[0].color_attachments.ptrw();
	for (int64_t i = 0; i < attachments.size() - 1; i++) {
		color_attachments[i] = (attachments[i].usage_flags == RD::AttachmentFormat::UNUSED_ATTACHMENT) ? RD::ATTACHMENT_UNUSED : i;
	}

	passes.ptrw()[0].depth_attachment = attachments.size() - 1;

	return RD::get_singleton()->framebuffer_format_create_multipass(attachments, passes, p_view_count);
}

static RD::FramebufferFormatID _get_reflection_probe_color_framebuffer_format_for_pipeline(bool p_storage) {
	RD::AttachmentFormat attachment;
	thread_local Vector<RD::AttachmentFormat> attachments;
	attachments.clear();

	attachment.format = RendererRD::LightStorage::get_reflection_probe_color_format();
	attachment.usage_flags = RendererRD::LightStorage::get_reflection_probe_color_usage_bits(p_storage);
	attachments.push_back(attachment);

	attachment.format = RendererRD::LightStorage::get_reflection_probe_depth_format();
	attachment.usage_flags = RendererRD::LightStorage::get_reflection_probe_depth_usage_bits();
	attachments.push_back(attachment);

	return RD::get_singleton()->framebuffer_format_create(attachments);
}

static RD::FramebufferFormatID _get_depth_framebuffer_format_for_pipeline(bool p_can_be_storage, RD::TextureSamples p_samples, bool p_normal_roughness, bool p_voxelgi) {
	const bool multisampling = p_samples > RD::TEXTURE_SAMPLES_1;
	RD::AttachmentFormat attachment;
	attachment.samples = p_samples;

	thread_local LocalVector<RD::AttachmentFormat> attachments;
	attachments.clear();

	attachment.format = RenderSceneBuffersRD::get_depth_format(false, multisampling, p_can_be_storage);
	attachment.usage_flags = RenderSceneBuffersRD::get_depth_usage_bits(false, multisampling, p_can_be_storage);
	attachments.push_back(attachment);

	if (p_normal_roughness) {
		attachment.format = RenderForwardClustered::RenderBufferDataForwardClustered::get_normal_roughness_format();
		attachment.usage_flags = RenderForwardClustered::RenderBufferDataForwardClustered::get_normal_roughness_usage_bits(false, multisampling, p_can_be_storage);
		attachments.push_back(attachment);
	}

	if (p_voxelgi) {
		attachment.format = RenderForwardClustered::RenderBufferDataForwardClustered::get_voxelgi_format();
		attachment.usage_flags = RenderForwardClustered::RenderBufferDataForwardClustered::get_voxelgi_usage_bits(false, multisampling, p_can_be_storage);
		attachments.push_back(attachment);
	}

	thread_local Vector<RD::FramebufferPass> passes;
	passes.resize(1);
	passes.ptrw()[0].color_attachments.resize(attachments.size() - 1);

	int *color_attachments = passes.ptrw()[0].color_attachments.ptrw();
	for (int64_t i = 1; i < attachments.size(); i++) {
		color_attachments[i - 1] = (attachments[i].usage_flags == RD::AttachmentFormat::UNUSED_ATTACHMENT) ? RD::ATTACHMENT_UNUSED : i;
	}

	passes.ptrw()[0].depth_attachment = 0;

	return RD::get_singleton()->framebuffer_format_create_multipass(Vector<RD::AttachmentFormat>(attachments), passes);
}

static RD::FramebufferFormatID _get_shadow_cubemap_framebuffer_format_for_pipeline() {
	thread_local LocalVector<RD::AttachmentFormat> attachments;
	attachments.clear();

	RD::AttachmentFormat attachment;
	attachment.format = RendererRD::LightStorage::get_cubemap_depth_format();
	attachment.usage_flags = RendererRD::LightStorage::get_cubemap_depth_usage_bits();
	attachments.push_back(attachment);

	return RD::get_singleton()->framebuffer_format_create(Vector<RD::AttachmentFormat>(attachments));
}

static RD::FramebufferFormatID _get_shadow_atlas_framebuffer_format_for_pipeline(bool p_use_16_bits) {
	thread_local LocalVector<RD::AttachmentFormat> attachments;
	attachments.clear();

	RD::AttachmentFormat attachment;
	attachment.format = RendererRD::LightStorage::get_shadow_atlas_depth_format(p_use_16_bits);
	attachment.usage_flags = RendererRD::LightStorage::get_shadow_atlas_depth_usage_bits();
	attachments.push_back(attachment);

	return RD::get_singleton()->framebuffer_format_create(Vector<RD::AttachmentFormat>(attachments));
}

static RD::FramebufferFormatID _get_reflection_probe_depth_framebuffer_format_for_pipeline() {
	thread_local LocalVector<RD::AttachmentFormat> attachments;
	attachments.clear();

	RD::AttachmentFormat attachment;
	attachment.format = RendererRD::LightStorage::get_reflection_probe_depth_format();
	attachment.usage_flags = RendererRD::LightStorage::get_reflection_probe_depth_usage_bits();
	attachments.push_back(attachment);

	return RD::get_singleton()->framebuffer_format_create(Vector<RD::AttachmentFormat>(attachments));
}

void RenderForwardClustered::_mesh_compile_pipeline_for_surface(SceneShaderForwardClustered::ShaderData *p_shader, void *p_mesh_surface, bool p_ubershader, bool p_instanced_surface, RSE::PipelineSource p_source, SceneShaderForwardClustered::ShaderData::PipelineKey &r_pipeline_key, Vector<ShaderPipelinePair> *r_pipeline_pairs) {
	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	uint64_t input_mask = p_shader->get_vertex_input_mask(r_pipeline_key.version, r_pipeline_key.color_pass_flags, p_ubershader);
	bool pipeline_motion_vectors = r_pipeline_key.color_pass_flags & SceneShaderForwardClustered::PIPELINE_COLOR_PASS_FLAG_MOTION_VECTORS;
	bool emulate_point_size = p_shader->uses_point_size && scene_shader.emulate_point_size;
	r_pipeline_key.vertex_format_id = mesh_storage->mesh_surface_get_vertex_format(p_mesh_surface, input_mask, p_instanced_surface, pipeline_motion_vectors, emulate_point_size);
	r_pipeline_key.ubershader = p_ubershader;

	p_shader->pipeline_hash_map.compile_pipeline(r_pipeline_key, r_pipeline_key.hash(), p_source, p_ubershader);

	if (r_pipeline_pairs != nullptr) {
		r_pipeline_pairs->push_back({ p_shader, r_pipeline_key });
	}
}

void RenderForwardClustered::_mesh_compile_pipelines_for_surface(const SurfacePipelineData &p_surface, const GlobalPipelineData &p_global, RSE::PipelineSource p_source, Vector<ShaderPipelinePair> *r_pipeline_pairs) {
	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	bool octmap_use_storage = !copy_effects->get_raster_effects().has_flag(RendererRD::CopyEffects::RASTER_EFFECT_OCTMAP);

	// Retrieve from the scene shader which groups are currently enabled.
	const bool multiview_enabled = p_global.use_multiview && scene_shader.is_multiview_shader_group_enabled();
	const RD::DataFormat buffers_color_format = _render_buffers_get_preferred_color_format();
	const bool buffers_can_be_storage = _render_buffers_can_be_storage();

	// Set the attributes common to all pipelines.
	SceneShaderForwardClustered::ShaderData::PipelineKey pipeline_key;
	pipeline_key.cull_mode = RD::POLYGON_CULL_DISABLED;
	pipeline_key.primitive_type = mesh_storage->mesh_surface_get_primitive(p_surface.mesh_surface);
	pipeline_key.wireframe = false;

	// Grab the shader and surface used for most passes.
	const uint32_t multiview_iterations = multiview_enabled ? 2 : 1;
	const uint32_t lightmap_iterations = p_global.use_lightmaps && p_surface.can_use_lightmap ? 2 : 1;
	const uint32_t alpha_iterations = p_surface.uses_transparent ? 2 : 1;
	for (uint32_t multiview = 0; multiview < multiview_iterations; multiview++) {
		for (uint32_t lightmap = 0; lightmap < lightmap_iterations; lightmap++) {
			for (uint32_t alpha = p_surface.uses_opaque ? 0 : 1; alpha < alpha_iterations; alpha++) {
				// Generate all the possible variants used during the color pass.
				pipeline_key.version = SceneShaderForwardClustered::PIPELINE_VERSION_COLOR_PASS;
				pipeline_key.color_pass_flags = 0;

				if (lightmap) {
					pipeline_key.color_pass_flags |= SceneShaderForwardClustered::PIPELINE_COLOR_PASS_FLAG_LIGHTMAP;
				}

				if (alpha) {
					pipeline_key.color_pass_flags |= SceneShaderForwardClustered::PIPELINE_COLOR_PASS_FLAG_TRANSPARENT;
				}

				if (multiview) {
					pipeline_key.color_pass_flags |= SceneShaderForwardClustered::PIPELINE_COLOR_PASS_FLAG_MULTIVIEW;
				} else if (p_global.use_reflection_probes) {
					// Reflection probe can't be rendered in multiview.
					pipeline_key.framebuffer_format_id = _get_reflection_probe_color_framebuffer_format_for_pipeline(octmap_use_storage);
					_mesh_compile_pipeline_for_surface(p_surface.shader, p_surface.mesh_surface, true, p_surface.instanced, p_source, pipeline_key, r_pipeline_pairs);
				}

				// View count is assumed to be 2 as the configuration is dependent on the viewport. It's likely a safe assumption for stereo rendering.
				uint32_t view_count = multiview ? 2 : 1;
				pipeline_key.framebuffer_format_id = _get_color_framebuffer_format_for_pipeline(buffers_color_format, buffers_can_be_storage, RD::TextureSamples(p_global.texture_samples), false, false, view_count);
				_mesh_compile_pipeline_for_surface(p_surface.shader, p_surface.mesh_surface, true, p_surface.instanced, p_source, pipeline_key, r_pipeline_pairs);

				// Generate all the possible variants used during the advanced color passes.
				const uint32_t separate_specular_iterations = p_global.use_separate_specular ? 2 : 1;
				const uint32_t motion_vectors_iterations = p_global.use_motion_vectors ? 2 : 1;
				uint32_t base_color_pass_flags = pipeline_key.color_pass_flags;
				for (uint32_t separate_specular = 0; separate_specular < separate_specular_iterations; separate_specular++) {
					for (uint32_t motion_vectors = 0; motion_vectors < motion_vectors_iterations; motion_vectors++) {
						if (!separate_specular && !motion_vectors) {
							// This case was already generated.
							continue;
						}

						pipeline_key.color_pass_flags = base_color_pass_flags;

						if (separate_specular) {
							pipeline_key.color_pass_flags |= SceneShaderForwardClustered::PIPELINE_COLOR_PASS_FLAG_SEPARATE_SPECULAR;
						}

						if (motion_vectors) {
							pipeline_key.color_pass_flags |= SceneShaderForwardClustered::PIPELINE_COLOR_PASS_FLAG_MOTION_VECTORS;
						}

						pipeline_key.framebuffer_format_id = _get_color_framebuffer_format_for_pipeline(buffers_color_format, buffers_can_be_storage, RD::TextureSamples(p_global.texture_samples), separate_specular, motion_vectors, view_count);
						_mesh_compile_pipeline_for_surface(p_surface.shader, p_surface.mesh_surface, true, p_surface.instanced, p_source, pipeline_key, r_pipeline_pairs);
					}
				}
			}
		}
	}

	if (!p_surface.uses_depth) {
		return;
	}

	// Generate the depth pipelines if the material supports depth or it must be part of the shadow pass.
	pipeline_key.color_pass_flags = 0;

	if (p_global.use_normal_and_roughness) {
		// A lot of different effects rely on normal and roughness being written to during the depth pass.
		pipeline_key.version = SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS;
		pipeline_key.framebuffer_format_id = _get_depth_framebuffer_format_for_pipeline(buffers_can_be_storage, RD::TextureSamples(p_global.texture_samples), true, false);
		_mesh_compile_pipeline_for_surface(p_surface.shader, p_surface.mesh_surface, true, p_surface.instanced, p_source, pipeline_key, r_pipeline_pairs);
	}

	if (p_global.use_voxelgi) {
		// Depth pass with VoxelGI support.
		pipeline_key.version = SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS_WITH_NORMAL_AND_ROUGHNESS_AND_VOXEL_GI;
		pipeline_key.framebuffer_format_id = _get_depth_framebuffer_format_for_pipeline(buffers_can_be_storage, RD::TextureSamples(p_global.texture_samples), true, true);
		_mesh_compile_pipeline_for_surface(p_surface.shader, p_surface.mesh_surface, true, p_surface.instanced, p_source, pipeline_key, r_pipeline_pairs);
	}

	if (p_global.use_sdfgi) {
		// Depth pass with SDFGI support.
		pipeline_key.version = SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS_WITH_SDF;
		pipeline_key.framebuffer_format_id = _get_depth_framebuffer_format_for_pipeline(buffers_can_be_storage, RD::TextureSamples(p_global.texture_samples), false, false);
		_mesh_compile_pipeline_for_surface(p_surface.shader, p_surface.mesh_surface, true, p_surface.instanced, p_source, pipeline_key, r_pipeline_pairs);

		// Depth pass with SDFGI support for an empty framebuffer.
		pipeline_key.framebuffer_format_id = RD::get_singleton()->framebuffer_format_create_empty();
		_mesh_compile_pipeline_for_surface(p_surface.shader, p_surface.mesh_surface, true, p_surface.instanced, p_source, pipeline_key, r_pipeline_pairs);
	}

	// The dedicated depth passes use a different version of the surface and the shader.
	pipeline_key.primitive_type = mesh_storage->mesh_surface_get_primitive(p_surface.mesh_surface_shadow);
	pipeline_key.version = SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS;
	pipeline_key.framebuffer_format_id = _get_depth_framebuffer_format_for_pipeline(buffers_can_be_storage, RD::TextureSamples(p_global.texture_samples), false, false);
	_mesh_compile_pipeline_for_surface(p_surface.shader_shadow, p_surface.mesh_surface_shadow, true, p_surface.instanced, p_source, pipeline_key, r_pipeline_pairs);

	if (p_global.use_shadow_dual_paraboloid) {
		pipeline_key.version = SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS_DP;
		_mesh_compile_pipeline_for_surface(p_surface.shader_shadow, p_surface.mesh_surface_shadow, true, p_surface.instanced, p_source, pipeline_key, r_pipeline_pairs);
	}

	if (p_global.use_shadow_cubemaps) {
		pipeline_key.version = SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS;
		pipeline_key.framebuffer_format_id = _get_shadow_cubemap_framebuffer_format_for_pipeline();
		_mesh_compile_pipeline_for_surface(p_surface.shader_shadow, p_surface.mesh_surface_shadow, true, p_surface.instanced, p_source, pipeline_key, r_pipeline_pairs);
	}

	// Atlas shadowmaps (omni lights) can be in both 16-bit and 32-bit versions.
	const uint32_t use_16_bits_start = p_global.use_32_bit_shadows ? 0 : 1;
	const uint32_t use_16_bits_iterations = p_global.use_16_bit_shadows ? 2 : 1;
	for (uint32_t use_16_bits = use_16_bits_start; use_16_bits < use_16_bits_iterations; use_16_bits++) {
		pipeline_key.version = SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS;
		pipeline_key.framebuffer_format_id = _get_shadow_atlas_framebuffer_format_for_pipeline(use_16_bits);
		_mesh_compile_pipeline_for_surface(p_surface.shader_shadow, p_surface.mesh_surface_shadow, true, p_surface.instanced, p_source, pipeline_key, r_pipeline_pairs);

		if (p_global.use_shadow_dual_paraboloid) {
			pipeline_key.version = SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS_DP;
			_mesh_compile_pipeline_for_surface(p_surface.shader_shadow, p_surface.mesh_surface_shadow, true, p_surface.instanced, p_source, pipeline_key, r_pipeline_pairs);
		}
	}

	if (p_global.use_reflection_probes) {
		// Depth pass for reflection probes. Normally this will be redundant as the format is the exact same as the shadow cubemap.
		pipeline_key.version = SceneShaderForwardClustered::PIPELINE_VERSION_DEPTH_PASS;
		pipeline_key.framebuffer_format_id = _get_reflection_probe_depth_framebuffer_format_for_pipeline();
		_mesh_compile_pipeline_for_surface(p_surface.shader_shadow, p_surface.mesh_surface_shadow, true, p_surface.instanced, p_source, pipeline_key, r_pipeline_pairs);
	}
}

void RenderForwardClustered::_mesh_generate_all_pipelines_for_surface_cache(GeometryInstanceSurfaceDataCache *p_surface_cache, const GlobalPipelineData &p_global) {
	bool uses_alpha_pass = (p_surface_cache->flags & GeometryInstanceSurfaceDataCache::FLAG_PASS_ALPHA) != 0;
	float multiplied_fade_alpha = p_surface_cache->owner->force_alpha * p_surface_cache->owner->parent_fade_alpha;
	bool uses_fade = (multiplied_fade_alpha < FADE_ALPHA_PASS_THRESHOLD) || p_surface_cache->owner->fade_near || p_surface_cache->owner->fade_far;
	SurfacePipelineData surface;
	surface.mesh_surface = p_surface_cache->surface;
	surface.mesh_surface_shadow = p_surface_cache->surface_shadow;
	surface.shader = p_surface_cache->shader;
	surface.shader_shadow = p_surface_cache->shader_shadow;
	surface.instanced = p_surface_cache->owner->mesh_instance.is_valid();
	surface.uses_opaque = !uses_alpha_pass;
	surface.uses_transparent = uses_alpha_pass || uses_fade;
	surface.uses_depth = (p_surface_cache->flags & (GeometryInstanceSurfaceDataCache::FLAG_PASS_DEPTH | GeometryInstanceSurfaceDataCache::FLAG_PASS_OPAQUE | GeometryInstanceSurfaceDataCache::FLAG_PASS_SHADOW)) != 0;
	surface.can_use_lightmap = p_surface_cache->owner->lightmap_instance.is_valid() || p_surface_cache->owner->lightmap_sh;
	_mesh_compile_pipelines_for_surface(surface, p_global, RSE::PIPELINE_SOURCE_SURFACE);
}

void RenderForwardClustered::_update_dirty_geometry_instances() {
	while (geometry_instance_dirty_list.first()) {
		_geometry_instance_update(geometry_instance_dirty_list.first()->self());
	}

	_update_dirty_geometry_pipelines();
}

void RenderForwardClustered::_update_dirty_geometry_pipelines() {
	if (global_pipeline_data_required.key != global_pipeline_data_compiled.key) {
		// Go through the entire list of surfaces and compile pipelines for everything again.
		SelfList<GeometryInstanceSurfaceDataCache> *list = geometry_surface_compilation_all_list.first();
		while (list != nullptr) {
			GeometryInstanceSurfaceDataCache *surface_cache = list->self();
			_mesh_generate_all_pipelines_for_surface_cache(surface_cache, global_pipeline_data_required);

			if (surface_cache->compilation_dirty_element.in_list()) {
				// Remove any elements from the dirty list as they don't need to be processed again.
				geometry_surface_compilation_dirty_list.remove(&surface_cache->compilation_dirty_element);
			}

			list = list->next();
		}

		global_pipeline_data_compiled.key = global_pipeline_data_required.key;
	} else {
		// Compile pipelines only for the dirty list.
		if (!geometry_surface_compilation_dirty_list.first()) {
			return;
		}

		while (geometry_surface_compilation_dirty_list.first() != nullptr) {
			GeometryInstanceSurfaceDataCache *surface_cache = geometry_surface_compilation_dirty_list.first()->self();
			_mesh_generate_all_pipelines_for_surface_cache(surface_cache, global_pipeline_data_compiled);
			surface_cache->compilation_dirty_element.remove_from_list();
		}
	}
}

void RenderForwardClustered::_geometry_instance_dependency_changed(Dependency::DependencyChangedNotification p_notification, DependencyTracker *p_tracker) {
	switch (p_notification) {
		case Dependency::DEPENDENCY_CHANGED_MATERIAL:
		case Dependency::DEPENDENCY_CHANGED_MESH:
		case Dependency::DEPENDENCY_CHANGED_PARTICLES:
		case Dependency::DEPENDENCY_CHANGED_PARTICLES_INSTANCES:
		case Dependency::DEPENDENCY_CHANGED_MULTIMESH:
		case Dependency::DEPENDENCY_CHANGED_SKELETON_DATA: {
			static_cast<RenderGeometryInstance *>(p_tracker->userdata)->_mark_dirty();
			static_cast<GeometryInstanceForwardClustered *>(p_tracker->userdata)->data->dirty_dependencies = true;
		} break;
		case Dependency::DEPENDENCY_CHANGED_MULTIMESH_VISIBLE_INSTANCES: {
			GeometryInstanceForwardClustered *ginstance = static_cast<GeometryInstanceForwardClustered *>(p_tracker->userdata);
			if (ginstance->data->base_type == RSE::INSTANCE_MULTIMESH) {
				ginstance->instance_count = RendererRD::MeshStorage::get_singleton()->multimesh_get_instances_to_draw(ginstance->data->base);
			}
		} break;
		default: {
			//rest of notifications of no interest
		} break;
	}
}
void RenderForwardClustered::_geometry_instance_dependency_deleted(const RID &p_dependency, DependencyTracker *p_tracker) {
	static_cast<RenderGeometryInstance *>(p_tracker->userdata)->_mark_dirty();
	static_cast<GeometryInstanceForwardClustered *>(p_tracker->userdata)->data->dirty_dependencies = true;
}

RenderGeometryInstance *RenderForwardClustered::geometry_instance_create(RID p_base) {
	RSE::InstanceType type = RSG::utilities->get_base_type(p_base);
	ERR_FAIL_COND_V(!((1 << type) & RSE::INSTANCE_GEOMETRY_MASK), nullptr);

	GeometryInstanceForwardClustered *ginstance = geometry_instance_alloc.alloc();
	ginstance->data = memnew(GeometryInstanceForwardClustered::Data);

	ginstance->data->base = p_base;
	ginstance->data->base_type = type;
	ginstance->data->dependency_tracker.userdata = ginstance;
	ginstance->data->dependency_tracker.changed_callback = _geometry_instance_dependency_changed;
	ginstance->data->dependency_tracker.deleted_callback = _geometry_instance_dependency_deleted;

	ginstance->_mark_dirty();

	return ginstance;
}

void RenderForwardClustered::GeometryInstanceForwardClustered::set_transform(const Transform3D &p_transform, const AABB &p_aabb, const AABB &p_transformed_aabb) {
	uint64_t frame = RSG::rasterizer->get_frame_number();
	if (frame != prev_transform_change_frame) {
		prev_transform = transform;
		prev_transform_change_frame = frame;
		transform_status = TransformStatus::MOVED;
	} else if (unlikely(transform_status == TransformStatus::TELEPORTED)) {
		prev_transform = transform;
	}

	RenderGeometryInstanceBase::set_transform(p_transform, p_aabb, p_transformed_aabb);
}

void RenderForwardClustered::GeometryInstanceForwardClustered::reset_motion_vectors() {
	prev_transform = transform;
	fellow_data_version++;
	transform_status = TransformStatus::TELEPORTED;
}

void RenderForwardClustered::GeometryInstanceForwardClustered::set_use_lightmap(RID p_lightmap_instance, const Rect2 &p_lightmap_uv_scale, int p_lightmap_slice_index) {
	lightmap_instance = p_lightmap_instance;
	lightmap_uv_scale = p_lightmap_uv_scale;
	lightmap_slice_index = p_lightmap_slice_index;
	fellow_data_version++;

	_mark_dirty();
}

void RenderForwardClustered::GeometryInstanceForwardClustered::set_lightmap_capture(const Color *p_sh9) {
	if (p_sh9) {
		if (lightmap_sh == nullptr) {
			lightmap_sh = RenderForwardClustered::get_singleton()->geometry_instance_lightmap_sh.alloc();
		}

		memcpy(lightmap_sh->sh, p_sh9, sizeof(Color) * 9);
	} else {
		if (lightmap_sh != nullptr) {
			RenderForwardClustered::get_singleton()->geometry_instance_lightmap_sh.free(lightmap_sh);
			lightmap_sh = nullptr;
		}
	}
	_mark_dirty();
}

void RenderForwardClustered::geometry_instance_free(RenderGeometryInstance *p_geometry_instance) {
	GeometryInstanceForwardClustered *ginstance = static_cast<GeometryInstanceForwardClustered *>(p_geometry_instance);
	ERR_FAIL_NULL(ginstance);
	if (ginstance->lightmap_sh != nullptr) {
		geometry_instance_lightmap_sh.free(ginstance->lightmap_sh);
	}
	GeometryInstanceSurfaceDataCache *surf = ginstance->surface_caches;
	while (surf) {
		GeometryInstanceSurfaceDataCache *next = surf->next;
		_fellow_slot_free(surf->fellow_slot);
		geometry_instance_surface_alloc.free(surf);
		surf = next;
	}
	memdelete(ginstance->data);
	geometry_instance_alloc.free(ginstance);
}

uint32_t RenderForwardClustered::geometry_instance_get_pair_mask() {
	return (1 << RSE::INSTANCE_VOXEL_GI);
}

void RenderForwardClustered::mesh_generate_pipelines(RID p_mesh, bool p_background_compilation) {
	RendererRD::MaterialStorage *material_storage = RendererRD::MaterialStorage::get_singleton();
	RendererRD::MeshStorage *mesh_storage = RendererRD::MeshStorage::get_singleton();
	RID shadow_mesh = mesh_storage->mesh_get_shadow_mesh(p_mesh);
	uint32_t surface_count = 0;
	const RID *materials = mesh_storage->mesh_get_surface_count_and_materials(p_mesh, surface_count);
	Vector<ShaderPipelinePair> pipeline_pairs;
	for (uint32_t i = 0; i < surface_count; i++) {
		if (materials[i].is_null()) {
			continue;
		}

		void *mesh_surface = mesh_storage->mesh_get_surface(p_mesh, i);
		void *mesh_surface_shadow = mesh_surface;
		SceneShaderForwardClustered::MaterialData *material = static_cast<SceneShaderForwardClustered::MaterialData *>(material_storage->material_get_data(materials[i], RendererRD::MaterialStorage::SHADER_TYPE_3D));
		if (material == nullptr || !material->shader_data->is_valid()) {
			continue;
		}

		SceneShaderForwardClustered::ShaderData *shader = material->shader_data;
		SceneShaderForwardClustered::ShaderData *shader_shadow = shader;
		if (material->shader_data->uses_shared_shadow_material()) {
			SceneShaderForwardClustered::MaterialData *material_shadow = static_cast<SceneShaderForwardClustered::MaterialData *>(material_storage->material_get_data(scene_shader.default_material, RendererRD::MaterialStorage::SHADER_TYPE_3D));
			if (material_shadow != nullptr) {
				shader_shadow = material_shadow->shader_data;
				if (shadow_mesh.is_valid()) {
					mesh_surface_shadow = mesh_storage->mesh_get_surface(shadow_mesh, i);
				}
			}
		}

		if (!shader->is_valid()) {
			continue;
		}

		SurfacePipelineData surface;
		surface.mesh_surface = mesh_surface;
		surface.mesh_surface_shadow = mesh_surface_shadow;
		surface.shader = shader;
		surface.shader_shadow = shader_shadow;
		surface.instanced = mesh_storage->mesh_needs_instance(p_mesh, true);
		surface.uses_opaque = !material->shader_data->uses_alpha_pass();
		surface.uses_transparent = material->shader_data->uses_alpha_pass();
		surface.uses_depth = surface.uses_opaque || (surface.uses_transparent && material->shader_data->uses_depth_in_alpha_pass());
		surface.can_use_lightmap = mesh_storage->mesh_surface_get_format(mesh_surface) & RSE::ARRAY_FORMAT_TEX_UV2;
		_mesh_compile_pipelines_for_surface(surface, global_pipeline_data_required, RSE::PIPELINE_SOURCE_MESH, &pipeline_pairs);
	}

	// Wait for all the pipelines that were compiled. This will force the loader to wait on all ubershader pipelines to be ready.
	if (!p_background_compilation && !pipeline_pairs.is_empty()) {
		for (ShaderPipelinePair pair : pipeline_pairs) {
			pair.first->pipeline_hash_map.wait_for_pipeline(pair.second.hash());
		}
	}
}

uint32_t RenderForwardClustered::get_pipeline_compilations(RSE::PipelineSource p_source) {
	return scene_shader.get_pipeline_compilations(p_source);
}

void RenderForwardClustered::enable_features(BitField<FeatureBits> p_feature_bits) {
	if (p_feature_bits.has_flag(FEATURE_MULTIVIEW_BIT)) {
		scene_shader.enable_multiview_shader_group();
	}

	if (p_feature_bits.has_flag(FEATURE_ADVANCED_BIT)) {
		scene_shader.enable_advanced_shader_group(p_feature_bits.has_flag(FEATURE_MULTIVIEW_BIT));
	}

	if (p_feature_bits.has_flag(FEATURE_VRS_BIT)) {
		gi.enable_vrs_shader_group();
	}
}

String RenderForwardClustered::get_name() const {
	return "forward_clustered";
}

void RenderForwardClustered::GeometryInstanceForwardClustered::pair_voxel_gi_instances(const RID *p_voxel_gi_instances, uint32_t p_voxel_gi_instance_count) {
	if (p_voxel_gi_instance_count > 0) {
		voxel_gi_instances[0] = p_voxel_gi_instances[0];
	} else {
		voxel_gi_instances[0] = RID();
	}

	if (p_voxel_gi_instance_count > 1) {
		voxel_gi_instances[1] = p_voxel_gi_instances[1];
	} else {
		voxel_gi_instances[1] = RID();
	}
}

void RenderForwardClustered::GeometryInstanceForwardClustered::set_softshadow_projector_pairing(bool p_softshadow, bool p_projector) {
	using_projectors = p_projector;
	using_softshadows = p_softshadow;
	_mark_dirty();
}

void RenderForwardClustered::_update_shader_quality_settings() {
	SceneShaderForwardClustered::ShaderSpecialization specialization = {};
	specialization.decal_use_mipmaps = decals_get_filter() == RSE::DECAL_FILTER_NEAREST_MIPMAPS ||
			decals_get_filter() == RSE::DECAL_FILTER_LINEAR_MIPMAPS ||
			decals_get_filter() == RSE::DECAL_FILTER_NEAREST_MIPMAPS_ANISOTROPIC ||
			decals_get_filter() == RSE::DECAL_FILTER_LINEAR_MIPMAPS_ANISOTROPIC;
	;
	specialization.projector_use_mipmaps = light_projectors_get_filter() == RSE::LIGHT_PROJECTOR_FILTER_NEAREST_MIPMAPS ||
			light_projectors_get_filter() == RSE::LIGHT_PROJECTOR_FILTER_LINEAR_MIPMAPS ||
			light_projectors_get_filter() == RSE::LIGHT_PROJECTOR_FILTER_NEAREST_MIPMAPS_ANISOTROPIC ||
			light_projectors_get_filter() == RSE::LIGHT_PROJECTOR_FILTER_LINEAR_MIPMAPS_ANISOTROPIC;

	specialization.soft_shadow_samples = soft_shadow_samples_get();
	specialization.penumbra_shadow_samples = penumbra_shadow_samples_get();
	specialization.directional_soft_shadow_samples = directional_soft_shadow_samples_get();
	specialization.directional_penumbra_shadow_samples = directional_penumbra_shadow_samples_get();
	specialization.use_lightmap_bicubic_filter = lightmap_filter_bicubic_get();
	specialization.fog_use_legacy_blending = fog_use_legacy_blending_get();
	specialization.fellow_pcss_blend_band = Fellow::shadow_pcss_blend_band();
	specialization.fellow_directional_max_mip = Fellow::shadow_directional_max_mip();
	specialization.fellow_positional_max_mip = Fellow::shadow_positional_max_mip();
	specialization.fellow_zero_attenuation_skip = Fellow::light_zero_attenuation_skip();
	specialization.fellow_skip_backfacing_positional = Fellow::shadow_skip_backfacing_positional();
	specialization.fellow_skip_backfacing_directional = Fellow::shadow_skip_backfacing();
	specialization.fellow_skip_unblended_cascade = Fellow::shadow_skip_unblended_cascade();
	specialization.fellow_cluster_log_depth = Fellow::light_cluster_depth_slices();
	specialization.fellow_blended_alpha_zero_skip = Fellow::blended_alpha_zero_skip();
	scene_shader.set_default_specialization(specialization);

	base_uniforms_changed(); //also need this
}

RenderForwardClustered::RenderForwardClustered() {
	fellow_parallel = FellowParallelFor::shared_acquire();
	singleton = this;
	RendererRD::MeshStorage::get_singleton()->fellow_clusters_supported = true; // FellowEngine mesh clusters (decision 0007).

	/* SCENE SHADER */

	{
		String defines;
		defines += "\n#define MAX_ROUGHNESS_LOD " + itos(get_roughness_layers() - 1) + ".0\n";
		if (is_using_radiance_octmap_array()) {
			defines += "\n#define USE_RADIANCE_OCTMAP_ARRAY \n";
		}
		defines += "\n#define SDFGI_OCT_SIZE " + itos(gi.sdfgi_get_lightprobe_octahedron_size()) + "\n";
		defines += "\n#define MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS " + itos(MAX_DIRECTIONAL_LIGHTS) + "\n";

		bool force_vertex_shading = GLOBAL_GET("rendering/shading/overrides/force_vertex_shading");
		if (force_vertex_shading) {
			defines += "\n#define USE_VERTEX_LIGHTING\n";
		}

		bool specular_occlusion = GLOBAL_GET("rendering/reflections/specular_occlusion/enabled");
		if (!specular_occlusion) {
			defines += "\n#define SPECULAR_OCCLUSION_DISABLED\n";
		}

		{
			//lightmaps
			scene_state.max_lightmaps = MAX_LIGHTMAPS;
			defines += "\n#define MAX_LIGHTMAP_TEXTURES " + itos(scene_state.max_lightmaps) + "\n";
			defines += "\n#define MAX_LIGHTMAPS " + itos(scene_state.max_lightmaps) + "\n";

			scene_state.lightmap_buffer = RD::get_singleton()->storage_buffer_create(sizeof(LightmapData) * scene_state.max_lightmaps);
		}
		{
			//captures
			scene_state.max_lightmap_captures = 2048;
			scene_state.lightmap_captures = memnew_arr(LightmapCaptureData, scene_state.max_lightmap_captures);
			scene_state.lightmap_capture_buffer = RD::get_singleton()->storage_buffer_create(sizeof(LightmapCaptureData) * scene_state.max_lightmap_captures);
		}
		{
			defines += "\n#define MATERIAL_UNIFORM_SET " + itos(MATERIAL_UNIFORM_SET) + "\n";
		}
#ifdef REAL_T_IS_DOUBLE
		{
			defines += "\n#define USE_DOUBLE_PRECISION \n";
		}
#endif

		scene_shader.init(defines);
	}

	/* shadow sampler */
	{
		RD::SamplerState sampler;
		sampler.mag_filter = RD::SAMPLER_FILTER_NEAREST;
		sampler.min_filter = RD::SAMPLER_FILTER_NEAREST;
		sampler.enable_compare = true;
		sampler.compare_op = RD::COMPARE_OP_GREATER;
		shadow_sampler = RD::get_singleton()->sampler_create(sampler);
	}

	{
		Vector<String> modes;
		modes.push_back("\n");
		best_fit_normal.shader.initialize(modes);
		best_fit_normal.shader_version = best_fit_normal.shader.version_create();
		best_fit_normal.pipeline = RD::get_singleton()->compute_pipeline_create(best_fit_normal.shader.version_get_shader(best_fit_normal.shader_version, 0));

		RD::TextureFormat tformat;
		tformat.format = RD::DATA_FORMAT_R8_UNORM;
		tformat.width = 1024;
		tformat.height = 1024;
		tformat.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		tformat.texture_type = RD::TEXTURE_TYPE_2D;
		best_fit_normal.texture = RD::get_singleton()->texture_create(tformat, RD::TextureView());

		RID shader = best_fit_normal.shader.version_get_shader(best_fit_normal.shader_version, 0);
		ERR_FAIL_COND(shader.is_null());

		Vector<RD::Uniform> uniforms;

		{
			RD::Uniform u;
			u.binding = 0;
			u.uniform_type = RD::UNIFORM_TYPE_IMAGE;
			u.append_id(best_fit_normal.texture);
			uniforms.push_back(u);
		}
		RID uniform_set = RD::get_singleton()->uniform_set_create(uniforms, shader, 0);

		RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, best_fit_normal.pipeline);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
		RD::get_singleton()->compute_list_dispatch_threads(compute_list, tformat.width, tformat.height, 1);
		RD::get_singleton()->compute_list_end();
	}

	/* DFG LUT */
	{
		Vector<String> modes;
		modes.push_back("\n");
		dfg_lut.shader.initialize(modes);
		dfg_lut.shader_version = dfg_lut.shader.version_create();
		dfg_lut.pipeline = RD::get_singleton()->compute_pipeline_create(dfg_lut.shader.version_get_shader(dfg_lut.shader_version, 0));

		RD::TextureFormat tformat;
		tformat.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
		tformat.width = 128;
		tformat.height = 128;
		tformat.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		tformat.texture_type = RD::TEXTURE_TYPE_2D;
		dfg_lut.texture = RD::get_singleton()->texture_create(tformat, RD::TextureView());

		RID shader = dfg_lut.shader.version_get_shader(dfg_lut.shader_version, 0);
		ERR_FAIL_COND(shader.is_null());

		Vector<RD::Uniform> uniforms;

		{
			RD::Uniform u;
			u.binding = 0;
			u.uniform_type = RD::UNIFORM_TYPE_IMAGE;
			u.append_id(dfg_lut.texture);
			uniforms.push_back(u);
		}
		RID uniform_set = RD::get_singleton()->uniform_set_create(uniforms, shader, 0);

		RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, dfg_lut.pipeline);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
		RD::get_singleton()->compute_list_dispatch_threads(compute_list, tformat.width, tformat.height, 1);
		RD::get_singleton()->compute_list_end();
	}

	_update_shader_quality_settings();
	_update_global_pipeline_data_requirements_from_project();

	taa = memnew(RendererRD::TAA);
	fsr2_effect = memnew(RendererRD::FSR2Effect);
	ss_effects = memnew(RendererRD::SSEffects);
	fellow_shadow_cache = memnew(RendererRD::FellowShadowCache);
	fellow_shadow_max_mip = memnew(RendererRD::FellowShadowMaxMip);
#ifdef METAL_MFXTEMPORAL_ENABLED
	motion_vectors_store = memnew(RendererRD::MotionVectorsStore);
	mfx_temporal_effect = memnew(RendererRD::MFXTemporalEffect);
#endif
}

RenderForwardClustered::~RenderForwardClustered() {
	FellowParallelFor::shared_release();
	fellow_parallel = nullptr;
	if (ss_effects != nullptr) {
		memdelete(ss_effects);
		ss_effects = nullptr;
	}

	if (fellow_shadow_cache != nullptr) {
		memdelete(fellow_shadow_cache);
		fellow_shadow_cache = nullptr;
	}

	if (fellow_shadow_max_mip != nullptr) {
		memdelete(fellow_shadow_max_mip);
		fellow_shadow_max_mip = nullptr;
	}

	if (taa != nullptr) {
		memdelete(taa);
		taa = nullptr;
	}

	if (fsr2_effect) {
		memdelete(fsr2_effect);
		fsr2_effect = nullptr;
	}

#ifdef METAL_MFXTEMPORAL_ENABLED
	if (mfx_temporal_effect) {
		memdelete(mfx_temporal_effect);
		mfx_temporal_effect = nullptr;
	}

#endif
	// Also created on demand by FellowEngine's TAA camera motion vectors (decision 0013), on every platform.
	if (motion_vectors_store) {
		memdelete(motion_vectors_store);
		motion_vectors_store = nullptr;
	}

	RD::get_singleton()->free_rid(shadow_sampler);
	RSG::light_storage->directional_shadow_atlas_set_size(0);

	RD::get_singleton()->free_rid(best_fit_normal.pipeline);
	RD::get_singleton()->free_rid(best_fit_normal.texture);
	best_fit_normal.shader.version_free(best_fit_normal.shader_version);

	RD::get_singleton()->free_rid(dfg_lut.pipeline);
	RD::get_singleton()->free_rid(dfg_lut.texture);
	dfg_lut.shader.version_free(dfg_lut.shader_version);

	if (ltc.lut1_texture.is_valid()) {
		RS::get_singleton()->free_rid(ltc.lut1_texture);
	}
	if (ltc.lut2_texture.is_valid()) {
		RS::get_singleton()->free_rid(ltc.lut2_texture);
	}

	{
		for (const RID &rid : scene_state.uniform_buffers) {
			RD::get_singleton()->free_rid(rid);
		}
		for (const RID &rid : scene_state.implementation_uniform_buffers) {
			RD::get_singleton()->free_rid(rid);
		}
		RD::get_singleton()->free_rid(scene_state.lightmap_buffer);
		RD::get_singleton()->free_rid(scene_state.lightmap_capture_buffer);
		for (uint32_t i = 0; i < RENDER_LIST_MAX; i++) {
			scene_state.instance_buffer[i].uninit();
		}
		_fellow_persistent_free();
		_fellow_occlusion_free();
		memdelete_arr(scene_state.lightmap_captures);
	}

	while (sdfgi_framebuffer_size_cache.begin()) {
		RD::get_singleton()->free_rid(sdfgi_framebuffer_size_cache.begin()->value);
		sdfgi_framebuffer_size_cache.remove(sdfgi_framebuffer_size_cache.begin());
	}
}
