/**************************************************************************/
/*  fellow_inertializer_3d.h                                              */
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

#include "core/templates/local_vector.h"
#include "scene/3d/skeleton_modifier_3d.h"

// FellowEngine (milestone 171, decision 0313): transitions blended after the animation, from the pose the character
// had, carried on by its own velocity, to the new animation's — dead blending (Holden 2023), an inertialization that
// needs only the new animation evaluated. The animation switches at once (an AnimationPlayer's play() with no blend,
// an AnimationTree's transitions with no cross-fade); this modifier, after it on the skeleton, hides the switch.
class FellowInertializer3D : public SkeletonModifier3D {
	GDCLASS(FellowInertializer3D, SkeletonModifier3D);

	float blend_time = 0.25f; // s
	float extrapolation_halflife = 0.05f; // s: the old pose's velocities halve this often as it is carried on
	NodePath animation_mixer_path; // start a blend on its animation_started
	bool detect_jumps = false;
	float jump_angle = 20.0f; // degrees: a bone turned this much further than its velocity carries it, in a frame

	struct BoneState {
		Vector3 position;
		Quaternion rotation;
		Vector3 scale = Vector3(1, 1, 1);
		Vector3 velocity; // the output's, per second
		Vector3 angular_velocity; // axis * radians per second, in the parent's space
		Vector3 scale_velocity;
	};
	LocalVector<BoneState> out; // the last output (its velocities measured only when a blend starts)
	LocalVector<BoneState> prev; // the one before it
	LocalVector<BoneState> ext; // the old pose, carried on, while blending
	real_t out_dt = 0.0; // the time between them
	bool have_out = false;
	bool have_velocity = false;
	bool blending = false;
	float blend_elapsed = 0.0f;
	float blend_duration = 0.25f;
	ObjectID mixer_connected;
	uint64_t blends = 0;
	uint64_t frames = 0;
	uint64_t usec = 0;

	void _connect_mixer();
	void _on_animation_started(const StringName &p_name);

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void _process_modification(double p_delta) override;

public:
	void set_blend_time(float p_seconds) { blend_time = CLAMP(p_seconds, 0.0f, 5.0f); }
	float get_blend_time() const { return blend_time; }
	void set_extrapolation_halflife(float p_seconds) { extrapolation_halflife = CLAMP(p_seconds, 0.001f, 5.0f); }
	float get_extrapolation_halflife() const { return extrapolation_halflife; }
	void set_animation_mixer_path(const NodePath &p_path);
	NodePath get_animation_mixer_path() const { return animation_mixer_path; }
	void set_detect_jumps(bool p_enabled) { detect_jumps = p_enabled; }
	bool is_detect_jumps() const { return detect_jumps; }
	void set_jump_angle(float p_degrees) { jump_angle = CLAMP(p_degrees, 1.0f, 180.0f); }
	float get_jump_angle() const { return jump_angle; }

	void blend(float p_seconds = -1.0f);
	bool is_blending() const { return blending; }
	float get_blend_progress() const;
	Dictionary get_stats() const;

	PackedStringArray get_configuration_warnings() const override;
};
