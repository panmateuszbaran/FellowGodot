/**************************************************************************/
/*  godot_collision_solver_3d.cpp                                         */
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

#include "godot_collision_solver_3d.h"

#include "gjk_epa.h"
#include "godot_collision_solver_3d_sat.h"
#include "godot_soft_body_3d.h"

#include "servers/rendering/fellow_settings.h"

#define collision_solver sat_calculate_penetration
//#define collision_solver gjk_epa_calculate_penetration

bool GodotCollisionSolver3D::solve_static_world_boundary(const GodotShape3D *p_shape_A, const Transform3D &p_transform_A, const GodotShape3D *p_shape_B, const Transform3D &p_transform_B, CallbackResult p_result_callback, void *p_userdata, bool p_swap_result, real_t p_margin) {
	const GodotWorldBoundaryShape3D *world_boundary = static_cast<const GodotWorldBoundaryShape3D *>(p_shape_A);
	if (p_shape_B->get_type() == PhysicsServer3D::SHAPE_WORLD_BOUNDARY) {
		return false;
	}
	Plane p = p_transform_A.xform(world_boundary->get_plane());

	static const int max_supports = 16;
	Vector3 supports[max_supports];
	int support_count;
	GodotShape3D::FeatureType support_type = GodotShape3D::FeatureType::FEATURE_POINT;
	p_shape_B->get_supports(p_transform_B.basis.xform_inv(-p.normal).normalized(), max_supports, supports, support_count, support_type);

	if (support_type == GodotShape3D::FEATURE_CIRCLE) {
		ERR_FAIL_COND_V(support_count != 3, false);

		Vector3 circle_pos = supports[0];
		Vector3 circle_axis_1 = supports[1] - circle_pos;
		Vector3 circle_axis_2 = supports[2] - circle_pos;

		// Use 3 equidistant points on the circle.
		for (int i = 0; i < 3; ++i) {
			Vector3 vertex_pos = circle_pos;
			vertex_pos += circle_axis_1 * Math::cos(2.0 * Math::PI * i / 3.0);
			vertex_pos += circle_axis_2 * Math::sin(2.0 * Math::PI * i / 3.0);
			supports[i] = vertex_pos;
		}
	}

	bool found = false;

	for (int i = 0; i < support_count; i++) {
		supports[i] += p_margin * supports[i].normalized();
		supports[i] = p_transform_B.xform(supports[i]);
		if (p.distance_to(supports[i]) >= 0) {
			continue;
		}
		found = true;

		Vector3 support_A = p.project(supports[i]);

		if (p_result_callback) {
			if (p_swap_result) {
				Vector3 normal = (support_A - supports[i]).normalized();
				p_result_callback(supports[i], 0, support_A, 0, normal, p_userdata);
			} else {
				Vector3 normal = (supports[i] - support_A).normalized();
				p_result_callback(support_A, 0, supports[i], 0, normal, p_userdata);
			}
		}
	}

	return found;
}

bool GodotCollisionSolver3D::solve_separation_ray(const GodotShape3D *p_shape_A, const Transform3D &p_transform_A, const GodotShape3D *p_shape_B, const Transform3D &p_transform_B, CallbackResult p_result_callback, void *p_userdata, bool p_swap_result, real_t p_margin) {
	const GodotSeparationRayShape3D *ray = static_cast<const GodotSeparationRayShape3D *>(p_shape_A);

	Vector3 from = p_transform_A.origin;
	Vector3 to = from + p_transform_A.basis.get_column(2) * (ray->get_length() + p_margin);
	Vector3 support_A = to;

	Transform3D ai = p_transform_B.affine_inverse();

	from = ai.xform(from);
	to = ai.xform(to);

	Vector3 p, n;
	int fi = -1;
	if (!p_shape_B->intersect_segment(from, to, p, n, fi, true)) {
		return false;
	}

	// Discard contacts when the ray is fully contained inside the shape.
	if (n == Vector3()) {
		return false;
	}

	// Discard contacts in the wrong direction.
	if (n.dot(from - to) < CMP_EPSILON) {
		return false;
	}

	Vector3 support_B = p_transform_B.xform(p);
	if (ray->get_slide_on_slope()) {
		Vector3 global_n = ai.basis.xform_inv(n).normalized();
		support_B = support_A + (support_B - support_A).length() * global_n;
	}

	if (p_result_callback) {
		Vector3 normal = (support_B - support_A).normalized();
		if (p_swap_result) {
			p_result_callback(support_B, 0, support_A, 0, -normal, p_userdata);
		} else {
			p_result_callback(support_A, 0, support_B, 0, normal, p_userdata);
		}
	}
	return true;
}

struct _SoftBodyContactCollisionInfo {
	int node_index = 0;
	GodotCollisionSolver3D::CallbackResult result_callback = nullptr;
	void *userdata = nullptr;
	bool swap_result = false;
	int contact_count = 0;
};

void GodotCollisionSolver3D::soft_body_contact_callback(const Vector3 &p_point_A, int p_index_A, const Vector3 &p_point_B, int p_index_B, const Vector3 &normal, void *p_userdata) {
	_SoftBodyContactCollisionInfo &cinfo = *(static_cast<_SoftBodyContactCollisionInfo *>(p_userdata));

	++cinfo.contact_count;

	if (!cinfo.result_callback) {
		return;
	}

	if (cinfo.swap_result) {
		cinfo.result_callback(p_point_B, cinfo.node_index, p_point_A, p_index_A, -normal, cinfo.userdata);
	} else {
		cinfo.result_callback(p_point_A, p_index_A, p_point_B, cinfo.node_index, normal, cinfo.userdata);
	}
}

struct _SoftBodyQueryInfo {
	GodotSoftBody3D *soft_body = nullptr;
	const GodotShape3D *shape_A = nullptr;
	const GodotShape3D *shape_B = nullptr;
	Transform3D transform_A;
	Transform3D node_transform;
	_SoftBodyContactCollisionInfo contact_info;
#ifdef DEBUG_ENABLED
	int node_query_count = 0;
	int convex_query_count = 0;
#endif
};

bool GodotCollisionSolver3D::soft_body_query_callback(uint32_t p_node_index, void *p_userdata) {
	_SoftBodyQueryInfo &query_cinfo = *(static_cast<_SoftBodyQueryInfo *>(p_userdata));

	Vector3 node_position = query_cinfo.soft_body->get_node_position(p_node_index);

	Transform3D transform_B;
	transform_B.origin = query_cinfo.node_transform.xform(node_position);

	query_cinfo.contact_info.node_index = p_node_index;
	bool collided = solve_static(query_cinfo.shape_A, query_cinfo.transform_A, query_cinfo.shape_B, transform_B, soft_body_contact_callback, &query_cinfo.contact_info);

#ifdef DEBUG_ENABLED
	++query_cinfo.node_query_count;
#endif

	// Stop at first collision if contacts are not needed.
	return (collided && !query_cinfo.contact_info.result_callback);
}

bool GodotCollisionSolver3D::soft_body_concave_callback(void *p_userdata, GodotShape3D *p_convex) {
	_SoftBodyQueryInfo &query_cinfo = *(static_cast<_SoftBodyQueryInfo *>(p_userdata));

	query_cinfo.shape_A = p_convex;

	// Calculate AABB for internal soft body query (in world space).
	AABB shape_aabb;
	for (int i = 0; i < 3; i++) {
		Vector3 axis;
		axis[i] = 1.0;

		real_t smin, smax;
		p_convex->project_range(axis, query_cinfo.transform_A, smin, smax);

		shape_aabb.position[i] = smin;
		shape_aabb.size[i] = smax - smin;
	}

	shape_aabb.grow_by(query_cinfo.soft_body->get_collision_margin());

	query_cinfo.soft_body->query_aabb(shape_aabb, soft_body_query_callback, &query_cinfo);

	bool collided = (query_cinfo.contact_info.contact_count > 0);

#ifdef DEBUG_ENABLED
	++query_cinfo.convex_query_count;
#endif

	// Stop at first collision if contacts are not needed.
	return (collided && !query_cinfo.contact_info.result_callback);
}

bool GodotCollisionSolver3D::solve_soft_body(const GodotShape3D *p_shape_A, const Transform3D &p_transform_A, const GodotShape3D *p_shape_B, const Transform3D &p_transform_B, CallbackResult p_result_callback, void *p_userdata, bool p_swap_result) {
	const GodotSoftBodyShape3D *soft_body_shape_B = static_cast<const GodotSoftBodyShape3D *>(p_shape_B);

	GodotSoftBody3D *soft_body = soft_body_shape_B->get_soft_body();
	const Transform3D &world_to_local = soft_body->get_inv_transform();

	const real_t collision_margin = soft_body->get_collision_margin();

	GodotSphereShape3D sphere_shape;
	sphere_shape.set_data(collision_margin);

	_SoftBodyQueryInfo query_cinfo;
	query_cinfo.contact_info.result_callback = p_result_callback;
	query_cinfo.contact_info.userdata = p_userdata;
	query_cinfo.contact_info.swap_result = p_swap_result;
	query_cinfo.soft_body = soft_body;
	query_cinfo.node_transform = p_transform_B * world_to_local;
	query_cinfo.shape_A = p_shape_A;
	query_cinfo.transform_A = p_transform_A;
	query_cinfo.shape_B = &sphere_shape;

	if (p_shape_A->is_concave()) {
		// In case of concave shape, query convex shapes first.
		const GodotConcaveShape3D *concave_shape_A = static_cast<const GodotConcaveShape3D *>(p_shape_A);

		AABB soft_body_aabb = soft_body->get_bounds();
		soft_body_aabb.grow_by(collision_margin);

		// Calculate AABB for internal concave shape query (in local space).
		AABB local_aabb;
		for (int i = 0; i < 3; i++) {
			Vector3 axis(p_transform_A.basis.get_column(i));
			real_t axis_scale = 1.0 / axis.length();

			real_t smin = soft_body_aabb.position[i];
			real_t smax = smin + soft_body_aabb.size[i];

			smin *= axis_scale;
			smax *= axis_scale;

			local_aabb.position[i] = smin;
			local_aabb.size[i] = smax - smin;
		}

		concave_shape_A->cull(local_aabb, soft_body_concave_callback, &query_cinfo, true);
	} else {
		AABB shape_aabb = p_transform_A.xform(p_shape_A->get_aabb());
		shape_aabb.grow_by(collision_margin);

		soft_body->query_aabb(shape_aabb, soft_body_query_callback, &query_cinfo);
	}

	return (query_cinfo.contact_info.contact_count > 0);
}

struct _ConcaveCollisionInfo {
	const Transform3D *transform_A = nullptr;
	const GodotShape3D *shape_A = nullptr;
	const Transform3D *transform_B = nullptr;
	GodotCollisionSolver3D::CallbackResult result_callback = nullptr;
	void *userdata = nullptr;
	bool swap_result = false;
	bool collided = false;
	int aabb_tests = 0;
	int collisions = 0;
	bool tested = false;
	real_t margin_A = 0.0f;
	real_t margin_B = 0.0f;
	Vector3 close_A;
	Vector3 close_B;
};

bool GodotCollisionSolver3D::concave_callback(void *p_userdata, GodotShape3D *p_convex) {
	_ConcaveCollisionInfo &cinfo = *(static_cast<_ConcaveCollisionInfo *>(p_userdata));
	cinfo.aabb_tests++;

	bool collided = collision_solver(cinfo.shape_A, *cinfo.transform_A, p_convex, *cinfo.transform_B, cinfo.result_callback, cinfo.userdata, cinfo.swap_result, nullptr, cinfo.margin_A, cinfo.margin_B);
	if (!collided) {
		return false;
	}

	cinfo.collided = true;
	cinfo.collisions++;

	// Stop at first collision if contacts are not needed.
	return !cinfo.result_callback;
}

bool GodotCollisionSolver3D::solve_concave(const GodotShape3D *p_shape_A, const Transform3D &p_transform_A, const GodotShape3D *p_shape_B, const Transform3D &p_transform_B, CallbackResult p_result_callback, void *p_userdata, bool p_swap_result, real_t p_margin_A, real_t p_margin_B) {
	const GodotConcaveShape3D *concave_B = static_cast<const GodotConcaveShape3D *>(p_shape_B);

	_ConcaveCollisionInfo cinfo;
	cinfo.transform_A = &p_transform_A;
	cinfo.shape_A = p_shape_A;
	cinfo.transform_B = &p_transform_B;
	cinfo.result_callback = p_result_callback;
	cinfo.userdata = p_userdata;
	cinfo.swap_result = p_swap_result;
	cinfo.collided = false;
	cinfo.collisions = 0;
	cinfo.margin_A = p_margin_A;
	cinfo.margin_B = p_margin_B;

	cinfo.aabb_tests = 0;

	Transform3D rel_transform = p_transform_A;
	rel_transform.origin -= p_transform_B.origin;

	//quickly compute a local AABB

	AABB local_aabb;
	for (int i = 0; i < 3; i++) {
		Vector3 axis(p_transform_B.basis.get_column(i));
		real_t axis_scale = 1.0 / axis.length();
		axis *= axis_scale;

		real_t smin = 0.0, smax = 0.0;
		p_shape_A->project_range(axis, rel_transform, smin, smax);
		smin -= p_margin_A;
		smax += p_margin_A;
		smin *= axis_scale;
		smax *= axis_scale;

		local_aabb.position[i] = smin;
		local_aabb.size[i] = smax - smin;
	}

	concave_B->cull(local_aabb, concave_callback, &cinfo, false);

	return cinfo.collided;
}

bool GodotCollisionSolver3D::solve_static(const GodotShape3D *p_shape_A, const Transform3D &p_transform_A, const GodotShape3D *p_shape_B, const Transform3D &p_transform_B, CallbackResult p_result_callback, void *p_userdata, Vector3 *r_sep_axis, real_t p_margin_A, real_t p_margin_B) {
	PhysicsServer3D::ShapeType type_A = p_shape_A->get_type();
	PhysicsServer3D::ShapeType type_B = p_shape_B->get_type();
	bool concave_A = p_shape_A->is_concave();
	bool concave_B = p_shape_B->is_concave();

	bool swap = false;

	if (type_A > type_B) {
		SWAP(type_A, type_B);
		SWAP(concave_A, concave_B);
		swap = true;
	}

	if (type_A == PhysicsServer3D::SHAPE_WORLD_BOUNDARY) {
		if (type_B == PhysicsServer3D::SHAPE_WORLD_BOUNDARY) {
			WARN_PRINT_ONCE("Collisions between world boundaries are not supported.");
			return false;
		}
		if (type_B == PhysicsServer3D::SHAPE_SEPARATION_RAY) {
			WARN_PRINT_ONCE("Collisions between world boundaries and rays are not supported.");
			return false;
		}
		if (type_B == PhysicsServer3D::SHAPE_SOFT_BODY) {
			WARN_PRINT_ONCE("Collisions between world boundaries and soft bodies are not supported.");
			return false;
		}

		if (swap) {
			return solve_static_world_boundary(p_shape_B, p_transform_B, p_shape_A, p_transform_A, p_result_callback, p_userdata, true, p_margin_A);
		} else {
			return solve_static_world_boundary(p_shape_A, p_transform_A, p_shape_B, p_transform_B, p_result_callback, p_userdata, false, p_margin_B);
		}

	} else if (type_A == PhysicsServer3D::SHAPE_SEPARATION_RAY) {
		if (type_B == PhysicsServer3D::SHAPE_SEPARATION_RAY) {
			WARN_PRINT_ONCE("Collisions between rays are not supported.");
			return false;
		}

		if (swap) {
			return solve_separation_ray(p_shape_B, p_transform_B, p_shape_A, p_transform_A, p_result_callback, p_userdata, true, p_margin_B);
		} else {
			return solve_separation_ray(p_shape_A, p_transform_A, p_shape_B, p_transform_B, p_result_callback, p_userdata, false, p_margin_A);
		}

	} else if (type_B == PhysicsServer3D::SHAPE_SOFT_BODY) {
		if (type_A == PhysicsServer3D::SHAPE_SOFT_BODY) {
			WARN_PRINT_ONCE("Collisions between soft bodies are not supported.");
			return false;
		}

		if (swap) {
			return solve_soft_body(p_shape_B, p_transform_B, p_shape_A, p_transform_A, p_result_callback, p_userdata, true);
		} else {
			return solve_soft_body(p_shape_A, p_transform_A, p_shape_B, p_transform_B, p_result_callback, p_userdata, false);
		}

	} else if (concave_B) {
		if (concave_A) {
			WARN_PRINT_ONCE("Collisions between two concave shapes are not supported.");
			return false;
		}

		if (!swap) {
			return solve_concave(p_shape_A, p_transform_A, p_shape_B, p_transform_B, p_result_callback, p_userdata, false, p_margin_A, p_margin_B);
		} else {
			return solve_concave(p_shape_B, p_transform_B, p_shape_A, p_transform_A, p_result_callback, p_userdata, true, p_margin_A, p_margin_B);
		}

	} else {
		return collision_solver(p_shape_A, p_transform_A, p_shape_B, p_transform_B, p_result_callback, p_userdata, false, r_sep_axis, p_margin_A, p_margin_B);
	}
}

bool GodotCollisionSolver3D::concave_distance_callback(void *p_userdata, GodotShape3D *p_convex) {
	_ConcaveCollisionInfo &cinfo = *(static_cast<_ConcaveCollisionInfo *>(p_userdata));
	cinfo.aabb_tests++;

	Vector3 close_A, close_B;
	cinfo.collided = !gjk_epa_calculate_distance(cinfo.shape_A, *cinfo.transform_A, p_convex, *cinfo.transform_B, close_A, close_B);

	if (cinfo.collided) {
		// No need to process any more result.
		return true;
	}

	if (!cinfo.tested || close_A.distance_squared_to(close_B) < cinfo.close_A.distance_squared_to(cinfo.close_B)) {
		cinfo.close_A = close_A;
		cinfo.close_B = close_B;
		cinfo.tested = true;
	}

	cinfo.collisions++;
	return false;
}

bool GodotCollisionSolver3D::solve_distance_world_boundary(const GodotShape3D *p_shape_A, const Transform3D &p_transform_A, const GodotShape3D *p_shape_B, const Transform3D &p_transform_B, Vector3 &r_point_A, Vector3 &r_point_B) {
	const GodotWorldBoundaryShape3D *world_boundary = static_cast<const GodotWorldBoundaryShape3D *>(p_shape_A);
	if (p_shape_B->get_type() == PhysicsServer3D::SHAPE_WORLD_BOUNDARY) {
		return false;
	}
	Plane p = p_transform_A.xform(world_boundary->get_plane());

	static const int max_supports = 16;
	Vector3 supports[max_supports];
	int support_count;
	GodotShape3D::FeatureType support_type;
	Vector3 support_direction = p_transform_B.basis.xform_inv(-p.normal).normalized();

	p_shape_B->get_supports(support_direction, max_supports, supports, support_count, support_type);

	if (support_count == 0) { // This is a poor man's way to detect shapes that don't implement get_supports, such as GodotMotionShape3D.
		Vector3 support_B = p_transform_B.xform(p_shape_B->get_support(support_direction));
		r_point_A = p.project(support_B);
		r_point_B = support_B;
		bool collided = p.distance_to(support_B) <= 0;
		return collided;
	}

	if (support_type == GodotShape3D::FEATURE_CIRCLE) {
		ERR_FAIL_COND_V(support_count != 3, false);

		Vector3 circle_pos = supports[0];
		Vector3 circle_axis_1 = supports[1] - circle_pos;
		Vector3 circle_axis_2 = supports[2] - circle_pos;

		// Use 3 equidistant points on the circle.
		for (int i = 0; i < 3; ++i) {
			Vector3 vertex_pos = circle_pos;
			vertex_pos += circle_axis_1 * Math::cos(2.0 * Math::PI * i / 3.0);
			vertex_pos += circle_axis_2 * Math::sin(2.0 * Math::PI * i / 3.0);
			supports[i] = vertex_pos;
		}
	}

	bool collided = false;
	Vector3 closest;
	real_t closest_d = 0;

	for (int i = 0; i < support_count; i++) {
		supports[i] = p_transform_B.xform(supports[i]);
		real_t d = p.distance_to(supports[i]);
		if (i == 0 || d < closest_d) {
			closest = supports[i];
			closest_d = d;
			if (d <= 0) {
				collided = true;
			}
		}
	}

	r_point_A = p.project(closest);
	r_point_B = closest;

	return collided;
}

bool GodotCollisionSolver3D::solve_distance(const GodotShape3D *p_shape_A, const Transform3D &p_transform_A, const GodotShape3D *p_shape_B, const Transform3D &p_transform_B, Vector3 &r_point_A, Vector3 &r_point_B, const AABB &p_concave_hint, Vector3 *r_sep_axis) {
	if (p_shape_B->get_type() == PhysicsServer3D::SHAPE_WORLD_BOUNDARY) {
		Vector3 a, b;
		bool col = solve_distance_world_boundary(p_shape_B, p_transform_B, p_shape_A, p_transform_A, a, b);
		r_point_A = b;
		r_point_B = a;
		return !col;

	} else if (p_shape_B->is_concave()) {
		if (p_shape_A->is_concave()) {
			return false;
		}

		const GodotConcaveShape3D *concave_B = static_cast<const GodotConcaveShape3D *>(p_shape_B);

		_ConcaveCollisionInfo cinfo;
		cinfo.transform_A = &p_transform_A;
		cinfo.shape_A = p_shape_A;
		cinfo.transform_B = &p_transform_B;
		cinfo.result_callback = nullptr;
		cinfo.userdata = nullptr;
		cinfo.swap_result = false;
		cinfo.collided = false;
		cinfo.collisions = 0;
		cinfo.aabb_tests = 0;
		cinfo.tested = false;

		Transform3D rel_transform = p_transform_A;
		rel_transform.origin -= p_transform_B.origin;

		//quickly compute a local AABB

		bool use_cc_hint = p_concave_hint != AABB();
		AABB cc_hint_aabb;
		if (use_cc_hint) {
			cc_hint_aabb = p_concave_hint;
			cc_hint_aabb.position -= p_transform_B.origin;
		}

		AABB local_aabb;
		for (int i = 0; i < 3; i++) {
			Vector3 axis(p_transform_B.basis.get_column(i));
			real_t axis_scale = ((real_t)1.0) / axis.length();
			axis *= axis_scale;

			real_t smin, smax;

			if (use_cc_hint) {
				cc_hint_aabb.project_range_in_plane(Plane(axis), smin, smax);
			} else {
				p_shape_A->project_range(axis, rel_transform, smin, smax);
			}

			smin *= axis_scale;
			smax *= axis_scale;

			local_aabb.position[i] = smin;
			local_aabb.size[i] = smax - smin;
		}

		concave_B->cull(local_aabb, concave_distance_callback, &cinfo, false);
		if (!cinfo.collided) {
			r_point_A = cinfo.close_A;
			r_point_B = cinfo.close_B;
		}

		return !cinfo.collided;
	} else {
		return gjk_epa_calculate_distance(p_shape_A, p_transform_A, p_shape_B, p_transform_B, r_point_A, r_point_B); //should pass sepaxis..
	}
}

// FellowEngine (milestone 17, decision 0085): yes/no distance tests against concave shapes without GJK for the faces that
// cannot be within its tolerance.
//
// GJK (gjk_epa.cpp) reports the shapes as touching when the point of the Minkowski difference closest to the origin it
// has found is nearer than GJK_MIN_DISTANCE (1e-4), when its simplex encloses the origin, or when it fails to converge in
// GJK_MAX_ITERATIONS. The first two need the faces' true distance to be within 1e-4 plus GJK's rounding (single
// precision, relative to the coordinates). A face is skipped only when a separating axis, evaluated in double precision,
// puts it farther than g = 1 mm + 1e-5 * (the largest coordinate involved) from a box that holds shape A over its whole
// motion. The third case (a failure to converge) is not geometric; physics/verify gives every skipped face to GJK anyway
// and counts any that it reports as touching (none in the Physics Lab).
struct _FellowSeparationInfo {
	const Transform3D *transform_A = nullptr;
	const GodotShape3D *shape_A = nullptr;
	const Transform3D *transform_B = nullptr;
	bool collided = false;
	bool verify = false;
	bool ordered = false;
	uint64_t verified = 0;
	uint64_t mismatches = 0;

	// Shape A's bound: the parallelepiped p_transform_A makes of its local AABB (centre, half-edge vectors), swept along
	// the motion; its extent [lo, hi] along each axis.
	double center[3];
	double edge[3][3];
	double motion[3];
	double magnitude = 0.0; // the largest coordinate of a point of the bound (and of p_transform_A's origin)
	int axis_count = 0;
	double axis[6][3];
	double lo[6];
	double hi[6];

	_FORCE_INLINE_ static double dot3(const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

	void range_along(const double *p_axis, double &r_lo, double &r_hi) const {
		const double c = dot3(p_axis, center);
		const double m = dot3(p_axis, motion);
		const double e = Math::abs(dot3(p_axis, edge[0])) + Math::abs(dot3(p_axis, edge[1])) + Math::abs(dot3(p_axis, edge[2]));
		r_lo = c + MIN(m, 0.0) - e;
		r_hi = c + MAX(m, 0.0) + e;
	}

	void add_axis(double x, double y, double z) {
		const double l = Math::sqrt(x * x + y * y + z * z);
		if (!(l > 1e-12)) {
			return;
		}
		double *a = axis[axis_count];
		a[0] = x / l;
		a[1] = y / l;
		a[2] = z / l;
		range_along(a, lo[axis_count], hi[axis_count]);
		axis_count++;
	}

	void setup(const Transform3D &p_transform, const AABB &p_bounds, const Vector3 &p_motion) {
		const Basis &b = p_transform.basis;
		double cl[3], hl[3], ml[3];
		for (int i = 0; i < 3; i++) {
			hl[i] = 0.5 * double(p_bounds.size[i]);
			cl[i] = double(p_bounds.position[i]) + hl[i];
			ml[i] = double(p_motion[i]);
		}
		magnitude = 0.0;
		for (int r = 0; r < 3; r++) {
			center[r] = double(p_transform.origin[r]) + double(b.rows[r][0]) * cl[0] + double(b.rows[r][1]) * cl[1] + double(b.rows[r][2]) * cl[2];
			motion[r] = double(b.rows[r][0]) * ml[0] + double(b.rows[r][1]) * ml[1] + double(b.rows[r][2]) * ml[2];
			for (int i = 0; i < 3; i++) {
				edge[i][r] = double(b.rows[r][i]) * hl[i];
			}
		}
		for (int r = 0; r < 3; r++) {
			// Every term of the transform's sums counted (GJK's single-precision rounding is relative to them, even where
			// they cancel).
			const double reach = Math::abs(double(p_transform.origin[r])) + Math::abs(double(b.rows[r][0]) * cl[0]) + Math::abs(double(b.rows[r][1]) * cl[1]) +
					Math::abs(double(b.rows[r][2]) * cl[2]) + Math::abs(edge[0][r]) + Math::abs(edge[1][r]) + Math::abs(edge[2][r]) + Math::abs(motion[r]);
			magnitude = MAX(magnitude, reach);
		}
		// The motion's direction and two directions across it (a long diagonal motion: the box's axes alone bound it no
		// better than its AABB; across the motion is where most faces are left out), then the box's own axes.
		axis_count = 0;
		const double ml2 = dot3(motion, motion);
		if (ml2 > 1e-18) {
			const double l = Math::sqrt(ml2);
			const double d[3] = { motion[0] / l, motion[1] / l, motion[2] / l };
			// A reference direction the least aligned with d.
			int k = 0;
			for (int i = 1; i < 3; i++) {
				if (Math::abs(d[i]) < Math::abs(d[k])) {
					k = i;
				}
			}
			double ref[3] = { 0.0, 0.0, 0.0 };
			ref[k] = 1.0;
			const double u[3] = { d[1] * ref[2] - d[2] * ref[1], d[2] * ref[0] - d[0] * ref[2], d[0] * ref[1] - d[1] * ref[0] };
			add_axis(u[0], u[1], u[2]);
			add_axis(d[1] * u[2] - d[2] * u[1], d[2] * u[0] - d[0] * u[2], d[0] * u[1] - d[1] * u[0]);
			add_axis(d[0], d[1], d[2]);
		}
		for (int i = 0; i < 3; i++) {
			add_axis(b.rows[0][i], b.rows[1][i], b.rows[2][i]);
		}
	}

	// The axes taken into the concave shape's local space (a projection of a world point B v + o on a is (Bt a) . v + a . o),
	// so that a face is tested on its local vertices; the face normal's direction in world space is the cofactor matrix of
	// B times the local normal. One g for the query: from the largest coordinate a vertex of the shape can have.
	double local_axis[6][3];
	double local_offset[6];
	double cofactor[3][3];
	double basis_b[3][3];
	double origin_b[3];
	double g = 0.0;

	void prepare_local(const Transform3D &p_transform_B, const AABB &p_shape_B_aabb) {
		const Basis &b = p_transform_B.basis;
		for (int r = 0; r < 3; r++) {
			origin_b[r] = p_transform_B.origin[r];
			for (int c = 0; c < 3; c++) {
				basis_b[r][c] = b.rows[r][c];
			}
		}
		for (int j = 0; j < axis_count; j++) {
			for (int c = 0; c < 3; c++) {
				local_axis[j][c] = axis[j][0] * basis_b[0][c] + axis[j][1] * basis_b[1][c] + axis[j][2] * basis_b[2][c];
			}
			local_offset[j] = dot3(axis[j], origin_b);
		}
		const double(&m)[3][3] = basis_b;
		cofactor[0][0] = m[1][1] * m[2][2] - m[1][2] * m[2][1];
		cofactor[0][1] = m[1][2] * m[2][0] - m[1][0] * m[2][2];
		cofactor[0][2] = m[1][0] * m[2][1] - m[1][1] * m[2][0];
		cofactor[1][0] = m[0][2] * m[2][1] - m[0][1] * m[2][2];
		cofactor[1][1] = m[0][0] * m[2][2] - m[0][2] * m[2][0];
		cofactor[1][2] = m[0][1] * m[2][0] - m[0][0] * m[2][1];
		cofactor[2][0] = m[0][1] * m[1][2] - m[0][2] * m[1][1];
		cofactor[2][1] = m[0][2] * m[1][0] - m[0][0] * m[1][2];
		cofactor[2][2] = m[0][0] * m[1][1] - m[0][1] * m[1][0];
		double vmax = magnitude;
		for (int r = 0; r < 3; r++) {
			double reach = Math::abs(origin_b[r]);
			for (int c = 0; c < 3; c++) {
				const double lo_c = Math::abs(double(p_shape_B_aabb.position[c]));
				const double hi_c = Math::abs(double(p_shape_B_aabb.position[c]) + double(p_shape_B_aabb.size[c]));
				reach += Math::abs(basis_b[r][c]) * MAX(lo_c, hi_c) * (1.0 + 1e-6);
			}
			vmax = MAX(vmax, reach);
		}
		g = 1e-3 + 1e-5 * vmax;
	}

	// True when the face is certainly farther from the bound than GJK's tolerance. r_score: the largest gap found over
	// the axes less g (<= 0 when the face is kept; the lower, the deeper the bound overlaps the face on every axis).
	bool face_separated(const GodotFaceShape3D *p_face, double &r_score) const {
		double v[3][3];
		for (int k = 0; k < 3; k++) {
			v[k][0] = p_face->vertex[k].x;
			v[k][1] = p_face->vertex[k].y;
			v[k][2] = p_face->vertex[k].z;
		}
		double gap = -1e300;
		for (int j = 0; j < axis_count; j++) {
			const double *a = local_axis[j];
			const double p0 = dot3(a, v[0]) + local_offset[j];
			const double p1 = dot3(a, v[1]) + local_offset[j];
			const double p2 = dot3(a, v[2]) + local_offset[j];
			const double pmin = MIN(p0, MIN(p1, p2));
			const double pmax = MAX(p0, MAX(p1, p2));
			if (pmin > hi[j] + g || pmax < lo[j] - g) {
				return true;
			}
			gap = MAX(gap, MAX(pmin - hi[j], lo[j] - pmax));
		}
		// The face's plane (any direction is a valid separating axis; the face's three vertices are projected on it).
		const Vector3 &nl_f = p_face->normal;
		double n[3];
		for (int r = 0; r < 3; r++) {
			n[r] = cofactor[r][0] * double(nl_f.x) + cofactor[r][1] * double(nl_f.y) + cofactor[r][2] * double(nl_f.z);
		}
		const double nl = Math::sqrt(dot3(n, n));
		if (nl > 1e-12) {
			n[0] /= nl;
			n[1] /= nl;
			n[2] /= nl;
			double l, h;
			range_along(n, l, h);
			double la[3];
			for (int c = 0; c < 3; c++) {
				la[c] = n[0] * basis_b[0][c] + n[1] * basis_b[1][c] + n[2] * basis_b[2][c];
			}
			const double off = dot3(n, origin_b);
			const double p0 = dot3(la, v[0]) + off;
			const double p1 = dot3(la, v[1]) + off;
			const double p2 = dot3(la, v[2]) + off;
			const double pmin = MIN(p0, MIN(p1, p2));
			const double pmax = MAX(p0, MAX(p1, p2));
			if (pmin > h + g || pmax < l - g) {
				return true;
			}
			gap = MAX(gap, MAX(pmin - h, l - pmax));
		}
		r_score = gap - g;
		return false;
	}

	// The faces kept, given to GJK in batches, the most deeply overlapped first: whether any is touching does not
	// depend on the order (GJK's answer for a face depends on the face alone), and a touching face found early ends the
	// search early.
	struct Candidate {
		Vector3 vertex[3];
		double score = 0.0;
	};
	static constexpr int MAX_CANDIDATES = 32;
	Candidate candidates[MAX_CANDIDATES];
	int candidate_count = 0;
	int batch = MAX_CANDIDATES;
	GodotFaceShape3D face;

	// GJK on the pending candidates; true when one is touching.
	bool flush() {
		const int n = candidate_count;
		candidate_count = 0;
		int order[MAX_CANDIDATES];
		for (int i = 0; i < n; i++) {
			int j = i;
			while (j > 0 && candidates[order[j - 1]].score > candidates[i].score) {
				order[j] = order[j - 1];
				j--;
			}
			order[j] = i;
		}
		Vector3 close_A, close_B;
		for (int i = 0; i < n; i++) {
			const Candidate &c = candidates[order[i]];
			face.vertex[0] = c.vertex[0];
			face.vertex[1] = c.vertex[1];
			face.vertex[2] = c.vertex[2];
			if (!gjk_epa_calculate_distance(shape_A, *transform_A, &face, *transform_B, close_A, close_B)) {
				collided = true;
				return true;
			}
		}
		return false;
	}
};

bool GodotCollisionSolver3D::fellow_separation_callback(void *p_userdata, GodotShape3D *p_convex) {
	_FellowSeparationInfo &info = *(static_cast<_FellowSeparationInfo *>(p_userdata));
	Vector3 close_A, close_B;
	const GodotFaceShape3D *face = static_cast<const GodotFaceShape3D *>(p_convex);
	double score = 0.0;
	if (info.face_separated(face, score)) {
		if (info.verify) {
			info.verified++;
			if (!gjk_epa_calculate_distance(info.shape_A, *info.transform_A, p_convex, *info.transform_B, close_A, close_B)) {
				info.mismatches++;
			}
		}
		return false;
	}
	if (info.ordered) {
		_FellowSeparationInfo::Candidate &c = info.candidates[info.candidate_count++];
		c.vertex[0] = face->vertex[0];
		c.vertex[1] = face->vertex[1];
		c.vertex[2] = face->vertex[2];
		c.score = score;
		return info.candidate_count >= info.batch ? info.flush() : false;
	}
	// As concave_distance_callback(): the first face found touching ends the search.
	info.collided = !gjk_epa_calculate_distance(info.shape_A, *info.transform_A, p_convex, *info.transform_B, close_A, close_B);
	return info.collided;
}

bool GodotCollisionSolver3D::fellow_test_separation(const GodotShape3D *p_shape_A, const Transform3D &p_transform_A, const AABB &p_bounds_A, const Vector3 &p_motion_A, const GodotShape3D *p_shape_B, const Transform3D &p_transform_B, const AABB &p_concave_hint) {
	if (p_shape_B->get_type() == PhysicsServer3D::SHAPE_WORLD_BOUNDARY || !p_shape_B->is_concave() || p_shape_A->is_concave() || !Fellow::physics_distance_face_cull()) {
		Vector3 a, b;
		return solve_distance(p_shape_A, p_transform_A, p_shape_B, p_transform_B, a, b, p_concave_hint);
	}

	const GodotConcaveShape3D *concave_B = static_cast<const GodotConcaveShape3D *>(p_shape_B);

	_FellowSeparationInfo info;
	info.transform_A = &p_transform_A;
	info.shape_A = p_shape_A;
	info.transform_B = &p_transform_B;
	info.verify = Fellow::physics_verify();
	info.ordered = Fellow::physics_distance_face_order();
	info.batch = CLAMP(Fellow::physics_distance_face_batch(), 1, _FellowSeparationInfo::MAX_CANDIDATES);
	info.setup(p_transform_A, p_bounds_A, p_motion_A);
	info.prepare_local(p_transform_B, p_shape_B->get_aabb());

	// The faces culled exactly as solve_distance() culls them (the same box, the same order).
	Transform3D rel_transform = p_transform_A;
	rel_transform.origin -= p_transform_B.origin;

	bool use_cc_hint = p_concave_hint != AABB();
	AABB cc_hint_aabb;
	if (use_cc_hint) {
		cc_hint_aabb = p_concave_hint;
		cc_hint_aabb.position -= p_transform_B.origin;
	}

	AABB local_aabb;
	for (int i = 0; i < 3; i++) {
		Vector3 axis(p_transform_B.basis.get_column(i));
		real_t axis_scale = ((real_t)1.0) / axis.length();
		axis *= axis_scale;

		real_t smin, smax;

		if (use_cc_hint) {
			cc_hint_aabb.project_range_in_plane(Plane(axis), smin, smax);
		} else {
			p_shape_A->project_range(axis, rel_transform, smin, smax);
		}

		smin *= axis_scale;
		smax *= axis_scale;

		local_aabb.position[i] = smin;
		local_aabb.size[i] = smax - smin;
	}

	concave_B->cull(local_aabb, fellow_separation_callback, &info, false);
	if (!info.collided && info.candidate_count > 0) {
		info.flush();
	}

	if (info.verify) {
		Fellow::stats().physics_verify_faces.add(info.verified);
		if (info.mismatches) {
			Fellow::stats().physics_verify_mismatches.add(info.mismatches);
		}
	}
	return !info.collided;
}
