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

#include "fellow_ladder_3d.h"

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
	d["hangs"] = int64_t(global_stats.hangs.get());
	d["transfers"] = int64_t(global_stats.transfers.get());
	d["ladders"] = int64_t(global_stats.ladders.get());
	return d;
}

void FellowTraversal3D::reset_global_stats() {
	for (SafeNumeric<uint64_t> *v : { &global_stats.checks, &global_stats.check_usec, &global_stats.steps, &global_stats.step_usec, &global_stats.mantles[0], &global_stats.mantles[1], &global_stats.mantles[2], &global_stats.poses, &global_stats.pose_usec, &global_stats.hangs, &global_stats.transfers, &global_stats.ladders }) {
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
	if (action == KIND_HANG) {
		_step_hang(p_delta);
		return;
	}
	if (action == KIND_LADDER) {
		_step_ladder(p_delta);
		return;
	}
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
	if (!active) {
		return;
	}
	if (action == KIND_HANG) {
		_end_hang(false);
	} else if (action == KIND_LADDER) {
		_end_ladder(false, Vector3());
	} else {
		_finish(true);
	}
}

// Milestone 75: the body placed (its feet, facing p_facing: horizontal), its velocity the move's.
void FellowTraversal3D::_place(CollisionObject3D *p_body, const Vector3 &p_feet, const Vector3 &p_facing, double p_delta) {
	const Transform3D current = p_body->get_global_transform();
	const Vector3 scale = current.basis.get_scale();
	Basis basis = current.basis;
	const Vector3 f = flat(p_facing);
	if (align_to_wall && f.length() > 1e-3f) {
		const float w = smooth(float(hold_time / 0.25));
		const Quaternion q = start_xf.basis.get_rotation_quaternion().slerp(Basis::looking_at(f.normalized(), UP).get_rotation_quaternion(), w);
		basis = Basis(q).scaled_local(scale);
	}
	const Transform3D xf = _body_at(p_feet, basis);
	const Vector3 previous = current.origin;
	p_body->set_global_transform(xf);
	if (CharacterBody3D *cb = Object::cast_to<CharacterBody3D>(p_body)) {
		cb->set_velocity(p_delta > 0.0 ? (xf.origin - previous) / p_delta : Vector3());
	}
}

// Hanging: the hands hang_reach over the feet at the edge, the capsule radius + 0.08 m off the wall's face.
Vector3 FellowTraversal3D::_hang_feet_at(const Vector3 &p_edge, const Vector3 &p_inward) const {
	return p_edge - p_inward * (radius + 0.08f) - UP * hang_reach;
}

// The ledge p_along metres along the edge (positive: the body's left, facing the wall): the wall's face just under the
// top found there, the top over it. False where the ledge ends: a corner, a gap, a step of more than 0.3 m, a face
// turned more than 50 degrees, a slope.
bool FellowTraversal3D::_probe_edge(const Vector3 &p_edge, const Vector3 &p_inward, float p_along, Ledge &r_ledge) const {
	CollisionObject3D *body = _get_body();
	if (!body || body->get_world_3d().is_null()) {
		return false;
	}
	PhysicsDirectSpaceState3D *space = body->get_world_3d()->get_direct_space_state();
	HashSet<RID> exclude;
	exclude.insert(body->get_rid());
	const Vector3 left = UP.cross(p_inward).normalized();
	const Vector3 p = p_edge + left * p_along;
	PhysicsDirectSpaceState3D::RayParameters fq;
	fq.from = p - p_inward * (radius + 0.3f) - UP * 0.08f;
	fq.to = p + p_inward * 0.3f - UP * 0.08f;
	fq.exclude = exclude;
	fq.collision_mask = collision_mask;
	PhysicsDirectSpaceState3D::RayResult fr;
	if (!space->intersect_ray(fq, fr)) {
		return false;
	}
	const Vector3 wn = flat(fr.normal);
	if (wn.length() < 0.6f) {
		return false;
	}
	const Vector3 inward = -wn.normalized();
	if (inward.dot(p_inward) < Math::cos(Math::deg_to_rad(50.0f))) {
		return false;
	}
	const Vector3 column = fr.position + inward * MAX(0.1f, radius * 0.3f);
	PhysicsDirectSpaceState3D::RayParameters tq;
	tq.from = Vector3(column.x, p_edge.y + 0.3f, column.z);
	tq.to = Vector3(column.x, p_edge.y - 0.3f, column.z);
	tq.exclude = exclude;
	tq.collision_mask = collision_mask;
	tq.hit_from_inside = true;
	PhysicsDirectSpaceState3D::RayResult tr;
	if (!space->intersect_ray(tq, tr) || tr.normal == Vector3() || tr.normal.y < Math::cos(Math::deg_to_rad(max_ledge_slope))) {
		return false;
	}
	r_ledge = ledge;
	r_ledge.kind = KIND_HANG;
	r_ledge.edge = Vector3(fr.position.x, tr.position.y, fr.position.z);
	r_ledge.inward = inward;
	r_ledge.top_normal = tr.normal;
	r_ledge.collider = tr.collider_id;
	return true;
}

// Milestone 81 (decision 0189): how far the edge goes on from where the body hangs (towards p_sign: + its left),
// found in 5 cm steps up to a metre.
float FellowTraversal3D::_edge_end(float p_sign) const {
	float last = 0.0f;
	Ledge l;
	for (float a = 0.05f; a <= 1.0f; a += 0.05f) {
		if (!_probe_edge(ledge.edge, ledge.inward, p_sign * a, l)) {
			break;
		}
		last = a;
	}
	return last;
}

// Where the ledge ends towards p_sign, the ledge round the corner: a wall meeting it (found looking on along the edge
// from where the body hangs) or the wall turning away (its side found from beyond the edge's end, a little behind the
// face, looking back); hung from at the same height, both hands on it, with room for the body there.
bool FellowTraversal3D::_find_corner(float p_sign, Ledge &r_ledge, Vector3 &r_control, int &r_how) const {
	CollisionObject3D *body = _get_body();
	if (!body || body->get_world_3d().is_null()) {
		return false;
	}
	PhysicsDirectSpaceState3D *space = body->get_world_3d()->get_direct_space_state();
	HashSet<RID> exclude;
	exclude.insert(body->get_rid());
	const Vector3 inward = ledge.inward;
	const Vector3 dir = UP.cross(inward).normalized() * p_sign;
	const Vector3 from_feet = _hang_feet_at(ledge.edge, inward);
	auto usable = [&](const Ledge &p_l) {
		Ledge hand;
		return Math::abs(p_l.edge.y - ledge.edge.y) < 0.3f && _probe_edge(p_l.edge, p_l.inward, 0.24f, hand) && _probe_edge(p_l.edge, p_l.inward, -0.24f, hand) && _capsule_free(_hang_feet_at(p_l.edge, p_l.inward) + UP * 0.02f, radius * 0.95f, height - 0.04f, exclude);
	};
	// A wall meeting it: its face across the way on.
	{
		PhysicsDirectSpaceState3D::RayParameters rq;
		rq.from = ledge.edge - inward * 0.12f - UP * 0.08f;
		rq.to = rq.from + dir * (radius + 0.7f);
		rq.exclude = exclude;
		rq.collision_mask = collision_mask;
		PhysicsDirectSpaceState3D::RayResult rr;
		if (space->intersect_ray(rq, rr)) {
			const Vector3 n = flat(rr.normal);
			if (n.length() > 0.6f && n.normalized().dot(-dir) > 0.8f) {
				Vector3 at = rr.position - inward * (radius + 0.25f);
				at.y = ledge.edge.y;
				Ledge l;
				if (_probe_edge(at, dir, 0.0f, l) && usable(l)) {
					r_ledge = l;
					r_control = (from_feet + _hang_feet_at(l.edge, l.inward)) * 0.5f;
					r_how = TRANSFER_INNER_CORNER;
					return true;
				}
			}
		}
	}
	// The wall turning away: its side, 0.32 m behind the face (the near hand 8 cm from the corner).
	const Vector3 c = ledge.edge + dir * _edge_end(p_sign);
	PhysicsDirectSpaceState3D::RayParameters rq;
	rq.from = c + dir * (radius + 0.45f) + inward * 0.32f - UP * 0.08f;
	rq.to = rq.from - dir * (radius + 0.9f);
	rq.exclude = exclude;
	rq.collision_mask = collision_mask;
	PhysicsDirectSpaceState3D::RayResult rr;
	if (space->intersect_ray(rq, rr)) {
		const Vector3 n = flat(rr.normal);
		Ledge l;
		if (n.length() > 0.6f && n.normalized().dot(dir) > 0.8f && _probe_edge(Vector3(rr.position.x, ledge.edge.y, rr.position.z), -dir, 0.0f, l) && usable(l)) {
			// Round the corner, wide of it.
			const float d = (radius + 0.08f) * 1.3f;
			r_ledge = l;
			r_control = c - inward * d + dir * d - UP * hang_reach;
			r_how = TRANSFER_OUTER_CORNER;
			return true;
		}
	}
	return false;
}

// A ledge to jump to from the hang, the way the controller wishes: along the edge past its end (the first edge within
// hang_jump_distance, at about the same height, hung from 0.3 m into it), up (within hang_jump_height, both hands on
// it) or (milestone 86) down (within hang_jump_depth, both hands on it); room for the body there and on the way.
bool FellowTraversal3D::_find_jump(const Vector3 &p_wish, Ledge &r_ledge, Vector3 &r_control, int &r_how) const {
	CollisionObject3D *body = _get_body();
	if (!body) {
		return false;
	}
	HashSet<RID> exclude;
	exclude.insert(body->get_rid());
	const Vector3 left = UP.cross(ledge.inward).normalized();
	const float along = p_wish.dot(left);
	const float up = MAX(p_wish.y, flat(p_wish).dot(ledge.inward));
	const Vector3 from_feet = _hang_feet_at(ledge.edge, ledge.inward);
	auto room = [&](const Vector3 &p_feet) {
		return _capsule_free(p_feet + UP * 0.02f, radius * 0.95f, height - 0.04f, exclude);
	};
	Ledge l;
	Ledge hold;
	if (Math::abs(along) > 0.5f) {
		const float s = SIGN(along);
		// The first edge found past the gap; hung from 0.3 m into it (the near hand 6 cm in).
		const float a0 = _edge_end(s) + 0.3f;
		for (float a = a0; a <= a0 + hang_jump_distance; a += 0.1f) {
			if (!_probe_edge(ledge.edge, ledge.inward, s * a, hold)) {
				continue;
			}
			if (!_probe_edge(ledge.edge, ledge.inward, s * (a + 0.3f), l) || !_probe_edge(ledge.edge, ledge.inward, s * (a + 0.55f), hold)) {
				break;
			}
			const Vector3 to_feet = _hang_feet_at(l.edge, l.inward);
			if (Math::abs(l.edge.y - ledge.edge.y) < 0.5f && room(to_feet) && room((from_feet + to_feet) * 0.5f + UP * 0.3f)) {
				r_ledge = l;
				r_control = (from_feet + to_feet) * 0.5f + UP * 0.35f;
				r_how = TRANSFER_JUMP_ALONG;
				return true;
			}
			break;
		}
	} else if (p_wish.y < -0.5f) {
		// Milestone 86: down to a ledge below (let go of this one, caught there).
		for (float h = 0.6f; h <= hang_jump_depth; h += 0.1f) {
			if (!_probe_edge(ledge.edge - UP * h, ledge.inward, 0.0f, l) || l.edge.y > ledge.edge.y - 0.4f) {
				continue;
			}
			if (!_probe_edge(l.edge, l.inward, 0.24f, hold) || !_probe_edge(l.edge, l.inward, -0.24f, hold)) {
				continue;
			}
			// Out to the lower ledge's face (and 0.15 m more) first, then down: clear of a ledge that stands out.
			const Vector3 to_feet = _hang_feet_at(l.edge, l.inward);
			const Vector3 control = Vector3(to_feet.x, from_feet.y, to_feet.z) - ledge.inward * 0.15f;
			if (room(to_feet) && room(control) && room(from_feet * 0.25f + control * 0.5f + to_feet * 0.25f)) {
				r_ledge = l;
				r_control = control;
				r_how = TRANSFER_JUMP_DOWN;
				return true;
			}
		}
	} else if (up > 0.5f) {
		for (float h = 0.6f; h <= hang_jump_height; h += 0.1f) {
			if (!_probe_edge(ledge.edge + UP * h, ledge.inward, 0.0f, l) || l.edge.y < ledge.edge.y + 0.4f) {
				continue;
			}
			if (!_probe_edge(l.edge, l.inward, 0.24f, hold) || !_probe_edge(l.edge, l.inward, -0.24f, hold)) {
				continue;
			}
			const Vector3 to_feet = _hang_feet_at(l.edge, l.inward);
			if (room(to_feet) && room((from_feet + to_feet) * 0.5f - ledge.inward * 0.15f)) {
				r_ledge = l;
				r_control = (from_feet + to_feet) * 0.5f - ledge.inward * 0.15f;
				r_how = TRANSFER_JUMP_UP;
				return true;
			}
		}
	}
	return false;
}

// Milestone 84 (decision 0192): the edge of the floor the body walks over (p_direction, flat): no floor a step ahead
// within drop_min (1 m) below, the floor's side found looking back from there; hung from facing back into it, both
// hands on it, room for the body hanging and just over the edge outside it (r_mid: where it goes before it goes down).
bool FellowTraversal3D::_find_drop(const Vector3 &p_direction, Ledge &r_ledge, Vector3 &r_mid) const {
	CollisionObject3D *body = _get_body();
	if (!body || body->get_world_3d().is_null()) {
		return false;
	}
	const Vector3 dir = flat(p_direction);
	if (dir.length() < 1e-3f) {
		return false;
	}
	const Vector3 d = dir.normalized();
	PhysicsDirectSpaceState3D *space = body->get_world_3d()->get_direct_space_state();
	HashSet<RID> exclude;
	exclude.insert(body->get_rid());
	const Vector3 feet = _feet_of(body->get_global_transform());
	const Vector3 ahead = feet + d * (radius + 0.2f);
	PhysicsDirectSpaceState3D::RayParameters dq;
	dq.from = ahead + UP * 0.3f;
	dq.to = ahead - UP * 1.0f;
	dq.exclude = exclude;
	dq.collision_mask = collision_mask;
	PhysicsDirectSpaceState3D::RayResult dr;
	if (space->intersect_ray(dq, dr)) {
		return false; // the floor goes on (or steps down less than a metre)
	}
	PhysicsDirectSpaceState3D::RayParameters bq;
	bq.from = ahead - UP * 0.12f;
	bq.to = bq.from - d * (radius + 0.45f);
	bq.exclude = exclude;
	bq.collision_mask = collision_mask;
	PhysicsDirectSpaceState3D::RayResult br;
	if (!space->intersect_ray(bq, br) || flat(br.normal).length() < 0.6f || flat(br.normal).normalized().dot(d) < 0.8f) {
		return false;
	}
	Ledge l;
	if (!_probe_edge(Vector3(br.position.x, feet.y, br.position.z), -d, 0.0f, l) || Math::abs(l.edge.y - feet.y) > 0.15f) {
		return false;
	}
	Ledge hand;
	if (!_probe_edge(l.edge, l.inward, 0.24f, hand) || !_probe_edge(l.edge, l.inward, -0.24f, hand)) {
		return false;
	}
	const Vector3 mid = Vector3(l.edge.x, feet.y + 0.02f, l.edge.z) - l.inward * (radius + 0.1f);
	if (!_capsule_free(_hang_feet_at(l.edge, l.inward) + UP * 0.02f, radius * 0.95f, height - 0.04f, exclude) || !_capsule_free(mid + UP * 0.02f, radius * 0.95f, height - 0.04f, exclude)) {
		return false;
	}
	r_ledge = l;
	r_mid = mid;
	return true;
}

// Into the hang over the edge: out over it, then down, turning to face the wall; the hands reaching for the edge.
bool FellowTraversal3D::_start_drop(const Ledge &p_ledge, const Vector3 &p_mid) {
	CollisionObject3D *body = _get_body();
	if (!body) {
		return false;
	}
	const Vector3 feet = _feet_of(body->get_global_transform());
	const Vector3 facing = -body->get_global_transform().basis.get_column(2);
	const Vector3 wish = flat(intent);
	if (!_start_hang(p_ledge, false)) {
		return false;
	}
	_start_transfer(ledge, p_mid, TRANSFER_DROP, drop_duration);
	transfer.from_feet = feet;
	transfer.from_inward = flat(facing).length() > 1e-3f ? flat(facing).normalized() : -ledge.inward;
	const Vector3 left = UP.cross(ledge.inward).normalized();
	transfer.from_hands[0] = ledge.edge + left * 0.24f + ledge.inward * 0.06f + UP * 0.02f;
	transfer.from_hands[1] = ledge.edge - left * 0.24f + ledge.inward * 0.06f + UP * 0.02f;
	// The wish that walked it over the edge points away from the wall: held as it was, it does nothing (it would let
	// go); changed, it is the world's again.
	remap = wish.length() > 0.3f;
	remap_from = remap ? wish.normalized() : Vector3();
	remap_to = Vector3();
	_step_transfer(0.0);
	return true;
}

bool FellowTraversal3D::try_drop(const Vector3 &p_direction) {
	CollisionObject3D *body = _get_body();
	if (!body || active || !hang) {
		return false;
	}
	_read_body_shape(body);
	Vector3 d = flat(p_direction);
	if (d.length() < 1e-3f) {
		d = flat(intent);
	}
	if (d.length() < 1e-3f) {
		d = flat(-body->get_global_transform().basis.get_column(2));
	}
	Ledge l;
	Vector3 mid;
	return _find_drop(d, l, mid) && _start_drop(l, mid);
}

void FellowTraversal3D::_start_transfer(const Ledge &p_to, const Vector3 &p_control, int p_how, double p_duration) {
	transfer.active = true;
	transfer.how = p_how;
	transfer.t = 0.0;
	transfer.duration = MAX(p_duration, 0.05);
	transfer.from_feet = hang_feet;
	transfer.control = p_control;
	transfer.to = p_to;
	transfer.to.kind = KIND_HANG;
	transfer.to_feet = _hang_feet_at(p_to.edge, p_to.inward);
	transfer.from_inward = ledge.inward;
	transfer.from_hands[0] = hints.hand[0];
	transfer.from_hands[1] = hints.hand[1];
	if (p_how == TRANSFER_JUMP_ALONG || p_how == TRANSFER_JUMP_UP || p_how == TRANSFER_JUMP_DOWN) {
		shimmy_velocity = 0.0f;
	}
	global_stats.transfers.increment();
}

// The body on its way to the other ledge: its feet along the curve, its facing turning, the hands moving to the new
// edge (let go of in a jump, caught at its end).
void FellowTraversal3D::_step_transfer(double p_delta) {
	CollisionObject3D *body = _get_body();
	if (!body) {
		_end_hang(false);
		return;
	}
	transfer.t += p_delta / transfer.duration;
	const float u = float(MIN(transfer.t, 1.0));
	const float w = smooth(u);
	Vector3 feet = transfer.from_feet * ((1.0f - w) * (1.0f - w)) + transfer.control * (2.0f * (1.0f - w) * w) + transfer.to_feet * (w * w);
	if (transfer.how == TRANSFER_DROP) {
		// Out over the edge (the first 40 %), then straight down.
		feet = u < 0.4f ? transfer.from_feet.lerp(transfer.control, smooth(u / 0.4f)) : transfer.control.lerp(transfer.to_feet, smooth((u - 0.4f) / 0.6f));
	}
	const Vector3 a = flat(transfer.from_inward).normalized();
	const Vector3 b = flat(transfer.to.inward).normalized();
	const float angle = Math::atan2(a.cross(b).dot(UP), a.dot(b));
	const Vector3 inward = a.rotated(UP, angle * w);
	_place(body, feet, inward, p_delta);
	hang_feet = feet;
	const Vector3 left = UP.cross(inward).normalized();
	const Vector3 to_left = UP.cross(transfer.to.inward).normalized();
	const bool jump = transfer.how == TRANSFER_JUMP_ALONG || transfer.how == TRANSFER_JUMP_UP || transfer.how == TRANSFER_JUMP_DOWN;
	hints.active = true;
	hints.kind = KIND_HANG;
	hints.inward = inward;
	hints.left = left;
	const Vector3 to_hands[2] = {
		transfer.to.edge + to_left * 0.24f + transfer.to.inward * 0.06f + UP * 0.02f,
		transfer.to.edge - to_left * 0.24f + transfer.to.inward * 0.06f + UP * 0.02f,
	};
	const float hold = jump ? 1.0f - 0.85f * Math::sin(u * float(Math::PI)) : (transfer.how == TRANSFER_DROP ? smooth(MIN(1.0f, u * 2.0f)) : 1.0f);
	const Vector3 face = feet + inward * (radius + 0.02f) + UP * 0.35f;
	for (int h = 0; h < 2; h++) {
		hints.hand[h] = transfer.from_hands[h].lerp(to_hands[h], w);
		hints.hand_weight[h] = hold;
		hints.foot[h] = face + left * (h == 0 ? 0.14f : -0.14f);
		hints.foot_weight[h] = jump ? 0.3f : (transfer.how == TRANSFER_DROP ? 0.8f * smooth(MAX(0.0f, u - 0.4f) / 0.6f) : 0.8f);
		hints.foot_follow[h] = 1.0f;
	}
	hints.progress = 0.0f;
	hints.lean = 0.0f;
	hints.hips_drop = 0.0f;
	hints.clip.unref();
	if (transfer.t >= 1.0) {
		transfer.active = false;
		ledge = transfer.to;
		hang_feet = transfer.to_feet;
		const Node3D *collider = ObjectDB::get_instance<Node3D>(ledge.collider);
		collider_start = collider ? collider->get_global_transform() : Transform3D();
		if (jump) {
			push_armed = false;
		}
		emit_signal(SNAME("hang_transferred"), _ledge_dict(ledge), transfer.how);
	}
}

bool FellowTraversal3D::_start_hang(const Ledge &p_ledge, bool p_step) {
	CollisionObject3D *body = _get_body();
	if (!body || !hang || p_ledge.inward == Vector3()) {
		return false;
	}
	const Vector3 feet = _hang_feet_at(p_ledge.edge, p_ledge.inward);
	HashSet<RID> exclude;
	exclude.insert(body->get_rid());
	if (!_capsule_free(feet + UP * 0.02f, radius * 0.95f, height - 0.04f, exclude)) {
		return false;
	}
	ledge = p_ledge;
	ledge.kind = KIND_HANG;
	action = KIND_HANG;
	start_xf = body->get_global_transform();
	start_feet = _feet_of(start_xf);
	hang_feet = feet;
	time = 0.0;
	duration = 1.0;
	hold_time = 0.0;
	shimmy_phase = 0.0f;
	shimmy_velocity = 0.0f;
	climb_requested = false;
	release_requested = false;
	push_armed = false;
	jump_requested = false;
	transfer.active = false;
	remap = false;
	corner_wait = 0.0;
	const Node3D *collider = ObjectDB::get_instance<Node3D>(ledge.collider);
	collider_start = collider ? collider->get_global_transform() : Transform3D();
	active = true;
	global_stats.hangs.increment();
	emit_signal(SNAME("hang_started"), _ledge_dict(ledge));
	if (p_step) {
		_step_hang(0.0); // the pose's targets from this frame
	}
	return true;
}

void FellowTraversal3D::_step_hang(double p_delta) {
	CollisionObject3D *body = _get_body();
	if (!body) {
		_end_hang(false);
		return;
	}
	hold_time += p_delta;
	// A moving collider carries the ledge.
	if (const Node3D *collider = ObjectDB::get_instance<Node3D>(ledge.collider)) {
		const Transform3D now = collider->get_global_transform();
		const Transform3D delta = now * collider_start.affine_inverse();
		if (!delta.is_equal_approx(Transform3D())) {
			ledge.edge = delta.xform(ledge.edge);
			const Vector3 i = flat(delta.basis.xform(ledge.inward));
			ledge.inward = i.length() > 1e-4f ? i.normalized() : ledge.inward;
		}
		collider_start = now;
	}
	if (transfer.active) {
		_step_transfer(p_delta);
		return;
	}
	Vector3 wish = intent;
	if (remap) {
		const Vector3 f = flat(intent);
		if (f.length() > 0.3f && f.normalized().dot(remap_from) > 0.7f) {
			wish = remap_to * f.length() + UP * intent.y;
		} else {
			remap = false;
		}
	}
	if (jump_requested) {
		// Milestone 81: a jump to another ledge the way the controller wishes (nothing there: nothing happens).
		jump_requested = false;
		Ledge to;
		Vector3 control;
		int how = 0;
		if (hold_time > 0.25 && _find_jump(wish, to, control, how)) {
			_start_transfer(to, control, how, how == TRANSFER_JUMP_UP ? 0.45 : (how == TRANSFER_JUMP_DOWN ? 0.4 : 0.5));
			_step_transfer(p_delta);
			return;
		}
	}
	const Vector3 left = UP.cross(ledge.inward).normalized();
	// The controller's wish: along the edge (shimmy), into the wall or up (climb), away or down (let go).
	const float along = wish.dot(left);
	const float in = flat(wish).dot(ledge.inward);
	if (release_requested || wish.y < -0.5f || in < -0.6f) {
		_end_hang(false);
		return;
	}
	if (in < 0.3f) {
		push_armed = true; // the push that jumped at the wall let go of: the next one climbs
	}
	if (hold_time > 0.25 && (climb_requested || wish.y > 0.5f || (in > 0.6f && push_armed))) {
		climb_requested = false;
		// A mantle from here, if there is room on the top.
		const Ledge l = _check(ledge.inward, true, 0.0f);
		last_check = l;
		if (l.kind == KIND_LOW || l.kind == KIND_HIGH) {
			_end_hang(true);
			_start(l, 0.0f);
			return;
		}
	}
	// Along the edge, following it as it turns and steps; stopping where it ends (looked for half the body's width
	// ahead) or something is in the way.
	const float wanted = CLAMP(along, -1.0f, 1.0f) * shimmy_speed;
	shimmy_velocity = Math::move_toward(shimmy_velocity, wanted, float(p_delta) * 5.0f * MAX(shimmy_speed, 0.2f));
	if (Math::abs(shimmy_velocity) > 1e-3f && hold_time > 0.2) {
		const float step = shimmy_velocity * float(p_delta);
		Ledge ahead;
		Ledge next;
		HashSet<RID> exclude;
		exclude.insert(body->get_rid());
		if (_probe_edge(ledge.edge, ledge.inward, step + SIGN(step) * radius * 0.6f, ahead) && _probe_edge(ledge.edge, ledge.inward, step, next) && _capsule_free(_hang_feet_at(next.edge, next.inward) + UP * 0.02f, radius * 0.95f, height - 0.04f, exclude)) {
			shimmy_phase += Math::abs(step);
			if (next.collider != ledge.collider) {
				const Node3D *collider = ObjectDB::get_instance<Node3D>(next.collider);
				collider_start = collider ? collider->get_global_transform() : Transform3D();
			}
			ledge = next;
		} else {
			// Milestone 81: the ledge ends — round its corner, if it turns.
			Ledge to;
			Vector3 control;
			int how = 0;
			// Looked for again 0.2 s after a look in vain (4 us each).
			corner_wait -= p_delta;
			const bool look = wrap_corners && Math::abs(wanted) > 0.3f * shimmy_speed && corner_wait <= 0.0;
			if (look && !_find_corner(SIGN(step), to, control, how)) {
				corner_wait = 0.2;
			} else if (look) {
				_start_transfer(to, control, how, how == TRANSFER_OUTER_CORNER ? 0.7 : 0.5);
				const Vector3 f = flat(intent);
				remap = f.length() > 0.3f;
				remap_from = remap ? f.normalized() : Vector3();
				remap_to = UP.cross(to.inward).normalized() * SIGN(step);
				_step_transfer(p_delta);
				return;
			}
			shimmy_velocity = 0.0f;
		}
	}
	hang_feet = _hang_feet_at(ledge.edge, ledge.inward);
	const float w = smooth(float(hold_time / 0.2));
	_place(body, start_feet.lerp(hang_feet, w), ledge.inward, p_delta);
	// The pose: both hands on the edge, stepping along it as the body moves; the feet against the wall.
	hints.active = true;
	hints.kind = KIND_HANG;
	hints.progress = 0.0f;
	hints.inward = ledge.inward;
	hints.left = left;
	const float stride = 0.3f;
	const float swing = Math::sin(shimmy_phase / stride * Math::PI);
	const float sgn = shimmy_velocity >= 0.0f ? 1.0f : -1.0f;
	hints.hand[0] = ledge.edge + left * (0.24f + 0.08f * swing * sgn) + ledge.inward * 0.06f + UP * 0.02f;
	hints.hand[1] = ledge.edge - left * (0.24f - 0.08f * swing * sgn) + ledge.inward * 0.06f + UP * 0.02f;
	hints.hand_weight[0] = hints.hand_weight[1] = w;
	const Vector3 face = ledge.edge - ledge.inward * 0.06f;
	for (int s = 0; s < 2; s++) {
		hints.foot[s] = Vector3(face.x, hang_feet.y + 0.35f, face.z) + left * (s == 0 ? 0.14f : -0.14f);
		hints.foot_weight[s] = 0.8f * w;
		hints.foot_follow[s] = 1.0f;
	}
	hints.lean = 0.0f;
	hints.hips_drop = 0.0f;
	hints.clip.unref();
}

void FellowTraversal3D::_end_hang(bool p_climbed) {
	active = false;
	transfer.active = false;
	hints.active = false;
	hints.hand_weight[0] = hints.hand_weight[1] = 0.0f;
	hints.foot_weight[0] = hints.foot_weight[1] = 0.0f;
	climb_requested = false;
	release_requested = false;
	cooldown = p_climbed ? 0.0 : 0.6; // let go: not caught by the same ledge at once
	if (!p_climbed) {
		if (CharacterBody3D *cb = Object::cast_to<CharacterBody3D>(_get_body())) {
			cb->set_velocity(-ledge.inward * 0.6f);
		}
	}
	emit_signal(SNAME("hang_finished"), p_climbed);
}

bool FellowTraversal3D::try_hang(const Vector3 &p_direction) {
	if (active || !enabled || !hang) {
		return false;
	}
	CollisionObject3D *body = _get_body();
	Vector3 d = p_direction;
	if (const CharacterBody3D *cb = Object::cast_to<CharacterBody3D>(body)) {
		if (d == Vector3()) {
			d = flat(intent).length() > 0.1f ? intent : cb->get_velocity();
		}
	}
	last_check = _check(d, true, 0.0f);
	if (last_check.inward == Vector3() || (last_check.kind != KIND_LOW && last_check.kind != KIND_HIGH && last_check.kind != KIND_BLOCKED)) {
		return false;
	}
	return _start_hang(last_check);
}

// Milestone 75: ladders. One in reach: in front of it and going towards it (the feet along it), or on its top going
// over it (onto it from the floor it leads to).
FellowLadder3D *FellowTraversal3D::_find_ladder(const Vector3 &p_feet, const Vector3 &p_direction, bool p_in_air, float &r_h, bool &r_from_top) const {
	const Vector3 d = flat(p_direction);
	if (d.length() < 1e-3f) {
		return nullptr;
	}
	const Vector3 dn = d.normalized();
	for (FellowLadder3D *l : FellowLadder3D::get_all()) {
		if (!l->is_enabled() || !l->is_inside_tree()) {
			continue;
		}
		const Vector3 foot = l->get_foot();
		const Vector3 out = flat(l->get_outward());
		if (out.length() < 1e-3f) {
			continue;
		}
		const Vector3 outn = out.normalized();
		const float top = l->get_global_height();
		const Vector3 rel = p_feet - foot;
		const float h = rel.dot(l->get_up());
		const float a = rel.dot(l->get_across());
		const float o = rel.dot(outn);
		if (Math::abs(a) > l->get_width() * 0.5f + 0.25f) {
			continue;
		}
		if (o > 0.0f && o < radius + ladder_reach && h > -0.6f && h < top - 0.6f && dn.dot(-outn) > 0.7f) {
			r_h = MAX(h, 0.0f);
			r_from_top = false;
			return l;
		}
		if (!p_in_air && o < 0.1f && o > -(radius + ladder_reach + 0.3f) && Math::abs(h - top) < 0.35f && dn.dot(outn) > 0.7f) {
			r_h = MAX(0.0f, top - 1.2f);
			r_from_top = true;
			return l;
		}
	}
	return nullptr;
}

bool FellowTraversal3D::_start_ladder(FellowLadder3D *p_ladder, float p_h, bool p_from_top) {
	CollisionObject3D *body = _get_body();
	if (!body || !p_ladder) {
		return false;
	}
	_read_body_shape(body);
	action = KIND_LADDER;
	ladder_id = p_ladder->get_instance_id();
	ladder_h = p_h;
	ladder_from_top = p_from_top;
	start_xf = body->get_global_transform();
	start_feet = _feet_of(start_xf);
	time = 0.0;
	duration = 1.0;
	hold_time = 0.0;
	climb_requested = false;
	release_requested = false;
	active = true;
	global_stats.ladders.increment();
	emit_signal(SNAME("ladder_started"), p_ladder);
	_step_ladder(0.0); // the pose's targets from this frame
	return true;
}

void FellowTraversal3D::_step_ladder(double p_delta) {
	CollisionObject3D *body = _get_body();
	FellowLadder3D *l = ObjectDB::get_instance<FellowLadder3D>(ladder_id);
	if (!body || !l || !l->is_inside_tree() || !l->is_enabled()) {
		_end_ladder(false, Vector3());
		return;
	}
	hold_time += p_delta;
	const Vector3 foot = l->get_foot();
	const Vector3 up = l->get_up();
	const Vector3 out = flat(l->get_outward()).normalized();
	const float top = l->get_global_height();
	if (release_requested) {
		_end_ladder(false, out * 1.5f);
		return;
	}
	// Up or down: the controller's climb input, else its intent (up or towards the ladder: up).
	float input = climb_input;
	if (Math::abs(input) < 0.05f) {
		input = Math::abs(intent.y) > 0.05f ? intent.y : flat(intent).dot(-out);
	}
	input = CLAMP(input, -1.0f, 1.0f);
	if (hold_time > (ladder_from_top ? 0.6 : 0.25)) {
		ladder_h += input * ladder_speed * float(p_delta);
	}
	const float highest = MAX(0.0f, top - 1.0f); // the hands at the top
	if (ladder_h >= highest) {
		ladder_h = highest;
		// Going up at the top: off onto what is there (a mantle from the ladder).
		if (input > 0.1f && l->is_exit_at_top() && hold_time > 0.25) {
			const Ledge le = _check(-out, true, 0.0f);
			last_check = le;
			if (le.kind == KIND_LOW || le.kind == KIND_HIGH) {
				_end_ladder(true, Vector3());
				_start(le, 0.0f);
				return;
			}
		}
	}
	if (ladder_h <= 0.0f && input < -0.1f && hold_time > 0.25) {
		ladder_h = 0.0f;
		_place(body, foot + out * (radius + 0.12f), -out, p_delta);
		_end_ladder(false, Vector3());
		return;
	}
	ladder_h = MAX(ladder_h, 0.0f);
	const Vector3 target = foot + up * ladder_h + out * (radius + 0.12f);
	float w = smooth(float(hold_time / 0.3));
	Vector3 feet = start_feet.lerp(target, w);
	if (ladder_from_top) {
		// From the floor at its top: out over the edge first, at the floor's height, then down onto the ladder (the
		// capsule clear of the edge).
		const float u = float(CLAMP(hold_time / 0.6, 0.0, 1.0));
		const Vector3 over = Vector3(target.x, start_feet.y + 0.02f, target.z);
		feet = u < 0.5f ? start_feet.lerp(over, smooth(u / 0.5f)) : over.lerp(target, smooth((u - 0.5f) / 0.5f));
		w = smooth(u / 0.5f);
	}
	_place(body, feet, -out, p_delta);
	// The pose: the hands and feet on the rungs, a rung apart, changing over as it climbs.
	const Vector3 left = UP.cross(-out).normalized();
	const float spacing = MAX(l->get_rung_spacing() * l->get_global_transform().basis.get_column(1).length(), 0.05f);
	const int phase = int(Math::floor(ladder_h / spacing));
	hints.active = true;
	hints.kind = KIND_LADDER;
	hints.progress = top > 0.0f ? ladder_h / top : 0.0f;
	hints.inward = -out;
	hints.left = left;
	for (int s = 0; s < 2; s++) {
		const float lead = ((phase + s) % 2 == 0) ? spacing : 0.0f;
		const float side = s == 0 ? 1.0f : -1.0f;
		hints.hand[s] = foot + up * l->rung_near(ladder_h + 1.55f + lead) + left * (0.17f * side) + out * 0.04f;
		hints.hand_weight[s] = w;
		hints.foot[s] = foot + up * l->rung_near(ladder_h + 0.05f + lead) + left * (0.12f * side) + out * 0.1f;
		hints.foot_weight[s] = w;
		hints.foot_follow[s] = 0.0f;
	}
	hints.lean = 0.0f;
	hints.hips_drop = 0.0f;
	hints.clip.unref();
}

void FellowTraversal3D::_end_ladder(bool p_at_top, const Vector3 &p_velocity) {
	active = false;
	hints.active = false;
	hints.hand_weight[0] = hints.hand_weight[1] = 0.0f;
	hints.foot_weight[0] = hints.foot_weight[1] = 0.0f;
	release_requested = false;
	cooldown = p_at_top ? 0.0 : 0.5;
	if (CharacterBody3D *cb = Object::cast_to<CharacterBody3D>(_get_body())) {
		cb->set_velocity(p_velocity);
	}
	ladder_id = ObjectID();
	emit_signal(SNAME("ladder_finished"), p_at_top);
}

bool FellowTraversal3D::try_ladder() {
	if (active || !enabled) {
		return false;
	}
	CollisionObject3D *body = _get_body();
	if (!body) {
		return false;
	}
	_read_body_shape(body);
	const Transform3D bx = body->get_global_transform();
	Vector3 d = flat(intent).length() > 0.1f ? intent : -bx.basis.get_column(2);
	const CharacterBody3D *cb = Object::cast_to<CharacterBody3D>(body);
	float h = 0.0f;
	bool from_top = false;
	FellowLadder3D *l = _find_ladder(_feet_of(bx), d, cb && !cb->is_on_floor(), h, from_top);
	return l && _start_ladder(l, h, from_top);
}

Node3D *FellowTraversal3D::get_ladder() const {
	return active && action == KIND_LADDER ? ObjectDB::get_instance<FellowLadder3D>(ladder_id) : nullptr;
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
				cancel();
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
				if (enabled && ladders && cooldown <= 0.0 && !FellowLadder3D::get_all().is_empty()) {
					// Milestone 75: a ladder in reach, going towards it.
					const CharacterBody3D *cb = Object::cast_to<CharacterBody3D>(body);
					const Vector3 d = flat(intent).length() > 0.1f ? intent : v;
					if (flat(d).length() > 0.1f) {
						_read_body_shape(body);
						float h = 0.0f;
						bool from_top = false;
						if (FellowLadder3D *l = _find_ladder(_feet_of(body->get_global_transform()), d, cb && !cb->is_on_floor(), h, from_top)) {
							_start_ladder(l, h, from_top);
						}
					}
				}
				if (!active && enabled && hang && drop_to_hang && cooldown <= 0.0) {
					// Milestone 84: walking slowly over an edge, hanging from it.
					const CharacterBody3D *cb = Object::cast_to<CharacterBody3D>(body);
					if (cb && cb->is_on_floor() && flat(intent).length() > 0.5f && flat(v).length() <= drop_max_speed) {
						_read_body_shape(body);
						Ledge l;
						Vector3 mid;
						if (_find_drop(intent, l, mid)) {
							_start_drop(l, mid);
						}
					}
				}
				if (!active && enabled && auto_mantle && cooldown <= 0.0) {
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
							// Milestone 75: in the air, a ledge it cannot climb (no room on its top), or any with
							// hang_first, is hung from when its edge is about the hands' reach above the feet.
							const float over = last_check.edge.y - _feet_of(body->get_global_transform()).y;
							const bool climbable = last_check.kind == KIND_LOW || last_check.kind == KIND_HIGH;
							const bool hangable = air && hang && last_check.inward != Vector3() && over > hang_reach - 0.8f && over < hang_reach + 0.4f && ((last_check.kind == KIND_BLOCKED && last_check.reason != "no room above") || (hang_first && climbable));
							if (hangable && _start_hang(last_check)) {
								// hanging
							} else if (!(air && last_check.kind == KIND_VAULT) && !(last_check.open_below && !air)) {
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
	ClassDB::bind_method(D_METHOD("try_hang", "direction"), &FellowTraversal3D::try_hang, DEFVAL(Vector3()));
	ClassDB::bind_method(D_METHOD("try_ladder"), &FellowTraversal3D::try_ladder);
	ClassDB::bind_method(D_METHOD("climb_up"), &FellowTraversal3D::climb_up);
	ClassDB::bind_method(D_METHOD("release"), &FellowTraversal3D::release);
	ClassDB::bind_method(D_METHOD("is_hanging"), &FellowTraversal3D::is_hanging);
	ClassDB::bind_method(D_METHOD("is_on_ladder"), &FellowTraversal3D::is_on_ladder);
	ClassDB::bind_method(D_METHOD("get_ladder"), &FellowTraversal3D::get_ladder);
	ClassDB::bind_method(D_METHOD("set_climb_input", "input"), &FellowTraversal3D::set_climb_input);
	ClassDB::bind_method(D_METHOD("get_climb_input"), &FellowTraversal3D::get_climb_input);
	ClassDB::bind_method(D_METHOD("set_hang", "enabled"), &FellowTraversal3D::set_hang);
	ClassDB::bind_method(D_METHOD("is_hang"), &FellowTraversal3D::is_hang);
	ClassDB::bind_method(D_METHOD("set_hang_first", "enabled"), &FellowTraversal3D::set_hang_first);
	ClassDB::bind_method(D_METHOD("is_hang_first"), &FellowTraversal3D::is_hang_first);
	ClassDB::bind_method(D_METHOD("set_hang_reach", "reach"), &FellowTraversal3D::set_hang_reach);
	ClassDB::bind_method(D_METHOD("get_hang_reach"), &FellowTraversal3D::get_hang_reach);
	ClassDB::bind_method(D_METHOD("set_shimmy_speed", "speed"), &FellowTraversal3D::set_shimmy_speed);
	ClassDB::bind_method(D_METHOD("get_shimmy_speed"), &FellowTraversal3D::get_shimmy_speed);
	ClassDB::bind_method(D_METHOD("set_wrap_corners", "enabled"), &FellowTraversal3D::set_wrap_corners);
	ClassDB::bind_method(D_METHOD("is_wrap_corners"), &FellowTraversal3D::is_wrap_corners);
	ClassDB::bind_method(D_METHOD("set_hang_jump_distance", "distance"), &FellowTraversal3D::set_hang_jump_distance);
	ClassDB::bind_method(D_METHOD("get_hang_jump_distance"), &FellowTraversal3D::get_hang_jump_distance);
	ClassDB::bind_method(D_METHOD("set_hang_jump_height", "height"), &FellowTraversal3D::set_hang_jump_height);
	ClassDB::bind_method(D_METHOD("get_hang_jump_height"), &FellowTraversal3D::get_hang_jump_height);
	ClassDB::bind_method(D_METHOD("set_hang_jump_depth", "depth"), &FellowTraversal3D::set_hang_jump_depth);
	ClassDB::bind_method(D_METHOD("get_hang_jump_depth"), &FellowTraversal3D::get_hang_jump_depth);
	ClassDB::bind_method(D_METHOD("set_drop_to_hang", "enabled"), &FellowTraversal3D::set_drop_to_hang);
	ClassDB::bind_method(D_METHOD("is_drop_to_hang"), &FellowTraversal3D::is_drop_to_hang);
	ClassDB::bind_method(D_METHOD("set_drop_max_speed", "speed"), &FellowTraversal3D::set_drop_max_speed);
	ClassDB::bind_method(D_METHOD("get_drop_max_speed"), &FellowTraversal3D::get_drop_max_speed);
	ClassDB::bind_method(D_METHOD("try_drop", "direction"), &FellowTraversal3D::try_drop, DEFVAL(Vector3()));
	ClassDB::bind_method(D_METHOD("hang_jump"), &FellowTraversal3D::hang_jump);
	ClassDB::bind_method(D_METHOD("is_transferring"), &FellowTraversal3D::is_transferring);
	ClassDB::bind_method(D_METHOD("set_ladders", "enabled"), &FellowTraversal3D::set_ladders);
	ClassDB::bind_method(D_METHOD("is_ladders"), &FellowTraversal3D::is_ladders);
	ClassDB::bind_method(D_METHOD("set_ladder_speed", "speed"), &FellowTraversal3D::set_ladder_speed);
	ClassDB::bind_method(D_METHOD("get_ladder_speed"), &FellowTraversal3D::get_ladder_speed);
	ClassDB::bind_method(D_METHOD("set_ladder_reach", "reach"), &FellowTraversal3D::set_ladder_reach);
	ClassDB::bind_method(D_METHOD("get_ladder_reach"), &FellowTraversal3D::get_ladder_reach);

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
	ADD_GROUP("Hanging", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hang"), "set_hang", "is_hang");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hang_first"), "set_hang_first", "is_hang_first");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hang_reach", PROPERTY_HINT_RANGE, "0.5,4,0.01,suffix:m"), "set_hang_reach", "get_hang_reach");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shimmy_speed", PROPERTY_HINT_RANGE, "0,5,0.01,suffix:m/s"), "set_shimmy_speed", "get_shimmy_speed");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "wrap_corners"), "set_wrap_corners", "is_wrap_corners");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hang_jump_distance", PROPERTY_HINT_RANGE, "0,5,0.01,suffix:m"), "set_hang_jump_distance", "get_hang_jump_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hang_jump_height", PROPERTY_HINT_RANGE, "0,4,0.01,suffix:m"), "set_hang_jump_height", "get_hang_jump_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hang_jump_depth", PROPERTY_HINT_RANGE, "0,6,0.01,suffix:m"), "set_hang_jump_depth", "get_hang_jump_depth");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "drop_to_hang"), "set_drop_to_hang", "is_drop_to_hang");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drop_max_speed", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:m/s"), "set_drop_max_speed", "get_drop_max_speed");
	ADD_GROUP("Ladders", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ladders"), "set_ladders", "is_ladders");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ladder_speed", PROPERTY_HINT_RANGE, "0,5,0.01,suffix:m/s"), "set_ladder_speed", "get_ladder_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ladder_reach", PROPERTY_HINT_RANGE, "0.05,2,0.01,suffix:m"), "set_ladder_reach", "get_ladder_reach");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_draw"), "set_debug_draw", "is_debug_draw");

	ADD_SIGNAL(MethodInfo("mantle_started", PropertyInfo(Variant::INT, "kind"), PropertyInfo(Variant::DICTIONARY, "ledge")));
	ADD_SIGNAL(MethodInfo("mantle_finished", PropertyInfo(Variant::INT, "kind")));
	ADD_SIGNAL(MethodInfo("mantle_cancelled", PropertyInfo(Variant::INT, "kind")));
	ADD_SIGNAL(MethodInfo("hang_started", PropertyInfo(Variant::DICTIONARY, "ledge")));
	ADD_SIGNAL(MethodInfo("hang_finished", PropertyInfo(Variant::BOOL, "climbed")));
	ADD_SIGNAL(MethodInfo("hang_transferred", PropertyInfo(Variant::DICTIONARY, "ledge"), PropertyInfo(Variant::INT, "how", PROPERTY_HINT_ENUM, "Outer Corner,Inner Corner,Jump Along,Jump Up,Drop,Jump Down")));
	ADD_SIGNAL(MethodInfo("ladder_started", PropertyInfo(Variant::OBJECT, "ladder", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "FellowLadder3D")));
	ADD_SIGNAL(MethodInfo("ladder_finished", PropertyInfo(Variant::BOOL, "at_top")));

	BIND_ENUM_CONSTANT(KIND_NONE);
	BIND_ENUM_CONSTANT(KIND_STEP);
	BIND_ENUM_CONSTANT(KIND_LOW);
	BIND_ENUM_CONSTANT(KIND_HIGH);
	BIND_ENUM_CONSTANT(KIND_VAULT);
	BIND_ENUM_CONSTANT(KIND_TOO_HIGH);
	BIND_ENUM_CONSTANT(KIND_BLOCKED);
	BIND_ENUM_CONSTANT(KIND_HANG);
	BIND_ENUM_CONSTANT(KIND_LADDER);
	BIND_ENUM_CONSTANT(TRANSFER_OUTER_CORNER);
	BIND_ENUM_CONSTANT(TRANSFER_INNER_CORNER);
	BIND_ENUM_CONSTANT(TRANSFER_JUMP_ALONG);
	BIND_ENUM_CONSTANT(TRANSFER_JUMP_UP);
	BIND_ENUM_CONSTANT(TRANSFER_DROP);
	BIND_ENUM_CONSTANT(TRANSFER_JUMP_DOWN);
	BIND_ENUM_CONSTANT(KIND_MAX);
}
