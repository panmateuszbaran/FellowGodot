/**************************************************************************/
/*  fellow_pipeline_warmer_3d.cpp                                         */
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

#include "fellow_pipeline_warmer_3d.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"
#include "servers/rendering/rendering_server.h"

int64_t FellowPipelineWarmer3D::_pipeline_count() {
	RenderingServer *rs = RenderingServer::get_singleton();
	return int64_t(rs->get_rendering_info(RSE::RENDERING_INFO_PIPELINE_COMPILATIONS_CANVAS)) +
			int64_t(rs->get_rendering_info(RSE::RENDERING_INFO_PIPELINE_COMPILATIONS_MESH)) +
			int64_t(rs->get_rendering_info(RSE::RENDERING_INFO_PIPELINE_COMPILATIONS_SURFACE)) +
			int64_t(rs->get_rendering_info(RSE::RENDERING_INFO_PIPELINE_COMPILATIONS_DRAW)) +
			int64_t(rs->get_rendering_info(RSE::RENDERING_INFO_PIPELINE_COMPILATIONS_SPECIALIZATION));
}

void FellowPipelineWarmer3D::start() {
	ERR_FAIL_COND_MSG(!is_inside_tree(), "FellowPipelineWarmer3D.start(): the node is not in the tree.");
	if (running || Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	Viewport *target = get_viewport();
	ERR_FAIL_NULL(target);
	// A small viewport drawing the same world, set up as the one the game draws in: what decides which pipelines a
	// frame asks for (anti-aliasing and the motion vectors TAA and the upscalers need, MSAA, the shadow atlas, HDR).
	viewport = memnew(SubViewport);
	viewport->set_name("FellowPipelineWarmup");
	viewport->set_size(Size2i(view_size, view_size));
	viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
	viewport->set_use_own_world_3d(false);
	viewport->set_world_3d(target->find_world_3d());
	viewport->set_msaa_3d(target->get_msaa_3d());
	viewport->set_screen_space_aa(target->get_screen_space_aa());
	viewport->set_use_taa(target->is_using_taa());
	viewport->set_use_debanding(target->is_using_debanding());
	viewport->set_scaling_3d_mode(target->get_scaling_3d_mode());
	viewport->set_scaling_3d_scale(target->get_scaling_3d_scale());
	viewport->set_fsr_sharpness(target->get_fsr_sharpness());
	viewport->set_texture_mipmap_bias(target->get_texture_mipmap_bias());
	viewport->set_mesh_lod_threshold(target->get_mesh_lod_threshold());
	viewport->set_use_occlusion_culling(target->is_using_occlusion_culling());
	viewport->set_positional_shadow_atlas_size(target->get_positional_shadow_atlas_size());
	viewport->set_positional_shadow_atlas_16_bits(target->get_positional_shadow_atlas_16_bits());
	for (int i = 0; i < 4; i++) {
		viewport->set_positional_shadow_atlas_quadrant_subdiv(i, target->get_positional_shadow_atlas_quadrant_subdiv(i));
	}
	viewport->set_use_hdr_2d(target->is_using_hdr_2d());
	viewport->set_anisotropic_filtering_level(target->get_anisotropic_filtering_level());
	camera = memnew(Camera3D);
	Camera3D *game_camera = target->get_camera_3d();
	if (game_camera) {
		camera->set_cull_mask(game_camera->get_cull_mask());
		camera->set_near(game_camera->get_near());
		camera->set_far(game_camera->get_far());
		camera->set_environment(game_camera->get_environment());
		camera->set_attributes(game_camera->get_attributes());
		camera->set_compositor(game_camera->get_compositor());
	}
	camera->set_fov(90.0f); // the six views of a cube cover every direction
	camera->set_keep_aspect_mode(Camera3D::KEEP_HEIGHT);
	viewport->add_child(camera);
	add_child(viewport, false, INTERNAL_MODE_BACK);
	camera->set_current(true);
	running = true;
	finished = false;
	frame = 0;
	view = 0;
	quiet = 0;
	start_count = _pipeline_count();
	last_count = start_count;
	start_usec = OS::get_singleton()->get_ticks_usec();
	_setup_view(0);
	set_process_internal(true);
}

void FellowPipelineWarmer3D::_setup_view(int p_view) {
	static const Vector3 dirs[6] = { Vector3(0, 0, -1), Vector3(1, 0, 0), Vector3(0, 0, 1), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, -1, 0) };
	Vector3 origin = get_global_position();
	if (from_camera) {
		Camera3D *game_camera = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
		if (game_camera && game_camera != camera) {
			origin = game_camera->get_global_position();
		}
	}
	const Vector3 d = dirs[CLAMP(p_view, 0, 5)];
	const Vector3 up = Math::abs(d.y) > 0.5f ? Vector3(0, 0, -1) : Vector3(0, 1, 0);
	camera->set_global_transform(Transform3D(Basis::looking_at(d, up), origin));
}

void FellowPipelineWarmer3D::_finish() {
	set_process_internal(false);
	running = false;
	finished = true;
	total_usec = OS::get_singleton()->get_ticks_usec() - start_usec;
	total_frames = frame;
	compiled = _pipeline_count() - start_count;
	if (viewport) {
		viewport->queue_free();
		viewport = nullptr;
		camera = nullptr;
	}
	emit_signal(SNAME("finished"));
}

void FellowPipelineWarmer3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (autostart && !Engine::get_singleton()->is_editor_hint()) {
				callable_mp(this, &FellowPipelineWarmer3D::start).call_deferred();
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!running) {
				break;
			}
			frame++;
			const int64_t count = _pipeline_count();
			quiet = count == last_count ? quiet + 1 : 0;
			last_count = count;
			if (view < 6) {
				// Each direction for a few frames (the first draws ask; TAA's history and the shadows settle).
				if (frame % MAX(1, frames_per_view) == 0) {
					view++;
					if (view < 6) {
						_setup_view(view);
					} else {
						quiet = 0;
					}
				}
			} else if (quiet >= settle_frames) {
				_finish(); // the pipelines asked for are compiled: nothing more for settle_frames frames
				break;
			}
			if (frame >= max_frames) {
				timed_out = true;
				_finish();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (running) {
				set_process_internal(false);
				running = false;
				viewport = nullptr; // freed with the node's children
				camera = nullptr;
			}
		} break;
	}
}

Dictionary FellowPipelineWarmer3D::get_stats() const {
	Dictionary d;
	d["running"] = running;
	d["finished"] = finished;
	d["frames"] = running ? frame : total_frames;
	d["msec"] = double(running ? OS::get_singleton()->get_ticks_usec() - start_usec : total_usec) / 1000.0;
	d["pipelines"] = running ? _pipeline_count() - start_count : compiled;
	d["timed_out"] = timed_out;
	return d;
}

void FellowPipelineWarmer3D::set_autostart(bool p_autostart) {
	autostart = p_autostart;
}

void FellowPipelineWarmer3D::set_from_camera(bool p_from_camera) {
	from_camera = p_from_camera;
}

void FellowPipelineWarmer3D::set_view_size(int p_size) {
	view_size = CLAMP(p_size, 16, 1024);
}

void FellowPipelineWarmer3D::set_frames_per_view(int p_frames) {
	frames_per_view = CLAMP(p_frames, 1, 60);
}

void FellowPipelineWarmer3D::set_settle_frames(int p_frames) {
	settle_frames = CLAMP(p_frames, 1, 600);
}

void FellowPipelineWarmer3D::set_max_frames(int p_frames) {
	max_frames = CLAMP(p_frames, 10, 100000);
}

void FellowPipelineWarmer3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_autostart", "autostart"), &FellowPipelineWarmer3D::set_autostart);
	ClassDB::bind_method(D_METHOD("get_autostart"), &FellowPipelineWarmer3D::get_autostart);
	ClassDB::bind_method(D_METHOD("set_from_camera", "from_camera"), &FellowPipelineWarmer3D::set_from_camera);
	ClassDB::bind_method(D_METHOD("get_from_camera"), &FellowPipelineWarmer3D::get_from_camera);
	ClassDB::bind_method(D_METHOD("set_view_size", "size"), &FellowPipelineWarmer3D::set_view_size);
	ClassDB::bind_method(D_METHOD("get_view_size"), &FellowPipelineWarmer3D::get_view_size);
	ClassDB::bind_method(D_METHOD("set_frames_per_view", "frames"), &FellowPipelineWarmer3D::set_frames_per_view);
	ClassDB::bind_method(D_METHOD("get_frames_per_view"), &FellowPipelineWarmer3D::get_frames_per_view);
	ClassDB::bind_method(D_METHOD("set_settle_frames", "frames"), &FellowPipelineWarmer3D::set_settle_frames);
	ClassDB::bind_method(D_METHOD("get_settle_frames"), &FellowPipelineWarmer3D::get_settle_frames);
	ClassDB::bind_method(D_METHOD("set_max_frames", "frames"), &FellowPipelineWarmer3D::set_max_frames);
	ClassDB::bind_method(D_METHOD("get_max_frames"), &FellowPipelineWarmer3D::get_max_frames);
	ClassDB::bind_method(D_METHOD("start"), &FellowPipelineWarmer3D::start);
	ClassDB::bind_method(D_METHOD("is_running"), &FellowPipelineWarmer3D::is_running);
	ClassDB::bind_method(D_METHOD("is_finished"), &FellowPipelineWarmer3D::is_finished);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowPipelineWarmer3D::get_stats);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autostart"), "set_autostart", "get_autostart");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "from_camera"), "set_from_camera", "get_from_camera");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "view_size", PROPERTY_HINT_RANGE, "16,1024,1,suffix:px"), "set_view_size", "get_view_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "frames_per_view", PROPERTY_HINT_RANGE, "1,60,1"), "set_frames_per_view", "get_frames_per_view");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "settle_frames", PROPERTY_HINT_RANGE, "1,600,1"), "set_settle_frames", "get_settle_frames");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_frames", PROPERTY_HINT_RANGE, "10,100000,1"), "set_max_frames", "get_max_frames");

	ADD_SIGNAL(MethodInfo("finished"));
}

FellowPipelineWarmer3D::FellowPipelineWarmer3D() {
}

FellowPipelineWarmer3D::~FellowPipelineWarmer3D() {
}
