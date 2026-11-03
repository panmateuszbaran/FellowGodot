/**************************************************************************/
/*  fellow_motion_rig.h                                                   */
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

#include "core/math/quaternion.h"
#include "core/math/vector3.h"
#include "core/string/ustring.h"
#include "core/templates/local_vector.h"
#include "core/variant/dictionary.h"

// The SOMA rig the model is trained on (77 joints, rooted at Hips; 30 of them the model's), and the glue around the
// model: poses (local joint transforms, Y up, metres) -> the model's inputs, its outputs -> poses with the endpoints
// corrected — MotionBricks' SOMA engine (encode / decode / endpoint-correction kernels); tools/fmb_soma.py is the
// NumPy reference.
struct FellowMotionRig {
	struct Xf {
		Vector3 p;
		Quaternion q;
	};
	static constexpr int MODEL_JOINTS = 30;
	static constexpr int BOUNDARY = 3;

	int joints = 0;
	LocalVector<String> names;
	LocalVector<int> parents;
	LocalVector<Xf> reference; // rest positions, relaxed-hands rotations (what joints the model does not drive keep)
	int model_to_pose[MODEL_JOINTS] = {};
	LocalVector<int> pose_to_model;
	int right_leg = -1;
	int left_leg = -1;
	float capture_offsets[MODEL_JOINTS * 9] = {};
	float standard_offsets[MODEL_JOINTS * 9] = {};

	bool setup(const Dictionary &p_skeleton, const float *p_capture, const float *p_standard, String &r_error);
	int find(const String &p_name) const;
	void world(const Xf *p_local, Xf *r_world) const;
	float heading_of(const Xf *p_world) const; // the legs' heading, atan2(forward.x, forward.z) (0 faces +Z)

	// A relaxed standing pose (the T pose of the model's standard rotations, arms lowered by p_arm_drop radians),
	// the hips at p_hips_height, facing +Z.
	void relaxed_pose(Xf *r_local, float p_arm_drop = 1.25f, float p_hips_height = 0.97f) const;

	// Encode four context poses and four target poses (the first p_target_poses given; local transforms in world
	// space) into the model's inputs of one request, at p_offset of the batch arrays. r_align is the alignment that
	// decode() takes back.
	void encode(const Xf *p_context, const Xf *p_target, int p_target_poses, bool p_target_has_pose, uint32_t p_allowed_bits,
			float *r_grv, int32_t *r_hg, float *r_lrv, int32_t *r_hl, float *r_lp, int32_t *r_hp, int32_t *r_allowed, Xf &r_align) const;

	// Decode one request's prediction: 4 T - 6 frames of local transforms (frames x joints) and foot contacts (x4).
	int decode(const float *p_motion, int p_tokens, const Xf &p_align, LocalVector<Xf> &r_frames, LocalVector<float> &r_contacts) const;

	// Spread the deltas that move the first frame onto p_start and the last onto p_end over the frames; p_end_weight
	// scales the end correction (0 for a root-only target).
	void correct_endpoints(LocalVector<Xf> &r_frames, int p_frames, const Xf *p_start, const Xf *p_end, bool p_correct_end, bool p_end_joints) const;
};
