/**************************************************************************/
/*  fellow_world_streamer_3d.cpp                                          */
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

#include "fellow_world_streamer_3d.h"

#include "core/config/engine.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/sort_array.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"

static float _point_aabb_distance(const Vector3 &p_point, const AABB &p_box) {
	const Vector3 c = p_box.get_center();
	const Vector3 h = p_box.size * 0.5f;
	const Vector3 d = ((p_point - c).abs() - h).max(Vector3());
	return d.length();
}

/* CELL */

void FellowStreamingCell3D::set_scene_path(const String &p_path) {
	scene_path = p_path;
}

void FellowStreamingCell3D::set_bounds(const AABB &p_bounds) {
	bounds = p_bounds;
	update_gizmos();
}

AABB FellowStreamingCell3D::get_global_bounds() const {
	return is_inside_tree() ? get_global_transform().xform(bounds) : bounds;
}

void FellowStreamingCell3D::_set_proxy_visible(bool p_visible) {
	for (int i = 0; i < get_child_count(); i++) {
		Node3D *n = Object::cast_to<Node3D>(get_child(i));
		if (n && n != content) {
			n->set_visible(p_visible);
		}
	}
}

void FellowStreamingCell3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				break;
			}
			for (Node *p = get_parent(); p; p = p->get_parent()) {
				FellowWorldStreamer3D *s = Object::cast_to<FellowWorldStreamer3D>(p);
				if (s) {
					s->_register_cell(this);
					break;
				}
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (streamer) {
				streamer->_unregister_cell(this);
			}
		} break;
	}
}

FellowStreamingCell3D::~FellowStreamingCell3D() {
	if (content && content->get_parent() == nullptr) {
		memdelete(content); // instantiated, never entered the tree
	}
}

void FellowStreamingCell3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_scene_path", "path"), &FellowStreamingCell3D::set_scene_path);
	ClassDB::bind_method(D_METHOD("get_scene_path"), &FellowStreamingCell3D::get_scene_path);
	ClassDB::bind_method(D_METHOD("set_bounds", "bounds"), &FellowStreamingCell3D::set_bounds);
	ClassDB::bind_method(D_METHOD("get_bounds"), &FellowStreamingCell3D::get_bounds);
	ClassDB::bind_method(D_METHOD("set_load_distance", "distance"), &FellowStreamingCell3D::set_load_distance);
	ClassDB::bind_method(D_METHOD("get_load_distance"), &FellowStreamingCell3D::get_load_distance);
	ClassDB::bind_method(D_METHOD("get_state"), &FellowStreamingCell3D::get_state);
	ClassDB::bind_method(D_METHOD("is_loaded"), &FellowStreamingCell3D::is_loaded);
	ClassDB::bind_method(D_METHOD("get_content"), &FellowStreamingCell3D::get_content);
	ClassDB::bind_method(D_METHOD("get_global_bounds"), &FellowStreamingCell3D::get_global_bounds);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "scene_path", PROPERTY_HINT_FILE, "*.tscn,*.scn"), "set_scene_path", "get_scene_path");
	ADD_PROPERTY(PropertyInfo(Variant::AABB, "bounds", PROPERTY_HINT_NONE, "suffix:m"), "set_bounds", "get_bounds");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "load_distance", PROPERTY_HINT_RANGE, "0,10000,0.1,or_greater,suffix:m"), "set_load_distance", "get_load_distance");

	ADD_SIGNAL(MethodInfo("loaded"));
	ADD_SIGNAL(MethodInfo("unloaded"));

	BIND_ENUM_CONSTANT(STATE_UNLOADED);
	BIND_ENUM_CONSTANT(STATE_LOADING);
	BIND_ENUM_CONSTANT(STATE_INSTANCING);
	BIND_ENUM_CONSTANT(STATE_READY);
	BIND_ENUM_CONSTANT(STATE_LOADED);
}

/* STREAMER */

void FellowWorldStreamer3D::_register_cell(FellowStreamingCell3D *p_cell) {
	if (p_cell->streamer == this) {
		return;
	}
	p_cell->streamer = this;
	cells.push_back(p_cell);
}

void FellowWorldStreamer3D::_unregister_cell(FellowStreamingCell3D *p_cell) {
	if (p_cell->streamer != this) {
		return;
	}
	p_cell->streamer = nullptr;
	cells.erase(p_cell);
	// A cell taken out of the tree keeps what it has; one still loading is forgotten (its load finishes unused).
	if (p_cell->state == FellowStreamingCell3D::STATE_LOADING || p_cell->state == FellowStreamingCell3D::STATE_INSTANCING) {
		p_cell->state = FellowStreamingCell3D::STATE_UNLOADED;
	}
}

void FellowWorldStreamer3D::_instance_task(void *p_data) {
	Instancing *inst = static_cast<Instancing *>(p_data);
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	inst->result = inst->scene->instantiate();
	inst->usec = OS::get_singleton()->get_ticks_usec() - t0;
	inst->done.store(true, std::memory_order_release);
}

void FellowWorldStreamer3D::_gather_points(LocalVector<Vector3> &r_points) const {
	if (use_camera && is_inside_tree()) {
		Camera3D *cam = get_viewport()->get_camera_3d();
		if (cam) {
			r_points.push_back(cam->get_global_position());
		}
	}
	for (int i = 0; i < targets.size(); i++) {
		Node3D *n = Object::cast_to<Node3D>(get_node_or_null(targets[i]));
		if (n) {
			r_points.push_back(n->get_global_position());
		}
	}
	for (const Vector3 &p : prefetch_points) {
		r_points.push_back(p);
	}
}

void FellowWorldStreamer3D::_activate(FellowStreamingCell3D *p_cell, Node *p_content) {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	p_cell->content = p_content;
	p_cell->add_child(p_content);
	p_cell->_set_proxy_visible(false);
	p_cell->state = FellowStreamingCell3D::STATE_LOADED;
	const uint64_t t1 = OS::get_singleton()->get_ticks_usec();
	activation_usec += t1 - t0;
	activation_usec_max = MAX(activation_usec_max, t1 - t0);
	const uint64_t latency = t1 - p_cell->request_usec;
	latency_usec += latency;
	latency_usec_max = MAX(latency_usec_max, latency);
	loads++;
	p_cell->emit_signal(SNAME("loaded"));
}

void FellowWorldStreamer3D::_release(FellowStreamingCell3D *p_cell) {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	Node *c = p_cell->content;
	p_cell->content = nullptr;
	if (c) {
		if (c->get_parent() == p_cell) {
			p_cell->remove_child(c);
		}
		c->queue_free();
	}
	p_cell->_set_proxy_visible(true);
	const bool was_loaded = p_cell->state == FellowStreamingCell3D::STATE_LOADED;
	p_cell->state = FellowStreamingCell3D::STATE_UNLOADED;
	const uint64_t dt = OS::get_singleton()->get_ticks_usec() - t0;
	release_usec += dt;
	release_usec_max = MAX(release_usec_max, dt);
	if (was_loaded) {
		releases++;
		p_cell->emit_signal(SNAME("unloaded"));
	}
}

void FellowWorldStreamer3D::_update() {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	frames++;
	// 1. Instancing done: the content waits for its turn to enter the tree (or is dropped).
	for (uint32_t i = 0; i < instancing.size();) {
		Instancing *inst = instancing[i];
		if (!inst->done.load(std::memory_order_acquire)) {
			i++;
			continue;
		}
		WorkerThreadPool::get_singleton()->wait_for_task_completion(inst->task);
		instancing_usec += inst->usec;
		FellowStreamingCell3D *cell = ObjectDB::get_instance<FellowStreamingCell3D>(inst->cell);
		if (cell && cell->streamer == this && cell->state == FellowStreamingCell3D::STATE_INSTANCING && inst->result) {
			cell->content = inst->result;
			cell->state = FellowStreamingCell3D::STATE_READY;
		} else {
			if (inst->result) {
				memdelete(inst->result);
			} else if (cell) {
				failures++;
				ERR_PRINT(vformat("FellowWorldStreamer3D: could not instantiate '%s'.", cell->scene_path));
				cell->state = FellowStreamingCell3D::STATE_UNLOADED;
			}
		}
		memdelete(inst);
		instancing.remove_at_unordered(i);
	}

	// 2. Distances.
	LocalVector<Vector3> points;
	_gather_points(points);
	for (FellowStreamingCell3D *cell : cells) {
		float d = 1e30f;
		const AABB box = cell->get_global_bounds();
		for (const Vector3 &p : points) {
			d = MIN(d, _point_aabb_distance(p, box));
		}
		for (const Vector3 &p : prefetch_points) {
			if (_point_aabb_distance(p, box) <= prefetch_radius) {
				d = 0.0f;
			}
		}
		cell->distance = streaming ? d : 0.0f;
	}
	auto wanted = [this](const FellowStreamingCell3D *c) {
		return c->distance <= (c->load_distance > 0.0 ? c->load_distance : load_distance);
	};
	auto beyond = [this](const FellowStreamingCell3D *c) {
		return c->distance > (c->load_distance > 0.0 ? c->load_distance : load_distance) + unload_margin;
	};

	// 3. Loads finished on the ResourceLoader's threads: instantiate them (on a worker thread), or drop them.
	int loading = 0;
	for (FellowStreamingCell3D *cell : cells) {
		if (cell->state != FellowStreamingCell3D::STATE_LOADING) {
			continue;
		}
		const ResourceLoader::ThreadLoadStatus st = ResourceLoader::load_threaded_get_status(cell->scene_path);
		if (st == ResourceLoader::THREAD_LOAD_IN_PROGRESS) {
			loading++;
			continue;
		}
		Ref<PackedScene> scene;
		if (st == ResourceLoader::THREAD_LOAD_LOADED) {
			scene = ResourceLoader::load_threaded_get(cell->scene_path);
		}
		if (scene.is_null()) {
			failures++;
			ERR_PRINT(vformat("FellowWorldStreamer3D: could not load '%s'.", cell->scene_path));
			cell->state = FellowStreamingCell3D::STATE_UNLOADED;
			continue;
		}
		if (beyond(cell)) {
			cell->state = FellowStreamingCell3D::STATE_UNLOADED; // the camera left meanwhile
			continue;
		}
		if (threaded_instancing) {
			Instancing *inst = memnew(Instancing);
			inst->cell = cell->get_instance_id();
			inst->scene = scene;
			cell->state = FellowStreamingCell3D::STATE_INSTANCING;
			instancing.push_back(inst);
			inst->task = WorkerThreadPool::get_singleton()->add_native_task(&FellowWorldStreamer3D::_instance_task, inst, false, "FellowEngine world streaming");
		} else {
			const uint64_t ti = OS::get_singleton()->get_ticks_usec();
			Node *n = scene->instantiate();
			instancing_usec += OS::get_singleton()->get_ticks_usec() - ti;
			if (n) {
				cell->content = n;
				cell->state = FellowStreamingCell3D::STATE_READY;
			} else {
				failures++;
				cell->state = FellowStreamingCell3D::STATE_UNLOADED;
			}
		}
	}

	struct Item {
		FellowStreamingCell3D *cell;
		float key;
		bool operator<(const Item &p_o) const { return key < p_o.key; }
	};
	LocalVector<Item> out, in, start;
	for (FellowStreamingCell3D *cell : cells) {
		switch (cell->state) {
			case FellowStreamingCell3D::STATE_LOADED:
				if (beyond(cell)) {
					out.push_back({ cell, -cell->distance });
				}
				break;
			case FellowStreamingCell3D::STATE_READY:
				if (beyond(cell)) {
					Node *c = cell->content;
					cell->content = nullptr;
					memdelete(c); // never entered the tree
					cell->state = FellowStreamingCell3D::STATE_UNLOADED;
				} else {
					in.push_back({ cell, cell->distance });
				}
				break;
			case FellowStreamingCell3D::STATE_UNLOADED:
				if (wanted(cell) && !cell->scene_path.is_empty()) {
					start.push_back({ cell, cell->distance });
				}
				break;
			default:
				break;
		}
	}
	// 4. The farthest out first, the nearest in first, the nearest requested first.
	out.sort();
	for (uint32_t i = 0; i < out.size() && int(i) < max_releases_per_frame; i++) {
		_release(out[i].cell);
	}
	in.sort();
	for (uint32_t i = 0; i < in.size() && int(i) < max_activations_per_frame; i++) {
		Node *c = in[i].cell->content;
		in[i].cell->content = nullptr;
		_activate(in[i].cell, c);
	}
	start.sort();
	for (const Item &it : start) {
		if (loading + int(instancing.size()) >= max_loading) {
			break;
		}
		FellowStreamingCell3D *cell = it.cell;
		if (ResourceLoader::load_threaded_request(cell->scene_path, "PackedScene") != OK) {
			failures++;
			continue;
		}
		cell->state = FellowStreamingCell3D::STATE_LOADING;
		cell->request_usec = OS::get_singleton()->get_ticks_usec();
		loading++;
	}
	update_usec += OS::get_singleton()->get_ticks_usec() - t0;
}

void FellowWorldStreamer3D::_cancel_all() {
	for (Instancing *inst : instancing) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(inst->task);
		if (inst->result) {
			memdelete(inst->result);
		}
		memdelete(inst);
	}
	instancing.clear();
}

void FellowWorldStreamer3D::set_streaming(bool p_enable) {
	streaming = p_enable;
}

void FellowWorldStreamer3D::prefetch(const Vector3 &p_position, real_t p_radius) {
	prefetch_points.push_back(p_position);
	prefetch_radius = MAX(prefetch_radius, p_radius);
}

void FellowWorldStreamer3D::clear_prefetch() {
	prefetch_points.clear();
	prefetch_radius = 0.0;
}

bool FellowWorldStreamer3D::is_settled() const {
	if (!instancing.is_empty()) {
		return false;
	}
	for (const FellowStreamingCell3D *cell : cells) {
		const float dist = cell->load_distance > 0.0 ? cell->load_distance : load_distance;
		switch (cell->state) {
			case FellowStreamingCell3D::STATE_LOADING:
			case FellowStreamingCell3D::STATE_INSTANCING:
			case FellowStreamingCell3D::STATE_READY:
				return false;
			case FellowStreamingCell3D::STATE_UNLOADED:
				if (cell->distance <= dist && !cell->scene_path.is_empty()) {
					return false;
				}
				break;
			case FellowStreamingCell3D::STATE_LOADED:
				if (cell->distance > dist + unload_margin) {
					return false;
				}
				break;
		}
	}
	return frames > 0;
}

int FellowWorldStreamer3D::load_now() {
	// Every cell in range, synchronously: the loads that are on their way are waited for.
	_update(); // distances, finished work
	_cancel_all();
	int entered = 0;
	for (FellowStreamingCell3D *cell : cells) {
		const float dist = cell->load_distance > 0.0 ? cell->load_distance : load_distance;
		if (cell->distance > dist || cell->scene_path.is_empty()) {
			continue;
		}
		if (cell->state == FellowStreamingCell3D::STATE_LOADED) {
			continue;
		}
		Ref<PackedScene> scene;
		if (cell->state == FellowStreamingCell3D::STATE_LOADING) {
			scene = ResourceLoader::load_threaded_get(cell->scene_path);
		} else if (cell->state == FellowStreamingCell3D::STATE_READY && cell->content) {
			Node *c = cell->content;
			cell->content = nullptr;
			_activate(cell, c);
			entered++;
			continue;
		} else {
			cell->request_usec = OS::get_singleton()->get_ticks_usec();
			scene = ResourceLoader::load(cell->scene_path, "PackedScene");
		}
		Node *n = scene.is_valid() ? scene->instantiate() : nullptr;
		if (n == nullptr) {
			failures++;
			cell->state = FellowStreamingCell3D::STATE_UNLOADED;
			continue;
		}
		_activate(cell, n);
		entered++;
	}
	return entered;
}

Dictionary FellowWorldStreamer3D::get_stats() const {
	int counts[5] = {};
	for (const FellowStreamingCell3D *cell : cells) {
		counts[cell->state]++;
	}
	Dictionary d;
	d["cells"] = cells.size();
	d["unloaded"] = counts[FellowStreamingCell3D::STATE_UNLOADED];
	d["loading"] = counts[FellowStreamingCell3D::STATE_LOADING];
	d["instancing"] = counts[FellowStreamingCell3D::STATE_INSTANCING];
	d["ready"] = counts[FellowStreamingCell3D::STATE_READY];
	d["loaded"] = counts[FellowStreamingCell3D::STATE_LOADED];
	d["loads"] = loads;
	d["releases"] = releases;
	d["failures"] = failures;
	d["activation_usec"] = activation_usec;
	d["activation_usec_max"] = activation_usec_max;
	d["release_usec"] = release_usec;
	d["release_usec_max"] = release_usec_max;
	d["instancing_usec"] = instancing_usec;
	d["latency_usec"] = latency_usec;
	d["latency_usec_max"] = latency_usec_max;
	d["update_usec"] = update_usec;
	d["frames"] = frames;
	d["settled"] = is_settled();
	return d;
}

void FellowWorldStreamer3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				set_process_internal(true);
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_update();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_cancel_all();
		} break;
	}
}

void FellowWorldStreamer3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_load_distance", "distance"), &FellowWorldStreamer3D::set_load_distance);
	ClassDB::bind_method(D_METHOD("get_load_distance"), &FellowWorldStreamer3D::get_load_distance);
	ClassDB::bind_method(D_METHOD("set_unload_margin", "margin"), &FellowWorldStreamer3D::set_unload_margin);
	ClassDB::bind_method(D_METHOD("get_unload_margin"), &FellowWorldStreamer3D::get_unload_margin);
	ClassDB::bind_method(D_METHOD("set_max_loading", "count"), &FellowWorldStreamer3D::set_max_loading);
	ClassDB::bind_method(D_METHOD("get_max_loading"), &FellowWorldStreamer3D::get_max_loading);
	ClassDB::bind_method(D_METHOD("set_max_activations_per_frame", "count"), &FellowWorldStreamer3D::set_max_activations_per_frame);
	ClassDB::bind_method(D_METHOD("get_max_activations_per_frame"), &FellowWorldStreamer3D::get_max_activations_per_frame);
	ClassDB::bind_method(D_METHOD("set_max_releases_per_frame", "count"), &FellowWorldStreamer3D::set_max_releases_per_frame);
	ClassDB::bind_method(D_METHOD("get_max_releases_per_frame"), &FellowWorldStreamer3D::get_max_releases_per_frame);
	ClassDB::bind_method(D_METHOD("set_threaded_instancing", "enable"), &FellowWorldStreamer3D::set_threaded_instancing);
	ClassDB::bind_method(D_METHOD("is_threaded_instancing"), &FellowWorldStreamer3D::is_threaded_instancing);
	ClassDB::bind_method(D_METHOD("set_streaming", "enable"), &FellowWorldStreamer3D::set_streaming);
	ClassDB::bind_method(D_METHOD("is_streaming"), &FellowWorldStreamer3D::is_streaming);
	ClassDB::bind_method(D_METHOD("set_use_camera", "enable"), &FellowWorldStreamer3D::set_use_camera);
	ClassDB::bind_method(D_METHOD("is_using_camera"), &FellowWorldStreamer3D::is_using_camera);
	ClassDB::bind_method(D_METHOD("set_targets", "targets"), &FellowWorldStreamer3D::set_targets);
	ClassDB::bind_method(D_METHOD("get_targets"), &FellowWorldStreamer3D::get_targets);
	ClassDB::bind_method(D_METHOD("prefetch", "position", "radius"), &FellowWorldStreamer3D::prefetch);
	ClassDB::bind_method(D_METHOD("clear_prefetch"), &FellowWorldStreamer3D::clear_prefetch);
	ClassDB::bind_method(D_METHOD("is_settled"), &FellowWorldStreamer3D::is_settled);
	ClassDB::bind_method(D_METHOD("load_now"), &FellowWorldStreamer3D::load_now);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowWorldStreamer3D::get_stats);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "load_distance", PROPERTY_HINT_RANGE, "0,10000,0.1,or_greater,suffix:m"), "set_load_distance", "get_load_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "unload_margin", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:m"), "set_unload_margin", "get_unload_margin");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_loading", PROPERTY_HINT_RANGE, "1,64,1"), "set_max_loading", "get_max_loading");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_activations_per_frame", PROPERTY_HINT_RANGE, "1,1024,1"), "set_max_activations_per_frame", "get_max_activations_per_frame");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_releases_per_frame", PROPERTY_HINT_RANGE, "1,1024,1"), "set_max_releases_per_frame", "get_max_releases_per_frame");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "threaded_instancing"), "set_threaded_instancing", "is_threaded_instancing");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "streaming"), "set_streaming", "is_streaming");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_camera"), "set_use_camera", "is_using_camera");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "targets", PROPERTY_HINT_ARRAY_TYPE, "NodePath"), "set_targets", "get_targets");
}

FellowWorldStreamer3D::FellowWorldStreamer3D() {
}

FellowWorldStreamer3D::~FellowWorldStreamer3D() {
	_cancel_all();
	for (FellowStreamingCell3D *cell : cells) {
		cell->streamer = nullptr;
	}
}
