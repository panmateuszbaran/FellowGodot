/**************************************************************************/
/*  render_forward_clustered.h                                            */
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

#include "core/templates/paged_allocator.h"
#include "servers/rendering/fellow_parallel.h"
#include "servers/rendering/fellow_sort.h"
#include "servers/rendering/multi_uma_buffer.h"
#include "servers/rendering/renderer_rd/cluster_builder_rd.h"
#include "servers/rendering/renderer_rd/effects/fellow_shadow_cache.h"
#include "servers/rendering/renderer_rd/effects/fellow_shadow_max_mip.h"
#include "servers/rendering/renderer_rd/effects/fsr2.h"
#include "servers/rendering/renderer_rd/effects/motion_vectors_store.h"
#include "servers/rendering/renderer_rd/effects/ss_effects.h"
#include "servers/rendering/renderer_rd/effects/taa.h"
#include "servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.h"
#include "servers/rendering/renderer_rd/renderer_scene_render_rd.h"
#include "servers/rendering/renderer_rd/shaders/effects/fellow_adaptive_vrs.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fellow_hzb.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fellow_occlusion_cull.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fellow_occlusion_test.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/forward_clustered/best_fit_normal.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/forward_clustered/integrate_dfg.glsl.gen.h"

#ifdef METAL_ENABLED
#include "servers/rendering/renderer_rd/effects/metal_fx.h"
#endif

#define RB_SCOPE_FORWARD_CLUSTERED SNAME("forward_clustered")

#define RB_TEX_SPECULAR SNAME("specular")
#define RB_TEX_SPECULAR_MSAA SNAME("specular_msaa")
#define RB_TEX_NORMAL_ROUGHNESS SNAME("normal_roughness")
#define RB_TEX_NORMAL_ROUGHNESS_MSAA SNAME("normal_roughness_msaa")
#define RB_TEX_VOXEL_GI SNAME("voxel_gi")
#define RB_TEX_VOXEL_GI_MSAA SNAME("voxel_gi_msaa")

namespace RendererSceneRenderImplementation {

class RenderForwardClustered : public RendererSceneRenderRD {
	friend SceneShaderForwardClustered;

	enum {
		SCENE_UNIFORM_SET = 0,
		RENDER_PASS_UNIFORM_SET = 1,
		TRANSFORMS_UNIFORM_SET = 2,
		MATERIAL_UNIFORM_SET = 3,
	};

	enum {
		SDFGI_MAX_CASCADES = 8,
		MAX_VOXEL_GI_INSTANCESS = 8,
		MAX_LIGHTMAPS = 8,
		MAX_VOXEL_GI_INSTANCESS_PER_INSTANCE = 2,
		INSTANCE_DATA_BUFFER_MIN_SIZE = 4096
	};

	enum RenderListType {
		RENDER_LIST_OPAQUE, //used for opaque objects
		RENDER_LIST_MOTION, //used for opaque objects with motion
		RENDER_LIST_ALPHA, //used for transparent objects
		RENDER_LIST_SECONDARY, //used for shadows and other objects
		RENDER_LIST_MAX
	};

	/* Scene Shader */

	SceneShaderForwardClustered scene_shader;

public:
	/* Framebuffer */

	class RenderBufferDataForwardClustered : public RenderBufferCustomDataRD {
		GDCLASS(RenderBufferDataForwardClustered, RenderBufferCustomDataRD)

	private:
		RenderSceneBuffersRD *render_buffers = nullptr;
		RendererRD::FSR2Context *fsr2_context = nullptr;
#ifdef METAL_MFXTEMPORAL_ENABLED
		RendererRD::MFXTemporalContext *mfx_temporal_context = nullptr;
#endif

	public:
		ClusterBuilderRD *cluster_builder = nullptr;

		struct SSEffectsData {
			Projection ssil_last_frame_projections[RendererSceneRender::MAX_RENDER_VIEWS];
			Transform3D ssil_last_frame_transform;

			Projection ssr_last_frame_projections[RendererSceneRender::MAX_RENDER_VIEWS];
			Transform3D ssr_last_frame_transform;

			RendererRD::SSEffects::SSILRenderBuffers ssil;
			RendererRD::SSEffects::SSAORenderBuffers ssao;
			RendererRD::SSEffects::SSRRenderBuffers ssr;
		} ss_effects_data;

		enum DepthFrameBufferType {
			DEPTH_FB,
			DEPTH_FB_ROUGHNESS,
			DEPTH_FB_ROUGHNESS_VOXELGI
		};

		RID render_sdfgi_uniform_set;

		void ensure_specular();
		bool has_specular() const { return render_buffers->has_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_SPECULAR); }
		RID get_specular() const { return render_buffers->get_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_SPECULAR); }
		RID get_specular(uint32_t p_layer) { return render_buffers->get_texture_slice(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_SPECULAR, p_layer, 0); }
		RID get_specular_msaa(uint32_t p_layer) { return render_buffers->get_texture_slice(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_SPECULAR_MSAA, p_layer, 0); }

		void ensure_normal_roughness_texture();
		bool has_normal_roughness() const { return render_buffers->has_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS); }
		RID get_normal_roughness() const { return render_buffers->get_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS); }
		RID get_normal_roughness(uint32_t p_layer) { return render_buffers->get_texture_slice(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS, p_layer, 0); }
		RID get_normal_roughness_msaa() const { return render_buffers->get_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS_MSAA); }
		RID get_normal_roughness_msaa(uint32_t p_layer) { return render_buffers->get_texture_slice(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_NORMAL_ROUGHNESS_MSAA, p_layer, 0); }

		void ensure_voxelgi();
		bool has_voxelgi() const { return render_buffers->has_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_VOXEL_GI); }
		RID get_voxelgi() const { return render_buffers->get_texture(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_VOXEL_GI); }
		RID get_voxelgi(uint32_t p_layer) { return render_buffers->get_texture_slice(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_VOXEL_GI, p_layer, 0); }
		RID get_voxelgi_msaa(uint32_t p_layer) { return render_buffers->get_texture_slice(RB_SCOPE_FORWARD_CLUSTERED, RB_TEX_VOXEL_GI_MSAA, p_layer, 0); }

		void ensure_fsr2(RendererRD::FSR2Effect *p_effect);
		RendererRD::FSR2Context *get_fsr2_context() const { return fsr2_context; }

#ifdef METAL_MFXTEMPORAL_ENABLED
		bool ensure_mfx_temporal(RendererRD::MFXTemporalEffect *p_effect);
		RendererRD::MFXTemporalContext *get_mfx_temporal_context() const { return mfx_temporal_context; }
#endif

		RID get_color_only_fb();
		RID get_color_pass_fb(uint32_t p_color_pass_flags);
		RID get_depth_fb(DepthFrameBufferType p_type = DEPTH_FB);
		RID get_specular_only_fb();
		RID get_velocity_only_fb();

		virtual void configure(RenderSceneBuffersRD *p_render_buffers) override;
		virtual void free_data() override;

		static RD::DataFormat get_specular_format();
		static uint32_t get_specular_usage_bits(bool p_resolve, bool p_msaa, bool p_storage);
		static RD::DataFormat get_normal_roughness_format();
		static uint32_t get_normal_roughness_usage_bits(bool p_resolve, bool p_msaa, bool p_storage);
		static RD::DataFormat get_voxelgi_format();
		static uint32_t get_voxelgi_usage_bits(bool p_resolve, bool p_msaa, bool p_storage);
	};

private:
	virtual void setup_render_buffer_data(Ref<RenderSceneBuffersRD> p_render_buffers) override;

	RID render_base_uniform_set;

	uint64_t lightmap_texture_array_version = 0xFFFFFFFF;

	void _update_render_base_uniform_set();
	RID _setup_sdfgi_render_pass_uniform_set(RID p_albedo_texture, RID p_emission_texture, RID p_emission_aniso_texture, RID p_geom_facing_texture, const RendererRD::MaterialStorage::Samplers &p_samplers, uint32_t p_uniform_buffer_index);
	RID _setup_render_pass_uniform_set(RenderListType p_render_list, const RenderDataRD *p_render_data, RID p_radiance_texture, const RendererRD::MaterialStorage::Samplers &p_samplers, uint32_t p_uniform_buffer_index, bool p_use_directional_shadow_atlas = false);

	struct BestFitNormal {
		BestFitNormalShaderRD shader;
		RID shader_version;
		RID pipeline;
		RID texture;
	} best_fit_normal;

	struct IntegrateDFG {
		IntegrateDfgShaderRD shader;
		RID shader_version;
		RID pipeline;
		RID texture;
	} dfg_lut;

	struct LTC {
		RID lut1_texture;
		RID lut2_texture;
	} ltc;

	enum PassMode {
		PASS_MODE_COLOR,
		PASS_MODE_SHADOW,
		PASS_MODE_SHADOW_DP,
		PASS_MODE_DEPTH,
		PASS_MODE_DEPTH_NORMAL_ROUGHNESS,
		PASS_MODE_DEPTH_NORMAL_ROUGHNESS_VOXEL_GI,
		PASS_MODE_DEPTH_MATERIAL,
		PASS_MODE_SDF,
		PASS_MODE_MAX
	};

	enum ColorPassFlags {
		COLOR_PASS_FLAG_TRANSPARENT = 1 << 0,
		COLOR_PASS_FLAG_SEPARATE_SPECULAR = 1 << 1,
		COLOR_PASS_FLAG_MULTIVIEW = 1 << 2,
		COLOR_PASS_FLAG_MOTION_VECTORS = 1 << 3,
	};

	struct GeometryInstanceSurfaceDataCache;
	struct RenderElementInfo;

	struct RenderListParameters {
		GeometryInstanceSurfaceDataCache **elements = nullptr;
		RenderElementInfo *element_info = nullptr;
		int element_count = 0;
		bool reverse_cull = false;
		PassMode pass_mode = PASS_MODE_COLOR;
		uint32_t color_pass_flags = 0;
		bool no_gi = false;
		uint32_t view_count = 1;
		RID render_pass_uniform_set;
		bool force_wireframe = false;
		Vector2 uv_offset;
		float lod_distance_multiplier = 0.0;
		float screen_mesh_lod_threshold = 0.0;
		RD::FramebufferFormatID framebuffer_format = 0;
		uint32_t element_offset = 0;
		bool use_directional_soft_shadow = false;
		SceneShaderForwardClustered::ShaderSpecialization base_specialization = {};
		// FellowEngine mesh clusters: world-space planes (normals outwards) of the volume clustered surfaces are culled
		// against; 0 planes = whole surfaces.
		const Plane *fellow_cluster_planes = nullptr;
		int fellow_cluster_plane_count = 0;
		// FellowEngine occlusion culling (opaque colour pass): per list element, the index of the culled run it starts
		// (its draw is then indirect, reading the compacted elements: one draw per chunk of 64 elements, the run's chunks
		// consecutive from that index) or UINT32_MAX.
		const uint32_t *fellow_occlusion_runs = nullptr;
		RID fellow_occlusion_args;

		RenderListParameters(GeometryInstanceSurfaceDataCache **p_elements, RenderElementInfo *p_element_info, int p_element_count, bool p_reverse_cull, PassMode p_pass_mode, uint32_t p_color_pass_flags, bool p_no_gi, bool p_use_directional_soft_shadows, RID p_render_pass_uniform_set, bool p_force_wireframe = false, const Vector2 &p_uv_offset = Vector2(), float p_lod_distance_multiplier = 0.0, float p_screen_mesh_lod_threshold = 0.0, uint32_t p_view_count = 1, uint32_t p_element_offset = 0, SceneShaderForwardClustered::ShaderSpecialization p_base_specialization = {}) {
			elements = p_elements;
			element_info = p_element_info;
			element_count = p_element_count;
			reverse_cull = p_reverse_cull;
			pass_mode = p_pass_mode;
			color_pass_flags = p_color_pass_flags;
			no_gi = p_no_gi;
			view_count = p_view_count;
			render_pass_uniform_set = p_render_pass_uniform_set;
			force_wireframe = p_force_wireframe;
			uv_offset = p_uv_offset;
			lod_distance_multiplier = p_lod_distance_multiplier;
			screen_mesh_lod_threshold = p_screen_mesh_lod_threshold;
			element_offset = p_element_offset;
			use_directional_soft_shadow = p_use_directional_soft_shadows;
			base_specialization = p_base_specialization;
		}
	};

	struct LightmapData {
		float normal_xform[12];
		float texture_size[2];
		float exposure_normalization;
		uint32_t flags;
	};

	struct LightmapCaptureData {
		float sh[9 * 4];
	};

	// When changing any of these enums, remember to change the corresponding enums in the shader files as well.
	enum {
		INSTANCE_DATA_FLAG_MULTIMESH_INDIRECT = 1 << 2,
		INSTANCE_DATA_FLAGS_DYNAMIC = 1 << 3,
		INSTANCE_DATA_FLAGS_NON_UNIFORM_SCALE = 1 << 4,
		INSTANCE_DATA_FLAG_USE_GI_BUFFERS = 1 << 5,
		INSTANCE_DATA_FLAG_USE_SDFGI = 1 << 6,
		INSTANCE_DATA_FLAG_USE_LIGHTMAP_CAPTURE = 1 << 7,
		INSTANCE_DATA_FLAG_USE_LIGHTMAP = 1 << 8,
		INSTANCE_DATA_FLAG_USE_SH_LIGHTMAP = 1 << 9,
		INSTANCE_DATA_FLAG_USE_VOXEL_GI = 1 << 10,
		INSTANCE_DATA_FLAG_PARTICLES = 1 << 11,
		INSTANCE_DATA_FLAG_MULTIMESH = 1 << 12,
		INSTANCE_DATA_FLAG_MULTIMESH_FORMAT_2D = 1 << 13,
		INSTANCE_DATA_FLAG_MULTIMESH_HAS_COLOR = 1 << 14,
		INSTANCE_DATA_FLAG_MULTIMESH_HAS_CUSTOM_DATA = 1 << 15,
		INSTANCE_DATA_FLAGS_PARTICLE_TRAIL_SHIFT = 16,
		INSTANCE_DATA_FLAGS_PARTICLE_TRAIL_MASK = 0xFF,
		INSTANCE_DATA_FLAGS_FADE_SHIFT = 24,
		INSTANCE_DATA_FLAGS_FADE_MASK = 0xFFUL << INSTANCE_DATA_FLAGS_FADE_SHIFT
	};

	struct SceneState {
		// This struct is loaded into Set 1 - Binding 1, populated at start of rendering a frame, must match with shader code
		struct UBO {
			uint32_t cluster_shift;
			uint32_t cluster_width;
			uint32_t cluster_type_size;
			uint32_t max_cluster_element_count_div_32;

			uint32_t ss_effects_flags;
			float ssao_light_affect;
			float ssao_ao_affect;
			uint32_t pad1;

			float sdf_to_bounds[16];

			int32_t sdf_offset[3];
			uint32_t pad2;

			int32_t sdf_size[3];
			uint32_t gi_upscale_for_msaa;

			uint32_t volumetric_fog_enabled;
			float volumetric_fog_inv_length;
			float volumetric_fog_detail_spread;
			uint32_t volumetric_fog_pad;
		};

		struct PushConstantUbershader {
			SceneShaderForwardClustered::ShaderSpecialization specialization;
			SceneShaderForwardClustered::UbershaderConstants constants;
		};

		struct PushConstant {
			uint32_t base_index; //
			uint32_t uv_offset; //packed
			uint32_t multimesh_motion_vectors_current_offset;
			uint32_t multimesh_motion_vectors_previous_offset;
			PushConstantUbershader ubershader;
		};

		struct InstanceData {
			float transform[12];
			float compressed_aabb_position[4];
			float compressed_aabb_size[4];
			float uv_scale[4];
			uint32_t flags;
			uint32_t instance_uniforms_ofs; //base offset in global buffer for instance variables
			uint32_t gi_offset; //GI information when using lightmapping (VCT or lightmap index)
			uint32_t layer_mask;
			float prev_transform[12];
			float lightmap_uv_scale[4];
#ifdef REAL_T_IS_DOUBLE
			float model_precision[4];
			float prev_model_precision[4];
#endif

			// These setters allow us to copy the data over with operation when using floats.
			inline void set_lightmap_uv_scale(const Rect2 &p_rect) {
#ifdef REAL_T_IS_DOUBLE
				lightmap_uv_scale[0] = p_rect.position.x;
				lightmap_uv_scale[1] = p_rect.position.y;
				lightmap_uv_scale[2] = p_rect.size.x;
				lightmap_uv_scale[3] = p_rect.size.y;
#else
				Rect2 *rect = reinterpret_cast<Rect2 *>(lightmap_uv_scale);
				*rect = p_rect;
#endif
			}

			inline void set_compressed_aabb(const AABB &p_aabb) {
#ifdef REAL_T_IS_DOUBLE
				compressed_aabb_position[0] = p_aabb.position.x;
				compressed_aabb_position[1] = p_aabb.position.y;
				compressed_aabb_position[2] = p_aabb.position.z;

				compressed_aabb_size[0] = p_aabb.size.x;
				compressed_aabb_size[1] = p_aabb.size.y;
				compressed_aabb_size[2] = p_aabb.size.z;
#else
				Vector3 *compressed_aabb_position_vec3 = reinterpret_cast<Vector3 *>(compressed_aabb_position);
				Vector3 *compressed_aabb_size_vec3 = reinterpret_cast<Vector3 *>(compressed_aabb_size);
				*compressed_aabb_position_vec3 = p_aabb.position;
				*compressed_aabb_size_vec3 = p_aabb.size;
#endif
			}

			inline void set_uv_scale(const Vector4 &p_uv_scale) {
#ifdef REAL_T_IS_DOUBLE
				uv_scale[0] = p_uv_scale.x;
				uv_scale[1] = p_uv_scale.y;
				uv_scale[2] = p_uv_scale.z;
				uv_scale[3] = p_uv_scale.w;
#else
				Vector4 *uv_scale_vec4 = reinterpret_cast<Vector4 *>(uv_scale);
				*uv_scale_vec4 = p_uv_scale;
#endif
			}
		};

		static_assert(std::is_trivially_destructible_v<InstanceData>);
		static_assert(std::is_trivially_constructible_v<InstanceData>);

		UBO ubo;

		LocalVector<RID> uniform_buffers;
		LocalVector<RID> implementation_uniform_buffers;
		uint32_t used_uniform_buffer_count = 0;

		LightmapData lightmaps[MAX_LIGHTMAPS];
		RID lightmap_ids[MAX_LIGHTMAPS];
		bool lightmap_has_sh[MAX_LIGHTMAPS];
		uint32_t lightmaps_used = 0;
		uint32_t max_lightmaps;
		RID lightmap_buffer;

		MultiUmaBuffer<1u> instance_buffer[RENDER_LIST_MAX] = { MultiUmaBuffer<1u>("RENDER_LIST_OPAQUE"), MultiUmaBuffer<1u>("RENDER_LIST_MOTION"), MultiUmaBuffer<1u>("RENDER_LIST_ALPHA"), MultiUmaBuffer<1u>("RENDER_LIST_SECONDARY") };
		InstanceData *curr_gpu_ptr[RENDER_LIST_MAX] = {};

		LightmapCaptureData *lightmap_captures = nullptr;
		uint32_t max_lightmap_captures;
		RID lightmap_capture_buffer;

		RID voxelgi_ids[MAX_VOXEL_GI_INSTANCESS];
		uint32_t voxelgis_used = 0;

		bool used_screen_texture = false;
		bool used_normal_texture = false;
		bool used_depth_texture = false;
		bool used_sss = false;
		bool used_lightmap = false;
		bool used_opaque_stencil = false;

		struct ShadowPass {
			uint32_t element_from;
			uint32_t element_count;
			PassMode pass_mode;

			RID rp_uniform_set;
			float lod_distance_multiplier;
			float screen_mesh_lod_threshold;

			RID framebuffer;
			Rect2i rect;
			bool clear_depth;
			bool flip_cull;

			uint32_t uniform_buffer_index;

			// FellowEngine directional shadow cache: this entry copies a cache region into `rect` of `framebuffer`.
			bool fellow_copy = false;
			RID fellow_source;
			Vector2i fellow_source_offset;
			float fellow_scale = 1.0;
			float fellow_bias = 0.0;

			// FellowEngine mesh clusters: world-space planes of the shadow camera's volume (no near plane when casters
			// are pancaked).
			Plane fellow_cluster_planes[6];
			int fellow_cluster_plane_count = 0;
		};

		LocalVector<ShadowPass> shadow_passes;

		void grow_instance_buffer(RenderListType p_render_list, uint32_t p_req_element_count, bool p_append);
	} scene_state;

	static RenderForwardClustered *singleton;

	uint32_t _setup_environment(const RenderDataRD *p_render_data, bool p_no_fog, const Size2i &p_screen_size, const Size2 &p_viewport_size, const Color &p_default_bg_color, bool p_opaque_render_buffers = false, bool p_apply_alpha_multiplier = false, bool p_pancake_shadows = false);
	void _setup_voxelgis(const PagedArray<RID> &p_voxelgis);
	void _setup_lightmaps(const RenderDataRD *p_render_data, const PagedArray<RID> &p_lightmaps, const Transform3D &p_cam_transform);

	struct RenderElementInfo {
		enum { MAX_REPEATS = (1 << 20) - 1 };
		union {
			struct {
				uint32_t lod_index : 8;
				uint32_t uses_softshadow : 1;
				uint32_t uses_projector : 1;
				uint32_t uses_forward_gi : 1;
				uint32_t uses_lightmap : 1;
			};
			uint32_t value;
		};
		uint32_t repeat;
	};

	static_assert(std::is_trivially_destructible_v<RenderElementInfo>);
	static_assert(std::is_trivially_constructible_v<RenderElementInfo>);

	template <PassMode p_pass_mode, uint32_t p_color_pass_flags = 0>
	_FORCE_INLINE_ void _render_list_template(RenderingDevice::DrawListID p_draw_list, RenderingDevice::FramebufferFormatID p_framebuffer_Format, RenderListParameters *p_params, uint32_t p_from_element, uint32_t p_to_element);
	void _render_list(RenderingDevice::DrawListID p_draw_list, RenderingDevice::FramebufferFormatID p_framebuffer_Format, RenderListParameters *p_params, uint32_t p_from_element, uint32_t p_to_element);
	void _render_list_serial(RenderingDevice::DrawListID p_draw_list, RenderingDevice::FramebufferFormatID p_framebuffer_Format, RenderListParameters *p_params, uint32_t p_from_element, uint32_t p_to_element);
	// FellowEngine parallel recording (rendering/fellow/cpu/parallel_recording, decision 0018).
	struct FellowRecordJob {
		RenderingDevice::DrawListID draw_list = 0;
		RenderingDevice::FramebufferFormatID framebuffer_format = 0;
		RenderListParameters *params = nullptr;
		LocalVector<uint32_t> runs; // the list's run starts (one draw each)
		LocalVector<uint32_t> starts; // segment i records the elements [starts[i], starts[i + 1])
		std::atomic<bool> redraw{ false }; // a shader uses TIME (RenderingServerDefault::redraw_request() at the join)
		std::atomic<bool> abort{ false }; // a pipeline is not in its map yet: the list is recorded again serially
	} fellow_record;
	bool _fellow_render_list_parallel(RenderingDevice::DrawListID p_draw_list, RenderingDevice::FramebufferFormatID p_framebuffer_Format, RenderListParameters *p_params, uint32_t p_from_element, uint32_t p_to_element);
	static void _fellow_record_chunk(void *p_self, uint32_t p_index);
	void _render_list_with_draw_list(RenderListParameters *p_params, RID p_framebuffer, BitField<RD::DrawFlags> p_draw_flags = RD::DRAW_DEFAULT_ALL, const Vector<Color> &p_clear_color_values = Vector<Color>(), float p_clear_depth_value = 0.0, uint32_t p_clear_stencil_value = 0, const Rect2 &p_region = Rect2());

	void _fill_instance_data(RenderListType p_render_list, int *p_render_info = nullptr, uint32_t p_offset = 0, int32_t p_max_elements = -1, bool p_update_buffer = true);
	void _fill_render_list(RenderListType p_render_list, const RenderDataRD *p_render_data, PassMode p_pass_mode, bool p_using_sdfgi = false, bool p_using_opaque_gi = false, bool p_using_motion_pass = false, bool p_append = false);

	// FellowEngine parallel render lists (docs/decisions/0016-parallel-render-lists.md). The list fill and the instance
	// data are computed by worker threads over contiguous ranges and joined in order, so every list, key, flag and
	// instance buffer word is the one the single-threaded loop produces.
	class GeometryInstanceForwardClustered;
	struct FellowFillParams {
		RenderListType render_list = RENDER_LIST_OPAQUE;
		const RenderDataRD *render_data = nullptr;
		PassMode pass_mode = PASS_MODE_COLOR;
		bool using_sdfgi = false;
		bool using_opaque_gi = false;
		bool using_motion_pass = false;
		uint64_t frame = 0;
		Plane near_plane;
		float z_max = 0.0;
		// What the loop reads from the render data (copied, so shadow passes can be filled after their render data is
		// gone).
		const PagedArray<RenderGeometryInstance *> *instances = nullptr;
		bool cam_orthogonal = false;
		Vector3 cam_origin;
		Vector3 main_cam_origin;
		float lod_distance_multiplier = 0.0;
		float fellow_lod_distance_bias = 0.0;
		float fellow_lod_ortho_extent = 0.0;
		float screen_mesh_lod_threshold = 0.0;
		bool count_primitives = false;
		RenderingServerTypes::RenderInfo *render_info = nullptr;
	};
	// One element of a shadow pass filled in record mode: the sort keys and instance flags the pass gives it.
	struct FellowShadowRecord {
		uint64_t key2;
		uint64_t key1;
		GeometryInstanceSurfaceDataCache *surface;
		uint32_t flags;
	};
	struct FellowShadowRecordByKey {
		_FORCE_INLINE_ bool operator()(const FellowShadowRecord &A, const FellowShadowRecord &B) const {
			return (A.key2 == B.key2) ? (A.key1 < B.key1) : (A.key2 < B.key2);
		}
	};
	struct FellowFillOutput {
		LocalVector<GeometryInstanceSurfaceDataCache *> *main = nullptr;
		LocalVector<GeometryInstanceSurfaceDataCache *> *alpha = nullptr;
		LocalVector<GeometryInstanceSurfaceDataCache *> *motion = nullptr;
		// Single-threaded: the lightmap capture slots are assigned in list order through this counter. Worker ranges
		// leave it null and collect the instances that need a slot; they are assigned afterwards, in list order.
		uint32_t *lightmap_captures_used = nullptr;
		LocalVector<GeometryInstanceForwardClustered *> *deferred_captures = nullptr;
		LocalVector<FellowShadowRecord> *records = nullptr; // record mode
		LocalVector<FellowShadowRecord> *sort_records = nullptr; // optional: the main list's sort keys (flags unused)
		LocalVector<GeometryInstanceForwardClustered *> *transform_updates = nullptr; // record mode
		uint64_t primitives = 0;
		bool used_sss = false;
		bool used_screen_texture = false;
		bool used_normal_texture = false;
		bool used_depth_texture = false;
		bool used_lightmap = false;
		bool used_opaque_stencil = false;
	};
	struct FellowFillChunk {
		LocalVector<GeometryInstanceSurfaceDataCache *> main;
		LocalVector<GeometryInstanceSurfaceDataCache *> alpha;
		LocalVector<GeometryInstanceSurfaceDataCache *> motion;
		LocalVector<GeometryInstanceForwardClustered *> deferred_captures;
		LocalVector<FellowShadowRecord> sort_records;
		FellowFillOutput out;
		// Where the join copies this chunk's elements (and sort records) in the lists.
		uint32_t main_at = 0;
		uint32_t alpha_at = 0;
		uint32_t motion_at = 0;
		uint32_t record_at = 0;
	};
	static constexpr uint32_t FELLOW_PARALLEL_LIST_MIN = 1024; // fewer instances or elements: single-threaded
	static constexpr uint32_t FELLOW_PARALLEL_LIST_CHUNK = 256; // instances or elements per worker range
	struct FellowInstanceDataJob {
		RenderListType render_list = RENDER_LIST_OPAQUE;
		uint32_t offset = 0;
		uint32_t total = 0;
		uint32_t chunk_size = 0;
	};
	LocalVector<FellowFillChunk> fellow_fill_chunks;
	FellowFillParams fellow_fill_params;
	uint32_t fellow_fill_count = 0;
	uint32_t fellow_fill_chunk_size = 0;
	FellowInstanceDataJob fellow_instance_job;
	LocalVector<uint8_t> fellow_instance_joinable;
	// Run lengths (instancing repeats) from the "joins the previous element" bytes, by worker ranges: an element's repeat
	// is the distance to the end of its run (the next element that does not join, or the end of the list), as stock's
	// counting loop gives it while no run reaches MAX_REPEATS (lists shorter than that). Each range finds its first
	// run start (in the instance data pass); the end of the run a range ends in is the next range's first start.
	struct FellowRunChunk {
		uint32_t from = 0; // positions in the join bytes
		uint32_t to = 0;
		uint32_t first_start = UINT32_MAX;
		uint32_t starts = 0; // runs starting in the range (draws)
		uint32_t next = 0; // the end of the run the range ends in
	};
	LocalVector<FellowRunChunk> fellow_run_chunks;
	RenderElementInfo *fellow_run_info = nullptr; // element info of join byte 0
	void _fellow_run_scan(uint32_t p_chunk); // first start and starts of a range (after its join bytes are written)
	void _fellow_runs(uint32_t p_end, uint32_t p_max_helpers); // run ends, then the repeats by worker ranges
	void _fellow_runs_chunk(uint32_t p_chunk, void *p_unused);
	static constexpr uint32_t FELLOW_PARALLEL_SORT_MIN = 4096; // fewer elements: single-threaded sort
	static constexpr uint32_t FELLOW_PARALLEL_SORT_GRAIN = 1024; // ranges up to this size are sorted by one worker

	template <bool t_records>
	void _fill_render_list_range(const FellowFillParams &p_params, uint32_t p_from, uint32_t p_to, FellowFillOutput &r_out);
	FellowFillParams _fellow_fill_params(RenderListType p_render_list, const RenderDataRD *p_render_data, PassMode p_pass_mode, bool p_using_sdfgi, bool p_using_opaque_gi, bool p_using_motion_pass, uint64_t p_frame);
	void _fellow_fill_apply_output(const FellowFillParams &p_params, const FellowFillOutput &p_out);

	// Batched shadow lists (decision 0016): the shadow passes appended between _render_shadow_begin() and
	// _render_shadow_process() are filled, sorted and turned into instance data together (_fellow_shadow_flush()).
	struct FellowShadowEntry {
		FellowFillParams params;
		RenderingServerTypes::RenderInfo *render_info = nullptr;
		uint32_t shadow_pass = 0; // index in scene_state.shadow_passes
		uint32_t chunk_first = 0;
		uint32_t chunk_count = 0;
		uint32_t from = 0; // in the secondary list
		uint32_t size = 0;
		uint64_t primitives = 0;
		LocalVector<FellowShadowRecord> records;
	};
	struct FellowShadowChunk {
		uint32_t entry = 0;
		uint32_t from = 0;
		uint32_t to = 0;
		uint32_t offset = 0; // of its records in the pass's records
		uint64_t primitives = 0;
		LocalVector<FellowShadowRecord> records;
		LocalVector<GeometryInstanceForwardClustered *> transform_updates;
		LocalVector<GeometryInstanceSurfaceDataCache *> unused; // the lists the record mode does not use
	};
	struct FellowShadowItem {
		uint32_t entry;
		uint32_t from;
		uint32_t to;
	};
	LocalVector<FellowShadowEntry> fellow_shadow_entries;
	uint32_t fellow_shadow_entry_count = 0;
	LocalVector<FellowShadowChunk> fellow_shadow_chunks;
	LocalVector<FellowShadowItem> fellow_shadow_items;
	bool fellow_shadow_batching = false;
	int fellow_shadow_hold = 0; // > 0: appends are built at once (their counters are read right after)
	void _fellow_shadow_flush();
	void _fellow_shadow_fill_chunk(uint32_t p_chunk, void *p_unused);
	void _fellow_shadow_sort_entry(uint32_t p_entry, void *p_unused);
	void _fellow_shadow_gather_chunk(uint32_t p_chunk, void *p_unused);
	void _fellow_shadow_instance_chunk(uint32_t p_item, void *p_unused);
	bool _fellow_fill_render_list_parallel(const FellowFillParams &p_params, uint32_t p_count, uint32_t &r_lightmap_captures_used);
	void _fellow_fill_render_list_chunk(uint32_t p_chunk, void *p_unused);
	void _fellow_fill_join_chunk(uint32_t p_chunk, void *p_unused);
	RenderListType fellow_join_list = RENDER_LIST_OPAQUE;
	bool fellow_join_records = false;
	void _fellow_make_instance_data(const GeometryInstanceSurfaceDataCache *p_surface, const GeometryInstanceForwardClustered *p_inst, SceneState::InstanceData &r_data, bool p_full) const;
	// Shadow passes (depth only, no motion vectors) never read an instance's prev_transform or lightmap_uv_scale, the
	// last fields of InstanceData: with cpu/compact_shadow_instances the batched shadow lists write only the bytes before
	// them (the buffer is written over PCIe on discrete GPUs).
#ifdef REAL_T_IS_DOUBLE
	static constexpr uint32_t FELLOW_SHADOW_INSTANCE_BYTES = sizeof(SceneState::InstanceData);
#else
	static constexpr uint32_t FELLOW_SHADOW_INSTANCE_BYTES = offsetof(SceneState::InstanceData, prev_transform);
	static_assert(offsetof(SceneState::InstanceData, lightmap_uv_scale) == FELLOW_SHADOW_INSTANCE_BYTES + sizeof(float) * 12 && sizeof(SceneState::InstanceData) == offsetof(SceneState::InstanceData, lightmap_uv_scale) + sizeof(float) * 4, "prev_transform and lightmap_uv_scale must be InstanceData's last fields");
#endif
	bool fellow_shadow_compact = false;
	void _fellow_fill_instance_data_chunk(uint32_t p_chunk, void *p_unused);
	void _fellow_sort_by_key(RenderListType p_render_list, uint32_t p_from, uint32_t p_size);
	void _fellow_sort_by_key_impl(RenderListType p_render_list, uint32_t p_from, uint32_t p_size);
	// Verification (rendering/fellow/cpu/verify): stock's results computed on the side and compared.
	uint32_t _fellow_verify_sort(RenderListType p_render_list, uint32_t p_from, uint32_t p_size, LocalVector<GeometryInstanceSurfaceDataCache *> &p_unsorted);
	uint32_t _fellow_verify_instances(RenderListType p_render_list, uint32_t p_from, uint32_t p_size, const uint32_t *p_flags, bool p_compact);
	void _fellow_verify_count(uint32_t p_mismatches);
	// Parallel loops over chunks (decision 0016): the render thread takes chunks itself, worker tasks help if they start
	// in time (servers/rendering/fellow_parallel.h).
	FellowParallelFor *fellow_parallel = nullptr; // shared with the scene cull (FellowParallelFor::shared_acquire())
	LocalVector<SceneState::InstanceData> fellow_debug_scratch[RENDER_LIST_MAX]; // cpu/debug_instance_scratch
	template <void (RenderForwardClustered::*M)(uint32_t, void *)>
	static void _fellow_chunk_thunk(void *p_self, uint32_t p_chunk) {
		(static_cast<RenderForwardClustered *>(p_self)->*M)(p_chunk, nullptr);
	}
	void _fellow_parallel_for(uint32_t p_count, FellowParallelFor::Func p_func, uint32_t p_max_helpers = UINT32_MAX);

	// Persistent instance data (cpu/persistent_instances, docs/decisions/0022-persistent-instance-data.md). Each surface
	// cache has an entry (slot) in a GPU buffer kept from frame to frame, holding stock's InstanceData without the
	// fields a pass sets (flags, GI offset); the per-frame instance buffers then hold one FellowElement per list
	// element. The buffer has one region per frame in flight (a dynamic persistent buffer, as stock's per-frame
	// buffers); the frame's first list fill maps the next region and writes into it the entries that changed since that
	// region was last mapped. A list element whose entry is out of date (its instance's data version changed) rebuilds
	// it, compares it with a copy kept on the CPU and, if it changed, writes it into the frame's region.
	struct FellowElement {
		uint32_t slot;
		uint32_t flags;
		uint32_t gi_offset;
		uint32_t pad;
	};
	struct FellowPersistent {
		static constexpr uint32_t MAPS = 4; // maps whose written entries are kept (more than the frames in flight)
		bool active = false; // this frame's choice
		uint64_t frame = UINT64_MAX;
		RID buffer; // dynamic persistent: one region of capacity entries per frame in flight
		uint32_t capacity = 0;
		uint8_t *region = nullptr; // this frame's region (host-visible)
		LocalVector<SceneState::InstanceData> mirror; // what each entry holds (valid ones)
		LocalVector<uint8_t> valid;
		LocalVector<uint32_t> free_slots;
		uint32_t slot_count = 0; // slots handed out (high water)
		// Per map of the buffer (the last MAPS): the entries written into its region, so that a region mapped again gets
		// them too.
		LocalVector<uint32_t> written[MAPS];
		std::atomic<uint32_t> written_count[MAPS] = {};
		uint64_t map_seq = 0; // maps of the buffer so far (the current one is map_seq - 1)
		uint8_t *region_ptr[MAPS] = {}; // the buffer's regions seen so far, and the map at which each was last mapped
		uint64_t region_map[MAPS] = {};
		uint32_t region_count = 0;
		bool needs_flush = false;
	} fellow_persistent;
	// Occlusion statistics (debug/occlusion_stats, docs/decisions/0026-occlusion-culling.md): after the depth pre-pass, a
	// depth pyramid is built and every opaque list element's AABB tested against it; how many (and how many indices)
	// would be hidden is counted. Measurement only: nothing is culled.
	struct FellowOcclusion {
		FellowHzbShaderRD hzb_shader;
		RID hzb_version;
		RID hzb_pipelines[2]; // levels 0-5 from the depth buffer, a level from the previous one
		FellowOcclusionTestShaderRD test_shader;
		RID test_version;
		RID test_pipeline;
		RID hzb; // R32F, level 0 covers 2 x 2 pixels (sides multiples of 64), 7 levels
		LocalVector<RID> hzb_levels; // a view per level
		Size2i hzb_size;
		RID element_buffer;
		uint32_t element_capacity = 0;
		RID counters;
		LocalVector<float> elements;
		// Debug (debug/occlusion_cull_approx, wrong images): elements the test hid are left out of the camera's opaque
		// lists in the following frames (visibility read back asynchronously), to size what culling would save.
		RID visible_by_slot;
		uint32_t visible_capacity = 0;
		LocalVector<uint8_t> visible_cpu; // per slot, from a readback 1-3 frames old
		LocalVector<GeometryInstanceSurfaceDataCache *> candidates; // the unfiltered opaque list (tested)
		bool filtered = false;

		// Culling (geometry/occlusion_culling): per surface slot its surface's local bounding box (8 floats: minimum,
		// 1 if known, maximum, 0), written when the slot is given to a surface; the runs of this frame's opaque pass
		// tested by the GPU, their indirect draw arguments and the compacted elements.
		FellowOcclusionCullShaderRD cull_shader;
		RID cull_version;
		RID cull_pipelines[2]; // test (a thread per element), compact (a thread per chunk)
		LocalVector<float> slot_bounds_cpu;
		RID slot_bounds;
		uint32_t slot_bounds_capacity = 0; // slots
		uint32_t bounds_dirty_from = UINT32_MAX; // slots written since the last upload
		uint32_t bounds_dirty_to = 0;
		// Each tested run is drawn in chunks of up to CHUNK elements (one indirect draw each, in order).
		static constexpr uint32_t CHUNK = 64;
		LocalVector<uint32_t> run_data; // per chunk: first element, count, indices per draw, 0
		LocalVector<uint32_t> run_of_element; // per opaque list element (see RenderListParameters::fellow_occlusion_runs)
		RID visibility; // per list element
		uint32_t visibility_capacity = 0;
		RID runs;
		RID args;
		uint32_t run_capacity = 0;
		RID compacted;
		uint32_t compacted_capacity = 0; // elements
		RID cull_counters;
		bool pass_active = false; // the opaque colour pass being set up draws culled runs (binding 41: compacted)
	} fellow_occlusion;
	// Adaptive shading rate (shading/adaptive_vrs, docs/decisions/0027-adaptive-shading-rate.md).
	struct FellowVRS {
		FellowAdaptiveVrsShaderRD shader;
		RID version;
		RID pipelines[3]; // estimate, dilate, clear
		RID counters;
	} fellow_vrs;
	bool _fellow_adaptive_vrs_target(const Ref<RenderSceneBuffersRD> &p_render_buffers) const;
	void _fellow_adaptive_vrs_begin(const Ref<RenderSceneBuffersRD> &p_render_buffers);
	void _fellow_adaptive_vrs_update(const Ref<RenderSceneBuffersRD> &p_render_buffers);
	void _fellow_adaptive_vrs_dispatch(const Ref<RenderSceneBuffersRD> &p_render_buffers, int p_mode);
	static void _fellow_adaptive_vrs_readback(const Vector<uint8_t> &p_data);
	void _fellow_occlusion_init();
	bool _fellow_occlusion_pyramid(const Ref<RenderSceneBuffersRD> &p_render_buffers);
	bool _fellow_occlusion_cull(RenderDataRD *p_render_data, uint32_t p_color_pass_flags);
	void _fellow_occlusion_set_slot_bounds(uint32_t p_slot, void *p_mesh_surface);
	static void _fellow_occlusion_cull_readback(const Vector<uint8_t> &p_data);
	void _fellow_occlusion_stats(RenderDataRD *p_render_data);
	void _fellow_occlusion_filter(RenderListType p_list);
	static void _fellow_occlusion_readback(const Vector<uint8_t> &p_data);
	void _fellow_occlusion_free();

	uint32_t _fellow_slot_alloc();
	void _fellow_slot_free(uint32_t p_slot);
	bool _fellow_persistent_begin(); // this frame's choice (taken by the frame's first list fill)
	void _fellow_persistent_check(GeometryInstanceSurfaceDataCache *p_surface, const GeometryInstanceForwardClustered *p_inst, uint64_t p_frame);
	bool _fellow_persistent_reserve(); // true: a new buffer (mapped)
	void _fellow_persistent_map();
	void _fellow_persistent_flush();
	void _fellow_persistent_free();
	uint32_t _fellow_verify_elements(RenderListType p_render_list, uint32_t p_from, uint32_t p_size, const uint32_t *p_flags);
	static constexpr uint32_t FELLOW_PREFETCH_DISTANCE = 8; // elements or instances ahead
	static constexpr uint32_t FELLOW_PREFETCH_INSTANCE_LINES = 6; // 64-byte lines of an instance (its fields used per frame)
	static void _fellow_prefetch_element(GeometryInstanceSurfaceDataCache *const *p_elements, uint32_t p_index, uint32_t p_end);

	HashMap<Size2i, RID> sdfgi_framebuffer_size_cache;

	struct GeometryInstanceData;
	class GeometryInstanceForwardClustered;

	struct GeometryInstanceLightmapSH {
		Color sh[9];
	};

	// Cached data for drawing surfaces
	struct GeometryInstanceSurfaceDataCache {
		enum {
			FLAG_PASS_DEPTH = 1,
			FLAG_PASS_OPAQUE = 2,
			FLAG_PASS_ALPHA = 4,
			FLAG_PASS_SHADOW = 8,
			FLAG_USES_SHARED_SHADOW_MATERIAL = 128,
			FLAG_USES_SUBSURFACE_SCATTERING = 2048,
			FLAG_USES_SCREEN_TEXTURE = 4096,
			FLAG_USES_DEPTH_TEXTURE = 8192,
			FLAG_USES_NORMAL_TEXTURE = 16384,
			FLAG_USES_DOUBLE_SIDED_SHADOWS = 32768,
			FLAG_USES_PARTICLE_TRAILS = 65536,
			FLAG_USES_MOTION_VECTOR = 131072,
			FLAG_USES_STENCIL = 262144,
		};

		union {
			struct {
				uint64_t sort_key1;
				uint64_t sort_key2;
			};
			struct {
				// Needs to be grouped together to be used in RenderElementInfo, as the value is masked directly.
				uint64_t lod_index : 8;
				uint64_t uses_softshadow : 1;
				uint64_t uses_projector : 1;
				uint64_t uses_forward_gi : 1;
				uint64_t uses_lightmap : 1;

				// Sorted based on optimal order for respecting priority and reducing the amount of rebinding of shaders, materials,
				// and geometry. This current order was found to be the most optimal in large projects. If you wish to measure
				// differences, refer to RenderingDeviceGraph and the methods available to print statistics for draw lists.
				uint64_t depth_layer : 4;
				uint64_t surface_index : 8;
				uint64_t geometry_id : 32;
				uint64_t material_id_hi : 8;

				uint64_t material_id_lo : 24;
				uint64_t shader_id : 32;
				uint64_t priority : 8;
			};
		} sort;

		RSE::PrimitiveType primitive = RSE::PRIMITIVE_MAX;
		uint32_t flags = 0;
		uint32_t surface_index = 0;
		uint32_t color_pass_inclusion_mask = 0;

		void *surface = nullptr;
		RID material_uniform_set;
		SceneShaderForwardClustered::ShaderData *shader = nullptr;
		SceneShaderForwardClustered::MaterialData *material = nullptr;

		void *surface_shadow = nullptr;
		RID material_uniform_set_shadow;
		SceneShaderForwardClustered::ShaderData *shader_shadow = nullptr;

		GeometryInstanceSurfaceDataCache *next = nullptr;
		GeometryInstanceForwardClustered *owner = nullptr;
		// FellowEngine persistent instance data (decision 0022): this surface's entry in the persistent buffer, and the
		// last frame it was checked (the first list element of a frame checks it).
		uint32_t fellow_slot = UINT32_MAX;
		std::atomic<uint64_t> fellow_checked_frame{ 0 };
		std::atomic<uint64_t> fellow_seen_version{ 0 }; // the instance's fellow_data_version the entry was last built from
		SelfList<GeometryInstanceSurfaceDataCache> compilation_dirty_element;
		SelfList<GeometryInstanceSurfaceDataCache> compilation_all_element;

		GeometryInstanceSurfaceDataCache() :
				compilation_dirty_element(this), compilation_all_element(this) {}
	};

	class GeometryInstanceForwardClustered : public RenderGeometryInstanceBase {
	public:
		// lightmap
		RID lightmap_instance;
		Rect2 lightmap_uv_scale;
		uint32_t lightmap_slice_index;
		GeometryInstanceLightmapSH *lightmap_sh = nullptr;

		//used during rendering

		uint32_t gi_offset_cache = 0;
		bool store_transform_cache = true;
		RID transforms_uniform_set;
		uint32_t instance_count = 0;
		uint32_t trail_steps = 1;
		bool can_sdfgi = false;
		bool using_projectors = false;
		bool using_softshadows = false;

		//used during setup
		uint64_t prev_transform_change_frame = 0xFFFFFFFF;
		enum TransformStatus {
			NONE,
			MOVED,
			TELEPORTED,
		} transform_status = TransformStatus::MOVED;
		Transform3D prev_transform;
		RID voxel_gi_instances[MAX_VOXEL_GI_INSTANCESS_PER_INSTANCE];
		GeometryInstanceSurfaceDataCache *surface_caches = nullptr;
		SelfList<GeometryInstanceForwardClustered> dirty_list_element;

		GeometryInstanceForwardClustered() :
				dirty_list_element(this) {}

		virtual void _mark_dirty() override;

		virtual void set_transform(const Transform3D &p_transform, const AABB &p_aabb, const AABB &p_transformed_aabb) override;
		virtual void reset_motion_vectors() override;
		virtual void set_use_lightmap(RID p_lightmap_instance, const Rect2 &p_lightmap_uv_scale, int p_lightmap_slice_index) override;
		virtual void set_lightmap_capture(const Color *p_sh9) override;

		virtual void clear_light_instances() override {}
		virtual void pair_light_instance(const RID p_light_instance, RSE::LightType light_type, uint32_t placement_idx) override {}
		virtual void pair_reflection_probe_instances(const RID *p_reflection_probe_instances, uint32_t p_reflection_probe_instance_count) override {}
		virtual void pair_decal_instances(const RID *p_decal_instances, uint32_t p_decal_instance_count) override {}
		virtual void pair_voxel_gi_instances(const RID *p_voxel_gi_instances, uint32_t p_voxel_gi_instance_count) override;

		virtual void set_softshadow_projector_pairing(bool p_softshadow, bool p_projector) override;
	};

	// These are not used in the Forward+ path, it has different light clustering tech.
	virtual uint32_t get_max_lights_total() override { return 0; }
	virtual uint32_t get_max_lights_per_mesh() override { return 0; }

	static void _geometry_instance_dependency_changed(Dependency::DependencyChangedNotification p_notification, DependencyTracker *p_tracker);
	static void _geometry_instance_dependency_deleted(const RID &p_dependency, DependencyTracker *p_tracker);

	SelfList<GeometryInstanceForwardClustered>::List geometry_instance_dirty_list;
	SelfList<GeometryInstanceSurfaceDataCache>::List geometry_surface_compilation_dirty_list;
	SelfList<GeometryInstanceSurfaceDataCache>::List geometry_surface_compilation_all_list;

	PagedAllocator<GeometryInstanceForwardClustered> geometry_instance_alloc;
	PagedAllocator<GeometryInstanceSurfaceDataCache> geometry_instance_surface_alloc;
	PagedAllocator<GeometryInstanceLightmapSH> geometry_instance_lightmap_sh;

	struct SurfacePipelineData {
		void *mesh_surface = nullptr;
		void *mesh_surface_shadow = nullptr;
		SceneShaderForwardClustered::ShaderData *shader = nullptr;
		SceneShaderForwardClustered::ShaderData *shader_shadow = nullptr;
		bool instanced = false;
		bool uses_opaque = false;
		bool uses_transparent = false;
		bool uses_depth = false;
		bool can_use_lightmap = false;
	};

	struct GlobalPipelineData {
		union {
			uint32_t key;

			struct {
				uint32_t texture_samples : 3;
				uint32_t use_reflection_probes : 1;
				uint32_t use_separate_specular : 1;
				uint32_t use_motion_vectors : 1;
				uint32_t use_normal_and_roughness : 1;
				uint32_t use_lightmaps : 1;
				uint32_t use_voxelgi : 1;
				uint32_t use_sdfgi : 1;
				uint32_t use_multiview : 1;
				uint32_t use_16_bit_shadows : 1;
				uint32_t use_32_bit_shadows : 1;
				uint32_t use_shadow_cubemaps : 1;
				uint32_t use_shadow_dual_paraboloid : 1;
			};
		};
	};

	GlobalPipelineData global_pipeline_data_compiled = {};
	GlobalPipelineData global_pipeline_data_required = {};

	typedef Pair<SceneShaderForwardClustered::ShaderData *, SceneShaderForwardClustered::ShaderData::PipelineKey> ShaderPipelinePair;

	void _update_global_pipeline_data_requirements_from_project();
	void _update_global_pipeline_data_requirements_from_light_storage();
	void _geometry_instance_add_surface_with_material(GeometryInstanceForwardClustered *ginstance, uint32_t p_surface, SceneShaderForwardClustered::MaterialData *p_material, uint32_t p_material_id, uint32_t p_shader_id, RID p_mesh);
	void _geometry_instance_add_surface_with_material_chain(GeometryInstanceForwardClustered *ginstance, uint32_t p_surface, SceneShaderForwardClustered::MaterialData *p_material, RID p_mat_src, RID p_mesh);
	void _geometry_instance_add_surface(GeometryInstanceForwardClustered *ginstance, uint32_t p_surface, RID p_material, RID p_mesh);
	void _fellow_process_ss_effects(RenderDataRD *p_render_data, const Ref<RenderSceneBuffersRD> &rb, bool p_use_ssao, bool p_use_ssil, bool p_use_ssr, const RID *p_normal_roughness_slices);
	void _fellow_decide_clusters(GeometryInstanceForwardClustered *ginstance, uint32_t p_surface, SceneShaderForwardClustered::MaterialData *p_material, RID p_mesh);
	void _geometry_instance_update(RenderGeometryInstance *p_geometry_instance);
	void _mesh_compile_pipeline_for_surface(SceneShaderForwardClustered::ShaderData *p_shader, void *p_mesh_surface, bool p_ubershader, bool p_instanced_surface, RSE::PipelineSource p_source, SceneShaderForwardClustered::ShaderData::PipelineKey &r_pipeline_key, Vector<ShaderPipelinePair> *r_pipeline_pairs = nullptr);
	void _mesh_compile_pipelines_for_surface(const SurfacePipelineData &p_surface, const GlobalPipelineData &p_global, RSE::PipelineSource p_source, Vector<ShaderPipelinePair> *r_pipeline_pairs = nullptr);
	void _mesh_generate_all_pipelines_for_surface_cache(GeometryInstanceSurfaceDataCache *p_surface_cache, const GlobalPipelineData &p_global);
	void _update_dirty_geometry_instances();
	void _update_dirty_geometry_pipelines();

	// Global data about the scene that can be used to pre-allocate resources without relying on culling.
	struct GlobalSurfaceData {
		bool screen_texture_used = false;
		bool normal_texture_used = false;
		bool depth_texture_used = false;
		bool sss_used = false;
	} global_surface_data;

	/* Render List */

	struct RenderList {
		LocalVector<GeometryInstanceSurfaceDataCache *> elements;
		LocalVector<RenderElementInfo> element_info;

		void clear() {
			elements.clear();
			element_info.clear();
			fellow_sort_records_count = 0;
		}

		//should eventually be replaced by radix

		struct SortByKey {
			_FORCE_INLINE_ bool operator()(const GeometryInstanceSurfaceDataCache *A, const GeometryInstanceSurfaceDataCache *B) const {
				return (A->sort.sort_key2 == B->sort.sort_key2) ? (A->sort.sort_key1 < B->sort.sort_key1) : (A->sort.sort_key2 < B->sort.sort_key2);
			}
		};

		void sort_by_key() {
			SortArray<GeometryInstanceSurfaceDataCache *, SortByKey> sorter;
			sorter.sort(elements.ptr(), elements.size());
		}

		void sort_by_key_range(uint32_t p_from, uint32_t p_size) {
			SortArray<GeometryInstanceSurfaceDataCache *, SortByKey> sorter;
			sorter.sort(elements.ptr() + p_from, p_size);
		}

		// FellowEngine compact sort (docs/decisions/0016-parallel-render-lists.md): the same introsort, run on
		// (key2, key1, element) records copied out of the surfaces. The comparisons see the same keys in the same
		// positions, so the algorithm takes the same steps and leaves the elements in the same order (ties included),
		// without dereferencing two scattered surfaces per comparison.
		struct FellowSortRecord {
			uint64_t key2;
			uint64_t key1;
			GeometryInstanceSurfaceDataCache *surface;
		};
		struct FellowSortRecordByKey {
			_FORCE_INLINE_ bool operator()(const FellowSortRecord &A, const FellowSortRecord &B) const {
				return (A.key2 == B.key2) ? (A.key1 < B.key1) : (A.key2 < B.key2);
			}
		};
		LocalVector<FellowSortRecord> fellow_sort_records;
		// Records taken by the parallel fill for elements [from, from + count) (count 0: none).
		uint32_t fellow_sort_records_from = 0;
		uint32_t fellow_sort_records_count = 0;

		// True when fellow_sort_records already holds the keys of exactly these elements (taken during the fill).
		bool fellow_take_fill_records(uint32_t p_from, uint32_t p_size) {
			const bool ready = fellow_sort_records_count > 0 && fellow_sort_records_from == p_from && fellow_sort_records_count == p_size;
			fellow_sort_records_count = 0;
			return ready;
		}

		void fellow_sort_by_key_range(uint32_t p_from, uint32_t p_size) {
			if (p_size < 2) {
				fellow_sort_records_count = 0;
				return;
			}
			GeometryInstanceSurfaceDataCache **e = elements.ptr() + p_from;
			if (!fellow_take_fill_records(p_from, p_size)) {
				fellow_sort_records.resize(p_size);
				FellowSortRecord *gather = fellow_sort_records.ptr();
				for (uint32_t i = 0; i < p_size; i++) {
					if (i + 16 < p_size) {
						fellow_prefetch(e[i + 16]); // no effect on results
					}
					gather[i].key2 = e[i]->sort.sort_key2;
					gather[i].key1 = e[i]->sort.sort_key1;
					gather[i].surface = e[i];
				}
			}
			FellowSortRecord *records = fellow_sort_records.ptr();
			SortArray<FellowSortRecord, FellowSortRecordByKey> sorter;
			sorter.sort(records, p_size);
			for (uint32_t i = 0; i < p_size; i++) {
				e[i] = records[i].surface;
			}
		}

		struct SortByDepth {
			_FORCE_INLINE_ bool operator()(const GeometryInstanceSurfaceDataCache *A, const GeometryInstanceSurfaceDataCache *B) const {
				return (A->owner->depth < B->owner->depth);
			}
		};

		void sort_by_depth() { //used for shadows

			SortArray<GeometryInstanceSurfaceDataCache *, SortByDepth> sorter;
			sorter.sort(elements.ptr(), elements.size());
		}

		struct SortByReverseDepthAndPriority {
			_FORCE_INLINE_ bool operator()(const GeometryInstanceSurfaceDataCache *A, const GeometryInstanceSurfaceDataCache *B) const {
				return (A->sort.priority == B->sort.priority) ? (A->owner->depth > B->owner->depth) : (A->sort.priority < B->sort.priority);
			}
		};

		void sort_by_reverse_depth_and_priority() { //used for alpha

			SortArray<GeometryInstanceSurfaceDataCache *, SortByReverseDepthAndPriority> sorter;
			sorter.sort(elements.ptr(), elements.size());
		}

		_FORCE_INLINE_ void add_element(GeometryInstanceSurfaceDataCache *p_element) {
			elements.push_back(p_element);
		}
	};

	RenderList render_list[RENDER_LIST_MAX];
	// Parallel sorts (servers/rendering/fellow_sort.h): stock's introsort, its independent parts on worker threads.
	FellowParallelSort<RenderList::FellowSortRecord, RenderList::FellowSortRecordByKey> fellow_list_sort;
	FellowParallelSort<FellowShadowRecord, FellowShadowRecordByKey> fellow_shadow_sort;
	static void _fellow_sort_write_back(void *p_elements, const RenderList::FellowSortRecord *p_records, int64_t p_first, int64_t p_last);

	virtual void _update_shader_quality_settings() override;

	/* Effects */

	RendererRD::TAA *taa = nullptr;
	RendererRD::FSR2Effect *fsr2_effect = nullptr;
	RendererRD::SSEffects *ss_effects = nullptr;
	RendererRD::FellowShadowCache *fellow_shadow_cache = nullptr;
	RendererRD::FellowShadowMaxMip *fellow_shadow_max_mip = nullptr;
	// Decision 0013: TAA with camera motion vectors (motion pass). Instances that move keep the "dynamic" bit of the
	// normal-roughness buffer, as in stock's TAA path (stock's upscaler motion pass drops it).
	bool fellow_taa_motion_pass = false;
	// Directional shadow atlas rects written since the min/max pyramid was last brought up to date (one per cascade;
	// a union would also cover the unchanged cascades between two changed ones).
	LocalVector<Rect2i> fellow_dir_atlas_dirty;
	bool fellow_dir_atlas_dirty_all = true;
	void _fellow_mark_dir_atlas_dirty(const Rect2i &p_rect) {
		for (const Rect2i &r : fellow_dir_atlas_dirty) {
			if (r.encloses(p_rect)) {
				return;
			}
		}
		fellow_dir_atlas_dirty.push_back(p_rect);
	}
	// Directional cascades of this render (atlas rect in texels, shadow camera, split distance) and, per cascade rect,
	// the part whose pyramid windows were computed from the atlas (shadows/max_mip_visible_region; elsewhere (0, 1)).
	struct FellowDirCascade {
		Rect2i rect;
		Projection projection;
		Transform3D transform;
		float split = 0.0;
	};
	LocalVector<FellowDirCascade> fellow_dir_cascades;
	LocalVector<Pair<Rect2i, Rect2i>> fellow_dir_built;
	Rect2i _fellow_dir_needed_rect(const FellowDirCascade &p_cascade, const RenderSceneDataRD *p_scene_data) const;
	// This frame, cascades rendered the stock way clear their own rect instead of the whole atlas being cleared (so the
	// cached cascades keep their content and an unchanged one is neither copied again nor rebuilt in the pyramid).
	bool fellow_dir_clear_stock_rects = false;
	// Positional shadow atlas texels written since each atlas's min/max pyramid was last brought up to date.
	struct FellowAtlasDirty {
		LocalVector<Rect2i> rects; // one per re-rendered slot (a union would cover the slots in between)
		bool all = true;
		void merge(const Rect2i &p_rect) {
			for (const Rect2i &r : rects) {
				if (r.encloses(p_rect)) {
					return;
				}
			}
			rects.push_back(p_rect);
		}
	};
	HashMap<RID, FellowAtlasDirty> fellow_pos_atlas_dirty;

#ifdef METAL_MFXTEMPORAL_ENABLED
	RendererRD::MFXTemporalEffect *mfx_temporal_effect = nullptr;
#endif
	RendererRD::MotionVectorsStore *motion_vectors_store = nullptr;

	/* Cluster builder */

	ClusterBuilderSharedDataRD cluster_builder_shared;
	ClusterBuilderRD *current_cluster_builder = nullptr;

	/* SDFGI */
	void _update_sdfgi(RenderDataRD *p_render_data);

	/* Volumetric fog */
	RID shadow_sampler;

	void _update_volumetric_fog(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_environment, const Projection &p_cam_projection, const Transform3D &p_cam_transform, const Transform3D &p_prev_cam_inv_transform, RID p_shadow_atlas, int p_directional_light_count, bool p_use_directional_shadows, int p_positional_light_count, int p_voxel_gi_count, const PagedArray<RID> &p_fog_volumes);

	/* Render shadows */

	void _render_shadow_pass(RID p_light, RID p_shadow_atlas, int p_pass, const PagedArray<RenderGeometryInstance *> &p_instances, float p_lod_distance_multiplier = 0, float p_screen_mesh_lod_threshold = 0.0, bool p_open_pass = true, bool p_close_pass = true, bool p_clear_region = true, RenderingServerTypes::RenderInfo *p_render_info = nullptr, const Size2i &p_viewport_size = Size2i(1, 1), const Transform3D &p_main_cam_transform = Transform3D(), const RendererSceneRender::FellowShadowCacheOp *p_fellow_op = nullptr);
	void _render_shadow_begin();
	void _render_shadow_append(RID p_framebuffer, const PagedArray<RenderGeometryInstance *> &p_instances, const Projection &p_projection, const Transform3D &p_transform, float p_zfar, float p_bias, float p_normal_bias, bool p_reverse_cull_face, bool p_use_dp, bool p_use_dp_flip, bool p_use_pancake, float p_lod_distance_multiplier = 0.0, float p_screen_mesh_lod_threshold = 0.0, const Rect2i &p_rect = Rect2i(), bool p_flip_y = false, bool p_clear_region = true, bool p_begin = true, bool p_end = true, RenderingServerTypes::RenderInfo *p_render_info = nullptr, const Size2i &p_viewport_size = Size2i(1, 1), const Transform3D &p_main_cam_transform = Transform3D(), float p_fellow_lod_distance_bias = 0.0, float p_fellow_lod_ortho_extent = 0.0);
	void _render_shadow_process();
	void _render_shadow_end();

	/* Render Scene */
	void _process_ssao(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_environment, const RID *p_normal_buffers, const Projection *p_projections);
	void _process_ssil(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_environment, const RID *p_normal_buffers, const Projection *p_projections, const Transform3D &p_transform);
	void _process_ssr(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_environment, const RID *p_normal_slices, const Projection *p_projections, const Vector3 *p_eye_offsets, const Transform3D &p_transform);
	void _copy_framebuffer_to_ss_effects(Ref<RenderSceneBuffersRD> p_render_buffers, bool p_use_ssil, bool p_use_ssr);
	void _pre_opaque_render(RenderDataRD *p_render_data, bool p_use_ssao, bool p_use_ssil, bool p_use_ssr, bool p_use_gi, const RID *p_normal_roughness_slices, RID p_voxel_gi_buffer);
	void _process_sss(Ref<RenderSceneBuffersRD> p_render_buffers, const Projection &p_camera);

	/* Debug */
	void _debug_draw_cluster(Ref<RenderSceneBuffersRD> p_render_buffers);

protected:
	/* setup */

	virtual RID _render_buffers_get_normal_texture(Ref<RenderSceneBuffersRD> p_render_buffers) override;
	virtual RID _render_buffers_get_velocity_texture(Ref<RenderSceneBuffersRD> p_render_buffers) override;

	virtual void environment_set_ssao_quality(RSE::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) override;
	virtual void environment_set_ssil_quality(RSE::EnvironmentSSILQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) override;
	virtual void environment_set_ssr_half_size(bool p_half_size) override;
	virtual void environment_set_ssr_roughness_quality(RSE::EnvironmentSSRRoughnessQuality p_quality) override;

	virtual void sub_surface_scattering_set_quality(RSE::SubSurfaceScatteringQuality p_quality) override;
	virtual void sub_surface_scattering_set_scale(float p_scale, float p_depth_scale) override;

	/* Rendering */

	virtual void _render_scene(RenderDataRD *p_render_data, const Color &p_default_bg_color) override;
	virtual void _render_buffers_debug_draw(const RenderDataRD *p_render_data) override;

	virtual void _render_material(const Transform3D &p_cam_transform, const Projection &p_cam_projection, bool p_cam_orthogonal, const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region, float p_exposure_normalization) override;
	virtual void _render_uv2(const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region) override;
	virtual void _render_sdfgi(Ref<RenderSceneBuffersRD> p_render_buffers, const Vector3i &p_from, const Vector3i &p_size, const AABB &p_bounds, const PagedArray<RenderGeometryInstance *> &p_instances, const RID &p_albedo_texture, const RID &p_emission_texture, const RID &p_emission_aniso_texture, const RID &p_geom_facing_texture, float p_exposure_normalization) override;
	virtual void _render_particle_collider_heightfield(RID p_fb, const Transform3D &p_cam_transform, const Projection &p_cam_projection, const PagedArray<RenderGeometryInstance *> &p_instances) override;

public:
	static RenderForwardClustered *get_singleton() { return singleton; }

	ClusterBuilderSharedDataRD *get_cluster_builder_shared() { return &cluster_builder_shared; }
	RendererRD::SSEffects *get_ss_effects() { return ss_effects; }

	/* callback from updating our lighting UBOs, used to populate cluster builder */
	virtual void setup_added_reflection_probe(const Transform3D &p_transform, const Vector3 &p_half_size) override;
	virtual void setup_added_light(const RSE::LightType p_type, const Transform3D &p_transform, float p_radius, float p_spot_aperture, const Vector2 &p_area_size) override;
	virtual void setup_added_decal(const Transform3D &p_transform, const Vector3 &p_half_size) override;

	virtual void base_uniforms_changed() override;

	/* SDFGI UPDATE */

	virtual void sdfgi_update(const Ref<RenderSceneBuffers> &p_render_buffers, RID p_environment, const Vector3 &p_world_position) override;
	virtual int sdfgi_get_pending_region_count(const Ref<RenderSceneBuffers> &p_render_buffers) const override;
	virtual AABB sdfgi_get_pending_region_bounds(const Ref<RenderSceneBuffers> &p_render_buffers, int p_region) const override;
	virtual uint32_t sdfgi_get_pending_region_cascade(const Ref<RenderSceneBuffers> &p_render_buffers, int p_region) const override;
	RID sdfgi_get_ubo() const { return gi.sdfgi_ubo; }

	/* GEOMETRY INSTANCE */

	virtual RenderGeometryInstance *geometry_instance_create(RID p_base) override;
	virtual void geometry_instance_free(RenderGeometryInstance *p_geometry_instance) override;

	virtual uint32_t geometry_instance_get_pair_mask() override;

	/* PIPELINES */

	virtual void mesh_generate_pipelines(RID p_mesh, bool p_background_compilation) override;
	virtual uint32_t get_pipeline_compilations(RSE::PipelineSource p_source) override;

	/* SHADER LIBRARY */

	virtual void enable_features(BitField<FeatureBits> p_feature_bits) override;
	virtual String get_name() const override;

	virtual bool free(RID p_rid) override;

	virtual bool fellow_directional_shadow_cache_supported() const override { return true; }
	virtual void fellow_directional_shadow_cache_free(int64_t p_cache_id) override;
	virtual bool fellow_directional_shadow_cache_take_lost(int64_t p_cache_id) override;
	virtual bool fellow_directional_shadow_cache_get_static_cost(int64_t p_cache_id, int p_cascade, uint64_t p_build_frame, uint64_t &r_primitives, uint32_t &r_draws) const override;

	virtual void update() override;

	RenderForwardClustered();
	~RenderForwardClustered();
};
} // namespace RendererSceneRenderImplementation
