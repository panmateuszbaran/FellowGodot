/**************************************************************************/
/*  motion_vectors_store.cpp                                              */
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

#include "motion_vectors_store.h"

#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

namespace RendererRD {

MotionVectorsStore::MotionVectorsStore() {
	Vector<String> modes;
	modes.push_back("");

	motion_shader.initialize(modes);
	shader_version = motion_shader.version_create();

	pipeline = RD::get_singleton()->compute_pipeline_create(motion_shader.version_get_shader(shader_version, 0));
}

MotionVectorsStore::~MotionVectorsStore() {
	motion_shader.version_free(shader_version);
}

void MotionVectorsStore::process(Ref<RenderSceneBuffersRD> p_render_buffers,
		const Projection &p_current_projection, const Transform3D &p_current_transform,
		const Projection &p_previous_projection, const Transform3D &p_previous_transform) {
	Size2i internal_size = p_render_buffers->get_internal_size();

	PushConstant push_constant;
	{
		push_constant.resolution[0] = internal_size.width;
		push_constant.resolution[1] = internal_size.height;
		push_constant.pad[0] = 0;
		push_constant.pad[1] = 0;

		Projection correction;
		correction.set_depth_correction(true, true, false);
		Projection reprojection = (correction * p_previous_projection) * p_previous_transform.affine_inverse() * p_current_transform * (correction * p_current_projection).inverse();
		RendererRD::MaterialStorage::store_camera(reprojection, push_constant.reprojection_matrix);
	}

	_dispatch(p_render_buffers, push_constant);
}

namespace {
// Column-major 4x4 in double precision (FellowEngine, decision 0013): the reprojection of a static camera must come
// out as the identity to well below the velocity buffer's half-float resolution.
struct FellowMat4d {
	double m[4][4]; // m[column][row]

	static FellowMat4d from_projection(const Projection &p_p) {
		FellowMat4d r;
		for (int c = 0; c < 4; c++) {
			for (int k = 0; k < 4; k++) {
				r.m[c][k] = double(p_p.columns[c][k]);
			}
		}
		return r;
	}

	static FellowMat4d from_transform(const Transform3D &p_t) {
		FellowMat4d r;
		for (int c = 0; c < 3; c++) {
			for (int k = 0; k < 3; k++) {
				r.m[c][k] = double(p_t.basis.rows[k][c]);
			}
			r.m[c][3] = 0.0;
		}
		r.m[3][0] = double(p_t.origin.x);
		r.m[3][1] = double(p_t.origin.y);
		r.m[3][2] = double(p_t.origin.z);
		r.m[3][3] = 1.0;
		return r;
	}

	FellowMat4d operator*(const FellowMat4d &p_b) const {
		FellowMat4d r;
		for (int c = 0; c < 4; c++) {
			for (int k = 0; k < 4; k++) {
				double v = 0.0;
				for (int i = 0; i < 4; i++) {
					v += m[i][k] * p_b.m[c][i];
				}
				r.m[c][k] = v;
			}
		}
		return r;
	}

	// Gauss-Jordan with partial pivoting.
	FellowMat4d inverse() const {
		double a[4][8];
		for (int k = 0; k < 4; k++) {
			for (int c = 0; c < 4; c++) {
				a[k][c] = m[c][k];
				a[k][c + 4] = (k == c) ? 1.0 : 0.0;
			}
		}
		for (int c = 0; c < 4; c++) {
			int piv = c;
			for (int k = c + 1; k < 4; k++) {
				if (Math::abs(a[k][c]) > Math::abs(a[piv][c])) {
					piv = k;
				}
			}
			if (piv != c) {
				for (int j = 0; j < 8; j++) {
					SWAP(a[c][j], a[piv][j]);
				}
			}
			const double d = a[c][c];
			for (int j = 0; j < 8; j++) {
				a[c][j] /= d;
			}
			for (int k = 0; k < 4; k++) {
				if (k != c) {
					const double f = a[k][c];
					for (int j = 0; j < 8; j++) {
						a[k][j] -= f * a[c][j];
					}
				}
			}
		}
		FellowMat4d r;
		for (int k = 0; k < 4; k++) {
			for (int c = 0; c < 4; c++) {
				r.m[c][k] = a[k][c + 4];
			}
		}
		return r;
	}
};
} // namespace

void MotionVectorsStore::process_fellow_taa(Ref<RenderSceneBuffersRD> p_render_buffers,
		const Projection &p_current_projection, const Transform3D &p_current_transform, const Vector2 &p_current_jitter,
		const Projection &p_previous_projection, const Transform3D &p_previous_transform) {
	Size2i internal_size = p_render_buffers->get_internal_size();

	// The scene shader writes 0.5 * ((prev_clip.xy / prev_clip.w - prev_jitter) - (clip.xy / clip.w - jitter)), both
	// clip positions from the jittered projections. Here a pixel centre's NDC and its depth are taken back through the
	// current jittered projection and forward through the previous projection without jitter; adding the current
	// jitter to the latter (its correction matrix) turns the shader's derive_motion_vector() (0.5 + 0.5 * prev_ndc - uv)
	// into exactly that expression.
	Projection correction;
	correction.set_depth_correction(true, true, false);
	correction.add_jitter_offset(p_current_jitter);
	const FellowMat4d c = FellowMat4d::from_projection(correction);
	const FellowMat4d reprojection = (c * FellowMat4d::from_projection(p_previous_projection)) *
			(FellowMat4d::from_transform(p_previous_transform).inverse() * FellowMat4d::from_transform(p_current_transform)) *
			(c * FellowMat4d::from_projection(p_current_projection)).inverse();

	PushConstant push_constant;
	for (int col = 0; col < 4; col++) {
		for (int k = 0; k < 4; k++) {
			push_constant.reprojection_matrix[col * 4 + k] = float(reprojection.m[col][k]);
		}
	}
	push_constant.resolution[0] = internal_size.width;
	push_constant.resolution[1] = internal_size.height;
	push_constant.pad[0] = 1; // Velocity 0 where the depth is still cleared.
	push_constant.pad[1] = 0;

	_dispatch(p_render_buffers, push_constant);
}

void MotionVectorsStore::_dispatch(Ref<RenderSceneBuffersRD> p_render_buffers, const PushConstant &p_push_constant) {
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);

	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);

	uint32_t view_count = p_render_buffers->get_view_count();
	Size2i internal_size = p_render_buffers->get_internal_size();

	RID default_sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	RD::get_singleton()->draw_command_begin_label("Motion Vector Store");

	RID shader = motion_shader.version_get_shader(shader_version, 0);
	ERR_FAIL_COND(shader.is_null());

	RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipeline);

	for (uint32_t v = 0; v < view_count; v++) {
		RID velocity = p_render_buffers->get_velocity_buffer(false, v);
		RID depth = p_render_buffers->get_depth_texture(v);
		RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ default_sampler, depth }));
		RD::Uniform u_velocity(RD::UNIFORM_TYPE_IMAGE, 1, velocity);

		RID uniform_set = uniform_set_cache->get_cache(shader, 0, u_depth, u_velocity);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
		RD::get_singleton()->compute_list_set_push_constant(compute_list, &p_push_constant, sizeof(PushConstant));
		RD::get_singleton()->compute_list_dispatch_threads(compute_list, internal_size.width, internal_size.height, 1);
	}

	RD::get_singleton()->compute_list_end();

	RD::get_singleton()->draw_command_end_label();
}

} //namespace RendererRD
