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
#include "core/io/dir_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/sort_array.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"

static float _point_aabb_distance(const Vector3 &p_point, const AABB &p_box) {
	const Vector3 c = p_box.get_center();
	const Vector3 h = p_box.size * 0.5f;
	const Vector3 d = ((p_point - c).abs() - h).max(Vector3());
	return d.length();
}

/* CELL */

FellowWorldStreamer3D *FellowStreamingCell3D::_find_streamer() const {
	for (Node *p = get_parent(); p; p = p->get_parent()) {
		if (FellowWorldStreamer3D *s = Object::cast_to<FellowWorldStreamer3D>(p)) {
			return s;
		}
	}
	return nullptr;
}

// Milestone 52: what the Scene dock's warning icon says.
PackedStringArray FellowStreamingCell3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (!_find_streamer()) {
		warnings.push_back(RTR("A FellowStreamingCell3D streams only under a FellowWorldStreamer3D."));
	}
	if (scene_path.is_empty()) {
		warnings.push_back(RTR("Scene Path is empty: the cell has nothing to stream."));
	} else if (!ResourceLoader::exists(scene_path)) {
		warnings.push_back(vformat(RTR("Scene Path (%s) does not exist."), scene_path));
	}
	return warnings;
}

void FellowStreamingCell3D::set_scene_path(const String &p_path) {
	scene_path = p_path;
	update_configuration_warnings();
	if (is_inside_tree()) {
		_update_editor_preview();
	}
}

void FellowStreamingCell3D::_update_editor_preview() {
#ifdef TOOLS_ENABLED
	if (!Engine::get_singleton()->is_editor_hint() || !is_inside_tree()) {
		return;
	}
	bool show = false;
	const Node *edited = get_tree()->get_edited_scene_root();
	if (!scene_path.is_empty() && edited && (edited == this || edited->is_ancestor_of(this))) {
		for (Node *p = get_parent(); p; p = p->get_parent()) {
			const FellowWorldStreamer3D *s = Object::cast_to<FellowWorldStreamer3D>(p);
			if (s) {
				show = s->is_editor_preview();
				break;
			}
		}
	}
	const String shown = editor_preview ? String(editor_preview->get_meta(SNAME("_fellow_preview_path"), String())) : String();
	if (show && editor_preview && shown == scene_path) {
		return;
	}
	if (editor_preview) {
		remove_child(editor_preview);
		memdelete(editor_preview);
		editor_preview = nullptr;
	}
	if (!show) {
		return;
	}
	Ref<PackedScene> scene = ResourceLoader::load(scene_path, "PackedScene");
	if (scene.is_null()) {
		return;
	}
	Node *n = scene->instantiate(PackedScene::GEN_EDIT_STATE_DISABLED);
	if (!n) {
		return;
	}
	n->set_meta(SNAME("_fellow_preview_path"), scene_path);
	// Internal and owned by nobody in the edited scene: not in the Scene dock, not saved; a click on it selects the
	// cell.
	add_child(n, false, INTERNAL_MODE_BACK);
	editor_preview = n;
#endif
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
		case NOTIFICATION_PARENTED: {
			update_configuration_warnings();
		} break;
		case NOTIFICATION_ENTER_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				callable_mp(this, &FellowStreamingCell3D::_update_editor_preview).call_deferred();
				if (FellowWorldStreamer3D *s = _find_streamer()) {
					s->update_configuration_warnings(); // it has a cell now
				}
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
		case NOTIFICATION_PREDELETE: {
			// Before Node frees the children: content in the tree is one of them (freed with them); content
			// instantiated but never added is this cell's to free. In the destructor the pointer could already be
			// dangling (a double free at exit, found in milestone 50).
			if (content) {
				if (content->get_parent() == nullptr) {
					memdelete(content);
				}
				content = nullptr;
			}
		} break;
	}
}

FellowStreamingCell3D::~FellowStreamingCell3D() {
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

LocalVector<FellowWorldStreamer3D *> FellowWorldStreamer3D::in_tree;

static void _fellow_update_previews(Node *p_node) {
	for (int i = 0; i < p_node->get_child_count(false); i++) {
		Node *c = p_node->get_child(i, false);
		FellowStreamingCell3D *cell = Object::cast_to<FellowStreamingCell3D>(c);
		if (cell) {
			cell->_update_editor_preview();
		}
		_fellow_update_previews(c);
	}
}

void FellowWorldStreamer3D::set_editor_preview(bool p_enable) {
	editor_preview = p_enable;
	if (is_inside_tree()) {
		_fellow_update_previews(this);
	}
}

void FellowWorldStreamer3D::get_totals(int &r_cells, int &r_loaded, int &r_loading) {
	r_cells = 0;
	r_loaded = 0;
	r_loading = 0;
	for (const FellowWorldStreamer3D *streamer : in_tree) {
		for (const FellowStreamingCell3D *cell : streamer->cells) {
			r_cells++;
			if (cell->state == FellowStreamingCell3D::STATE_LOADED) {
				r_loaded++;
			} else if (cell->state != FellowStreamingCell3D::STATE_UNLOADED) {
				r_loading++;
			}
		}
	}
}

// Milestone 52: what the Scene dock's warning icon says.
PackedStringArray FellowWorldStreamer3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (find_children("*", "FellowStreamingCell3D", true, false).is_empty()) {
		warnings.push_back(RTR("No FellowStreamingCell3D under this node: nothing to stream. Project > Tools > Split into Streaming Cells (FellowEngine)... makes them from a level."));
	}
	if (!use_camera && targets.is_empty()) {
		warnings.push_back(RTR("Neither Use Camera nor Targets: no cell ever comes within range."));
	}
	if (is_inside_tree()) {
		for (int i = 0; i < targets.size(); i++) {
			const NodePath path = targets[i];
			if (!Object::cast_to<Node3D>(get_node_or_null(path))) {
				warnings.push_back(vformat(RTR("Target %d (%s) leads to no Node3D."), i, String(path)));
			}
		}
	}
	return warnings;
}

void FellowWorldStreamer3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_CHILD_ORDER_CHANGED: {
			update_configuration_warnings();
		} break;
		case NOTIFICATION_ENTER_TREE: {
			in_tree.push_back(this);
		} break;
		case NOTIFICATION_READY: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				set_process_internal(true);
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_update();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			in_tree.erase(this);
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
	ClassDB::bind_method(D_METHOD("set_editor_preview", "enable"), &FellowWorldStreamer3D::set_editor_preview);
	ClassDB::bind_method(D_METHOD("is_editor_preview"), &FellowWorldStreamer3D::is_editor_preview);
	ClassDB::bind_method(D_METHOD("set_use_camera", "enable"), &FellowWorldStreamer3D::set_use_camera);
	ClassDB::bind_method(D_METHOD("is_using_camera"), &FellowWorldStreamer3D::is_using_camera);
	ClassDB::bind_method(D_METHOD("set_targets", "targets"), &FellowWorldStreamer3D::set_targets);
	ClassDB::bind_method(D_METHOD("get_targets"), &FellowWorldStreamer3D::get_targets);
	ClassDB::bind_method(D_METHOD("prefetch", "position", "radius"), &FellowWorldStreamer3D::prefetch);
	ClassDB::bind_method(D_METHOD("clear_prefetch"), &FellowWorldStreamer3D::clear_prefetch);
	ClassDB::bind_method(D_METHOD("is_settled"), &FellowWorldStreamer3D::is_settled);
	ClassDB::bind_method(D_METHOD("load_now"), &FellowWorldStreamer3D::load_now);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowWorldStreamer3D::get_stats);
	ClassDB::bind_method(D_METHOD("split_into_cells", "root", "cell_size", "directory", "options"), &FellowWorldStreamer3D::split_into_cells, DEFVAL(Dictionary()));

	// Milestone 53: grouped in the inspector.
	ADD_GROUP("Range", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "streaming"), "set_streaming", "is_streaming");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_camera"), "set_use_camera", "is_using_camera");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "targets", PROPERTY_HINT_ARRAY_TYPE, "NodePath"), "set_targets", "get_targets");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "load_distance", PROPERTY_HINT_RANGE, "0,10000,0.1,or_greater,suffix:m"), "set_load_distance", "get_load_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "unload_margin", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:m"), "set_unload_margin", "get_unload_margin");
	ADD_GROUP("Budget", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_loading", PROPERTY_HINT_RANGE, "1,64,1"), "set_max_loading", "get_max_loading");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_activations_per_frame", PROPERTY_HINT_RANGE, "1,1024,1"), "set_max_activations_per_frame", "get_max_activations_per_frame");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_releases_per_frame", PROPERTY_HINT_RANGE, "1,1024,1"), "set_max_releases_per_frame", "get_max_releases_per_frame");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "threaded_instancing"), "set_threaded_instancing", "is_threaded_instancing");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "editor_preview"), "set_editor_preview", "is_editor_preview");
}

// The bounds of a node's visible descendants in p_space (the node's origin where it has none).
static AABB _fellow_split_bounds(Node3D *p_node, const Transform3D &p_space_inv) {
	AABB box;
	bool any = false;
	LocalVector<Node *> stack;
	stack.push_back(p_node);
	while (!stack.is_empty()) {
		Node *n = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		if (VisualInstance3D *vi = Object::cast_to<VisualInstance3D>(n)) {
			const AABB local = vi->get_aabb();
			if (local.has_volume() || local.has_surface()) {
				const AABB b = (p_space_inv * vi->get_global_transform()).xform(local);
				box = any ? box.merge(b) : b;
				any = true;
			}
		}
		for (int i = 0; i < n->get_child_count(); i++) {
			stack.push_back(n->get_child(i));
		}
	}
	if (!any) {
		box = AABB((p_space_inv * p_node->get_global_transform()).origin, Vector3());
	}
	return box;
}

// Hands the nodes the old scene owned (or none owned) to p_new, keeping instanced scenes' own nodes theirs.
static void _fellow_split_reown(Node *p_node, Node *p_old_owner, Node *p_new_owner) {
	if (p_node != p_new_owner && (p_node->get_owner() == p_old_owner || p_node->get_owner() == nullptr)) {
		p_node->set_owner(p_new_owner);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_fellow_split_reown(p_node->get_child(i), p_old_owner, p_new_owner);
	}
}

Array FellowWorldStreamer3D::split_into_cells(Node3D *p_root, real_t p_cell_size, const String &p_directory, const Dictionary &p_options) {
	Array made;
	ERR_FAIL_NULL_V(p_root, made);
	ERR_FAIL_COND_V_MSG(p_cell_size <= 0.0, made, "The cell size must be positive.");
	ERR_FAIL_COND_V_MSG(!is_inside_tree() || !p_root->is_inside_tree(), made, "The streamer and the root must be in the scene tree.");
	ERR_FAIL_COND_V_MSG(p_root == this || is_ancestor_of(p_root), made, "The root cannot be the streamer or under it.");

	PackedStringArray exclude;
	if (p_options.has("exclude_classes")) {
		exclude = p_options["exclude_classes"];
	} else {
		exclude = { "Camera3D", "DirectionalLight3D", "WorldEnvironment", "FellowWorldStreamer3D", "FellowStreamingCell3D" };
	}
	const real_t max_span = p_options.get("max_span", 2.0);
	const real_t cell_load_distance = p_options.get("load_distance", 0.0);
	const String extension = p_options.get("extension", "scn");
	Node *old_owner = p_root->get_owner() ? p_root->get_owner() : p_root;
	Node *cell_owner = Object::cast_to<Node>(p_options.get("owner", Variant()).get_validated_object());
	if (!cell_owner) {
		cell_owner = get_owner() ? get_owner() : (p_root->get_owner() ? p_root->get_owner() : nullptr);
	}

	const Error dir_err = DirAccess::make_dir_recursive_absolute(p_directory);
	ERR_FAIL_COND_V_MSG(dir_err != OK && dir_err != ERR_ALREADY_EXISTS, made, vformat("Cannot create the directory \"%s\".", p_directory));

	// The children by cell, in the streamer's space.
	const Transform3D streamer_global = get_global_transform();
	const Transform3D space_inv = streamer_global.affine_inverse();
	struct Group {
		LocalVector<Node3D *> nodes;
		AABB bounds;
	};
	HashMap<Vector2i, Group> groups;
	LocalVector<Vector2i> keys;
	LocalVector<Node *> children;
	for (int i = 0; i < p_root->get_child_count(); i++) {
		children.push_back(p_root->get_child(i));
	}
	for (Node *child : children) {
		Node3D *n = Object::cast_to<Node3D>(child);
		if (!n || n == this || n->is_ancestor_of(this)) {
			continue;
		}
		bool skip = false;
		for (const String &c : exclude) {
			if (n->is_class(c)) {
				skip = true;
				break;
			}
		}
		if (skip) {
			continue;
		}
		const AABB box = _fellow_split_bounds(n, space_inv);
		if (box.size.x > max_span * p_cell_size || box.size.z > max_span * p_cell_size) {
			continue; // Terrain and the like: larger than a few cells, it stays loaded.
		}
		const Vector3 c = box.get_center();
		const Vector2i key(int(Math::floor(c.x / p_cell_size)), int(Math::floor(c.z / p_cell_size)));
		Group *g = groups.getptr(key);
		if (!g) {
			g = &groups.insert(key, Group())->value;
			g->bounds = box;
			keys.push_back(key);
		} else {
			g->bounds = g->bounds.merge(box);
		}
		g->nodes.push_back(n);
	}

	keys.sort();
	for (const Vector2i &key : keys) {
		Group &g = groups[key];
		const String name = vformat("cell_%d_%d", key.x, key.y);
		const Vector3 centre((key.x + 0.5) * p_cell_size, 0.0, (key.y + 0.5) * p_cell_size);
		const Transform3D cell_global = streamer_global * Transform3D(Basis(), centre);
		const Transform3D cell_inv = cell_global.affine_inverse();

		// The cell's content: its nodes moved under a new root at the cell's origin, saved, then freed.
		Node3D *content = memnew(Node3D);
		content->set_name(name);
		for (Node3D *n : g.nodes) {
			const Transform3D global = n->get_global_transform();
			n->get_parent()->remove_child(n);
			content->add_child(n);
			n->set_transform(cell_inv * global);
			_fellow_split_reown(n, old_owner, content);
		}
		Ref<PackedScene> scene;
		scene.instantiate();
		const Error pack_err = scene->pack(content);
		const String path = p_directory.path_join(name + "." + extension);
		const Error save_err = pack_err == OK ? ResourceSaver::save(scene, path) : pack_err;
		memdelete(content);
		ERR_CONTINUE_MSG(save_err != OK, vformat("Cannot save the cell \"%s\" (error %d).", path, int(save_err)));

		FellowStreamingCell3D *cell = memnew(FellowStreamingCell3D);
		cell->set_name(vformat("Cell_%d_%d", key.x, key.y));
		cell->set_scene_path(path);
		cell->set_load_distance(cell_load_distance);
		AABB local_bounds = g.bounds;
		local_bounds.position -= centre;
		cell->set_bounds(local_bounds.grow(0.01));
		add_child(cell, true);
		cell->set_transform(Transform3D(Basis(), centre));
		if (cell_owner) {
			cell->set_owner(cell_owner);
		}
		made.push_back(cell);
	}
	return made;
}

FellowWorldStreamer3D::FellowWorldStreamer3D() {
}

FellowWorldStreamer3D::~FellowWorldStreamer3D() {
	_cancel_all();
	for (FellowStreamingCell3D *cell : cells) {
		cell->streamer = nullptr;
	}
}
