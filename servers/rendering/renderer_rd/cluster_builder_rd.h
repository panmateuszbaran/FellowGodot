/**************************************************************************/
/*  cluster_builder_rd.h                                                  */
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

#include "servers/rendering/renderer_rd/shaders/cluster_debug.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/cluster_render.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/cluster_store.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"

class ClusterBuilderSharedDataRD {
	friend class ClusterBuilderRD;

	RID sphere_vertex_buffer;
	RID sphere_vertex_array;
	RID sphere_index_buffer;
	RID sphere_index_array;
	float sphere_overfit = 0.0; // Because an icosphere is not a perfect sphere, we need to enlarge it to cover the sphere area.

	RID cone_vertex_buffer;
	RID cone_vertex_array;
	RID cone_index_buffer;
	RID cone_index_array;
	float cone_overfit = 0.0; // Because an cone mesh is not a perfect cone, we need to enlarge it to cover the actual cone area.

	RID box_vertex_buffer;
	RID box_vertex_array;
	RID box_index_buffer;
	RID box_index_array;

	enum Divisor {
		DIVISOR_1,
		DIVISOR_2,
		DIVISOR_4,
	};

	struct ClusterRender {
		struct PushConstant {
			uint32_t base_index;
			uint32_t pad0;
			uint32_t pad1;
			uint32_t pad2;
		};

		ClusterRenderShaderRD cluster_render_shader;
		RID shader_version;
		RID shader;

		enum ShaderVariant {
			SHADER_NORMAL,
			SHADER_USE_ATTACHMENT,
		};

		enum PipelineVersion {
			PIPELINE_NORMAL,
			PIPELINE_MSAA,
			PIPELINE_MAX
		};

		RID shader_pipelines[PIPELINE_MAX];
		RID fellow_instanced_pipelines[PIPELINE_MAX]; // FellowEngine (decision 0051).
	} cluster_render;

	struct ClusterStore {
		struct PushConstant {
			uint32_t cluster_render_data_size; // how much data for a single cluster takes
			uint32_t max_render_element_count_div_32; // divided by 32
			uint32_t cluster_screen_size[2];
			uint32_t render_element_count_div_32; // divided by 32
			uint32_t max_cluster_element_count_div_32; // divided by 32

			uint32_t pad1;
			uint32_t pad2;
		};

		ClusterStoreShaderRD cluster_store_shader;
		RID shader_version;
		RID shader;
		RID shader_pipeline;
		RID fellow_parallel_pipeline; // FellowEngine (decision 0014).
		RID fellow_pack_elements_pipeline; // FellowEngine (decision 0051).
	} cluster_store;

	struct ClusterDebug {
		struct PushConstant {
			uint32_t screen_size[2];
			uint32_t cluster_screen_size[2];

			uint32_t cluster_shift;
			uint32_t cluster_type;
			float z_near;
			float z_far;

			uint32_t orthogonal;
			uint32_t max_cluster_element_count_div_32;

			uint32_t fellow_log_depth; // FellowEngine: logarithmic depth slices.
			uint32_t pad2;
		};

		ClusterDebugShaderRD cluster_debug_shader;
		RID shader_version;
		RID shader;
		RID shader_pipeline;
	} cluster_debug;

public:
	ClusterBuilderSharedDataRD();
	~ClusterBuilderSharedDataRD();
};

class ClusterBuilderRD {
public:
	static constexpr float WIDE_SPOT_ANGLE_THRESHOLD_DEG = 60.0f;

	enum LightType {
		LIGHT_TYPE_OMNI,
		LIGHT_TYPE_SPOT,
		LIGHT_TYPE_AREA,
	};

	enum BoxType {
		BOX_TYPE_REFLECTION_PROBE,
		BOX_TYPE_DECAL,
	};

	enum ElementType {
		ELEMENT_TYPE_OMNI_LIGHT,
		ELEMENT_TYPE_SPOT_LIGHT,
		ELEMENT_TYPE_AREA_LIGHT,
		ELEMENT_TYPE_DECAL,
		ELEMENT_TYPE_REFLECTION_PROBE,
		ELEMENT_TYPE_MAX,
	};

private:
	ClusterBuilderSharedDataRD *shared = nullptr;

	struct RenderElementData {
		uint32_t type; // 0-4
		uint32_t touches_near;
		uint32_t touches_far;
		uint32_t original_index;
		float transform_inv[12]; // Transposed transform for less space.
		float scale[3];
		// Bit 0: spot light drawn with the sphere proxy (wide angle). FellowEngine depth slices: bit 1 set when bits 8-15
		// (first slice) and 16-23 (end slice, exclusive) hold the element's conservative depth range.
		uint32_t has_wide_spot_angle;
	}; // Keep aligned to 32 bytes.

	// FellowEngine depth slices (fellow_settings.h, light_cluster_depth_slices): 32 slices logarithmic in view depth
	// between the near and far planes instead of linear over [0, far].
	bool fellow_log_depth = false;
	float fellow_log_scale = 0.0; // Slices per doubling of the view depth.

	// Slice of a view depth (the scene shaders compute the same function).
	_FORCE_INLINE_ uint32_t _fellow_log_slice(float p_depth) const {
		if (!(p_depth > z_near)) {
			return 0;
		}
		const float s = Math::log2(p_depth / z_near) * fellow_log_scale;
		return s >= 31.0f ? 31u : uint32_t(s);
	}

	// Stores the slices covering view depths [p_min, p_max] (a conservative range of everything the element can affect),
	// widened by 0.1 % on each side so that the GPU's evaluation of the slice function can never put a depth inside the
	// range into a slice outside it.
	_FORCE_INLINE_ void _fellow_store_depth_range(RenderElementData &r_e, float p_min, float p_max) const {
		if (!fellow_log_depth) {
			return;
		}
		const uint32_t from = (p_min <= z_near || r_e.touches_near) ? 0u : _fellow_log_slice(p_min * 0.999f);
		const uint32_t to = (p_max >= z_far || r_e.touches_far) ? 32u : _fellow_log_slice(p_max * 1.001f) + 1u;
		r_e.has_wide_spot_angle = (r_e.has_wide_spot_angle & 1u) | 2u | (from << 8) | (to << 16);
	}

	// FellowEngine (decision 0051): the render buffer is all zero (the element pack pass cleared what it read).
	bool fellow_render_buffer_clear = false;

	// FellowEngine (decision 0051): the render pass (one draw per element, or instanced runs of the same proxy mesh) and
	// the pack pass (stock's, decision 0014's or decision 0051's), split out of bake_cluster() for its verification.
	enum FellowPackMode {
		FELLOW_PACK_STOCK,
		FELLOW_PACK_PARALLEL,
		FELLOW_PACK_ELEMENTS,
	};
	void _fellow_render_elements(bool p_instanced, bool p_skip_range_depth);
	void _fellow_pack(FellowPackMode p_mode, bool p_range_depth_skipped);

	// FellowEngine (decision 0051): the proxy mesh an element is drawn with (0 sphere, 1 cone, 2 box).
	static _FORCE_INLINE_ uint32_t _fellow_proxy_mesh(const RenderElementData &p_e) {
		switch (p_e.type) {
			case ELEMENT_TYPE_OMNI_LIGHT:
				return 0;
			case ELEMENT_TYPE_SPOT_LIGHT:
				return (p_e.has_wide_spot_angle & 1u) ? 0 : 1;
			default:
				return 2;
		}
	}

	uint32_t cluster_count_by_type[ELEMENT_TYPE_MAX] = {};
	uint32_t max_elements_by_type = 0;

	RenderElementData *render_elements = nullptr;
	uint32_t render_element_count = 0;
	uint32_t render_element_max = 0;

	Transform3D view_xform;
	Projection adjusted_projection;
	Projection projection;
	float z_far = 0;
	float z_near = 0;
	bool camera_orthogonal = false;

	enum Divisor {
		DIVISOR_1,
		DIVISOR_2,
		DIVISOR_4,
	};

	uint32_t cluster_size = 32;
	bool use_msaa = true;
	Divisor divisor = DIVISOR_4;

	Size2i screen_size;
	Size2i cluster_screen_size;

	RID framebuffer;
	RID cluster_render_buffer; // Used for creating.
	RID cluster_buffer; // Used for rendering.
	RID element_buffer; // Used for storing, to hint element touches far plane or near plane.
	uint32_t cluster_render_buffer_size = 0;
	uint32_t cluster_buffer_size = 0;

	RID cluster_render_uniform_set;
	RID cluster_store_uniform_set;

	// Persistent data.

	void _clear();

	struct StateUniform {
		float projection[16];
		float inv_z_far;
		uint32_t screen_to_clusters_shift; // Shift to obtain coordinates in block indices.
		uint32_t cluster_screen_width;
		uint32_t cluster_data_size; // How much data is needed for a single cluster.
		uint32_t cluster_depth_offset;

		uint32_t pad0;
		uint32_t pad1;
		uint32_t pad2;
	};

	RID state_uniform;

	RID debug_uniform_set;

public:
	void setup(Size2i p_screen_size, uint32_t p_max_elements, RID p_depth_buffer, RID p_depth_buffer_sampler, RID p_color_buffer);

	// p_fellow_log_depth: FellowEngine depth slices (applied to perspective projections only; see fellow_get_log_depth()).
	void begin(const Transform3D &p_view_transform, const Projection &p_cam_projection, bool p_flip_y, bool p_fellow_log_depth = false);
	// Whether the clusters of this frame use FellowEngine depth slices (the shaders must use the same slice function).
	bool fellow_get_log_depth() const { return fellow_log_depth; }
	// Near plane of the logarithmic slices, 0 for stock's linear slices (volumetric fog).
	float fellow_get_log_z_near() const { return fellow_log_depth ? z_near : 0.0f; }

	_FORCE_INLINE_ void add_light(LightType p_type, const Transform3D &p_transform, float p_radius, float p_spot_aperture, const Vector2 &p_area_size) {
		if (p_type == LIGHT_TYPE_OMNI && cluster_count_by_type[ELEMENT_TYPE_OMNI_LIGHT] == max_elements_by_type) {
			return; // Max number elements reached.
		}
		if (p_type == LIGHT_TYPE_SPOT && cluster_count_by_type[ELEMENT_TYPE_SPOT_LIGHT] == max_elements_by_type) {
			return; // Max number elements reached.
		}
		if (p_type == LIGHT_TYPE_AREA && cluster_count_by_type[ELEMENT_TYPE_AREA_LIGHT] == max_elements_by_type) {
			return; // Max number elements reached.
		}

		RenderElementData &e = render_elements[render_element_count];

		Transform3D xform = view_xform * p_transform;

		float radius = xform.basis.get_uniform_scale();
		if (radius < 0.98 || radius > 1.02) {
			xform.basis.orthonormalize();
		}

		radius *= p_radius;

		// Spotlights with wide angle are trated as Omni lights.
		// If the spot angle is above the threshold, we need a sphere instead of a cone for building the clusters
		// since the cone gets too flat/large (spot angle close to 90 degrees) or
		// can't even cover the affected area of the light (spot angle above 90 degrees).
		if (p_type == LIGHT_TYPE_OMNI || (p_type == LIGHT_TYPE_SPOT && p_spot_aperture > WIDE_SPOT_ANGLE_THRESHOLD_DEG)) {
			radius *= shared->sphere_overfit; // Overfit icosphere.

			float depth = -xform.origin.z;
			if (camera_orthogonal) {
				e.touches_near = (depth - radius) < z_near;
			} else {
				// Contains camera inside light.
				float radius2 = radius * shared->sphere_overfit; // Overfit again for outer size (camera may be outside actual sphere but behind an icosphere vertex)
				e.touches_near = xform.origin.length_squared() < radius2 * radius2;
			}

			e.touches_far = (depth + radius) > z_far;
			e.scale[0] = radius;
			e.scale[1] = radius;
			e.scale[2] = radius;
			if (p_type == LIGHT_TYPE_OMNI) {
				e.type = ELEMENT_TYPE_OMNI_LIGHT;
				e.has_wide_spot_angle = 0;
				e.original_index = cluster_count_by_type[ELEMENT_TYPE_OMNI_LIGHT];
				cluster_count_by_type[ELEMENT_TYPE_OMNI_LIGHT]++;
			} else { // LIGHT_TYPE_SPOT with wide angle.
				e.type = ELEMENT_TYPE_SPOT_LIGHT;
				e.has_wide_spot_angle = 1;
				e.original_index = cluster_count_by_type[ELEMENT_TYPE_SPOT_LIGHT];
				cluster_count_by_type[ELEMENT_TYPE_SPOT_LIGHT]++;
			}
			if (fellow_log_depth) {
				// The shaders attenuate with the light's range, not scaled by its transform: cover both.
				const float r = MAX(radius, p_radius * shared->sphere_overfit);
				_fellow_store_depth_range(e, depth - r, depth + r);
			}

			RendererRD::MaterialStorage::store_transform_transposed_3x4(xform, e.transform_inv);

		} else if (p_type == LIGHT_TYPE_SPOT) { /*LIGHT_TYPE_SPOT with no wide angle*/
			radius *= shared->cone_overfit; // Overfit cone.

			real_t len = Math::tan(Math::deg_to_rad(p_spot_aperture)) * radius;
			// Approximate, probably better to use a cone support function.
			float max_d = -1e20;
			float min_d = 1e20;
#define CONE_MINMAX(m_x, m_y) \
	{ \
		float d = -xform.xform(Vector3(len * m_x, len * m_y, -radius)).z; \
		min_d = MIN(d, min_d); \
		max_d = MAX(d, max_d); \
	}

			CONE_MINMAX(1, 1);
			CONE_MINMAX(-1, 1);
			CONE_MINMAX(-1, -1);
			CONE_MINMAX(1, -1);

			if (camera_orthogonal) {
				e.touches_near = min_d < z_near;
			} else {
				Plane base_plane(-xform.basis.get_column(Vector3::AXIS_Z), xform.origin);
				float dist = base_plane.distance_to(Vector3());
				if (dist >= 0 && dist < radius) {
					// Contains camera inside light, check angle.
					float angle = Math::rad_to_deg(Math::acos((-xform.origin.normalized()).dot(-xform.basis.get_column(Vector3::AXIS_Z))));
					e.touches_near = angle < p_spot_aperture * 1.05; //overfit aperture a little due to cone overfit
				} else {
					e.touches_near = false;
				}
			}

			e.touches_far = max_d > z_far;
			e.scale[0] = len * shared->cone_overfit;
			e.scale[1] = len * shared->cone_overfit;
			e.scale[2] = radius;
			e.has_wide_spot_angle = 0;
			e.type = ELEMENT_TYPE_SPOT_LIGHT;
			e.original_index = cluster_count_by_type[ELEMENT_TYPE_SPOT_LIGHT];
			if (fellow_log_depth) {
				// Depth range of the proxy cone (apex and base disc), which contains the lit region (range from the apex
				// within the spot angle; scaled up to the unscaled range if the transform shrinks it).
				const float k = MAX(1.0f, p_radius * shared->cone_overfit / MAX(radius, 1e-6f));
				const float h = radius * k;
				const float rho = len * shared->cone_overfit * k;
				const Vector3 axis = -xform.basis.get_column(Vector3::AXIS_Z).normalized();
				const float apex_d = -xform.origin.z;
				const float base_d = apex_d - axis.z * h;
				const float spread = rho * Math::sqrt(MAX(0.0f, 1.0f - axis.z * axis.z));
				_fellow_store_depth_range(e, MIN(apex_d, base_d - spread), MAX(apex_d, base_d + spread));
			}

			RendererRD::MaterialStorage::store_transform_transposed_3x4(xform, e.transform_inv);

			cluster_count_by_type[ELEMENT_TYPE_SPOT_LIGHT]++;
		} else { /* LIGHT_TYPE_AREA */
			Vector3 scale = Vector3(p_area_size.x / 2.0 + radius, p_area_size.y / 2.0 + radius, radius / 2.0);

			for (uint32_t i = 0; i < 3; i++) {
				float s = xform.basis.rows[i].length();
				//scale[i] *= s; // lights ignore scale
				xform.basis.rows[i] /= s;
			}
			xform.origin -= xform.basis.get_column(Vector3::AXIS_Z) * scale.z; // translate center to center of box

			float depth = -xform.origin.z;
			float box_depth = Math::abs(xform.basis.xform_inv(Vector3(0, 0, -1)).dot(scale));

			if (camera_orthogonal) {
				e.touches_near = (depth - box_depth) < z_near;
			} else {
				// Contains camera inside box.
				Vector3 inside = xform.xform_inv(Vector3(0, 0, 0)).abs();
				e.touches_near = inside.x < scale.x && inside.y < scale.y && inside.z < scale.z;
			}

			e.touches_far = depth + box_depth > z_far;

			e.scale[0] = scale.x;
			e.scale[1] = scale.y;
			e.scale[2] = scale.z;
			e.has_wide_spot_angle = 0;
			if (radius >= p_radius * 0.999f) {
				// (A transform scale below 1 shrinks the proxy box below the lit region: keep stock's range then.)
				// View-depth extent of the proxy box (box_depth above projects the extents with signs).
				const float fellow_box_depth = Math::abs(xform.basis.rows[2][0]) * scale.x + Math::abs(xform.basis.rows[2][1]) * scale.y + Math::abs(xform.basis.rows[2][2]) * scale.z;
				_fellow_store_depth_range(e, depth - fellow_box_depth, depth + fellow_box_depth);
			}

			e.type = ELEMENT_TYPE_AREA_LIGHT;
			e.original_index = cluster_count_by_type[ELEMENT_TYPE_AREA_LIGHT];

			RendererRD::MaterialStorage::store_transform_transposed_3x4(xform, e.transform_inv);

			cluster_count_by_type[ELEMENT_TYPE_AREA_LIGHT]++;
		}

		render_element_count++;
	}

	_FORCE_INLINE_ void add_box(BoxType p_box_type, const Transform3D &p_transform, const Vector3 &p_half_size) {
		if (p_box_type == BOX_TYPE_DECAL && cluster_count_by_type[ELEMENT_TYPE_DECAL] == max_elements_by_type) {
			return; // Max number elements reached.
		}
		if (p_box_type == BOX_TYPE_REFLECTION_PROBE && cluster_count_by_type[ELEMENT_TYPE_REFLECTION_PROBE] == max_elements_by_type) {
			return; // Max number elements reached.
		}

		RenderElementData &e = render_elements[render_element_count];
		Transform3D xform = view_xform * p_transform;

		// FellowEngine depth slices: view-depth extent of the box itself (sum of its scaled axes' depth components).
		const float fellow_box_depth = Math::abs(xform.basis.rows[2][0]) * p_half_size.x + Math::abs(xform.basis.rows[2][1]) * p_half_size.y + Math::abs(xform.basis.rows[2][2]) * p_half_size.z;
		const float fellow_box_center = -xform.origin.z;

		// Extract scale and scale the matrix by it, makes things simpler.
		Vector3 scale = p_half_size;
		for (uint32_t i = 0; i < 3; i++) {
			float s = xform.basis.rows[i].length();
			scale[i] *= s;
			xform.basis.rows[i] /= s;
		};

		float box_depth = Math::abs(xform.basis.xform_inv(Vector3(0, 0, -1)).dot(scale));
		float depth = -xform.origin.z;

		if (camera_orthogonal) {
			e.touches_near = depth - box_depth < z_near;
		} else {
			// Contains camera inside box.
			Vector3 inside = xform.xform_inv(Vector3(0, 0, 0)).abs();
			e.touches_near = inside.x < scale.x && inside.y < scale.y && inside.z < scale.z;
		}

		e.touches_far = depth + box_depth > z_far;

		e.scale[0] = scale.x;
		e.scale[1] = scale.y;
		e.scale[2] = scale.z;
		e.has_wide_spot_angle = 0;
		// (Not box_depth above: that projects the extents with signs and can be smaller than the box's real extent.)
		_fellow_store_depth_range(e, fellow_box_center - fellow_box_depth, fellow_box_center + fellow_box_depth);

		e.type = (p_box_type == BOX_TYPE_DECAL) ? ELEMENT_TYPE_DECAL : ELEMENT_TYPE_REFLECTION_PROBE;
		e.original_index = cluster_count_by_type[e.type];

		RendererRD::MaterialStorage::store_transform_transposed_3x4(xform, e.transform_inv);

		cluster_count_by_type[e.type]++;
		render_element_count++;
	}

	_FORCE_INLINE_ uint32_t get_cluster_count_by_type(ElementType p_element_type) const {
		DEV_ASSERT(p_element_type < ELEMENT_TYPE_MAX);
		return cluster_count_by_type[p_element_type];
	}

	void bake_cluster();
	void debug(ElementType p_element);

	RID get_cluster_buffer() const;
	uint32_t get_cluster_size() const;
	uint32_t get_max_cluster_elements() const;

	void set_shared(ClusterBuilderSharedDataRD *p_shared);

	ClusterBuilderRD();
	~ClusterBuilderRD();
};
