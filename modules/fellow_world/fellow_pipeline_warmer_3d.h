/**************************************************************************/
/*  fellow_pipeline_warmer_3d.h                                           */
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

#include "scene/3d/node_3d.h"

class Camera3D;
class SubViewport;

// Milestone 59 (decision 0162): pipelines compiled while a scene loads rather than when its materials first come into
// view. For a few frames the node draws its world from the viewport camera's place (or its own) in the six directions
// of a cube, into a small viewport set up as its own viewport is (anti-aliasing, TAA, MSAA, scaling, shadow atlas, the
// camera's environment, layers and range), so that every material around asks for the specialized pipelines the game's
// frames will ask for; it waits until the frames compile nothing more, frees the viewport and emits `finished`. A game
// shows its loading screen until then.
class FellowPipelineWarmer3D : public Node3D {
	GDCLASS(FellowPipelineWarmer3D, Node3D);

	bool autostart = true;
	bool from_camera = true;
	int view_size = 128;
	int frames_per_view = 3;
	int settle_frames = 15;
	int max_frames = 900;

	SubViewport *viewport = nullptr;
	Camera3D *camera = nullptr;
	bool running = false;
	bool finished = false;
	int frame = 0;
	int view = 0;
	int quiet = 0;
	int64_t last_count = -1;
	int64_t start_count = 0;
	uint64_t start_usec = 0;
	uint64_t total_usec = 0;
	int total_frames = 0;
	int64_t compiled = 0;
	bool timed_out = false;

	static int64_t _pipeline_count();
	void _setup_view(int p_view);
	void _finish();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_autostart(bool p_autostart);
	bool get_autostart() const { return autostart; }
	void set_from_camera(bool p_from_camera);
	bool get_from_camera() const { return from_camera; }
	void set_view_size(int p_size);
	int get_view_size() const { return view_size; }
	void set_frames_per_view(int p_frames);
	int get_frames_per_view() const { return frames_per_view; }
	void set_settle_frames(int p_frames);
	int get_settle_frames() const { return settle_frames; }
	void set_max_frames(int p_frames);
	int get_max_frames() const { return max_frames; }

	void start();
	bool is_running() const { return running; }
	bool is_finished() const { return finished; }
	Dictionary get_stats() const;

	FellowPipelineWarmer3D();
	~FellowPipelineWarmer3D();
};
