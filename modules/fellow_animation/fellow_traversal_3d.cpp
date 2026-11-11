/**************************************************************************/
/*  fellow_traversal_3d.cpp                                               */
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

#include "fellow_traversal_3d.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/physics/character_body_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/cylinder_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/immediate_mesh.h"
#include "scene/resources/material.h"
#include "servers/physics_3d/physics_server_3d.h"

namespace {

const Vector3 UP(0, 1, 0);

Vector3 flat(const Vector3 &p_v) {
	return Vector3(p_v.x, 0.0f, p_v.z);
}

float smooth(float p_x) {
	const float x = CLAMP(p_x, 0.0f, 1.0f);
	return x * x * (3.0f - 2.0f * x);
}

// 0 before p_a, up to 1 at p_b, 1 until p_c, down to 0 at p_d (smoothly).
float window(float p_t, float p_a, float p_b, float p_c, float p_d) {
	if (p_t <= p_a || p_t >= p_d) {
		return 0.0f;
	}
	if (p_t < p_b) {
		return smooth((p_t - p_a) / MAX(p_b - p_a, 1e-4f));
	}
	if (p_t <= p_c) {
		return 1.0f;
	}
	return 1.0f - smooth((p_t - p_c) / MAX(p_d - p_c, 1e-4f));
}

int clip_slot(FellowTraversal3D::Kind p_kind) {
	return p_kind == FellowTraversal3D::KIND_LOW ? 0 : (p_kind == FellowTraversal3D::KIND_HIGH ? 1 : 2);
}

} // namespace

FellowTraversal3D::GlobalStats FellowTraversal3D::global_stats;

FellowTraversal3D::FellowTraversal3D() {
}

Dictionary FellowTraversal3D::get_global_stats() {
	Dictionary d;
	d["checks"] = int64_t(global_stats.checks.get());
	d["check_usec"] = int64_t(global_stats.check_usec.get());
	d["steps"] = int64_t(global_stats.steps.get());
	d["step_usec"] = int64_t(global_stats.step_usec.get());
	d["mantles_low"] = int64_t(global_stats.mantles[0].get());
	d["mantles_high"] = int64_t(global_stats.mantles[1].get());
	d["vaults"] = int64_t(global_stats.mantles[2].get());
	d["poses"] = int64_t(global_stats.poses.get());
	d["pose_usec"] = int64_t(global_stats.pose_usec.get());
	return d;
}

void FellowTraversal3D::reset_global_stats() {
	for (SafeNumeric<uint64_t> *v : { &global_stats.checks, &global_stats.check_usec, &global_stats.steps, &global_stats.step_usec, &global_stats.mantles[0], &global_stats.mantles[1], &global_stats.mantles[2], &global_stats.poses, &global_stats.pose_usec }) {
		v->set(0);
	}
}

FellowTraversal3D::~FellowTraversal3D() {
	PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
	if (ps) {
		if (probe_shape.is_valid()) {
			ps->free_rid(probe_shape);
		}
		if (body_shape.is_valid()) {
			ps->free_rid(body_shape);
		}
	}
}

CollisionObject3D *FellowTraversal3D::_get_body() const {
	if (!body_path.is_empty()) {
		return Object::cast_to<CollisionObject3D>(get_node_or_null(body_path));
	}
	return Object::cast_to<CollisionObject3D>(get_parent());
}

void FellowTraversal3D::_read_body_shape(CollisionObject3D *p_body) {
	for (int i = 0; i < p_body->get_child_count(); i++) {
		const CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(p_body->get_child(i));
		if (!cs || cs->is_disabled() || cs->get_shape().is_null()) {
			continue;
		}
		const Ref<Shape3D> s = cs->get_shape();
		if (const CapsuleShape3D *c = Object::cast_to<CapsuleShape3D>(s.ptr())) {
			radius = c->get_radius();
			height = MAX(c->get_height(), radius * 2.0f);
		} else if (const CylinderShape3D *cy = Object::cast_to<CylinderShape3D>(s.ptr())) {
			radius = cy->get_radius();
			height = cy->get_height();
		} else if (const BoxShape3D *b = Object::cast_to<BoxShape3D>(s.ptr())) {
			radius = MAX(b->get_size().x, b->get_size().z) * 0.5f;
			height = b->get_size().y;
		} else if (const SphereShape3D *sp = Object::cast_to<SphereShape3D>(s.ptr())) {
			radius = sp->get_radius();
			height = radius * 2.0f;
		} else {
			continue;
		}
		shape_center = cs->get_transform().origin;
		return;
	}
}

Vector3 FellowTraversal3D::_feet_of(const Transform3D &p_body_xf) const {
	return p_body_xf.xform(shape_center - Vector3(0.0f, height * 0.5f, 0.0f));
}

Transform3D FellowTraversal3D::_body_at(const Vector3 &p_feet, const Basis &p_basis) const {
	return Transform3D(p_basis, p_feet - p_basis.xform(shape_center - Vector3(0.0f, height * 0.5f, 0.0f)));
}

Vector3 FellowTraversal3D::_body_velocity(CollisionObject3D *p_body, double p_delta) {
	const Vector3 origin = p_body->get_global_position();
	Vector3 v;
	if (const CharacterBody3D *cb = Object::cast_to<CharacterBody3D>(p_body)) {
		v = cb->get_velocity();
	} else if (const RigidBody3D *rb = Object::cast_to<RigidBody3D>(p_body)) {
		v = rb->get_linear_velocity();
	} else if (has_last_origin && p_delta > 0.0) {
		v = (origin - last_body_origin) / p_delta;
	}
	last_body_origin = origin;
	has_last_origin = true;
	return v;
}

bool FellowTraversal3D::_capsule_free(const Vector3 &p_feet, float p_radius, float p_height, const HashSet<RID> &p_exclude) const {
	CollisionObject3D *body = _get_body();
	PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
	Dictionary shape_data;
	shape_data["radius"] = p_radius;
	shape_data["height"] = MAX(p_height, p_radius * 2.0f);
	ps->shape_set_data(body_shape, shape_data);
	PhysicsDirectSpaceState3D::ShapeParameters sp;
	sp.shape_rid = body_shape;
	sp.transform = Transform3D(Basis(), p_feet + UP * (MAX(p_height, p_radius * 2.0f) * 0.5f));
	sp.exclude = p_exclude;
	sp.collision_mask = collision_mask;
	PhysicsDirectSpaceState3D::ShapeResult r;
	return body->get_world_3d()->get_direct_space_state()->intersect_shape(sp, &r, 1) == 0;
}

FellowTraversal3D::Ledge FellowTraversal3D::_check(const Vector3 &p_direction, bool p_in_air, float p_speed) {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	const Ledge l = _check_ledge(p_direction, p_in_air, p_speed);
	global_stats.checks.increment();
	global_stats.check_usec.add(OS::get_singleton()->get_ticks_usec() - t0);
	return l;
}

FellowTraversal3D::Ledge FellowTraversal3D::_check_ledge(const Vector3 &p_direction, bool p_in_air, float p_speed) {
	Ledge l;
	CollisionObject3D *body = _get_body();
	if (!body || !body->is_inside_tree() || body->get_world_3d().is_null()) {
		l.reason = "no body";
		return l;
	}
	PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
	if (probe_shape.is_null()) {
		probe_shape = ps->capsule_shape_create();
		body_shape = ps->capsule_shape_create();
	}
	_read_body_shape(body);
	PhysicsDirectSpaceState3D *space = body->get_world_3d()->get_direct_space_state();
	const Transform3D bx = body->get_global_transform();
	const Vector3 feet = _feet_of(bx);
	Vector3 d = flat(p_direction);
	if (d.length() < 1e-3f) {
		d = flat(-bx.basis.get_column(2));
	}
	if (d.length() < 1e-3f) {
		l.reason = "no direction";
		return l;
	}
	d.normalize();
	HashSet<RID> exclude;
	exclude.insert(body->get_rid());
	const float lo = min_height;
	float hi = max_height + (p_in_air ? 0.5f : 0.0f); // hanging, the feet are lower than standing
	// A ceiling over the body bounds the heights searched (a probe through it would find the ceiling, not a wall).
	bool ceiling = false;
	{
		PhysicsDirectSpaceState3D::RayParameters cq;
		cq.from = feet + UP * (height * 0.5f);
		cq.to = feet + UP * (hi + 0.15f);
		cq.exclude = exclude;
		cq.collision_mask = collision_mask;
		PhysicsDirectSpaceState3D::RayResult cres;
		if (space->intersect_ray(cq, cres) && cres.position.y - feet.y - 0.05f < hi + 0.1f) {
			hi = MIN(hi, cres.position.y - feet.y - 0.05f);
			ceiling = true;
		}
	}
	if (hi < lo + 0.1f) {
		l.reason = "no room above";
		return l;
	}

	// 1. A wall within reach: a capsule over the heights a ledge may have, swept forward from the body's axis.
	const float pr = radius * 0.75f;
	const float ph = MAX(hi - lo, pr * 2.0f + 0.01f);
	Dictionary shape_data;
	shape_data["radius"] = pr;
	shape_data["height"] = ph;
	ps->shape_set_data(probe_shape, shape_data);
	PhysicsDirectSpaceState3D::ShapeParameters sp;
	sp.shape_rid = probe_shape;
	sp.transform = Transform3D(Basis(), Vector3(feet.x, feet.y + (lo + hi) * 0.5f, feet.z));
	sp.motion = d * (reach_distance + radius - pr);
	sp.exclude = exclude;
	sp.collision_mask = collision_mask;
	real_t safe = 1.0;
	real_t unsafe = 1.0;
	if (!space->cast_motion(sp, safe, unsafe) || unsafe >= 1.0) {
		l.reason = "no wall";
		return l;
	}
	PhysicsDirectSpaceState3D::ShapeParameters rp = sp;
	rp.transform.origin += sp.motion * unsafe;
	rp.motion = Vector3();
	PhysicsDirectSpaceState3D::ShapeRestInfo ri;
	if (!space->rest_info(rp, &ri)) {
		l.reason = "no wall";
		return l;
	}
	Vector3 wn = flat(ri.normal);
	if (wn.length() < 0.6f) {
		l.reason = "no wall"; // a slope or a ceiling
		return l;
	}
	Vector3 inward = -wn.normalized();
	if (inward.dot(d) < Math::cos(Math::deg_to_rad(max_wall_angle))) {
		l.reason = "wall at an angle";
		return l;
	}

	// 2. The top: a ray down just inside the wall's face, from above the highest ledge.
	const float in = MAX(0.12f, radius * 0.4f);
	const Vector3 column = ri.point + inward * in;
	PhysicsDirectSpaceState3D::RayParameters rq;
	rq.from = Vector3(column.x, feet.y + hi + (ceiling ? 0.0f : 0.1f), column.z);
	rq.to = Vector3(column.x, feet.y + lo - 0.1f, column.z);
	rq.exclude = exclude;
	rq.collision_mask = collision_mask;
	rq.hit_from_inside = true;
	PhysicsDirectSpaceState3D::RayResult rr;
	if (!space->intersect_ray(rq, rr)) {
		l.reason = "no top"; // thinner than the column (a pole)
		return l;
	}
	if (rr.normal == Vector3() || rr.position.is_equal_approx(rq.from)) {
		l.kind = KIND_TOO_HIGH;
		l.reason = "too high";
		l.height = hi;
		return l;
	}
	const Vector3 top = rr.position;
	l.height = top.y - feet.y;
	l.top_normal = rr.normal;
	l.collider = rr.collider_id;
	if (l.height < lo) {
		l.kind = KIND_STEP;
		return l;
	}
	if (l.height > hi) {
		l.kind = KIND_TOO_HIGH;
		l.reason = "too high";
		return l;
	}
	if (rr.normal.y < Math::cos(Math::deg_to_rad(max_ledge_slope))) {
		l.kind = KIND_BLOCKED;
		l.reason = "slope";
		return l;
	}
	// The edge: the wall's face found again level with the top (a leaning wall's face moves with height).
	Vector3 face = ri.point;
	{
		PhysicsDirectSpaceState3D::RayParameters fq;
		fq.from = Vector3(feet.x, top.y - 0.06f, feet.z);
		fq.to = fq.from + inward * (radius + reach_distance + in + 0.3f);
		fq.exclude = exclude;
		fq.collision_mask = collision_mask;
		PhysicsDirectSpaceState3D::RayResult fr;
		if (space->intersect_ray(fq, fr) && flat(fr.normal).length() > 0.6f && -flat(fr.normal).normalized().dot(inward) > 0.7f) {
			face = fr.position;
			inward = -flat(fr.normal).normalized();
		}
	}
	const Vector3 edge(face.x, top.y, face.z);
	l.edge = edge;
	l.inward = inward;

	// 3. A thin obstacle: its far side drops within vault_max_depth.
	bool thin = false;
	Vector3 landing;
	if (vault && !p_in_air && l.height <= vault_max_height) {
		const Vector3 far = edge + inward * (vault_max_depth + 0.05f);
		PhysicsDirectSpaceState3D::RayParameters vq;
		vq.from = Vector3(far.x, top.y + 0.1f, far.z);
		vq.to = Vector3(far.x, feet.y - 1.0f, far.z);
		vq.exclude = exclude;
		vq.collision_mask = collision_mask;
		vq.hit_from_inside = true;
		PhysicsDirectSpaceState3D::RayResult vr;
		if (space->intersect_ray(vq, vr) && vr.normal != Vector3() && vr.position.y < top.y - 0.25f) {
			// The far face, from beyond it back towards the body.
			PhysicsDirectSpaceState3D::RayParameters bq;
			bq.from = Vector3(far.x, top.y - 0.06f, far.z);
			bq.to = Vector3(edge.x, top.y - 0.06f, edge.z);
			bq.exclude = exclude;
			bq.collision_mask = collision_mask;
			PhysicsDirectSpaceState3D::RayResult br;
			l.depth = space->intersect_ray(bq, br) ? MAX(0.0f, (br.position - edge).dot(inward)) : vault_max_depth;
			// The landing: beyond the far face, on whatever is under it.
			landing = edge + inward * (l.depth + radius + 0.3f);
			PhysicsDirectSpaceState3D::RayParameters lq;
			lq.from = Vector3(landing.x, top.y + 0.1f, landing.z);
			lq.to = Vector3(landing.x, feet.y - 1.5f, landing.z);
			lq.exclude = exclude;
			lq.collision_mask = collision_mask;
			lq.hit_from_inside = true;
			PhysicsDirectSpaceState3D::RayResult lr;
			if (space->intersect_ray(lq, lr) && lr.normal != Vector3() && lr.normal.y >= Math::cos(Math::deg_to_rad(max_ledge_slope))) {
				landing.y = lr.position.y;
				thin = true;
			}
		}
	}

	// 4. Room: on the top, straight up in front of the wall, over the edge (a capsule a little smaller than the body's,
	// so that touching is not blocking).
	const float cr = radius * 0.95f;
	const float ch = height - 0.04f;
	Vector3 top_feet = edge + inward * (radius + 0.12f);
	bool room = false;
	{
		PhysicsDirectSpaceState3D::RayParameters tq;
		tq.from = Vector3(top_feet.x, top.y + 0.5f, top_feet.z);
		tq.to = Vector3(top_feet.x, top.y - 0.5f, top_feet.z);
		tq.exclude = exclude;
		tq.collision_mask = collision_mask;
		tq.hit_from_inside = true;
		PhysicsDirectSpaceState3D::RayResult tr;
		// The same top (not the ground beyond a thin wall, not a step on it).
		if (space->intersect_ray(tq, tr) && tr.normal != Vector3() && tr.normal.y >= Math::cos(Math::deg_to_rad(max_ledge_slope)) && Math::abs(tr.position.y - top.y) < 0.25f) {
			top_feet.y = tr.position.y;
			room = _capsule_free(top_feet + UP * 0.03f, cr, ch, exclude);
		}
	}
	const Vector3 front = edge - inward * (radius + 0.03f);
	const bool lift = _capsule_free(Vector3(front.x, top.y + 0.03f, front.z), cr, ch, exclude);
	const Vector3 over = edge + inward * (radius * 0.5f);
	const bool over_free = _capsule_free(Vector3(over.x, top.y + 0.03f, over.z), cr, ch, exclude);
	bool vault_ok = false;
	if (thin) {
		const Vector3 middle = edge + inward * (l.depth * 0.5f);
		vault_ok = lift && _capsule_free(Vector3(middle.x, top.y + 0.03f, middle.z), cr, ch, exclude) && _capsule_free(landing + UP * 0.03f, cr, ch, exclude);
	}
	const bool mantle_ok = room && lift && over_free;
	if (thin && vault_ok && (!mantle_ok || p_speed >= vault_min_speed)) {
		l.kind = KIND_VAULT;
		l.target = landing;
	} else if (mantle_ok) {
		l.kind = l.height <= low_max_height ? KIND_LOW : KIND_HIGH;
		l.target = top_feet;
	} else {
		l.kind = KIND_BLOCKED;
		l.reason = !lift ? "no room above" : (!room ? "no room on top" : "no room over the edge");
		l.target = top_feet;
	}
	// A ledge the body could walk under (a beam, a shelf over head height): the automatic mantle leaves it.
	if (!p_in_air && (l.kind == KIND_LOW || l.kind == KIND_HIGH)) {
		Dictionary body_data;
		body_data["radius"] = cr;
		body_data["height"] = ch;
		ps->shape_set_data(body_shape, body_data);
		PhysicsDirectSpaceState3D::ShapeParameters bp;
		bp.shape_rid = body_shape;
		bp.transform = Transform3D(Basis(), feet + UP * (0.05f + ch * 0.5f));
		bp.motion = inward * (reach_distance + 0.1f);
		bp.exclude = exclude;
		bp.collision_mask = collision_mask;
		real_t bs = 1.0;
		real_t bu = 1.0;
		l.open_below = !space->cast_motion(bp, bs, bu) || bu >= 1.0;
	}
	return l;
}

bool FellowTraversal3D::_start(const Ledge &p_ledge, float p_speed) {
	CollisionObject3D *body = _get_body();
	if (!body || (p_ledge.kind != KIND_LOW && p_ledge.kind != KIND_HIGH && p_ledge.kind != KIND_VAULT)) {
		return false;
	}
	ledge = p_ledge;
	action = p_ledge.kind;
	start_xf = body->get_global_transform();
	start_feet = _feet_of(start_xf);
	entry_speed = p_speed;
	time = 0.0;
	const float h = MAX(ledge.height, 0.1f);
	switch (action) {
		case KIND_LOW:
			duration = low_duration * CLAMP(Math::sqrt(h / 1.0f), 0.85f, 1.2f);
			break;
		case KIND_HIGH:
			duration = high_duration * CLAMP(Math::sqrt(h / 1.8f), 0.85f, 1.15f);
			break;
		default:
			duration = vault_duration;
			break;
	}
	const Ref<Animation> &clip = clips[clip_slot(action)];
	if (clip.is_valid() && clip->get_length() > 0.05) {
		duration = clip->get_length();
	}
	_prepare_clip();
	const Vector3 scale = start_xf.basis.get_scale();
	if (align_to_wall) {
		end_basis = Basis::looking_at(ledge.inward, UP).scaled_local(scale);
	} else {
		end_basis = start_xf.basis;
	}
	const Node3D *collider = ObjectDB::get_instance<Node3D>(ledge.collider);
	collider_start = collider ? collider->get_global_transform() : Transform3D();
	active = true;
	global_stats.mantles[clip_slot(action)].increment();
	_update_hints(start_feet, ledge.edge, ledge.inward);
	emit_signal(SNAME("mantle_started"), int(action), _ledge_dict(ledge));
	return true;
}

void FellowTraversal3D::_prepare_clip() {
	clip_root.clear();
	const Ref<Animation> &clip = clips[clip_slot(action)];
	if (clip.is_null() || clip_root_bone == StringName()) {
		return;
	}
	for (int i = 0; i < clip->get_track_count(); i++) {
		if (clip->track_get_type(i) != Animation::TYPE_POSITION_3D || StringName(clip->track_get_path(i).get_concatenated_subnames()) != clip_root_bone) {
			continue;
		}
		const int n = MAX(2, int(Math::ceil(clip->get_length() / clip_root_step)) + 1);
		Vector3 first;
		for (int k = 0; k < n; k++) {
			Vector3 p;
			clip->try_position_track_interpolate(i, MIN(k * clip_root_step, clip->get_length()), &p);
			if (k == 0) {
				first = p;
			}
			clip_root.push_back(p - first);
		}
		return;
	}
}

// The feet along the mantle (procedural): to the wall's face, up to the top's height (no farther than the face until
// above it), then onto the top; a vault keeps going over the obstacle and down beyond it. A clip with root motion gives
// the path instead, its rise and its advance scaled onto the ledge.
Vector3 FellowTraversal3D::_path(double p_t, const Vector3 &p_edge, const Vector3 &p_inward, const Vector3 &p_target) const {
	const float t = float(p_t);
	const Vector3 p0 = start_feet;
	const float s0 = flat(p_edge - p0).dot(p_inward);
	const float wall_s = MAX(0.0f, s0 - radius - 0.03f);
	const float end_s = flat(p_target - p0).dot(p_inward);
	const Vector3 lateral = flat(p_target - p0) - p_inward * end_s;
	const float clear = 0.04f;
	const float y_top = p_edge.y + clear;

	if (clip_root.size() >= 2) {
		const Vector3 d = clip_root[clip_root.size() - 1];
		const Vector3 dh = flat(d);
		if (dh.length() > 0.05f) {
			float rise_max = 0.0f;
			for (const Vector3 &r : clip_root) {
				rise_max = MAX(rise_max, r.y);
			}
			const float f = t * float(clip_root.size() - 1);
			const int i = MIN(int(f), int(clip_root.size()) - 2);
			const Vector3 r = clip_root[i].lerp(clip_root[i + 1], f - float(i));
			const Vector3 fwd = dh.normalized();
			const float sh = end_s / dh.length();
			float sv;
			if (action == KIND_VAULT || d.y < 0.1f) {
				sv = (y_top - p0.y) / MAX(rise_max, 0.1f);
			} else {
				sv = (p_target.y - p0.y) / d.y;
			}
			float s = flat(r).dot(fwd) * sh;
			const float y = p0.y + r.y * sv;
			// Not into the wall before the clip's root is over the top (a clip that leans in early, scaled onto a
			// higher ledge).
			if (y < y_top - 0.01f && r.y < rise_max * 0.98f && (i + 1 < int(clip_root.size()) && clip_root[i + 1].y >= r.y)) {
				s = MIN(s, wall_s);
			}
			Vector3 p = flat(p0) + p_inward * s + lateral * smooth(t);
			p.y = y;
			return p;
		}
	}

	float s;
	float y;
	if (action == KIND_VAULT) {
		const float t_up = 0.32f;
		const float s_far = s0 + ledge.depth + radius + 0.02f;
		if (t < t_up) {
			s = wall_s * smooth(t / t_up);
			y = p0.y + (y_top - p0.y) * smooth(t / t_up);
		} else {
			const float u = (t - t_up) / (1.0f - t_up);
			s = wall_s + (end_s - wall_s) * u;
			const float t_over = t_up + CLAMP((s_far - wall_s) / MAX(end_s - wall_s, 1e-3f), 0.0f, 1.0f) * (1.0f - t_up);
			if (t < t_over || t_over >= 0.999f) {
				y = y_top;
			} else {
				const float v = (t - t_over) / (1.0f - t_over);
				y = y_top + (p_target.y - y_top) * v * v;
			}
		}
	} else {
		const float t1 = action == KIND_HIGH ? 0.62f : 0.55f;
		if (t < t1) {
			const float u = t / t1;
			s = wall_s * smooth(u / 0.4f);
			y = p0.y + (y_top - p0.y) * smooth(u);
		} else {
			const float u = smooth((t - t1) / (1.0f - t1));
			s = wall_s + (end_s - wall_s) * u;
			y = y_top + (p_target.y - y_top) * u;
		}
	}
	Vector3 p = flat(p0) + p_inward * s + lateral * smooth(t);
	p.y = y;
	return p;
}

// What the body's pose follows this frame: the hands on the edge, the feet on the wall's face or the top, the torso
// tipped towards the wall (windows of the mantle's progress, per kind).
void FellowTraversal3D::_update_hints(const Vector3 &p_feet, const Vector3 &p_edge, const Vector3 &p_inward) {
	const float t = float(CLAMP(time / duration, 0.0, 1.0));
	hints.active = active;
	hints.kind = action;
	hints.progress = t;
	hints.inward = p_inward;
	hints.left = UP.cross(p_inward).normalized();
	const Vector3 left = hints.left;
	const float ankle = 0.08f;
	hints.hand[0] = p_edge + left * 0.22f + p_inward * 0.06f + UP * 0.02f;
	hints.hand[1] = p_edge - left * 0.22f + p_inward * 0.06f + UP * 0.02f;
	const Vector3 on_top = p_edge + p_inward * 0.2f + UP * ankle;
	const Vector3 on_wall = p_edge - p_inward * 0.1f;
	// How far the hips may hang below the capsule's place (the arms reaching the edge, the legs against the wall's
	// face): no lower than the body has risen, none once it moves over the top.
	const float rise = MAX(0.0f, p_feet.y - start_feet.y);
	hints.foot_follow[0] = hints.foot_follow[1] = 1.0f;
	switch (action) {
		case KIND_LOW: {
			hints.hand_weight[0] = hints.hand_weight[1] = window(t, 0.04f, 0.24f, 0.7f, 0.88f);
			hints.lean = 0.45f * window(t, 0.2f, 0.5f, 0.8f, 1.0f);
			hints.hips_drop = MIN(rise, 0.6f) * (1.0f - smooth((t - 0.5f) / 0.12f));
			// The right foot steps onto the top, the left tucks against the wall.
			hints.foot[1] = on_top - left * 0.12f;
			hints.foot_weight[1] = window(t, 0.45f, 0.6f, 0.85f, 0.97f);
			hints.foot_follow[1] = 0.0f;
			hints.foot[0] = Vector3(on_wall.x, p_feet.y + 0.3f, on_wall.z) + left * 0.12f;
			hints.foot_weight[0] = window(t, 0.2f, 0.36f, 0.6f, 0.82f);
		} break;
		case KIND_HIGH: {
			hints.hand_weight[0] = hints.hand_weight[1] = window(t, 0.0f, 0.16f, 0.78f, 0.92f);
			hints.lean = 0.5f * window(t, 0.4f, 0.62f, 0.85f, 1.0f);
			hints.hips_drop = MIN(rise, 0.9f) * (1.0f - smooth((t - 0.56f) / 0.12f));
			// Both feet against the wall while pulling up, then the right onto the top.
			const float to_top = smooth((t - 0.56f) / 0.12f);
			const Vector3 wall_r = Vector3(on_wall.x, p_feet.y + 0.35f, on_wall.z) - left * 0.12f;
			hints.foot[1] = wall_r.lerp(on_top - left * 0.12f, to_top);
			hints.foot_follow[1] = 1.0f - to_top;
			hints.foot_weight[1] = window(t, 0.08f, 0.22f, 0.84f, 0.98f);
			hints.foot[0] = Vector3(on_wall.x, p_feet.y + 0.3f, on_wall.z) + left * 0.12f;
			hints.foot_weight[0] = window(t, 0.1f, 0.24f, 0.55f, 0.75f);
		} break;
		case KIND_VAULT: {
			// One hand planted on the top, the legs swung over tucked.
			hints.hand[0] = p_edge + p_inward * MIN(ledge.depth * 0.5f, 0.25f) + left * 0.12f + UP * 0.02f;
			hints.hand_weight[0] = window(t, 0.08f, 0.26f, 0.48f, 0.64f);
			hints.hand_weight[1] = 0.0f;
			hints.lean = 0.3f * window(t, 0.15f, 0.35f, 0.55f, 0.8f);
			hints.hips_drop = MIN(rise, 0.45f) * window(t, 0.1f, 0.3f, 0.6f, 0.85f);
			for (int s = 0; s < 2; s++) {
				hints.foot[s] = p_feet + UP * 0.45f + p_inward * 0.15f + left * (s == 0 ? 0.1f : -0.1f);
				hints.foot_weight[s] = window(t, 0.22f, 0.38f, 0.58f, 0.78f);
			}
		} break;
		default:
			hints.hips_drop = 0.0f;
			break;
	}
	const Ref<Animation> &clip = clips[clip_slot(action)];
	if (clip.is_valid() && active) {
		hints.clip = clip;
		hints.clip_time = t * clip->get_length();
		hints.clip_root_bone = clip_root_bone;
		Vector3 r;
		if (clip_root.size() >= 2) {
			const float f = t * float(clip_root.size() - 1);
			const int i = MIN(int(f), int(clip_root.size()) - 2);
			r = clip_root[i].lerp(clip_root[i + 1], f - float(i));
		}
		hints.clip_root_offset = r;
	} else {
		hints.clip.unref();
	}
}

void FellowTraversal3D::_step(double p_delta) {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	_step_body(p_delta);
	global_stats.steps.increment();
	global_stats.step_usec.add(OS::get_singleton()->get_ticks_usec() - t0);
}

void FellowTraversal3D::_step_body(double p_delta) {
	CollisionObject3D *body = _get_body();
	if (!body) {
		_finish(true);
		return;
	}
	time += p_delta;
	const double t = MIN(time / duration, 1.0);
	// A moving collider (a platform) carries the ledge.
	Vector3 edge = ledge.edge;
	Vector3 inward = ledge.inward;
	Vector3 target = ledge.target;
	if (const Node3D *collider = ObjectDB::get_instance<Node3D>(ledge.collider)) {
		const Transform3D delta = collider->get_global_transform() * collider_start.affine_inverse();
		if (!delta.is_equal_approx(Transform3D())) {
			edge = delta.xform(edge);
			target = delta.xform(target);
			const Vector3 i = flat(delta.basis.xform(inward));
			inward = i.length() > 1e-4f ? i.normalized() : inward;
		}
	}
	const Vector3 feet = _path(t, edge, inward, target);
	const float w = smooth(float(t) / 0.3f);
	const Quaternion q = start_xf.basis.get_rotation_quaternion().slerp(end_basis.get_rotation_quaternion(), w);
	const Basis basis = Basis(q).scaled_local(start_xf.basis.get_scale());
	const Transform3D xf = _body_at(feet, basis);
	const Vector3 previous = body->get_global_position();
	body->set_global_transform(xf);
	if (CharacterBody3D *cb = Object::cast_to<CharacterBody3D>(body)) {
		cb->set_velocity(p_delta > 0.0 ? (xf.origin - previous) / p_delta : Vector3());
	}
	_update_hints(feet, edge, inward);
	if (t >= 1.0) {
		_finish(false);
	}
}

void FellowTraversal3D::_finish(bool p_cancelled) {
	const Kind done = action;
	active = false;
	hints.active = false;
	hints.hand_weight[0] = hints.hand_weight[1] = 0.0f;
	hints.foot_weight[0] = hints.foot_weight[1] = 0.0f;
	hints.lean = 0.0f;
	hints.hips_drop = 0.0f;
	hints.clip.unref();
	cooldown = 0.25;
	if (CharacterBody3D *cb = Object::cast_to<CharacterBody3D>(_get_body())) {
		const float speed = done == KIND_VAULT ? MAX(entry_speed, exit_speed) : exit_speed;
		cb->set_velocity(p_cancelled ? Vector3() : ledge.inward * speed);
	}
	if (p_cancelled) {
		emit_signal(SNAME("mantle_cancelled"), int(done));
	} else {
		emit_signal(SNAME("mantle_finished"), int(done));
	}
}

void FellowTraversal3D::cancel() {
	if (active) {
		_finish(true);
	}
}

Dictionary FellowTraversal3D::find_ledge(const Vector3 &p_direction) {
	CollisionObject3D *body = _get_body();
	Vector3 d = p_direction;
	bool air = false;
	float speed = 0.0f;
	if (const CharacterBody3D *cb = Object::cast_to<CharacterBody3D>(body)) {
		if (d == Vector3()) {
			d = flat(intent).length() > 0.1f ? intent : cb->get_velocity();
		}
		air = !cb->is_on_floor();
		speed = flat(cb->get_velocity()).length();
	}
	last_check = _check(d, air && grab_in_air, speed);
	return _ledge_dict(last_check);
}

bool FellowTraversal3D::try_mantle(const Vector3 &p_direction) {
	if (active || !enabled) {
		return false;
	}
	CollisionObject3D *body = _get_body();
	Vector3 d = p_direction;
	bool air = false;
	float speed = 0.0f;
	if (const CharacterBody3D *cb = Object::cast_to<CharacterBody3D>(body)) {
		if (d == Vector3()) {
			d = flat(intent).length() > 0.1f ? intent : cb->get_velocity();
		}
		air = !cb->is_on_floor();
		speed = flat(cb->get_velocity()).length();
	}
	if (air && !grab_in_air) {
		return false;
	}
	last_check = _check(d, air, speed);
	return _start(last_check, speed);
}

Dictionary FellowTraversal3D::get_pose_targets() const {
	Dictionary d;
	d["active"] = hints.active;
	d["kind"] = int(hints.kind);
	d["progress"] = hints.progress;
	d["inward"] = hints.inward;
	d["hand_left"] = hints.hand[0];
	d["hand_right"] = hints.hand[1];
	d["hand_weights"] = Vector2(hints.hand_weight[0], hints.hand_weight[1]);
	d["foot_left"] = hints.foot[0];
	d["foot_right"] = hints.foot[1];
	d["foot_weights"] = Vector2(hints.foot_weight[0], hints.foot_weight[1]);
	d["lean"] = hints.lean;
	d["hips_drop"] = hints.hips_drop;
	d["foot_follow"] = Vector2(hints.foot_follow[0], hints.foot_follow[1]);
	d["clip"] = hints.clip;
	d["clip_time"] = hints.clip_time;
	return d;
}

Dictionary FellowTraversal3D::_ledge_dict(const Ledge &p_ledge) {
	Dictionary d;
	d["kind"] = int(p_ledge.kind);
	d["reason"] = p_ledge.reason;
	d["edge"] = p_ledge.edge;
	d["inward"] = p_ledge.inward;
	d["top_normal"] = p_ledge.top_normal;
	d["height"] = p_ledge.height;
	d["target"] = p_ledge.target;
	d["depth"] = p_ledge.depth;
	d["open_below"] = p_ledge.open_below;
	d["collider"] = ObjectDB::get_instance(p_ledge.collider);
	return d;
}

void FellowTraversal3D::_draw_debug() {
	if (!debug_draw || !is_inside_tree()) {
		return;
	}
	if (!debug_instance) {
		debug_mesh.instantiate();
		debug_material.instantiate();
		debug_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		debug_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		debug_material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
		debug_instance = memnew(MeshInstance3D);
		debug_instance->set_as_top_level(true);
		debug_instance->set_mesh(debug_mesh);
		debug_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		add_child(debug_instance, false, INTERNAL_MODE_BACK);
		debug_instance->set_global_transform(Transform3D());
	}
	debug_mesh->clear_surfaces();
	const Ledge &l = active ? ledge : last_check;
	if (l.kind == KIND_NONE) {
		return;
	}
	const bool ok = l.kind == KIND_LOW || l.kind == KIND_HIGH || l.kind == KIND_VAULT;
	const Color c = ok ? Color(0.2f, 1.0f, 0.3f) : Color(1.0f, 0.25f, 0.2f);
	debug_mesh->surface_begin(Mesh::PRIMITIVE_LINES, debug_material);
	auto line = [&](const Vector3 &a, const Vector3 &b, const Color &col) {
		debug_mesh->surface_set_color(col);
		debug_mesh->surface_add_vertex(a);
		debug_mesh->surface_set_color(col);
		debug_mesh->surface_add_vertex(b);
	};
	const Vector3 left = UP.cross(l.inward).normalized();
	line(l.edge - left * 0.4f, l.edge + left * 0.4f, c);
	line(l.edge, l.edge + l.inward * 0.3f, c);
	// The capsule's footprint where the body goes.
	for (int i = 0; i < 16; i++) {
		const float a0 = Math::TAU * i / 16.0f;
		const float a1 = Math::TAU * (i + 1) / 16.0f;
		const Vector3 o = l.target + UP * 0.02f;
		line(o + Vector3(Math::cos(a0), 0, Math::sin(a0)) * radius, o + Vector3(Math::cos(a1), 0, Math::sin(a1)) * radius, c);
		line(o + UP * height + Vector3(Math::cos(a0), 0, Math::sin(a0)) * radius, o + UP * height + Vector3(Math::cos(a1), 0, Math::sin(a1)) * radius, c);
	}
	if (active) {
		Vector3 prev = _path(0.0, ledge.edge, ledge.inward, ledge.target);
		for (int i = 1; i <= 16; i++) {
			const Vector3 p = _path(i / 16.0, ledge.edge, ledge.inward, ledge.target);
			line(prev, p, Color(1.0f, 0.85f, 0.2f));
			prev = p;
		}
	}
	debug_mesh->surface_end();
}

void FellowTraversal3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_physics_process_internal(!Engine::get_singleton()->is_editor_hint());
			skip_checks = int(uint64_t(get_instance_id()) % uint64_t(check_interval));
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (active) {
				_finish(true);
			}
			has_last_origin = false;
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			const double dt = get_physics_process_delta_time();
			CollisionObject3D *body = _get_body();
			if (!body) {
				break;
			}
			if (active) {
				_step(dt);
				has_last_origin = false;
			} else {
				cooldown -= dt;
				const Vector3 v = _body_velocity(body, dt);
				if (enabled && auto_mantle && cooldown <= 0.0) {
					const CharacterBody3D *cb = Object::cast_to<CharacterBody3D>(body);
					const bool air = cb && !cb->is_on_floor();
					const float speed = flat(v).length();
					// The player's intent when the controller gives it (a body pressed against a wall has lost its
					// velocity into it to the slide), else the body's velocity.
					const bool wants = flat(intent).length() > 0.1f;
					if ((wants || speed >= auto_min_speed) && (!air || (grab_in_air && v.y < 1.0f))) {
						// Nothing in reach last time: the next check check_interval frames later (staggered by
						// node); a wall in reach: every frame.
						if (skip_checks > 0) {
							skip_checks--;
						} else {
							last_check = _check(wants ? intent : v, air, speed);
							if (last_check.kind == KIND_NONE) {
								skip_checks = check_interval - 1;
							}
							if (!(air && last_check.kind == KIND_VAULT) && !(last_check.open_below && !air)) {
								_start(last_check, speed);
							}
						}
					}
				}
			}
			_draw_debug();
		} break;
	}
}

PackedStringArray FellowTraversal3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (!is_inside_tree()) {
		return warnings;
	}
	const CollisionObject3D *body = _get_body();
	if (!body) {
		if (body_path.is_empty()) {
			warnings.push_back(RTR("A FellowTraversal3D moves the body it is a child of: put it under a CharacterBody3D (or set Body Path)."));
		} else {
			warnings.push_back(vformat(RTR("Body Path (%s) leads to no CollisionObject3D."), String(body_path)));
		}
	} else {
		bool shape = false;
		for (int i = 0; i < body->get_child_count(); i++) {
			const CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(body->get_child(i));
			shape = shape || (cs && cs->get_shape().is_valid());
		}
		if (!shape) {
			warnings.push_back(RTR("The body has no CollisionShape3D child: its capsule (the room it needs on a ledge) is taken as 0.35 m by 1.8 m."));
		}
	}
	if (!(min_height < low_max_height && low_max_height <= max_height)) {
		warnings.push_back(RTR("The heights must grow: Min Height < Low Max Height <= Max Height."));
	}
	return warnings;
}

void FellowTraversal3D::set_body_path(const NodePath &p_path) {
	body_path = p_path;
	has_last_origin = false;
	update_configuration_warnings();
}

void FellowTraversal3D::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	if (!enabled && active) {
		_finish(true);
	}
}

void FellowTraversal3D::set_min_height(float p_height) {
	min_height = MAX(0.0f, p_height);
	update_configuration_warnings();
	update_gizmos();
}

void FellowTraversal3D::set_low_max_height(float p_height) {
	low_max_height = MAX(0.0f, p_height);
	update_configuration_warnings();
	update_gizmos();
}

void FellowTraversal3D::set_max_height(float p_height) {
	max_height = MAX(0.0f, p_height);
	update_configuration_warnings();
	update_gizmos();
}

void FellowTraversal3D::set_reach_distance(float p_distance) {
	reach_distance = MAX(0.0f, p_distance);
	update_gizmos();
}

void FellowTraversal3D::set_debug_draw(bool p_enabled) {
	debug_draw = p_enabled;
	if (!debug_draw && debug_instance) {
		debug_instance->queue_free();
		debug_instance = nullptr;
		debug_mesh.unref();
	}
}

void FellowTraversal3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("find_ledge", "direction"), &FellowTraversal3D::find_ledge, DEFVAL(Vector3()));
	ClassDB::bind_method(D_METHOD("try_mantle", "direction"), &FellowTraversal3D::try_mantle, DEFVAL(Vector3()));
	ClassDB::bind_method(D_METHOD("cancel"), &FellowTraversal3D::cancel);
	ClassDB::bind_method(D_METHOD("is_active"), &FellowTraversal3D::is_active);
	ClassDB::bind_method(D_METHOD("get_action"), &FellowTraversal3D::get_action);
	ClassDB::bind_method(D_METHOD("get_progress"), &FellowTraversal3D::get_progress);
	ClassDB::bind_method(D_METHOD("get_last_check"), &FellowTraversal3D::get_last_check);
	ClassDB::bind_method(D_METHOD("get_pose_targets"), &FellowTraversal3D::get_pose_targets);
	ClassDB::bind_static_method("FellowTraversal3D", D_METHOD("get_global_stats"), &FellowTraversal3D::get_global_stats);
	ClassDB::bind_static_method("FellowTraversal3D", D_METHOD("reset_global_stats"), &FellowTraversal3D::reset_global_stats);
	ClassDB::bind_method(D_METHOD("set_intent", "direction"), &FellowTraversal3D::set_intent);
	ClassDB::bind_method(D_METHOD("get_intent"), &FellowTraversal3D::get_intent);

	ClassDB::bind_method(D_METHOD("set_body_path", "path"), &FellowTraversal3D::set_body_path);
	ClassDB::bind_method(D_METHOD("get_body_path"), &FellowTraversal3D::get_body_path);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &FellowTraversal3D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &FellowTraversal3D::is_enabled);
	ClassDB::bind_method(D_METHOD("set_auto_mantle", "enabled"), &FellowTraversal3D::set_auto_mantle);
	ClassDB::bind_method(D_METHOD("is_auto_mantle"), &FellowTraversal3D::is_auto_mantle);
	ClassDB::bind_method(D_METHOD("set_auto_min_speed", "speed"), &FellowTraversal3D::set_auto_min_speed);
	ClassDB::bind_method(D_METHOD("get_auto_min_speed"), &FellowTraversal3D::get_auto_min_speed);
	ClassDB::bind_method(D_METHOD("set_grab_in_air", "enabled"), &FellowTraversal3D::set_grab_in_air);
	ClassDB::bind_method(D_METHOD("is_grab_in_air"), &FellowTraversal3D::is_grab_in_air);
	ClassDB::bind_method(D_METHOD("set_vault", "enabled"), &FellowTraversal3D::set_vault);
	ClassDB::bind_method(D_METHOD("is_vault"), &FellowTraversal3D::is_vault);
	ClassDB::bind_method(D_METHOD("set_align_to_wall", "enabled"), &FellowTraversal3D::set_align_to_wall);
	ClassDB::bind_method(D_METHOD("is_align_to_wall"), &FellowTraversal3D::is_align_to_wall);
	ClassDB::bind_method(D_METHOD("set_min_height", "height"), &FellowTraversal3D::set_min_height);
	ClassDB::bind_method(D_METHOD("get_min_height"), &FellowTraversal3D::get_min_height);
	ClassDB::bind_method(D_METHOD("set_low_max_height", "height"), &FellowTraversal3D::set_low_max_height);
	ClassDB::bind_method(D_METHOD("get_low_max_height"), &FellowTraversal3D::get_low_max_height);
	ClassDB::bind_method(D_METHOD("set_max_height", "height"), &FellowTraversal3D::set_max_height);
	ClassDB::bind_method(D_METHOD("get_max_height"), &FellowTraversal3D::get_max_height);
	ClassDB::bind_method(D_METHOD("set_vault_max_height", "height"), &FellowTraversal3D::set_vault_max_height);
	ClassDB::bind_method(D_METHOD("get_vault_max_height"), &FellowTraversal3D::get_vault_max_height);
	ClassDB::bind_method(D_METHOD("set_vault_max_depth", "depth"), &FellowTraversal3D::set_vault_max_depth);
	ClassDB::bind_method(D_METHOD("get_vault_max_depth"), &FellowTraversal3D::get_vault_max_depth);
	ClassDB::bind_method(D_METHOD("set_vault_min_speed", "speed"), &FellowTraversal3D::set_vault_min_speed);
	ClassDB::bind_method(D_METHOD("get_vault_min_speed"), &FellowTraversal3D::get_vault_min_speed);
	ClassDB::bind_method(D_METHOD("set_reach_distance", "distance"), &FellowTraversal3D::set_reach_distance);
	ClassDB::bind_method(D_METHOD("get_reach_distance"), &FellowTraversal3D::get_reach_distance);
	ClassDB::bind_method(D_METHOD("set_check_interval", "frames"), &FellowTraversal3D::set_check_interval);
	ClassDB::bind_method(D_METHOD("get_check_interval"), &FellowTraversal3D::get_check_interval);
	ClassDB::bind_method(D_METHOD("set_max_wall_angle", "degrees"), &FellowTraversal3D::set_max_wall_angle);
	ClassDB::bind_method(D_METHOD("get_max_wall_angle"), &FellowTraversal3D::get_max_wall_angle);
	ClassDB::bind_method(D_METHOD("set_max_ledge_slope", "degrees"), &FellowTraversal3D::set_max_ledge_slope);
	ClassDB::bind_method(D_METHOD("get_max_ledge_slope"), &FellowTraversal3D::get_max_ledge_slope);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &FellowTraversal3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &FellowTraversal3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_low_duration", "seconds"), &FellowTraversal3D::set_low_duration);
	ClassDB::bind_method(D_METHOD("get_low_duration"), &FellowTraversal3D::get_low_duration);
	ClassDB::bind_method(D_METHOD("set_high_duration", "seconds"), &FellowTraversal3D::set_high_duration);
	ClassDB::bind_method(D_METHOD("get_high_duration"), &FellowTraversal3D::get_high_duration);
	ClassDB::bind_method(D_METHOD("set_vault_duration", "seconds"), &FellowTraversal3D::set_vault_duration);
	ClassDB::bind_method(D_METHOD("get_vault_duration"), &FellowTraversal3D::get_vault_duration);
	ClassDB::bind_method(D_METHOD("set_exit_speed", "speed"), &FellowTraversal3D::set_exit_speed);
	ClassDB::bind_method(D_METHOD("get_exit_speed"), &FellowTraversal3D::get_exit_speed);
	ClassDB::bind_method(D_METHOD("set_low_clip", "clip"), &FellowTraversal3D::set_low_clip);
	ClassDB::bind_method(D_METHOD("get_low_clip"), &FellowTraversal3D::get_low_clip);
	ClassDB::bind_method(D_METHOD("set_high_clip", "clip"), &FellowTraversal3D::set_high_clip);
	ClassDB::bind_method(D_METHOD("get_high_clip"), &FellowTraversal3D::get_high_clip);
	ClassDB::bind_method(D_METHOD("set_vault_clip", "clip"), &FellowTraversal3D::set_vault_clip);
	ClassDB::bind_method(D_METHOD("get_vault_clip"), &FellowTraversal3D::get_vault_clip);
	ClassDB::bind_method(D_METHOD("set_clip_root_bone", "bone"), &FellowTraversal3D::set_clip_root_bone);
	ClassDB::bind_method(D_METHOD("get_clip_root_bone"), &FellowTraversal3D::get_clip_root_bone);
	ClassDB::bind_method(D_METHOD("set_debug_draw", "enabled"), &FellowTraversal3D::set_debug_draw);
	ClassDB::bind_method(D_METHOD("is_debug_draw"), &FellowTraversal3D::is_debug_draw);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "body_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "CollisionObject3D"), "set_body_path", "get_body_path");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_mantle"), "set_auto_mantle", "is_auto_mantle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "auto_min_speed", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m/s"), "set_auto_min_speed", "get_auto_min_speed");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "grab_in_air"), "set_grab_in_air", "is_grab_in_air");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "align_to_wall"), "set_align_to_wall", "is_align_to_wall");
	ADD_GROUP("Heights", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_height", PROPERTY_HINT_RANGE, "0,3,0.01,or_greater,suffix:m"), "set_min_height", "get_min_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "low_max_height", PROPERTY_HINT_RANGE, "0,3,0.01,or_greater,suffix:m"), "set_low_max_height", "get_low_max_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_height", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater,suffix:m"), "set_max_height", "get_max_height");
	ADD_GROUP("Detection", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reach_distance", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:m"), "set_reach_distance", "get_reach_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_wall_angle", PROPERTY_HINT_RANGE, "0,89,0.1,degrees"), "set_max_wall_angle", "get_max_wall_angle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_ledge_slope", PROPERTY_HINT_RANGE, "0,89,0.1,degrees"), "set_max_ledge_slope", "get_max_ledge_slope");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "check_interval", PROPERTY_HINT_RANGE, "1,8,1"), "set_check_interval", "get_check_interval");
	ADD_GROUP("Vault", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "vault"), "set_vault", "is_vault");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vault_max_height", PROPERTY_HINT_RANGE, "0,3,0.01,or_greater,suffix:m"), "set_vault_max_height", "get_vault_max_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vault_max_depth", PROPERTY_HINT_RANGE, "0.05,2,0.01,or_greater,suffix:m"), "set_vault_max_depth", "get_vault_max_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vault_min_speed", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m/s"), "set_vault_min_speed", "get_vault_min_speed");
	ADD_GROUP("Motion", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "low_duration", PROPERTY_HINT_RANGE, "0.05,3,0.01,or_greater,suffix:s"), "set_low_duration", "get_low_duration");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "high_duration", PROPERTY_HINT_RANGE, "0.05,3,0.01,or_greater,suffix:s"), "set_high_duration", "get_high_duration");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vault_duration", PROPERTY_HINT_RANGE, "0.05,3,0.01,or_greater,suffix:s"), "set_vault_duration", "get_vault_duration");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "exit_speed", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m/s"), "set_exit_speed", "get_exit_speed");
	ADD_GROUP("Clips", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "low_clip", PROPERTY_HINT_RESOURCE_TYPE, "Animation"), "set_low_clip", "get_low_clip");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "high_clip", PROPERTY_HINT_RESOURCE_TYPE, "Animation"), "set_high_clip", "get_high_clip");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "vault_clip", PROPERTY_HINT_RESOURCE_TYPE, "Animation"), "set_vault_clip", "get_vault_clip");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "clip_root_bone"), "set_clip_root_bone", "get_clip_root_bone");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_draw"), "set_debug_draw", "is_debug_draw");

	ADD_SIGNAL(MethodInfo("mantle_started", PropertyInfo(Variant::INT, "kind"), PropertyInfo(Variant::DICTIONARY, "ledge")));
	ADD_SIGNAL(MethodInfo("mantle_finished", PropertyInfo(Variant::INT, "kind")));
	ADD_SIGNAL(MethodInfo("mantle_cancelled", PropertyInfo(Variant::INT, "kind")));

	BIND_ENUM_CONSTANT(KIND_NONE);
	BIND_ENUM_CONSTANT(KIND_STEP);
	BIND_ENUM_CONSTANT(KIND_LOW);
	BIND_ENUM_CONSTANT(KIND_HIGH);
	BIND_ENUM_CONSTANT(KIND_VAULT);
	BIND_ENUM_CONSTANT(KIND_TOO_HIGH);
	BIND_ENUM_CONSTANT(KIND_BLOCKED);
	BIND_ENUM_CONSTANT(KIND_MAX);
}
