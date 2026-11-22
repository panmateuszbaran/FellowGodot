/**************************************************************************/
/*  fellow_inertializer_3d.cpp                                            */
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

#include "fellow_inertializer_3d.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/animation/animation_mixer.h"

namespace {

// Rotation vectors (axis * angle) to and from unit quaternions, the shorter way round.
Quaternion _fellow_exp(const Vector3 &p_v) {
	const real_t angle = p_v.length();
	if (angle < (real_t)1e-8) {
		return Quaternion();
	}
	return Quaternion(p_v / angle, angle);
}

Vector3 _fellow_log(Quaternion p_q) {
	if (p_q.w < 0) {
		p_q = -p_q;
	}
	const real_t s = Vector3(p_q.x, p_q.y, p_q.z).length();
	if (s < (real_t)1e-8) {
		return Vector3();
	}
	const real_t angle = 2.0 * Math::atan2(s, p_q.w);
	return Vector3(p_q.x, p_q.y, p_q.z) * (angle / s);
}

} // namespace

void FellowInertializer3D::blend(float p_seconds) {
	if (!have_out) {
		return; // nothing yet to blend from
	}
	ext = out;
	for (uint32_t b = 0; b < ext.size(); b++) {
		BoneState &e = ext[b];
		if (have_velocity && out_dt > 0.0) {
			const BoneState &p = prev[b];
			e.velocity = (e.position - p.position) / out_dt;
			e.angular_velocity = _fellow_log(e.rotation * p.rotation.inverse()) / out_dt;
			e.scale_velocity = (e.scale - p.scale) / out_dt;
		} else {
			e.velocity = Vector3();
			e.angular_velocity = Vector3();
			e.scale_velocity = Vector3();
		}
	}
	blend_duration = p_seconds >= 0.0f ? p_seconds : blend_time;
	blend_elapsed = 0.0f;
	blending = blend_duration > 0.0f;
	blends++;
}

float FellowInertializer3D::get_blend_progress() const {
	if (!blending || blend_duration <= 0.0f) {
		return 1.0f;
	}
	return CLAMP(blend_elapsed / blend_duration, 0.0f, 1.0f);
}

void FellowInertializer3D::_process_modification(double p_delta) {
	Skeleton3D *skeleton = get_skeleton();
	if (!skeleton) {
		return;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	const int count = skeleton->get_bone_count();
	if ((int)out.size() != count) {
		out.resize(count);
		prev.resize(count);
		ext.resize(count);
		have_out = false;
		have_velocity = false;
		blending = false;
	}
	// The skeleton may process its modifiers more than once a frame, the later times with no time passed (an
	// AnimationPlayer advanced by hand and the skeleton's own process both asking): the blend applied again as it
	// stands, the velocities not measured over no time.
	const bool moved = p_delta > 1e-6;
	const real_t dt = moved ? (real_t)p_delta : (real_t)0.0;

	// A jump in the new pose: a bone turned further than its last velocity carries it.
	if (detect_jumps && moved && have_velocity && !blending && out_dt > 0.0) {
		const real_t limit = Math::deg_to_rad((real_t)jump_angle);
		for (int b = 0; b < count; b++) {
			const Vector3 w = _fellow_log(out[b].rotation * prev[b].rotation.inverse()) / out_dt;
			const Quaternion predicted = (_fellow_exp(w * dt) * out[b].rotation).normalized();
			if (predicted.angle_to(skeleton->get_bone_pose_rotation(b)) > limit) {
				blend();
				break;
			}
		}
	}

	if (blending) {
		blend_elapsed += (float)dt;
		const real_t x = CLAMP((real_t)(blend_elapsed / blend_duration), (real_t)0.0, (real_t)1.0);
		const real_t a = x * x * (3.0 - 2.0 * x); // smoothstep: no step in the velocity at either end
		const real_t decay = Math::exp(-Math::LN2 * dt / (real_t)extrapolation_halflife);
		for (int b = 0; b < count; b++) {
			// The old pose carried on by its own velocity, the velocity fading; then towards the new animation's.
			BoneState &e = ext[b];
			e.velocity *= decay;
			e.position += e.velocity * dt;
			e.angular_velocity *= decay;
			e.rotation = (_fellow_exp(e.angular_velocity * dt) * e.rotation).normalized();
			e.scale_velocity *= decay;
			e.scale += e.scale_velocity * dt;
			skeleton->set_bone_pose_position(b, e.position.lerp(skeleton->get_bone_pose_position(b), a));
			skeleton->set_bone_pose_rotation(b, e.rotation.slerp(skeleton->get_bone_pose_rotation(b).normalized(), a));
			skeleton->set_bone_pose_scale(b, e.scale.lerp(skeleton->get_bone_pose_scale(b), a));
		}
		if (x >= 1.0) {
			blending = false;
		}
	}

	if (!moved && have_out) {
		usec += OS::get_singleton()->get_ticks_usec() - t0;
		return;
	}
	// The output, and the one before it: the next blend's start and its velocity.
	if (have_out) {
		SWAP(prev, out);
	}
	for (int b = 0; b < count; b++) {
		BoneState &o = out[b];
		o.position = skeleton->get_bone_pose_position(b);
		o.rotation = skeleton->get_bone_pose_rotation(b).normalized();
		o.scale = skeleton->get_bone_pose_scale(b);
	}
	out_dt = dt;
	have_velocity = have_out && moved;
	have_out = true;
	frames++;
	usec += OS::get_singleton()->get_ticks_usec() - t0;
}

void FellowInertializer3D::_on_animation_started(const StringName &p_name) {
	blend();
}

void FellowInertializer3D::_connect_mixer() {
	const Callable cb = callable_mp(this, &FellowInertializer3D::_on_animation_started);
	if (Object *old = ObjectDB::get_instance(mixer_connected)) {
		if (old->is_connected(SNAME("animation_started"), cb)) {
			old->disconnect(SNAME("animation_started"), cb);
		}
	}
	mixer_connected = ObjectID();
	if (!is_inside_tree() || animation_mixer_path.is_empty()) {
		return;
	}
	AnimationMixer *mixer = Object::cast_to<AnimationMixer>(get_node_or_null(animation_mixer_path));
	if (mixer) {
		mixer->connect(SNAME("animation_started"), cb);
		mixer_connected = mixer->get_instance_id();
	}
}

void FellowInertializer3D::set_animation_mixer_path(const NodePath &p_path) {
	animation_mixer_path = p_path;
	_connect_mixer();
	update_configuration_warnings();
}

Dictionary FellowInertializer3D::get_stats() const {
	Dictionary d;
	d["frames"] = (int64_t)frames;
	d["blends"] = (int64_t)blends;
	d["usec_per_frame"] = frames > 0 ? double(usec) / double(frames) : 0.0;
	return d;
}

PackedStringArray FellowInertializer3D::get_configuration_warnings() const {
	PackedStringArray warnings = SkeletonModifier3D::get_configuration_warnings();
	if (!animation_mixer_path.is_empty() && is_inside_tree() && !Object::cast_to<AnimationMixer>(get_node_or_null(animation_mixer_path))) {
		warnings.push_back(RTR("animation_mixer_path does not point to an AnimationPlayer or AnimationTree: no blend starts on its animations' starts."));
	}
	return warnings;
}

void FellowInertializer3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_connect_mixer();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (Object *old = ObjectDB::get_instance(mixer_connected)) {
				const Callable cb = callable_mp(this, &FellowInertializer3D::_on_animation_started);
				if (old->is_connected(SNAME("animation_started"), cb)) {
					old->disconnect(SNAME("animation_started"), cb);
				}
			}
			mixer_connected = ObjectID();
		} break;
	}
}

void FellowInertializer3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_blend_time", "seconds"), &FellowInertializer3D::set_blend_time);
	ClassDB::bind_method(D_METHOD("get_blend_time"), &FellowInertializer3D::get_blend_time);
	ClassDB::bind_method(D_METHOD("set_extrapolation_halflife", "seconds"), &FellowInertializer3D::set_extrapolation_halflife);
	ClassDB::bind_method(D_METHOD("get_extrapolation_halflife"), &FellowInertializer3D::get_extrapolation_halflife);
	ClassDB::bind_method(D_METHOD("set_animation_mixer_path", "path"), &FellowInertializer3D::set_animation_mixer_path);
	ClassDB::bind_method(D_METHOD("get_animation_mixer_path"), &FellowInertializer3D::get_animation_mixer_path);
	ClassDB::bind_method(D_METHOD("set_detect_jumps", "enabled"), &FellowInertializer3D::set_detect_jumps);
	ClassDB::bind_method(D_METHOD("is_detect_jumps"), &FellowInertializer3D::is_detect_jumps);
	ClassDB::bind_method(D_METHOD("set_jump_angle", "degrees"), &FellowInertializer3D::set_jump_angle);
	ClassDB::bind_method(D_METHOD("get_jump_angle"), &FellowInertializer3D::get_jump_angle);
	ClassDB::bind_method(D_METHOD("blend", "seconds"), &FellowInertializer3D::blend, DEFVAL(-1.0));
	ClassDB::bind_method(D_METHOD("is_blending"), &FellowInertializer3D::is_blending);
	ClassDB::bind_method(D_METHOD("get_blend_progress"), &FellowInertializer3D::get_blend_progress);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowInertializer3D::get_stats);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend_time", PROPERTY_HINT_RANGE, "0,2,0.01,suffix:s"), "set_blend_time", "get_blend_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "extrapolation_halflife", PROPERTY_HINT_RANGE, "0.001,1,0.001,suffix:s"), "set_extrapolation_halflife", "get_extrapolation_halflife");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "animation_mixer_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "AnimationMixer"), "set_animation_mixer_path", "get_animation_mixer_path");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "detect_jumps"), "set_detect_jumps", "is_detect_jumps");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "jump_angle", PROPERTY_HINT_RANGE, "1,180,0.1,suffix:°"), "set_jump_angle", "get_jump_angle");
}
