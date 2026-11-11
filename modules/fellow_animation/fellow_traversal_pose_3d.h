/**************************************************************************/
/*  fellow_traversal_pose_3d.h                                            */
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

#include "fellow_traversal_3d.h"

#include "scene/3d/skeleton_modifier_3d.h"

// FellowEngine (milestone 72, decision 0180): a character's pose while its FellowTraversal3D mantles or vaults — the
// hands reaching for the edge and holding it, the feet on the wall's face and stepping onto the top, the torso tipped
// towards the wall (two-bone IK over whatever the character's animation plays) — or, when the traversal has a clip for
// the kind, that clip played across the mantle with its root's motion taken out (the traversal moves the body).
class FellowTraversalPose3D : public SkeletonModifier3D {
	GDCLASS(FellowTraversalPose3D, SkeletonModifier3D);

public:
	enum Role {
		ROLE_HIPS,
		ROLE_SPINE,
		ROLE_CHEST,
		ROLE_LEFT_UPPER_ARM,
		ROLE_LEFT_LOWER_ARM,
		ROLE_LEFT_HAND,
		ROLE_RIGHT_UPPER_ARM,
		ROLE_RIGHT_LOWER_ARM,
		ROLE_RIGHT_HAND,
		ROLE_LEFT_UPPER_LEG,
		ROLE_LEFT_LOWER_LEG,
		ROLE_LEFT_FOOT,
		ROLE_RIGHT_UPPER_LEG,
		ROLE_RIGHT_LOWER_LEG,
		ROLE_RIGHT_FOOT,
		ROLE_MAX,
	};

private:
	NodePath traversal_path;
	float blend_time = 0.15f;
	bool hand_ik = true;
	bool foot_ik = true;
	bool lean = true;
	Dictionary bone_names; // role name -> bone name, over the names found by themselves

	// The skeleton's bones for each role (-1: not found).
	int bones[ROLE_MAX];
	ObjectID bones_skeleton;
	int bones_count = -1;
	bool bones_dirty = true;

	// The blend in and out (a clip's last pose held while it fades).
	float weight = 0.0f;
	float hips_drop = 0.0f; // the last pose's, in meters
	FellowTraversal3D::PoseHints last;
	mutable ObjectID traversal_cache;

	FellowTraversal3D *_get_traversal() const;
	void _find_bones(Skeleton3D *p_skeleton);
	void _apply_clip(Skeleton3D *p_skeleton, const FellowTraversal3D::PoseHints &p_hints, float p_weight);
	void _lean(Skeleton3D *p_skeleton, const Vector3 &p_axis, float p_angle);
	void _pose(double p_delta);
	void _two_bone(Skeleton3D *p_skeleton, int p_upper, int p_lower, int p_end, const Vector3 &p_target, const Vector3 &p_pole, float p_weight);

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void _process_modification(double p_delta) override;
	virtual void _skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) override;

public:
	virtual PackedStringArray get_configuration_warnings() const override;
	virtual bool fellow_processing_is_noop() const override;

	static const char *get_role_name(Role p_role);
	int get_role_bone(Role p_role);
	float get_hips_drop() const { return hips_drop; }
	float get_weight() const { return weight; }

	void set_traversal_path(const NodePath &p_path);
	NodePath get_traversal_path() const { return traversal_path; }
	void set_blend_time(float p_seconds) { blend_time = MAX(0.0f, p_seconds); }
	float get_blend_time() const { return blend_time; }
	void set_hand_ik(bool p_enabled) { hand_ik = p_enabled; }
	bool is_hand_ik() const { return hand_ik; }
	void set_foot_ik(bool p_enabled) { foot_ik = p_enabled; }
	bool is_foot_ik() const { return foot_ik; }
	void set_lean(bool p_enabled) { lean = p_enabled; }
	bool is_lean() const { return lean; }
	void set_bone_names(const Dictionary &p_names);
	Dictionary get_bone_names() const { return bone_names; }

	FellowTraversalPose3D();
};

VARIANT_ENUM_CAST(FellowTraversalPose3D::Role);
