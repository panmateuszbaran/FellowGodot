/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "fellow_face_animator.h"
#include "fellow_head_motion_3d.h"
#include "fellow_ladder_3d.h"
#include "fellow_motion_database.h"
#include "fellow_motion_matcher_3d.h"
#include "fellow_traversal_3d.h"
#include "fellow_traversal_pose_3d.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "main/performance.h"

// Milestone 50 (tools): motion matching in the debugger's Monitors tab: the matchers' CPU time a frame (summed over
// the worker threads) and their updates a frame, averaged since the debugger's previous sample. Milestone 72: the
// traversals' time a frame (their checks, the mantles moving bodies, the poses).
static const char *FELLOW_ANIMATION_MONITORS[] = {
	"FellowEngine/Motion matching time",
	"FellowEngine/Motion matching updates",
	"FellowEngine/Traversal time",
};

static Variant _fellow_animation_monitor(int p_which) {
	static uint64_t last_frame = 0;
	static int64_t last_usec = 0;
	static int64_t last_updates = 0;
	static int64_t last_traversal_usec = 0;
	static double time_per_frame = 0.0;
	static double updates_per_frame = 0.0;
	static double traversal_per_frame = 0.0;
	const uint64_t frame = Engine::get_singleton()->get_process_frames();
	if (frame != last_frame) {
		const Dictionary d = FellowMotionMatcher3D::get_global_stats();
		const int64_t usec = d.get("update_usec", 0);
		const int64_t updates = d.get("updates", 0);
		const Dictionary t = FellowTraversal3D::get_global_stats();
		const int64_t traversal_usec = int64_t(t.get("check_usec", 0)) + int64_t(t.get("step_usec", 0)) + int64_t(t.get("pose_usec", 0));
		const double frames = double(frame - last_frame);
		// Counters reset by reset_global_stats() start again from zero.
		time_per_frame = usec >= last_usec ? (usec - last_usec) / frames / 1e6 : 0.0;
		updates_per_frame = updates >= last_updates ? (updates - last_updates) / frames : 0.0;
		traversal_per_frame = traversal_usec >= last_traversal_usec ? (traversal_usec - last_traversal_usec) / frames / 1e6 : 0.0;
		last_frame = frame;
		last_usec = usec;
		last_updates = updates;
		last_traversal_usec = traversal_usec;
	}
	return p_which == 0 ? time_per_frame : (p_which == 1 ? updates_per_frame : traversal_per_frame);
}

void initialize_fellow_animation_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(FellowMotionDatabase);
		GDREGISTER_CLASS(FellowMotionMatcher3D);
		GDREGISTER_CLASS(FellowLadder3D); // milestone 75
		GDREGISTER_CLASS(FellowTraversal3D);
		GDREGISTER_CLASS(FellowTraversalPose3D);
		GDREGISTER_CLASS(FellowFaceAnimator); // milestone 83
		GDREGISTER_CLASS(FellowHeadMotion3D); // milestone 104

		Performance *performance = Performance::get_singleton();
		if (performance) {
			static const Performance::MonitorType types[3] = { Performance::MONITOR_TYPE_TIME, Performance::MONITOR_TYPE_QUANTITY, Performance::MONITOR_TYPE_TIME };
			for (int i = 0; i < 3; i++) {
				if (!performance->has_custom_monitor(FELLOW_ANIMATION_MONITORS[i])) {
					performance->add_custom_monitor(FELLOW_ANIMATION_MONITORS[i], callable_mp_static(&_fellow_animation_monitor).bind(i), Vector<Variant>(), types[i]);
				}
			}
		}
	}
}

void uninitialize_fellow_animation_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		Performance *performance = Performance::get_singleton();
		if (performance) {
			for (const char *id : FELLOW_ANIMATION_MONITORS) {
				if (performance->has_custom_monitor(id)) {
					performance->remove_custom_monitor(id);
				}
			}
		}
	}
}
